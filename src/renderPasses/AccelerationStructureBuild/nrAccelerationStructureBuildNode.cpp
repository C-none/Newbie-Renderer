module nr.renderPasses;
import dependency.math;
import dependency.vulkan;

import :accelerationStructureBuild;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.neuralAppearanceAsset;
import nr.utils;
import std;
import :nodeType;
import :rtHitSbtPlan;

namespace nr::renderPasses::detail
{
using GraphResourceHandle = nr::renderer::GraphResourceHandle;
using ResourceLifetime = nr::renderer::ResourceLifetime;
using BufferUsageIntent = nr::renderer::BufferUsageIntent;

inline constexpr vk::DeviceSize blasStorageOffsetAlignment = 256u;

struct BlasGeometrySignature
{
    bool indexed = false;
    vk::IndexType indexType = vk::IndexType::eUint32;
    vk::DeviceSize vertexStride = 0;
    std::uint32_t maxVertex = 0;
    std::uint32_t primitiveCount = 0;
    std::uint32_t firstVertex = 0;
    vk::DeviceSize primitiveOffset = 0;
    vk::GeometryFlagsKHR geometryFlags{};

    [[nodiscard]] bool operator==(const BlasGeometrySignature &) const = default;
};

struct BlasBuildSignature
{
    std::vector<BlasGeometrySignature> geometries{};

    [[nodiscard]] bool operator==(const BlasBuildSignature &) const = default;
};

struct CachedBlasBuildDesc
{
    nr::scene::SceneAccelerationStructureMesh sceneMesh{};
    std::vector<nr::rhi::BlasGeometryRecord> geometries{};
    BlasBuildSignature signature{};
    nr::rhi::AsBuildSizes sizes{};
    nr::scene::SceneAccelerationStructureMeshSemanticKey semanticKey{};
    bool valid = false;
};

struct BlasCacheEntry
{
    nr::rhi::AccelerationStructureResource accelerationStructure{};
    nr::renderer::RetainedAccelerationStructureState retainedState{};
    std::uint64_t gpuVersion = 0;
    std::uint64_t lastSeenFrameSerial = 0;
    std::uint64_t atlasGeneration = 0;
    vk::DeviceSize storageOffset = 0;
    vk::DeviceSize storageSize = 0;
    vk::DeviceSize buildScratchSize = 0;
    BlasBuildSignature buildSignature{};
    CachedBlasBuildDesc cachedBuild{};
};

struct RetiredBlasAccelerationStructure
{
    nr::rhi::AccelerationStructureResource accelerationStructure{};
    std::uint64_t retireAfterFrameSerial = 0;
};

struct RetiredBlasAtlasResources
{
    nr::rhi::Buffer storage{};
    std::vector<nr::rhi::AccelerationStructureResource> accelerationStructures{};
    std::uint64_t retireAfterFrameSerial = 0;
};

struct BlasStorageAtlas
{
    nr::rhi::Buffer buffer{};
    vk::DeviceSize capacityBytes = 0;
    vk::DeviceSize usedBytes = 0;
    std::uint64_t generation = 0;
    std::vector<RetiredBlasAtlasResources> retired{};
};

/**
 * @brief Device-local RT metadata tables shared by every frame in flight.
 *
 * The tables only describe scene structure, so they carry no per-frame data and
 * are stored once instead of once per frame slot. A structural plan change
 * allocates a replacement set and retires the previous one after the in-flight
 * frames that still reference it have completed.
 */
struct RtMetadataResidentSet
{
    nr::rhi::Buffer instances{};
    nr::rhi::Buffer geometries{};
    nr::rhi::Buffer materialHeaders{};
    nr::rhi::Buffer materialLayers{};
    nr::rhi::Buffer materialTextureRefs{};
    nr::rhi::Buffer neuralMaterialRefs{};
    nr::rhi::Buffer neuralModelParameters{};
    nr::rhi::Image neuralLatentTexture0{};
    nr::rhi::Image neuralLatentTexture1{};
    std::uint64_t planGeneration = 0u;

    [[nodiscard]] bool valid() const noexcept
    {
        return instances.valid() && geometries.valid() && materialHeaders.valid() && materialLayers.valid() &&
               materialTextureRefs.valid() && neuralMaterialRefs.valid() && neuralModelParameters.valid() &&
               neuralLatentTexture0.valid() && neuralLatentTexture1.valid();
    }
};

struct RetiredRtMetadataSet
{
    RtMetadataResidentSet metadata{};
    std::uint64_t retireAfterFrameSerial = 0;
};

struct FrameSlotAsResources
{
    nr::rhi::Buffer instanceBuffer{};
    vk::DeviceSize instanceBufferSize = 0;

    nr::rhi::Buffer scratchBuffer{};
    vk::DeviceSize scratchBufferSize = 0;

    nr::rhi::Buffer tlasStorage{};
    nr::rhi::AccelerationStructureResource tlas{};
    std::uint32_t tlasCapacity = 0;
};

struct BlasBuildWork
{
    std::reference_wrapper<const nr::rhi::AccelerationStructureResource> dst;
    std::reference_wrapper<const nr::rhi::Buffer> scratchBuffer;
    std::size_t geometryOffset = 0;
    std::size_t geometryCount = 0;
    vk::DeviceAddress scratchAddress = 0;
    vk::DeviceSize scratchSize = 0;
    nr::rhi::AsBuildOptions options{};
};

struct BlasBuildFrameData
{
    std::vector<nr::rhi::BlasGeometryRecord> geometries{};
    std::vector<BlasBuildWork> works{};
    vk::DeviceSize scratchAlignment = 1;
};

struct TlasBuildFrameData
{
    std::reference_wrapper<const nr::rhi::AccelerationStructureResource> dst;
    std::reference_wrapper<const nr::rhi::Buffer> instanceBuffer;
    std::reference_wrapper<const nr::rhi::Buffer> scratchBuffer;
    vk::DeviceAddress scratchAddress = 0;
    nr::rhi::TlasBuildInput buildInput{};
    nr::rhi::AsBuildOptions options{};
    vk::DeviceSize scratchAlignment = 1;
};

struct PendingBlasBuild
{
    nr::resource::MeshHandle mesh{};
    std::reference_wrapper<const CachedBlasBuildDesc> cachedBuild;
};

struct PlannedInstance
{
    nr::resource::MeshHandle mesh{};
    vk::AccelerationStructureInstanceKHR instance{};
};

struct PreparedAsFrame
{
    std::vector<nr::resource::MeshHandle> dirtyMeshes{};
    std::vector<PlannedInstance> instances{};
    nr::resource::MeshHandle rtAtlasMesh{};
    nr::rhi::TlasBuildInput tlasInput{};
    std::size_t frameSlot = 0u;
    bool available = false;
};

using AsStructuralRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;

using BlasRevisionProjection = nr::revision::RevisionProjection<nr::scene::SceneRtRevisionDomain::meshContent,
                                                                nr::scene::SceneRtRevisionDomain::meshLayout>;

using RtMaterialRevisionProjection = nr::revision::RevisionProjection<
    nr::scene::SceneRtRevisionDomain::materialBinding, nr::scene::SceneRtRevisionDomain::materialPayload,
    nr::scene::SceneRtRevisionDomain::textureBinding, nr::scene::SceneRtRevisionDomain::textureResidency>;

struct AsStructuralPacketKey
{
    std::uint64_t renderableId = 0u;
    nr::resource::MeshHandle mesh{};
    std::uint16_t tlasBucket = 0u;

    [[nodiscard]] bool operator==(const AsStructuralPacketKey &) const noexcept = default;
};

struct AsStructuralMeshSemanticEntry
{
    nr::resource::MeshHandle mesh{};
    nr::scene::SceneAccelerationStructureMeshSemanticKey semanticKey{};

    [[nodiscard]] bool operator==(const AsStructuralMeshSemanticEntry &) const noexcept = default;
};

// Artifacts are immutable after strict V2 loading, but their payload is not
// represented by the scene RT revision domains. Keep their complete binding
// identity in the structural key so a different resident payload cannot reuse
// stale neural descriptor resources.
struct NeuralArtifactStructuralIdentity
{
    nr::resource::MaterialHandle material{};
    std::uint32_t bindingSourceMaterialIndex = 0u;
    std::uint32_t artifactSourceMaterialIndex = 0u;
    std::uint32_t uvSet = 0u;
    std::array<float, 6u> uvAffine{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    std::array<std::byte, 32u> sourceSceneDigest{};
    std::array<std::byte, 32u> trainingProfileDigest{};
    std::array<std::byte, 32u> payloadDigest{};

    [[nodiscard]] bool operator==(const NeuralArtifactStructuralIdentity &) const noexcept = default;
};

struct AsStructuralPlanKey
{
    std::uint64_t sceneIdentity = 0u;
    AsStructuralRevisionProjection revisions{};
    std::vector<AsStructuralPacketKey> packets{};
    std::vector<AsStructuralMeshSemanticEntry> meshSemantics{};
    std::vector<NeuralArtifactStructuralIdentity> neuralArtifacts{};

    [[nodiscard]] bool operator==(const AsStructuralPlanKey &) const noexcept = default;
};

struct AppendedRtInstanceMetadata
{
    std::uint32_t instanceMetadataIndex = 0u;
    std::uint32_t hitRecordBase = 0u;
};

struct RtMetadataBuildState
{
    std::map<nr::resource::MaterialHandle, std::uint32_t> materialIndexByHandle{};
    std::vector<nr::scene::RtCompiledMaterial> materials{};
    std::vector<nr::scene::RtGeometryMetadata> geometries{};
    std::vector<nr::scene::RtInstanceMetadata> instances{};
};

struct RtNeuralMaterialTable
{
    std::vector<nr::shader::share::RtNeuralMaterialRef> refs{};
    std::shared_ptr<const nr::neuralAppearance::Artifact> artifact{};
};
static_assert(sizeof(nr::shader::share::RtNeuralMaterialRef) == 16u,
              "RT neural material reference ABI must stay four uint32 lanes.");

struct StaticRtInstanceTemplate
{
    std::size_t packetOrdinal = 0u;
    std::uint64_t renderableId = 0u;
    nr::resource::MeshHandle mesh{};
    std::uint16_t tlasBucket = 0u;
    vk::GeometryInstanceFlagsKHR baseFlags{};
    std::uint32_t hitRecordBase = 0u;
    std::uint32_t instanceMetadataIndex = 0u;
};

struct RtStructuralPlanCache
{
    std::optional<AsStructuralPlanKey> key{};
    RtMetadataBuildState metadata{};
    nr::scene::RtMaterialTable materialTable{};
    RtNeuralMaterialTable neuralMaterialTable{};
    std::shared_ptr<const SceneRtHitSbtPlan> hitSbtPlan{};
    std::vector<StaticRtInstanceTemplate> instances{};
    std::uint64_t generation = 0u;
};

struct RtMaterialCacheKey
{
    nr::resource::MaterialHandle material{};
    std::uint64_t materialCpuVersion = 0;
    nr::scene::SceneMaterialTextureIds textureIds{};
    std::array<std::uint64_t, nr::scene::sceneMaterialTextureSlotCount> textureGpuVersions{};
    RtMaterialRevisionProjection revisions{};

    [[nodiscard]] auto operator<=>(const RtMaterialCacheKey &) const = default;
};

struct RtMaterialCacheEntry
{
    nr::scene::RtCompiledMaterial compiled{};
    std::uint64_t lastSeenFrameSerial = 0;
};

struct AccelerationStructureBuildRuntimeCache
{
    BlasStorageAtlas blasAtlas{};
    std::map<nr::resource::MeshHandle, BlasCacheEntry> blasCache{};
    std::map<RtMaterialCacheKey, RtMaterialCacheEntry> rtMaterialCache{};
    std::vector<RetiredBlasAccelerationStructure> retiredBlas{};
    std::array<FrameSlotAsResources, nr::maxFrameInFlight> frameSlots{};
    RtMetadataResidentSet rtMetadata{};
    std::vector<RetiredRtMetadataSet> retiredRtMetadata{};
    RtStructuralPlanCache structuralPlan{};
    nr::rhi::AsBuildLimits limits{};
    std::uint64_t frameSerial = 0;
    std::uint64_t activeSceneIdentity = 0u;
    std::optional<BlasRevisionProjection> blasRevisions{};
    std::optional<std::uint64_t> neuralFallbackWarningGeneration{};
};

[[nodiscard]] vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    if (alignment <= 1u)
    {
        return value;
    }

    auto const remainder = value % alignment;
    return remainder == 0u ? value : value + (alignment - remainder);
}

[[nodiscard]] vk::TransformMatrixKHR packTransform(const DirectX::XMFLOAT4X4 &world) noexcept
{
    constexpr auto affineTolerance = 32.0f * std::numeric_limits<float>::epsilon();
    nrAssert(std::isfinite(world._11) && std::isfinite(world._12) && std::isfinite(world._13) &&
                 std::isfinite(world._14) && std::isfinite(world._21) && std::isfinite(world._22) &&
                 std::isfinite(world._23) && std::isfinite(world._24) && std::isfinite(world._31) &&
                 std::isfinite(world._32) && std::isfinite(world._33) && std::isfinite(world._34) &&
                 std::isfinite(world._41) && std::isfinite(world._42) && std::isfinite(world._43) &&
                 std::isfinite(world._44) && std::abs(world._14) <= affineTolerance &&
                 std::abs(world._24) <= affineTolerance && std::abs(world._34) <= affineTolerance &&
                 std::abs(world._44 - 1.0f) <= affineTolerance,
             "TLAS instance transforms must be finite affine row-vector matrices.");

    // Vulkan ray tracing consumes a column-vector 3x4 transform. Transpose the DirectX row-vector matrix while
    // packing its affine 4x3 payload so both APIs describe the same object-to-world transform.
    auto transform = vk::TransformMatrixKHR{};
    transform.matrix[0][0] = world._11;
    transform.matrix[0][1] = world._21;
    transform.matrix[0][2] = world._31;
    transform.matrix[0][3] = world._41;
    transform.matrix[1][0] = world._12;
    transform.matrix[1][1] = world._22;
    transform.matrix[1][2] = world._32;
    transform.matrix[1][3] = world._42;
    transform.matrix[2][0] = world._13;
    transform.matrix[2][1] = world._23;
    transform.matrix[2][2] = world._33;
    transform.matrix[2][3] = world._43;
    return transform;
}

[[nodiscard]] nr::rhi::AsBuildOptions staticBlasBuildOptions() noexcept
{
    return nr::rhi::AsBuildOptions{
        .buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
    };
}

[[nodiscard]] nr::rhi::AsBuildOptions tlasBuildOptions() noexcept
{
    return nr::rhi::AsBuildOptions{
        .buildFlags = vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace,
    };
}

[[nodiscard]] std::vector<nr::rhi::BlasGeometryRecord> makeBlasGeometryRecords(
    const nr::scene::SceneAccelerationStructureMesh &mesh)
{
    nrAssert(mesh.hasVertexBuffer(), "AS build mesh requires a vertex atlas buffer.");
    auto const &vertexBuffer = mesh.vertexBuffer.buffer->get();
    auto const vertexAddress = vertexBuffer.deviceAddress() + mesh.vertexByteOffset;
    auto const indexBuffer = mesh.hasIndexBuffer()
                                 ? std::optional{std::cref(mesh.indexBuffer.buffer->get())}
                                 : std::optional<std::reference_wrapper<const nr::rhi::Buffer>>{};
    auto const indexAddress = indexBuffer.has_value()
                                  ? indexBuffer->get().deviceAddress() + mesh.indexByteOffset
                                  : vk::DeviceAddress{0};

    auto records = std::vector<nr::rhi::BlasGeometryRecord>{};
    records.reserve(mesh.geometries.size());

    std::ranges::for_each(mesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        nrAssert(geometry.primitiveOffset <= std::numeric_limits<std::uint32_t>::max(),
                 "AS build geometry primitive offset exceeds the current RHI uint32 range.");
        auto const layout = nr::rhi::BlasGeometryLayout{
            .vertexStride = mesh.vertexStride,
            .indexType = geometry.indexed ? mesh.indexType : vk::IndexType::eNoneKHR,
            .maxVertex = mesh.maxVertex,
            .geometryFlags = geometry.geometryFlags,
        };
        auto const input = nr::rhi::BlasGeometryInput{
            .vertexAddress = vertexAddress,
            .indexAddress = geometry.indexed ? indexAddress : vk::DeviceAddress{0},
            .primitiveCount = geometry.primitiveCount,
            .firstVertex = geometry.firstVertex,
            .primitiveOffset = static_cast<std::uint32_t>(geometry.primitiveOffset),
        };
        records.push_back(nr::rhi::makeBlasTriangleGeometryRecord(
            nr::rhi::BlasTriangleGeometryBuffers{
                .vertex = std::cref(vertexBuffer),
                .index = geometry.indexed ? indexBuffer : std::nullopt,
            },
            layout, input));
    });

    return records;
}

[[nodiscard]] BlasBuildSignature makeBlasBuildSignature(const nr::scene::SceneAccelerationStructureMesh &mesh)
{
    auto signature = BlasBuildSignature{};
    signature.geometries.reserve(mesh.geometries.size());

    std::ranges::for_each(mesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        signature.geometries.push_back(BlasGeometrySignature{
            .indexed = geometry.indexed,
            .indexType = geometry.indexed ? mesh.indexType : vk::IndexType::eNoneKHR,
            .vertexStride = mesh.vertexStride,
            .maxVertex = mesh.maxVertex,
            .primitiveCount = geometry.primitiveCount,
            .firstVertex = geometry.firstVertex,
            .primitiveOffset = geometry.primitiveOffset,
            .geometryFlags = geometry.geometryFlags,
        });
    });

    return signature;
}

void retireBlasResources(AccelerationStructureBuildRuntimeCache &runtime, BlasCacheEntry &entry,
                         std::uint64_t retireDelay)
{
    if (!entry.accelerationStructure.valid())
    {
        return;
    }

    runtime.retiredBlas.push_back(RetiredBlasAccelerationStructure{
        .accelerationStructure = std::move(entry.accelerationStructure),
        .retireAfterFrameSerial = runtime.frameSerial + retireDelay,
    });
    entry.gpuVersion = 0;
    entry.atlasGeneration = 0;
    entry.storageOffset = 0;
    entry.storageSize = 0;
    entry.buildScratchSize = 0;
    entry.buildSignature = {};
    entry.retainedState.reset();
}

void reapRetiredResources(AccelerationStructureBuildRuntimeCache &runtime)
{
    std::erase_if(runtime.retiredBlas, [&](const RetiredBlasAccelerationStructure &retired) {
        return retired.retireAfterFrameSerial <= runtime.frameSerial;
    });
    std::erase_if(runtime.blasAtlas.retired, [&](const RetiredBlasAtlasResources &retired) {
        return retired.retireAfterFrameSerial <= runtime.frameSerial;
    });
    std::erase_if(runtime.retiredRtMetadata, [&](const RetiredRtMetadataSet &retired) {
        return retired.retireAfterFrameSerial <= runtime.frameSerial;
    });
}

void pruneBlasCache(AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::Scene &scene,
                    std::uint64_t unusedFrameRetireLatency, std::uint64_t retireDelay)
{
    auto it = runtime.blasCache.begin();
    while (it != runtime.blasCache.end())
    {
        auto const unusedTooLong = runtime.frameSerial > it->second.lastSeenFrameSerial &&
                                   (runtime.frameSerial - it->second.lastSeenFrameSerial) > unusedFrameRetireLatency;
        auto const meshMissing = !scene.tryGetMeshAsset(it->first).has_value();
        if (!meshMissing && !unusedTooLong)
        {
            ++it;
            continue;
        }

        retireBlasResources(runtime, it->second, retireDelay);
        it = runtime.blasCache.erase(it);
    }
}

void pruneRtMaterialCache(AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::Scene &scene,
                          std::uint64_t unusedFrameRetireLatency)
{
    auto it = runtime.rtMaterialCache.begin();
    while (it != runtime.rtMaterialCache.end())
    {
        auto const unusedTooLong = runtime.frameSerial > it->second.lastSeenFrameSerial &&
                                   (runtime.frameSerial - it->second.lastSeenFrameSerial) > unusedFrameRetireLatency;
        auto const materialMissing = !scene.tryGetMaterialAsset(it->first.material).has_value();
        if (!materialMissing && !unusedTooLong)
        {
            ++it;
            continue;
        }

        it = runtime.rtMaterialCache.erase(it);
    }
}

[[nodiscard]] vk::DeviceSize grownBlasAtlasCapacity(vk::DeviceSize currentCapacity,
                                                    vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    auto const maxCapacity = std::numeric_limits<vk::DeviceSize>::max();
    auto const doubledCapacity = currentCapacity > (maxCapacity / 2u) ? maxCapacity : currentCapacity * 2u;
    return std::max({requiredCapacity, doubledCapacity, minCapacity});
}

[[nodiscard]] vk::DeviceSize replacementBlasAtlasCapacity(vk::DeviceSize currentCapacity,
                                                          vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    return std::max({requiredCapacity, currentCapacity, minCapacity});
}

[[nodiscard]] vk::DeviceSize alignedBlasSliceEnd(vk::DeviceSize cursor, vk::DeviceSize size) noexcept
{
    return alignUp(cursor, blasStorageOffsetAlignment) + size;
}

[[nodiscard]] vk::DeviceSize requiredBlasAtlasBytes(
    std::span<const std::reference_wrapper<const PendingBlasBuild>> builds) noexcept
{
    auto cursor = vk::DeviceSize{0};
    std::ranges::for_each(builds, [&](std::reference_wrapper<const PendingBlasBuild> buildRef) {
        cursor = alignedBlasSliceEnd(cursor, buildRef.get().cachedBuild.get().sizes.accelerationStructureSize);
    });
    return cursor;
}

[[nodiscard]] bool cachedBlasBuildNeedsRefresh(const CachedBlasBuildDesc &cached,
                                               const nr::scene::SceneAccelerationStructureMeshSemanticKey &semanticKey)
{
    return !cached.valid || cached.semanticKey != semanticKey;
}

struct BlasBuildDirtyContext
{
    const BlasCacheEntry &entry;
    const CachedBlasBuildDesc &cached;
    const BlasStorageAtlas &atlas;
};

[[nodiscard]] bool blasEntryNeedsBuild(const BlasBuildDirtyContext &context) noexcept
{
    return !context.entry.accelerationStructure.valid() || !context.atlas.buffer.valid() ||
           context.entry.gpuVersion != context.cached.semanticKey.meshGpuVersion ||
           context.entry.atlasGeneration != context.atlas.generation ||
           context.entry.storageSize < context.cached.sizes.accelerationStructureSize ||
           context.entry.buildSignature != context.cached.signature;
}

[[nodiscard]] std::optional<std::reference_wrapper<const CachedBlasBuildDesc>> ensureCachedBlasBuild(
    const nr::scene::Scene &scene, nr::rhi::Device &device, nr::resource::MeshHandle meshHandle, BlasCacheEntry &entry)
{
    auto semanticKey = scene.tryGetAccelerationStructureMeshSemanticKey(meshHandle);
    if (!semanticKey.has_value())
    {
        return std::nullopt;
    }

    if (!cachedBlasBuildNeedsRefresh(entry.cachedBuild, *semanticKey))
    {
        return std::cref(entry.cachedBuild);
    }

    auto sceneMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
    if (!sceneMesh.has_value())
    {
        return std::nullopt;
    }
    nrAssert(sceneMesh->semanticKey == *semanticKey,
             "Scene acceleration-structure mesh semantic key changed while refreshing the BLAS build cache.");

    auto mesh = std::move(*sceneMesh);
    auto geometries = makeBlasGeometryRecords(mesh);
    auto const signature = makeBlasBuildSignature(mesh);
    auto const sizes = nr::rhi::queryBlasBuildSizes(
        device.device, std::span<const nr::rhi::BlasGeometryRecord>{geometries}, staticBlasBuildOptions());

    entry.cachedBuild = CachedBlasBuildDesc{
        .sceneMesh = std::move(mesh),
        .geometries = std::move(geometries),
        .signature = signature,
        .sizes = sizes,
        .semanticKey = *semanticKey,
        .valid = true,
    };
    return std::cref(entry.cachedBuild);
}

void retireCurrentBlasAtlas(AccelerationStructureBuildRuntimeCache &runtime, std::uint64_t retireDelay)
{
    auto retired = RetiredBlasAtlasResources{
        .storage = std::move(runtime.blasAtlas.buffer),
        .retireAfterFrameSerial = runtime.frameSerial + retireDelay,
    };

    std::ranges::for_each(runtime.blasCache, [&](auto &pair) {
        auto &entry = pair.second;
        if (entry.accelerationStructure.valid())
        {
            retired.accelerationStructures.push_back(std::move(entry.accelerationStructure));
        }
        entry.gpuVersion = 0;
        entry.atlasGeneration = 0;
        entry.storageOffset = 0;
        entry.storageSize = 0;
        entry.buildScratchSize = 0;
        entry.buildSignature = {};
        entry.retainedState.reset();
    });

    if (retired.storage.valid() || !retired.accelerationStructures.empty())
    {
        runtime.blasAtlas.retired.push_back(std::move(retired));
    }

    runtime.blasAtlas.usedBytes = 0;
}

void createBlasAtlas(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device,
                     vk::DeviceSize requiredCapacity, bool growCapacity)
{
    auto const capacity = growCapacity
                              ? grownBlasAtlasCapacity(runtime.blasAtlas.capacityBytes, requiredCapacity)
                              : replacementBlasAtlasCapacity(runtime.blasAtlas.capacityBytes, requiredCapacity);
    runtime.blasAtlas.buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(capacity, 1u),
                                      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                          vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly, "ASBuild.BLAS.Atlas");
    nrAssert(runtime.blasAtlas.buffer.valid(), "AccelerationStructureBuildNode failed to create BLAS storage atlas.");
    runtime.blasAtlas.capacityBytes = capacity;
    runtime.blasAtlas.usedBytes = 0;
    ++runtime.blasAtlas.generation;
}

[[nodiscard]] vk::DeviceSize allocateBlasAtlasSlice(AccelerationStructureBuildRuntimeCache &runtime,
                                                    vk::DeviceSize size)
{
    auto const offset = alignUp(runtime.blasAtlas.usedBytes, blasStorageOffsetAlignment);
    nrAssert(offset <= runtime.blasAtlas.capacityBytes && size <= runtime.blasAtlas.capacityBytes - offset,
             "AccelerationStructureBuildNode BLAS atlas allocation exceeds capacity.");
    runtime.blasAtlas.usedBytes = offset + size;
    return offset;
}

[[nodiscard]] BlasCacheEntry &ensureBlasEntry(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device,
                                              const PendingBlasBuild &pending, std::uint64_t retireDelay)
{
    auto [entryIt, inserted] = runtime.blasCache.try_emplace(pending.mesh);
    auto &entry = entryIt->second;
    auto const &cached = pending.cachedBuild.get();
    auto const needsNewStorage = inserted || blasEntryNeedsBuild(BlasBuildDirtyContext{
                                                 .entry = entry,
                                                 .cached = cached,
                                                 .atlas = runtime.blasAtlas,
                                             });

    if (needsNewStorage)
    {
        retireBlasResources(runtime, entry, retireDelay);
        entry.storageOffset = allocateBlasAtlasSlice(runtime, cached.sizes.accelerationStructureSize);
        entry.storageSize = cached.sizes.accelerationStructureSize;
        entry.atlasGeneration = runtime.blasAtlas.generation;
        entry.accelerationStructure = nr::rhi::AccelerationStructureResource::create(
            device.device, runtime.blasAtlas.buffer, entry.storageOffset, cached.sizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eBottomLevel, std::format("ASBuild.BLAS.mesh{}", pending.mesh.slot));
    }

    entry.gpuVersion = cached.semanticKey.meshGpuVersion;
    entry.atlasGeneration = runtime.blasAtlas.generation;
    entry.buildSignature = cached.signature;
    entry.buildScratchSize = cached.sizes.buildScratchSize;
    return entry;
}

void ensureScratchBuffer(nr::rhi::Device &device, FrameSlotAsResources &slot, vk::DeviceSize requiredSize)
{
    if (slot.scratchBuffer.valid() && slot.scratchBufferSize >= requiredSize)
    {
        return;
    }

    slot.scratchBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(requiredSize, 1u),
                                      vk::BufferUsageFlagBits::eStorageBuffer |
                                          vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly, "ASBuild.Scratch");
    nrAssert(slot.scratchBuffer.valid(), "AccelerationStructureBuildNode failed to create scratch buffer.");
    slot.scratchBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

void ensureInstanceBuffer(nr::rhi::Device &device, FrameSlotAsResources &slot, vk::DeviceSize requiredSize)
{
    if (slot.instanceBuffer.valid() && slot.instanceBufferSize >= requiredSize)
    {
        return;
    }

    slot.instanceBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(requiredSize, 1u),
                                      vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                                          vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::CpuToGpu, "ASBuild.TLAS.Instances");
    nrAssert(slot.instanceBuffer.valid(), "AccelerationStructureBuildNode failed to create TLAS instance buffer.");
    slot.instanceBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

[[nodiscard]] std::vector<std::uint32_t> rtMetadataQueueFamilyIndices(nr::rhi::Device &device)
{
    auto families = std::vector<std::uint32_t>{
        device.queueManager.transfer().queueFamilyIndex(),
        device.queueManager.graphics().queueFamilyIndex(),
        device.queueManager.compute().queueFamilyIndex(),
    };
    std::ranges::sort(families);
    auto const duplicates = std::ranges::unique(families);
    families.erase(duplicates.begin(), duplicates.end());
    return families;
}

[[nodiscard]] nr::rhi::Buffer createRtMetadataBuffer(nr::rhi::Device &device, vk::DeviceSize byteSize,
                                                     std::span<const std::uint32_t> queueFamilyIndices,
                                                     std::string_view debugName)
{
    // Device-local so ray-tracing hit shaders read the tables from VRAM instead of
    // host-visible memory. Uploads therefore go through the RHI staging ring.
    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = std::max<vk::DeviceSize>(byteSize, 1u);
    createInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst;
    if (queueFamilyIndices.size() > 1u)
    {
        createInfo.sharingMode = vk::SharingMode::eConcurrent;
        createInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(queueFamilyIndices.size());
        createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }

    auto buffer = device.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::GpuOnly, debugName);
    nrAssert(buffer.valid(), "AccelerationStructureBuildNode failed to create {}.", debugName);
    return buffer;
}

[[nodiscard]] nr::rhi::Buffer createRtNeuralBuffer(nr::rhi::Device &device, vk::DeviceSize byteSize,
                                                    std::string_view debugName)
{
    auto const createInfo = nr::rhi::makeBufferCreateInfo(
        std::max<vk::DeviceSize>(byteSize, 1u),
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst);
    return device.resourceFactory.createBuffer(createInfo, nr::rhi::MemoryUsage::GpuOnly, debugName);
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan makeRtNeuralUploadPlan(
    nr::rhi::Device &device, vk::AccessFlags2 destinationAccess)
{
    return nr::rhi::ops::BufferUploadOwnershipPlan{
        .releaseToDestination = nr::rhi::ops::makeQueueOwnershipTransfer(
            device.queueManager.transfer().queueFamilyIndex(), device.queueManager.graphics().queueFamilyIndex(),
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eTransfer,
                .access = vk::AccessFlagBits2::eTransferWrite,
            },
            nr::rhi::ops::QueueAccessScope{
                .stages = vk::PipelineStageFlagBits2::eAllCommands,
                .access = destinationAccess,
            }),
    };
}

template <typename T>
[[nodiscard]] std::uint64_t uploadRtMetadataTable(nr::rhi::Device &device, const nr::rhi::Buffer &buffer,
                                                  std::span<const T> values)
{
    if (values.empty())
    {
        return 0u;
    }

    auto const ticket = device.uploadReadback().uploadBuffer(std::as_bytes(values), buffer, 0);
    nrAssert(ticket.valid(), "AccelerationStructureBuildNode failed to upload an RT metadata table.");
    return ticket.signalValue;
}

void retireRtMetadata(AccelerationStructureBuildRuntimeCache &runtime, std::uint64_t retireDelay)
{
    auto retired = std::exchange(runtime.rtMetadata, {});
    if (!retired.valid())
    {
        return;
    }

    runtime.retiredRtMetadata.push_back(RetiredRtMetadataSet{
        .metadata = std::move(retired),
        .retireAfterFrameSerial = runtime.frameSerial + retireDelay,
    });
}

void ensureRtMetadataResidentSet(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device,
                                 const RtMetadataBuildState &metadata,
                                 const nr::scene::RtMaterialTable &materialTable,
                                 const RtNeuralMaterialTable &neuralMaterialTable, std::uint64_t planGeneration,
                                 std::uint64_t retireDelay)
{
    if (runtime.rtMetadata.planGeneration == planGeneration && runtime.rtMetadata.valid())
    {
        return;
    }

    auto const families = rtMetadataQueueFamilyIndices(device);
    auto const neuralModelParameters = neuralMaterialTable.artifact
                                           ? neuralMaterialTable.artifact->modelBytes()
                                           : std::span<const std::byte>{};
    auto const neuralLatent0 = neuralMaterialTable.artifact ? neuralMaterialTable.artifact->latentPlane(0u)
                                                             : std::span<const std::byte>{};
    auto const neuralLatent1 = neuralMaterialTable.artifact ? neuralMaterialTable.artifact->latentPlane(1u)
                                                             : std::span<const std::byte>{};
    auto const neutralModelParameters = std::array<std::byte, nr::neuralAppearance::v2ModelBytes>{};
    auto const neutralLatent = std::array<std::byte, nr::neuralAppearance::v2LatentPlaneBytes>{};
    auto const modelBytes = neuralModelParameters.empty() ? std::span<const std::byte>{neutralModelParameters}
                                                            : neuralModelParameters;
    auto const latent0Bytes = neuralLatent0.empty() ? std::span<const std::byte>{neutralLatent} : neuralLatent0;
    auto const latent1Bytes = neuralLatent1.empty() ? std::span<const std::byte>{neutralLatent} : neuralLatent1;
    nrAssert(modelBytes.size() == nr::neuralAppearance::v2ModelBytes,
             "RT neural material model parameter size must match the V2 ABI.");
    nrAssert(latent0Bytes.size() == nr::neuralAppearance::v2LatentPlaneBytes &&
                 latent1Bytes.size() == nr::neuralAppearance::v2LatentPlaneBytes,
             "RT neural material latent plane size must match the V2 ABI.");
    auto createNeuralLatentImage = [&](std::string_view debugName) {
        auto createInfo = nr::rhi::makeImageCreateInfo(
            vk::Format::eR16G16B16A16Sfloat, vk::Extent2D{64u, 64u},
            vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled);
        createInfo.sharingMode = vk::SharingMode::eExclusive;
        return device.resourceFactory.createImage(createInfo, nr::rhi::MemoryUsage::GpuOnly, debugName);
    };
    auto resident = RtMetadataResidentSet{
        .instances = createRtMetadataBuffer(device, metadata.instances.size() * sizeof(nr::scene::RtInstanceMetadata),
                                            families, "ASBuild.RT.InstanceMetadata"),
        .geometries = createRtMetadataBuffer(device, metadata.geometries.size() * sizeof(nr::scene::RtGeometryMetadata),
                                             families, "ASBuild.RT.GeometryMetadata"),
        .materialHeaders = createRtMetadataBuffer(
            device, materialTable.headers.size() * sizeof(nr::scene::RtMaterialHeader), families,
            "ASBuild.RT.MaterialHeaders"),
        .materialLayers = createRtMetadataBuffer(
            device, materialTable.layers.size() * sizeof(nr::scene::RtMaterialLayerRecord), families,
            "ASBuild.RT.MaterialLayers"),
        .materialTextureRefs = createRtMetadataBuffer(
            device, materialTable.textureRefs.size() * sizeof(nr::scene::RtMaterialTextureRef), families,
            "ASBuild.RT.MaterialTextureRefs"),
        .planGeneration = planGeneration,
    };

    auto signalValue = std::uint64_t{0};
    auto const trackSignal = [&signalValue](std::uint64_t value) noexcept {
        signalValue = std::max(signalValue, value);
    };
    trackSignal(uploadRtMetadataTable(device, resident.instances,
                                      std::span<const nr::scene::RtInstanceMetadata>{metadata.instances}));
    trackSignal(uploadRtMetadataTable(device, resident.geometries,
                                      std::span<const nr::scene::RtGeometryMetadata>{metadata.geometries}));
    trackSignal(uploadRtMetadataTable(device, resident.materialHeaders,
                                      std::span<const nr::scene::RtMaterialHeader>{materialTable.headers}));
    trackSignal(uploadRtMetadataTable(device, resident.materialLayers,
                                      std::span<const nr::scene::RtMaterialLayerRecord>{materialTable.layers}));
    trackSignal(uploadRtMetadataTable(device, resident.materialTextureRefs,
                                      std::span<const nr::scene::RtMaterialTextureRef>{materialTable.textureRefs}));

    if (signalValue > 0u)
    {
        // Structural plan changes are rare, so the transfer completion is awaited inline
        // instead of threading upload tickets through the graph submission.
        device.uploadReadback().waitUploadComplete(signalValue);
        device.uploadReadback().reclaimCompletedUploads();
    }

    auto const transferQueueFamily = device.queueManager.transfer().queueFamilyIndex();
    auto const graphicsQueueFamily = device.queueManager.graphics().queueFamilyIndex();
    auto const storageUploadPlan = makeRtNeuralUploadPlan(device, vk::AccessFlagBits2::eShaderStorageRead);
    auto const sampledUploadPlan = makeRtNeuralUploadPlan(device, vk::AccessFlagBits2::eShaderSampledRead);
    auto createNeuralResources = [&](RtMetadataResidentSet &target) {
        target.neuralMaterialRefs = createRtNeuralBuffer(
            device, neuralMaterialTable.refs.size() * sizeof(nr::shader::share::RtNeuralMaterialRef),
            "ASBuild.RT.NeuralMaterialRefs");
        target.neuralModelParameters =
            createRtNeuralBuffer(device, nr::neuralAppearance::v2ModelBytes, "ASBuild.RT.NeuralModelParameters");
        target.neuralLatentTexture0 = createNeuralLatentImage("ASBuild.RT.NeuralLatentTexture0");
        target.neuralLatentTexture1 = createNeuralLatentImage("ASBuild.RT.NeuralLatentTexture1");
        return target.neuralMaterialRefs.valid() && target.neuralModelParameters.valid() &&
               target.neuralLatentTexture0.valid() && target.neuralLatentTexture1.valid();
    };
    auto tryUploadNeuralResources = [&](RtMetadataResidentSet &target,
                                        std::span<const nr::shader::share::RtNeuralMaterialRef> refs,
                                        std::span<const std::byte> parameters, std::span<const std::byte> latent0,
                                        std::span<const std::byte> latent1) {
        if (!target.neuralMaterialRefs.valid() || !target.neuralModelParameters.valid() ||
            !target.neuralLatentTexture0.valid() || !target.neuralLatentTexture1.valid())
        {
            return false;
        }
        auto const bufferTickets = std::array{
            device.uploadReadback().uploadBuffer(std::as_bytes(refs), target.neuralMaterialRefs, 0, storageUploadPlan),
            device.uploadReadback().uploadBuffer(parameters, target.neuralModelParameters, 0, storageUploadPlan),
        };
        auto const imageTickets = std::array{
            device.uploadReadback().uploadImage(latent0, target.neuralLatentTexture0, vk::ImageLayout::eUndefined,
                                                 vk::ImageLayout::eShaderReadOnlyOptimal, sampledUploadPlan),
            device.uploadReadback().uploadImage(latent1, target.neuralLatentTexture1, vk::ImageLayout::eUndefined,
                                                 vk::ImageLayout::eShaderReadOnlyOptimal, sampledUploadPlan),
        };
        auto const uploadSignal = std::ranges::fold_left(
            bufferTickets | std::views::transform([](const nr::rhi::ops::BufferUploadTicket &ticket) {
                return ticket.signalValue;
            }), std::uint64_t{0u}, [](std::uint64_t maximum, std::uint64_t value) { return std::max(maximum, value); });
        auto const completedSignal = std::ranges::fold_left(
            imageTickets | std::views::transform([](const nr::rhi::ops::ImageUploadTicket &ticket) {
                return ticket.signalValue;
            }), uploadSignal, [](std::uint64_t maximum, std::uint64_t value) { return std::max(maximum, value); });
        auto const ticketsValid =
            std::ranges::all_of(bufferTickets, [](const nr::rhi::ops::BufferUploadTicket &ticket) {
                return ticket.valid();
            }) &&
            std::ranges::all_of(imageTickets, [](const nr::rhi::ops::ImageUploadTicket &ticket) {
                return ticket.valid();
            });
        if (!ticketsValid)
        {
            if (completedSignal > 0u)
            {
                device.uploadReadback().waitUploadComplete(completedSignal);
                device.uploadReadback().reclaimCompletedUploads();
            }
            return false;
        }
        if (transferQueueFamily == graphicsQueueFamily)
        {
            device.uploadReadback().waitUploadComplete(completedSignal);
            device.uploadReadback().reclaimCompletedUploads();
            return true;
        }

        auto commandPool = nr::rhi::CommandPool{device.device, graphicsQueueFamily,
                                                 vk::CommandPoolCreateFlagBits::eTransient};
        auto commandBuffers = commandPool.allocatePrimary(1u);
        auto &commandBuffer = commandBuffers.front();
        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        std::ranges::for_each(bufferTickets,
                              [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
                                  device.uploadReadback().recordAcquireBarrier(commandBuffer, ticket);
                              });
        std::ranges::for_each(imageTickets,
                              [&](const nr::rhi::ops::ImageUploadTicket &ticket) {
                                  device.uploadReadback().recordImageAcquireBarrier(commandBuffer, ticket);
                              });
        nr::rhi::CommandRecorder::end(commandBuffer);
        auto submission = nr::rhi::CommandBatch{};
        submission.addWait(device.uploadReadback().uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                           completedSignal);
        submission.addCommandBuffer(commandBuffer);
        auto fence = vk::raii::Fence(device.device, vk::FenceCreateInfo{});
        device.queueManager.graphics().submit(std::move(submission), std::cref(fence));
        auto const waitResult =
            device.device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max());
        device.uploadReadback().reclaimCompletedUploads();
        return waitResult == vk::Result::eSuccess;
    };

    auto const neutralRefs = std::vector<nr::shader::share::RtNeuralMaterialRef>{neuralMaterialTable.refs.size()};
    auto const artifactRequested = static_cast<bool>(neuralMaterialTable.artifact);
    auto neuralReady = createNeuralResources(resident) &&
                       tryUploadNeuralResources(resident,
                                                artifactRequested
                                                    ? std::span<const nr::shader::share::RtNeuralMaterialRef>{
                                                          neuralMaterialTable.refs}
                                                    : std::span<const nr::shader::share::RtNeuralMaterialRef>{neutralRefs},
                                                modelBytes, latent0Bytes, latent1Bytes);
    if (!neuralReady && artifactRequested)
    {
        if (runtime.neuralFallbackWarningGeneration != planGeneration)
        {
            nr::nrLog<nr::LogLevel::warning>(
                "RT neural artifact upload failed for structural revision {}; publishing analytic-material fallback.",
                planGeneration);
            runtime.neuralFallbackWarningGeneration = planGeneration;
        }
        resident.neuralMaterialRefs = {};
        resident.neuralModelParameters = {};
        resident.neuralLatentTexture0 = {};
        resident.neuralLatentTexture1 = {};
        neuralReady = createNeuralResources(resident) &&
                      tryUploadNeuralResources(resident, neutralRefs, std::span<const std::byte>{neutralModelParameters},
                                               std::span<const std::byte>{neutralLatent},
                                               std::span<const std::byte>{neutralLatent});
    }
    nrAssert(neuralReady,
             "AccelerationStructureBuildNode could not publish the mandatory neutral neural material fallback.");
    retireRtMetadata(runtime, retireDelay);
    runtime.rtMetadata = std::move(resident);
}

void ensureTlas(nr::rhi::Device &device, FrameSlotAsResources &slot, const nr::rhi::AsBuildSizes &sizes,
                std::uint32_t instanceCount)
{
    auto const needsCreate = !slot.tlas.valid() || !slot.tlasStorage.valid() ||
                             slot.tlasStorage.size() < sizes.accelerationStructureSize ||
                             slot.tlasCapacity < instanceCount;
    if (!needsCreate)
    {
        return;
    }

    auto newStorage = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(sizes.accelerationStructureSize, 1u),
                                      vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                                          vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly, "ASBuild.TLAS.Storage");
    nrAssert(newStorage.valid(), "AccelerationStructureBuildNode failed to create TLAS storage.");
    auto newTlas = nr::rhi::AccelerationStructureResource::create(
        device.device, newStorage, 0, sizes.accelerationStructureSize,
        vk::AccelerationStructureTypeKHR::eTopLevel, "ASBuild.TLAS");

    // The AS must be released before replacing its backing allocation.
    slot.tlas = {};
    slot.tlasStorage = std::move(newStorage);
    slot.tlas = std::move(newTlas);
    slot.tlasCapacity = instanceCount;
}

[[nodiscard]] GraphResourceHandle importBufferOnce(
    NodeBuildContext &context, std::map<const nr::rhi::Buffer *, GraphResourceHandle> &importedBuffers,
    const nr::rhi::Buffer &buffer, std::string_view debugName, ResourceLifetime lifetime,
    std::initializer_list<BufferUsageIntent> usageIntents)
{
    auto const *key = std::addressof(buffer);
    if (auto it = importedBuffers.find(key); it != importedBuffers.end())
    {
        return it->second;
    }

    auto resource = context.importBuffer(buffer, debugName, lifetime, usageIntents,
                                         nr::renderer::ownershipDomainFromQueue(context.queue));
    importedBuffers.emplace(key, resource);
    return resource;
}

[[nodiscard]] std::uint32_t stableTlasInstanceCustomIndex(const nr::scene::TlasBuildInputPacket &packet) noexcept
{
    auto value = static_cast<std::uint64_t>(packet.renderable.id());
    value ^= packet.mesh.packed() + 0x9E37'79B9'7F4A'7C15ull + (value << 6u) + (value >> 2u);
    value ^= value >> 33u;
    value *= 0xFF51'AFD7'ED55'8CCDull;
    value ^= value >> 33u;
    value *= 0xC4CE'B9FE'1A85'EC53ull;
    value ^= value >> 33u;
    return static_cast<std::uint32_t>(value & 0x00FF'FFFFu);
}

[[nodiscard]] std::uint32_t checkedRtMetadataUint32(vk::DeviceSize value, std::string_view label)
{
    nrAssert(value <= std::numeric_limits<std::uint32_t>::max(), "{} exceeds the RT metadata uint32 ABI range.", label);
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t rtGeometryPrimitiveElementOffset(
    const nr::scene::SceneAccelerationStructureMesh &sceneMesh,
    const nr::scene::SceneAccelerationStructureGeometry &geometry)
{
    auto const elementSize = geometry.indexed ? vk::DeviceSize{sizeof(std::uint32_t)} : sceneMesh.vertexStride;
    nrAssert(elementSize > 0u, "RT geometry metadata requires a non-zero vertex stride.");
    nrAssert(geometry.primitiveOffset % elementSize == 0u,
             "RT geometry primitive offset must align to its indexed/non-indexed element size.");
    return checkedRtMetadataUint32(geometry.primitiveOffset / elementSize, "RT geometry primitive element offset");
}

[[nodiscard]] std::uint32_t rtMeshVertexBase(const nr::scene::SceneAccelerationStructureMesh &sceneMesh)
{
    nrAssert(sceneMesh.vertexStride > 0u, "RT instance metadata requires a non-zero vertex stride.");
    nrAssert(sceneMesh.vertexByteOffset % sceneMesh.vertexStride == 0u,
             "RT instance vertex atlas offset must align to the vertex stride.");
    return checkedRtMetadataUint32(sceneMesh.vertexByteOffset / sceneMesh.vertexStride, "RT instance vertex base");
}

[[nodiscard]] std::uint32_t rtMeshIndexBase(const nr::scene::SceneAccelerationStructureMesh &sceneMesh)
{
    if (!sceneMesh.hasIndexBuffer())
    {
        return 0u;
    }

    nrAssert(sceneMesh.indexByteOffset % sizeof(std::uint32_t) == 0u,
             "RT instance index atlas offset must align to the uint32 index size.");
    return checkedRtMetadataUint32(sceneMesh.indexByteOffset / sizeof(std::uint32_t), "RT instance index base");
}

[[nodiscard]] nr::resource::MaterialHandle rtGeometryMaterialHandle(const nr::scene::Scene &scene,
                                                                    nr::resource::MeshHandle meshHandle,
                                                                    std::uint32_t geometryIndex)
{
    auto meshRecord = scene.tryGetMeshAsset(meshHandle);
    if (!meshRecord.has_value() || !meshRecord->get().cpuReady ||
        geometryIndex >= meshRecord->get().cpu.geometries.size())
    {
        return {};
    }

    return meshRecord->get().cpu.geometries[geometryIndex].material;
}

[[nodiscard]] RtMaterialCacheKey makeRtMaterialCacheKey(const nr::scene::Scene &scene,
                                                        nr::resource::MaterialHandle materialHandle,
                                                        const nr::scene::MaterialAssetRecord &materialRecord,
                                                        RtMaterialRevisionProjection revisions) noexcept
{
    auto key = RtMaterialCacheKey{
        .material = materialHandle,
        .materialCpuVersion = materialRecord.cpuVersion,
        .revisions = revisions,
    };

    auto const &material = materialRecord.cpu;
    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto textureHandle = material.textureSlots[slotIndex].texture;
        if (!textureHandle.valid())
        {
            return;
        }

        auto binding = scene.tryGetSampledTextureBinding(textureHandle);
        if (!binding.has_value())
        {
            return;
        }

        nrAssert(binding->descriptorIndex <= nr::scene::kMaxSceneTextureId, "RT material texture descriptor id {} exceeds packed uint16 id capacity {}.",
                             binding->descriptorIndex, nr::scene::kMaxSceneTextureId);
        key.textureIds[slotIndex] = static_cast<nr::scene::SceneTextureId>(binding->descriptorIndex);
        key.textureGpuVersions[slotIndex] = binding->gpuVersion;
    });

    return key;
}

[[nodiscard]] const nr::scene::RtCompiledMaterial &ensureCachedRtMaterial(
    AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::MaterialAssetRecord &materialRecord,
    RtMaterialCacheKey key)
{
    auto [entryIt, inserted] = runtime.rtMaterialCache.try_emplace(std::move(key));
    auto &entry = entryIt->second;
    entry.lastSeenFrameSerial = runtime.frameSerial;
    if (inserted)
    {
        entry.compiled = nr::scene::compileRtMaterial(materialRecord.cpu, entryIt->first.textureIds);
    }
    return entry.compiled;
}

void initializeRtMetadataBuildState(RtMetadataBuildState &state)
{
    if (!state.materials.empty())
    {
        return;
    }

    state.materials.push_back(nr::scene::makeFallbackRtMaterial());
}

[[nodiscard]] std::uint32_t ensureRtMaterialIndex(AccelerationStructureBuildRuntimeCache &runtime,
                                                  RtMetadataBuildState &state, const nr::scene::Scene &scene,
                                                  nr::resource::MaterialHandle materialHandle,
                                                  RtMaterialRevisionProjection revisions)
{
    if (!materialHandle.valid())
    {
        return nr::scene::kRtMaterialFallbackIndex;
    }

    if (auto found = state.materialIndexByHandle.find(materialHandle); found != state.materialIndexByHandle.end())
    {
        return found->second;
    }

    auto materialRecord = scene.tryGetMaterialAsset(materialHandle);
    if (!materialRecord.has_value() || !materialRecord->get().cpuReady)
    {
        return nr::scene::kRtMaterialFallbackIndex;
    }

    auto const key = makeRtMaterialCacheKey(scene, materialHandle, materialRecord->get(), revisions);
    auto const &compiled = ensureCachedRtMaterial(runtime, materialRecord->get(), key);
    auto const materialIndex = static_cast<std::uint32_t>(state.materials.size());
    state.materials.push_back(compiled);
    state.materialIndexByHandle.emplace(materialHandle, materialIndex);
    return materialIndex;
}

[[nodiscard]] AppendedRtInstanceMetadata appendRtInstanceMetadata(
    AccelerationStructureBuildRuntimeCache &runtime, RtMetadataBuildState &state, SceneRtHitSbtPlan &hitSbtPlan,
    std::map<RtHitPermutationKey, std::uint32_t> &hitPermutationLookup, const nr::scene::Scene &scene,
    const nr::scene::TlasBuildInputPacket &packet, const nr::scene::SceneAccelerationStructureMesh &sceneMesh,
    RtMaterialRevisionProjection materialRevisions)
{
    initializeRtMetadataBuildState(state);

    auto const geometryOffset = checkedRtMetadataUint32(state.geometries.size(), "RT geometry metadata offset");
    auto const geometryCount = checkedRtMetadataUint32(sceneMesh.geometries.size(), "RT instance geometry count");
    nrAssert(geometryOffset <= std::numeric_limits<std::uint32_t>::max() - geometryCount,
             "RT geometry metadata range exceeds the uint32 ABI.");

    auto geometryPermutationKeys = std::vector<RtHitPermutationKey>{};
    geometryPermutationKeys.reserve(sceneMesh.geometries.size());
    std::ranges::for_each(sceneMesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        auto const materialHandle = rtGeometryMaterialHandle(scene, packet.mesh, geometry.geometryIndex);
        auto const materialIndex = ensureRtMaterialIndex(runtime, state, scene, materialHandle, materialRevisions);
        nrAssert(materialIndex < state.materials.size(),
                 "RT material index must resolve before building hit SBT plan.");
        auto const &materialHeader = state.materials[materialIndex].header;
        auto const requiresMaterialPolicy =
            !static_cast<bool>(geometry.geometryFlags & vk::GeometryFlagBitsKHR::eOpaque);
        geometryPermutationKeys.push_back(
            makeRtHitPermutationKey(materialHeader.layerFlags, materialHeader.featureFlags, requiresMaterialPolicy));
        state.geometries.push_back(nr::scene::RtGeometryMetadata{
            .materialIndex = materialIndex,
            .geometryIndex = geometry.geometryIndex,
            .primitiveOffset = rtGeometryPrimitiveElementOffset(sceneMesh, geometry),
            .firstVertex = geometry.firstVertex,
            .primitiveCount = geometry.primitiveCount,
            .flags = geometry.indexed ? nr::scene::kRtGeometryFlagIndexed : nr::scene::RtGeometryFlag::none,
        });
    });

    auto const instanceMetadataIndex = static_cast<std::uint32_t>(state.instances.size());
    nrAssert(instanceMetadataIndex <= 0x00FF'FFFFu,
             "RT instance metadata index must fit the 24-bit Vulkan InstanceCustomIndex field.");
    state.instances.push_back(nr::scene::RtInstanceMetadata{
        .geometryOffset = geometryOffset,
        .geometryCount = geometryCount,
        .stableInstanceId = stableTlasInstanceCustomIndex(packet),
        .vertexBase = rtMeshVertexBase(sceneMesh),
        .indexBase = rtMeshIndexBase(sceneMesh),
        .vertexStride = checkedRtMetadataUint32(sceneMesh.vertexStride, "RT instance vertex stride"),
    });

    auto const hitRecordBase = appendRtHitSbtPlanInstance(
        hitSbtPlan, hitPermutationLookup, instanceMetadataIndex,
        std::span<const RtHitPermutationKey>{geometryPermutationKeys.data(), geometryPermutationKeys.size()});
    return AppendedRtInstanceMetadata{
        .instanceMetadataIndex = instanceMetadataIndex,
        .hitRecordBase = hitRecordBase,
    };
}

[[nodiscard]] nr::scene::RtMaterialTable makeRtMaterialTable(const RtMetadataBuildState &state)
{
    auto materialRefs =
        state.materials |
        std::views::transform([](const nr::scene::RtCompiledMaterial &material) { return std::cref(material); }) |
        std::ranges::to<std::vector>();
    return nr::scene::makeRtMaterialTable(std::span<const std::reference_wrapper<const nr::scene::RtCompiledMaterial>>{
        materialRefs.data(), materialRefs.size()});
}

[[nodiscard]] bool neuralP0MaterialEligible(const nr::resource::Material &material) noexcept
{
    auto const identityUv = [](const nr::resource::MaterialTextureTransform &transform) noexcept {
        return transform.linear.x == 1.0f && transform.linear.y == 0.0f && transform.linear.z == 0.0f &&
               transform.linear.w == 1.0f && transform.offset.x == 0.0f && transform.offset.y == 0.0f;
    };
    auto const slotPresent = [&](nr::resource::MaterialTextureSlotSemantic semantic) noexcept {
        return material.slot(semantic).texture.valid();
    };
    auto const &baseColor = material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor);
    return material.isOpaque() && material.core.baseColorFactor.w == 1.0f && !material.core.doubleSided && !material.unlit &&
           !material.volumeBoundary.has_value() && baseColor.texture.valid() && baseColor.uvSet == 0u &&
           identityUv(baseColor.transform) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::normal) && !material.clearcoat.has_value() &&
           !material.sheen.has_value() && !material.transmission.has_value() && !material.anisotropy.has_value() &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::clearcoat) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::clearcoatNormal) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::sheenColor) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::sheenRoughness) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::transmission) &&
           !slotPresent(nr::resource::MaterialTextureSlotSemantic::anisotropy);
}

[[nodiscard]] std::vector<NeuralArtifactStructuralIdentity> makeNeuralArtifactStructuralIdentities(
    const nr::scene::Scene &scene, const std::map<nr::resource::MeshHandle, PendingBlasBuild> &pendingByMesh)
{
    auto identitiesByMaterial = std::map<nr::resource::MaterialHandle, NeuralArtifactStructuralIdentity>{};
    std::ranges::for_each(pendingByMesh, [&](const auto &pendingEntry) {
        auto const &sceneMesh = pendingEntry.second.cachedBuild.get().sceneMesh;
        std::ranges::for_each(sceneMesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
            auto const materialHandle = rtGeometryMaterialHandle(scene, pendingEntry.first, geometry.geometryIndex);
            auto const materialRecord = scene.tryGetMaterialAsset(materialHandle);
            if (!materialRecord.has_value() || !materialRecord->get().neuralAppearance.has_value())
            {
                return;
            }

            auto const &binding = *materialRecord->get().neuralAppearance;
            if (!binding.artifact)
            {
                return;
            }

            auto const &contract = binding.artifact->bindingContract();
            identitiesByMaterial.emplace(materialHandle, NeuralArtifactStructuralIdentity{
                                                          .material = materialHandle,
                                                          .bindingSourceMaterialIndex = binding.sourceMaterialIndex,
                                                          .artifactSourceMaterialIndex = contract.sourceMaterialIndex,
                                                          .uvSet = contract.uvSet,
                                                          .uvAffine = contract.uvAffine,
                                                          .sourceSceneDigest = contract.sourceSceneDigest,
                                                          .trainingProfileDigest = contract.trainingProfileDigest,
                                                          .payloadDigest = binding.artifact->payloadDigest(),
                                                      });
        });
    });
    return identitiesByMaterial | std::views::values | std::ranges::to<std::vector>();
}

[[nodiscard]] RtNeuralMaterialTable makeRtNeuralMaterialTable(const RtMetadataBuildState &state,
                                                               const nr::scene::Scene &scene)
{
    auto table = RtNeuralMaterialTable{};
    table.refs.resize(state.materials.size());
    std::ranges::for_each(state.materialIndexByHandle, [&](const auto &entry) {
        auto const materialRecord = scene.tryGetMaterialAsset(entry.first);
        if (!materialRecord.has_value() || !materialRecord->get().neuralAppearance.has_value())
        {
            return;
        }

        auto const &binding = *materialRecord->get().neuralAppearance;
        if (!binding.artifact || !neuralP0MaterialEligible(materialRecord->get().cpu))
        {
            nr::nrLog<nr::LogLevel::warning>(
                "RT neural material binding '{}' is not eligible for the strict P0 base-surface contract; using analytic material.",
                materialRecord->get().stableKey);
            return;
        }
        nrAssert(binding.sourceMaterialIndex == 0u,
                 "RT neural material binding must target the P0 source material index 0.");
        nrAssert(!table.artifact || table.artifact == binding.artifact,
                 "P0 supports exactly one neural artifact per scene.");
        table.artifact = binding.artifact;
        table.refs[entry.second] = nr::shader::share::RtNeuralMaterialRef{
            .flags = nr::shader::share::RtNeuralMaterialFlag::enabled,
            .artifactIndex = 0u,
        };
    });
    return table;
}

[[nodiscard]] AsStructuralPlanKey makeAsStructuralPlanKey(
    const nr::scene::SceneRevisionSnapshot &revisions, std::span<const nr::scene::TlasBuildInputPacket> packets,
    const std::map<nr::resource::MeshHandle, PendingBlasBuild> &pendingByMesh, const nr::scene::Scene &scene)
{
    auto packetKeys = packets | std::views::transform([](const nr::scene::TlasBuildInputPacket &packet) {
                          return AsStructuralPacketKey{
                              .renderableId = static_cast<std::uint64_t>(packet.renderable.id()),
                              .mesh = packet.mesh,
                              .tlasBucket = packet.tlasBucket,
                          };
                      }) |
                      std::ranges::to<std::vector>();
    auto meshSemantics = pendingByMesh | std::views::transform([](const auto &entry) {
                             return AsStructuralMeshSemanticEntry{
                                 .mesh = entry.first,
                                 .semanticKey = entry.second.cachedBuild.get().semanticKey,
                             };
                         }) |
                         std::ranges::to<std::vector>();
    return AsStructuralPlanKey{
        .sceneIdentity = revisions.sceneIdentity,
        .revisions = AsStructuralRevisionProjection::capture(revisions.rt),
        .packets = std::move(packetKeys),
        .meshSemantics = std::move(meshSemantics),
        .neuralArtifacts = makeNeuralArtifactStructuralIdentities(scene, pendingByMesh),
    };
}

[[nodiscard]] vk::AccelerationStructureInstanceKHR makeTlasInstance(const nr::scene::TlasBuildInputPacket &packet,
                                                                    vk::GeometryInstanceFlagsKHR baseFlags,
                                                                    const nr::rhi::AccelerationStructureResource &blas,
                                                                    std::uint32_t logicalHitRecordBase,
                                                                    std::uint32_t logicalHitRecordCount,
                                                                    std::uint32_t instanceCustomIndex)
{
    auto const physicalHitRecordBase = rtPhysicalHitRecordIndex(logicalHitRecordBase, RtRayType::material);
    auto const physicalHitRecordCount = rtPhysicalHitRecordCount(logicalHitRecordCount);
    nrAssert(physicalHitRecordCount > 0u, "AS build requires at least one physical hit SBT record.");
    nrAssert(physicalHitRecordCount <= std::numeric_limits<std::uint32_t>::max(),
             "Physical RT hit SBT record count exceeds uint32 ABI.");
    nrAssert(physicalHitRecordBase < physicalHitRecordCount, "TLAS physical hit SBT record base {} exceeds physical hit SBT record count {}.",
                         physicalHitRecordBase, physicalHitRecordCount);
    nrAssert(physicalHitRecordBase <= 0x00FF'FFFFu,
             "TLAS physical hit SBT record base must fit the 24-bit Vulkan "
             "instanceShaderBindingTableRecordOffset field.");

    auto instance = vk::AccelerationStructureInstanceKHR{};
    instance.setTransform(packTransform(packet.world));
    // InstanceCustomIndex is a dense metadata index for RT shaders. The stable debug hash is stored in RtInstanceMetadata.
    instance.setInstanceCustomIndex(instanceCustomIndex);
    instance.setMask(packet.instanceMask & 0xFFu);
    instance.setInstanceShaderBindingTableRecordOffset(static_cast<std::uint32_t>(physicalHitRecordBase));
    // Vulkan RT determines triangle facing in object space. Instance transforms, including
    // negative-determinant transforms, do not change the source mesh winding policy.
    instance.setFlags(baseFlags);
    instance.setAccelerationStructureReference(blas.deviceAddress());
    return instance;
}

[[nodiscard]] std::vector<nr::rhi::BlasBatchBuildRecordInfo> makeBatchRecords(const BlasBuildFrameData &frameData)
{
    auto batch = std::vector<nr::rhi::BlasBatchBuildRecordInfo>{};
    batch.reserve(frameData.works.size());
    auto const geometrySpan = std::span<const nr::rhi::BlasGeometryRecord>{frameData.geometries};

    std::ranges::for_each(frameData.works, [&](const BlasBuildWork &work) {
        batch.push_back(nr::rhi::BlasBatchBuildRecordInfo{
            .build =
                nr::rhi::BlasGeometriesBuildRecordInfo{
                    .dst = work.dst.get(),
                    .geometries = geometrySpan.subspan(work.geometryOffset, work.geometryCount),
                    .scratchBuffer = work.scratchBuffer.get(),
                    .scratchAddress = work.scratchAddress,
                    .options = work.options,
                },
            .scratchSize = work.scratchSize,
        });
    });

    return batch;
}

[[nodiscard]] PreparedAsFrame prepareAsFrame(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device,
                                             const AccelerationStructureBuildNodeInput &input,
                                             const nr::renderer::NodeFrameParameters &frameParameters)
{
    auto prepared = PreparedAsFrame{};
    prepared.frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % runtime.frameSlots.size());
    ++runtime.frameSerial;
    reapRetiredResources(runtime);

    if (!frameParameters.scene.has_value() || !frameParameters.sceneTlasBuildInputs.has_value())
    {
        return prepared;
    }

    auto const &scene = frameParameters.scene->get();
    auto const &packets = frameParameters.sceneTlasBuildInputs->get();
    auto const revisions =
        frameParameters.sceneRevisions.valid() ? frameParameters.sceneRevisions : scene.revisionsSnapshot();
    auto const retireDelay = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);
    if (runtime.activeSceneIdentity != revisions.sceneIdentity)
    {
        retireCurrentBlasAtlas(runtime, retireDelay);
        runtime.blasCache.clear();
        runtime.rtMaterialCache.clear();
        runtime.structuralPlan = {};
        runtime.blasRevisions.reset();
        runtime.activeSceneIdentity = revisions.sceneIdentity;
    }
    auto const blasRevisions = BlasRevisionProjection::capture(revisions.rt);
    if (runtime.blasRevisions.has_value() && *runtime.blasRevisions != blasRevisions)
    {
        retireCurrentBlasAtlas(runtime, retireDelay);
        std::ranges::for_each(runtime.blasCache, [](auto &entry) { entry.second.cachedBuild = {}; });
    }
    runtime.blasRevisions = blasRevisions;
    pruneBlasCache(runtime, scene, input.unusedFrameRetireLatency, retireDelay);
    pruneRtMaterialCache(runtime, scene, input.unusedFrameRetireLatency);
    if (packets.empty())
    {
        return prepared;
    }

    auto pendingByMesh = std::map<nr::resource::MeshHandle, PendingBlasBuild>{};
    auto const stablePacketTopology =
        runtime.structuralPlan.key.has_value() &&
        runtime.structuralPlan.key->sceneIdentity == revisions.sceneIdentity &&
        runtime.structuralPlan.key->revisions == AsStructuralRevisionProjection::capture(revisions.rt) &&
        runtime.structuralPlan.key->packets.size() == packets.size() &&
        std::ranges::equal(runtime.structuralPlan.key->packets, packets,
                           [](const AsStructuralPacketKey &key, const nr::scene::TlasBuildInputPacket &packet) {
                               return key.renderableId == static_cast<std::uint64_t>(packet.renderable.id()) &&
                                      key.mesh == packet.mesh && key.tlasBucket == packet.tlasBucket;
                           });
    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        if (pendingByMesh.contains(packet.mesh))
        {
            runtime.blasCache.try_emplace(packet.mesh).first->second.lastSeenFrameSerial = runtime.frameSerial;
            return;
        }
        auto [entryIt, inserted] = runtime.blasCache.try_emplace(packet.mesh);
        auto cachedBuild = stablePacketTopology && entryIt->second.cachedBuild.valid
                               ? std::optional<std::reference_wrapper<const CachedBlasBuildDesc>>{std::cref(
                                     entryIt->second.cachedBuild)}
                               : ensureCachedBlasBuild(scene, device, packet.mesh, entryIt->second);
        if (!cachedBuild.has_value())
        {
            if (inserted)
            {
                runtime.blasCache.erase(entryIt);
            }
            return;
        }
        entryIt->second.lastSeenFrameSerial = runtime.frameSerial;
        pendingByMesh.emplace(packet.mesh, PendingBlasBuild{
                                               .mesh = packet.mesh,
                                               .cachedBuild = *cachedBuild,
                                           });
    });
    if (pendingByMesh.empty())
    {
        return prepared;
    }

    auto pendingRefs = pendingByMesh | std::views::values |
                       std::views::transform([](const PendingBlasBuild &pending) { return std::cref(pending); }) |
                       std::ranges::to<std::vector>();
    auto dirtyBuilds = pendingRefs | std::views::filter([&](std::reference_wrapper<const PendingBlasBuild> pendingRef) {
                           auto const &pending = pendingRef.get();
                           return blasEntryNeedsBuild(BlasBuildDirtyContext{
                               .entry = runtime.blasCache.at(pending.mesh),
                               .cached = pending.cachedBuild.get(),
                               .atlas = runtime.blasAtlas,
                           });
                       }) |
                       std::ranges::to<std::vector>();

    auto atlasCursor = runtime.blasAtlas.usedBytes;
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const PendingBlasBuild> pending) {
        atlasCursor = alignedBlasSliceEnd(atlasCursor, pending.get().cachedBuild.get().sizes.accelerationStructureSize);
    });
    auto const capacityOverflow = runtime.blasAtlas.buffer.valid() && atlasCursor > runtime.blasAtlas.capacityBytes;
    auto repackBuilds = std::map<nr::resource::MeshHandle, PendingBlasBuild>{};
    if (!runtime.blasAtlas.buffer.valid() || capacityOverflow)
    {
        repackBuilds = pendingByMesh;
        std::ranges::for_each(runtime.blasCache, [&](auto &pair) {
            if (repackBuilds.contains(pair.first))
            {
                return;
            }
            auto cachedBuild = ensureCachedBlasBuild(scene, device, pair.first, pair.second);
            if (cachedBuild.has_value())
            {
                repackBuilds.emplace(pair.first, PendingBlasBuild{
                                                     .mesh = pair.first,
                                                     .cachedBuild = *cachedBuild,
                                                 });
            }
        });
        auto repackRefs = repackBuilds | std::views::values |
                          std::views::transform([](const PendingBlasBuild &pending) { return std::cref(pending); }) |
                          std::ranges::to<std::vector>();
        auto const requiredBytes = requiredBlasAtlasBytes(repackRefs);
        retireCurrentBlasAtlas(runtime, retireDelay);
        createBlasAtlas(runtime, device, requiredBytes, capacityOverflow);
        dirtyBuilds = std::move(repackRefs);
    }
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const PendingBlasBuild> pending) {
        auto &entry = ensureBlasEntry(runtime, device, pending.get(), retireDelay);
        entry.lastSeenFrameSerial = runtime.frameSerial;
        prepared.dirtyMeshes.push_back(pending.get().mesh);
    });
    auto structuralKey = makeAsStructuralPlanKey(revisions, packets, pendingByMesh, scene);
    auto &structuralPlan = runtime.structuralPlan;
    if (!structuralPlan.key.has_value() || *structuralPlan.key != structuralKey)
    {
        auto metadata = RtMetadataBuildState{};
        auto hitSbtPlan = SceneRtHitSbtPlan{};
        auto hitPermutationLookup = std::map<RtHitPermutationKey, std::uint32_t>{};
        auto templates = std::vector<StaticRtInstanceTemplate>{};
        auto const materialRevisions = RtMaterialRevisionProjection::capture(revisions.rt);
        auto const packetOrdinals = std::views::iota(std::size_t{0u}, packets.size());
        std::ranges::for_each(packetOrdinals, [&](std::size_t packetOrdinal) {
            auto const &packet = packets[packetOrdinal];
            auto entryIt = runtime.blasCache.find(packet.mesh);
            auto pendingIt = pendingByMesh.find(packet.mesh);
            if (entryIt == runtime.blasCache.end() || !entryIt->second.accelerationStructure.valid() ||
                pendingIt == pendingByMesh.end())
            {
                return;
            }
            auto const &sceneMesh = pendingIt->second.cachedBuild.get().sceneMesh;
            auto const appended = appendRtInstanceMetadata(runtime, metadata, hitSbtPlan, hitPermutationLookup, scene,
                                                           packet, sceneMesh, materialRevisions);
            templates.push_back(StaticRtInstanceTemplate{
                .packetOrdinal = packetOrdinal,
                .renderableId = static_cast<std::uint64_t>(packet.renderable.id()),
                .mesh = packet.mesh,
                .tlasBucket = packet.tlasBucket,
                .baseFlags = sceneMesh.instanceFlags,
                .hitRecordBase = appended.hitRecordBase,
                .instanceMetadataIndex = appended.instanceMetadataIndex,
            });
        });
        if (templates.empty())
        {
            return prepared;
        }
        finalizeSceneRtHitSbtPlan(hitSbtPlan);
        nrAssert(hitSbtPlan.valid(), "AS build generated an invalid RT hit SBT plan.");
        auto const nextGeneration = structuralPlan.generation == std::numeric_limits<std::uint64_t>::max()
                                        ? 1u
                                        : structuralPlan.generation + 1u;
        auto materialTable = makeRtMaterialTable(metadata);
        auto neuralMaterialTable = makeRtNeuralMaterialTable(metadata, scene);
        structuralPlan = RtStructuralPlanCache{
            .key = std::move(structuralKey),
            .metadata = std::move(metadata),
            .materialTable = std::move(materialTable),
            .neuralMaterialTable = std::move(neuralMaterialTable),
            .hitSbtPlan = std::make_shared<const SceneRtHitSbtPlan>(std::move(hitSbtPlan)),
            .instances = std::move(templates),
            .generation = nextGeneration,
        };
    }
    auto &instances = prepared.instances;
    nrAssert(structuralPlan.hitSbtPlan && structuralPlan.hitSbtPlan->valid(),
             "AS structural plan requires a valid logical hit SBT plan.");
    nrAssert(structuralPlan.hitSbtPlan->records.size() <=
                 static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
             "AS structural plan logical hit SBT record count exceeds uint32 ABI.");
    auto const logicalHitRecordCount = static_cast<std::uint32_t>(structuralPlan.hitSbtPlan->records.size());
    std::ranges::for_each(structuralPlan.instances, [&](const StaticRtInstanceTemplate &instanceTemplate) {
        nrAssert(instanceTemplate.packetOrdinal < packets.size(), "AS structural plan packet ordinal is out of range.");
        auto const &packet = packets[instanceTemplate.packetOrdinal];
        auto const entryIt = runtime.blasCache.find(instanceTemplate.mesh);
        nrAssert(entryIt != runtime.blasCache.end() && entryIt->second.accelerationStructure.valid(),
                 "AS structural plan requires a live BLAS cache entry.");
        instances.push_back(PlannedInstance{
            .mesh = packet.mesh,
            .instance = makeTlasInstance(packet, instanceTemplate.baseFlags, entryIt->second.accelerationStructure,
                                         instanceTemplate.hitRecordBase, logicalHitRecordCount,
                                         instanceTemplate.instanceMetadataIndex),
        });
    });
    if (instances.empty())
    {
        return prepared;
    }
    auto &slot = runtime.frameSlots[frameParameters.frameIndex % runtime.frameSlots.size()];
    auto const instanceBytes =
        static_cast<vk::DeviceSize>(instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
    ensureInstanceBuffer(device, slot, instanceBytes);
    auto instanceValues = instances |
                          std::views::transform([](const PlannedInstance &planned) { return planned.instance; }) |
                          std::ranges::to<std::vector>();
    slot.instanceBuffer.writeMappedAndFlush(std::span<const vk::AccelerationStructureInstanceKHR>{instanceValues});

    auto const &metadata = structuralPlan.metadata;
    auto const &materialTable = structuralPlan.materialTable;
    auto const &neuralMaterialTable = structuralPlan.neuralMaterialTable;
    ensureRtMetadataResidentSet(runtime, device, metadata, materialTable, neuralMaterialTable,
                                structuralPlan.generation, retireDelay);

    prepared.tlasInput = nr::rhi::TlasBuildInput{
        .instancesAddress = slot.instanceBuffer.deviceAddress(),
        .instanceCount = static_cast<std::uint32_t>(instances.size()),
    };
    auto const tlasSizes = nr::rhi::queryTlasBuildSizes(device.device, prepared.tlasInput, tlasBuildOptions());
    ensureTlas(device, slot, tlasSizes, prepared.tlasInput.instanceCount);
    auto blasScratchBytes = vk::DeviceSize{0u};
    std::ranges::for_each(prepared.dirtyMeshes, [&](nr::resource::MeshHandle mesh) {
        blasScratchBytes = alignUp(blasScratchBytes, runtime.limits.minScratchAlignment);
        blasScratchBytes += runtime.blasCache.at(mesh).cachedBuild.sizes.buildScratchSize;
    });
    ensureScratchBuffer(device, slot, std::max(blasScratchBytes, tlasSizes.buildScratchSize));
    prepared.rtAtlasMesh = instances.front().mesh;
    prepared.available = true;
    return prepared;
}

void declarePreparedAsFrame(nr::renderer::NodeBuildContext &context, AccelerationStructureBuildRuntimeCache &runtime,
                            PreparedAsFrame prepared)
{
    if (!prepared.available)
    {
        return;
    }
    auto &slot = runtime.frameSlots[prepared.frameSlot];
    auto importedBuffers = std::map<const nr::rhi::Buffer *, GraphResourceHandle>{};
    auto blasResourceByMesh = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};
    auto const ownership = nr::renderer::ownershipDomainFromQueue(context.queue);
    auto const scratchResource = context.importBuffer(
        slot.scratchBuffer, "ASBuild.Scratch", ResourceLifetime::FrameLocal,
        {BufferUsageIntent::AccelerationStructureScratch, BufferUsageIntent::ShaderDeviceAddress}, ownership);
    auto const instanceResource = context.importBuffer(
        slot.instanceBuffer, "ASBuild.TLAS.Instances", ResourceLifetime::FrameLocal,
        {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress}, ownership);
    auto const tlasResource =
        context.importAccelerationStructure(slot.tlas, "ASBuild.TLAS", ResourceLifetime::FrameLocal, ownership);
    context.publishFrameResource(nr::renderer::frameResource::sceneTlas, tlasResource);

    auto importStorage = [&](const nr::rhi::Buffer &buffer, std::string_view name) {
        return context.importBuffer(buffer, name, ResourceLifetime::ScenePersistent, {BufferUsageIntent::StorageRead},
                                    ownership);
    };
    auto const &rtMetadata = runtime.rtMetadata;
    nrAssert(rtMetadata.valid(), "AS build requires a resident RT metadata set before graph declaration.");
    auto const instanceMetadata = importStorage(rtMetadata.instances, "ASBuild.RT.InstanceMetadata");
    auto const geometryMetadata = importStorage(rtMetadata.geometries, "ASBuild.RT.GeometryMetadata");
    auto const materialHeaders = importStorage(rtMetadata.materialHeaders, "ASBuild.RT.MaterialHeaders");
    auto const materialLayers = importStorage(rtMetadata.materialLayers, "ASBuild.RT.MaterialLayers");
    auto const materialTextureRefs = importStorage(rtMetadata.materialTextureRefs, "ASBuild.RT.MaterialTextureRefs");
    auto const neuralMaterialRefs = importStorage(rtMetadata.neuralMaterialRefs, "ASBuild.RT.NeuralMaterialRefs");
    auto const neuralModelParameters =
        importStorage(rtMetadata.neuralModelParameters, "ASBuild.RT.NeuralModelParameters");
    auto const neuralLatentTexture0 = context.importSampledImage(
        rtMetadata.neuralLatentTexture0, "ASBuild.RT.NeuralLatentTexture0", vk::Extent3D{64u, 64u, 1u},
        vk::Format::eR16G16B16A16Sfloat, ResourceLifetime::ScenePersistent, ownership);
    auto const neuralLatentTexture1 = context.importSampledImage(
        rtMetadata.neuralLatentTexture1, "ASBuild.RT.NeuralLatentTexture1", vk::Extent3D{64u, 64u, 1u},
        vk::Format::eR16G16B16A16Sfloat, ResourceLifetime::ScenePersistent, ownership);

    auto const &atlasMesh = runtime.blasCache.at(prepared.rtAtlasMesh).cachedBuild.sceneMesh;
    auto const vertexAtlas =
        context.importBuffer(atlasMesh.vertexBuffer.buffer->get(), "ASBuild.RT.VertexAtlas",
                             ResourceLifetime::ScenePersistent, {BufferUsageIntent::StorageRead}, ownership);
    auto indexAtlas = vertexAtlas;
    if (atlasMesh.hasIndexBuffer())
    {
        indexAtlas =
            context.importBuffer(atlasMesh.indexBuffer.buffer->get(), "ASBuild.RT.IndexAtlas",
                                 ResourceLifetime::ScenePersistent, {BufferUsageIntent::StorageRead}, ownership);
    }
    context.publishFrameResource(nr::renderer::frameResource::sceneRtInstanceMetadata, instanceMetadata);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtGeometryMetadata, geometryMetadata);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialHeaders, materialHeaders);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialLayers, materialLayers);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialTextureRefs, materialTextureRefs);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtNeuralMaterialRefs, neuralMaterialRefs);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtNeuralModelParameters, neuralModelParameters);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtNeuralLatentTexture0, neuralLatentTexture0);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtNeuralLatentTexture1, neuralLatentTexture1);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtVertexAtlas, vertexAtlas);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtIndexAtlas, indexAtlas);
    auto const hitPlan = context.importFrameData("ASBuild.RT.HitSbtPlan", runtime.structuralPlan.hitSbtPlan);
    context.publishFrameData(nr::renderer::frameData::sceneRtHitSbtPlan, hitPlan);

    auto importBlas = [&](nr::resource::MeshHandle mesh) {
        if (auto found = blasResourceByMesh.find(mesh); found != blasResourceByMesh.end())
        {
            return found->second;
        }
        auto &entry = runtime.blasCache.at(mesh);
        auto resource = context.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = std::format("ASBuild.BLAS.mesh{}", mesh.slot),
            .lifetime = ResourceLifetime::RendererPersistent,
            .initialOwnership =
                entry.retainedState.common.initialized ? entry.retainedState.common.ownership : ownership,
            .type = entry.accelerationStructure.type(),
            .size = entry.accelerationStructure.size(),
            .importedResource = std::cref(entry.accelerationStructure),
            .retainedState = std::ref(entry.retainedState),
        });
        blasResourceByMesh.emplace(mesh, resource);
        return resource;
    };

    auto dirtyBlasResources = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};
    std::ranges::for_each(prepared.instances,
                          [&](const PlannedInstance &instance) { static_cast<void>(importBlas(instance.mesh)); });

    auto blasFrameDataHandle = nr::renderer::GraphFrameDataHandle{};
    if (!prepared.dirtyMeshes.empty())
    {
        auto frameData = BlasBuildFrameData{
            .scratchAlignment = runtime.limits.minScratchAlignment,
        };
        auto uses = std::vector<nr::renderer::PassResourceUseDesc>{
            nr::renderer::use::accelerationStructureScratchWrite(scratchResource),
        };
        auto importedInputs = std::set<GraphResourceHandle>{};
        auto scratchOffset = vk::DeviceSize{0u};
        std::ranges::for_each(prepared.dirtyMeshes, [&](nr::resource::MeshHandle mesh) {
            auto const &cached = runtime.blasCache.at(mesh).cachedBuild;
            auto const geometryOffset = frameData.geometries.size();
            frameData.geometries.insert(frameData.geometries.end(), cached.geometries.begin(), cached.geometries.end());
            scratchOffset = alignUp(scratchOffset, runtime.limits.minScratchAlignment);
            frameData.works.push_back(BlasBuildWork{
                .dst = std::cref(runtime.blasCache.at(mesh).accelerationStructure),
                .scratchBuffer = std::cref(slot.scratchBuffer),
                .geometryOffset = geometryOffset,
                .geometryCount = cached.geometries.size(),
                .scratchAddress = slot.scratchBuffer.deviceAddress() + scratchOffset,
                .scratchSize = cached.sizes.buildScratchSize,
                .options = staticBlasBuildOptions(),
            });
            scratchOffset += cached.sizes.buildScratchSize;
            auto const blas = importBlas(mesh);
            dirtyBlasResources.emplace(mesh, blas);
            uses.push_back(nr::renderer::use::accelerationStructureBuildWrite(blas));
            importedInputs.insert(importBufferOnce(
                context, importedBuffers, cached.sceneMesh.vertexBuffer.buffer->get(), "ASBuild.SceneVertexAtlas",
                ResourceLifetime::ScenePersistent,
                {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress}));
            if (cached.sceneMesh.hasIndexBuffer())
            {
                importedInputs.insert(importBufferOnce(
                    context, importedBuffers, cached.sceneMesh.indexBuffer.buffer->get(), "ASBuild.SceneIndexAtlas",
                    ResourceLifetime::ScenePersistent,
                    {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress}));
            }
        });
        std::ranges::for_each(importedInputs, [&](GraphResourceHandle resource) {
            uses.push_back(nr::renderer::use::accelerationStructureBuildInputRead(resource));
        });
        blasFrameDataHandle = context.importFrameData("ASBuild.BLAS.FrameData", std::move(frameData));
        auto const blasFrameDataUses = std::array{blasFrameDataHandle};
        static_cast<void>(context.addPass(
            uses, "ASBuild.BuildBLAS", [blasFrameDataHandle](const nr::renderer::PassRecordContext &recordContext) {
                auto const &data = recordContext.frameData<BlasBuildFrameData>(blasFrameDataHandle);
                auto records = makeBatchRecords(data);
                nr::rhi::recordBuildBlasBatch(recordContext.commandBuffer->get(), records, data.scratchAlignment);
            },
            nullptr, false, vk::PipelineStageFlags2{}, blasFrameDataUses));
    }

    auto tlasUses = std::vector<nr::renderer::PassResourceUseDesc>{
        nr::renderer::use::accelerationStructureBuildInputRead(instanceResource),
        nr::renderer::use::orderedAfterPrevious(nr::renderer::use::accelerationStructureScratchWrite(scratchResource)),
        nr::renderer::use::accelerationStructureBuildWrite(tlasResource),
    };
    auto blasReads = std::map<GraphResourceHandle, bool>{};
    std::ranges::for_each(prepared.instances, [&](const PlannedInstance &instance) {
        auto const resource = blasResourceByMesh.at(instance.mesh);
        blasReads.insert_or_assign(resource, dirtyBlasResources.contains(instance.mesh));
    });
    std::ranges::for_each(blasReads, [&](const auto &pair) {
        auto read = nr::renderer::use::accelerationStructureBuildRead(pair.first);
        tlasUses.push_back(pair.second ? nr::renderer::use::orderedAfterPrevious(read) : read);
    });
    auto const tlasFrameData =
        context.importFrameData("ASBuild.TLAS.FrameData", TlasBuildFrameData{
                                                              .dst = std::cref(slot.tlas),
                                                              .instanceBuffer = std::cref(slot.instanceBuffer),
                                                              .scratchBuffer = std::cref(slot.scratchBuffer),
                                                              .scratchAddress = slot.scratchBuffer.deviceAddress(),
                                                              .buildInput = prepared.tlasInput,
                                                              .options = tlasBuildOptions(),
                                                              .scratchAlignment = runtime.limits.minScratchAlignment,
                                                          });
    auto const tlasFrameDataUses = std::array{tlasFrameData};
    static_cast<void>(context.addPass(tlasUses, "ASBuild.BuildTLAS",
                                      [tlasFrameData](const nr::renderer::PassRecordContext &recordContext) {
                                          auto const &data = recordContext.frameData<TlasBuildFrameData>(tlasFrameData);
                                          nr::rhi::recordBuildTlas(recordContext.commandBuffer->get(),
                                                                   nr::rhi::TlasBuildRecordInfo{
                                                                       .dst = data.dst.get(),
                                                                       .instanceBuffer = data.instanceBuffer.get(),
                                                                       .scratchBuffer = data.scratchBuffer.get(),
                                                                       .scratchAddress = data.scratchAddress,
                                                                       .buildInput = data.buildInput,
                                                                       .options = data.options,
                                                                   },
                                                                    data.scratchAlignment);
                                      },
                                      nullptr, false, vk::PipelineStageFlags2{}, tlasFrameDataUses));
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
[[nodiscard]] bool neuralMaterialP0Eligible(const nr::resource::Material &material) noexcept
{
    return detail::neuralP0MaterialEligible(material);
}

AccelerationStructureBuildNode::~AccelerationStructureBuildNode() = default;

void AccelerationStructureBuildNode::initialize(NodeInitContext &context)
{
    device_ = context.device;
    runtime_ = std::make_shared<detail::AccelerationStructureBuildRuntimeCache>();
    runtime_->limits = nr::rhi::queryAsBuildLimits(context.device.get().physicalDevice);
}

void AccelerationStructureBuildNode::shutdown(NodeShutdownContext &)
{
    runtime_.reset();
    device_.reset();
}

void AccelerationStructureBuildNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}

void AccelerationStructureBuildNode::materializeCurrentFrame(NodeBuildContext &context,
                                                             const NodeFrameParameters &frameParameters)
{
    nrAssert(static_cast<bool>(runtime_), "AccelerationStructureBuild build stage requires initialized runtime state.");
    nrAssert(device_.has_value(),
             "AccelerationStructureBuild build stage requires device reference from initialize stage.");
    auto prepared = detail::prepareAsFrame(*runtime_, device_->get(), input, frameParameters);
    detail::declarePreparedAsFrame(context, *runtime_, std::move(prepared));
}
} // namespace nr::renderPasses
