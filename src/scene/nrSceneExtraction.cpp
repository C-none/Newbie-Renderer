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

[[nodiscard]] ScenePacketSet Scene::extractPackets(SceneExtractProfileHandle profile, const SceneExtractInput &input) const
{
    auto const *profileRecord = extractProfiles_.tryGet(profile);
    if (profileRecord == nullptr)
    {
        return {};
    }

    return extractPacketsDedicated(*profileRecord, input);
}

[[nodiscard]] std::optional<SceneResolvedCamera> Scene::tryGetPrimaryCamera(const std::optional<glm::uvec2> &viewportExtent) const
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

        if (!bestImported.has_value() || sourceCameraIndex < bestImported->sourceCameraIndex || (sourceCameraIndex == bestImported->sourceCameraIndex && entity.id() < bestImported->entity.id()))
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

[[nodiscard]] std::optional<SceneSampledTextureBinding> Scene::tryGetSampledTextureBinding(nr::resource::TextureHandle handle) const noexcept
{
    auto const *textureRecord = textures_.tryGet(handle);
    if (textureRecord == nullptr || textureRecord->gpuState != GpuResidencyState::resident || textureRecord->gpuVersion < textureRecord->cpuVersion || !textureRecord->gpu.has_value() || !textureRecord->gpu->image.valid())
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

[[nodiscard]] std::optional<SceneAccelerationStructureMeshSemanticKey> Scene::tryGetAccelerationStructureMeshSemanticKey(nr::resource::MeshHandle handle) const noexcept
{
    auto const *meshRecord = meshes_.tryGet(handle);
    if (!geometryAtlasReadyForUse() || meshRecord == nullptr || !meshRecord->cpuReady || meshRecord->gpuState != GpuResidencyState::resident || !meshRecord->gpu.has_value() || !geometryAtlas_.vertexBuffer.valid())
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
        return materialRecord != nullptr &&
               materialRecord->cpuReady &&
               (materialRecord->cpu.core.doubleSided ||
                (!materialRecord->cpu.unlit &&
                materialRecord->cpu.hasVolumeTransmissionBoundary()));
    };
    auto geometryPrimitiveCount = [&](const nr::resource::MeshGeometry &geometry) {
        return geometry.indexCount / 3u;
    };

    auto const instanceKeepsBackFaces = !meshRecord->cpu.geometries.empty() &&
                                        std::ranges::any_of(
                                            std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size())),
                                            [&](std::uint32_t geometryIndex) {
                                                auto const &geometry = meshRecord->cpu.geometries[geometryIndex];
                                                return geometryPrimitiveCount(geometry) > 0u &&
                                                       geometryKeepsBackFaces(geometry, geometryIndex);
                                            });
    if (instanceKeepsBackFaces)
    {
        semanticKey.instanceFlags =
            semanticKey.instanceFlags |
            vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable;
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
            materialRecord != nullptr &&
            materialRecord->cpuReady &&
            materialRecord->cpu.isAlphaMasked();
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
        auto const geometryIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
        std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) { appendGeometryKey(meshRecord->cpu.geometries[geometryIndex], geometryIndex); });
    }

    if (semanticKey.geometries.empty())
    {
        return std::nullopt;
    }

    return semanticKey;
}

[[nodiscard]] std::optional<SceneAccelerationStructureMesh> Scene::tryGetAccelerationStructureMesh(nr::resource::MeshHandle handle) const noexcept
{
    auto semanticKey = tryGetAccelerationStructureMeshSemanticKey(handle);
    if (!semanticKey.has_value())
    {
        return std::nullopt;
    }

    auto const *meshRecord = meshes_.tryGet(handle);
    if (meshRecord == nullptr || !meshRecord->cpuReady || meshRecord->gpuState != GpuResidencyState::resident || !meshRecord->gpu.has_value() || !geometryAtlas_.vertexBuffer.valid())
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

        auto const semanticGeometry = std::ranges::find(
            semanticKey->geometries,
            geometryIndex,
            &SceneAccelerationStructureGeometrySemanticKey::geometryIndex);
        nrAssert(
            semanticGeometry != semanticKey->geometries.end(),
            "AS mesh geometry must have a matching semantic-key entry.");

        mesh.geometries.push_back(SceneAccelerationStructureGeometry{
            .geometryIndex = geometryIndex,
            .indexed = indexed,
            .primitiveOffset = static_cast<vk::DeviceSize>(geometry.firstIndex) * (indexed ? sizeof(std::uint32_t) : sizeof(nr::resource::Vertex)),
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
        auto const geometryIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(meshRecord->cpu.geometries.size()));
        std::ranges::for_each(geometryIndices, [&](std::uint32_t geometryIndex) { appendGeometry(meshRecord->cpu.geometries[geometryIndex], geometryIndex); });
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

[[nodiscard]] float Scene::resolveProjectionAspectRatio(const nr::resource::CameraAsset &camera, const std::optional<glm::uvec2> &viewportExtent) noexcept
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

[[nodiscard]] glm::mat4 Scene::buildProjectionMatrix(const nr::resource::CameraAsset &camera, float aspectRatio) noexcept
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
        transposed[3] + transposed[0], transposed[3] - transposed[0], transposed[3] + transposed[1], transposed[3] - transposed[1], transposed[3] + transposed[2], transposed[3] - transposed[2],
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

[[nodiscard]] std::optional<SceneResolvedCamera> Scene::buildResolvedCamera(flecs::entity entity, nr::resource::CameraAssetHandle cameraHandle, bool fallback, const std::optional<glm::uvec2> &viewportExtent) const
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
    default:
        return std::nullopt;
    }
}

[[nodiscard]] const flecs::query<const RenderableBinding, const SceneSelectionBits, const ScenePartitionId, const TlasBucketId, const WorldTransform, const WorldBounds> &Scene::candidateQueryFor(ScenePacketDomain domain) const noexcept
{
    if (domain == ScenePacketDomain::rasterDraw)
    {
        return rasterCandidatesQuery_;
    }

    return rtCandidatesQuery_;
}

[[nodiscard]] ScenePacketSet Scene::extractPacketsDedicated(const SceneExtractProfileRecord &profileRecord, const SceneExtractInput &input) const
{
    auto packetSet = ScenePacketSet{};
    packetSet.revisions = revisionsSnapshot();
    packetSet.domain = profileRecord.domain;

    auto selectedFrustum = resolveExtractFrustum(input);
    // Domain readiness only depends on the candidate's mesh (and its material/
    // texture residency), so memoize per mesh within this extraction to avoid
    // repeating the deep material/texture residency walk for every instance that
    // shares a mesh. Residency is read live, preserving the extract-time contract.
    auto readinessMemo = std::map<nr::resource::MeshHandle, bool>{};
    auto appendCandidate = [&](flecs::entity entity, const RenderableBinding &binding, const SceneSelectionBits &selectionBits, const ScenePartitionId &partitionId, const TlasBucketId &tlasBucketId, const WorldTransform &worldTransform, const WorldBounds &worldBounds) {
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
            auto [memoIt, inserted] = readinessMemo.try_emplace(binding.mesh, false);
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
            appendPacketsForCandidate<ScenePacketDomain::rasterDraw>(packetSet, entity, binding, selectionBits.value, tlasBucketId, worldTransform, worldBounds);
            break;
        case ScenePacketDomain::rayTracingInstance:
            appendPacketsForCandidate<ScenePacketDomain::rayTracingInstance>(packetSet, entity, binding, selectionBits.value, tlasBucketId, worldTransform, worldBounds);
            break;
        case ScenePacketDomain::tlasBuildInput:
            appendPacketsForCandidate<ScenePacketDomain::tlasBuildInput>(packetSet, entity, binding, selectionBits.value, tlasBucketId, worldTransform, worldBounds);
            break;
        default:
            break;
        }
    };

    auto const &query = candidateQueryFor(profileRecord.domain);
    query.each(appendCandidate);

    lightCandidatesQuery_.each([&](flecs::entity entity, const SceneLightBinding &binding, const WorldTransform &worldTransform) {
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
        std::ranges::sort(packetSet.rasterDraws, [](const RasterDrawPacket &lhs, const RasterDrawPacket &rhs) { return lhs.sortKey < rhs.sortKey; });
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

[[nodiscard]] bool Scene::materialIsResident(nr::resource::MaterialHandle materialHandle) const noexcept
{
    auto const *materialRecord = materials_.tryGet(materialHandle);
    if (materialRecord == nullptr)
    {
        return false;
    }

    return materialRecord->gpuState == GpuResidencyState::resident && materialRecord->gpuVersion >= materialRecord->cpuVersion;
}

[[nodiscard]] bool Scene::textureIsResident(nr::resource::TextureHandle textureHandle) const noexcept
{
    auto const *textureRecord = textures_.tryGet(textureHandle);
    if (textureRecord == nullptr)
    {
        return false;
    }

    return textureRecord->gpuState == GpuResidencyState::resident && textureRecord->gpuVersion >= textureRecord->cpuVersion;
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

[[nodiscard]] std::optional<nr::resource::MaterialHandle> Scene::meshGeometryMaterial(nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex) const noexcept
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
        auto materialHandle = geometry.material;
        if (!materialHandle.valid())
        {
            return true;
        }

        if (!materialIsResident(materialHandle))
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

[[nodiscard]] bool Scene::renderableReadyForDomain(ScenePacketDomain domain, const RenderableBinding &binding) const noexcept
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

[[nodiscard]] std::uint64_t Scene::rasterSortKey(nr::resource::MeshHandle meshHandle, nr::resource::MaterialHandle materialHandle, std::uint64_t selectionBits, std::uint32_t geometryIndex, flecs::entity entity) const noexcept
{
    auto const pass = (selectionBits & sceneSelectionMask(SceneSelectionBit::rasterTransparent)) != 0 ? 1ull : 0ull;
    auto const pipelineFamily = (selectionBits & sceneSelectionMask(SceneSelectionBit::alphaTest)) != 0 ? 1ull : 0ull;

    auto const materialSlot = materialHandle.valid() ? static_cast<std::uint64_t>(materialHandle.slot) : ((1ull << 20u) - 1u);
    auto const meshSlot = meshHandle.valid() ? static_cast<std::uint64_t>(meshHandle.slot) : ((1ull << 18u) - 1u);
    auto const entityBits = static_cast<std::uint64_t>(entity.id()) & ((1ull << 10u) - 1u);

    return (pass << 63u) | (pipelineFamily << 62u) | ((materialSlot & ((1ull << 20u) - 1u)) << 42u) | ((meshSlot & ((1ull << 18u) - 1u)) << 24u) | ((static_cast<std::uint64_t>(geometryIndex) & ((1ull << 14u) - 1u)) << 10u) | entityBits;
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
