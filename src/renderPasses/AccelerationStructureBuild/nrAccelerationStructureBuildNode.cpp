module nr.renderPasses;
import dependency.math;
import dependency.vulkan;

import :accelerationStructureBuild;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.resource;
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

struct FrameSlotAsResources
{
    nr::rhi::Buffer instanceBuffer{};
    vk::DeviceSize instanceBufferSize = 0;

    nr::rhi::Buffer rtInstanceMetadataBuffer{};
    vk::DeviceSize rtInstanceMetadataBufferSize = 0;

    nr::rhi::Buffer rtGeometryMetadataBuffer{};
    vk::DeviceSize rtGeometryMetadataBufferSize = 0;

    nr::rhi::Buffer rtMaterialHeaderBuffer{};
    vk::DeviceSize rtMaterialHeaderBufferSize = 0;

    nr::rhi::Buffer rtMaterialLayerBuffer{};
    vk::DeviceSize rtMaterialLayerBufferSize = 0;

    nr::rhi::Buffer rtMaterialTextureRefBuffer{};
    vk::DeviceSize rtMaterialTextureRefBufferSize = 0;

    nr::rhi::Buffer scratchBuffer{};
    vk::DeviceSize scratchBufferSize = 0;

    nr::rhi::Buffer tlasStorage{};
    nr::rhi::AccelerationStructureResource tlas{};
    std::uint32_t tlasCapacity = 0;
    std::uint64_t appliedStructuralPlanGeneration = 0u;
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
    std::string branchKey{"none"};
    std::size_t frameSlot = 0u;
    double cacheScanMilliseconds = 0.0;
    double metadataPlanMilliseconds = 0.0;
    double cpuWritesMilliseconds = 0.0;
    double tlasSizingMilliseconds = 0.0;
    bool available = false;
};

using AsStructuralRevisionProjection = nr::scene::SceneRtStructuralRevisionProjection;

using BlasRevisionProjection = nr::revision::RevisionProjection<nr::scene::SceneRtRevisionDomain::meshContent, nr::scene::SceneRtRevisionDomain::meshLayout>;

using RtMaterialRevisionProjection =
    nr::revision::RevisionProjection<nr::scene::SceneRtRevisionDomain::materialBinding, nr::scene::SceneRtRevisionDomain::materialPayload, nr::scene::SceneRtRevisionDomain::textureBinding, nr::scene::SceneRtRevisionDomain::textureResidency>;

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

struct AsStructuralPlanKey
{
    std::uint64_t sceneIdentity = 0u;
    AsStructuralRevisionProjection revisions{};
    std::vector<AsStructuralPacketKey> packets{};
    std::vector<AsStructuralMeshSemanticEntry> meshSemantics{};

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
    RtStructuralPlanCache structuralPlan{};
    nr::rhi::AsBuildLimits limits{};
    std::uint64_t frameSerial = 0;
    std::uint64_t activeSceneIdentity = 0u;
    std::optional<BlasRevisionProjection> blasRevisions{};
    std::optional<PreparedAsFrame> preparedFrame{};
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

[[nodiscard]] vk::TransformMatrixKHR packTransform(const glm::mat4 &world) noexcept
{
    auto transform = vk::TransformMatrixKHR{};
    transform.matrix[0][0] = world[0][0];
    transform.matrix[0][1] = world[1][0];
    transform.matrix[0][2] = world[2][0];
    transform.matrix[0][3] = world[3][0];
    transform.matrix[1][0] = world[0][1];
    transform.matrix[1][1] = world[1][1];
    transform.matrix[1][2] = world[2][1];
    transform.matrix[1][3] = world[3][1];
    transform.matrix[2][0] = world[0][2];
    transform.matrix[2][1] = world[1][2];
    transform.matrix[2][2] = world[2][2];
    transform.matrix[2][3] = world[3][2];
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

[[nodiscard]] std::vector<nr::rhi::BlasGeometryRecord> makeBlasGeometryRecords(const nr::scene::SceneAccelerationStructureMesh &mesh)
{
    nrAssert(mesh.hasVertexBuffer(), "AS build mesh requires a vertex atlas buffer.");
    auto const &vertexBuffer = mesh.vertexBuffer.buffer->get();
    auto const vertexAddress = vertexBuffer.deviceAddress() + mesh.vertexByteOffset;
    auto const indexAddress = mesh.hasIndexBuffer() ? mesh.indexBuffer.buffer->get().deviceAddress() + mesh.indexByteOffset : vk::DeviceAddress{0};

    auto records = std::vector<nr::rhi::BlasGeometryRecord>{};
    records.reserve(mesh.geometries.size());

    std::ranges::for_each(mesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        nrAssert(geometry.primitiveOffset <= std::numeric_limits<std::uint32_t>::max(), "AS build geometry primitive offset exceeds the current RHI uint32 range.");
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
        records.push_back(nr::rhi::makeBlasTriangleGeometryRecord(vertexBuffer, layout, input));
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

void retireBlasResources(AccelerationStructureBuildRuntimeCache &runtime, BlasCacheEntry &entry, std::uint64_t retireDelay)
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

void reapRetiredBlas(AccelerationStructureBuildRuntimeCache &runtime)
{
    std::erase_if(runtime.retiredBlas, [&](const RetiredBlasAccelerationStructure &retired) { return retired.retireAfterFrameSerial <= runtime.frameSerial; });
    std::erase_if(runtime.blasAtlas.retired, [&](const RetiredBlasAtlasResources &retired) { return retired.retireAfterFrameSerial <= runtime.frameSerial; });
}

void pruneBlasCache(AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::Scene &scene, std::uint64_t unusedFrameRetireLatency, std::uint64_t retireDelay)
{
    auto it = runtime.blasCache.begin();
    while (it != runtime.blasCache.end())
    {
        auto const unusedTooLong = runtime.frameSerial > it->second.lastSeenFrameSerial && (runtime.frameSerial - it->second.lastSeenFrameSerial) > unusedFrameRetireLatency;
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

void pruneRtMaterialCache(AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::Scene &scene, std::uint64_t unusedFrameRetireLatency)
{
    auto it = runtime.rtMaterialCache.begin();
    while (it != runtime.rtMaterialCache.end())
    {
        auto const unusedTooLong = runtime.frameSerial > it->second.lastSeenFrameSerial && (runtime.frameSerial - it->second.lastSeenFrameSerial) > unusedFrameRetireLatency;
        auto const materialMissing = !scene.tryGetMaterialAsset(it->first.material).has_value();
        if (!materialMissing && !unusedTooLong)
        {
            ++it;
            continue;
        }

        it = runtime.rtMaterialCache.erase(it);
    }
}

[[nodiscard]] vk::DeviceSize grownBlasAtlasCapacity(vk::DeviceSize currentCapacity, vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    auto const maxCapacity = std::numeric_limits<vk::DeviceSize>::max();
    auto const doubledCapacity = currentCapacity > (maxCapacity / 2u) ? maxCapacity : currentCapacity * 2u;
    return std::max({requiredCapacity, doubledCapacity, minCapacity});
}

[[nodiscard]] vk::DeviceSize replacementBlasAtlasCapacity(vk::DeviceSize currentCapacity, vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    return std::max({requiredCapacity, currentCapacity, minCapacity});
}

[[nodiscard]] vk::DeviceSize alignedBlasSliceEnd(vk::DeviceSize cursor, vk::DeviceSize size) noexcept
{
    return alignUp(cursor, blasStorageOffsetAlignment) + size;
}

[[nodiscard]] vk::DeviceSize requiredBlasAtlasBytes(std::span<const std::reference_wrapper<const PendingBlasBuild>> builds) noexcept
{
    auto cursor = vk::DeviceSize{0};
    std::ranges::for_each(builds, [&](std::reference_wrapper<const PendingBlasBuild> buildRef) { cursor = alignedBlasSliceEnd(cursor, buildRef.get().cachedBuild.get().sizes.accelerationStructureSize); });
    return cursor;
}

[[nodiscard]] bool cachedBlasBuildNeedsRefresh(const CachedBlasBuildDesc &cached, const nr::scene::SceneAccelerationStructureMeshSemanticKey &semanticKey)
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
    return !context.entry.accelerationStructure.valid() || !context.atlas.buffer.valid() || context.entry.gpuVersion != context.cached.semanticKey.meshGpuVersion || context.entry.atlasGeneration != context.atlas.generation ||
           context.entry.storageSize < context.cached.sizes.accelerationStructureSize || context.entry.buildSignature != context.cached.signature;
}

[[nodiscard]] std::optional<std::reference_wrapper<const CachedBlasBuildDesc>> ensureCachedBlasBuild(const nr::scene::Scene &scene, nr::rhi::Device &device, nr::resource::MeshHandle meshHandle, BlasCacheEntry &entry)
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
    nrAssert(sceneMesh->semanticKey == *semanticKey, "Scene acceleration-structure mesh semantic key changed while refreshing the BLAS build cache.");

    auto mesh = std::move(*sceneMesh);
    auto geometries = makeBlasGeometryRecords(mesh);
    auto const signature = makeBlasBuildSignature(mesh);
    auto const sizes = nr::rhi::queryBlasBuildSizes(device.device, std::span<const nr::rhi::BlasGeometryRecord>{geometries}, staticBlasBuildOptions());

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

void createBlasAtlas(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device, vk::DeviceSize requiredCapacity, bool growCapacity)
{
    auto const capacity = growCapacity ? grownBlasAtlasCapacity(runtime.blasAtlas.capacityBytes, requiredCapacity) : replacementBlasAtlasCapacity(runtime.blasAtlas.capacityBytes, requiredCapacity);
    runtime.blasAtlas.buffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(capacity, 1u), vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), nr::rhi::MemoryUsage::GpuOnly, "ASBuild.BLAS.Atlas");
    nrAssert(runtime.blasAtlas.buffer.valid(), "AccelerationStructureBuildNode failed to create BLAS storage atlas.");
    runtime.blasAtlas.capacityBytes = capacity;
    runtime.blasAtlas.usedBytes = 0;
    ++runtime.blasAtlas.generation;
}

[[nodiscard]] vk::DeviceSize allocateBlasAtlasSlice(AccelerationStructureBuildRuntimeCache &runtime, vk::DeviceSize size)
{
    auto const offset = alignUp(runtime.blasAtlas.usedBytes, blasStorageOffsetAlignment);
    nrAssert(offset <= runtime.blasAtlas.capacityBytes && size <= runtime.blasAtlas.capacityBytes - offset, "AccelerationStructureBuildNode BLAS atlas allocation exceeds capacity.");
    runtime.blasAtlas.usedBytes = offset + size;
    return offset;
}

[[nodiscard]] BlasCacheEntry &ensureBlasEntry(AccelerationStructureBuildRuntimeCache &runtime, nr::rhi::Device &device, const PendingBlasBuild &pending, std::uint64_t retireDelay)
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
        entry.accelerationStructure = nr::rhi::AccelerationStructureResource::create(device.device, runtime.blasAtlas.buffer, entry.storageOffset, cached.sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eBottomLevel, std::format("ASBuild.BLAS.mesh{}", pending.mesh.slot));
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

    slot.scratchBuffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(requiredSize, 1u), vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress), nr::rhi::MemoryUsage::GpuOnly, "ASBuild.Scratch");
    nrAssert(slot.scratchBuffer.valid(), "AccelerationStructureBuildNode failed to create scratch buffer.");
    slot.scratchBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

void ensureInstanceBuffer(nr::rhi::Device &device, FrameSlotAsResources &slot, vk::DeviceSize requiredSize)
{
    if (slot.instanceBuffer.valid() && slot.instanceBufferSize >= requiredSize)
    {
        return;
    }

    slot.instanceBuffer =
        device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(requiredSize, 1u), vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), nr::rhi::MemoryUsage::CpuToGpu, "ASBuild.TLAS.Instances");
    nrAssert(slot.instanceBuffer.valid(), "AccelerationStructureBuildNode failed to create TLAS instance buffer.");
    slot.instanceBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

void ensureStorageMetadataBuffer(nr::rhi::Device &device, nr::rhi::Buffer &buffer, vk::DeviceSize &bufferSize, vk::DeviceSize requiredSize, std::string_view debugName)
{
    if (buffer.valid() && bufferSize >= requiredSize)
    {
        return;
    }

    buffer = device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(requiredSize, 1u), vk::BufferUsageFlagBits::eStorageBuffer), nr::rhi::MemoryUsage::CpuToGpu, debugName);
    nrAssert(buffer.valid(), std::format("AccelerationStructureBuildNode failed to create {}.", debugName));
    bufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

template <typename T> void writeMetadataBuffer(nr::rhi::Buffer &buffer, std::span<const T> values)
{
    if (values.empty())
    {
        return;
    }

    buffer.writeMappedAndFlush(values);
}

void ensureTlas(nr::rhi::Device &device, FrameSlotAsResources &slot, const nr::rhi::AsBuildSizes &sizes, std::uint32_t instanceCount)
{
    auto const needsCreate = !slot.tlas.valid() || !slot.tlasStorage.valid() || slot.tlasStorage.size() < sizes.accelerationStructureSize || slot.tlasCapacity < instanceCount;
    if (!needsCreate)
    {
        return;
    }

    slot.tlasStorage =
        device.resourceFactory.createBuffer(nr::rhi::makeBufferCreateInfo(std::max<vk::DeviceSize>(sizes.accelerationStructureSize, 1u), vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress), nr::rhi::MemoryUsage::GpuOnly, "ASBuild.TLAS.Storage");
    nrAssert(slot.tlasStorage.valid(), "AccelerationStructureBuildNode failed to create TLAS storage.");
    slot.tlas = nr::rhi::AccelerationStructureResource::create(device.device, slot.tlasStorage, 0, sizes.accelerationStructureSize, vk::AccelerationStructureTypeKHR::eTopLevel, "ASBuild.TLAS");
    slot.tlasCapacity = instanceCount;
}

[[nodiscard]] GraphResourceHandle importBufferOnce(NodeBuildContext &context, std::map<const nr::rhi::Buffer *, GraphResourceHandle> &importedBuffers, const nr::rhi::Buffer &buffer, std::string_view debugName, ResourceLifetime lifetime, std::initializer_list<BufferUsageIntent> usageIntents)
{
    auto const *key = std::addressof(buffer);
    if (auto it = importedBuffers.find(key); it != importedBuffers.end())
    {
        return it->second;
    }

    auto resource = context.importBuffer(buffer, debugName, lifetime, usageIntents, nr::renderer::ownershipDomainFromQueue(context.queue));
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
    nrAssert(value <= std::numeric_limits<std::uint32_t>::max(), std::format("{} exceeds the RT metadata uint32 ABI range.", label));
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::uint32_t rtGeometryPrimitiveElementOffset(const nr::scene::SceneAccelerationStructureMesh &sceneMesh, const nr::scene::SceneAccelerationStructureGeometry &geometry)
{
    auto const elementSize = geometry.indexed ? vk::DeviceSize{sizeof(std::uint32_t)} : sceneMesh.vertexStride;
    nrAssert(elementSize > 0u, "RT geometry metadata requires a non-zero vertex stride.");
    nrAssert(geometry.primitiveOffset % elementSize == 0u, "RT geometry primitive offset must align to its indexed/non-indexed element size.");
    return checkedRtMetadataUint32(geometry.primitiveOffset / elementSize, "RT geometry primitive element offset");
}

[[nodiscard]] std::uint32_t rtMeshVertexBase(const nr::scene::SceneAccelerationStructureMesh &sceneMesh)
{
    nrAssert(sceneMesh.vertexStride > 0u, "RT instance metadata requires a non-zero vertex stride.");
    nrAssert(sceneMesh.vertexByteOffset % sceneMesh.vertexStride == 0u, "RT instance vertex atlas offset must align to the vertex stride.");
    return checkedRtMetadataUint32(sceneMesh.vertexByteOffset / sceneMesh.vertexStride, "RT instance vertex base");
}

[[nodiscard]] std::uint32_t rtMeshIndexBase(const nr::scene::SceneAccelerationStructureMesh &sceneMesh)
{
    if (!sceneMesh.hasIndexBuffer())
    {
        return 0u;
    }

    nrAssert(sceneMesh.indexByteOffset % sizeof(std::uint32_t) == 0u, "RT instance index atlas offset must align to the uint32 index size.");
    return checkedRtMetadataUint32(sceneMesh.indexByteOffset / sizeof(std::uint32_t), "RT instance index base");
}

[[nodiscard]] nr::resource::MaterialHandle rtGeometryMaterialHandle(const nr::scene::Scene &scene, nr::resource::MeshHandle meshHandle, std::uint32_t geometryIndex)
{
    auto meshRecord = scene.tryGetMeshAsset(meshHandle);
    if (!meshRecord.has_value() || !meshRecord->get().cpuReady || geometryIndex >= meshRecord->get().cpu.geometries.size())
    {
        return {};
    }

    return meshRecord->get().cpu.geometries[geometryIndex].material;
}

[[nodiscard]] RtMaterialCacheKey makeRtMaterialCacheKey(const nr::scene::Scene &scene, nr::resource::MaterialHandle materialHandle, const nr::scene::MaterialAssetRecord &materialRecord, RtMaterialRevisionProjection revisions) noexcept
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

        nrAssert(binding->descriptorIndex <= nr::scene::kMaxSceneTextureId, std::format("RT material texture descriptor id {} exceeds packed uint16 id capacity {}.", binding->descriptorIndex, nr::scene::kMaxSceneTextureId));
        key.textureIds[slotIndex] = static_cast<nr::scene::SceneTextureId>(binding->descriptorIndex);
        key.textureGpuVersions[slotIndex] = binding->gpuVersion;
    });

    return key;
}

[[nodiscard]] const nr::scene::RtCompiledMaterial &ensureCachedRtMaterial(AccelerationStructureBuildRuntimeCache &runtime, const nr::scene::MaterialAssetRecord &materialRecord, RtMaterialCacheKey key)
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

[[nodiscard]] std::uint32_t ensureRtMaterialIndex(AccelerationStructureBuildRuntimeCache &runtime, RtMetadataBuildState &state, const nr::scene::Scene &scene, nr::resource::MaterialHandle materialHandle, RtMaterialRevisionProjection revisions)
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

[[nodiscard]] AppendedRtInstanceMetadata appendRtInstanceMetadata(AccelerationStructureBuildRuntimeCache &runtime, RtMetadataBuildState &state, SceneRtHitSbtPlan &hitSbtPlan, std::map<RtHitPermutationKey, std::uint32_t> &hitPermutationLookup, const nr::scene::Scene &scene,
                                                                  const nr::scene::TlasBuildInputPacket &packet, const nr::scene::SceneAccelerationStructureMesh &sceneMesh, RtMaterialRevisionProjection materialRevisions)
{
    initializeRtMetadataBuildState(state);

    auto const geometryOffset = checkedRtMetadataUint32(state.geometries.size(), "RT geometry metadata offset");
    auto const geometryCount = checkedRtMetadataUint32(sceneMesh.geometries.size(), "RT instance geometry count");
    nrAssert(geometryOffset <= std::numeric_limits<std::uint32_t>::max() - geometryCount, "RT geometry metadata range exceeds the uint32 ABI.");

    auto geometryPermutationKeys = std::vector<RtHitPermutationKey>{};
    geometryPermutationKeys.reserve(sceneMesh.geometries.size());
    std::ranges::for_each(sceneMesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        auto const materialHandle = rtGeometryMaterialHandle(scene, packet.mesh, geometry.geometryIndex);
        auto const materialIndex = ensureRtMaterialIndex(runtime, state, scene, materialHandle, materialRevisions);
        nrAssert(materialIndex < state.materials.size(), "RT material index must resolve before building hit SBT plan.");
        auto const &materialHeader = state.materials[materialIndex].header;
        geometryPermutationKeys.push_back(makeRtHitPermutationKey(materialHeader.layerFlags, materialHeader.featureFlags));
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
    nrAssert(instanceMetadataIndex <= 0x00FF'FFFFu, "RT instance metadata index must fit the 24-bit Vulkan InstanceCustomIndex field.");
    state.instances.push_back(nr::scene::RtInstanceMetadata{
        .geometryOffset = geometryOffset,
        .geometryCount = geometryCount,
        .stableInstanceId = stableTlasInstanceCustomIndex(packet),
        .vertexBase = rtMeshVertexBase(sceneMesh),
        .indexBase = rtMeshIndexBase(sceneMesh),
        .vertexStride = checkedRtMetadataUint32(sceneMesh.vertexStride, "RT instance vertex stride"),
    });

    auto const hitRecordBase = appendRtHitSbtPlanInstance(hitSbtPlan, hitPermutationLookup, instanceMetadataIndex, std::span<const RtHitPermutationKey>{geometryPermutationKeys.data(), geometryPermutationKeys.size()});
    return AppendedRtInstanceMetadata{
        .instanceMetadataIndex = instanceMetadataIndex,
        .hitRecordBase = hitRecordBase,
    };
}

[[nodiscard]] nr::scene::RtMaterialTable makeRtMaterialTable(const RtMetadataBuildState &state)
{
    auto materialRefs = state.materials | std::views::transform([](const nr::scene::RtCompiledMaterial &material) { return std::cref(material); }) | std::ranges::to<std::vector>();
    return nr::scene::makeRtMaterialTable(std::span<const std::reference_wrapper<const nr::scene::RtCompiledMaterial>>{materialRefs.data(), materialRefs.size()});
}

[[nodiscard]] AsStructuralPlanKey makeAsStructuralPlanKey(const nr::scene::SceneRevisionSnapshot &revisions, std::span<const nr::scene::TlasBuildInputPacket> packets, const std::map<nr::resource::MeshHandle, PendingBlasBuild> &pendingByMesh)
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
    };
}

[[nodiscard]] bool transformFlipsTriangleFacing(const glm::mat4 &world) noexcept
{
    auto const column0 = glm::vec3{world[0]};
    auto const column1 = glm::vec3{world[1]};
    auto const column2 = glm::vec3{world[2]};
    auto const determinant = glm::dot(column0, glm::cross(column1, column2));
    return std::isfinite(determinant) && determinant < 0.0f;
}

[[nodiscard]] vk::GeometryInstanceFlagsKHR tlasInstanceFlags(const nr::scene::TlasBuildInputPacket &packet, vk::GeometryInstanceFlagsKHR baseFlags) noexcept
{
    auto flags = baseFlags;
    if (transformFlipsTriangleFacing(packet.world))
    {
        // glTF global transforms with a negative determinant reverse triangle winding.
        flags ^= vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing;
    }
    return flags;
}

[[nodiscard]] vk::AccelerationStructureInstanceKHR makeTlasInstance(const nr::scene::TlasBuildInputPacket &packet, vk::GeometryInstanceFlagsKHR baseFlags, const nr::rhi::AccelerationStructureResource &blas, std::uint32_t hitRecordBase, std::uint32_t hitRecordCount, std::uint32_t instanceCustomIndex)
{
    nrAssert(hitRecordCount > 0u, "AS build requires at least one hit SBT record.");
    nrAssert(hitRecordBase < hitRecordCount, std::format("TLAS hit SBT record base {} exceeds hit SBT record count {}.", hitRecordBase, hitRecordCount));
    nrAssert(hitRecordBase <= 0x00FF'FFFFu, "TLAS hit SBT record base must fit the 24-bit Vulkan instanceShaderBindingTableRecordOffset field.");

    auto instance = vk::AccelerationStructureInstanceKHR{};
    instance.setTransform(packTransform(packet.world));
    // InstanceCustomIndex is a dense metadata index for RT shaders. The stable debug hash is stored in RtInstanceMetadata.
    instance.setInstanceCustomIndex(instanceCustomIndex);
    instance.setMask(packet.instanceMask & 0xFFu);
    instance.setInstanceShaderBindingTableRecordOffset(hitRecordBase);
    instance.setFlags(tlasInstanceFlags(packet, baseFlags));
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

[[nodiscard]] std::string makePreparedAsBranchKey(
    const AccelerationStructureBuildRuntimeCache& runtime,
    const PreparedAsFrame& prepared,
    const nr::scene::SceneRevisionSnapshot& revisions)
{
    auto const& slot = runtime.frameSlots[prepared.frameSlot];
    auto branch = std::format(
        "{};scene={};plan={};atlasGen={};atlasCapacity={};dirty={};instances={};tlasCapacity={};tlasSize={}",
        prepared.available ? (prepared.dirtyMeshes.empty() ? "tlas-only" : "dirty-blas") : "no-instances",
        revisions.sceneIdentity,
        runtime.structuralPlan.generation,
        runtime.blasAtlas.generation,
        runtime.blasAtlas.capacityBytes,
        prepared.dirtyMeshes.size(),
        prepared.instances.size(),
        slot.tlasCapacity,
        slot.tlas.valid() ? slot.tlas.size() : 0u);
    auto const structuralRevisions = AsStructuralRevisionProjection::capture(revisions.rt);
    std::ranges::for_each(structuralRevisions.values, [&](auto revision) {
        branch += std::format(";revision={}", revision.value);
    });
    std::ranges::for_each(prepared.dirtyMeshes, [&](nr::resource::MeshHandle mesh) {
        auto const& cached = runtime.blasCache.at(mesh).cachedBuild;
        branch += std::format(
            ";mesh={}:geometries={}:indexed={}",
            mesh.packed(),
            cached.geometries.size(),
            cached.sceneMesh.hasIndexBuffer() ? 1u : 0u);
    });
    if (prepared.available)
    {
        auto const& frameSlot = runtime.frameSlots[prepared.frameSlot];
        auto const& atlasMesh = runtime.blasCache.at(prepared.rtAtlasMesh).cachedBuild.sceneMesh;
        branch += std::format(
            ";instanceBytes={};scratchBytes={};rtIndex={};metadata={},{},{},{},{}",
            frameSlot.instanceBufferSize,
            frameSlot.scratchBufferSize,
            atlasMesh.hasIndexBuffer() ? 1u : 0u,
            frameSlot.rtInstanceMetadataBufferSize,
            frameSlot.rtGeometryMetadataBufferSize,
            frameSlot.rtMaterialHeaderBufferSize,
            frameSlot.rtMaterialLayerBufferSize,
            frameSlot.rtMaterialTextureRefBufferSize);
    }
    return branch;
}

[[nodiscard]] PreparedAsFrame prepareAsFrame(
    AccelerationStructureBuildRuntimeCache& runtime,
    nr::rhi::Device& device,
    const AccelerationStructureBuildNodeInput& input,
    const nr::renderer::NodeFrameParameters& frameParameters)
{
    auto prepared = PreparedAsFrame{};
    prepared.frameSlot = static_cast<std::size_t>(
        frameParameters.frameIndex % runtime.frameSlots.size());
    auto const probeStart = std::chrono::steady_clock::now();
    ++runtime.frameSerial;
    reapRetiredBlas(runtime);

    if (!frameParameters.scene.has_value() || !frameParameters.sceneTlasBuildInputs.has_value())
    {
        return prepared;
    }

    auto const& scene = frameParameters.scene->get();
    auto const& packets = frameParameters.sceneTlasBuildInputs->get();
    auto const revisions = frameParameters.sceneRevisions.valid()
                               ? frameParameters.sceneRevisions
                               : scene.revisionsSnapshot();
    auto const retireDelay = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);
    if (runtime.activeSceneIdentity != revisions.sceneIdentity)
    {
        retireCurrentBlasAtlas(runtime, retireDelay);
        runtime.blasCache.clear();
        runtime.rtMaterialCache.clear();
        runtime.structuralPlan = {};
        runtime.blasRevisions.reset();
        std::ranges::for_each(runtime.frameSlots, [](FrameSlotAsResources& slot) {
            slot.appliedStructuralPlanGeneration = 0u;
        });
        runtime.activeSceneIdentity = revisions.sceneIdentity;
    }
    auto const blasRevisions = BlasRevisionProjection::capture(revisions.rt);
    if (runtime.blasRevisions.has_value() && *runtime.blasRevisions != blasRevisions)
    {
        retireCurrentBlasAtlas(runtime, retireDelay);
        std::ranges::for_each(runtime.blasCache, [](auto& entry) {
            entry.second.cachedBuild = {};
        });
    }
    runtime.blasRevisions = blasRevisions;
    pruneBlasCache(runtime, scene, input.unusedFrameRetireLatency, retireDelay);
    pruneRtMaterialCache(runtime, scene, input.unusedFrameRetireLatency);
    if (packets.empty())
    {
        prepared.branchKey = makePreparedAsBranchKey(runtime, prepared, revisions);
        return prepared;
    }

    auto pendingByMesh = std::map<nr::resource::MeshHandle, PendingBlasBuild>{};
    auto const stablePacketTopology =
        runtime.structuralPlan.key.has_value() &&
        runtime.structuralPlan.key->sceneIdentity == revisions.sceneIdentity &&
        runtime.structuralPlan.key->revisions == AsStructuralRevisionProjection::capture(revisions.rt) &&
        runtime.structuralPlan.key->packets.size() == packets.size() &&
        std::ranges::equal(
            runtime.structuralPlan.key->packets, packets,
            [](const AsStructuralPacketKey& key, const nr::scene::TlasBuildInputPacket& packet) {
                return key.renderableId == static_cast<std::uint64_t>(packet.renderable.id()) &&
                       key.mesh == packet.mesh &&
                       key.tlasBucket == packet.tlasBucket;
            });
    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket& packet) {
        if (pendingByMesh.contains(packet.mesh))
        {
            runtime.blasCache.try_emplace(packet.mesh).first->second.lastSeenFrameSerial = runtime.frameSerial;
            return;
        }
        auto [entryIt, inserted] = runtime.blasCache.try_emplace(packet.mesh);
        auto cachedBuild = stablePacketTopology && entryIt->second.cachedBuild.valid
                               ? std::optional<std::reference_wrapper<const CachedBlasBuildDesc>>{
                                     std::cref(entryIt->second.cachedBuild)}
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
        prepared.branchKey = makePreparedAsBranchKey(runtime, prepared, revisions);
        return prepared;
    }

    auto pendingRefs = pendingByMesh |
                       std::views::values |
                       std::views::transform([](const PendingBlasBuild& pending) {
                           return std::cref(pending);
                       }) |
                       std::ranges::to<std::vector>();
    auto dirtyBuilds = pendingRefs |
                       std::views::filter([&](std::reference_wrapper<const PendingBlasBuild> pendingRef) {
                           auto const& pending = pendingRef.get();
                           return blasEntryNeedsBuild(BlasBuildDirtyContext{
                               .entry = runtime.blasCache.at(pending.mesh),
                               .cached = pending.cachedBuild.get(),
                               .atlas = runtime.blasAtlas,
                           });
                       }) |
                       std::ranges::to<std::vector>();

    auto atlasCursor = runtime.blasAtlas.usedBytes;
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const PendingBlasBuild> pending) {
        atlasCursor = alignedBlasSliceEnd(
            atlasCursor, pending.get().cachedBuild.get().sizes.accelerationStructureSize);
    });
    auto const capacityOverflow =
        runtime.blasAtlas.buffer.valid() && atlasCursor > runtime.blasAtlas.capacityBytes;
    auto repackBuilds = std::map<nr::resource::MeshHandle, PendingBlasBuild>{};
    if (!runtime.blasAtlas.buffer.valid() || capacityOverflow)
    {
        repackBuilds = pendingByMesh;
        std::ranges::for_each(runtime.blasCache, [&](auto& pair) {
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
        auto repackRefs = repackBuilds |
                          std::views::values |
                          std::views::transform([](const PendingBlasBuild& pending) {
                              return std::cref(pending);
                          }) |
                          std::ranges::to<std::vector>();
        auto const requiredBytes = requiredBlasAtlasBytes(repackRefs);
        retireCurrentBlasAtlas(runtime, retireDelay);
        createBlasAtlas(runtime, device, requiredBytes, capacityOverflow);
        dirtyBuilds = std::move(repackRefs);
    }
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const PendingBlasBuild> pending) {
        auto& entry = ensureBlasEntry(runtime, device, pending.get(), retireDelay);
        entry.lastSeenFrameSerial = runtime.frameSerial;
        prepared.dirtyMeshes.push_back(pending.get().mesh);
    });
    auto const cacheScanEnd = std::chrono::steady_clock::now();

    auto structuralKey = makeAsStructuralPlanKey(revisions, packets, pendingByMesh);
    auto& structuralPlan = runtime.structuralPlan;
    if (!structuralPlan.key.has_value() || *structuralPlan.key != structuralKey)
    {
        auto metadata = RtMetadataBuildState{};
        auto hitSbtPlan = SceneRtHitSbtPlan{};
        auto hitPermutationLookup = std::map<RtHitPermutationKey, std::uint32_t>{};
        auto templates = std::vector<StaticRtInstanceTemplate>{};
        auto const materialRevisions = RtMaterialRevisionProjection::capture(revisions.rt);
        auto const packetOrdinals = std::views::iota(std::size_t{0u}, packets.size());
        std::ranges::for_each(packetOrdinals, [&](std::size_t packetOrdinal) {
            auto const& packet = packets[packetOrdinal];
            auto entryIt = runtime.blasCache.find(packet.mesh);
            auto pendingIt = pendingByMesh.find(packet.mesh);
            if (entryIt == runtime.blasCache.end() ||
                !entryIt->second.accelerationStructure.valid() ||
                pendingIt == pendingByMesh.end())
            {
                return;
            }
            auto const& sceneMesh = pendingIt->second.cachedBuild.get().sceneMesh;
            auto const appended = appendRtInstanceMetadata(
                runtime, metadata, hitSbtPlan, hitPermutationLookup, scene, packet,
                sceneMesh, materialRevisions);
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
            prepared.branchKey = makePreparedAsBranchKey(runtime, prepared, revisions);
            return prepared;
        }
        finalizeSceneRtHitSbtPlan(hitSbtPlan);
        nrAssert(hitSbtPlan.valid(), "AS build generated an invalid RT hit SBT plan.");
        auto const nextGeneration =
            structuralPlan.generation == std::numeric_limits<std::uint64_t>::max()
                ? 1u
                : structuralPlan.generation + 1u;
        auto materialTable = makeRtMaterialTable(metadata);
        structuralPlan = RtStructuralPlanCache{
            .key = std::move(structuralKey),
            .metadata = std::move(metadata),
            .materialTable = std::move(materialTable),
            .hitSbtPlan = std::make_shared<const SceneRtHitSbtPlan>(std::move(hitSbtPlan)),
            .instances = std::move(templates),
            .generation = nextGeneration,
        };
    }

    auto& instances = prepared.instances;
    std::ranges::for_each(structuralPlan.instances, [&](const StaticRtInstanceTemplate& instanceTemplate) {
        nrAssert(instanceTemplate.packetOrdinal < packets.size(), "AS structural plan packet ordinal is out of range.");
        auto const& packet = packets[instanceTemplate.packetOrdinal];
        auto const entryIt = runtime.blasCache.find(instanceTemplate.mesh);
        nrAssert(
            entryIt != runtime.blasCache.end() && entryIt->second.accelerationStructure.valid(),
            "AS structural plan requires a live BLAS cache entry.");
        instances.push_back(PlannedInstance{
            .mesh = packet.mesh,
            .instance = makeTlasInstance(
                packet, instanceTemplate.baseFlags, entryIt->second.accelerationStructure,
                instanceTemplate.hitRecordBase,
                static_cast<std::uint32_t>(structuralPlan.hitSbtPlan->records.size()),
                instanceTemplate.instanceMetadataIndex),
        });
    });
    if (instances.empty())
    {
        prepared.branchKey = makePreparedAsBranchKey(runtime, prepared, revisions);
        return prepared;
    }
    auto const metadataPlanEnd = std::chrono::steady_clock::now();

    auto& slot = runtime.frameSlots[frameParameters.frameIndex % runtime.frameSlots.size()];
    auto const instanceBytes = static_cast<vk::DeviceSize>(
        instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
    ensureInstanceBuffer(device, slot, instanceBytes);
    auto instanceValues = instances |
                          std::views::transform([](const PlannedInstance& planned) {
                              return planned.instance;
                          }) |
                          std::ranges::to<std::vector>();
    slot.instanceBuffer.writeMappedAndFlush(
        std::span<const vk::AccelerationStructureInstanceKHR>{instanceValues});

    auto const& metadata = structuralPlan.metadata;
    auto const& materialTable = structuralPlan.materialTable;
    if (slot.appliedStructuralPlanGeneration != structuralPlan.generation)
    {
        ensureStorageMetadataBuffer(device, slot.rtInstanceMetadataBuffer, slot.rtInstanceMetadataBufferSize, metadata.instances.size() * sizeof(nr::scene::RtInstanceMetadata), "ASBuild.RT.InstanceMetadata");
        ensureStorageMetadataBuffer(device, slot.rtGeometryMetadataBuffer, slot.rtGeometryMetadataBufferSize, metadata.geometries.size() * sizeof(nr::scene::RtGeometryMetadata), "ASBuild.RT.GeometryMetadata");
        ensureStorageMetadataBuffer(device, slot.rtMaterialHeaderBuffer, slot.rtMaterialHeaderBufferSize, materialTable.headers.size() * sizeof(nr::scene::RtMaterialHeader), "ASBuild.RT.MaterialHeaders");
        ensureStorageMetadataBuffer(device, slot.rtMaterialLayerBuffer, slot.rtMaterialLayerBufferSize, materialTable.layers.size() * sizeof(nr::scene::RtMaterialLayerRecord), "ASBuild.RT.MaterialLayers");
        ensureStorageMetadataBuffer(device, slot.rtMaterialTextureRefBuffer, slot.rtMaterialTextureRefBufferSize, materialTable.textureRefs.size() * sizeof(nr::scene::RtMaterialTextureRef), "ASBuild.RT.MaterialTextureRefs");
        writeMetadataBuffer(slot.rtInstanceMetadataBuffer, std::span<const nr::scene::RtInstanceMetadata>{metadata.instances});
        writeMetadataBuffer(slot.rtGeometryMetadataBuffer, std::span<const nr::scene::RtGeometryMetadata>{metadata.geometries});
        writeMetadataBuffer(slot.rtMaterialHeaderBuffer, std::span<const nr::scene::RtMaterialHeader>{materialTable.headers});
        writeMetadataBuffer(slot.rtMaterialLayerBuffer, std::span<const nr::scene::RtMaterialLayerRecord>{materialTable.layers});
        writeMetadataBuffer(slot.rtMaterialTextureRefBuffer, std::span<const nr::scene::RtMaterialTextureRef>{materialTable.textureRefs});
        slot.appliedStructuralPlanGeneration = structuralPlan.generation;
    }
    auto const cpuWritesEnd = std::chrono::steady_clock::now();

    prepared.tlasInput = nr::rhi::TlasBuildInput{
        .instancesAddress = slot.instanceBuffer.deviceAddress(),
        .instanceCount = static_cast<std::uint32_t>(instances.size()),
    };
    auto const tlasSizes = nr::rhi::queryTlasBuildSizes(
        device.device, prepared.tlasInput, tlasBuildOptions());
    ensureTlas(device, slot, tlasSizes, prepared.tlasInput.instanceCount);
    auto blasScratchBytes = vk::DeviceSize{0u};
    std::ranges::for_each(prepared.dirtyMeshes, [&](nr::resource::MeshHandle mesh) {
        blasScratchBytes = alignUp(blasScratchBytes, runtime.limits.minScratchAlignment);
        blasScratchBytes += runtime.blasCache.at(mesh).cachedBuild.sizes.buildScratchSize;
    });
    ensureScratchBuffer(device, slot, std::max(blasScratchBytes, tlasSizes.buildScratchSize));
    prepared.rtAtlasMesh = instances.front().mesh;
    prepared.available = true;
    auto const sizingEnd = std::chrono::steady_clock::now();

    prepared.cacheScanMilliseconds =
        std::chrono::duration<double, std::milli>(cacheScanEnd - probeStart).count();
    prepared.metadataPlanMilliseconds =
        std::chrono::duration<double, std::milli>(metadataPlanEnd - cacheScanEnd).count();
    prepared.cpuWritesMilliseconds =
        std::chrono::duration<double, std::milli>(cpuWritesEnd - metadataPlanEnd).count();
    prepared.tlasSizingMilliseconds =
        std::chrono::duration<double, std::milli>(sizingEnd - cpuWritesEnd).count();
    prepared.branchKey = makePreparedAsBranchKey(runtime, prepared, revisions);
    return prepared;
}

void declarePreparedAsFrame(
    nr::renderer::NodeBuildContext& context,
    AccelerationStructureBuildRuntimeCache& runtime,
    PreparedAsFrame prepared)
{
    if (!prepared.available)
    {
        return;
    }
    auto& slot = runtime.frameSlots[prepared.frameSlot];
    auto importedBuffers = std::map<const nr::rhi::Buffer*, GraphResourceHandle>{};
    auto blasResourceByMesh = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};
    auto const ownership = nr::renderer::ownershipDomainFromQueue(context.queue);
    auto const scratchResource = context.importBuffer(
        slot.scratchBuffer, "ASBuild.Scratch", ResourceLifetime::FrameLocal,
        {BufferUsageIntent::AccelerationStructureScratch, BufferUsageIntent::ShaderDeviceAddress},
        ownership);
    auto const instanceResource = context.importBuffer(
        slot.instanceBuffer, "ASBuild.TLAS.Instances", ResourceLifetime::FrameLocal,
        {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress},
        ownership);
    auto const tlasResource = context.importAccelerationStructure(
        slot.tlas, "ASBuild.TLAS", ResourceLifetime::FrameLocal, ownership);
    context.publishFrameResource(nr::renderer::frameResource::sceneTlas, tlasResource);

    auto importStorage = [&](const nr::rhi::Buffer& buffer, std::string_view name) {
        return context.importBuffer(
            buffer, name, ResourceLifetime::FrameLocal,
            {BufferUsageIntent::StorageRead}, ownership);
    };
    auto const instanceMetadata = importStorage(
        slot.rtInstanceMetadataBuffer, "ASBuild.RT.InstanceMetadata");
    auto const geometryMetadata = importStorage(
        slot.rtGeometryMetadataBuffer, "ASBuild.RT.GeometryMetadata");
    auto const materialHeaders = importStorage(
        slot.rtMaterialHeaderBuffer, "ASBuild.RT.MaterialHeaders");
    auto const materialLayers = importStorage(
        slot.rtMaterialLayerBuffer, "ASBuild.RT.MaterialLayers");
    auto const materialTextureRefs = importStorage(
        slot.rtMaterialTextureRefBuffer, "ASBuild.RT.MaterialTextureRefs");

    auto const& atlasMesh = runtime.blasCache.at(prepared.rtAtlasMesh).cachedBuild.sceneMesh;
    auto const vertexAtlas = context.importBuffer(
        atlasMesh.vertexBuffer.buffer->get(), "ASBuild.RT.VertexAtlas",
        ResourceLifetime::ScenePersistent, {BufferUsageIntent::StorageRead}, ownership);
    auto indexAtlas = vertexAtlas;
    if (atlasMesh.hasIndexBuffer())
    {
        indexAtlas = context.importBuffer(
            atlasMesh.indexBuffer.buffer->get(), "ASBuild.RT.IndexAtlas",
            ResourceLifetime::ScenePersistent, {BufferUsageIntent::StorageRead}, ownership);
    }
    context.publishFrameResource(nr::renderer::frameResource::sceneRtInstanceMetadata, instanceMetadata);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtGeometryMetadata, geometryMetadata);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialHeaders, materialHeaders);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialLayers, materialLayers);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialTextureRefs, materialTextureRefs);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtVertexAtlas, vertexAtlas);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtIndexAtlas, indexAtlas);
    auto const hitPlan = context.importFrameData(
        "ASBuild.RT.HitSbtPlan", runtime.structuralPlan.hitSbtPlan);
    context.publishFrameData(nr::renderer::frameData::sceneRtHitSbtPlan, hitPlan);

    auto importBlas = [&](nr::resource::MeshHandle mesh) {
        if (auto found = blasResourceByMesh.find(mesh); found != blasResourceByMesh.end())
        {
            return found->second;
        }
        auto& entry = runtime.blasCache.at(mesh);
        auto resource = context.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = std::format("ASBuild.BLAS.mesh{}", mesh.slot),
            .lifetime = ResourceLifetime::RendererPersistent,
            .initialOwnership = entry.retainedState.common.initialized
                                    ? entry.retainedState.common.ownership
                                    : ownership,
            .type = entry.accelerationStructure.type(),
            .size = entry.accelerationStructure.size(),
            .importedResource = std::cref(entry.accelerationStructure),
            .retainedState = std::ref(entry.retainedState),
        });
        blasResourceByMesh.emplace(mesh, resource);
        return resource;
    };

    auto dirtyBlasResources = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};
    std::ranges::for_each(prepared.instances, [&](const PlannedInstance& instance) {
        static_cast<void>(importBlas(instance.mesh));
    });
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
            auto const& cached = runtime.blasCache.at(mesh).cachedBuild;
            auto const geometryOffset = frameData.geometries.size();
            frameData.geometries.insert(
                frameData.geometries.end(), cached.geometries.begin(), cached.geometries.end());
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
                context, importedBuffers, cached.sceneMesh.vertexBuffer.buffer->get(),
                "ASBuild.SceneVertexAtlas", ResourceLifetime::ScenePersistent,
                {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress}));
            if (cached.sceneMesh.hasIndexBuffer())
            {
                importedInputs.insert(importBufferOnce(
                    context, importedBuffers, cached.sceneMesh.indexBuffer.buffer->get(),
                    "ASBuild.SceneIndexAtlas", ResourceLifetime::ScenePersistent,
                    {BufferUsageIntent::AccelerationStructureBuildInput, BufferUsageIntent::ShaderDeviceAddress}));
            }
        });
        std::ranges::for_each(importedInputs, [&](GraphResourceHandle resource) {
            uses.push_back(nr::renderer::use::accelerationStructureBuildInputRead(resource));
        });
        blasFrameDataHandle = context.importFrameData(
            "ASBuild.BLAS.FrameData", std::move(frameData));
        static_cast<void>(context.addPass(
            uses, "ASBuild.BuildBLAS",
            [blasFrameDataHandle](const nr::renderer::PassRecordContext& recordContext) {
                auto const& data = recordContext.frameData<BlasBuildFrameData>(blasFrameDataHandle);
                auto records = makeBatchRecords(data);
                nr::rhi::recordBuildBlasBatch(
                    recordContext.commandBuffer->get(), records, data.scratchAlignment);
            }));
    }

    auto tlasUses = std::vector<nr::renderer::PassResourceUseDesc>{
        nr::renderer::use::accelerationStructureBuildInputRead(instanceResource),
        nr::renderer::use::orderedAfterPrevious(
            nr::renderer::use::accelerationStructureScratchWrite(scratchResource)),
        nr::renderer::use::accelerationStructureBuildWrite(tlasResource),
    };
    auto blasReads = std::map<GraphResourceHandle, bool>{};
    std::ranges::for_each(prepared.instances, [&](const PlannedInstance& instance) {
        auto const resource = blasResourceByMesh.at(instance.mesh);
        blasReads.insert_or_assign(resource, dirtyBlasResources.contains(instance.mesh));
    });
    std::ranges::for_each(blasReads, [&](const auto& pair) {
        auto read = nr::renderer::use::accelerationStructureBuildRead(pair.first);
        tlasUses.push_back(pair.second
                               ? nr::renderer::use::orderedAfterPrevious(read)
                               : read);
    });
    auto const tlasFrameData = context.importFrameData(
        "ASBuild.TLAS.FrameData",
        TlasBuildFrameData{
            .dst = std::cref(slot.tlas),
            .instanceBuffer = std::cref(slot.instanceBuffer),
            .scratchBuffer = std::cref(slot.scratchBuffer),
            .scratchAddress = slot.scratchBuffer.deviceAddress(),
            .buildInput = prepared.tlasInput,
            .options = tlasBuildOptions(),
            .scratchAlignment = runtime.limits.minScratchAlignment,
        });
    static_cast<void>(context.addPass(
        tlasUses, "ASBuild.BuildTLAS",
        [tlasFrameData](const nr::renderer::PassRecordContext& recordContext) {
            auto const& data = recordContext.frameData<TlasBuildFrameData>(tlasFrameData);
            nr::rhi::recordBuildTlas(
                recordContext.commandBuffer->get(),
                nr::rhi::TlasBuildRecordInfo{
                    .dst = data.dst.get(),
                    .instanceBuffer = data.instanceBuffer.get(),
                    .scratchBuffer = data.scratchBuffer.get(),
                    .scratchAddress = data.scratchAddress,
                    .buildInput = data.buildInput,
                    .options = data.options,
                },
                data.scratchAlignment);
        }));

    if (context.benchmarkTelemetry.has_value())
    {
        auto& telemetry = context.benchmarkTelemetry->get().accelerationStructure.get();
        telemetry.recorded = true;
        telemetry.available = true;
        telemetry.cacheScanMilliseconds = prepared.cacheScanMilliseconds;
        telemetry.metadataPlanMilliseconds = prepared.metadataPlanMilliseconds;
        telemetry.cpuWritesMilliseconds = prepared.cpuWritesMilliseconds;
        telemetry.tlasSizingMilliseconds = prepared.tlasSizingMilliseconds;
        telemetry.instanceCount = prepared.instances.size();
        telemetry.dirtyBlasCount = prepared.dirtyMeshes.size();
    }
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
using GraphResourceHandle = nr::renderer::GraphResourceHandle;
using PassResourceUseDesc = nr::renderer::PassResourceUseDesc;
using ResourceLifetime = nr::renderer::ResourceLifetime;
using ResourceOwnershipDomain = nr::renderer::ResourceOwnershipDomain;
using BufferUsageIntent = nr::renderer::BufferUsageIntent;
namespace use = nr::renderer::use;

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

[[nodiscard]] std::optional<nr::renderer::NodeRuntime::StructuralSnapshot>
AccelerationStructureBuildNode::structuralSnapshot(const NodeFrameParameters& frameParameters) const
{
    nrAssert(static_cast<bool>(runtime_) && device_.has_value(), "AS Skeleton preflight requires initialized state.");
    runtime_->preparedFrame = detail::prepareAsFrame(
        *runtime_, device_->get(), input, frameParameters);
    auto branch = runtime_->preparedFrame->branchKey;
    branch += std::format(";retireLatency={}", input.unusedFrameRetireLatency);
    return StructuralSnapshot{
        .configurationRevision = std::max<std::uint64_t>(1u, std::hash<std::string>{}(branch)),
        .branchKey = std::move(branch),
    };
}

bool AccelerationStructureBuildNode::materializeRenderGraphSkeleton(
    nr::renderer::RenderGraphSkeletonPatchContext& context,
    const NodeFrameParameters& frameParameters,
    const StructuralSnapshot& snapshot)
{
    nrAssert(static_cast<bool>(runtime_) && device_.has_value(), "AS Skeleton patch requires initialized state.");
    if (!runtime_->preparedFrame.has_value())
    {
        return false;
    }
    auto expectedBranch = runtime_->preparedFrame->branchKey;
    expectedBranch += std::format(";retireLatency={}", input.unusedFrameRetireLatency);
    if (snapshot.branchKey != expectedBranch)
    {
        return false;
    }
    auto prepared = std::move(*runtime_->preparedFrame);
    runtime_->preparedFrame.reset();
    if (!prepared.available)
    {
        if (frameParameters.benchmarkTelemetry.has_value())
        {
            auto& telemetry = frameParameters.benchmarkTelemetry->get().accelerationStructure.get();
            telemetry.recorded = true;
        }
        return true;
    }

    auto& runtime = *runtime_;
    auto& slot = runtime.frameSlots[prepared.frameSlot];
    auto const ownership = nr::renderer::ownershipDomainFromQueue(context.queue());
    auto resourceSlot = std::size_t{0u};
    auto patchBuffer = [&](const nr::rhi::Buffer& buffer, std::string debugName,
                           nr::renderer::ResourceLifetime lifetime,
                           std::vector<nr::renderer::BufferUsageIntent> usages) {
        auto const handle = context.resource(resourceSlot);
        context.patchResource(resourceSlot++, nr::renderer::GraphImportedBufferDesc{
            .debugName = std::move(debugName),
            .lifetime = lifetime,
            .initialOwnership = ownership,
            .size = buffer.size(),
            .usageIntents = std::move(usages),
            .importedResource = std::cref(buffer),
        });
        return handle;
    };
    auto patchAs = [&](const nr::rhi::AccelerationStructureResource& accelerationStructure,
                       std::string debugName,
                       nr::renderer::ResourceLifetime lifetime,
                       std::optional<std::reference_wrapper<
                           nr::renderer::RetainedAccelerationStructureState>> retainedState = {}) {
        auto const handle = context.resource(resourceSlot);
        auto const initialOwnership =
            retainedState.has_value() && retainedState->get().common.initialized
                ? retainedState->get().common.ownership
                : ownership;
        context.patchResource(
            resourceSlot++, nr::renderer::GraphImportedAccelerationStructureDesc{
                .debugName = std::move(debugName),
                .lifetime = lifetime,
                .initialOwnership = initialOwnership,
                .type = accelerationStructure.type(),
                .size = accelerationStructure.size(),
                .importedResource = std::cref(accelerationStructure),
                .retainedState = retainedState,
            });
        return handle;
    };

    auto const scratchResource = patchBuffer(
        slot.scratchBuffer, "ASBuild.Scratch", nr::renderer::ResourceLifetime::FrameLocal,
        {nr::renderer::BufferUsageIntent::AccelerationStructureScratch,
         nr::renderer::BufferUsageIntent::ShaderDeviceAddress});
    auto const instanceResource = patchBuffer(
        slot.instanceBuffer, "ASBuild.TLAS.Instances", nr::renderer::ResourceLifetime::FrameLocal,
        {nr::renderer::BufferUsageIntent::AccelerationStructureBuildInput,
         nr::renderer::BufferUsageIntent::ShaderDeviceAddress});
    auto const tlasResource = patchAs(
        slot.tlas, "ASBuild.TLAS", nr::renderer::ResourceLifetime::FrameLocal);
    static_cast<void>(scratchResource);
    static_cast<void>(instanceResource);
    static_cast<void>(tlasResource);
    patchBuffer(slot.rtInstanceMetadataBuffer, "ASBuild.RT.InstanceMetadata",
                nr::renderer::ResourceLifetime::FrameLocal,
                {nr::renderer::BufferUsageIntent::StorageRead});
    patchBuffer(slot.rtGeometryMetadataBuffer, "ASBuild.RT.GeometryMetadata",
                nr::renderer::ResourceLifetime::FrameLocal,
                {nr::renderer::BufferUsageIntent::StorageRead});
    patchBuffer(slot.rtMaterialHeaderBuffer, "ASBuild.RT.MaterialHeaders",
                nr::renderer::ResourceLifetime::FrameLocal,
                {nr::renderer::BufferUsageIntent::StorageRead});
    patchBuffer(slot.rtMaterialLayerBuffer, "ASBuild.RT.MaterialLayers",
                nr::renderer::ResourceLifetime::FrameLocal,
                {nr::renderer::BufferUsageIntent::StorageRead});
    patchBuffer(slot.rtMaterialTextureRefBuffer, "ASBuild.RT.MaterialTextureRefs",
                nr::renderer::ResourceLifetime::FrameLocal,
                {nr::renderer::BufferUsageIntent::StorageRead});
    auto const& atlasMesh = runtime.blasCache.at(prepared.rtAtlasMesh).cachedBuild.sceneMesh;
    patchBuffer(atlasMesh.vertexBuffer.buffer->get(), "ASBuild.RT.VertexAtlas",
                nr::renderer::ResourceLifetime::ScenePersistent,
                {nr::renderer::BufferUsageIntent::StorageRead});
    if (atlasMesh.hasIndexBuffer())
    {
        patchBuffer(atlasMesh.indexBuffer.buffer->get(), "ASBuild.RT.IndexAtlas",
                    nr::renderer::ResourceLifetime::ScenePersistent,
                    {nr::renderer::BufferUsageIntent::StorageRead});
    }

    auto frameDataSlot = std::size_t{0u};
    context.patchFrameData(
        frameDataSlot++, "ASBuild.RT.HitSbtPlan",
        std::make_any<std::shared_ptr<const SceneRtHitSbtPlan>>(
            runtime.structuralPlan.hitSbtPlan));

    auto patchedBlas = std::set<nr::resource::MeshHandle>{};
    std::ranges::for_each(prepared.instances, [&](const detail::PlannedInstance& instance) {
        if (!patchedBlas.insert(instance.mesh).second)
        {
            return;
        }
        auto& entry = runtime.blasCache.at(instance.mesh);
        patchAs(
            entry.accelerationStructure,
            std::format("ASBuild.BLAS.mesh{}", instance.mesh.slot),
            nr::renderer::ResourceLifetime::RendererPersistent,
            std::ref(entry.retainedState));
    });

    auto blasFrameDataHandle = nr::renderer::GraphFrameDataHandle{};
    auto passSlot = std::size_t{0u};
    if (!prepared.dirtyMeshes.empty())
    {
        auto frameData = detail::BlasBuildFrameData{
            .scratchAlignment = runtime.limits.minScratchAlignment,
        };
        auto scratchOffset = vk::DeviceSize{0u};
        auto importedInputs = std::map<const nr::rhi::Buffer*, GraphResourceHandle>{};
        std::ranges::for_each(prepared.dirtyMeshes, [&](nr::resource::MeshHandle mesh) {
            auto const& cached = runtime.blasCache.at(mesh).cachedBuild;
            auto const geometryOffset = frameData.geometries.size();
            frameData.geometries.insert(
                frameData.geometries.end(), cached.geometries.begin(), cached.geometries.end());
            scratchOffset = detail::alignUp(scratchOffset, runtime.limits.minScratchAlignment);
            frameData.works.push_back(detail::BlasBuildWork{
                .dst = std::cref(runtime.blasCache.at(mesh).accelerationStructure),
                .scratchBuffer = std::cref(slot.scratchBuffer),
                .geometryOffset = geometryOffset,
                .geometryCount = cached.geometries.size(),
                .scratchAddress = slot.scratchBuffer.deviceAddress() + scratchOffset,
                .scratchSize = cached.sizes.buildScratchSize,
                .options = detail::staticBlasBuildOptions(),
            });
            scratchOffset += cached.sizes.buildScratchSize;
            auto patchInput = [&](const nr::rhi::Buffer& buffer, std::string_view name) {
                if (importedInputs.contains(std::addressof(buffer)))
                {
                    return;
                }
                auto handle = patchBuffer(
                    buffer, std::string{name}, nr::renderer::ResourceLifetime::ScenePersistent,
                    {nr::renderer::BufferUsageIntent::AccelerationStructureBuildInput,
                     nr::renderer::BufferUsageIntent::ShaderDeviceAddress});
                importedInputs.emplace(std::addressof(buffer), handle);
            };
            patchInput(cached.sceneMesh.vertexBuffer.buffer->get(), "ASBuild.SceneVertexAtlas");
            if (cached.sceneMesh.hasIndexBuffer())
            {
                patchInput(cached.sceneMesh.indexBuffer.buffer->get(), "ASBuild.SceneIndexAtlas");
            }
        });
        blasFrameDataHandle = context.frameData(frameDataSlot);
        context.patchFrameData(
            frameDataSlot++, "ASBuild.BLAS.FrameData",
            std::make_any<detail::BlasBuildFrameData>(std::move(frameData)));
        context.patchPass(
            passSlot++, "ASBuild.BuildBLAS", nullptr,
            [blasFrameDataHandle](const nr::renderer::PassRecordContext& recordContext) {
                auto const& data =
                    recordContext.frameData<detail::BlasBuildFrameData>(blasFrameDataHandle);
                auto records = detail::makeBatchRecords(data);
                nr::rhi::recordBuildBlasBatch(
                    recordContext.commandBuffer->get(), records, data.scratchAlignment);
            });
    }

    auto const tlasFrameDataHandle = context.frameData(frameDataSlot);
    context.patchFrameData(
        frameDataSlot, "ASBuild.TLAS.FrameData",
        std::make_any<detail::TlasBuildFrameData>(detail::TlasBuildFrameData{
            .dst = std::cref(slot.tlas),
            .instanceBuffer = std::cref(slot.instanceBuffer),
            .scratchBuffer = std::cref(slot.scratchBuffer),
            .scratchAddress = slot.scratchBuffer.deviceAddress(),
            .buildInput = prepared.tlasInput,
            .options = detail::tlasBuildOptions(),
            .scratchAlignment = runtime.limits.minScratchAlignment,
        }));
    context.patchPass(
        passSlot, "ASBuild.BuildTLAS", nullptr,
        [tlasFrameDataHandle](const nr::renderer::PassRecordContext& recordContext) {
            auto const& data =
                recordContext.frameData<detail::TlasBuildFrameData>(tlasFrameDataHandle);
            nr::rhi::recordBuildTlas(
                recordContext.commandBuffer->get(),
                nr::rhi::TlasBuildRecordInfo{
                    .dst = data.dst.get(),
                    .instanceBuffer = data.instanceBuffer.get(),
                    .scratchBuffer = data.scratchBuffer.get(),
                    .scratchAddress = data.scratchAddress,
                    .buildInput = data.buildInput,
                    .options = data.options,
                },
                data.scratchAlignment);
        });

    if (frameParameters.benchmarkTelemetry.has_value())
    {
        auto& telemetry = frameParameters.benchmarkTelemetry->get().accelerationStructure.get();
        telemetry.recorded = true;
        telemetry.available = true;
        telemetry.cacheScanMilliseconds = prepared.cacheScanMilliseconds;
        telemetry.metadataPlanMilliseconds = prepared.metadataPlanMilliseconds;
        telemetry.cpuWritesMilliseconds = prepared.cpuWritesMilliseconds;
        telemetry.tlasSizingMilliseconds = prepared.tlasSizingMilliseconds;
        telemetry.graphDeclareMilliseconds = 0.0;
        telemetry.instanceCount = prepared.instances.size();
        telemetry.dirtyBlasCount = prepared.dirtyMeshes.size();
    }
    return true;
}

void AccelerationStructureBuildNode::materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nrAssert(static_cast<bool>(runtime_), "AccelerationStructureBuild build stage requires initialized runtime state.");
    nrAssert(device_.has_value(), "AccelerationStructureBuild build stage requires device reference from initialize stage.");
    if (runtime_->preparedFrame.has_value())
    {
        auto prepared = std::move(*runtime_->preparedFrame);
        runtime_->preparedFrame.reset();
        detail::declarePreparedAsFrame(context, *runtime_, std::move(prepared));
        return;
    }

    auto benchmarkTelemetry = context.benchmarkTelemetry;
    auto const probeStart = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    if (benchmarkTelemetry.has_value())
    {
        benchmarkTelemetry->get().accelerationStructure.get().recorded = true;
    }

    auto &runtime = *runtime_;
    auto &device = device_->get();
    ++runtime.frameSerial;
    detail::reapRetiredBlas(runtime);

    if (!frameParameters.scene.has_value() || !frameParameters.sceneTlasBuildInputs.has_value())
    {
        return;
    }

    auto const &scene = frameParameters.scene->get();
    auto const &packets = frameParameters.sceneTlasBuildInputs->get();
    auto revisions = frameParameters.sceneRevisions.valid() ? frameParameters.sceneRevisions : scene.revisionsSnapshot();
    if (benchmarkTelemetry.has_value())
    {
        benchmarkTelemetry->get().accelerationStructure.get().packetCount = packets.size();
    }
    auto const retireDelay = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);
    if (runtime.activeSceneIdentity != revisions.sceneIdentity)
    {
        detail::retireCurrentBlasAtlas(runtime, retireDelay);
        runtime.blasCache.clear();
        runtime.rtMaterialCache.clear();
        runtime.structuralPlan = {};
        runtime.blasRevisions.reset();
        std::ranges::for_each(runtime.frameSlots, [](detail::FrameSlotAsResources &slot) { slot.appliedStructuralPlanGeneration = 0u; });
        runtime.activeSceneIdentity = revisions.sceneIdentity;
    }
    auto const blasRevisions = detail::BlasRevisionProjection::capture(revisions.rt);
    if (runtime.blasRevisions.has_value() && *runtime.blasRevisions != blasRevisions)
    {
        detail::retireCurrentBlasAtlas(runtime, retireDelay);
        std::ranges::for_each(runtime.blasCache, [](auto &entry) { entry.second.cachedBuild = {}; });
    }
    runtime.blasRevisions = blasRevisions;
    detail::pruneBlasCache(runtime, scene, input.unusedFrameRetireLatency, retireDelay);
    detail::pruneRtMaterialCache(runtime, scene, input.unusedFrameRetireLatency);
    if (packets.empty())
    {
        return;
    }

    auto pendingByMesh = std::map<nr::resource::MeshHandle, detail::PendingBlasBuild>{};
    auto instances = std::vector<detail::PlannedInstance>{};
    instances.reserve(packets.size());

    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        if (pendingByMesh.contains(packet.mesh))
        {
            auto &entry = runtime.blasCache.try_emplace(packet.mesh).first->second;
            entry.lastSeenFrameSerial = runtime.frameSerial;
            return;
        }

        auto [entryIt, inserted] = runtime.blasCache.try_emplace(packet.mesh);
        auto cachedBuild = detail::ensureCachedBlasBuild(scene, device, packet.mesh, entryIt->second);
        if (!cachedBuild.has_value())
        {
            if (inserted)
            {
                runtime.blasCache.erase(entryIt);
            }
            return;
        }

        entryIt->second.lastSeenFrameSerial = runtime.frameSerial;
        pendingByMesh.emplace(packet.mesh, detail::PendingBlasBuild{
                                               .mesh = packet.mesh,
                                               .cachedBuild = *cachedBuild,
                                           });
    });

    if (pendingByMesh.empty())
    {
        return;
    }

    auto pendingRefs = std::vector<std::reference_wrapper<const detail::PendingBlasBuild>>{};
    pendingRefs.reserve(pendingByMesh.size());
    std::ranges::for_each(pendingByMesh, [&](const auto &pair) { pendingRefs.push_back(std::cref(pair.second)); });

    auto dirtyBuilds = std::vector<std::reference_wrapper<const detail::PendingBlasBuild>>{};
    dirtyBuilds.reserve(pendingRefs.size());
    std::ranges::for_each(pendingRefs, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        auto const &pending = pendingRef.get();
        auto &entry = runtime.blasCache.try_emplace(pending.mesh).first->second;
        if (detail::blasEntryNeedsBuild(detail::BlasBuildDirtyContext{
                .entry = entry,
                .cached = pending.cachedBuild.get(),
                .atlas = runtime.blasAtlas,
            }))
        {
            dirtyBuilds.push_back(pendingRef);
        }
    });

    auto atlasCursor = runtime.blasAtlas.usedBytes;
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) { atlasCursor = detail::alignedBlasSliceEnd(atlasCursor, pendingRef.get().cachedBuild.get().sizes.accelerationStructureSize); });
    auto const capacityOverflow = runtime.blasAtlas.buffer.valid() && atlasCursor > runtime.blasAtlas.capacityBytes;
    auto const needsAtlasRepack = !runtime.blasAtlas.buffer.valid() || capacityOverflow;

    auto repackBuilds = std::map<nr::resource::MeshHandle, detail::PendingBlasBuild>{};
    if (needsAtlasRepack)
    {
        repackBuilds = pendingByMesh;
        std::ranges::for_each(runtime.blasCache, [&](auto &pair) {
            auto const meshHandle = pair.first;
            if (repackBuilds.contains(meshHandle))
            {
                return;
            }

            auto cachedBuild = detail::ensureCachedBlasBuild(scene, device, meshHandle, pair.second);
            if (!cachedBuild.has_value())
            {
                return;
            }

            repackBuilds.emplace(meshHandle, detail::PendingBlasBuild{
                                                 .mesh = meshHandle,
                                                 .cachedBuild = *cachedBuild,
                                             });
        });

        auto repackRefs = std::vector<std::reference_wrapper<const detail::PendingBlasBuild>>{};
        repackRefs.reserve(repackBuilds.size());
        std::ranges::for_each(repackBuilds, [&](const auto &pair) { repackRefs.push_back(std::cref(pair.second)); });

        auto const requiredAtlasBytes = detail::requiredBlasAtlasBytes(std::span<const std::reference_wrapper<const detail::PendingBlasBuild>>{repackRefs.data(), repackRefs.size()});
        detail::retireCurrentBlasAtlas(runtime, retireDelay);
        detail::createBlasAtlas(runtime, device, requiredAtlasBytes, capacityOverflow);
        dirtyBuilds = std::move(repackRefs);
    }

    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        auto &entry = detail::ensureBlasEntry(runtime, device, pendingRef.get(), retireDelay);
        entry.lastSeenFrameSerial = runtime.frameSerial;
    });
    auto const cacheScanEnd = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : probeStart;

    auto structuralKey = detail::makeAsStructuralPlanKey(revisions, packets, pendingByMesh);
    auto &structuralPlan = runtime.structuralPlan;
    if (!structuralPlan.key.has_value() || *structuralPlan.key != structuralKey)
    {
        auto metadata = detail::RtMetadataBuildState{};
        auto hitSbtPlan = SceneRtHitSbtPlan{};
        auto hitPermutationLookup = std::map<RtHitPermutationKey, std::uint32_t>{};
        auto templates = std::vector<detail::StaticRtInstanceTemplate>{};
        auto const materialRevisions = detail::RtMaterialRevisionProjection::capture(revisions.rt);
        templates.reserve(packets.size());

        auto packetOrdinals = std::views::iota(std::size_t{0}, packets.size());
        std::ranges::for_each(packetOrdinals, [&](std::size_t packetOrdinal) {
            auto const &packet = packets[packetOrdinal];
            auto entryIt = runtime.blasCache.find(packet.mesh);
            auto pendingIt = pendingByMesh.find(packet.mesh);
            if (entryIt == runtime.blasCache.end() || !entryIt->second.accelerationStructure.valid() || pendingIt == pendingByMesh.end())
            {
                return;
            }

            auto const &sceneMesh = pendingIt->second.cachedBuild.get().sceneMesh;
            auto const appended = detail::appendRtInstanceMetadata(runtime, metadata, hitSbtPlan, hitPermutationLookup, scene, packet, sceneMesh, materialRevisions);
            templates.push_back(detail::StaticRtInstanceTemplate{
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
            return;
        }

        finalizeSceneRtHitSbtPlan(hitSbtPlan);
        nrAssert(hitSbtPlan.valid(), "AS build generated an invalid RT hit SBT plan.");
        auto const nextGeneration = structuralPlan.generation == std::numeric_limits<std::uint64_t>::max() ? 1u : structuralPlan.generation + 1u;
        auto materialTable = detail::makeRtMaterialTable(metadata);
        auto sharedHitSbtPlan = std::make_shared<const SceneRtHitSbtPlan>(std::move(hitSbtPlan));
        structuralPlan = detail::RtStructuralPlanCache{
            .key = std::move(structuralKey),
            .metadata = std::move(metadata),
            .materialTable = std::move(materialTable),
            .hitSbtPlan = std::move(sharedHitSbtPlan),
            .instances = std::move(templates),
            .generation = nextGeneration,
        };
    }

    nrAssert(static_cast<bool>(structuralPlan.hitSbtPlan), "AS structural plan requires an immutable hit SBT plan.");
    instances.reserve(structuralPlan.instances.size());
    std::ranges::for_each(structuralPlan.instances, [&](const detail::StaticRtInstanceTemplate &instanceTemplate) {
        nrAssert(instanceTemplate.packetOrdinal < packets.size(), "AS structural plan packet ordinal is out of range.");
        auto const &packet = packets[instanceTemplate.packetOrdinal];
        nrAssert(static_cast<std::uint64_t>(packet.renderable.id()) == instanceTemplate.renderableId && packet.mesh == instanceTemplate.mesh && packet.tlasBucket == instanceTemplate.tlasBucket, "AS structural plan instance template no longer matches ordered packet topology.");
        auto entryIt = runtime.blasCache.find(instanceTemplate.mesh);
        nrAssert(entryIt != runtime.blasCache.end() && entryIt->second.accelerationStructure.valid(), "AS structural plan requires a live BLAS cache entry.");
        instances.push_back(detail::PlannedInstance{
            .mesh = packet.mesh,
            .instance = detail::makeTlasInstance(packet, instanceTemplate.baseFlags, entryIt->second.accelerationStructure, instanceTemplate.hitRecordBase, static_cast<std::uint32_t>(structuralPlan.hitSbtPlan->records.size()), instanceTemplate.instanceMetadataIndex),
        });
    });

    if (instances.empty())
    {
        return;
    }
    auto const metadataPlanEnd = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : probeStart;

    auto &slot = runtime.frameSlots[context.frameIndex % runtime.frameSlots.size()];
    auto const instanceBytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
    detail::ensureInstanceBuffer(device, slot, instanceBytes);

    auto instanceValues = instances | std::views::transform([](const detail::PlannedInstance &planned) { return planned.instance; }) | std::ranges::to<std::vector>();
    slot.instanceBuffer.writeMappedAndFlush(std::span<const vk::AccelerationStructureInstanceKHR>{instanceValues});

    auto const &rtMetadata = structuralPlan.metadata;
    auto const &rtMaterialTable = structuralPlan.materialTable;
    if (slot.appliedStructuralPlanGeneration != structuralPlan.generation)
    {
        detail::ensureStorageMetadataBuffer(device, slot.rtInstanceMetadataBuffer, slot.rtInstanceMetadataBufferSize, static_cast<vk::DeviceSize>(rtMetadata.instances.size() * sizeof(nr::scene::RtInstanceMetadata)), "ASBuild.RT.InstanceMetadata");
        detail::ensureStorageMetadataBuffer(device, slot.rtGeometryMetadataBuffer, slot.rtGeometryMetadataBufferSize, static_cast<vk::DeviceSize>(rtMetadata.geometries.size() * sizeof(nr::scene::RtGeometryMetadata)), "ASBuild.RT.GeometryMetadata");
        detail::ensureStorageMetadataBuffer(device, slot.rtMaterialHeaderBuffer, slot.rtMaterialHeaderBufferSize, static_cast<vk::DeviceSize>(rtMaterialTable.headers.size() * sizeof(nr::scene::RtMaterialHeader)), "ASBuild.RT.MaterialHeaders");
        detail::ensureStorageMetadataBuffer(device, slot.rtMaterialLayerBuffer, slot.rtMaterialLayerBufferSize, static_cast<vk::DeviceSize>(rtMaterialTable.layers.size() * sizeof(nr::scene::RtMaterialLayerRecord)), "ASBuild.RT.MaterialLayers");
        detail::ensureStorageMetadataBuffer(device, slot.rtMaterialTextureRefBuffer, slot.rtMaterialTextureRefBufferSize, static_cast<vk::DeviceSize>(rtMaterialTable.textureRefs.size() * sizeof(nr::scene::RtMaterialTextureRef)), "ASBuild.RT.MaterialTextureRefs");

        detail::writeMetadataBuffer(slot.rtInstanceMetadataBuffer, std::span<const nr::scene::RtInstanceMetadata>{rtMetadata.instances});
        detail::writeMetadataBuffer(slot.rtGeometryMetadataBuffer, std::span<const nr::scene::RtGeometryMetadata>{rtMetadata.geometries});
        detail::writeMetadataBuffer(slot.rtMaterialHeaderBuffer, std::span<const nr::scene::RtMaterialHeader>{rtMaterialTable.headers});
        detail::writeMetadataBuffer(slot.rtMaterialLayerBuffer, std::span<const nr::scene::RtMaterialLayerRecord>{rtMaterialTable.layers});
        detail::writeMetadataBuffer(slot.rtMaterialTextureRefBuffer, std::span<const nr::scene::RtMaterialTextureRef>{rtMaterialTable.textureRefs});
        slot.appliedStructuralPlanGeneration = structuralPlan.generation;
    }
    auto const cpuWritesEnd = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : probeStart;

    auto const tlasInput = nr::rhi::TlasBuildInput{
        .instancesAddress = slot.instanceBuffer.deviceAddress(),
        .instanceCount = static_cast<std::uint32_t>(instances.size()),
    };
    auto const tlasSizes = nr::rhi::queryTlasBuildSizes(device.device, tlasInput, detail::tlasBuildOptions());

    detail::ensureTlas(device, slot, tlasSizes, tlasInput.instanceCount);

    auto blasScratchBytes = vk::DeviceSize{0};
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        blasScratchBytes = detail::alignUp(blasScratchBytes, runtime.limits.minScratchAlignment);
        blasScratchBytes += pendingRef.get().cachedBuild.get().sizes.buildScratchSize;
    });
    auto const requiredScratchBytes = std::max(blasScratchBytes, tlasSizes.buildScratchSize);
    detail::ensureScratchBuffer(device, slot, requiredScratchBytes);
    auto const tlasSizingEnd = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : probeStart;

    auto importedBuffers = std::map<const nr::rhi::Buffer *, GraphResourceHandle>{};
    auto blasResourceByMesh = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};

    auto const scratchResource = context.importBuffer(slot.scratchBuffer, "ASBuild.Scratch", ResourceLifetime::FrameLocal,
                                                      {
                                                          BufferUsageIntent::AccelerationStructureScratch,
                                                          BufferUsageIntent::ShaderDeviceAddress,
                                                      },
                                                      nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const instanceResource = context.importBuffer(slot.instanceBuffer, "ASBuild.TLAS.Instances", ResourceLifetime::FrameLocal,
                                                       {
                                                           BufferUsageIntent::AccelerationStructureBuildInput,
                                                           BufferUsageIntent::ShaderDeviceAddress,
                                                       },
                                                       nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const tlasResource = context.importAccelerationStructure(slot.tlas, "ASBuild.TLAS", ResourceLifetime::FrameLocal, nr::renderer::ownershipDomainFromQueue(context.queue));
    context.publishFrameResource(nr::renderer::frameResource::sceneTlas, tlasResource);

    auto const rtInstanceMetadataResource = context.importBuffer(slot.rtInstanceMetadataBuffer, "ASBuild.RT.InstanceMetadata", ResourceLifetime::FrameLocal,
                                                                 {
                                                                     BufferUsageIntent::StorageRead,
                                                                 },
                                                                 nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const rtGeometryMetadataResource = context.importBuffer(slot.rtGeometryMetadataBuffer, "ASBuild.RT.GeometryMetadata", ResourceLifetime::FrameLocal,
                                                                 {
                                                                     BufferUsageIntent::StorageRead,
                                                                 },
                                                                 nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const rtMaterialHeaderResource = context.importBuffer(slot.rtMaterialHeaderBuffer, "ASBuild.RT.MaterialHeaders", ResourceLifetime::FrameLocal,
                                                               {
                                                                   BufferUsageIntent::StorageRead,
                                                               },
                                                               nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const rtMaterialLayerResource = context.importBuffer(slot.rtMaterialLayerBuffer, "ASBuild.RT.MaterialLayers", ResourceLifetime::FrameLocal,
                                                              {
                                                                  BufferUsageIntent::StorageRead,
                                                              },
                                                              nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const rtMaterialTextureRefResource = context.importBuffer(slot.rtMaterialTextureRefBuffer, "ASBuild.RT.MaterialTextureRefs", ResourceLifetime::FrameLocal,
                                                                   {
                                                                       BufferUsageIntent::StorageRead,
                                                                   },
                                                                   nr::renderer::ownershipDomainFromQueue(context.queue));
    auto atlasSourceIt = std::ranges::find_if(instances, [&](const detail::PlannedInstance &planned) { return pendingByMesh.contains(planned.mesh); });
    nrAssert(atlasSourceIt != instances.end(), "AS build expected a planned instance with a scene geometry atlas.");
    auto const &rtAtlasMesh = pendingByMesh.at(atlasSourceIt->mesh).cachedBuild.get().sceneMesh;
    nrAssert(rtAtlasMesh.hasVertexBuffer(), "AS build requires a vertex atlas for RT vertex-data binding.");
    auto const &vertexAtlasBuffer = rtAtlasMesh.vertexBuffer.buffer->get();
    auto const rtVertexAtlasResource = context.importBuffer(vertexAtlasBuffer, "ASBuild.RT.VertexAtlas", ResourceLifetime::ScenePersistent,
                                                            {
                                                                BufferUsageIntent::StorageRead,
                                                            },
                                                            nr::renderer::ownershipDomainFromQueue(context.queue));
    auto rtIndexAtlasResource = rtVertexAtlasResource;
    if (rtAtlasMesh.hasIndexBuffer())
    {
        rtIndexAtlasResource = context.importBuffer(rtAtlasMesh.indexBuffer.buffer->get(), "ASBuild.RT.IndexAtlas", ResourceLifetime::ScenePersistent,
                                                    {
                                                        BufferUsageIntent::StorageRead,
                                                    },
                                                    nr::renderer::ownershipDomainFromQueue(context.queue));
    }

    context.publishFrameResource(nr::renderer::frameResource::sceneRtInstanceMetadata, rtInstanceMetadataResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtGeometryMetadata, rtGeometryMetadataResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialHeaders, rtMaterialHeaderResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialLayers, rtMaterialLayerResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtMaterialTextureRefs, rtMaterialTextureRefResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtVertexAtlas, rtVertexAtlasResource);
    context.publishFrameResource(nr::renderer::frameResource::sceneRtIndexAtlas, rtIndexAtlasResource);
    auto const rtHitSbtPlanFrameData = context.importFrameData("ASBuild.RT.HitSbtPlan", structuralPlan.hitSbtPlan);
    context.publishFrameData(nr::renderer::frameData::sceneRtHitSbtPlan, rtHitSbtPlanFrameData);

    auto importBlasResource = [&](nr::resource::MeshHandle meshHandle) {
        if (auto it = blasResourceByMesh.find(meshHandle); it != blasResourceByMesh.end())
        {
            return it->second;
        }

        auto cacheIt = runtime.blasCache.find(meshHandle);
        nrAssert(cacheIt != runtime.blasCache.end(), "AS build node expected a BLAS cache entry.");
        auto &entry = cacheIt->second;
        auto const ownership = nr::renderer::ownershipDomainFromQueue(context.queue);
        auto resource = context.addResource(nr::renderer::GraphImportedAccelerationStructureDesc{
            .debugName = std::format("ASBuild.BLAS.mesh{}", meshHandle.slot),
            .lifetime = ResourceLifetime::RendererPersistent,
            .initialOwnership = entry.retainedState.common.initialized
                                    ? entry.retainedState.common.ownership
                                    : ownership,
            .type = entry.accelerationStructure.type(),
            .size = entry.accelerationStructure.size(),
            .importedResource = std::cref(entry.accelerationStructure),
            .retainedState = std::ref(entry.retainedState),
        });
        blasResourceByMesh.emplace(meshHandle, resource);
        return resource;
    };

    auto dirtyBlasResourceByMesh = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};
    std::ranges::for_each(instances, [&](const detail::PlannedInstance &instance) {
        static_cast<void>(importBlasResource(instance.mesh));
    });
    if (!dirtyBuilds.empty())
    {
        auto blasFrameData = detail::BlasBuildFrameData{
            .scratchAlignment = runtime.limits.minScratchAlignment,
        };
        auto blasUses = std::vector<PassResourceUseDesc>{
            use::accelerationStructureScratchWrite(scratchResource),
        };
        auto importedBuildInputResources = std::set<GraphResourceHandle>{};
        auto scratchOffset = vk::DeviceSize{0};

        std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
            auto const &pending = pendingRef.get();
            auto const &cached = pending.cachedBuild.get();
            auto &entry = runtime.blasCache.at(pending.mesh);
            auto const geometryOffset = blasFrameData.geometries.size();
            blasFrameData.geometries.insert(blasFrameData.geometries.end(), cached.geometries.begin(), cached.geometries.end());

            scratchOffset = detail::alignUp(scratchOffset, runtime.limits.minScratchAlignment);
            blasFrameData.works.push_back(detail::BlasBuildWork{
                .dst = std::cref(entry.accelerationStructure),
                .scratchBuffer = std::cref(slot.scratchBuffer),
                .geometryOffset = geometryOffset,
                .geometryCount = cached.geometries.size(),
                .scratchAddress = slot.scratchBuffer.deviceAddress() + scratchOffset,
                .scratchSize = cached.sizes.buildScratchSize,
                .options = detail::staticBlasBuildOptions(),
            });
            scratchOffset += cached.sizes.buildScratchSize;

            auto const blasResource = importBlasResource(pending.mesh);
            dirtyBlasResourceByMesh.emplace(pending.mesh, blasResource);
            blasUses.push_back(use::accelerationStructureBuildWrite(blasResource));

            auto const vertexResource = detail::importBufferOnce(context, importedBuffers, cached.sceneMesh.vertexBuffer.buffer->get(), "ASBuild.SceneVertexAtlas", ResourceLifetime::ScenePersistent,
                                                                 {
                                                                     BufferUsageIntent::AccelerationStructureBuildInput,
                                                                     BufferUsageIntent::ShaderDeviceAddress,
                                                                 });
            importedBuildInputResources.insert(vertexResource);
            if (cached.sceneMesh.hasIndexBuffer())
            {
                auto const indexResource = detail::importBufferOnce(context, importedBuffers, cached.sceneMesh.indexBuffer.buffer->get(), "ASBuild.SceneIndexAtlas", ResourceLifetime::ScenePersistent,
                                                                    {
                                                                        BufferUsageIntent::AccelerationStructureBuildInput,
                                                                        BufferUsageIntent::ShaderDeviceAddress,
                                                                    });
                importedBuildInputResources.insert(indexResource);
            }
        });

        std::ranges::for_each(importedBuildInputResources, [&](GraphResourceHandle resource) { blasUses.push_back(use::accelerationStructureBuildInputRead(resource)); });

        auto const blasFrameDataHandle = context.importFrameData("ASBuild.BLAS.FrameData", std::move(blasFrameData));
        [[maybe_unused]] auto buildBlasPass = context.addPass(std::span<const PassResourceUseDesc>{blasUses}, "ASBuild.BuildBLAS", [blasFrameDataHandle](const nr::renderer::PassRecordContext &recordContext) {
            auto const &frameData = recordContext.frameData<detail::BlasBuildFrameData>(blasFrameDataHandle);
            auto batchRecords = detail::makeBatchRecords(frameData);
            nr::rhi::recordBuildBlasBatch(recordContext.commandBuffer->get(), std::span<const nr::rhi::BlasBatchBuildRecordInfo>{batchRecords}, frameData.scratchAlignment);
        });
    }

    auto tlasUses = std::vector<PassResourceUseDesc>{
        use::accelerationStructureBuildInputRead(instanceResource),
        use::orderedAfterPrevious(use::accelerationStructureScratchWrite(scratchResource)),
        use::accelerationStructureBuildWrite(tlasResource),
    };
    auto blasReadResources = std::map<GraphResourceHandle, bool>{};
    std::ranges::for_each(instances, [&](const detail::PlannedInstance &planned) {
        auto const resource = blasResourceByMesh.at(planned.mesh);
        blasReadResources.insert_or_assign(resource, dirtyBlasResourceByMesh.contains(planned.mesh));
    });
    std::ranges::for_each(blasReadResources, [&](const auto &pair) {
        auto read = use::accelerationStructureBuildRead(pair.first);
        tlasUses.push_back(pair.second ? use::orderedAfterPrevious(read) : read);
    });

    auto const tlasFrameDataHandle = context.importFrameData("ASBuild.TLAS.FrameData", detail::TlasBuildFrameData{
                                                                                           .dst = std::cref(slot.tlas),
                                                                                           .instanceBuffer = std::cref(slot.instanceBuffer),
                                                                                           .scratchBuffer = std::cref(slot.scratchBuffer),
                                                                                           .scratchAddress = slot.scratchBuffer.deviceAddress(),
                                                                                           .buildInput = tlasInput,
                                                                                           .options = detail::tlasBuildOptions(),
                                                                                           .scratchAlignment = runtime.limits.minScratchAlignment,
                                                                                       });
    [[maybe_unused]] auto buildTlasPass = context.addPass(std::span<const PassResourceUseDesc>{tlasUses}, "ASBuild.BuildTLAS", [tlasFrameDataHandle](const nr::renderer::PassRecordContext &recordContext) {
        auto const &frameData = recordContext.frameData<detail::TlasBuildFrameData>(tlasFrameDataHandle);
        nr::rhi::recordBuildTlas(recordContext.commandBuffer->get(),
                                 nr::rhi::TlasBuildRecordInfo{
                                     .dst = frameData.dst.get(),
                                     .instanceBuffer = frameData.instanceBuffer.get(),
                                     .scratchBuffer = frameData.scratchBuffer.get(),
                                     .scratchAddress = frameData.scratchAddress,
                                     .buildInput = frameData.buildInput,
                                     .options = frameData.options,
                                 },
                                 frameData.scratchAlignment);
    });
    auto const graphDeclareEnd = benchmarkTelemetry.has_value() ? std::chrono::steady_clock::now() : probeStart;
    if (benchmarkTelemetry.has_value())
    {
        auto &telemetry = benchmarkTelemetry->get().accelerationStructure.get();
        telemetry.available = true;
        telemetry.cacheScanMilliseconds = std::chrono::duration<double, std::milli>(cacheScanEnd - probeStart).count();
        telemetry.metadataPlanMilliseconds = std::chrono::duration<double, std::milli>(metadataPlanEnd - cacheScanEnd).count();
        telemetry.cpuWritesMilliseconds = std::chrono::duration<double, std::milli>(cpuWritesEnd - metadataPlanEnd).count();
        telemetry.tlasSizingMilliseconds = std::chrono::duration<double, std::milli>(tlasSizingEnd - cpuWritesEnd).count();
        telemetry.graphDeclareMilliseconds = std::chrono::duration<double, std::milli>(graphDeclareEnd - tlasSizingEnd).count();
        telemetry.instanceCount = instances.size();
        telemetry.dirtyBlasCount = dirtyBuilds.size();
    }
}
} // namespace nr::renderPasses
