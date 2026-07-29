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
[[nodiscard]] std::uint64_t nextSceneIdentity() noexcept
{
    static auto nextIdentity = std::atomic<std::uint64_t>{1u};
    auto const identity = nextIdentity.fetch_add(1u, std::memory_order_relaxed);
    nrAssert(identity != 0u, "Scene identity space exhausted.");
    return identity;
}

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
    world.component<ActiveInstanceTag>();
    world.component<SceneCameraBinding>();
    world.component<SceneLightBinding>();

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

} // namespace nr::scene::detail

namespace nr::scene
{
Scene::Scene(const SceneCreateInfo &createInfo) : device_(createInfo.device), identity_(detail::nextSceneIdentity()), uploadBudgetBytesPerFrame_(createInfo.uploadBudgetBytesPerFrame), cpuRetention_(createInfo.cpuRetention)
{
    detail::registerSceneComponents(world_);

    runtimeRootQuery_ = world_.query_builder<const SceneInstanceRef>().cached().build();

    rasterCandidatesQuery_ = world_.query_builder<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId, const TlasBucketId, const WorldTransform, const WorldBounds>().cached().without(EcsPrefab).build();

    rtCandidatesQuery_ = world_.query_builder<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId, const TlasBucketId, const WorldTransform, const WorldBounds>().cached().without(EcsPrefab).build();

    cameraCandidatesQuery_ = world_.query_builder<const SceneCameraBinding, const WorldTransform>().cached().without(EcsPrefab).build();

    lightCandidatesQuery_ = world_.query_builder<const SceneLightBinding, const WorldTransform>().cached().without(EcsPrefab).build();
}

Scene::~Scene() noexcept
{
    if (*device_.device != nullptr)
    {
        device_.waitIdle();
        if (device_.uploadReadbackContext_.has_value())
        {
            device_.uploadReadback().reclaimCompletedUploads();
        }
    }

    pendingGraphicsSyncBatches_.clear();
    submittedGraphicsSyncWork_.clear();
    submittedGeometryAtlasGrowWork_.clear();
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

void Scene::commitMutation(SceneRevisionMutation mutation) noexcept
{
    auto batch = nr::revision::RevisionBatch<SceneRtRevisionDomain, SceneRevisionMutation, SceneRevisionMutationPolicy>{revisions.get<SceneRtRevisionDomain>()};
    batch.apply(mutation);
    batch.commit();
}

void Scene::commitExternalMutation(SceneRevisionMutation mutation) noexcept
{
    commitMutation(mutation);
}

[[nodiscard]] SceneRevisionSnapshot Scene::revisionsSnapshot() const noexcept
{
    return SceneRevisionSnapshot{
        .sceneIdentity = identity_,
        .rt = revisions.get<SceneRtRevisionDomain>().snapshot(),
    };
}

[[nodiscard]] SceneTemplateHandle Scene::registerTemplate(const nr::load::SceneAsset &sceneAsset, const SceneTemplateCreateInfo &createInfo)
{
    hasImportErrors_ = false;
    auto bridgePlan = SceneBridge::buildPlan(sceneAsset);
    hasImportErrors_ = !bridgePlan.valid();

    auto templateStableKey = createInfo.stableKey.empty() ? sceneAsset.sourcePath.generic_string() : createInfo.stableKey;
    if (templateStableKey.empty())
    {
        reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, "Template stable key is empty. Provide SceneTemplateCreateInfo.stableKey or SceneAsset.sourcePath.");
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

    auto pinSet = buildTemplatePinSet(meshHandlesBySource, materialHandlesBySource, textureHandlesBySource, cameraHandlesBySource, lightHandlesBySource);

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
        reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, "Failed to create SceneTemplateRecord from slot-map storage.", templateStableKey);
        return {};
    }

    auto hierarchyBuilt = buildTemplateHierarchy(handle, *templateRecord, sceneAsset, meshHandlesBySource, materialHandlesBySource, cameraHandlesBySource, lightHandlesBySource);

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

    reportImport<nr::LogLevel::info>(ImportStage::templateRegistration,
                                     std::format("Registered template '{}' with {} node(s), {} mesh-binding entity(ies), {} camera-binding entity(ies) and {} light-binding entity(ies).", templateStableKey, templateRecord->templateNodeCount, templateRecord->templateMeshBindingCount,
                                                 templateRecord->templateCameraBindingCount, templateRecord->templateLightBindingCount),
                                     templateStableKey);

    commitMutation(SceneRevisionMutation::templateRegistered);
    return handle;
}

[[nodiscard]] SceneInstanceHandle Scene::instantiate(SceneTemplateHandle templateHandle, const SceneInstantiateInfo &createInfo)
{
    hasImportErrors_ = false;
    auto *templateRecord = templates_.tryGet(templateHandle);
    if (templateRecord == nullptr)
    {
        reportImport<nr::LogLevel::error>(ImportStage::instanceRegistration, "Cannot instantiate an unknown SceneTemplateHandle.");
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
            .expectedEntityCount = templateRecord->templateNodeCount + templateRecord->templateMeshBindingCount + templateRecord->templateCameraBindingCount + templateRecord->templateLightBindingCount + 1u,
        };
    });

    ++templateRecord->liveInstanceCount;
    if (auto *instanceRecord = instances_.tryGet(handle); instanceRecord != nullptr)
    {
        initializeInstanceRuntimeState(*instanceRecord);
        updateInstanceHierarchy(*instanceRecord);
    }

    syncFallbackCameraInfrastructure();
    commitMutation(SceneRevisionMutation::instanceAdded);
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
    commitMutation(SceneRevisionMutation::templateDestroyed);
    return DestroyTemplateResult::destroyed;
}

void Scene::destroyInstance(SceneInstanceHandle instanceHandle)
{
    auto *instanceRecord = instances_.tryGet(instanceHandle);
    if (instanceRecord == nullptr)
    {
        return;
    }

    if (auto *templateRecord = templates_.tryGet(instanceRecord->templateHandle); templateRecord != nullptr && templateRecord->liveInstanceCount > 0u)
    {
        --templateRecord->liveInstanceCount;
    }

    if (instanceRecord->root.is_alive())
    {
        instanceRecord->root.destruct();
    }

    instances_.erase(instanceHandle);
    syncFallbackCameraInfrastructure();
    commitMutation(SceneRevisionMutation::instanceRemoved);
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
    commitMutation(SceneRevisionMutation::simulationUpdated);
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

[[nodiscard]] bool Scene::useParentHierarchyStorage(const nr::load::SceneAsset &sceneAsset, TemplateHierarchyPolicy policy) noexcept
{
    switch (policy)
    {
    case TemplateHierarchyPolicy::preferParent:
        return true;
    case TemplateHierarchyPolicy::preferChildOf:
        return false;
    case TemplateHierarchyPolicy::autoSelect:
    default:
        break;
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

    if (sourceMaterial.alphaModeHint == nr::load::MaterialAlphaModeHint::blend || sourceMaterial.opacity < 1.0f)
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
    auto bits = sceneSelectionMask(SceneSelectionBit::rtMain) | sceneSelectionMask(SceneSelectionBit::shadowCaster);

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
        return bits | sceneSelectionMask(SceneSelectionBit::rasterTransparent) | sceneSelectionMask(SceneSelectionBit::rtTransparent);
    }

    if (materialRecord->cpu.isAlphaMasked())
    {
        return bits | sceneSelectionMask(SceneSelectionBit::rasterOpaque) | sceneSelectionMask(SceneSelectionBit::alphaTest);
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

    auto applyActiveTag = [active = instanceRecord.active](flecs::entity entity) {
        if (active)
        {
            entity.add<ActiveInstanceTag>();
        }
        else
        {
            entity.remove<ActiveInstanceTag>();
        }
    };

    applyActiveTag(instanceRecord.root);

    auto initializeChildren = [&](auto &&self, flecs::entity parent) -> void {
        auto childIterator = ecs_children(world_.c_ptr(), parent.id());
        while (ecs_children_next(&childIterator))
        {
            auto const indices = std::views::iota(0, childIterator.count);
            std::ranges::for_each(indices, [&](int index) {
                auto child = flecs::entity{world_.c_ptr(), childIterator.entities[index]};

                if (child.has<SceneInstanceRef>())
                {
                    // Nested instance root: it owns its own runtime state and
                    // active-instance tagging via its own initialization pass.
                    return;
                }

                applyActiveTag(child);

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

[[nodiscard]] bool Scene::buildTemplateHierarchy(SceneTemplateHandle templateHandle, SceneTemplateRecord &templateRecord, const nr::load::SceneAsset &sceneAsset, std::span<const nr::resource::MeshHandle> meshHandlesBySource, std::span<const nr::resource::MaterialHandle> materialHandlesBySource,
                                                 std::span<const nr::resource::CameraAssetHandle> cameraHandlesBySource, std::span<const nr::resource::LightAssetHandle> lightHandlesBySource)
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
        reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, "SceneAsset root node index is invalid for template hierarchy construction.", templateRecord.stableKey, sceneAsset.rootNodeIndex);
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
        reportImport<nr::LogLevel::info>(ImportStage::templateRegistration, std::format("Template '{}' auto-selected {} hierarchy storage.", templateRecord.stableKey, useParentStorage ? "Parent" : "ChildOf"), templateRecord.stableKey);
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
            reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' resolved to invalid node index {} and will not be bound to template hierarchy.", cameraAsset.name, cameraAsset.nodeIndex), {}, cameraIndex);
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
            reportImport<nr::LogLevel::warning>(ImportStage::light, std::format("Light '{}' resolved to invalid node index {} and will not be bound to template hierarchy.", lightAsset.name, lightAsset.nodeIndex), {}, lightIndex);
            return;
        }

        lightIndicesByNode[lightAsset.nodeIndex].push_back(lightIndex);
    });

    auto buildNode = [&](auto &&self, std::uint32_t nodeIndex, flecs::entity parentEntity) -> void {
        if (nodeIndex >= sceneAsset.nodes.size())
        {
            hierarchyHasError = true;
            reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, std::format("Template hierarchy references out-of-range node index {}.", nodeIndex), templateRecord.stableKey, nodeIndex);
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
                reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, std::format("Node '{}' references out-of-range mesh index {}.", nodeAsset.name, sourceMeshIndex), templateRecord.stableKey, nodeIndex);
                return;
            }

            auto meshHandle = meshHandlesBySource[sourceMeshIndex];
            if (!meshHandle.valid())
            {
                hierarchyHasError = true;
                reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, std::format("Node '{}' references unresolved mesh index {}.", nodeAsset.name, sourceMeshIndex), templateRecord.stableKey, nodeIndex);
                return;
            }

            auto materialHandle = nr::resource::MaterialHandle{};
            auto const *meshRecord = meshes_.tryGet(meshHandle);
            if (meshRecord != nullptr && meshRecord->cpuReady && !meshRecord->cpu.geometries.empty())
            {
                materialHandle = meshRecord->cpu.geometries.front().material;
            }

            auto const meshEntityName = detail::makeTemplateMeshEntityName(templateHandle, nodeIndex, static_cast<std::uint32_t>(meshSlot));

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

                auto const cameraEntityName = detail::makeTemplateCameraEntityName(templateHandle, nodeIndex, static_cast<std::uint32_t>(cameraSlot));

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

                auto const lightEntityName = detail::makeTemplateLightEntityName(templateHandle, nodeIndex, static_cast<std::uint32_t>(lightSlot));

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
                lightEntity.set(SceneLightBinding{
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
                reportImport<nr::LogLevel::error>(ImportStage::templateRegistration, std::format("Node '{}' references out-of-range child index {}.", nodeAsset.name, childIndex), templateRecord.stableKey, nodeIndex);
                return;
            }

            self(self, childIndex, nodeEntity);
        });
    };

    buildNode(buildNode, sceneAsset.rootNodeIndex, templateRecord.prefabRoot);

    auto const reachableNodeCount = static_cast<std::size_t>(std::ranges::count(visited, true));
    if (reachableNodeCount != sceneAsset.nodes.size())
    {
        reportImport<nr::LogLevel::warning>(ImportStage::templateRegistration, std::format("Template '{}' contains {} node(s), but only {} are reachable from root index {}.", templateRecord.stableKey, sceneAsset.nodes.size(), reachableNodeCount, sceneAsset.rootNodeIndex), templateRecord.stableKey,
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

[[nodiscard]] flecs::entity Scene::makeTemplatePrefab(SceneTemplateHandle handle, std::string_view stableKey, std::string_view debugName)
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
    auto [cameraHandle, created] = cameras_.getOrCreate(std::string{kFallbackCameraStableKey}, [](nr::resource::CameraAssetHandle newHandle, const std::string &key) {
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
        auto const fallbackEntityName = std::format("scene_runtime_fallback_camera_{}_{}", cameraHandle.slot, cameraHandle.generation);
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
