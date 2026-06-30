module nr.scene;
import :scene;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.utils;
import std;
import :bridge;
import :utils;
import :type;

namespace nr::scene::detail
{
void registerSceneComponents(flecs::world &world)
{
    world.component<LocalTransform>();
    world.component<WorldTransform>();
    world.component<LocalBounds>();
    world.component<WorldBounds>();
    world.component<RenderableBinding>();
    world.component<SceneSelectionBits>();
    world.component<ScenePartitionId>();
    world.component<TlasBucketId>();
    world.component<StaticObject>();
    world.component<DynamicObject>();
    world.component<SceneCameraBinding>();

    world.component<SceneTemplateRef>();
    world.component<SceneInstanceRef>();
    world.component<SceneTemplateNodeRef>();
    world.component<SceneTemplateNodeTransform>();
    world.component<SceneTemplateMeshBindingRef>();
    world.component<SceneTemplateCameraBindingRef>();
    world.component<SceneTemplateLightBindingRef>();
}

[[nodiscard]] flecs::world makeSceneWorld()
{
    auto world = flecs::world{ecs_init()};
    world.make_owner();
    return world;
}

struct MetallicRoughnessFactorSet
{
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
};

[[nodiscard]] float clamp01(float value) noexcept
{
    return glm::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] glm::vec3 clamp01(glm::vec3 value) noexcept
{
    return glm::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] float perceivedBrightness(glm::vec3 color) noexcept
{
    color = clamp01(color);
    return std::sqrt(0.299f * color.r * color.r +
                     0.587f * color.g * color.g +
                     0.114f * color.b * color.b);
}

[[nodiscard]] float solveMetallic(float diffuseBrightness,
                                  float specularBrightness,
                                  float oneMinusSpecularStrength) noexcept
{
    constexpr auto dielectricSpecular = 0.04f;
    auto a = dielectricSpecular;
    auto b = diffuseBrightness * oneMinusSpecularStrength / (1.0f - dielectricSpecular) +
             specularBrightness -
             2.0f * dielectricSpecular;
    auto c = dielectricSpecular - specularBrightness;
    auto discriminant = std::max((b * b) - (4.0f * a * c), 0.0f);
    return clamp01((-b + std::sqrt(discriminant)) / (2.0f * a));
}

[[nodiscard]] MetallicRoughnessFactorSet convertSpecularGlossinessToMetallicRoughness(
    const nr::load::MaterialAsset &material) noexcept
{
    constexpr auto dielectricSpecular = 0.04f;
    constexpr auto epsilon = 1.0e-6f;

    auto diffuse = clamp01(glm::vec3{
        material.baseColorFactor[0],
        material.baseColorFactor[1],
        material.baseColorFactor[2],
    });
    auto alpha = clamp01(material.baseColorFactor[3] * material.opacity);

    auto specular = glm::vec3{1.0f};
    if (material.specularFactor.has_value())
    {
        specular = clamp01(glm::vec3{
            (*material.specularFactor)[0],
            (*material.specularFactor)[1],
            (*material.specularFactor)[2],
        });
    }

    auto const glossiness = clamp01(material.glossinessFactor.value_or(1.0f));
    auto const specularStrength = std::max({specular.r, specular.g, specular.b});
    auto const oneMinusSpecularStrength = 1.0f - specularStrength;
    auto const metallic = solveMetallic(
        perceivedBrightness(diffuse),
        perceivedBrightness(specular),
        oneMinusSpecularStrength);

    auto const oneMinusMetallic = 1.0f - metallic;
    auto const baseColorFromDiffuse =
        diffuse *
        (oneMinusSpecularStrength /
         ((1.0f - dielectricSpecular) * std::max(oneMinusMetallic, epsilon)));

    auto const baseColorFromSpecular =
        (specular - glm::vec3{dielectricSpecular * oneMinusMetallic * oneMinusMetallic}) /
        std::max(1.0f - oneMinusMetallic * oneMinusMetallic, epsilon);

    auto const baseColor = clamp01(glm::mix(baseColorFromDiffuse, baseColorFromSpecular, metallic * metallic));
    return MetallicRoughnessFactorSet{
        .baseColorFactor = glm::vec4{baseColor, alpha},
        .metallicFactor = metallic,
        .roughnessFactor = clamp01(1.0f - glossiness),
    };
}

[[nodiscard]] bool hasSpecularGlossinessFactors(const nr::load::MaterialAsset &material) noexcept
{
    return material.specularFactor.has_value() || material.glossinessFactor.has_value();
}

[[nodiscard]] detail::MaterialGpuData buildMaterialGpuData(const nr::resource::Material &material)
{
    auto data = detail::MaterialGpuData{};
    data.featureFlags = static_cast<std::uint32_t>(material.featureFlags());
    data.alphaMode = static_cast<std::uint32_t>(material.core.alphaMode);
    data.textureSlotCount = static_cast<std::uint32_t>(material.textureSlots.size());

    data.baseColorFactor = material.core.baseColorFactor;
    data.emissiveAndMetallic = glm::vec4{material.core.emissiveFactor, material.core.metallicFactor};
    data.roughnessNormalOcclusionAlpha = glm::vec4{
        material.core.roughnessFactor,
        material.core.normalScale,
        material.core.occlusionStrength,
        material.core.alphaCutoff,
    };

    if (material.clearcoat.has_value())
    {
        data.clearcoatFactorRoughness = glm::vec4{
            material.clearcoat->factor,
            material.clearcoat->roughnessFactor,
            0.0f,
            0.0f,
        };
    }

    if (material.sheen.has_value())
    {
        data.sheenColorRoughness = glm::vec4{
            material.sheen->colorFactor,
            material.sheen->roughnessFactor,
        };
    }

    data.transmissionAnisotropy = glm::vec4{
        material.transmission.has_value() ? material.transmission->factor : 0.0f,
        material.anisotropy.has_value() ? material.anisotropy->factor : 0.0f,
        material.anisotropy.has_value() ? material.anisotropy->rotation : 0.0f,
        0.0f,
    };

    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        data.textureHandles[slotIndex] = material.textureSlots[slotIndex].texture.packed();
        data.uvSets[slotIndex] = material.textureSlots[slotIndex].uvSet;
    });

    return data;
}

[[nodiscard]] detail::CameraGpuData buildCameraGpuData(const nr::resource::CameraAsset &camera)
{
    return detail::CameraGpuData{
        .projection = static_cast<std::uint32_t>(camera.projection),
        .verticalFovRadians = camera.verticalFovRadians,
        .orthoHeight = camera.orthoHeight,
        .nearPlane = camera.nearPlane,
        .farPlane = camera.farPlane,
    };
}

[[nodiscard]] detail::LightGpuData buildLightGpuData(const nr::resource::LightAsset &light)
{
    return detail::LightGpuData{
        .type = static_cast<std::uint32_t>(light.type),
        .intensity = light.intensity,
        .color = light.color,
        .range = light.range,
        .innerConeRadians = light.innerConeRadians,
        .outerConeRadians = light.outerConeRadians,
        .castShadow = light.castShadow ? 1u : 0u,
    };
}
} // namespace nr::scene::detail

namespace nr::scene
{
Scene::Scene(const SceneCreateInfo &createInfo)
        : device_(createInfo.device),
          uploadBudgetBytesPerFrame_(createInfo.uploadBudgetBytesPerFrame),
          cpuRetention_(createInfo.cpuRetention)
{
        detail::registerSceneComponents(world_);

        runtimeRootQuery_ = world_.query_builder<const SceneInstanceRef>()
            .cached()
            .build();

        rasterCandidatesQuery_ = world_.query_builder<
                const RenderableBinding,
                const SceneSelectionBits,
                const ScenePartitionId,
                const TlasBucketId,
                const WorldTransform,
                const WorldBounds>()
            .cached()
            .without(EcsPrefab)
            .build();

        rtCandidatesQuery_ = world_.query_builder<
                const RenderableBinding,
                const SceneSelectionBits,
                const ScenePartitionId,
                const TlasBucketId,
                const WorldTransform,
                const WorldBounds>()
            .cached()
            .without(EcsPrefab)
            .build();

        cameraCandidatesQuery_ = world_.query_builder<
                const SceneCameraBinding,
                const WorldTransform>()
            .cached()
            .without(EcsPrefab)
            .build();
    }

[[nodiscard]] nr::rhi::Device &Scene::device() noexcept
{
        return device_;
    }

[[nodiscard]] const nr::rhi::Device &Scene::device() const noexcept
{
        return device_;
    }

[[nodiscard]] flecs::world &Scene::ecs() noexcept
{
        return world_;
    }

[[nodiscard]] const flecs::world &Scene::ecs() const noexcept
{
        return world_;
    }

[[nodiscard]] SceneTemplateHandle Scene::registerTemplate(const nr::load::SceneAsset &sceneAsset,
                                                       const SceneTemplateCreateInfo &createInfo)
{
        hasImportErrors_ = false;
        auto bridgePlan = SceneBridge::buildPlan(sceneAsset);
        hasImportErrors_ = !bridgePlan.valid();

        auto templateStableKey = createInfo.stableKey.empty() ? sceneAsset.sourcePath.generic_string() : createInfo.stableKey;
        if (templateStableKey.empty())
        {
            reportImport<nr::LogLevel::error>(
                ImportStage::templateRegistration,
                "Template stable key is empty. Provide SceneTemplateCreateInfo.stableKey or SceneAsset.sourcePath.");
        }

        if (hasImportErrors_)
        {
            return {};
        }

        if (auto existing = templatesByStableKey_.find(templateStableKey); existing != templatesByStableKey_.end())
        {
            if (templates_.tryGet(existing->second) != nullptr)
            {
                return existing->second;
            }
        }

        auto textureHandlesBySource = std::vector<nr::resource::TextureHandle>(sceneAsset.textures.size());
        auto materialHandlesBySource = std::vector<nr::resource::MaterialHandle>(sceneAsset.materials.size());
        auto meshHandlesBySource = std::vector<nr::resource::MeshHandle>(sceneAsset.meshes.size());
        auto cameraHandlesBySource = std::vector<nr::resource::CameraAssetHandle>(sceneAsset.cameras.size());
        auto lightHandlesBySource = std::vector<nr::resource::LightAssetHandle>(sceneAsset.lights.size());

        bridgeTextures(sceneAsset, bridgePlan, textureHandlesBySource);
        bridgeMaterials(sceneAsset, bridgePlan, textureHandlesBySource, materialHandlesBySource);
        bridgeMeshes(sceneAsset, bridgePlan, materialHandlesBySource, meshHandlesBySource);
        bridgeCameras(sceneAsset, bridgePlan, cameraHandlesBySource);
        bridgeLights(sceneAsset, bridgePlan, lightHandlesBySource);

        if (hasImportErrors_)
        {
            return {};
        }

        auto pinSet = buildTemplatePinSet(
            meshHandlesBySource,
            materialHandlesBySource,
            textureHandlesBySource,
            cameraHandlesBySource,
            lightHandlesBySource);

        auto const debugName = createInfo.debugName.empty() ? sceneAsset.sourcePath.stem().string() : createInfo.debugName;
        auto handle = templates_.emplace([&](SceneTemplateHandle newHandle) {
            auto prefabRoot = makeTemplatePrefab(newHandle, templateStableKey, debugName);
            prefabRoot.set(SceneTemplateRef{newHandle});

            return SceneTemplateRecord{
                .handle = newHandle,
                .stableKey = templateStableKey,
                .prefabRoot = prefabRoot,
                .prefabEntities = {prefabRoot.id()},
                .pins = std::move(pinSet),
                .hierarchyPolicy = createInfo.hierarchyPolicy,
            };
        });

        auto *templateRecord = templates_.tryGet(handle);
        if (templateRecord == nullptr)
        {
            reportImport<nr::LogLevel::error>(
                ImportStage::templateRegistration,
                "Failed to create SceneTemplateRecord from slot-map storage.",
                templateStableKey);
            return {};
        }

        auto hierarchyBuilt = buildTemplateHierarchy(
            handle,
            *templateRecord,
            sceneAsset,
            meshHandlesBySource,
            materialHandlesBySource,
            cameraHandlesBySource,
            lightHandlesBySource);

        if (!hierarchyBuilt || hasImportErrors_)
        {
            destroyTemplatePrefabEntities(*templateRecord);
            templates_.erase(handle);
            return {};
        }

        retainTemplatePins(templateRecord->pins);

        templatesByStableKey_.emplace(templateStableKey, handle);
        templateNodeCount_ += templateRecord->templateNodeCount;
        templateMeshBindingCount_ += templateRecord->templateMeshBindingCount;
        templateCameraBindingCount_ += templateRecord->templateCameraBindingCount;
        templateLightBindingCount_ += templateRecord->templateLightBindingCount;

        reportImport<nr::LogLevel::info>(
            ImportStage::templateRegistration,
            std::format("Registered template '{}' with {} node(s), {} mesh-binding entity(ies), {} camera-binding entity(ies) and {} light-binding entity(ies).",
                        templateStableKey,
                        templateRecord->templateNodeCount,
                        templateRecord->templateMeshBindingCount,
                        templateRecord->templateCameraBindingCount,
                        templateRecord->templateLightBindingCount),
            templateStableKey);

        return handle;
    }

[[nodiscard]] SceneInstanceHandle Scene::instantiate(SceneTemplateHandle templateHandle,
                                                  const SceneInstantiateInfo &createInfo)
{
        hasImportErrors_ = false;
        auto *templateRecord = templates_.tryGet(templateHandle);
        if (templateRecord == nullptr)
        {
            reportImport<nr::LogLevel::error>(
                ImportStage::instanceRegistration,
                "Cannot instantiate an unknown SceneTemplateHandle.");
            return {};
        }

        auto handle = instances_.emplace([&](SceneInstanceHandle newHandle) {
            auto root = makeInstanceEntity(newHandle, templateRecord->stableKey);
            root.add(EcsIsA, templateRecord->prefabRoot.id());

            if (createInfo.runtimeParent.has_value())
            {
                root.add(EcsChildOf, createInfo.runtimeParent->get().id());
            }

            root.set(SceneInstanceRef{
                .handle = newHandle,
                .templateHandle = templateHandle,
            });
            root.set(LocalTransform{.value = createInfo.rootTransform});
            root.set(WorldTransform{.value = createInfo.rootTransform});
            root.set(WorldBounds{});
            if (createInfo.activate)
            {
                root.add<DynamicObject>();
            }
            else
            {
                root.remove<DynamicObject>();
            }

            return SceneInstanceRecord{
                .handle = newHandle,
                .templateHandle = templateHandle,
                .root = root,
                .active = createInfo.activate,
                .expectedEntityCount = templateRecord->templateNodeCount +
                                       templateRecord->templateMeshBindingCount +
                                       templateRecord->templateCameraBindingCount +
                                       templateRecord->templateLightBindingCount +
                                       1u,
            };
        });

        ++templateRecord->liveInstanceCount;
        if (auto *instanceRecord = instances_.tryGet(handle); instanceRecord != nullptr)
        {
            initializeInstanceRuntimeState(*instanceRecord);
            updateInstanceHierarchy(*instanceRecord);
        }

        syncFallbackCameraInfrastructure();
        return handle;
    }

[[nodiscard]] DestroyTemplateResult Scene::destroyTemplate(SceneTemplateHandle templateHandle)
{
        auto *templateRecord = templates_.tryGet(templateHandle);
        if (templateRecord == nullptr)
        {
            return DestroyTemplateResult::notFound;
        }

        auto runtimeInstancesAlive = false;
        runtimeRootQuery_.each([&](flecs::entity, const SceneInstanceRef &instanceRef) {
            if (!runtimeInstancesAlive && instanceRef.templateHandle == templateHandle)
            {
                runtimeInstancesAlive = true;
            }
        });

        if (templateRecord->liveInstanceCount > 0u || runtimeInstancesAlive)
        {
            return DestroyTemplateResult::instancesAlive;
        }

        auto pinSet = std::move(templateRecord->pins);
        auto const stableKey = templateRecord->stableKey;
        auto const nodeCount = templateRecord->templateNodeCount;
        auto const meshBindingCount = templateRecord->templateMeshBindingCount;
        auto const cameraBindingCount = templateRecord->templateCameraBindingCount;
        auto const lightBindingCount = templateRecord->templateLightBindingCount;
        destroyTemplatePrefabEntities(*templateRecord);

        releaseTemplatePins(pinSet);
        collectTemplatePinnedAssets(pinSet);

        templatesByStableKey_.erase(stableKey);
        templates_.erase(templateHandle);

        subtractSaturating(templateNodeCount_, nodeCount);
        subtractSaturating(templateMeshBindingCount_, meshBindingCount);
        subtractSaturating(templateCameraBindingCount_, cameraBindingCount);
        subtractSaturating(templateLightBindingCount_, lightBindingCount);

        destroyFallbackCameraInfrastructureIfUnused();
        compactDeadAssetHandles();
        return DestroyTemplateResult::destroyed;
    }

void Scene::destroyInstance(SceneInstanceHandle instanceHandle)
{
        auto *instanceRecord = instances_.tryGet(instanceHandle);
        if (instanceRecord == nullptr)
        {
            return;
        }

        if (auto *templateRecord = templates_.tryGet(instanceRecord->templateHandle);
            templateRecord != nullptr && templateRecord->liveInstanceCount > 0u)
        {
            --templateRecord->liveInstanceCount;
        }

        if (instanceRecord->root.is_alive())
        {
            instanceRecord->root.destruct();
        }

        instances_.erase(instanceHandle);
        syncFallbackCameraInfrastructure();
    }

void Scene::updateSimulation(const SceneUpdateInput &input)
{
        (void)input;

        runtimeRootQuery_.each([&](flecs::entity entity, const SceneInstanceRef &instanceRef) {
            auto *instanceRecord = instances_.tryGet(instanceRef.handle);
            if (instanceRecord == nullptr || !instanceRecord->active)
            {
                return;
            }

            if (instanceRecord->root != entity)
            {
                return;
            }

            updateInstanceHierarchy(*instanceRecord);
        });

        syncFallbackCameraInfrastructure();
    }

void Scene::beginFrame(std::uint32_t frameSlot)
{
        currentFrame_.frameSlot = frameSlot;
        currentFrame_.frameSerial += 1u;
        uploadBytesThisFrame_ = 0;

        reapSubmittedGeometryAtlasGrowWork();
        reapRetiredGpuVersions();
        queueGpuUploadsForFrame();
    }

void Scene::uploadPending()
{
        if (!uploadContextAvailable())
        {
            return;
        }

        auto &uploadContext = device_.uploadReadback();
        reapSubmittedGeometryAtlasGrowWork();
        reapSubmittedAcquireWork();
        submitReadyGraphicsAcquireBatches(uploadContext);

        auto uploadBudgetRemaining = uploadBudgetBytesPerFrame_;

        uploadQueuedCameraAssets(uploadBudgetRemaining);
        uploadQueuedLightAssets(uploadBudgetRemaining);
        uploadQueuedMaterialAssets(uploadContext, uploadBudgetRemaining);
        uploadQueuedMeshAssets(uploadContext, uploadBudgetRemaining);
        uploadQueuedTextureAssets(uploadContext, uploadBudgetRemaining);
        pollUploadTimelineUntilAcquireBatchesReady(uploadContext);
    }

[[nodiscard]] SceneExtractProfileHandle Scene::registerExtractProfile(
        const SceneExtractProfileCreateInfo &createInfo)
{
        return extractProfiles_.emplace([&](SceneExtractProfileHandle newHandle) {
            auto debugName = createInfo.debugName;
            if (debugName.empty())
            {
                debugName = std::format("extract_profile_{}", newHandle.slot);
            }

            return SceneExtractProfileRecord{
                .handle = newHandle,
                .debugName = std::move(debugName),
                .domain = createInfo.domain,
                .selection = createInfo.selection,
                .requireReadyForDomain = createInfo.requireReadyForDomain,
                .requireActiveInstances = createInfo.requireActiveInstances,
                .enableCoarseGrouping = createInfo.enableCoarseGrouping,
            };
        });
    }

void Scene::destroyExtractProfile(SceneExtractProfileHandle profile)
{
        extractProfiles_.erase(profile);
    }

[[nodiscard]] ScenePacketSet Scene::extractPackets(
        SceneExtractProfileHandle profile,
        const SceneExtractInput &input) const
{
        auto const *profileRecord = extractProfiles_.tryGet(profile);
        if (profileRecord == nullptr)
        {
            return {};
        }

        return extractPacketsDedicated(*profileRecord, input);
    }

[[nodiscard]] std::optional<SceneResolvedCamera> Scene::tryGetPrimaryCamera(
        const std::optional<glm::uvec2> &viewportExtent) const
{
        struct ImportedCameraCandidate
        {
            flecs::entity entity{};
            nr::resource::CameraAssetHandle camera{};
            std::uint32_t sourceCameraIndex = std::numeric_limits<std::uint32_t>::max();
        };

        auto bestImported = std::optional<ImportedCameraCandidate>{};
        forEachImportedCameraInActiveInstances([&](flecs::entity entity, const SceneCameraBinding &cameraBinding) {
            auto sourceCameraIndex = std::numeric_limits<std::uint32_t>::max();
            if (auto sourceBinding = entity.try_get<SceneTemplateCameraBindingRef>(); sourceBinding != nullptr)
            {
                sourceCameraIndex = sourceBinding->sourceCameraIndex;
            }

            if (!bestImported.has_value() ||
                sourceCameraIndex < bestImported->sourceCameraIndex ||
                (sourceCameraIndex == bestImported->sourceCameraIndex && entity.id() < bestImported->entity.id()))
            {
                bestImported = ImportedCameraCandidate{
                    .entity = entity,
                    .camera = cameraBinding.camera,
                    .sourceCameraIndex = sourceCameraIndex,
                };
            }
        });

        if (bestImported.has_value())
        {
            return buildResolvedCamera(bestImported->entity, bestImported->camera, false, viewportExtent);
        }

        if (templates_.size() == 0 || !fallbackCameraHandle_.valid() || !fallbackCameraEntity_.is_alive())
        {
            return std::nullopt;
        }

        return buildResolvedCamera(fallbackCameraEntity_, fallbackCameraHandle_, true, viewportExtent);
    }

[[nodiscard]] SceneFrameStamp Scene::currentFrameStamp() const noexcept
{
        return currentFrame_;
    }

[[nodiscard]] SceneStatistics Scene::statistics() const noexcept
{
        return SceneStatistics{
            .templateCount = templates_.size(),
            .instanceCount = instances_.size(),
            .extractProfileCount = extractProfiles_.size(),
            .meshAssetCount = meshes_.size(),
            .materialAssetCount = materials_.size(),
            .textureAssetCount = textures_.size(),
            .cameraAssetCount = cameras_.size(),
            .lightAssetCount = lights_.size(),
            .templateNodeCount = templateNodeCount_,
            .templateMeshBindingCount = templateMeshBindingCount_,
            .templateCameraBindingCount = templateCameraBindingCount_,
            .templateLightBindingCount = templateLightBindingCount_,
        };
    }

[[nodiscard]] std::optional<std::reference_wrapper<const SceneTemplateRecord>> Scene::tryGetTemplate(SceneTemplateHandle handle) const noexcept
{
        return tryGetRecordRef(templates_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const SceneInstanceRecord>> Scene::tryGetInstance(SceneInstanceHandle handle) const noexcept
{
        return tryGetRecordRef(instances_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const MeshAssetRecord>> Scene::tryGetMeshAsset(nr::resource::MeshHandle handle) const noexcept
{
        return tryGetRecordRef(meshes_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const MaterialAssetRecord>> Scene::tryGetMaterialAsset(nr::resource::MaterialHandle handle) const noexcept
{
        return tryGetRecordRef(materials_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const TextureAssetRecord>> Scene::tryGetTextureAsset(nr::resource::TextureHandle handle) const noexcept
{
        return tryGetRecordRef(textures_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const CameraAssetRecord>> Scene::tryGetCameraAsset(nr::resource::CameraAssetHandle handle) const noexcept
{
        return tryGetRecordRef(cameras_, handle);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const LightAssetRecord>> Scene::tryGetLightAsset(nr::resource::LightAssetHandle handle) const noexcept
{
        return tryGetRecordRef(lights_, handle);
    }

[[nodiscard]] std::optional<SceneBridgeGeometryBuffers> Scene::tryGetRasterGeometryBuffers() const noexcept
{
        if (!geometryAtlas_.vertexBuffer.valid())
        {
            return std::nullopt;
        }

        auto buffers = SceneBridgeGeometryBuffers{
            .vertexBuffer = SceneBridgeBufferBinding{
                .buffer = std::cref(geometryAtlas_.vertexBuffer),
            },
        };

        if (geometryAtlas_.indexBuffer.valid())
        {
            buffers.indexBuffer = SceneBridgeBufferBinding{
                .buffer = std::cref(geometryAtlas_.indexBuffer),
            };
        }

        return buffers;
    }

[[nodiscard]] std::optional<SceneAccelerationStructureMeshSemanticKey> Scene::tryGetAccelerationStructureMeshSemanticKey(
        nr::resource::MeshHandle handle) const noexcept
{
        auto const *meshRecord = meshes_.tryGet(handle);
        if (meshRecord == nullptr ||
            !meshRecord->cpuReady ||
            meshRecord->gpuState != GpuResidencyState::resident ||
            !meshRecord->gpu.has_value() ||
            !geometryAtlas_.vertexBuffer.valid())
        {
            return std::nullopt;
        }

        auto const &atlas = meshRecord->gpu->atlas;
        if (atlas.vertexCount == 0u)
        {
            return std::nullopt;
        }

        auto semanticKey = SceneAccelerationStructureMeshSemanticKey{
            .meshGpuVersion = meshRecord->gpuVersion,
        };
        if (meshRecord->cpu.clockwiseFrontFace)
        {
            semanticKey.instanceFlags = semanticKey.instanceFlags | vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing;
        }

        auto const geometryCount = meshRecord->cpu.geometries.empty()
                                       ? std::size_t{1}
                                       : meshRecord->cpu.geometries.size();
        semanticKey.geometries.reserve(geometryCount);

        auto appendGeometryKey = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
            auto const indexed = atlas.indexCount > 0u;
            auto const primitiveElementCount = geometry.indexCount > 0u
                                                   ? geometry.indexCount
                                                   : (indexed ? atlas.indexCount : atlas.vertexCount);
            auto const primitiveCount = primitiveElementCount / 3u;
            if (primitiveCount == 0u)
            {
                return;
            }

            auto flags = vk::GeometryFlagsKHR{};
            auto const materialHandle = meshGeometryMaterial(handle, geometryIndex).value_or(geometry.material);
            if (materialHandle.valid())
            {
                auto const *materialRecord = materials_.tryGet(materialHandle);
                if (materialRecord == nullptr || !materialRecord->cpuReady || materialRecord->cpu.isOpaque())
                {
                    flags = flags | vk::GeometryFlagBitsKHR::eOpaque;
                }
                if (materialRecord != nullptr && materialRecord->cpuReady && materialRecord->cpu.core.doubleSided)
                {
                    semanticKey.instanceFlags =
                        semanticKey.instanceFlags | vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
                }
            }
            else
            {
                flags = flags | vk::GeometryFlagBitsKHR::eOpaque;
            }

            semanticKey.geometries.push_back(SceneAccelerationStructureGeometrySemanticKey{
                .geometryIndex = geometryIndex,
                .geometryFlags = flags,
            });
        };

        if (meshRecord->cpu.geometries.empty())
        {
            auto geometry = nr::resource::MeshGeometry{};
            geometry.indexCount = atlas.indexCount > 0u ? atlas.indexCount : atlas.vertexCount;
            appendGeometryKey(geometry, 0u);
        }
        else
        {
            auto const geometryIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
            std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) {
                appendGeometryKey(meshRecord->cpu.geometries[geometryIndex], geometryIndex);
            });
        }

        if (semanticKey.geometries.empty())
        {
            return std::nullopt;
        }

        return semanticKey;
    }

[[nodiscard]] std::optional<SceneAccelerationStructureMesh> Scene::tryGetAccelerationStructureMesh(
        nr::resource::MeshHandle handle) const noexcept
{
        auto semanticKey = tryGetAccelerationStructureMeshSemanticKey(handle);
        if (!semanticKey.has_value())
        {
            return std::nullopt;
        }

        auto const *meshRecord = meshes_.tryGet(handle);
        if (meshRecord == nullptr ||
            !meshRecord->cpuReady ||
            meshRecord->gpuState != GpuResidencyState::resident ||
            !meshRecord->gpu.has_value() ||
            !geometryAtlas_.vertexBuffer.valid())
        {
            return std::nullopt;
        }

        auto const &atlas = meshRecord->gpu->atlas;
        if (atlas.vertexCount == 0u)
        {
            return std::nullopt;
        }

        auto mesh = SceneAccelerationStructureMesh{
            .mesh = handle,
            .gpuVersion = meshRecord->gpuVersion,
            .instanceFlags = semanticKey->instanceFlags,
            .semanticKey = *semanticKey,
            .vertexBuffer = SceneBridgeBufferBinding{
                .buffer = std::cref(geometryAtlas_.vertexBuffer),
            },
            .vertexByteOffset = atlas.vertexByteOffset,
            .indexByteOffset = atlas.indexByteOffset,
            .maxVertex = atlas.vertexCount - 1u,
        };

        if (atlas.indexCount > 0u)
        {
            if (!geometryAtlas_.indexBuffer.valid())
            {
                return std::nullopt;
            }
            mesh.indexBuffer = SceneBridgeBufferBinding{
                .buffer = std::cref(geometryAtlas_.indexBuffer),
            };
        }

        auto const geometryCount = meshRecord->cpu.geometries.empty()
                                       ? std::size_t{1}
                                       : meshRecord->cpu.geometries.size();
        mesh.geometries.reserve(geometryCount);

        auto appendGeometry = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
            auto const indexed = atlas.indexCount > 0u;
            auto const primitiveElementCount = geometry.indexCount > 0u
                                                   ? geometry.indexCount
                                                   : (indexed ? atlas.indexCount : atlas.vertexCount);
            auto const primitiveCount = primitiveElementCount / 3u;
            if (primitiveCount == 0u)
            {
                return;
            }

            auto flags = vk::GeometryFlagsKHR{};
            auto const materialHandle = meshGeometryMaterial(handle, geometryIndex).value_or(geometry.material);
            if (materialHandle.valid())
            {
                auto const *materialRecord = materials_.tryGet(materialHandle);
                if (materialRecord == nullptr || !materialRecord->cpuReady || materialRecord->cpu.isOpaque())
                {
                    flags = flags | vk::GeometryFlagBitsKHR::eOpaque;
                }
            }
            else
            {
                flags = flags | vk::GeometryFlagBitsKHR::eOpaque;
            }

            mesh.geometries.push_back(SceneAccelerationStructureGeometry{
                .geometryIndex = geometryIndex,
                .indexed = indexed,
                .primitiveOffset = static_cast<vk::DeviceSize>(geometry.firstIndex) *
                                   (indexed ? sizeof(std::uint32_t) : sizeof(nr::resource::Vertex)),
                .firstVertex = indexed ? geometry.vertexOffset : 0u,
                .primitiveCount = primitiveCount,
                .geometryFlags = flags,
            });
        };

        if (meshRecord->cpu.geometries.empty())
        {
            auto geometry = nr::resource::MeshGeometry{};
            geometry.indexCount = atlas.indexCount > 0u ? atlas.indexCount : atlas.vertexCount;
            appendGeometry(geometry, 0u);
        }
        else
        {
            auto const geometryIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
            std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) {
                appendGeometry(meshRecord->cpu.geometries[geometryIndex], geometryIndex);
            });
        }

        if (mesh.geometries.empty())
        {
            return std::nullopt;
        }

        return mesh;
    }

[[nodiscard]] std::optional<nr::resource::MeshHandle> Scene::findMeshHandleByStableKey(std::string_view stableKey) const noexcept
{
        return findHandleByStableKey(meshes_, stableKey);
    }

[[nodiscard]] std::optional<nr::resource::MaterialHandle> Scene::findMaterialHandleByStableKey(std::string_view stableKey) const noexcept
{
        return findHandleByStableKey(materials_, stableKey);
    }

[[nodiscard]] std::optional<nr::resource::TextureHandle> Scene::findTextureHandleByStableKey(std::string_view stableKey) const noexcept
{
        return findHandleByStableKey(textures_, stableKey);
    }

[[nodiscard]] std::optional<nr::resource::CameraAssetHandle> Scene::findCameraHandleByStableKey(std::string_view stableKey) const noexcept
{
        return findHandleByStableKey(cameras_, stableKey);
    }

[[nodiscard]] std::optional<nr::resource::LightAssetHandle> Scene::findLightHandleByStableKey(std::string_view stableKey) const noexcept
{
        return findHandleByStableKey(lights_, stableKey);
    }

[[nodiscard]] bool Scene::matchesSelectionMask(std::uint64_t bits,
                                                   const SceneSelectionMask &mask) noexcept
{
        if ((bits & mask.requireAll) != mask.requireAll)
        {
            return false;
        }

        if (mask.requireAny != 0 && (bits & mask.requireAny) == 0)
        {
            return false;
        }

        if ((bits & mask.rejectAny) != 0)
        {
            return false;
        }

        return true;
    }

[[nodiscard]] bool Scene::intersectsFrustum(const nr::resource::Aabb &bounds,
                                                const SceneFrustum &frustum) noexcept
{
        if (!bounds.valid())
        {
            return true;
        }

        auto isOutsidePlane = [&](const glm::vec4 &plane) {
            auto normal = glm::vec3{plane.x, plane.y, plane.z};
            auto positive = glm::vec3{
                normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
                normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
                normal.z >= 0.0f ? bounds.max.z : bounds.min.z,
            };

            auto distance = glm::dot(normal, positive) + plane.w;
            return distance < 0.0f;
        };

        return std::ranges::none_of(frustum.planes, isOutsidePlane);
    }

[[nodiscard]] glm::mat4 Scene::buildViewMatrixFromWorld(const glm::mat4 &world) noexcept
{
        if (!detail::finiteMat4(world))
        {
            return glm::mat4{1.0f};
        }

        auto const determinant = glm::determinant(world);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-8f)
        {
            return glm::mat4{1.0f};
        }

        auto const view = glm::inverse(world);
        if (!detail::finiteMat4(view))
        {
            return glm::mat4{1.0f};
        }

        return view;
    }

[[nodiscard]] std::optional<float> Scene::aspectRatioFromViewportExtent(const std::optional<glm::uvec2> &viewportExtent) noexcept
{
        if (!viewportExtent.has_value())
        {
            return std::nullopt;
        }

        auto const extent = viewportExtent.value();
        if (extent.x == 0u || extent.y == 0u)
        {
            return std::nullopt;
        }

        return static_cast<float>(extent.x) / static_cast<float>(extent.y);
    }

[[nodiscard]] float Scene::resolveProjectionAspectRatio(const nr::resource::CameraAsset &camera,
                                                            const std::optional<glm::uvec2> &viewportExtent) noexcept
{
        constexpr auto kMinAspect = 1e-4f;
        constexpr auto kFallbackAspectRatio = 16.0f / 9.0f;

        if (auto viewportAspectRatio = aspectRatioFromViewportExtent(viewportExtent); viewportAspectRatio.has_value())
        {
            if (std::isfinite(*viewportAspectRatio) && *viewportAspectRatio > kMinAspect)
            {
                return *viewportAspectRatio;
            }
        }

        if (camera.authoredAspectRatio.has_value())
        {
            auto const authoredAspectRatio = *camera.authoredAspectRatio;
            if (std::isfinite(authoredAspectRatio) && authoredAspectRatio > kMinAspect)
            {
                return authoredAspectRatio;
            }
        }

        return kFallbackAspectRatio;
    }

[[nodiscard]] glm::mat4 Scene::buildProjectionMatrix(const nr::resource::CameraAsset &camera,
                                                         float aspectRatio) noexcept
{
        constexpr auto kFallbackAspectRatio = 16.0f / 9.0f;
        constexpr auto kMinAspectRatio = 1e-4f;
        constexpr auto kMinNearPlane = 1e-3f;
        constexpr auto kMinDepthRange = 1e-3f;

        if (!(std::isfinite(aspectRatio) && aspectRatio > kMinAspectRatio))
        {
            aspectRatio = kFallbackAspectRatio;
        }

        auto nearPlane = camera.nearPlane;
        if (!(std::isfinite(nearPlane) && nearPlane > kMinNearPlane))
        {
            nearPlane = 0.1f;
        }

        auto farPlane = camera.farPlane;
        if (!(std::isfinite(farPlane) && farPlane > nearPlane + kMinDepthRange))
        {
            farPlane = nearPlane + 1000.0f;
        }

        if (camera.perspective())
        {
            auto verticalFov = camera.verticalFovRadians;
            if (!(std::isfinite(verticalFov) && verticalFov > kMinDepthRange && verticalFov < (glm::pi<float>() - kMinDepthRange)))
            {
                verticalFov = glm::radians(60.0f);
            }

            return glm::perspectiveRH_ZO(verticalFov, aspectRatio, nearPlane, farPlane);
        }

        auto halfHeight = camera.orthoHeight;
        if (!(std::isfinite(halfHeight) && halfHeight > kMinDepthRange))
        {
            halfHeight = 10.0f;
        }

        auto const halfWidth = halfHeight * aspectRatio;
        return glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, nearPlane, farPlane);
    }

[[nodiscard]] SceneFrustum Scene::buildFrustumFromViewProjection(const glm::mat4 &viewProjection) noexcept
{
        auto frustum = SceneFrustum{};
        auto const transposed = glm::transpose(viewProjection);

        auto const rawPlanes = std::array{
            transposed[3] + transposed[0],
            transposed[3] - transposed[0],
            transposed[3] + transposed[1],
            transposed[3] - transposed[1],
            transposed[3] + transposed[2],
            transposed[3] - transposed[2],
        };

        auto const planeIndices = std::views::iota(std::size_t{0}, rawPlanes.size());
        std::ranges::for_each(planeIndices, [&](std::size_t planeIndex) {
            auto plane = rawPlanes[planeIndex];
            auto const normal = glm::vec3{plane.x, plane.y, plane.z};
            auto const normalLength = glm::length(normal);
            if (std::isfinite(normalLength) && normalLength > 1e-6f)
            {
                plane /= normalLength;
            }

            frustum.planes[planeIndex] = plane;
        });

        return frustum;
    }

[[nodiscard]] std::optional<SceneResolvedCamera> Scene::buildResolvedCamera(flecs::entity entity,
                                                                          nr::resource::CameraAssetHandle cameraHandle,
                                                                          bool fallback,
                                                                          const std::optional<glm::uvec2> &viewportExtent) const
{
        if (!entity.is_alive() || !cameraHandle.valid())
        {
            return std::nullopt;
        }

        auto const *cameraRecord = cameras_.tryGet(cameraHandle);
        if (cameraRecord == nullptr || !cameraRecord->cpuReady)
        {
            return std::nullopt;
        }

        auto world = glm::mat4{1.0f};
        if (auto worldTransform = entity.try_get<WorldTransform>(); worldTransform != nullptr && detail::finiteMat4(worldTransform->value))
        {
            world = worldTransform->value;
        }

        auto const view = buildViewMatrixFromWorld(world);
        auto const aspectRatio = resolveProjectionAspectRatio(cameraRecord->cpu, viewportExtent);
        auto const projection = buildProjectionMatrix(cameraRecord->cpu, aspectRatio);
        auto const frustum = buildFrustumFromViewProjection(projection * view);

        return SceneResolvedCamera{
            .entity = entity,
            .camera = cameraHandle,
            .world = world,
            .view = view,
            .projection = projection,
            .frustum = frustum,
            .fallback = fallback,
        };
    }

[[nodiscard]] std::optional<SceneFrustum> Scene::resolveExtractFrustum(const SceneExtractInput &input) const
{
        switch (input.visibility)
        {
        case SceneVisibilityMode::none:
            return std::nullopt;
        case SceneVisibilityMode::customFrustum:
            if (input.customFrustum.has_value())
            {
                return *input.customFrustum;
            }
            return std::nullopt;
        case SceneVisibilityMode::primaryCameraFrustum:
            if (auto primaryCamera = tryGetPrimaryCamera(input.viewportExtent); primaryCamera.has_value())
            {
                return primaryCamera->frustum;
            }
            return std::nullopt;
        default: return std::nullopt;
        }
    }

[[nodiscard]] const flecs::query<
        const RenderableBinding,
        const SceneSelectionBits,
        const ScenePartitionId,
        const TlasBucketId,
        const WorldTransform,
        const WorldBounds> &
    Scene::candidateQueryFor(ScenePacketDomain domain) const noexcept
{
        if (domain == ScenePacketDomain::rasterDraw)
        {
            return rasterCandidatesQuery_;
        }

        return rtCandidatesQuery_;
    }

[[nodiscard]] ScenePacketSet Scene::extractPacketsDedicated(
        const SceneExtractProfileRecord &profileRecord,
        const SceneExtractInput &input) const
{
        auto packetSet = ScenePacketSet{};
        packetSet.domain = profileRecord.domain;

        auto selectedFrustum = resolveExtractFrustum(input);
        auto appendCandidate = [&](flecs::entity entity,
                                   const RenderableBinding &binding,
                                   const SceneSelectionBits &selectionBits,
                                   const ScenePartitionId &partitionId,
                                   const TlasBucketId &tlasBucketId,
                                   const WorldTransform &worldTransform,
                                   const WorldBounds &worldBounds) {
            if (!matchesSelectionMask(selectionBits.value, profileRecord.selection))
            {
                return;
            }

            if (input.partitionOverride.has_value() && partitionId.value != *input.partitionOverride)
            {
                return;
            }

            if (profileRecord.requireActiveInstances && !belongsToActiveInstance(entity))
            {
                return;
            }

            if (profileRecord.requireReadyForDomain &&
                !renderableReadyForDomain(profileRecord.domain, binding))
            {
                return;
            }

            if (selectedFrustum.has_value() && !intersectsFrustum(worldBounds.value, *selectedFrustum))
            {
                return;
            }

            switch (profileRecord.domain)
            {
            case ScenePacketDomain::rasterDraw:
                appendPacketsForCandidate<ScenePacketDomain::rasterDraw>(
                    packetSet,
                    entity,
                    binding,
                    selectionBits.value,
                    tlasBucketId,
                    worldTransform,
                    worldBounds);
                break;
            case ScenePacketDomain::rayTracingInstance:
                appendPacketsForCandidate<ScenePacketDomain::rayTracingInstance>(
                    packetSet,
                    entity,
                    binding,
                    selectionBits.value,
                    tlasBucketId,
                    worldTransform,
                    worldBounds);
                break;
            case ScenePacketDomain::tlasBuildInput:
                appendPacketsForCandidate<ScenePacketDomain::tlasBuildInput>(
                    packetSet,
                    entity,
                    binding,
                    selectionBits.value,
                    tlasBucketId,
                    worldTransform,
                    worldBounds);
                break;
            default: break;
            }
        };

        auto const &query = candidateQueryFor(profileRecord.domain);
        query.each(appendCandidate);

        if (profileRecord.domain == ScenePacketDomain::rasterDraw)
        {
            std::ranges::sort(packetSet.rasterDraws, [](const RasterDrawPacket &lhs, const RasterDrawPacket &rhs) {
                return lhs.sortKey < rhs.sortKey;
            });
        }

        return packetSet;
    }

[[nodiscard]] bool Scene::belongsToActiveInstance(flecs::entity entity) const
{
        auto currentId = entity.id();
        while (currentId != 0)
        {
            auto current = flecs::entity{world_.c_ptr(), currentId};
            if (auto instanceRef = current.try_get<SceneInstanceRef>(); instanceRef != nullptr)
            {
                auto *instanceRecord = instances_.tryGet(instanceRef->handle);
                return instanceRecord != nullptr && instanceRecord->active;
            }

            currentId = ecs_get_parent(world_.c_ptr(), currentId);
        }

        return false;
    }

[[nodiscard]] bool Scene::meshIsResident(nr::resource::MeshHandle meshHandle) const noexcept
{
        auto const *meshRecord = meshes_.tryGet(meshHandle);
        if (meshRecord == nullptr)
        {
            return false;
        }

        return meshRecord->gpuState == GpuResidencyState::resident &&
               meshRecord->gpuVersion >= meshRecord->cpuVersion;
    }

[[nodiscard]] bool Scene::materialIsResident(nr::resource::MaterialHandle materialHandle) const noexcept
{
        auto const *materialRecord = materials_.tryGet(materialHandle);
        if (materialRecord == nullptr)
        {
            return false;
        }

        return materialRecord->gpuState == GpuResidencyState::resident &&
               materialRecord->gpuVersion >= materialRecord->cpuVersion;
    }

[[nodiscard]] bool Scene::textureIsResident(nr::resource::TextureHandle textureHandle) const noexcept
{
        auto const *textureRecord = textures_.tryGet(textureHandle);
        if (textureRecord == nullptr)
        {
            return false;
        }

        return textureRecord->gpuState == GpuResidencyState::resident &&
               textureRecord->gpuVersion >= textureRecord->cpuVersion;
    }

[[nodiscard]] bool Scene::materialTexturesReady(const nr::resource::Material &material) const noexcept
{
        return std::ranges::all_of(material.textureSlots, [&](const nr::resource::MaterialTextureSlot &slot) {
            auto textureHandle = slot.texture;
            if (!textureHandle.valid())
            {
                return true;
            }

            return textureIsResident(textureHandle);
        });
    }

[[nodiscard]] std::optional<nr::resource::MaterialHandle> Scene::meshGeometryMaterial(
        nr::resource::MeshHandle meshHandle,
        std::uint32_t geometryIndex) const noexcept
{
        auto const *meshRecord = meshes_.tryGet(meshHandle);
        if (meshRecord == nullptr || !meshRecord->cpuReady || geometryIndex >= meshRecord->cpu.geometries.size())
        {
            return std::nullopt;
        }

        auto materialHandle = meshRecord->cpu.geometries[geometryIndex].material;
        if (!materialHandle.valid())
        {
            return std::nullopt;
        }

        return materialHandle;
    }

[[nodiscard]] bool Scene::renderableReadyForRaster(const RenderableBinding &binding) const noexcept
{
        if (!meshIsResident(binding.mesh))
        {
            return false;
        }

        auto const *meshRecord = meshes_.tryGet(binding.mesh);
        if (meshRecord == nullptr || !meshRecord->cpuReady || meshRecord->cpu.geometries.empty())
        {
            return false;
        }

        return std::ranges::all_of(meshRecord->cpu.geometries, [&](const nr::resource::MeshGeometry &geometry) {
            auto materialHandle = geometry.material;
            if (!materialHandle.valid() || !materialIsResident(materialHandle))
            {
                return false;
            }

            auto const *materialRecord = materials_.tryGet(materialHandle);
            if (materialRecord == nullptr || !materialRecord->cpuReady)
            {
                return false;
            }

            return materialTexturesReady(materialRecord->cpu);
        });
    }

[[nodiscard]] bool Scene::renderableReadyForMeshOnlyDomain(const RenderableBinding &binding) const noexcept
{
        return meshIsResident(binding.mesh);
    }

[[nodiscard]] bool Scene::renderableReadyForDomain(ScenePacketDomain domain,
                                                const RenderableBinding &binding) const noexcept
{
        switch (domain)
        {
        case ScenePacketDomain::rasterDraw:
            return renderableReadyForRaster(binding);
        case ScenePacketDomain::rayTracingInstance:
        case ScenePacketDomain::tlasBuildInput:
            return renderableReadyForMeshOnlyDomain(binding);
        default:
            return false;
        }
    }

[[nodiscard]] std::uint64_t Scene::rasterSortKey(
        nr::resource::MeshHandle meshHandle,
        nr::resource::MaterialHandle materialHandle,
        std::uint64_t selectionBits,
        std::uint32_t geometryIndex,
        flecs::entity entity) const noexcept
{
        auto const pass = (selectionBits & sceneSelectionMask(SceneSelectionBit::rasterTransparent)) != 0 ? 1ull : 0ull;
        auto const pipelineFamily = (selectionBits & sceneSelectionMask(SceneSelectionBit::alphaTest)) != 0 ? 1ull : 0ull;

        auto const materialSlot = materialHandle.valid() ? static_cast<std::uint64_t>(materialHandle.slot) : ((1ull << 20u) - 1u);
        auto const meshSlot = meshHandle.valid() ? static_cast<std::uint64_t>(meshHandle.slot) : ((1ull << 18u) - 1u);
        auto const entityBits = static_cast<std::uint64_t>(entity.id()) & ((1ull << 10u) - 1u);

        return (pass << 63u) |
               (pipelineFamily << 62u) |
               ((materialSlot & ((1ull << 20u) - 1u)) << 42u) |
               ((meshSlot & ((1ull << 18u) - 1u)) << 24u) |
               ((static_cast<std::uint64_t>(geometryIndex) & ((1ull << 14u) - 1u)) << 10u) |
               entityBits;
    }

[[nodiscard]] std::uint32_t Scene::makeRayTracingInstanceMask(std::uint64_t selectionBits) noexcept
{
        auto const masked = static_cast<std::uint32_t>(selectionBits & 0xFFu);
        if (masked == 0u)
        {
            return 0xFFu;
        }

        return masked;
    }

[[nodiscard]] std::uint32_t Scene::resolveRenderableGeometryCount(const RenderableBinding &binding) const noexcept
{
        if (binding.geometryCount > 0)
        {
            return binding.geometryCount;
        }

        auto const resolved = meshGeometryCount(binding.mesh);
        if (resolved > 0)
        {
            return resolved;
        }

        return 1u;
    }

[[nodiscard]] bool Scene::uploadContextAvailable() const noexcept
{
        return *device_.device != nullptr && device_.uploadReadbackContext_.has_value();
    }

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan Scene::makeTransferToGraphicsOwnershipPlan(
        vk::PipelineStageFlags2 acquireStages,
        vk::AccessFlags2 acquireAccess) const
{
        auto const transferQueueFamily = device_.queueManager.transfer().queueFamilyIndex();
        auto const graphicsQueueFamily = device_.queueManager.graphics().queueFamilyIndex();
        nrAssert(transferQueueFamily != graphicsQueueFamily,
                 "Scene Phase4 upload requires distinct transfer and graphics queue families.");

        auto plan = nr::rhi::ops::BufferUploadOwnershipPlan{};
        plan.releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
            transferQueueFamily,
            graphicsQueueFamily,
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = acquireStages,
                .access = acquireAccess,
            });
        return plan;
    }

[[nodiscard]] std::size_t Scene::meshUploadBytes(const nr::resource::Mesh &mesh) noexcept
{
        auto vertexBytes = std::span{mesh.vertices}.size_bytes();
        auto indexBytes = std::span{mesh.indices}.size_bytes();
        return vertexBytes + indexBytes;
    }

[[nodiscard]] std::size_t Scene::textureUploadBytes(const nr::resource::Texture &texture) noexcept
{
        auto withPixels = std::ranges::find_if(texture.levels, [](const nr::resource::ImageLevel &level) {
            return !level.bytes.empty();
        });

        if (withPixels == texture.levels.end())
        {
            return 0;
        }

        return withPixels->bytes.size();
    }

[[nodiscard]] std::optional<std::reference_wrapper<const nr::resource::ImageLevel>>
    Scene::firstTextureLevelWithPixels(const nr::resource::Texture &texture)
{
        auto withPixels = std::ranges::find_if(texture.levels, [](const nr::resource::ImageLevel &level) {
            return !level.bytes.empty();
        });

        if (withPixels == texture.levels.end())
        {
            return std::nullopt;
        }

        return std::cref(*withPixels);
    }

[[nodiscard]] vk::DeviceSize Scene::alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
        if (alignment <= 1u)
        {
            return value;
        }

        auto const remainder = value % alignment;
        return remainder == 0u ? value : value + (alignment - remainder);
    }

[[nodiscard]] std::uint32_t Scene::checkedDeviceSizeToUint32(vk::DeviceSize value, std::string_view label)
{
        nrAssert(
            value <= std::numeric_limits<std::uint32_t>::max(),
            std::format("{} value {} exceeds uint32_t range.", label, value));
        return static_cast<std::uint32_t>(value);
    }

[[nodiscard]] std::vector<std::uint32_t> Scene::makeGeometryAtlasQueueFamilyIndices() const
{
        auto families = std::vector<std::uint32_t>{
            device_.queueManager.transfer().queueFamilyIndex(),
            device_.queueManager.graphics().queueFamilyIndex(),
            device_.queueManager.compute().queueFamilyIndex(),
        };
        std::ranges::sort(families);
        auto uniqueTail = std::ranges::unique(families);
        families.erase(uniqueTail.begin(), uniqueTail.end());
        return families;
    }

[[nodiscard]] vk::BufferCreateInfo Scene::makeGeometryAtlasBufferCreateInfo(
        vk::DeviceSize size,
        vk::BufferUsageFlags bindingUsage,
        std::span<const std::uint32_t> queueFamilyIndices) noexcept
{
        auto createInfo = vk::BufferCreateInfo{};
        createInfo.size = size;
        createInfo.usage = vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eTransferSrc |
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eShaderDeviceAddress |
                           vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                           bindingUsage;
        if (queueFamilyIndices.size() > 1u)
        {
            createInfo.sharingMode = vk::SharingMode::eConcurrent;
            createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size());
            createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
        }
        else
        {
            createInfo.sharingMode = vk::SharingMode::eExclusive;
        }
        return createInfo;
    }

[[nodiscard]] vk::DeviceSize Scene::grownGeometryAtlasCapacity(
        vk::DeviceSize currentCapacity,
        vk::DeviceSize requiredCapacity) noexcept
{
        constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
        auto const maxCapacity = std::numeric_limits<vk::DeviceSize>::max();
        auto const doubledCapacity = currentCapacity > (maxCapacity / 2u)
                                         ? maxCapacity
                                         : currentCapacity * 2u;
        return std::max({requiredCapacity, doubledCapacity, minCapacity});
    }

[[nodiscard]] detail::MeshGeometryAtlasAllocation Scene::reserveGeometryAtlasAllocation(
        const nr::resource::Mesh &mesh,
        nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        auto vertexBytes = std::as_bytes(std::span{mesh.vertices});
        auto indexBytes = std::as_bytes(std::span{mesh.indices});
        nrAssert(!vertexBytes.empty(), "Scene geometry atlas allocation requires vertex data.");

        constexpr auto vertexStride = vk::DeviceSize{sizeof(nr::resource::Vertex)};
        constexpr auto indexStride = vk::DeviceSize{sizeof(std::uint32_t)};

        auto const vertexByteOffset = alignUp(geometryAtlas_.vertexUsedBytes, vertexStride);
        auto const indexByteOffset = indexBytes.empty()
                                         ? vk::DeviceSize{0}
                                         : alignUp(geometryAtlas_.indexUsedBytes, indexStride);
        nrAssert(
            std::numeric_limits<vk::DeviceSize>::max() - vertexByteOffset >= vertexBytes.size_bytes(),
            "Scene geometry vertex atlas allocation overflowed.");
        nrAssert(
            indexBytes.empty() ||
                std::numeric_limits<vk::DeviceSize>::max() - indexByteOffset >= indexBytes.size_bytes(),
            "Scene geometry index atlas allocation overflowed.");

        auto const requiredVertexBytes = vertexByteOffset + vertexBytes.size_bytes();
        auto const requiredIndexBytes = indexBytes.empty()
                                            ? geometryAtlas_.indexUsedBytes
                                            : indexByteOffset + indexBytes.size_bytes();
        ensureGeometryAtlasCapacity(requiredVertexBytes, requiredIndexBytes, uploadContext);

        geometryAtlas_.vertexUsedBytes = requiredVertexBytes;
        geometryAtlas_.indexUsedBytes = requiredIndexBytes;

        auto allocation = detail::MeshGeometryAtlasAllocation{
            .vertexByteOffset = vertexByteOffset,
            .indexByteOffset = indexByteOffset,
            .vertexBase = checkedDeviceSizeToUint32(vertexByteOffset / vertexStride, "Scene geometry atlas vertex base"),
            .indexBase = checkedDeviceSizeToUint32(indexByteOffset / indexStride, "Scene geometry atlas index base"),
            .vertexCount = checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.vertices.size()), "Scene mesh vertex count"),
            .indexCount = checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.indices.size()), "Scene mesh index count"),
        };
        return allocation;
    }

void Scene::ensureGeometryAtlasCapacity(
        vk::DeviceSize requiredVertexBytes,
        vk::DeviceSize requiredIndexBytes,
        nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        auto const growVertex = requiredVertexBytes > geometryAtlas_.vertexCapacityBytes;
        auto const growIndex = requiredIndexBytes > geometryAtlas_.indexCapacityBytes;
        if (!growVertex && !growIndex)
        {
            return;
        }

        auto const copyExistingVertexPrefix =
            growVertex && geometryAtlas_.vertexBuffer.valid() && geometryAtlas_.vertexUsedBytes > 0u;
        auto const copyExistingIndexPrefix =
            growIndex && geometryAtlas_.indexBuffer.valid() && geometryAtlas_.indexUsedBytes > 0u;
        if ((copyExistingVertexPrefix || copyExistingIndexPrefix) && !pendingAcquireBatches_.empty())
        {
            pollUploadTimelineUntilAcquireBatchesReady(uploadContext);
        }

        auto oldVertexBuffer = std::optional<nr::rhi::Buffer>{};
        auto oldIndexBuffer = std::optional<nr::rhi::Buffer>{};
        auto oldVertexUsedBytes = vk::DeviceSize{0};
        auto oldIndexUsedBytes = vk::DeviceSize{0};

        if (growVertex)
        {
            oldVertexUsedBytes = geometryAtlas_.vertexUsedBytes;
            if (geometryAtlas_.vertexBuffer.valid())
            {
                oldVertexBuffer.emplace(std::move(geometryAtlas_.vertexBuffer));
            }

            geometryAtlas_.vertexCapacityBytes = grownGeometryAtlasCapacity(
                geometryAtlas_.vertexCapacityBytes,
                requiredVertexBytes);
            auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
            geometryAtlas_.vertexBuffer = device_.resourceFactory.createBuffer(
                makeGeometryAtlasBufferCreateInfo(
                    geometryAtlas_.vertexCapacityBytes,
                    vk::BufferUsageFlagBits::eVertexBuffer,
                    std::span<const std::uint32_t>{atlasQueueFamilies}),
                nr::rhi::MemoryUsage::GpuOnly,
                "scene_geometry_vertex_atlas");
            nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene failed to create vertex geometry atlas buffer.");
        }

        if (growIndex && requiredIndexBytes > 0u)
        {
            oldIndexUsedBytes = geometryAtlas_.indexUsedBytes;
            if (geometryAtlas_.indexBuffer.valid())
            {
                oldIndexBuffer.emplace(std::move(geometryAtlas_.indexBuffer));
            }

            geometryAtlas_.indexCapacityBytes = grownGeometryAtlasCapacity(
                geometryAtlas_.indexCapacityBytes,
                requiredIndexBytes);
            auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
            geometryAtlas_.indexBuffer = device_.resourceFactory.createBuffer(
                makeGeometryAtlasBufferCreateInfo(
                    geometryAtlas_.indexCapacityBytes,
                    vk::BufferUsageFlagBits::eIndexBuffer,
                    std::span<const std::uint32_t>{atlasQueueFamilies}),
                nr::rhi::MemoryUsage::GpuOnly,
                "scene_geometry_index_atlas");
            nrAssert(geometryAtlas_.indexBuffer.valid(), "Scene failed to create index geometry atlas buffer.");
        }

        submitGeometryAtlasGrowCopy(
            std::move(oldVertexBuffer),
            oldVertexUsedBytes,
            std::move(oldIndexBuffer),
            oldIndexUsedBytes);
    }

void Scene::submitGeometryAtlasGrowCopy(
        std::optional<nr::rhi::Buffer> oldVertexBuffer,
        vk::DeviceSize oldVertexUsedBytes,
        std::optional<nr::rhi::Buffer> oldIndexBuffer,
        vk::DeviceSize oldIndexUsedBytes)
{
        auto retiredBuffers = detail::RetiredSceneGeometryAtlasBuffers{};
        if (oldVertexBuffer.has_value())
        {
            retiredBuffers.vertexBuffer = std::move(*oldVertexBuffer);
        }
        if (oldIndexBuffer.has_value())
        {
            retiredBuffers.indexBuffer = std::move(*oldIndexBuffer);
        }

        auto const copyVertexPrefix =
            retiredBuffers.vertexBuffer.valid() &&
            geometryAtlas_.vertexBuffer.valid() &&
            oldVertexUsedBytes > 0u;
        auto const copyIndexPrefix =
            retiredBuffers.indexBuffer.valid() &&
            geometryAtlas_.indexBuffer.valid() &&
            oldIndexUsedBytes > 0u;

        if (!copyVertexPrefix && !copyIndexPrefix)
        {
            retireGeometryAtlasBuffers(std::move(retiredBuffers));
            return;
        }

        auto growWork = SubmittedGeometryAtlasGrowWork{};
        growWork.retiredBuffers = std::move(retiredBuffers);
        growWork.commandPool = nr::rhi::CommandPool{
            device_.device,
            device_.queueManager.graphics().queueFamilyIndex(),
            vk::CommandPoolCreateFlagBits::eTransient,
        };
        growWork.commandBuffers.emplace(growWork.commandPool.allocatePrimary(1));
        auto &commandBuffer = growWork.commandBuffers->front();

        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};

            if (copyVertexPrefix)
            {
                auto copyRegion = vk::BufferCopy{};
                copyRegion.size = oldVertexUsedBytes;
                nr::rhi::ops::copyBuffer2(
                    commandBuffer,
                    growWork.retiredBuffers.vertexBuffer.handle(),
                    geometryAtlas_.vertexBuffer.handle(),
                    nr::rhi::ops::toBufferCopy2(copyRegion));
                postCopyBarriers.addBuffer(geometryAtlas_.vertexBuffer, vk::BufferMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    vk::Buffer{},
                    0,
                    oldVertexUsedBytes,
                    nullptr,
                });
            }

            if (copyIndexPrefix)
            {
                auto copyRegion = vk::BufferCopy{};
                copyRegion.size = oldIndexUsedBytes;
                nr::rhi::ops::copyBuffer2(
                    commandBuffer,
                    growWork.retiredBuffers.indexBuffer.handle(),
                    geometryAtlas_.indexBuffer.handle(),
                    nr::rhi::ops::toBufferCopy2(copyRegion));
                postCopyBarriers.addBuffer(geometryAtlas_.indexBuffer, vk::BufferMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eAllCommands,
                    vk::AccessFlagBits2::eMemoryRead,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    vk::Buffer{},
                    0,
                    oldIndexUsedBytes,
                    nullptr,
                });
            }

            nr::rhi::ops::pipelineBarrier(commandBuffer, postCopyBarriers);
        }
        nr::rhi::CommandRecorder::end(commandBuffer);

        auto growSubmission = nr::rhi::CommandBatch{};
        growSubmission.addCommandBuffer(commandBuffer);

        growWork.fence = vk::raii::Fence(device_.device, vk::FenceCreateInfo{});
        device_.queueManager.graphics().submit(std::move(growSubmission), std::cref(growWork.fence));
        submittedGeometryAtlasGrowWork_.push_back(std::move(growWork));
    }

void Scene::retireGeometryAtlasBuffers(detail::RetiredSceneGeometryAtlasBuffers retiredBuffers)
{
        if (!retiredBuffers.vertexBuffer.valid() && !retiredBuffers.indexBuffer.valid())
        {
            return;
        }

        retiredBuffers.retireAfterSerial = currentFrame_.frameSerial + detail::kDefaultRetireLatencySerial;
        retiredGeometryAtlasBuffers_.push_back(std::move(retiredBuffers));
    }

void Scene::reapSubmittedGeometryAtlasGrowWork()
{
        std::erase_if(submittedGeometryAtlasGrowWork_, [&](SubmittedGeometryAtlasGrowWork &work) {
            nrAssert(*work.fence != nullptr, "Scene geometry atlas grow work requires a valid fence.");
            auto result = device_.device.waitForFences(*work.fence, vk::True, 0u);
            nrAssert(
                result == vk::Result::eSuccess || result == vk::Result::eTimeout,
                "Scene failed querying geometry atlas grow fence status.");
            if (result != vk::Result::eSuccess)
            {
                return false;
            }

            retireGeometryAtlasBuffers(std::move(work.retiredBuffers));
            return true;
        });
    }

void Scene::queueGpuUploadsForFrame()
{
        queueUploadsFor(std::span{meshHandles_}, meshes_);
        queueUploadsFor(std::span{materialHandles_}, materials_);
        queueUploadsFor(std::span{textureHandles_}, textures_);
        queueUploadsFor(std::span{cameraHandles_}, cameras_);
        queueUploadsFor(std::span{lightHandles_}, lights_);
    }

void Scene::reapRetiredGpuVersions()
{
        auto const serial = currentFrame_.frameSerial;
        auto expired = [serial](const auto &retiredVersion) {
            return retiredVersion.retireAfterSerial <= serial;
        };

        reapRetiredFor(std::span{meshHandles_}, meshes_, expired);
        reapRetiredFor(std::span{materialHandles_}, materials_, expired);
        reapRetiredFor(std::span{textureHandles_}, textures_, expired);
        reapRetiredFor(std::span{cameraHandles_}, cameras_, expired);
        reapRetiredFor(std::span{lightHandles_}, lights_, expired);

        std::erase_if(retiredMeshPayloadGraveyard_, expired);
        std::erase_if(retiredMaterialPayloadGraveyard_, expired);
        std::erase_if(retiredTexturePayloadGraveyard_, expired);
        std::erase_if(retiredCameraPayloadGraveyard_, expired);
        std::erase_if(retiredLightPayloadGraveyard_, expired);
        std::erase_if(retiredGeometryAtlasBuffers_, expired);
    }

[[nodiscard]] std::size_t Scene::pixelFormatByteSize(nr::resource::PixelFormat format)
{
        switch (format)
        {
        case vk::Format::eR8Unorm: return 1;
        case vk::Format::eR8G8Unorm: return 2;
        case vk::Format::eR8G8B8A8Unorm:
        case vk::Format::eR8G8B8A8Srgb:
        case vk::Format::eB8G8R8A8Unorm:
        case vk::Format::eB8G8R8A8Srgb: return 4;
        case vk::Format::eR16G16B16A16Sfloat: return 8;
        case vk::Format::eR32G32B32A32Sfloat: return 16;
        default: break;
        }

        nrAssert(false, std::format("Unsupported vk::Format '{}' for Scene texture byte-size calculation.", static_cast<std::uint32_t>(format)));
        return 4;
    }

[[nodiscard]] std::vector<std::byte> Scene::makeFallbackTextureBytes(nr::resource::PixelFormat format)
{
        auto const byteSize = pixelFormatByteSize(format);
        return std::vector<std::byte>(byteSize, std::byte{0xFF});
    }

[[nodiscard]] bool Scene::uploadMaterialAsset(nr::resource::MaterialHandle handle,
                                           MaterialAssetRecord &record,
                                           nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        auto materialGpuData = detail::buildMaterialGpuData(record.cpu);
        auto materialBytes = std::as_bytes(std::span{std::addressof(materialGpuData), std::size_t{1}});

        auto createInfo = vk::BufferCreateInfo{};
        createInfo.size = materialBytes.size();
        createInfo.usage = vk::BufferUsageFlagBits::eTransferDst |
                           vk::BufferUsageFlagBits::eTransferSrc |
                           vk::BufferUsageFlagBits::eStorageBuffer |
                           vk::BufferUsageFlagBits::eUniformBuffer;
        createInfo.sharingMode = vk::SharingMode::eExclusive;

        auto payload = detail::MaterialGpuPayload{};
        payload.buffer = device_.resourceFactory.createBuffer(
            createInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            std::format("scene_material_{}_gpu", handle.slot));
        payload.byteSize = materialBytes.size();

        auto ownership = makeTransferToGraphicsOwnershipPlan(
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eMemoryRead);
        auto uploadTicket = uploadContext.uploadBuffer(materialBytes, payload.buffer, 0, ownership);
        if (!uploadTicket.valid())
        {
            return false;
        }

        queueRetiredPayload(record.gpu, record.retiredGpu);
        record.gpu = std::move(payload);
        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::waitingAcquire;
        record.lastUploadFrameSerial = currentFrame_.frameSerial;

        // uploadTicket stores a Buffer reference; rebind to the record-owned payload after move.
        uploadTicket.buffer = std::cref(record.gpu->buffer);

        auto pendingBatch = PendingAcquireBatch{};
        pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::material);
        pendingBatch.targetGpuVersion = record.cpuVersion;
        pendingBatch.bufferTickets.push_back(uploadTicket);
        pendingAcquireBatches_.push_back(std::move(pendingBatch));
        return true;
    }

[[nodiscard]] bool Scene::uploadMeshAsset(nr::resource::MeshHandle handle,
                                       MeshAssetRecord &record,
                                       nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        auto vertexBytes = std::as_bytes(std::span{record.cpu.vertices});
        auto indexBytes = std::as_bytes(std::span{record.cpu.indices});
        if (vertexBytes.empty())
        {
            record.uploadQueued = false;
            record.gpuState = GpuResidencyState::none;
            return false;
        }

        auto payload = detail::MeshGpuPayload{};
        payload.atlas = reserveGeometryAtlasAllocation(record.cpu, uploadContext);
        nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene mesh upload requires a valid vertex geometry atlas.");

        auto pendingBatch = PendingAcquireBatch{};
        pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::mesh);
        pendingBatch.targetGpuVersion = record.cpuVersion;

        auto vertexTicket = uploadContext.uploadBuffer(
            vertexBytes,
            geometryAtlas_.vertexBuffer,
            payload.atlas.vertexByteOffset);
        if (!vertexTicket.valid())
        {
            return false;
        }

        auto indexTicket = std::optional<nr::rhi::ops::BufferUploadTicket>{};
        if (!indexBytes.empty())
        {
            nrAssert(geometryAtlas_.indexBuffer.valid(), "Scene indexed mesh upload requires a valid index geometry atlas.");
            auto uploadedIndexTicket = uploadContext.uploadBuffer(
                indexBytes,
                geometryAtlas_.indexBuffer,
                payload.atlas.indexByteOffset);
            if (!uploadedIndexTicket.valid())
            {
                return false;
            }

            indexTicket = std::move(uploadedIndexTicket);
        }

        queueRetiredPayload(record.gpu, record.retiredGpu);
        record.gpu = std::move(payload);
        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::waitingAcquire;
        record.lastUploadFrameSerial = currentFrame_.frameSerial;

        // Tickets store Buffer references; keep them bound to the scene-owned atlas buffers.
        vertexTicket.buffer = std::cref(geometryAtlas_.vertexBuffer);
        pendingBatch.bufferTickets.push_back(vertexTicket);

        if (indexTicket.has_value() && geometryAtlas_.indexBuffer.valid())
        {
            indexTicket->buffer = std::cref(geometryAtlas_.indexBuffer);
            pendingBatch.bufferTickets.push_back(*indexTicket);
        }

        pendingAcquireBatches_.push_back(std::move(pendingBatch));
        return true;
    }

[[nodiscard]] bool Scene::uploadTextureAsset(nr::resource::TextureHandle handle,
                                          TextureAssetRecord &record,
                                          nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        auto fallbackBytes = std::vector<std::byte>{};
        auto sourceLevel = firstTextureLevelWithPixels(record.cpu);

        auto levelExtent = vk::Extent3D{
            std::max(record.cpu.width, 1u),
            std::max(record.cpu.height, 1u),
            std::max(record.cpu.depth, 1u),
        };

        auto imageData = std::span<const std::byte>{};
        if (sourceLevel.has_value())
        {
            auto const &level = sourceLevel->get();
            levelExtent = vk::Extent3D{
                std::max(level.width, 1u),
                std::max(level.height, 1u),
                std::max(level.depth, 1u),
            };
            imageData = std::span<const std::byte>{level.bytes};
        }
        else
        {
            fallbackBytes = makeFallbackTextureBytes(record.cpu.format);
            imageData = std::span<const std::byte>{fallbackBytes};
        }

        auto imageCreateInfo = vk::ImageCreateInfo{};
        imageCreateInfo.imageType = record.cpu.dimension;
        imageCreateInfo.format = record.cpu.format;
        imageCreateInfo.extent = levelExtent;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
        imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
        imageCreateInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                                vk::ImageUsageFlagBits::eTransferSrc |
                                vk::ImageUsageFlagBits::eSampled;
        imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
        imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;

        auto payload = detail::TextureGpuPayload{};
        payload.image = device_.resourceFactory.createImage(
            imageCreateInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            std::format("scene_texture_{}_gpu", handle.slot));
        payload.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

        auto ownership = makeTransferToGraphicsOwnershipPlan(
            vk::PipelineStageFlagBits2::eAllCommands,
            vk::AccessFlagBits2::eShaderRead);
        auto uploadTicket = uploadContext.uploadImage(
            imageData,
            payload.image,
            vk::ImageLayout::eUndefined,
            payload.layout,
            ownership);
        if (!uploadTicket.valid())
        {
            return false;
        }

        queueRetiredPayload(record.gpu, record.retiredGpu);
        record.gpu = std::move(payload);
        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::waitingAcquire;
        record.lastUploadFrameSerial = currentFrame_.frameSerial;

        // uploadTicket stores an Image reference; rebind to the record-owned payload after move.
        uploadTicket.image = std::cref(record.gpu->image);

        auto pendingBatch = PendingAcquireBatch{};
        pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::texture);
        pendingBatch.targetGpuVersion = record.cpuVersion;
        pendingBatch.imageTickets.push_back(uploadTicket);
        pendingAcquireBatches_.push_back(std::move(pendingBatch));
        return true;
    }

[[nodiscard]] bool Scene::uploadCameraAsset(nr::resource::CameraAssetHandle handle,
                                         CameraAssetRecord &record)
{
        if (!record.gpu.has_value() || !record.gpu->buffer.valid())
        {
            queueRetiredPayload(record.gpu, record.retiredGpu);

            auto createInfo = vk::BufferCreateInfo{};
            createInfo.size = sizeof(detail::CameraGpuData);
            createInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer |
                               vk::BufferUsageFlagBits::eStorageBuffer;
            createInfo.sharingMode = vk::SharingMode::eExclusive;

            auto payload = detail::CameraGpuPayload{};
            payload.buffer = device_.resourceFactory.createBuffer(
                createInfo,
                nr::rhi::MemoryUsage::CpuToGpu,
                std::format("scene_camera_{}_cpu", handle.slot));
            record.gpu = std::move(payload);
        }

        auto cameraGpuData = detail::buildCameraGpuData(record.cpu);
        record.gpu->buffer.writeMappedAndFlush(cameraGpuData);

        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::resident;
        record.gpuVersion = record.cpuVersion;
        record.lastUploadFrameSerial = currentFrame_.frameSerial;
        return true;
    }

[[nodiscard]] bool Scene::uploadLightAsset(nr::resource::LightAssetHandle handle,
                                        LightAssetRecord &record)
{
        if (!record.gpu.has_value() || !record.gpu->buffer.valid())
        {
            queueRetiredPayload(record.gpu, record.retiredGpu);

            auto createInfo = vk::BufferCreateInfo{};
            createInfo.size = sizeof(detail::LightGpuData);
            createInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer |
                               vk::BufferUsageFlagBits::eStorageBuffer;
            createInfo.sharingMode = vk::SharingMode::eExclusive;

            auto payload = detail::LightGpuPayload{};
            payload.buffer = device_.resourceFactory.createBuffer(
                createInfo,
                nr::rhi::MemoryUsage::CpuToGpu,
                std::format("scene_light_{}_cpu", handle.slot));
            record.gpu = std::move(payload);
        }

        auto lightGpuData = detail::buildLightGpuData(record.cpu);
        record.gpu->buffer.writeMappedAndFlush(lightGpuData);

        record.uploadQueued = false;
        record.gpuState = GpuResidencyState::resident;
        record.gpuVersion = record.cpuVersion;
        record.lastUploadFrameSerial = currentFrame_.frameSerial;
        return true;
    }

void Scene::uploadQueuedCameraAssets(std::size_t &uploadBudgetRemaining)
{
        (void)uploadBudgetRemaining;
        forEachLiveRecord(std::span{cameraHandles_}, cameras_, [&](nr::resource::CameraAssetHandle handle, CameraAssetRecord &record) {
            if (!record.uploadQueued)
            {
                return;
            }

            if (uploadCameraAsset(handle, record))
            {
                uploadBytesThisFrame_ += sizeof(detail::CameraGpuData);
            }
        });
    }

void Scene::uploadQueuedLightAssets(std::size_t &uploadBudgetRemaining)
{
        (void)uploadBudgetRemaining;
        forEachLiveRecord(std::span{lightHandles_}, lights_, [&](nr::resource::LightAssetHandle handle, LightAssetRecord &record) {
            if (!record.uploadQueued)
            {
                return;
            }

            if (uploadLightAsset(handle, record))
            {
                uploadBytesThisFrame_ += sizeof(detail::LightGpuData);
            }
        });
    }

void Scene::uploadQueuedMaterialAssets(nr::rhi::ops::UploadReadbackContext &uploadContext,
                                    std::size_t &uploadBudgetRemaining)
{
        forEachLiveRecord(std::span{materialHandles_}, materials_, [&](nr::resource::MaterialHandle handle, MaterialAssetRecord &record) {
            if (!record.uploadQueued)
            {
                return;
            }

            auto const bytesNeeded = sizeof(detail::MaterialGpuData);
            if (bytesNeeded > uploadBudgetRemaining)
            {
                return;
            }

            if (uploadMaterialAsset(handle, record, uploadContext))
            {
                uploadBudgetRemaining -= bytesNeeded;
                uploadBytesThisFrame_ += bytesNeeded;
            }
        });
    }

void Scene::uploadQueuedMeshAssets(nr::rhi::ops::UploadReadbackContext &uploadContext,
                                std::size_t &uploadBudgetRemaining)
{
        forEachLiveRecord(std::span{meshHandles_}, meshes_, [&](nr::resource::MeshHandle handle, MeshAssetRecord &record) {
            if (!record.uploadQueued)
            {
                return;
            }

            auto const bytesNeeded = meshUploadBytes(record.cpu);
            if (bytesNeeded == 0 || bytesNeeded > uploadBudgetRemaining)
            {
                return;
            }

            if (uploadMeshAsset(handle, record, uploadContext))
            {
                uploadBudgetRemaining -= bytesNeeded;
                uploadBytesThisFrame_ += bytesNeeded;
            }
        });
    }

void Scene::uploadQueuedTextureAssets(nr::rhi::ops::UploadReadbackContext &uploadContext,
                                   std::size_t &uploadBudgetRemaining)
{
        forEachLiveRecord(std::span{textureHandles_}, textures_, [&](nr::resource::TextureHandle handle, TextureAssetRecord &record) {
            if (!record.uploadQueued)
            {
                return;
            }

            auto bytesNeeded = textureUploadBytes(record.cpu);
            if (bytesNeeded == 0)
            {
                bytesNeeded = pixelFormatByteSize(record.cpu.format);
            }

            if (bytesNeeded > uploadBudgetRemaining)
            {
                return;
            }

            if (uploadTextureAsset(handle, record, uploadContext))
            {
                uploadBudgetRemaining -= bytesNeeded;
                uploadBytesThisFrame_ += bytesNeeded;
            }
        });
    }

void Scene::markAcquireBatchResident(const PendingAcquireBatch &batch)
{
        switch (batch.asset.kind)
        {
        case GpuAssetKind::mesh:
        {
            auto handle = nr::resource::MeshHandle{batch.asset.slot, batch.asset.generation};
            if (auto *record = meshes_.tryGet(handle); record != nullptr)
            {
                record->gpuState = GpuResidencyState::resident;
                record->gpuVersion = batch.targetGpuVersion;
                record->lastUploadFrameSerial = currentFrame_.frameSerial;
                record->uploadQueued = false;
                discardUploadSourceIfConfigured(*record);
            }
            break;
        }
        case GpuAssetKind::material:
        {
            auto handle = nr::resource::MaterialHandle{batch.asset.slot, batch.asset.generation};
            if (auto *record = materials_.tryGet(handle); record != nullptr)
            {
                record->gpuState = GpuResidencyState::resident;
                record->gpuVersion = batch.targetGpuVersion;
                record->lastUploadFrameSerial = currentFrame_.frameSerial;
                record->uploadQueued = false;
                discardUploadSourceIfConfigured(*record);
            }
            break;
        }
        case GpuAssetKind::texture:
        {
            auto handle = nr::resource::TextureHandle{batch.asset.slot, batch.asset.generation};
            if (auto *record = textures_.tryGet(handle); record != nullptr)
            {
                record->gpuState = GpuResidencyState::resident;
                record->gpuVersion = batch.targetGpuVersion;
                record->lastUploadFrameSerial = currentFrame_.frameSerial;
                record->uploadQueued = false;
                discardUploadSourceIfConfigured(*record);
            }
            break;
        }
        case GpuAssetKind::camera:
        {
            auto handle = nr::resource::CameraAssetHandle{batch.asset.slot, batch.asset.generation};
            if (auto *record = cameras_.tryGet(handle); record != nullptr)
            {
                record->gpuState = GpuResidencyState::resident;
                record->gpuVersion = batch.targetGpuVersion;
                record->lastUploadFrameSerial = currentFrame_.frameSerial;
                record->uploadQueued = false;
                discardUploadSourceIfConfigured(*record);
            }
            break;
        }
        case GpuAssetKind::light:
        {
            auto handle = nr::resource::LightAssetHandle{batch.asset.slot, batch.asset.generation};
            if (auto *record = lights_.tryGet(handle); record != nullptr)
            {
                record->gpuState = GpuResidencyState::resident;
                record->gpuVersion = batch.targetGpuVersion;
                record->lastUploadFrameSerial = currentFrame_.frameSerial;
                record->uploadQueued = false;
                discardUploadSourceIfConfigured(*record);
            }
            break;
        }
        default: break;
        }
    }

[[nodiscard]] std::uint64_t Scene::maxUploadSignalValue(const PendingAcquireBatch &batch) noexcept
{
        auto maxSignalValue = std::uint64_t{0};
        std::ranges::for_each(batch.bufferTickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
            maxSignalValue = std::max(maxSignalValue, ticket.signalValue);
        });
        std::ranges::for_each(batch.imageTickets, [&](const nr::rhi::ops::ImageUploadTicket &ticket) {
            maxSignalValue = std::max(maxSignalValue, ticket.signalValue);
        });
        return maxSignalValue;
    }

void Scene::reapSubmittedAcquireWork()
{
        std::erase_if(submittedAcquireWork_, [&](SubmittedAcquireWork &work) {
            nrAssert(*work.fence != nullptr, "Scene submitted acquire work requires a valid fence.");
            auto result = device_.device.waitForFences(*work.fence, vk::True, 0u);
            nrAssert(
                result == vk::Result::eSuccess || result == vk::Result::eTimeout,
                "Scene failed querying graphics acquire fence status.");
            return result == vk::Result::eSuccess;
        });
    }

void Scene::submitReadyGraphicsAcquireBatches(nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        if (pendingAcquireBatches_.empty())
        {
            return;
        }

        auto const completedUploadValue = uploadContext.completedUploadValue();
        uploadContext.reclaimCompletedUploads();

        auto readyBatches = std::vector<PendingAcquireBatch>{};
        auto waitingBatches = std::vector<PendingAcquireBatch>{};
        readyBatches.reserve(pendingAcquireBatches_.size());
        waitingBatches.reserve(pendingAcquireBatches_.size());

        std::ranges::for_each(pendingAcquireBatches_, [&](PendingAcquireBatch &batch) {
            auto const batchSignalValue = maxUploadSignalValue(batch);
            if (batchSignalValue <= completedUploadValue)
            {
                readyBatches.push_back(std::move(batch));
            }
            else
            {
                waitingBatches.push_back(std::move(batch));
            }
        });
        pendingAcquireBatches_ = std::move(waitingBatches);

        if (readyBatches.empty())
        {
            return;
        }

        auto maxReadySignalValue = std::uint64_t{0};
        std::ranges::for_each(readyBatches, [&](const PendingAcquireBatch &batch) {
            maxReadySignalValue = std::max(maxReadySignalValue, maxUploadSignalValue(batch));
        });

        if (maxReadySignalValue == 0)
        {
            std::ranges::for_each(readyBatches, [&](const PendingAcquireBatch &batch) {
                markAcquireBatchResident(batch);
            });
            return;
        }

        auto acquireWork = SubmittedAcquireWork{};
        acquireWork.commandPool = nr::rhi::CommandPool{
            device_.device,
            device_.queueManager.graphics().queueFamilyIndex(),
            vk::CommandPoolCreateFlagBits::eTransient,
        };
        acquireWork.commandBuffers.emplace(acquireWork.commandPool.allocatePrimary(1));
        auto &acquireCommandBuffer = acquireWork.commandBuffers->front();

        nr::rhi::CommandRecorder::beginPrimary(acquireCommandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            std::ranges::for_each(readyBatches, [&](const PendingAcquireBatch &batch) {
                std::ranges::for_each(batch.bufferTickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
                    uploadContext.recordAcquireBarrier(acquireCommandBuffer, ticket);
                });
                std::ranges::for_each(batch.imageTickets, [&](const nr::rhi::ops::ImageUploadTicket &ticket) {
                    uploadContext.recordImageAcquireBarrier(acquireCommandBuffer, ticket);
                });
            });
        }
        nr::rhi::CommandRecorder::end(acquireCommandBuffer);

        auto acquireSubmission = nr::rhi::CommandBatch{};
        acquireSubmission.addWait(
            uploadContext.uploadTimelineSemaphore(),
            vk::PipelineStageFlagBits2::eAllCommands,
            maxReadySignalValue);
        acquireSubmission.addCommandBuffer(acquireCommandBuffer);

        acquireWork.fence = vk::raii::Fence(device_.device, vk::FenceCreateInfo{});
        device_.queueManager.graphics().submit(std::move(acquireSubmission), std::cref(acquireWork.fence));
        submittedAcquireWork_.push_back(std::move(acquireWork));

        std::ranges::for_each(readyBatches, [&](const PendingAcquireBatch &batch) {
            markAcquireBatchResident(batch);
        });
    }

void Scene::pollUploadTimelineUntilAcquireBatchesReady(nr::rhi::ops::UploadReadbackContext &uploadContext)
{
        while (!pendingAcquireBatches_.empty())
        {
            submitReadyGraphicsAcquireBatches(uploadContext);
            if (!pendingAcquireBatches_.empty())
            {
                std::this_thread::yield();
            }
        }
    }

void Scene::subtractSaturating(std::size_t &value, std::size_t delta) noexcept
{
        value = value >= delta ? value - delta : 0u;
    }

void Scene::compactDeadAssetHandles()
{
        compactDeadHandles(meshHandles_, meshes_);
        compactDeadHandles(materialHandles_, materials_);
        compactDeadHandles(textureHandles_, textures_);
        compactDeadHandles(cameraHandles_, cameras_);
        compactDeadHandles(lightHandles_, lights_);
    }

void Scene::collectUnusedMeshAsset(nr::resource::MeshHandle handle)
{
        collectUnusedAsset(handle, meshes_, retiredMeshPayloadGraveyard_);
    }

void Scene::collectUnusedMaterialAsset(nr::resource::MaterialHandle handle)
{
        collectUnusedAsset(handle, materials_, retiredMaterialPayloadGraveyard_);
    }

void Scene::collectUnusedTextureAsset(nr::resource::TextureHandle handle)
{
        collectUnusedAsset(handle, textures_, retiredTexturePayloadGraveyard_);
    }

void Scene::collectUnusedCameraAsset(nr::resource::CameraAssetHandle handle)
{
        collectUnusedAsset(handle, cameras_, retiredCameraPayloadGraveyard_);
    }

void Scene::collectUnusedLightAsset(nr::resource::LightAssetHandle handle)
{
        collectUnusedAsset(handle, lights_, retiredLightPayloadGraveyard_);
    }

void Scene::collectTemplatePinnedAssets(const TemplateResourcePinSet &pinSet)
{
        auto collectAssets = [&]<typename CollectorFn>(const auto &handles, CollectorFn &&collector) {
            std::ranges::for_each(handles, std::forward<CollectorFn>(collector));
        };

        collectAssets(pinSet.meshes, [this](auto h) { collectUnusedMeshAsset(h); });
        collectAssets(pinSet.materials, [this](auto h) { collectUnusedMaterialAsset(h); });
        collectAssets(pinSet.textures, [this](auto h) { collectUnusedTextureAsset(h); });
        collectAssets(pinSet.cameras, [this](auto h) { collectUnusedCameraAsset(h); });
        collectAssets(pinSet.lights, [this](auto h) { collectUnusedLightAsset(h); });
    }

void Scene::bridgeTextures(const nr::load::SceneAsset &sceneAsset,
                        const SceneBridgePlan &plan,
                        std::vector<nr::resource::TextureHandle> &textureHandlesBySource)
{
        auto const colorHints = detail::buildTextureColorSpaceHints(sceneAsset);

        std::ranges::for_each(plan.textures, [&](const TextureBridgeInput &entry) {
            if (entry.sourceIndex >= sceneAsset.textures.size())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::texture,
                    std::format("Texture bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto [handle, created] = textures_.getOrCreate(entry.canonicalKey, [](nr::resource::TextureHandle newHandle, const std::string &key) {
                return TextureAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                textureHandles_.push_back(handle);
            }

            textureHandlesBySource[entry.sourceIndex] = handle;

            auto *record = textures_.tryGet(handle);
            if (record == nullptr)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::texture,
                    std::format("Texture storage lookup failed for key '{}'.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            if (!created && record->cpuReady)
            {
                return;
            }

            auto const &sourceTexture = sceneAsset.textures[entry.sourceIndex];
            auto const hint = entry.sourceIndex < colorHints.size() ? colorHints[entry.sourceIndex] : detail::TextureColorSpaceHint{};
            if (hint.hasColor && hint.hasLinear)
            {
                reportImport<nr::LogLevel::warning>(
                    ImportStage::texture,
                    std::format("Texture '{}' is referenced by both color and linear slots; forcing linear sampling.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
            }

            auto texture = nr::resource::Texture{};
            texture.name = sourceTexture.key;
            texture.sourcePath = sourceTexture.resolvedPath;
            texture.srgb = hint.preferSrgb;

            if (sourceTexture.decodedImage.has_value())
            {
                auto prepared = detail::prepareDecodedImageLevel(*sourceTexture.decodedImage);
                if (!prepared.has_value())
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::texture,
                        std::format("Decoded texture '{}' failed canonical image-level preparation.", entry.canonicalKey),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    return;
                }

                texture.width = sourceTexture.decodedImage->width;
                texture.height = sourceTexture.decodedImage->height;
                texture.format = detail::pickTextureFormat(prepared->channelCount, texture.srgb);
                texture.levels.push_back(std::move(prepared->level));
            }
            else if (sourceTexture.rawRgba8.has_value())
            {
                auto const &raw = *sourceTexture.rawRgba8;
                if (raw.width == 0u || raw.height == 0u)
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::texture,
                        std::format("Embedded raw texture '{}' has invalid dimensions {}x{}.",
                                    entry.canonicalKey,
                                    raw.width,
                                    raw.height),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    return;
                }

                texture.width = raw.width;
                texture.height = raw.height;
                texture.format = detail::pickTextureFormat(4u, texture.srgb);
                texture.levels.push_back(detail::prepareRawImageLevel(raw));
            }
            else
            {
                texture.width = 1;
                texture.height = 1;
                texture.format = detail::pickTextureFormat(4u, texture.srgb);
            }

            if (!texture.valid())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::texture,
                    std::format("Canonical texture '{}' failed resource::Texture::valid() validation.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            record->cpu = std::move(texture);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        });
    }

void Scene::bridgeMaterials(const nr::load::SceneAsset &sceneAsset,
                         const SceneBridgePlan &plan,
                         const std::vector<nr::resource::TextureHandle> &textureHandlesBySource,
                         std::vector<nr::resource::MaterialHandle> &materialHandlesBySource)
{
        auto ensureExtensionForSlot = [](nr::resource::Material &material,
                                         nr::resource::MaterialTextureSlotSemantic semantic) {
            switch (semantic)
            {
            case nr::resource::MaterialTextureSlotSemantic::clearcoat:
            case nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness:
            case nr::resource::MaterialTextureSlotSemantic::clearcoatNormal:
                if (!material.clearcoat.has_value())
                {
                    material.clearcoat.emplace();
                }
                break;
            case nr::resource::MaterialTextureSlotSemantic::sheenColor:
            case nr::resource::MaterialTextureSlotSemantic::sheenRoughness:
                if (!material.sheen.has_value())
                {
                    material.sheen.emplace();
                }
                break;
            case nr::resource::MaterialTextureSlotSemantic::transmission:
                if (!material.transmission.has_value())
                {
                    material.transmission.emplace();
                }
                break;
            case nr::resource::MaterialTextureSlotSemantic::anisotropy:
                if (!material.anisotropy.has_value())
                {
                    material.anisotropy.emplace();
                }
                break;
            default: break;
            }
        };

        std::ranges::for_each(plan.materials, [&](const MaterialBridgeInput &entry) {
            if (entry.sourceIndex >= sceneAsset.materials.size())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::material,
                    std::format("Material bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto [handle, created] = materials_.getOrCreate(entry.canonicalKey, [](nr::resource::MaterialHandle newHandle, const std::string &key) {
                return MaterialAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                materialHandles_.push_back(handle);
            }

            materialHandlesBySource[entry.sourceIndex] = handle;

            auto *record = materials_.tryGet(handle);
            if (record == nullptr)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::material,
                    std::format("Material storage lookup failed for key '{}'.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            if (!created && record->cpuReady)
            {
                return;
            }

            auto const &sourceMaterial = sceneAsset.materials[entry.sourceIndex];
            auto material = nr::resource::Material{};
            material.name = sourceMaterial.name;

            auto convertedFactors = detail::MetallicRoughnessFactorSet{
                .baseColorFactor = glm::vec4{
                    sourceMaterial.baseColorFactor[0],
                    sourceMaterial.baseColorFactor[1],
                    sourceMaterial.baseColorFactor[2],
                    sourceMaterial.baseColorFactor[3] * sourceMaterial.opacity,
                },
                .metallicFactor = sourceMaterial.metallicFactor,
                .roughnessFactor = sourceMaterial.roughnessFactor,
            };

            if (detail::hasSpecularGlossinessFactors(sourceMaterial))
            {
                convertedFactors = detail::convertSpecularGlossinessToMetallicRoughness(sourceMaterial);
                reportImport<nr::LogLevel::warning>(
                    ImportStage::material,
                    std::format("Material '{}' uses specular-glossiness factors; converted approximately to metallic-roughness.",
                                material.name),
                    entry.canonicalKey,
                    entry.sourceIndex);
            }

            material.core.baseColorFactor = convertedFactors.baseColorFactor;
            material.core.emissiveFactor = glm::vec3{
                sourceMaterial.emissiveFactor[0],
                sourceMaterial.emissiveFactor[1],
                sourceMaterial.emissiveFactor[2],
            };
            material.core.metallicFactor = convertedFactors.metallicFactor;
            material.core.roughnessFactor = convertedFactors.roughnessFactor;
            material.core.doubleSided = sourceMaterial.doubleSided;
            assignIfPresent(sourceMaterial.normalScale, [&](float normalScale) {
                material.core.normalScale = normalScale;
            });
            assignIfPresent(sourceMaterial.occlusionStrength, [&](float occlusionStrength) {
                material.core.occlusionStrength = occlusionStrength;
            });
            assignIfPresent(sourceMaterial.alphaCutoff, [&](float alphaCutoff) {
                material.core.alphaCutoff = alphaCutoff;
            });

            material.core.alphaMode = resolveMaterialAlphaMode(sourceMaterial);

            assignIfPresent(sourceMaterial.clearcoatFactor, [&](float clearcoatFactor) {
                if (!material.clearcoat.has_value())
                {
                    material.clearcoat.emplace();
                }
                material.clearcoat->factor = clearcoatFactor;
            });
            assignIfPresent(sourceMaterial.clearcoatRoughnessFactor, [&](float clearcoatRoughnessFactor) {
                if (!material.clearcoat.has_value())
                {
                    material.clearcoat.emplace();
                }
                material.clearcoat->roughnessFactor = clearcoatRoughnessFactor;
            });
            assignIfPresent(sourceMaterial.sheenColorFactor, [&](const std::array<float, 3> &sheenColorFactor) {
                if (!material.sheen.has_value())
                {
                    material.sheen.emplace();
                }
                material.sheen->colorFactor = glm::vec3{sheenColorFactor[0], sheenColorFactor[1], sheenColorFactor[2]};
            });
            assignIfPresent(sourceMaterial.sheenRoughnessFactor, [&](float sheenRoughnessFactor) {
                if (!material.sheen.has_value())
                {
                    material.sheen.emplace();
                }
                material.sheen->roughnessFactor = sheenRoughnessFactor;
            });
            assignIfPresent(sourceMaterial.transmissionFactor, [&](float transmissionFactor) {
                if (!material.transmission.has_value())
                {
                    material.transmission.emplace();
                }
                material.transmission->factor = transmissionFactor;
            });
            assignIfPresent(sourceMaterial.anisotropyFactor, [&](float anisotropyFactor) {
                if (!material.anisotropy.has_value())
                {
                    material.anisotropy.emplace();
                }
                material.anisotropy->factor = anisotropyFactor;
            });
            assignIfPresent(sourceMaterial.anisotropyRotation, [&](float anisotropyRotation) {
                if (!material.anisotropy.has_value())
                {
                    material.anisotropy.emplace();
                }
                material.anisotropy->rotation = anisotropyRotation;
            });

            auto materialHasError = false;
            std::ranges::for_each(sourceMaterial.textures, [&](const nr::load::MaterialTextureBinding &binding) {
                auto const sourceSemantic = binding.sourceSemanticName.empty()
                                                ? nr::resource::materialTextureSlotSemanticName(binding.semantic)
                                                : std::string_view{binding.sourceSemanticName};
                if (binding.textureIndex >= textureHandlesBySource.size())
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::material,
                        std::format("Material '{}' references out-of-range texture index {}.", material.name, binding.textureIndex),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    materialHasError = true;
                    return;
                }

                auto textureHandle = textureHandlesBySource[binding.textureIndex];
                if (!textureHandle.valid())
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::material,
                        std::format("Material '{}' references unresolved texture index {}.", material.name, binding.textureIndex),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    materialHasError = true;
                    return;
                }

                if (textures_.tryGet(textureHandle) == nullptr)
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::material,
                        std::format("Material '{}' references unknown texture handle (slot={}, generation={}).",
                                    material.name,
                                    textureHandle.slot,
                                    textureHandle.generation),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    materialHasError = true;
                    return;
                }

                if (!nr::resource::materialTextureSlotSemanticValid(binding.semantic))
                {
                    auto const specGlossTexture =
                        sourceSemantic == "specular" ||
                        sourceSemantic == "shininess" ||
                        sourceSemantic == "maya_specular" ||
                        sourceSemantic == "maya_specular_color" ||
                        sourceSemantic == "maya_specular_roughness";
                    reportImport<nr::LogLevel::warning>(
                        ImportStage::material,
                        specGlossTexture
                            ? std::format("Material '{}' ignored specular-glossiness texture semantic '{}' because texture baking to metallic-roughness is not implemented.",
                                          material.name,
                                          sourceSemantic)
                            : std::format("Material '{}' ignored unsupported texture semantic '{}'.",
                                          material.name,
                                          sourceSemantic),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    return;
                }

                ensureExtensionForSlot(material, binding.semantic);
                auto *slot = std::addressof(material.slot(binding.semantic));

                if (slot->texture.valid())
                {
                    if (slot->texture == textureHandle)
                    {
                        return;
                    }

                    reportImport<nr::LogLevel::warning>(
                        ImportStage::material,
                        std::format("Material '{}' has duplicate semantic '{}' with a different texture; keeping first slot assignment for {}.",
                                    material.name,
                                    sourceSemantic,
                                    nr::resource::materialTextureSlotSemanticName(binding.semantic)),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    return;
                }

                slot->texture = textureHandle;
                slot->uvSet = binding.uvChannel;
            });

            std::ranges::for_each(material.textureSlots, [&](const nr::resource::MaterialTextureSlot &slot) {
                auto textureHandle = slot.texture;
                if (!textureHandle.valid())
                {
                    return;
                }

                if (textures_.tryGet(textureHandle) == nullptr)
                {
                    materialHasError = true;
                    reportImport<nr::LogLevel::error>(
                        ImportStage::material,
                        std::format("Material '{}' resolved to texture handle (slot={}, generation={}) missing from registry.",
                                    material.name,
                                    textureHandle.slot,
                                    textureHandle.generation),
                        entry.canonicalKey,
                        entry.sourceIndex);
                }
            });

            if (materialHasError)
            {
                return;
            }

            record->cpu = std::move(material);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        });
    }

void Scene::bridgeMeshes(const nr::load::SceneAsset &sceneAsset,
                      const SceneBridgePlan &plan,
                      const std::vector<nr::resource::MaterialHandle> &materialHandlesBySource,
                      std::vector<nr::resource::MeshHandle> &meshHandlesBySource)
{
        auto defaultMaterialHandle = std::optional<nr::resource::MaterialHandle>{};
        auto ensureDefaultMaterial = [&]() -> nr::resource::MaterialHandle {
            if (defaultMaterialHandle.has_value())
            {
                return *defaultMaterialHandle;
            }

            auto defaultKey = sceneAsset.sourcePath.empty()
                                  ? std::string{"<scene>::material[default]"}
                                  : std::format("{}::material[default]", sceneAsset.sourcePath.generic_string());
            auto [handle, created] = materials_.getOrCreate(defaultKey, [](nr::resource::MaterialHandle newHandle, const std::string &key) {
                return MaterialAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                materialHandles_.push_back(handle);
            }

            auto *record = materials_.tryGet(handle);
            if (record != nullptr && !record->cpuReady)
            {
                auto material = nr::resource::Material{};
                material.name = "default_material";
                record->cpu = std::move(material);
                record->cpuReady = true;
                record->uploadQueued = false;
                if (record->cpuVersion == 0u)
                {
                    record->cpuVersion = 1u;
                }
            }

            defaultMaterialHandle = handle;
            return handle;
        };

        std::ranges::for_each(plan.meshes, [&](const MeshBridgeInput &entry) {
            if (entry.sourceIndex >= sceneAsset.meshes.size())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::mesh,
                    std::format("Mesh bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto [handle, created] = meshes_.getOrCreate(entry.canonicalKey, [](nr::resource::MeshHandle newHandle, const std::string &key) {
                return MeshAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                meshHandles_.push_back(handle);
            }

            meshHandlesBySource[entry.sourceIndex] = handle;

            auto *record = meshes_.tryGet(handle);
            if (record == nullptr)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::mesh,
                    std::format("Mesh storage lookup failed for key '{}'.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            if (!created && record->cpuReady)
            {
                return;
            }

            auto const &sourceMesh = sceneAsset.meshes[entry.sourceIndex];
            auto mesh = nr::resource::Mesh{};
            mesh.name = sourceMesh.name;
            mesh.clockwiseFrontFace = sourceMesh.clockwiseFrontFace;

            mesh.vertices.reserve(sourceMesh.vertices.size());
            std::ranges::for_each(sourceMesh.vertices, [&](const nr::load::VertexAsset &sourceVertex) {
                auto vertex = nr::resource::Vertex{};
                vertex.position = glm::vec3{sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2]};
                vertex.normal = glm::vec3{sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]};
                vertex.tangent = glm::vec4{sourceVertex.tangent[0], sourceVertex.tangent[1], sourceVertex.tangent[2], sourceVertex.tangent[3]};
                vertex.texCoord0 = glm::vec2{sourceVertex.texCoord0[0], sourceVertex.texCoord0[1]};
                vertex.color0 = glm::vec4{sourceVertex.color0[0], sourceVertex.color0[1], sourceVertex.color0[2], sourceVertex.color0[3]};
                mesh.vertices.push_back(vertex);
            });

            mesh.indices = sourceMesh.indices;

            if (sourceMesh.geometries.empty())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::mesh,
                    std::format("Mesh '{}' has no geometry records.", sourceMesh.name),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto meshHasError = false;
            auto resolveGeometryMaterial = [&](const nr::load::MeshGeometryAsset &sourceGeometry) {
                if (sourceGeometry.materialIndex == nr::load::invalidIndex)
                {
                    auto handle = ensureDefaultMaterial();
                    reportImport<nr::LogLevel::warning>(
                        ImportStage::mesh,
                        std::format("Mesh '{}' geometry '{}' has no source material; using default material.",
                                    sourceMesh.name,
                                    sourceGeometry.name),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    return handle;
                }

                if (sourceGeometry.materialIndex >= materialHandlesBySource.size())
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::mesh,
                        std::format("Mesh '{}' geometry '{}' references out-of-range material index {}.",
                                    sourceMesh.name,
                                    sourceGeometry.name,
                                    sourceGeometry.materialIndex),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    meshHasError = true;
                    return nr::resource::MaterialHandle{};
                }

                auto materialHandle = materialHandlesBySource[sourceGeometry.materialIndex];
                if (!materialHandle.valid())
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::mesh,
                        std::format("Mesh '{}' geometry '{}' references unresolved material index {}.",
                                    sourceMesh.name,
                                    sourceGeometry.name,
                                    sourceGeometry.materialIndex),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    meshHasError = true;
                    return nr::resource::MaterialHandle{};
                }

                if (materials_.tryGet(materialHandle) == nullptr)
                {
                    reportImport<nr::LogLevel::error>(
                        ImportStage::mesh,
                        std::format("Mesh '{}' geometry '{}' resolved material handle (slot={}, generation={}) missing in registry.",
                                    sourceMesh.name,
                                    sourceGeometry.name,
                                    materialHandle.slot,
                                    materialHandle.generation),
                        entry.canonicalKey,
                        entry.sourceIndex);
                    meshHasError = true;
                    return nr::resource::MaterialHandle{};
                }

                return materialHandle;
            };

            auto buildGeometryBounds = [&](const nr::resource::MeshGeometry &geometry) {
                auto bounds = nr::resource::Aabb{};
                if (!mesh.indexed())
                {
                    auto begin = static_cast<std::size_t>(geometry.firstIndex);
                    auto end = begin + static_cast<std::size_t>(geometry.indexCount);
                    if (end <= mesh.vertices.size())
                    {
                        auto vertexRange = std::ranges::subrange(mesh.vertices.begin() + static_cast<std::ptrdiff_t>(begin),
                                                                  mesh.vertices.begin() + static_cast<std::ptrdiff_t>(end));
                        std::ranges::for_each(vertexRange, [&](const nr::resource::Vertex &vertex) {
                            bounds.expand(vertex.position);
                        });
                    }
                    return bounds;
                }

                auto begin = static_cast<std::size_t>(geometry.firstIndex);
                auto end = begin + static_cast<std::size_t>(geometry.indexCount);
                if (end <= mesh.indices.size())
                {
                    auto indexRange = std::ranges::subrange(mesh.indices.begin() + static_cast<std::ptrdiff_t>(begin),
                                                            mesh.indices.begin() + static_cast<std::ptrdiff_t>(end));
                    std::ranges::for_each(indexRange, [&](std::uint32_t localIndex) {
                        auto vertexIndex = static_cast<std::uint64_t>(localIndex) + static_cast<std::uint64_t>(geometry.vertexOffset);
                        if (vertexIndex < mesh.vertices.size())
                        {
                            bounds.expand(mesh.vertices[static_cast<std::size_t>(vertexIndex)].position);
                        }
                    });
                }
                return bounds;
            };

            mesh.geometries.reserve(sourceMesh.geometries.size());
            auto geometryIndices = std::views::iota(std::size_t{0}, sourceMesh.geometries.size());
            std::ranges::for_each(geometryIndices, [&](std::size_t geometryIndex) {
                auto const &sourceGeometry = sourceMesh.geometries[geometryIndex];
                auto materialHandle = resolveGeometryMaterial(sourceGeometry);
                if (!materialHandle.valid())
                {
                    return;
                }

                auto geometry = nr::resource::MeshGeometry{};
                geometry.name = sourceGeometry.name.empty()
                                    ? std::format("{}_geometry_{}", sourceMesh.name.empty() ? "mesh" : sourceMesh.name, geometryIndex)
                                    : sourceGeometry.name;
                geometry.firstIndex = sourceGeometry.firstIndex;
                geometry.indexCount = sourceGeometry.indexCount;
                geometry.vertexOffset = sourceGeometry.vertexOffset;
                geometry.material = materialHandle;
                mesh.geometries.push_back(std::move(geometry));
            });

            if (meshHasError)
            {
                return;
            }

            mesh.rebuildLocalBounds();
            mesh.rebuildLocalSphere();
            std::ranges::for_each(mesh.geometries, [&](nr::resource::MeshGeometry &geometry) {
                geometry.localBounds = buildGeometryBounds(geometry);
            });

            if (!mesh.validate())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::mesh,
                    std::format("Canonical mesh '{}' failed validate() after normalization.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            record->cpu = std::move(mesh);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        });
    }

void Scene::bridgeCameras(const nr::load::SceneAsset &sceneAsset,
                       const SceneBridgePlan &plan,
                       std::vector<nr::resource::CameraAssetHandle> &cameraHandlesBySource)
{
        constexpr auto kEpsilon = 1e-4f;
        constexpr auto kFallbackFov = glm::radians(60.0f);

        std::ranges::for_each(plan.cameras, [&](const CameraBridgeInput &entry) {
            if (entry.sourceIndex >= sceneAsset.cameras.size())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::camera,
                    std::format("Camera bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto [handle, created] = cameras_.getOrCreate(entry.canonicalKey, [](nr::resource::CameraAssetHandle newHandle, const std::string &key) {
                return CameraAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                cameraHandles_.push_back(handle);
            }

            cameraHandlesBySource[entry.sourceIndex] = handle;

            auto *record = cameras_.tryGet(handle);
            if (record == nullptr)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::camera,
                    std::format("Camera storage lookup failed for key '{}'.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            if (!created && record->cpuReady)
            {
                return;
            }

            auto const &sourceCamera = sceneAsset.cameras[entry.sourceIndex];

            auto camera = nr::resource::CameraAsset{};
            camera.name = sourceCamera.name.empty()
                              ? std::format("camera_{}", entry.sourceIndex)
                              : sourceCamera.name;

            if (sourceCamera.orthographicWidth > kEpsilon)
            {
                camera.projection = nr::resource::CameraProjection::orthographic;

                auto aspect = sourceCamera.aspect;
                if (std::isfinite(aspect) && aspect > kEpsilon)
                {
                    camera.authoredAspectRatio = aspect;
                }
                else
                {
                    aspect = 1.0f;
                    reportImport<nr::LogLevel::warning>(
                        ImportStage::camera,
                        std::format("Camera '{}' has invalid aspect {} on orthographic path; falling back to aspect=1.",
                                    camera.name,
                                    sourceCamera.aspect),
                        entry.canonicalKey,
                        entry.sourceIndex);
                }

                camera.orthoHeight = sourceCamera.orthographicWidth / aspect;
            }
            else
            {
                camera.projection = nr::resource::CameraProjection::perspective;

                auto horizontalFov = sourceCamera.horizontalFov;
                if (!(std::isfinite(horizontalFov) && horizontalFov > kEpsilon))
                {
                    horizontalFov = kFallbackFov;
                    reportImport<nr::LogLevel::warning>(
                        ImportStage::camera,
                        std::format("Camera '{}' has invalid horizontalFov {} on perspective path; falling back to 60 degrees.",
                                    camera.name,
                                    sourceCamera.horizontalFov),
                        entry.canonicalKey,
                        entry.sourceIndex);
                }

                auto aspect = sourceCamera.aspect;
                if (std::isfinite(aspect) && aspect > kEpsilon)
                {
                    camera.authoredAspectRatio = aspect;
                    camera.verticalFovRadians = 2.0f * std::atan(std::tan(horizontalFov * 0.5f) / aspect);
                }
                else
                {
                    camera.verticalFovRadians = horizontalFov;
                    reportImport<nr::LogLevel::warning>(
                        ImportStage::camera,
                        std::format("Camera '{}' has invalid aspect {} on perspective path; using horizontalFov as verticalFov.",
                                    camera.name,
                                    sourceCamera.aspect),
                        entry.canonicalKey,
                        entry.sourceIndex);
                }
            }

            auto nearPlane = sourceCamera.nearPlane;
            if (!(std::isfinite(nearPlane) && nearPlane > kEpsilon))
            {
                nearPlane = 0.1f;
                reportImport<nr::LogLevel::warning>(
                    ImportStage::camera,
                    std::format("Camera '{}' has invalid near plane {}; falling back to 0.1.",
                                camera.name,
                                sourceCamera.nearPlane),
                    entry.canonicalKey,
                    entry.sourceIndex);
            }

            auto farPlane = sourceCamera.farPlane;
            if (!(std::isfinite(farPlane) && farPlane > nearPlane + kEpsilon))
            {
                farPlane = nearPlane + 1000.0f;
                reportImport<nr::LogLevel::warning>(
                    ImportStage::camera,
                    std::format("Camera '{}' has invalid far plane {}; falling back to near+1000.",
                                camera.name,
                                sourceCamera.farPlane),
                    entry.canonicalKey,
                    entry.sourceIndex);
            }

            camera.nearPlane = nearPlane;
            camera.farPlane = farPlane;

            record->cpu = std::move(camera);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        });
    }

void Scene::bridgeLights(const nr::load::SceneAsset &sceneAsset,
                      const SceneBridgePlan &plan,
                      std::vector<nr::resource::LightAssetHandle> &lightHandlesBySource)
{
        constexpr auto kEpsilon = 1e-4f;

        std::ranges::for_each(plan.lights, [&](const LightBridgeInput &entry) {
            if (entry.sourceIndex >= sceneAsset.lights.size())
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::light,
                    std::format("Light bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto const &sourceLight = sceneAsset.lights[entry.sourceIndex];
            auto mappedType = detail::mapLightType(sourceLight.type);
            if (!mappedType.has_value())
            {
                reportImport<nr::LogLevel::warning>(
                    ImportStage::light,
                    std::format("Light '{}' uses unsupported type '{}' (raw={}) and will be skipped.",
                                sourceLight.name,
                                sourceLight.type,
                                sourceLight.typeRaw),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            auto [handle, created] = lights_.getOrCreate(entry.canonicalKey, [](nr::resource::LightAssetHandle newHandle, const std::string &key) {
                return LightAssetRecord{
                    .handle = newHandle,
                    .stableKey = key,
                };
            });

            if (created)
            {
                lightHandles_.push_back(handle);
            }

            lightHandlesBySource[entry.sourceIndex] = handle;

            auto *record = lights_.tryGet(handle);
            if (record == nullptr)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::light,
                    std::format("Light storage lookup failed for key '{}'.", entry.canonicalKey),
                    entry.canonicalKey,
                    entry.sourceIndex);
                return;
            }

            if (!created && record->cpuReady)
            {
                return;
            }

            auto light = nr::resource::LightAsset{};
            light.name = sourceLight.name.empty()
                             ? std::format("light_{}", entry.sourceIndex)
                             : sourceLight.name;
            light.type = *mappedType;

            auto diffuseColor = detail::toVec3(sourceLight.colorDiffuse);
            auto specularColor = detail::toVec3(sourceLight.colorSpecular);
            auto ambientColor = detail::toVec3(sourceLight.colorAmbient);

            auto color = diffuseColor;
            if (glm::dot(color, color) <= kEpsilon)
            {
                if (glm::dot(specularColor, specularColor) > kEpsilon)
                {
                    color = specularColor;
                }
                else if (glm::dot(ambientColor, ambientColor) > kEpsilon)
                {
                    color = ambientColor;
                }
                else
                {
                    color = glm::vec3{1.0f};
                }
            }

            light.color = color;
            light.intensity = std::max({color.r, color.g, color.b, 1e-3f});

            if (light.type != nr::resource::LightType::directional)
            {
                if (std::isfinite(sourceLight.attenuationQuadratic) && sourceLight.attenuationQuadratic > kEpsilon)
                {
                    light.range = std::sqrt(1.0f / sourceLight.attenuationQuadratic);
                }
                else if (std::isfinite(sourceLight.attenuationLinear) && sourceLight.attenuationLinear > kEpsilon)
                {
                    light.range = 1.0f / sourceLight.attenuationLinear;
                }
            }

            auto innerCone = sourceLight.innerCone;
            if (!(std::isfinite(innerCone) && innerCone >= 0.0f))
            {
                innerCone = 0.0f;
            }

            auto outerCone = sourceLight.outerCone;
            if (!(std::isfinite(outerCone) && outerCone > 0.0f))
            {
                outerCone = glm::radians(45.0f);
            }

            if (outerCone < innerCone)
            {
                outerCone = innerCone;
                reportImport<nr::LogLevel::warning>(
                    ImportStage::light,
                    std::format("Light '{}' has outer cone smaller than inner cone; clamping outer to inner.", light.name),
                    entry.canonicalKey,
                    entry.sourceIndex);
            }

            light.innerConeRadians = innerCone;
            light.outerConeRadians = outerCone;

            record->cpu = std::move(light);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        });
    }

[[nodiscard]] TemplateResourcePinSet Scene::buildTemplatePinSet(
        std::span<const nr::resource::MeshHandle> meshHandles,
        std::span<const nr::resource::MaterialHandle> materialHandles,
        std::span<const nr::resource::TextureHandle> textureHandles,
        std::span<const nr::resource::CameraAssetHandle> cameraHandles,
        std::span<const nr::resource::LightAssetHandle> lightHandles) const
{
        auto pinSet = TemplateResourcePinSet{};
        auto appendHandles = [&](const auto &handles, auto &collection) {
            appendValidUniqueHandles(handles, collection);
        };

        appendHandles(meshHandles, pinSet.meshes);
        appendHandles(materialHandles, pinSet.materials);

        auto geometryMaterialSeen = std::set<std::uint64_t>{};
        std::ranges::for_each(pinSet.materials, [&](nr::resource::MaterialHandle materialHandle) {
            if (materialHandle.valid())
            {
                geometryMaterialSeen.emplace(materialHandle.packed());
            }
        });
        std::ranges::for_each(pinSet.meshes, [&](nr::resource::MeshHandle meshHandle) {
            auto const *meshRecord = meshes_.tryGet(meshHandle);
            if (meshRecord == nullptr || !meshRecord->cpuReady)
            {
                return;
            }

            std::ranges::for_each(meshRecord->cpu.geometries, [&](const nr::resource::MeshGeometry &geometry) {
                if (geometry.material.valid())
                {
                    detail::appendUniqueHandle(pinSet.materials, geometryMaterialSeen, geometry.material);
                }
            });
        });

        appendHandles(textureHandles, pinSet.textures);
        appendHandles(cameraHandles, pinSet.cameras);
        appendHandles(lightHandles, pinSet.lights);

        return pinSet;
    }

void Scene::retainTemplatePins(const TemplateResourcePinSet &pinSet)
{
        auto incrementPins = [&](const auto &collection, auto &storage) {
            incrementTemplatePins(std::span{collection}, storage);
        };

        incrementPins(pinSet.meshes, meshes_);
        incrementPins(pinSet.materials, materials_);
        incrementPins(pinSet.textures, textures_);
        incrementPins(pinSet.cameras, cameras_);
        incrementPins(pinSet.lights, lights_);
    }

void Scene::releaseTemplatePins(const TemplateResourcePinSet &pinSet)
{
        auto decrementPins = [&](const auto &collection, auto &storage) {
            decrementTemplatePins(std::span{collection}, storage);
        };

        decrementPins(pinSet.meshes, meshes_);
        decrementPins(pinSet.materials, materials_);
        decrementPins(pinSet.textures, textures_);
        decrementPins(pinSet.cameras, cameras_);
        decrementPins(pinSet.lights, lights_);
    }

[[nodiscard]] bool Scene::useParentHierarchyStorage(const nr::load::SceneAsset &sceneAsset,
                                                        TemplateHierarchyPolicy policy) noexcept
{
        switch (policy)
        {
        case TemplateHierarchyPolicy::preferParent: return true;
        case TemplateHierarchyPolicy::preferChildOf: return false;
        case TemplateHierarchyPolicy::autoSelect:
        default: break;
        }

        // Prefer ChildOf in auto mode for stable prefab teardown semantics.
        (void)sceneAsset;
        return false;
    }

void Scene::attachHierarchyRelation(flecs::entity child, flecs::entity parent, bool useParentStorage)
{
        if (useParentStorage)
        {
            child.set(EcsParent{.value = parent.id()});
            return;
        }

        child.add(EcsChildOf, parent.id());
    }

[[nodiscard]] nr::resource::AlphaMode Scene::resolveMaterialAlphaMode(const nr::load::MaterialAsset &sourceMaterial) noexcept
{
        if (sourceMaterial.alphaModeHint == nr::load::MaterialAlphaModeHint::mask)
        {
            return nr::resource::AlphaMode::mask;
        }

        if (sourceMaterial.alphaModeHint == nr::load::MaterialAlphaModeHint::blend ||
            sourceMaterial.opacity < 1.0f)
        {
            return nr::resource::AlphaMode::blend;
        }

        return nr::resource::AlphaMode::opaque;
    }

[[nodiscard]] nr::resource::Aabb Scene::meshLocalBounds(nr::resource::MeshHandle meshHandle) const
{
        if (!meshHandle.valid())
        {
            return nr::resource::Aabb{};
        }

        auto const *meshRecord = meshes_.tryGet(meshHandle);
        if (meshRecord == nullptr || !meshRecord->cpuReady)
        {
            return nr::resource::Aabb{};
        }

        return meshRecord->cpu.localBounds;
    }

[[nodiscard]] std::uint32_t Scene::meshGeometryCount(nr::resource::MeshHandle meshHandle) const noexcept
{
        auto const *meshRecord = meshes_.tryGet(meshHandle);
        if (meshRecord == nullptr || !meshRecord->cpuReady)
        {
            return 0;
        }

        return static_cast<std::uint32_t>(meshRecord->cpu.geometries.size());
    }

[[nodiscard]] nr::resource::MeshHandle Scene::meshHandleForEntity(flecs::entity entity) noexcept
{
        if (auto binding = entity.try_get<RenderableBinding>(); binding != nullptr && binding->mesh.valid())
        {
            return binding->mesh;
        }

        if (auto binding = entity.try_get<SceneTemplateMeshBindingRef>(); binding != nullptr)
        {
            return binding->mesh;
        }

        return {};
    }

[[nodiscard]] std::uint64_t Scene::defaultSelectionBits(nr::resource::MaterialHandle materialHandle) const noexcept
{
        // All objects get ray-tracing and shadow casting capabilities
        auto bits = sceneSelectionMask(SceneSelectionBit::rtMain) |
                    sceneSelectionMask(SceneSelectionBit::shadowCaster);
        
        // Default to raster opaque if no material or not ready
        if (!materialHandle.valid())
        {
            return bits | sceneSelectionMask(SceneSelectionBit::rasterOpaque);
        }

        auto const *materialRecord = materials_.tryGet(materialHandle);
        if (materialRecord == nullptr || !materialRecord->cpuReady)
        {
            return bits | sceneSelectionMask(SceneSelectionBit::rasterOpaque);
        }

        // Determine raster and RT blending modes from alpha mode
        // NOTE: Material workflow (metallic/roughness vs specular/glossiness vs anisotropy)
        // does NOT affect whether an object goes into graphics path - all materials are renderable
        if (materialRecord->cpu.isAlphaBlended())
        {
            return bits |
                   sceneSelectionMask(SceneSelectionBit::rasterTransparent) |
                   sceneSelectionMask(SceneSelectionBit::rtTransparent);
        }

        if (materialRecord->cpu.isAlphaMasked())
        {
            return bits |
                   sceneSelectionMask(SceneSelectionBit::rasterOpaque) |
                   sceneSelectionMask(SceneSelectionBit::alphaTest);
        }

        // All other cases (opaque, specular/glossiness, anisotropy, etc.) go to raster opaque
        return bits | sceneSelectionMask(SceneSelectionBit::rasterOpaque);
    }

void Scene::initializeInstanceRuntimeState(SceneInstanceRecord &instanceRecord)
{
        if (!instanceRecord.root.is_alive())
        {
            return;
        }

        auto rootTransform = instanceRecord.root.try_get<LocalTransform>();
        if (rootTransform == nullptr)
        {
            instanceRecord.root.set(LocalTransform{});
        }

        instanceRecord.root.set(WorldTransform{});
        instanceRecord.root.set(WorldBounds{});

        auto initializeChildren = [&](auto &&self, flecs::entity parent) -> void {
            auto childIterator = ecs_children(world_.c_ptr(), parent.id());
            while (ecs_children_next(&childIterator))
            {
                auto const indices = std::views::iota(0, childIterator.count);
                std::ranges::for_each(indices, [&](int index) {
                    auto child = flecs::entity{world_.c_ptr(), childIterator.entities[index]};

                    if (auto templateTransform = child.try_get<SceneTemplateNodeTransform>(); templateTransform != nullptr)
                    {
                        child.set(LocalTransform{.value = detail::toGlmMat4(templateTransform->localTransform)});
                    }
                    else if (child.try_get<LocalTransform>() == nullptr)
                    {
                        child.set(LocalTransform{});
                    }

                    child.set(WorldTransform{});

                    auto const renderMesh = meshHandleForEntity(child);
                    if (renderMesh.valid())
                    {
                        auto bounds = meshLocalBounds(renderMesh);
                        child.set(LocalBounds{.value = bounds});
                        if (bounds.valid())
                        {
                            child.add<StaticObject>();
                        }
                        else
                        {
                            child.remove<StaticObject>();
                        }
                    }
                    else if (child.try_get<LocalBounds>() == nullptr)
                    {
                        child.set(LocalBounds{});
                    }

                    child.set(WorldBounds{});
                    self(self, child);
                });
            }
        };

        initializeChildren(initializeChildren, instanceRecord.root);
    }

[[nodiscard]] nr::resource::Aabb Scene::updateHierarchyNode(flecs::entity entity, const glm::mat4 &parentWorld)
{
        auto localMatrix = glm::mat4{1.0f};

        if (auto local = entity.try_get<LocalTransform>(); local != nullptr)
        {
            if (detail::finiteMat4(local->value))
            {
                localMatrix = local->value;
            }
            else
            {
                entity.set(LocalTransform{});
            }
        }
        else if (auto templateTransform = entity.try_get<SceneTemplateNodeTransform>(); templateTransform != nullptr)
        {
            auto converted = detail::toGlmMat4(templateTransform->localTransform);
            if (detail::finiteMat4(converted))
            {
                localMatrix = converted;
                entity.set(LocalTransform{.value = converted});
            }
            else
            {
                entity.set(LocalTransform{});
            }
        }
        else
        {
            entity.set(LocalTransform{});
        }

        auto const worldMatrix = parentWorld * localMatrix;
        entity.set(WorldTransform{.value = worldMatrix});

        auto aggregateWorldBounds = nr::resource::Aabb{};

        auto localBounds = nr::resource::Aabb{};
        if (auto local = entity.try_get<LocalBounds>(); local != nullptr)
        {
            localBounds = local->value;
        }

        if (!localBounds.valid())
        {
            auto const renderMesh = meshHandleForEntity(entity);
            if (renderMesh.valid())
            {
                localBounds = meshLocalBounds(renderMesh);
                if (localBounds.valid())
                {
                    entity.set(LocalBounds{.value = localBounds});
                }
            }
        }

        if (localBounds.valid())
        {
            aggregateWorldBounds.merge(detail::transformAabb(localBounds, worldMatrix));
        }

        auto childIterator = ecs_children(world_.c_ptr(), entity.id());
        while (ecs_children_next(&childIterator))
        {
            auto const indices = std::views::iota(0, childIterator.count);
            std::ranges::for_each(indices, [&](int index) {
                auto child = flecs::entity{world_.c_ptr(), childIterator.entities[index]};
                aggregateWorldBounds.merge(updateHierarchyNode(child, worldMatrix));
            });
        }

        entity.set(WorldBounds{.value = aggregateWorldBounds});
        return aggregateWorldBounds;
    }

void Scene::updateInstanceHierarchy(SceneInstanceRecord &instanceRecord)
{
        if (!instanceRecord.root.is_alive())
        {
            return;
        }

        auto parentWorld = glm::mat4{1.0f};
        auto const parentId = ecs_get_parent(world_.c_ptr(), instanceRecord.root.id());
        if (parentId != 0)
        {
            auto parentEntity = flecs::entity{world_.c_ptr(), parentId};
            if (auto parentTransform = parentEntity.try_get<WorldTransform>(); parentTransform != nullptr && detail::finiteMat4(parentTransform->value))
            {
                parentWorld = parentTransform->value;
            }
        }

        [[maybe_unused]] auto const instanceBounds = updateHierarchyNode(instanceRecord.root, parentWorld);
    }

[[nodiscard]] bool Scene::buildTemplateHierarchy(
        SceneTemplateHandle templateHandle,
        SceneTemplateRecord &templateRecord,
        const nr::load::SceneAsset &sceneAsset,
        std::span<const nr::resource::MeshHandle> meshHandlesBySource,
        std::span<const nr::resource::MaterialHandle> materialHandlesBySource,
        std::span<const nr::resource::CameraAssetHandle> cameraHandlesBySource,
        std::span<const nr::resource::LightAssetHandle> lightHandlesBySource)
{
        (void)materialHandlesBySource;

        templateRecord.prefabEntities.clear();
        if (templateRecord.prefabRoot.is_alive())
        {
            templateRecord.prefabEntities.push_back(templateRecord.prefabRoot.id());
        }

        if (sceneAsset.nodes.empty())
        {
            templateRecord.templateNodeCount = 0;
            templateRecord.templateMeshBindingCount = 0;
            templateRecord.templateCameraBindingCount = 0;
            templateRecord.templateLightBindingCount = 0;
            return true;
        }

        if (sceneAsset.rootNodeIndex == nr::load::invalidIndex || sceneAsset.rootNodeIndex >= sceneAsset.nodes.size())
        {
            reportImport<nr::LogLevel::error>(
                ImportStage::templateRegistration,
                "SceneAsset root node index is invalid for template hierarchy construction.",
                templateRecord.stableKey,
                sceneAsset.rootNodeIndex);
            return false;
        }

        auto visited = std::vector<bool>(sceneAsset.nodes.size(), false);
        auto siblingNames = detail::SiblingNameTable{};
        auto meshBindingCount = std::size_t{0};
        auto cameraBindingCount = std::size_t{0};
        auto lightBindingCount = std::size_t{0};
        auto hierarchyHasError = false;
        auto const useParentStorage = useParentHierarchyStorage(sceneAsset, templateRecord.hierarchyPolicy);

        if (templateRecord.hierarchyPolicy == TemplateHierarchyPolicy::autoSelect)
        {
            reportImport<nr::LogLevel::info>(
                ImportStage::templateRegistration,
                std::format("Template '{}' auto-selected {} hierarchy storage.",
                            templateRecord.stableKey,
                            useParentStorage ? "Parent" : "ChildOf"),
                templateRecord.stableKey);
        }

        auto cameraIndicesByNode = std::map<std::uint32_t, std::vector<std::uint32_t>>{};
        auto const cameraIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.cameras.size()));
        std::ranges::for_each(cameraIndices, [&](std::uint32_t cameraIndex) {
            if (cameraIndex >= cameraHandlesBySource.size())
            {
                return;
            }

            auto cameraHandle = cameraHandlesBySource[cameraIndex];
            if (!cameraHandle.valid())
            {
                return;
            }

            auto const &cameraAsset = sceneAsset.cameras[cameraIndex];
            if (cameraAsset.nodeIndex == nr::load::invalidIndex || cameraAsset.nodeIndex >= sceneAsset.nodes.size())
            {
                reportImport<nr::LogLevel::warning>(
                    ImportStage::camera,
                    std::format("Camera '{}' resolved to invalid node index {} and will not be bound to template hierarchy.",
                                cameraAsset.name,
                                cameraAsset.nodeIndex),
                    {},
                    cameraIndex);
                return;
            }

            cameraIndicesByNode[cameraAsset.nodeIndex].push_back(cameraIndex);
        });

        auto lightIndicesByNode = std::map<std::uint32_t, std::vector<std::uint32_t>>{};
        auto const lightIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(sceneAsset.lights.size()));
        std::ranges::for_each(lightIndices, [&](std::uint32_t lightIndex) {
            if (lightIndex >= lightHandlesBySource.size())
            {
                return;
            }

            auto lightHandle = lightHandlesBySource[lightIndex];
            if (!lightHandle.valid())
            {
                return;
            }

            auto const &lightAsset = sceneAsset.lights[lightIndex];
            if (lightAsset.nodeIndex == nr::load::invalidIndex || lightAsset.nodeIndex >= sceneAsset.nodes.size())
            {
                reportImport<nr::LogLevel::warning>(
                    ImportStage::light,
                    std::format("Light '{}' resolved to invalid node index {} and will not be bound to template hierarchy.",
                                lightAsset.name,
                                lightAsset.nodeIndex),
                    {},
                    lightIndex);
                return;
            }

            lightIndicesByNode[lightAsset.nodeIndex].push_back(lightIndex);
        });

        auto buildNode = [&](auto &&self, std::uint32_t nodeIndex, flecs::entity parentEntity) -> void {
            if (nodeIndex >= sceneAsset.nodes.size())
            {
                hierarchyHasError = true;
                reportImport<nr::LogLevel::error>(
                    ImportStage::templateRegistration,
                    std::format("Template hierarchy references out-of-range node index {}.", nodeIndex),
                    templateRecord.stableKey,
                    nodeIndex);
                return;
            }

            visited[nodeIndex] = true;

            auto const &nodeAsset = sceneAsset.nodes[nodeIndex];
            auto const resolvedName = detail::makeDeterministicChildName(siblingNames, parentEntity.id(), nodeAsset.name);
            auto const entityName = detail::makeTemplateNodeEntityName(templateHandle, nodeIndex, resolvedName);

            auto nodeEntity = world_.entity(entityName.c_str());
            nodeEntity.add(EcsPrefab);
            attachHierarchyRelation(nodeEntity, parentEntity, useParentStorage);
            nodeEntity.set(SceneTemplateRef{templateHandle});
            templateRecord.prefabEntities.push_back(nodeEntity.id());
            nodeEntity.set(SceneTemplateNodeRef{
                .templateHandle = templateHandle,
                .sourceNodeIndex = nodeIndex,
                .sourceName = nodeAsset.name,
                .resolvedName = resolvedName,
            });
            nodeEntity.set(SceneTemplateNodeTransform{.localTransform = nodeAsset.localTransform});
            nodeEntity.set(LocalTransform{.value = detail::toGlmMat4(nodeAsset.localTransform)});
            nodeEntity.set(WorldTransform{});
            nodeEntity.set(LocalBounds{});
            nodeEntity.set(WorldBounds{});

            auto const meshSlots = std::views::iota(std::size_t{0}, nodeAsset.meshIndices.size());
            std::ranges::for_each(meshSlots, [&](std::size_t meshSlot) {
                auto const sourceMeshIndex = nodeAsset.meshIndices[meshSlot];
                if (sourceMeshIndex >= meshHandlesBySource.size())
                {
                    hierarchyHasError = true;
                    reportImport<nr::LogLevel::error>(
                        ImportStage::templateRegistration,
                        std::format("Node '{}' references out-of-range mesh index {}.", nodeAsset.name, sourceMeshIndex),
                        templateRecord.stableKey,
                        nodeIndex);
                    return;
                }

                auto meshHandle = meshHandlesBySource[sourceMeshIndex];
                if (!meshHandle.valid())
                {
                    hierarchyHasError = true;
                    reportImport<nr::LogLevel::error>(
                        ImportStage::templateRegistration,
                        std::format("Node '{}' references unresolved mesh index {}.", nodeAsset.name, sourceMeshIndex),
                        templateRecord.stableKey,
                        nodeIndex);
                    return;
                }

                auto materialHandle = nr::resource::MaterialHandle{};
                auto const *meshRecord = meshes_.tryGet(meshHandle);
                if (meshRecord != nullptr && meshRecord->cpuReady && !meshRecord->cpu.geometries.empty())
                {
                    materialHandle = meshRecord->cpu.geometries.front().material;
                }

                auto const meshEntityName = detail::makeTemplateMeshEntityName(
                    templateHandle,
                    nodeIndex,
                    static_cast<std::uint32_t>(meshSlot));

                auto meshEntity = world_.entity(meshEntityName.c_str());
                meshEntity.add(EcsPrefab);
                attachHierarchyRelation(meshEntity, nodeEntity, useParentStorage);
                meshEntity.set(SceneTemplateRef{templateHandle});
                templateRecord.prefabEntities.push_back(meshEntity.id());
                meshEntity.set(SceneTemplateMeshBindingRef{
                    .templateHandle = templateHandle,
                    .sourceNodeIndex = nodeIndex,
                    .sourceMeshIndex = sourceMeshIndex,
                    .mesh = meshHandle,
                    .material = materialHandle,
                });
                meshEntity.set(RenderableBinding{
                    .mesh = meshHandle,
                    .material = materialHandle,
                    .geometryCount = meshGeometryCount(meshHandle),
                });
                meshEntity.set(SceneSelectionBits{
                    .value = defaultSelectionBits(materialHandle),
                });
                meshEntity.set(ScenePartitionId{.value = 0u});
                meshEntity.set(TlasBucketId{.value = 0u});
                meshEntity.set(LocalTransform{});
                meshEntity.set(WorldTransform{});
                meshEntity.set(LocalBounds{.value = meshLocalBounds(meshHandle)});
                meshEntity.set(WorldBounds{});

                ++meshBindingCount;
            });

            if (auto found = cameraIndicesByNode.find(nodeIndex); found != cameraIndicesByNode.end())
            {
                auto const cameraSlots = std::views::iota(std::size_t{0}, found->second.size());
                std::ranges::for_each(cameraSlots, [&](std::size_t cameraSlot) {
                    auto const sourceCameraIndex = found->second[cameraSlot];
                    auto const cameraHandle = cameraHandlesBySource[sourceCameraIndex];
                    if (!cameraHandle.valid())
                    {
                        return;
                    }

                    auto const cameraEntityName = detail::makeTemplateCameraEntityName(
                        templateHandle,
                        nodeIndex,
                        static_cast<std::uint32_t>(cameraSlot));

                    auto cameraEntity = world_.entity(cameraEntityName.c_str());
                    cameraEntity.add(EcsPrefab);
                    attachHierarchyRelation(cameraEntity, nodeEntity, useParentStorage);
                    cameraEntity.set(SceneTemplateRef{templateHandle});
                    templateRecord.prefabEntities.push_back(cameraEntity.id());
                    cameraEntity.set(SceneTemplateCameraBindingRef{
                        .templateHandle = templateHandle,
                        .sourceNodeIndex = nodeIndex,
                        .sourceCameraIndex = sourceCameraIndex,
                        .camera = cameraHandle,
                    });
                    cameraEntity.set(SceneCameraBinding{
                        .camera = cameraHandle,
                    });
                    cameraEntity.set(LocalTransform{});
                    cameraEntity.set(WorldTransform{});
                    cameraEntity.set(LocalBounds{});
                    cameraEntity.set(WorldBounds{});

                    ++cameraBindingCount;
                });
            }

            if (auto found = lightIndicesByNode.find(nodeIndex); found != lightIndicesByNode.end())
            {
                auto const lightSlots = std::views::iota(std::size_t{0}, found->second.size());
                std::ranges::for_each(lightSlots, [&](std::size_t lightSlot) {
                    auto const sourceLightIndex = found->second[lightSlot];
                    auto const lightHandle = lightHandlesBySource[sourceLightIndex];
                    if (!lightHandle.valid())
                    {
                        return;
                    }

                    auto const lightEntityName = detail::makeTemplateLightEntityName(
                        templateHandle,
                        nodeIndex,
                        static_cast<std::uint32_t>(lightSlot));

                    auto lightEntity = world_.entity(lightEntityName.c_str());
                    lightEntity.add(EcsPrefab);
                    attachHierarchyRelation(lightEntity, nodeEntity, useParentStorage);
                    lightEntity.set(SceneTemplateRef{templateHandle});
                    templateRecord.prefabEntities.push_back(lightEntity.id());
                    lightEntity.set(SceneTemplateLightBindingRef{
                        .templateHandle = templateHandle,
                        .sourceNodeIndex = nodeIndex,
                        .sourceLightIndex = sourceLightIndex,
                        .light = lightHandle,
                    });
                    lightEntity.set(LocalTransform{});
                    lightEntity.set(WorldTransform{});
                    lightEntity.set(LocalBounds{});
                    lightEntity.set(WorldBounds{});

                    ++lightBindingCount;
                });
            }

            std::ranges::for_each(nodeAsset.childIndices, [&](std::uint32_t childIndex) {
                if (childIndex >= sceneAsset.nodes.size())
                {
                    hierarchyHasError = true;
                    reportImport<nr::LogLevel::error>(
                        ImportStage::templateRegistration,
                        std::format("Node '{}' references out-of-range child index {}.", nodeAsset.name, childIndex),
                        templateRecord.stableKey,
                        nodeIndex);
                    return;
                }

                self(self, childIndex, nodeEntity);
            });
        };

        buildNode(buildNode, sceneAsset.rootNodeIndex, templateRecord.prefabRoot);

        auto const reachableNodeCount = static_cast<std::size_t>(std::ranges::count(visited, true));
        if (reachableNodeCount != sceneAsset.nodes.size())
        {
            reportImport<nr::LogLevel::warning>(
                ImportStage::templateRegistration,
                std::format("Template '{}' contains {} node(s), but only {} are reachable from root index {}.",
                            templateRecord.stableKey,
                            sceneAsset.nodes.size(),
                            reachableNodeCount,
                            sceneAsset.rootNodeIndex),
                templateRecord.stableKey,
                sceneAsset.rootNodeIndex);
        }

        templateRecord.templateNodeCount = reachableNodeCount;
        templateRecord.templateMeshBindingCount = meshBindingCount;
        templateRecord.templateCameraBindingCount = cameraBindingCount;
        templateRecord.templateLightBindingCount = lightBindingCount;

        return !hierarchyHasError;
    }

void Scene::destroyTemplatePrefabEntities(SceneTemplateRecord &templateRecord)
{
        auto const reverseEntities = std::views::reverse(templateRecord.prefabEntities);
        std::ranges::for_each(reverseEntities, [&](flecs::entity_t prefabEntityId) {
            if (prefabEntityId == 0)
            {
                return;
            }

            auto prefabEntity = flecs::entity{world_.c_ptr(), prefabEntityId};
            if (prefabEntity.is_alive())
            {
                prefabEntity.destruct();
            }
        });

        templateRecord.prefabEntities.clear();
        templateRecord.prefabRoot = {};
    }

[[nodiscard]] bool Scene::hasImportedCameraInActiveInstances() const
{
        auto hasImportedCamera = false;
        forEachImportedCameraInActiveInstances([&](flecs::entity, const SceneCameraBinding &) {
            if (hasImportedCamera)
            {
                return;
            }

            hasImportedCamera = true;
        });

        return hasImportedCamera;
    }

void Scene::destroyFallbackCameraInfrastructureIfUnused()
{
        auto const fallbackEntityAlive = fallbackCameraEntity_.is_alive();
        auto const fallbackHandleValid = fallbackCameraHandle_.valid();
        if (!fallbackEntityAlive && !fallbackHandleValid)
        {
            return;
        }

        if (templates_.size() > 0u || hasImportedCameraInActiveInstances())
        {
            return;
        }

        if (fallbackEntityAlive)
        {
            fallbackCameraEntity_.destruct();
        }
        fallbackCameraEntity_ = {};

        if (fallbackHandleValid)
        {
            collectUnusedCameraAsset(fallbackCameraHandle_);
        }
        fallbackCameraHandle_ = {};
    }

void Scene::syncFallbackCameraInfrastructure()
{
        if (templates_.size() > 0u && !hasImportedCameraInActiveInstances())
        {
            initializeFallbackCameraInfrastructure();
            return;
        }

        destroyFallbackCameraInfrastructureIfUnused();
    }

[[nodiscard]] flecs::entity Scene::makeTemplatePrefab(SceneTemplateHandle handle,
                                                   std::string_view stableKey,
                                                   std::string_view debugName)
{
        auto label = debugName.empty() ? stableKey : debugName;
        auto sanitized = detail::sanitizeEntityName(label);
        auto name = std::format("scene_template_{}_{}_{}", handle.slot, handle.generation, sanitized);
        auto prefab = world_.entity(name.c_str());
        prefab.add(EcsPrefab);
        return prefab;
    }

[[nodiscard]] flecs::entity Scene::makeInstanceEntity(SceneInstanceHandle handle, std::string_view templateStableKey)
{
        auto sanitized = detail::sanitizeEntityName(templateStableKey);
        auto name = std::format("scene_instance_{}_{}_{}", handle.slot, handle.generation, sanitized);
        return world_.entity(name.c_str());
    }

[[nodiscard]] nr::resource::CameraAsset Scene::makeFallbackCameraAsset()
{
        auto fallbackCamera = nr::resource::CameraAsset{};
        fallbackCamera.name = "scene_runtime_fallback_camera";
        return fallbackCamera;
    }

void Scene::initializeFallbackCameraInfrastructure()
{
        auto [cameraHandle, created] = cameras_.getOrCreate(std::string{kFallbackCameraStableKey},
                                                            [](nr::resource::CameraAssetHandle newHandle, const std::string &key) {
                                                                return CameraAssetRecord{
                                                                    .handle = newHandle,
                                                                    .stableKey = key,
                                                                };
                                                            });

        if (created)
        {
            cameraHandles_.push_back(cameraHandle);
        }

        if (auto *cameraRecord = cameras_.tryGet(cameraHandle); cameraRecord != nullptr)
        {
            cameraRecord->cpu = makeFallbackCameraAsset();
            cameraRecord->cpuReady = true;
            cameraRecord->uploadQueued = false;
            if (cameraRecord->cpuVersion == 0u)
            {
                cameraRecord->cpuVersion = 1u;
            }
        }

        fallbackCameraHandle_ = cameraHandle;
        if (!fallbackCameraEntity_.is_alive())
        {
            auto const fallbackEntityName = std::format("scene_runtime_fallback_camera_{}_{}",
                                                        cameraHandle.slot,
                                                        cameraHandle.generation);
            fallbackCameraEntity_ = world_.entity(fallbackEntityName.c_str());
        }

        fallbackCameraEntity_.set(SceneCameraBinding{
            .camera = cameraHandle,
            .synthetic = true,
        });
        fallbackCameraEntity_.set(LocalTransform{});
        fallbackCameraEntity_.set(WorldTransform{});
        fallbackCameraEntity_.set(LocalBounds{});
        fallbackCameraEntity_.set(WorldBounds{});
    }
} // namespace nr::scene
