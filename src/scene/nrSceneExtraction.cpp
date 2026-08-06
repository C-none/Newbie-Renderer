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

namespace nr::scene
{
[[nodiscard]] SceneExtractProfileHandle Scene::registerExtractProfile(const SceneExtractProfileCreateInfo &createInfo)
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

[[nodiscard]] ScenePacketSet Scene::extractPackets(SceneExtractProfileHandle profile,
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

        if (!bestImported.has_value() || sourceCameraIndex < bestImported->sourceCameraIndex ||
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

[[nodiscard]] std::optional<std::reference_wrapper<const SceneTemplateRecord>> Scene::tryGetTemplate(
    SceneTemplateHandle handle) const noexcept
{
    return tryGetRecordRef(templates_, handle);
}

[[nodiscard]] std::optional<std::reference_wrapper<const SceneInstanceRecord>> Scene::tryGetInstance(
    SceneInstanceHandle handle) const noexcept
{
    return tryGetRecordRef(instances_, handle);
}

[[nodiscard]] std::optional<std::reference_wrapper<const MeshAssetRecord>> Scene::tryGetMeshAsset(
    nr::resource::MeshHandle handle) const noexcept
{
    return tryGetRecordRef(meshes_, handle);
}

[[nodiscard]] std::optional<std::reference_wrapper<const MaterialAssetRecord>> Scene::tryGetMaterialAsset(
    nr::resource::MaterialHandle handle) const noexcept
{
    return tryGetRecordRef(materials_, handle);
}

[[nodiscard]] std::optional<std::reference_wrapper<const TextureAssetRecord>> Scene::tryGetTextureAsset(
    nr::resource::TextureHandle handle) const noexcept
{
    return tryGetRecordRef(textures_, handle);
}

[[nodiscard]] std::optional<SceneSampledTextureBinding> Scene::tryGetSampledTextureBinding(
    nr::resource::TextureHandle handle) const noexcept
{
    auto const *textureRecord = textures_.tryGet(handle);
    if (textureRecord == nullptr || textureRecord->gpuState != GpuResidencyState::resident ||
        textureRecord->gpuVersion < textureRecord->cpuVersion || !textureRecord->gpu.has_value() ||
        !textureRecord->gpu->image.valid())
    {
        return std::nullopt;
    }

    auto const &image = textureRecord->gpu->image;
    return SceneSampledTextureBinding{
        .texture = handle,
        .descriptorIndex = sceneTextureId(handle),
        .image = std::cref(image),
        .layout = textureRecord->gpu->layout,
        .format = image.format(),
        .extent = image.extent(),
        .gpuVersion = textureRecord->gpuVersion,
    };
}

[[nodiscard]] std::optional<std::reference_wrapper<const CameraAssetRecord>> Scene::tryGetCameraAsset(
    nr::resource::CameraAssetHandle handle) const noexcept
{
    return tryGetRecordRef(cameras_, handle);
}

[[nodiscard]] std::optional<std::reference_wrapper<const LightAssetRecord>> Scene::tryGetLightAsset(
    nr::resource::LightAssetHandle handle) const noexcept
{
    return tryGetRecordRef(lights_, handle);
}

[[nodiscard]] std::optional<SceneBridgeGeometryBuffers> Scene::tryGetRasterGeometryBuffers() const noexcept
{
    if (!geometryAtlasReadyForUse() || !geometryAtlas_.vertexBuffer.valid())
    {
        return std::nullopt;
    }

    auto buffers = SceneBridgeGeometryBuffers{
        .vertexBuffer =
            SceneBridgeBufferBinding{
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

[[nodiscard]] std::optional<SceneBridgeDrawGeometry> Scene::tryResolveRasterDrawRange(
    nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex,
    const SceneBridgeGeometryBuffers &geometryBuffers) const noexcept
{
    auto const *meshRecord = meshes_.tryGet(meshHandle);
    if (meshRecord == nullptr || !meshRecord->cpuReady || meshRecord->gpuState != GpuResidencyState::resident ||
        meshRecord->gpuVersion < meshRecord->cpuVersion || !meshRecord->gpu.has_value() || !geometryBuffers.valid() ||
        geometryIndex >= meshRecord->cpu.geometries.size())
    {
        return std::nullopt;
    }

    auto const &meshGeometry = meshRecord->cpu.geometries[geometryIndex];
    auto const &atlas = meshRecord->gpu->atlas;
    auto const checkedUint32 = [](std::uint32_t base, std::uint32_t offset) -> std::optional<std::uint32_t> {
        auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
        if (value > std::numeric_limits<std::uint32_t>::max())
        {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    };
    auto const checkedInt32 = [](std::uint32_t base, std::uint32_t offset) -> std::optional<std::int32_t> {
        auto const value = static_cast<std::uint64_t>(base) + static_cast<std::uint64_t>(offset);
        if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()))
        {
            return std::nullopt;
        }
        return static_cast<std::int32_t>(value);
    };
    auto const rangeFits = [](std::uint32_t first, std::uint32_t count, std::uint32_t capacity) {
        return count > 0u && static_cast<std::uint64_t>(first) + static_cast<std::uint64_t>(count) <= capacity;
    };

    auto geometry = SceneBridgeDrawGeometry{
        .frontFace = meshRecord->cpu.clockwiseFrontFace ? vk::FrontFace::eClockwise : vk::FrontFace::eCounterClockwise,
    };
    if (atlas.indexCount > 0u)
    {
        if (!geometryBuffers.hasIndexBuffer() || atlas.vertexCount == 0u ||
            !rangeFits(meshGeometry.firstIndex, meshGeometry.indexCount, atlas.indexCount) ||
            meshGeometry.vertexOffset >= atlas.vertexCount)
        {
            return std::nullopt;
        }

        auto firstIndex = checkedUint32(atlas.indexBase, meshGeometry.firstIndex);
        auto vertexOffset = checkedInt32(atlas.vertexBase, meshGeometry.vertexOffset);
        if (!firstIndex.has_value() || !vertexOffset.has_value())
        {
            return std::nullopt;
        }

        geometry.firstIndex = *firstIndex;
        geometry.indexCount = meshGeometry.indexCount;
        geometry.vertexOffset = *vertexOffset;
        return geometry;
    }

    if (!rangeFits(meshGeometry.firstIndex, meshGeometry.indexCount, atlas.vertexCount))
    {
        return std::nullopt;
    }

    auto firstVertex = checkedUint32(atlas.vertexBase, meshGeometry.firstIndex);
    if (!firstVertex.has_value())
    {
        return std::nullopt;
    }

    geometry.firstVertex = *firstVertex;
    geometry.vertexCount = meshGeometry.indexCount;
    return geometry;
}

[[nodiscard]] std::optional<Scene::RasterMaterialResolution> Scene::resolveRasterMaterial(
    nr::resource::MaterialHandle materialHandle) const noexcept
{
    auto const *materialRecord = materials_.tryGet(materialHandle);
    if (materialRecord == nullptr || !materialRecord->cpuReady)
    {
        return std::nullopt;
    }

    auto resolution = RasterMaterialResolution{};
    resolution.raster.cullMode =
        materialRecord->cpu.core.doubleSided ? vk::CullModeFlagBits::eNone : vk::CullModeFlagBits::eBack;
    auto const slotIndices = std::views::iota(std::size_t{0}, materialRecord->cpu.textureSlots.size());
    auto valid = true;
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        if (!valid)
        {
            return;
        }

        auto const textureHandle = materialRecord->cpu.textureSlots[slotIndex].texture;
        if (!textureHandle.valid())
        {
            return;
        }

        auto binding = tryGetSampledTextureBinding(textureHandle);
        if (!binding.has_value() || binding->descriptorIndex > kMaxSceneTextureId)
        {
            valid = false;
            return;
        }

        resolution.textures.ids[slotIndex] = static_cast<SceneTextureId>(binding->descriptorIndex);
        auto [it, inserted] = resolution.textureHandlesById.try_emplace(binding->descriptorIndex, textureHandle);
        if (!inserted && it->second != textureHandle)
        {
            valid = false;
        }
    });
    if (!valid)
    {
        return std::nullopt;
    }

    auto const normalSlotIndex =
        nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal);
    auto const &normalSlot = materialRecord->cpu.textureSlots[normalSlotIndex];
    if (normalSlot.uvSet > 1u)
    {
        return std::nullopt;
    }

    resolution.textures.normal = SceneMaterialNormalTextureBinding{
        .textureId = resolution.textures.ids[normalSlotIndex],
        .uvSet = normalSlot.uvSet,
        .uvLinear = normalSlot.transform.linear,
        .uvOffset = normalSlot.transform.offset,
        .normalScale = materialRecord->cpu.core.normalScale,
    };
    return resolution;
}

[[nodiscard]] SceneGeometryAtlasBackingGeneration Scene::geometryAtlasBackingGeneration() const noexcept
{
    return geometryAtlas_.backingGeneration;
}

[[nodiscard]] SceneGeometryAtlasStats Scene::geometryAtlasStats() const noexcept
{
    return SceneGeometryAtlasStats{
        .vertex =
            SceneGeometryAtlasDomainStats{
                .capacityBytes = geometryAtlas_.vertexCapacityBytes,
                .highWaterBytes = geometryAtlas_.vertexHighWaterBytes,
                .reusableBytes = geometryAtlasReusableBytes(geometryAtlas_.vertexReusableRanges,
                                                            geometryAtlas_.vertexHighWaterBytes),
            },
        .index =
            SceneGeometryAtlasDomainStats{
                .capacityBytes = geometryAtlas_.indexCapacityBytes,
                .highWaterBytes = geometryAtlas_.indexHighWaterBytes,
                .reusableBytes =
                    geometryAtlasReusableBytes(geometryAtlas_.indexReusableRanges, geometryAtlas_.indexHighWaterBytes),
            },
    };
}

[[nodiscard]] std::optional<SceneAccelerationStructureMeshSemanticKey> Scene::
    tryGetAccelerationStructureMeshSemanticKey(nr::resource::MeshHandle handle) const noexcept
{
    auto const *meshRecord = meshes_.tryGet(handle);
    if (!geometryAtlasReadyForUse() || meshRecord == nullptr || !meshRecord->cpuReady ||
        meshRecord->gpuState != GpuResidencyState::resident || !meshRecord->gpu.has_value() ||
        !geometryAtlas_.vertexBuffer.valid())
    {
        return std::nullopt;
    }

    auto const &atlas = meshRecord->gpu->atlas;
    if (atlas.vertexCount == 0u)
    {
        return std::nullopt;
    }

    auto addressGeneration = SceneGeometryAtlasBackingGeneration{
        .vertex = geometryAtlas_.backingGeneration.vertex,
    };
    if (atlas.indexCount > 0u)
    {
        addressGeneration.index = geometryAtlas_.backingGeneration.index;
    }

    auto semanticKey = SceneAccelerationStructureMeshSemanticKey{
        .meshGpuVersion = meshRecord->gpuVersion,
        .atlasBackingGeneration = addressGeneration,
    };
    // Vulkan ray traversal treats a right-handed CCW triangle as front-facing when
    // viewed from its positive geometric-normal side. Invert only imported meshes
    // that explicitly declare clockwise front faces.
    if (meshRecord->cpu.clockwiseFrontFace)
    {
        semanticKey.instanceFlags = semanticKey.instanceFlags | vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing;
    }

    auto const geometryCount = meshRecord->cpu.geometries.empty() ? std::size_t{1} : meshRecord->cpu.geometries.size();
    semanticKey.geometries.reserve(geometryCount);

    auto materialRecordForGeometry = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
        auto const materialHandle = meshGeometryMaterial(handle, geometryIndex).value_or(geometry.material);
        return materialHandle.valid() ? materials_.tryGet(materialHandle) : nullptr;
    };
    auto geometryKeepsBackFaces = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
        auto const *materialRecord = materialRecordForGeometry(geometry, geometryIndex);
        return materialRecord != nullptr && materialRecord->cpuReady &&
               (materialRecord->cpu.core.doubleSided ||
                (!materialRecord->cpu.unlit && materialRecord->cpu.hasVolumeTransmissionBoundary()));
    };
    auto geometryPrimitiveCount = [&](const nr::resource::MeshGeometry &geometry) { return geometry.indexCount / 3u; };

    auto const instanceKeepsBackFaces =
        !meshRecord->cpu.geometries.empty() &&
        std::ranges::any_of(
            std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size())),
            [&](std::uint32_t geometryIndex) {
                auto const &geometry = meshRecord->cpu.geometries[geometryIndex];
                return geometryPrimitiveCount(geometry) > 0u && geometryKeepsBackFaces(geometry, geometryIndex);
            });
    if (instanceKeepsBackFaces)
    {
        semanticKey.instanceFlags =
            semanticKey.instanceFlags | vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
    }

    auto appendGeometryKey = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
        auto const primitiveCount = geometryPrimitiveCount(geometry);
        if (primitiveCount == 0u)
        {
            return;
        }

        auto flags = vk::GeometryFlagsKHR{};
        auto const *materialRecord = materialRecordForGeometry(geometry, geometryIndex);
        auto const alphaMasked =
            materialRecord != nullptr && materialRecord->cpuReady && materialRecord->cpu.isAlphaMasked();
        auto const keepsBackFaces = geometryKeepsBackFaces(geometry, geometryIndex);
        // Facing cull disable is an instance-wide Vulkan flag. In a mixed mesh, keep unrelated
        // single-sided geometries non-opaque so the shared any-hit policy can reject their back faces.
        if (!alphaMasked && (!instanceKeepsBackFaces || keepsBackFaces))
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
        auto const geometryIndices =
            std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
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
    if (meshRecord == nullptr || !meshRecord->cpuReady || meshRecord->gpuState != GpuResidencyState::resident ||
        !meshRecord->gpu.has_value() || !geometryAtlas_.vertexBuffer.valid())
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
        .vertexBuffer =
            SceneBridgeBufferBinding{
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

    auto const geometryCount = meshRecord->cpu.geometries.empty() ? std::size_t{1} : meshRecord->cpu.geometries.size();
    mesh.geometries.reserve(geometryCount);

    auto appendGeometry = [&](const nr::resource::MeshGeometry &geometry, std::uint32_t geometryIndex) {
        auto const indexed = atlas.indexCount > 0u;
        auto const primitiveCount = geometry.indexCount / 3u;
        if (primitiveCount == 0u)
        {
            return;
        }

        auto const semanticGeometry = std::ranges::find(semanticKey->geometries, geometryIndex,
                                                        &SceneAccelerationStructureGeometrySemanticKey::geometryIndex);
        nrAssert(semanticGeometry != semanticKey->geometries.end(),
                 "AS mesh geometry must have a matching semantic-key entry.");

        mesh.geometries.push_back(SceneAccelerationStructureGeometry{
            .geometryIndex = geometryIndex,
            .indexed = indexed,
            .primitiveOffset = static_cast<vk::DeviceSize>(geometry.firstIndex) *
                               (indexed ? sizeof(std::uint32_t) : sizeof(nr::resource::Vertex)),
            .firstVertex = indexed ? geometry.vertexOffset : 0u,
            .primitiveCount = primitiveCount,
            .geometryFlags = semanticGeometry->geometryFlags,
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
        auto const geometryIndices =
            std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
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

[[nodiscard]] std::optional<nr::resource::MeshHandle> Scene::findMeshHandleByStableKey(
    std::string_view stableKey) const noexcept
{
    return meshes_.findHandleByStableKey(stableKey);
}

[[nodiscard]] std::optional<nr::resource::MaterialHandle> Scene::findMaterialHandleByStableKey(
    std::string_view stableKey) const noexcept
{
    return materials_.findHandleByStableKey(stableKey);
}

[[nodiscard]] std::optional<nr::resource::TextureHandle> Scene::findTextureHandleByStableKey(
    std::string_view stableKey) const noexcept
{
    return textures_.findHandleByStableKey(stableKey);
}

[[nodiscard]] std::optional<nr::resource::CameraAssetHandle> Scene::findCameraHandleByStableKey(
    std::string_view stableKey) const noexcept
{
    return cameras_.findHandleByStableKey(stableKey);
}

[[nodiscard]] std::optional<nr::resource::LightAssetHandle> Scene::findLightHandleByStableKey(
    std::string_view stableKey) const noexcept
{
    return lights_.findHandleByStableKey(stableKey);
}

[[nodiscard]] bool Scene::matchesSelectionMask(std::uint64_t bits, const SceneSelectionMask &mask) noexcept
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

[[nodiscard]] bool Scene::intersectsFrustum(const nr::resource::Aabb &bounds, const SceneFrustum &frustum) noexcept
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

[[nodiscard]] std::optional<float> Scene::aspectRatioFromViewportExtent(
    const std::optional<glm::uvec2> &viewportExtent) noexcept
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
        if (!(std::isfinite(verticalFov) && verticalFov > kMinDepthRange &&
              verticalFov < (glm::pi<float>() - kMinDepthRange)))
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
        transposed[3] + transposed[0], transposed[3] - transposed[0], transposed[3] + transposed[1],
        transposed[3] - transposed[1], transposed[3] + transposed[2], transposed[3] - transposed[2],
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

[[nodiscard]] std::optional<SceneResolvedCamera> Scene::buildResolvedCamera(
    flecs::entity entity, nr::resource::CameraAssetHandle cameraHandle, bool fallback,
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
    if (auto worldTransform = entity.try_get<WorldTransform>();
        worldTransform != nullptr && detail::finiteMat4(worldTransform->value))
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
    default:
        return std::nullopt;
    }
}

[[nodiscard]] const flecs::query<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId,
                                 const TlasBucketId, const WorldTransform, const WorldBounds> &
Scene::candidateQueryFor(ScenePacketDomain domain) const noexcept
{
    if (domain == ScenePacketDomain::rasterDraw)
    {
        return rasterCandidatesQuery_;
    }

    return rtCandidatesQuery_;
}

[[nodiscard]] ScenePacketSet Scene::extractPacketsDedicated(const SceneExtractProfileRecord &profileRecord,
                                                            const SceneExtractInput &input) const
{
    auto packetSet = ScenePacketSet{};
    packetSet.revisions = revisionsSnapshot();
    packetSet.domain = profileRecord.domain;

    if (profileRecord.domain == ScenePacketDomain::rasterDraw && profileRecord.requireReadyForDomain)
    {
        if (auto geometryBuffers = tryGetRasterGeometryBuffers(); geometryBuffers.has_value())
        {
            packetSet.rasterGeometryBuffers = *geometryBuffers;
        }
    }

    auto selectedFrustum = resolveExtractFrustum(input);
    // A geometry without its own material resolves through the renderable's
    // fallback material, so readiness is shared only by candidates with the same
    // mesh and fallback-material pair. Residency is read live at extraction time.
    using ReadinessKey = std::pair<nr::resource::MeshHandle, nr::resource::MaterialHandle>;
    auto readinessMemo = std::map<ReadinessKey, bool>{};
    auto appendCandidate = [&](flecs::entity entity, const RenderableBinding &binding,
                               const SceneSelectionBits &selectionBits, const ScenePartitionId &partitionId,
                               const TlasBucketId &tlasBucketId, const WorldTransform &worldTransform,
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

        if (profileRecord.requireReadyForDomain)
        {
            auto [memoIt, inserted] = readinessMemo.try_emplace(ReadinessKey{binding.mesh, binding.material}, false);
            if (inserted)
            {
                memoIt->second = renderableReadyForDomain(profileRecord.domain, binding);
            }

            if (!memoIt->second)
            {
                return;
            }
        }

        if (selectedFrustum.has_value() && !intersectsFrustum(worldBounds.value, *selectedFrustum))
        {
            return;
        }

        switch (profileRecord.domain)
        {
        case ScenePacketDomain::rasterDraw:
            appendPacketsForCandidate<ScenePacketDomain::rasterDraw>(packetSet, entity, binding, selectionBits.value,
                                                                     profileRecord.requireReadyForDomain, tlasBucketId,
                                                                     worldTransform, worldBounds);
            break;
        case ScenePacketDomain::rayTracingInstance:
            appendPacketsForCandidate<ScenePacketDomain::rayTracingInstance>(
                packetSet, entity, binding, selectionBits.value, profileRecord.requireReadyForDomain, tlasBucketId,
                worldTransform, worldBounds);
            break;
        case ScenePacketDomain::tlasBuildInput:
            appendPacketsForCandidate<ScenePacketDomain::tlasBuildInput>(
                packetSet, entity, binding, selectionBits.value, profileRecord.requireReadyForDomain, tlasBucketId,
                worldTransform, worldBounds);
            break;
        default:
            break;
        }
    };

    auto const &query = candidateQueryFor(profileRecord.domain);
    query.each(appendCandidate);

    lightCandidatesQuery_.each(
        [&](flecs::entity entity, const SceneLightBinding &binding, const WorldTransform &worldTransform) {
            if (profileRecord.requireActiveInstances && !belongsToActiveInstance(entity))
            {
                return;
            }

            if (!binding.light.valid() || !detail::finiteMat4(worldTransform.value))
            {
                return;
            }

            auto const position = glm::vec3{worldTransform.value[3]};
            if (!nr::resource::math::finiteVec(position))
            {
                return;
            }

            auto direction = glm::vec3{worldTransform.value * glm::vec4{0.0f, 0.0f, -1.0f, 0.0f}};
            if (!nr::resource::math::finiteVec(direction) || glm::dot(direction, direction) <= 1.0e-8f)
            {
                direction = glm::vec3{0.0f, 0.0f, -1.0f};
            }
            else
            {
                direction = glm::normalize(direction);
            }

            packetSet.lights.push_back(SceneLightPacket{
                .entity = entity,
                .light = binding.light,
                .world = worldTransform.value,
                .position = position,
                .direction = direction,
                .stableInstanceId = static_cast<std::uint32_t>(entity.id()),
            });
        });

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
    // Active-instance membership is materialized as ActiveInstanceTag on every
    // entity of an active instance (see initializeInstanceRuntimeState), so this
    // is an O(1) archetype-level check instead of an O(depth) parent-chain walk.
    return entity.has<ActiveInstanceTag>();
}

[[nodiscard]] bool Scene::meshIsResident(nr::resource::MeshHandle meshHandle) const noexcept
{
    if (!geometryAtlasReadyForUse())
    {
        return false;
    }

    auto const *meshRecord = meshes_.tryGet(meshHandle);
    if (meshRecord == nullptr)
    {
        return false;
    }

    return meshRecord->gpuState == GpuResidencyState::resident && meshRecord->gpuVersion >= meshRecord->cpuVersion;
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

[[nodiscard]] bool Scene::materialTexturesReady(const nr::resource::Material &material,
                                                bool allowUnavailableAnisotropy) const noexcept
{
    auto const anisotropySlotIndex =
        nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::anisotropy);
    auto const slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    return std::ranges::all_of(slotIndices, [&](std::size_t slotIndex) {
        auto const &slot = material.textureSlots[slotIndex];
        auto textureHandle = slot.texture;
        if (!textureHandle.valid() || (allowUnavailableAnisotropy && slotIndex == anisotropySlotIndex))
        {
            return true;
        }

        return textureIsResident(textureHandle);
    });
}

[[nodiscard]] std::optional<nr::resource::MaterialHandle> Scene::meshGeometryMaterial(
    nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex) const noexcept
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
        auto materialHandle = geometry.material.valid() ? geometry.material : binding.material;
        if (!materialHandle.valid())
        {
            return false;
        }

        auto const *materialRecord = materials_.tryGet(materialHandle);
        if (materialRecord == nullptr || !materialRecord->cpuReady)
        {
            return false;
        }

        return materialTexturesReady(materialRecord->cpu, false);
    });
}

[[nodiscard]] bool Scene::renderableReadyForMeshOnlyDomain(const RenderableBinding &binding) const noexcept
{
    return meshIsResident(binding.mesh);
}

[[nodiscard]] bool Scene::renderableReadyForRayTracing(const RenderableBinding &binding) const noexcept
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
        auto materialHandle = geometry.material.valid() ? geometry.material : binding.material;
        if (!materialHandle.valid())
        {
            return true;
        }

        auto const *materialRecord = materials_.tryGet(materialHandle);
        if (materialRecord == nullptr || !materialRecord->cpuReady)
        {
            return false;
        }

        return materialTexturesReady(materialRecord->cpu, true);
    });
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
        return renderableReadyForRayTracing(binding);
    default:
        return false;
    }
}

[[nodiscard]] std::uint64_t Scene::rasterSortKey(nr::resource::MeshHandle meshHandle,
                                                 nr::resource::MaterialHandle materialHandle,
                                                 std::uint64_t selectionBits, std::uint32_t geometryIndex,
                                                 flecs::entity entity) const noexcept
{
    auto const pass = (selectionBits & sceneSelectionMask(SceneSelectionBit::rasterTransparent)) != 0 ? 1ull : 0ull;
    auto const pipelineFamily = (selectionBits & sceneSelectionMask(SceneSelectionBit::alphaTest)) != 0 ? 1ull : 0ull;

    auto const materialSlot =
        materialHandle.valid() ? static_cast<std::uint64_t>(materialHandle.slot) : ((1ull << 20u) - 1u);
    auto const meshSlot = meshHandle.valid() ? static_cast<std::uint64_t>(meshHandle.slot) : ((1ull << 18u) - 1u);
    auto const entityBits = static_cast<std::uint64_t>(entity.id()) & ((1ull << 10u) - 1u);

    return (pass << 63u) | (pipelineFamily << 62u) | ((materialSlot & ((1ull << 20u) - 1u)) << 42u) |
           ((meshSlot & ((1ull << 18u) - 1u)) << 24u) |
           ((static_cast<std::uint64_t>(geometryIndex) & ((1ull << 14u) - 1u)) << 10u) | entityBits;
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

} // namespace nr::scene
