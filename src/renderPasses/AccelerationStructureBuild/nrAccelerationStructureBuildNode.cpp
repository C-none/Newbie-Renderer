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

struct AsBuildCpuProfileFrame
{
    double totalMilliseconds = 0.0;
    double retireAndPruneMilliseconds = 0.0;
    double collectMeshesMilliseconds = 0.0;
    double dirtySelectMilliseconds = 0.0;
    double atlasPlanMilliseconds = 0.0;
    double atlasRepackMilliseconds = 0.0;
    double blasEnsureMilliseconds = 0.0;
    double instanceBuildMilliseconds = 0.0;
    double instanceUploadMilliseconds = 0.0;
    double blasSizeQueryMilliseconds = 0.0;
    double tlasSizeQueryMilliseconds = 0.0;
    double frameResourceEnsureMilliseconds = 0.0;
    double graphImportMilliseconds = 0.0;
    double blasPassEmitMilliseconds = 0.0;
    double tlasPassEmitMilliseconds = 0.0;

    std::uint64_t packetCount = 0;
    std::uint64_t uniqueMeshCount = 0;
    std::uint64_t dirtyBlasCount = 0;
    std::uint64_t repackBlasCount = 0;
    std::uint64_t instanceCount = 0;
    std::uint64_t blasCacheEntryCount = 0;
    std::uint64_t cachedBlasHitCount = 0;
    std::uint64_t cachedBlasRefreshCount = 0;
    std::uint64_t missingAsMeshCount = 0;
    std::uint64_t atlasGrowFrameCount = 0;
    std::uint64_t blasPassFrameCount = 0;
    std::uint64_t tlasPassFrameCount = 0;
};

struct AsBuildCpuProfileAccumulator
{
    AsBuildCpuProfileFrame total{};
    std::uint32_t frameCount = 0;
};

struct AccelerationStructureBuildRuntimeCache
{
    BlasStorageAtlas blasAtlas{};
    std::map<nr::resource::MeshHandle, BlasCacheEntry> blasCache{};
    std::vector<RetiredBlasAccelerationStructure> retiredBlas{};
    std::array<FrameSlotAsResources, nr::maxFrameInFlight> frameSlots{};
    nr::rhi::AsBuildLimits limits{};
    std::uint64_t frameSerial = 0;
    AsBuildCpuProfileAccumulator cpuProfile{};
};

[[nodiscard]] double elapsedProfileMilliseconds(std::chrono::steady_clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

void addElapsedProfileMilliseconds(double &target, std::chrono::steady_clock::time_point start)
{
    target += elapsedProfileMilliseconds(start);
}

void accumulateAsBuildProfile(AsBuildCpuProfileAccumulator &accumulator, const AsBuildCpuProfileFrame &sample) noexcept
{
    auto &total = accumulator.total;
    total.totalMilliseconds += sample.totalMilliseconds;
    total.retireAndPruneMilliseconds += sample.retireAndPruneMilliseconds;
    total.collectMeshesMilliseconds += sample.collectMeshesMilliseconds;
    total.dirtySelectMilliseconds += sample.dirtySelectMilliseconds;
    total.atlasPlanMilliseconds += sample.atlasPlanMilliseconds;
    total.atlasRepackMilliseconds += sample.atlasRepackMilliseconds;
    total.blasEnsureMilliseconds += sample.blasEnsureMilliseconds;
    total.instanceBuildMilliseconds += sample.instanceBuildMilliseconds;
    total.instanceUploadMilliseconds += sample.instanceUploadMilliseconds;
    total.blasSizeQueryMilliseconds += sample.blasSizeQueryMilliseconds;
    total.tlasSizeQueryMilliseconds += sample.tlasSizeQueryMilliseconds;
    total.frameResourceEnsureMilliseconds += sample.frameResourceEnsureMilliseconds;
    total.graphImportMilliseconds += sample.graphImportMilliseconds;
    total.blasPassEmitMilliseconds += sample.blasPassEmitMilliseconds;
    total.tlasPassEmitMilliseconds += sample.tlasPassEmitMilliseconds;

    total.packetCount += sample.packetCount;
    total.uniqueMeshCount += sample.uniqueMeshCount;
    total.dirtyBlasCount += sample.dirtyBlasCount;
    total.repackBlasCount += sample.repackBlasCount;
    total.instanceCount += sample.instanceCount;
    total.blasCacheEntryCount += sample.blasCacheEntryCount;
    total.cachedBlasHitCount += sample.cachedBlasHitCount;
    total.cachedBlasRefreshCount += sample.cachedBlasRefreshCount;
    total.missingAsMeshCount += sample.missingAsMeshCount;
    total.atlasGrowFrameCount += sample.atlasGrowFrameCount;
    total.blasPassFrameCount += sample.blasPassFrameCount;
    total.tlasPassFrameCount += sample.tlasPassFrameCount;
    ++accumulator.frameCount;
}

void reportAsBuildProfileIfReady(
    AccelerationStructureBuildRuntimeCache &runtime,
    const nr::renderPasses::AccelerationStructureBuildNodeInput &input,
    const AsBuildCpuProfileFrame &sample)
{
    if (input.cpuProfileLogIntervalFrames == 0u)
    {
        return;
    }

    accumulateAsBuildProfile(runtime.cpuProfile, sample);
    if (runtime.cpuProfile.frameCount < input.cpuProfileLogIntervalFrames)
    {
        return;
    }

    auto const frameCount = runtime.cpuProfile.frameCount;
    auto const divisor = static_cast<double>(frameCount);
    auto const &total = runtime.cpuProfile.total;
    auto avgMs = [divisor](double milliseconds) {
        return milliseconds / divisor;
    };
    auto avgCount = [divisor](std::uint64_t count) {
        return static_cast<double>(count) / divisor;
    };

    nrInfo(std::format(
        "[ASBuild CPU profile avg/{} frames] total={:.3f} ms, retirePrune={:.3f}, collectMeshes={:.3f}, "
        "dirtySelect={:.3f}, atlasPlan={:.3f}, atlasRepack={:.3f}, ensureBLAS={:.3f}, instances={:.3f}, "
        "uploadInstances={:.3f}, blasSizeQuery={:.3f}, tlasSizeQuery={:.3f}, ensureFrameResources={:.3f}, "
        "graphImport={:.3f}, emitBLASPass={:.3f}, emitTLASPass={:.3f}",
        frameCount,
        avgMs(total.totalMilliseconds),
        avgMs(total.retireAndPruneMilliseconds),
        avgMs(total.collectMeshesMilliseconds),
        avgMs(total.dirtySelectMilliseconds),
        avgMs(total.atlasPlanMilliseconds),
        avgMs(total.atlasRepackMilliseconds),
        avgMs(total.blasEnsureMilliseconds),
        avgMs(total.instanceBuildMilliseconds),
        avgMs(total.instanceUploadMilliseconds),
        avgMs(total.blasSizeQueryMilliseconds),
        avgMs(total.tlasSizeQueryMilliseconds),
        avgMs(total.frameResourceEnsureMilliseconds),
        avgMs(total.graphImportMilliseconds),
        avgMs(total.blasPassEmitMilliseconds),
        avgMs(total.tlasPassEmitMilliseconds)));
    nrInfo(std::format(
        "[ASBuild CPU profile counts avg/{} frames] packets={:.1f}, uniqueMeshes={:.1f}, dirtyBLAS={:.1f}, "
        "repackBLAS={:.1f}, instances={:.1f}, cacheEntries={:.1f}; totals: cacheHits={}, cacheRefreshes={}, "
        "missingMeshes={}, atlasGrowFrames={}, blasPassFrames={}, tlasPassFrames={}",
        frameCount,
        avgCount(total.packetCount),
        avgCount(total.uniqueMeshCount),
        avgCount(total.dirtyBlasCount),
        avgCount(total.repackBlasCount),
        avgCount(total.instanceCount),
        avgCount(total.blasCacheEntryCount),
        total.cachedBlasHitCount,
        total.cachedBlasRefreshCount,
        total.missingAsMeshCount,
        total.atlasGrowFrameCount,
        total.blasPassFrameCount,
        total.tlasPassFrameCount));

    runtime.cpuProfile = {};
}

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

[[nodiscard]] std::vector<nr::rhi::BlasGeometryRecord> makeBlasGeometryRecords(
    const nr::scene::SceneAccelerationStructureMesh &mesh)
{
    nrAssert(mesh.hasVertexBuffer(), "AS build mesh requires a vertex atlas buffer.");
    auto const &vertexBuffer = mesh.vertexBuffer.buffer->get();
    auto const vertexAddress = vertexBuffer.deviceAddress() + mesh.vertexByteOffset;
    auto const indexAddress = mesh.hasIndexBuffer()
                                  ? mesh.indexBuffer.buffer->get().deviceAddress() + mesh.indexByteOffset
                                  : vk::DeviceAddress{0};

    auto records = std::vector<nr::rhi::BlasGeometryRecord>{};
    records.reserve(mesh.geometries.size());

    std::ranges::for_each(mesh.geometries, [&](const nr::scene::SceneAccelerationStructureGeometry &geometry) {
        nrAssert(
            geometry.primitiveOffset <= std::numeric_limits<std::uint32_t>::max(),
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
        records.push_back(nr::rhi::makeBlasTriangleGeometryRecord(vertexBuffer, layout, input));
    });

    return records;
}

[[nodiscard]] BlasBuildSignature makeBlasBuildSignature(
    const nr::scene::SceneAccelerationStructureMesh &mesh)
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

void retireBlasResources(
    AccelerationStructureBuildRuntimeCache &runtime,
    BlasCacheEntry &entry,
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
}

void reapRetiredBlas(AccelerationStructureBuildRuntimeCache &runtime)
{
    std::erase_if(runtime.retiredBlas, [&](const RetiredBlasAccelerationStructure &retired) {
        return retired.retireAfterFrameSerial <= runtime.frameSerial;
    });
    std::erase_if(runtime.blasAtlas.retired, [&](const RetiredBlasAtlasResources &retired) {
        return retired.retireAfterFrameSerial <= runtime.frameSerial;
    });
}

void pruneBlasCache(
    AccelerationStructureBuildRuntimeCache &runtime,
    const nr::scene::Scene &scene,
    std::uint64_t unusedFrameRetireLatency,
    std::uint64_t retireDelay)
{
    auto it = runtime.blasCache.begin();
    while (it != runtime.blasCache.end())
    {
        auto const unusedTooLong =
            runtime.frameSerial > it->second.lastSeenFrameSerial &&
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

[[nodiscard]] vk::DeviceSize grownBlasAtlasCapacity(
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

[[nodiscard]] vk::DeviceSize alignedBlasSliceEnd(
    vk::DeviceSize cursor,
    vk::DeviceSize size) noexcept
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

[[nodiscard]] bool cachedBlasBuildNeedsRefresh(
    const CachedBlasBuildDesc &cached,
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
    return !context.entry.accelerationStructure.valid() ||
           !context.atlas.buffer.valid() ||
           context.entry.gpuVersion != context.cached.semanticKey.meshGpuVersion ||
           context.entry.atlasGeneration != context.atlas.generation ||
           context.entry.storageSize < context.cached.sizes.accelerationStructureSize ||
           context.entry.buildSignature != context.cached.signature;
}

[[nodiscard]] std::optional<std::reference_wrapper<const CachedBlasBuildDesc>> ensureCachedBlasBuild(
    const nr::scene::Scene &scene,
    nr::rhi::Device &device,
    nr::resource::MeshHandle meshHandle,
    BlasCacheEntry &entry,
    std::optional<std::reference_wrapper<AsBuildCpuProfileFrame>> profile = std::nullopt)
{
    auto semanticKey = scene.tryGetAccelerationStructureMeshSemanticKey(meshHandle);
    if (!semanticKey.has_value())
    {
        if (profile.has_value())
        {
            ++profile->get().missingAsMeshCount;
        }
        return std::nullopt;
    }

    if (!cachedBlasBuildNeedsRefresh(entry.cachedBuild, *semanticKey))
    {
        if (profile.has_value())
        {
            ++profile->get().cachedBlasHitCount;
        }
        return std::cref(entry.cachedBuild);
    }

    if (profile.has_value())
    {
        ++profile->get().cachedBlasRefreshCount;
    }

    auto sceneMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
    if (!sceneMesh.has_value())
    {
        if (profile.has_value())
        {
            ++profile->get().missingAsMeshCount;
        }
        return std::nullopt;
    }
    nrAssert(
        sceneMesh->semanticKey == *semanticKey,
        "Scene acceleration-structure mesh semantic key changed while refreshing the BLAS build cache.");

    auto mesh = std::move(*sceneMesh);
    auto geometries = makeBlasGeometryRecords(mesh);
    auto const signature = makeBlasBuildSignature(mesh);
    auto const queryStart = std::chrono::steady_clock::now();
    auto const sizes = nr::rhi::queryBlasBuildSizes(
        device.device,
        std::span<const nr::rhi::BlasGeometryRecord>{geometries},
        staticBlasBuildOptions());
    if (profile.has_value())
    {
        addElapsedProfileMilliseconds(profile->get().blasSizeQueryMilliseconds, queryStart);
    }

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

void retireCurrentBlasAtlas(
    AccelerationStructureBuildRuntimeCache &runtime,
    std::uint64_t retireDelay)
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
    });

    if (retired.storage.valid() || !retired.accelerationStructures.empty())
    {
        runtime.blasAtlas.retired.push_back(std::move(retired));
    }

    runtime.blasAtlas.usedBytes = 0;
}

void createBlasAtlas(
    AccelerationStructureBuildRuntimeCache &runtime,
    nr::rhi::Device &device,
    vk::DeviceSize requiredCapacity)
{
    auto const capacity = grownBlasAtlasCapacity(runtime.blasAtlas.capacityBytes, requiredCapacity);
    runtime.blasAtlas.buffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            std::max<vk::DeviceSize>(capacity, 1u),
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly,
        "ASBuild.BLAS.Atlas");
    nrAssert(runtime.blasAtlas.buffer.valid(), "AccelerationStructureBuildNode failed to create BLAS storage atlas.");
    runtime.blasAtlas.capacityBytes = capacity;
    runtime.blasAtlas.usedBytes = 0;
    ++runtime.blasAtlas.generation;
}

[[nodiscard]] vk::DeviceSize allocateBlasAtlasSlice(
    AccelerationStructureBuildRuntimeCache &runtime,
    vk::DeviceSize size)
{
    auto const offset = alignUp(runtime.blasAtlas.usedBytes, blasStorageOffsetAlignment);
    nrAssert(
        offset <= runtime.blasAtlas.capacityBytes && size <= runtime.blasAtlas.capacityBytes - offset,
        "AccelerationStructureBuildNode BLAS atlas allocation exceeds capacity.");
    runtime.blasAtlas.usedBytes = offset + size;
    return offset;
}

[[nodiscard]] BlasCacheEntry &ensureBlasEntry(
    AccelerationStructureBuildRuntimeCache &runtime,
    nr::rhi::Device &device,
    const PendingBlasBuild &pending,
    std::uint64_t retireDelay)
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
            device.device,
            runtime.blasAtlas.buffer,
            entry.storageOffset,
            cached.sizes.accelerationStructureSize,
            vk::AccelerationStructureTypeKHR::eBottomLevel,
            std::format("ASBuild.BLAS.mesh{}", pending.mesh.slot));
    }

    entry.gpuVersion = cached.semanticKey.meshGpuVersion;
    entry.atlasGeneration = runtime.blasAtlas.generation;
    entry.buildSignature = cached.signature;
    entry.buildScratchSize = cached.sizes.buildScratchSize;
    return entry;
}

void ensureScratchBuffer(
    nr::rhi::Device &device,
    FrameSlotAsResources &slot,
    vk::DeviceSize requiredSize)
{
    if (slot.scratchBuffer.valid() && slot.scratchBufferSize >= requiredSize)
    {
        return;
    }

    slot.scratchBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            std::max<vk::DeviceSize>(requiredSize, 1u),
            vk::BufferUsageFlagBits::eStorageBuffer |
                vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly,
        "ASBuild.Scratch");
    nrAssert(slot.scratchBuffer.valid(), "AccelerationStructureBuildNode failed to create scratch buffer.");
    slot.scratchBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

void ensureInstanceBuffer(
    nr::rhi::Device &device,
    FrameSlotAsResources &slot,
    vk::DeviceSize requiredSize)
{
    if (slot.instanceBuffer.valid() && slot.instanceBufferSize >= requiredSize)
    {
        return;
    }

    slot.instanceBuffer = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            std::max<vk::DeviceSize>(requiredSize, 1u),
            vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::CpuToGpu,
        "ASBuild.TLAS.Instances");
    nrAssert(slot.instanceBuffer.valid(), "AccelerationStructureBuildNode failed to create TLAS instance buffer.");
    slot.instanceBufferSize = std::max<vk::DeviceSize>(requiredSize, 1u);
}

void ensureTlas(
    nr::rhi::Device &device,
    FrameSlotAsResources &slot,
    const nr::rhi::AsBuildSizes &sizes,
    std::uint32_t instanceCount)
{
    auto const needsCreate =
        !slot.tlas.valid() ||
        !slot.tlasStorage.valid() ||
        slot.tlasStorage.size() < sizes.accelerationStructureSize ||
        slot.tlasCapacity < instanceCount;
    if (!needsCreate)
    {
        return;
    }

    slot.tlasStorage = device.resourceFactory.createBuffer(
        nr::rhi::makeBufferCreateInfo(
            std::max<vk::DeviceSize>(sizes.accelerationStructureSize, 1u),
            vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR |
                vk::BufferUsageFlagBits::eShaderDeviceAddress),
        nr::rhi::MemoryUsage::GpuOnly,
        "ASBuild.TLAS.Storage");
    nrAssert(slot.tlasStorage.valid(), "AccelerationStructureBuildNode failed to create TLAS storage.");
    slot.tlas = nr::rhi::AccelerationStructureResource::create(
        device.device,
        slot.tlasStorage,
        0,
        sizes.accelerationStructureSize,
        vk::AccelerationStructureTypeKHR::eTopLevel,
        "ASBuild.TLAS");
    slot.tlasCapacity = instanceCount;
}

[[nodiscard]] GraphResourceHandle importBufferOnce(
    NodeBuildContext &context,
    std::map<const nr::rhi::Buffer *, GraphResourceHandle> &importedBuffers,
    const nr::rhi::Buffer &buffer,
    std::string_view debugName,
    ResourceLifetime lifetime,
    std::initializer_list<BufferUsageIntent> usageIntents)
{
    auto const *key = std::addressof(buffer);
    if (auto it = importedBuffers.find(key); it != importedBuffers.end())
    {
        return it->second;
    }

    auto resource = context.importBuffer(
        buffer,
        debugName,
        lifetime,
        usageIntents,
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

[[nodiscard]] bool transformFlipsTriangleFacing(const glm::mat4 &world) noexcept
{
    auto const column0 = glm::vec3{world[0]};
    auto const column1 = glm::vec3{world[1]};
    auto const column2 = glm::vec3{world[2]};
    auto const determinant = glm::dot(column0, glm::cross(column1, column2));
    return std::isfinite(determinant) && determinant < 0.0f;
}

[[nodiscard]] vk::GeometryInstanceFlagsKHR tlasInstanceFlags(
    const nr::scene::TlasBuildInputPacket &packet,
    const nr::scene::SceneAccelerationStructureMesh &sceneMesh) noexcept
{
    auto flags = sceneMesh.instanceFlags;
    if (transformFlipsTriangleFacing(packet.world))
    {
        // glTF global transforms with a negative determinant reverse triangle winding.
        flags ^= vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing;
    }
    return flags;
}

[[nodiscard]] vk::AccelerationStructureInstanceKHR makeTlasInstance(
    const nr::scene::TlasBuildInputPacket &packet,
    const nr::scene::SceneAccelerationStructureMesh &sceneMesh,
    const nr::rhi::AccelerationStructureResource &blas,
    std::uint32_t hitShaderBindingTableRecordCount)
{
    nrAssert(hitShaderBindingTableRecordCount > 0u, "AS build requires at least one hit SBT record.");
    nrAssert(
        packet.tlasBucket < hitShaderBindingTableRecordCount,
        std::format(
            "TLAS packet bucket {} exceeds hit SBT record count {}.",
            packet.tlasBucket,
            hitShaderBindingTableRecordCount));

    auto instance = vk::AccelerationStructureInstanceKHR{};
    instance.setTransform(packTransform(packet.world));
    // InstanceCustomIndex is the shader-visible InstanceID(). Keep it independent from TLAS packet order.
    instance.setInstanceCustomIndex(stableTlasInstanceCustomIndex(packet));
    instance.setMask(packet.instanceMask & 0xFFu);
    instance.setInstanceShaderBindingTableRecordOffset(packet.tlasBucket);
    instance.setFlags(tlasInstanceFlags(packet, sceneMesh));
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
            .build = nr::rhi::BlasGeometriesBuildRecordInfo{
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

[[nodiscard]] NodeDescription AccelerationStructureBuildNode::describe() const
{
    return NodeDescription{
        .name = "AccelerationStructureBuild",
    };
}

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
    nrAssert(static_cast<bool>(runtime_), "AccelerationStructureBuild build stage requires initialized runtime state.");
    nrAssert(device_.has_value(), "AccelerationStructureBuild build stage requires device reference from initialize stage.");

    auto &runtime = *runtime_;
    auto &device = device_->get();
    auto profile = detail::AsBuildCpuProfileFrame{};
    auto const buildProfileStart = std::chrono::steady_clock::now();
    auto finishProfile = [&]() {
        profile.totalMilliseconds = detail::elapsedProfileMilliseconds(buildProfileStart);
        profile.blasCacheEntryCount = runtime.blasCache.size();
        detail::reportAsBuildProfileIfReady(runtime, input, profile);
    };

    auto sectionStart = std::chrono::steady_clock::now();
    ++runtime.frameSerial;
    detail::reapRetiredBlas(runtime);

    if (!frameParameters.scene.has_value() || !frameParameters.sceneTlasBuildInputs.has_value())
    {
        detail::addElapsedProfileMilliseconds(profile.retireAndPruneMilliseconds, sectionStart);
        finishProfile();
        return;
    }

    auto const &scene = frameParameters.scene->get();
    auto const &packets = frameParameters.sceneTlasBuildInputs->get();
    auto const retireDelay = static_cast<std::uint64_t>(nr::maxFrameInFlight + 1u);
    detail::pruneBlasCache(runtime, scene, input.unusedFrameRetireLatency, retireDelay);
    detail::addElapsedProfileMilliseconds(profile.retireAndPruneMilliseconds, sectionStart);
    profile.packetCount = packets.size();
    if (packets.empty())
    {
        finishProfile();
        return;
    }

    auto pendingByMesh = std::map<nr::resource::MeshHandle, detail::PendingBlasBuild>{};
    auto instances = std::vector<detail::PlannedInstance>{};
    instances.reserve(packets.size());

    sectionStart = std::chrono::steady_clock::now();
    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        if (pendingByMesh.contains(packet.mesh))
        {
            auto &entry = runtime.blasCache.try_emplace(packet.mesh).first->second;
            entry.lastSeenFrameSerial = runtime.frameSerial;
            return;
        }

        auto [entryIt, inserted] = runtime.blasCache.try_emplace(packet.mesh);
        auto cachedBuild = detail::ensureCachedBlasBuild(scene, device, packet.mesh, entryIt->second, std::ref(profile));
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
    detail::addElapsedProfileMilliseconds(profile.collectMeshesMilliseconds, sectionStart);
    profile.uniqueMeshCount = pendingByMesh.size();

    if (pendingByMesh.empty())
    {
        finishProfile();
        return;
    }

    auto pendingRefs = std::vector<std::reference_wrapper<const detail::PendingBlasBuild>>{};
    pendingRefs.reserve(pendingByMesh.size());
    std::ranges::for_each(pendingByMesh, [&](const auto &pair) {
        pendingRefs.push_back(std::cref(pair.second));
    });

    auto dirtyBuilds = std::vector<std::reference_wrapper<const detail::PendingBlasBuild>>{};
    dirtyBuilds.reserve(pendingRefs.size());
    sectionStart = std::chrono::steady_clock::now();
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
    detail::addElapsedProfileMilliseconds(profile.dirtySelectMilliseconds, sectionStart);
    profile.dirtyBlasCount = dirtyBuilds.size();

    sectionStart = std::chrono::steady_clock::now();
    auto atlasCursor = runtime.blasAtlas.usedBytes;
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        atlasCursor = detail::alignedBlasSliceEnd(
            atlasCursor,
            pendingRef.get().cachedBuild.get().sizes.accelerationStructureSize);
    });
    auto const needsAtlasGrow = !runtime.blasAtlas.buffer.valid() || atlasCursor > runtime.blasAtlas.capacityBytes;
    profile.atlasGrowFrameCount = needsAtlasGrow ? 1u : 0u;
    detail::addElapsedProfileMilliseconds(profile.atlasPlanMilliseconds, sectionStart);

    auto repackBuilds = std::map<nr::resource::MeshHandle, detail::PendingBlasBuild>{};
    if (needsAtlasGrow)
    {
        sectionStart = std::chrono::steady_clock::now();
        repackBuilds = pendingByMesh;
        std::ranges::for_each(runtime.blasCache, [&](auto &pair) {
            auto const meshHandle = pair.first;
            if (repackBuilds.contains(meshHandle))
            {
                return;
            }

            auto cachedBuild = detail::ensureCachedBlasBuild(scene, device, meshHandle, pair.second, std::ref(profile));
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
        std::ranges::for_each(repackBuilds, [&](const auto &pair) {
            repackRefs.push_back(std::cref(pair.second));
        });

        auto const requiredAtlasBytes = detail::requiredBlasAtlasBytes(std::span<const std::reference_wrapper<const detail::PendingBlasBuild>>{
            repackRefs.data(),
            repackRefs.size()});
        detail::retireCurrentBlasAtlas(runtime, retireDelay);
        detail::createBlasAtlas(runtime, device, requiredAtlasBytes);
        dirtyBuilds = std::move(repackRefs);
        profile.repackBlasCount = repackBuilds.size();
        profile.dirtyBlasCount = dirtyBuilds.size();
        detail::addElapsedProfileMilliseconds(profile.atlasRepackMilliseconds, sectionStart);
    }

    sectionStart = std::chrono::steady_clock::now();
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        auto &entry = detail::ensureBlasEntry(runtime, device, pendingRef.get(), retireDelay);
        entry.lastSeenFrameSerial = runtime.frameSerial;
    });
    detail::addElapsedProfileMilliseconds(profile.blasEnsureMilliseconds, sectionStart);

    sectionStart = std::chrono::steady_clock::now();
    std::ranges::for_each(packets, [&](const nr::scene::TlasBuildInputPacket &packet) {
        auto entryIt = runtime.blasCache.find(packet.mesh);
        if (entryIt == runtime.blasCache.end() || !entryIt->second.accelerationStructure.valid())
        {
            return;
        }
        auto pendingIt = pendingByMesh.find(packet.mesh);
        if (pendingIt == pendingByMesh.end())
        {
            return;
        }

        instances.push_back(detail::PlannedInstance{
            .mesh = packet.mesh,
            .instance = detail::makeTlasInstance(
                packet,
                pendingIt->second.cachedBuild.get().sceneMesh,
                entryIt->second.accelerationStructure,
                input.hitShaderBindingTableRecordCount),
        });
    });
    detail::addElapsedProfileMilliseconds(profile.instanceBuildMilliseconds, sectionStart);
    profile.instanceCount = instances.size();

    if (instances.empty())
    {
        finishProfile();
        return;
    }

    auto &slot = runtime.frameSlots[context.frameIndex % runtime.frameSlots.size()];
    auto const instanceBytes = static_cast<vk::DeviceSize>(instances.size() * sizeof(vk::AccelerationStructureInstanceKHR));
    sectionStart = std::chrono::steady_clock::now();
    detail::ensureInstanceBuffer(device, slot, instanceBytes);
    detail::addElapsedProfileMilliseconds(profile.frameResourceEnsureMilliseconds, sectionStart);

    sectionStart = std::chrono::steady_clock::now();
    auto instanceValues = instances |
                          std::views::transform([](const detail::PlannedInstance &planned) { return planned.instance; }) |
                          std::ranges::to<std::vector>();
    slot.instanceBuffer.writeMappedAndFlush(std::span<const vk::AccelerationStructureInstanceKHR>{instanceValues});
    detail::addElapsedProfileMilliseconds(profile.instanceUploadMilliseconds, sectionStart);

    auto const tlasInput = nr::rhi::TlasBuildInput{
        .instancesAddress = slot.instanceBuffer.deviceAddress(),
        .instanceCount = static_cast<std::uint32_t>(instances.size()),
    };
    sectionStart = std::chrono::steady_clock::now();
    auto const tlasSizes = nr::rhi::queryTlasBuildSizes(device.device, tlasInput, detail::tlasBuildOptions());
    detail::addElapsedProfileMilliseconds(profile.tlasSizeQueryMilliseconds, sectionStart);

    sectionStart = std::chrono::steady_clock::now();
    detail::ensureTlas(device, slot, tlasSizes, tlasInput.instanceCount);

    auto blasScratchBytes = vk::DeviceSize{0};
    std::ranges::for_each(dirtyBuilds, [&](std::reference_wrapper<const detail::PendingBlasBuild> pendingRef) {
        blasScratchBytes = detail::alignUp(blasScratchBytes, runtime.limits.minScratchAlignment);
        blasScratchBytes += pendingRef.get().cachedBuild.get().sizes.buildScratchSize;
    });
    auto const requiredScratchBytes = std::max(blasScratchBytes, tlasSizes.buildScratchSize);
    detail::ensureScratchBuffer(device, slot, requiredScratchBytes);
    detail::addElapsedProfileMilliseconds(profile.frameResourceEnsureMilliseconds, sectionStart);

    auto importedBuffers = std::map<const nr::rhi::Buffer *, GraphResourceHandle>{};
    auto blasResourceByMesh = std::map<nr::resource::MeshHandle, GraphResourceHandle>{};

    sectionStart = std::chrono::steady_clock::now();
    auto const scratchResource = context.importBuffer(
        slot.scratchBuffer,
        "ASBuild.Scratch",
        ResourceLifetime::FrameLocal,
        {
            BufferUsageIntent::AccelerationStructureScratch,
            BufferUsageIntent::ShaderDeviceAddress,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const instanceResource = context.importBuffer(
        slot.instanceBuffer,
        "ASBuild.TLAS.Instances",
        ResourceLifetime::FrameLocal,
        {
            BufferUsageIntent::AccelerationStructureBuildInput,
            BufferUsageIntent::ShaderDeviceAddress,
        },
        nr::renderer::ownershipDomainFromQueue(context.queue));
    auto const tlasResource = context.importAccelerationStructure(
        slot.tlas,
        "ASBuild.TLAS",
        ResourceLifetime::FrameLocal,
        nr::renderer::ownershipDomainFromQueue(context.queue));
    context.publishFrameResource(nr::renderer::frameResource::sceneTlas, tlasResource);
    detail::addElapsedProfileMilliseconds(profile.graphImportMilliseconds, sectionStart);

    auto importBlasResource = [&](nr::resource::MeshHandle meshHandle) {
        if (auto it = blasResourceByMesh.find(meshHandle); it != blasResourceByMesh.end())
        {
            return it->second;
        }

        auto cacheIt = runtime.blasCache.find(meshHandle);
        nrAssert(cacheIt != runtime.blasCache.end(), "AS build node expected a BLAS cache entry.");
        auto resource = context.importAccelerationStructure(
            cacheIt->second.accelerationStructure,
            std::format("ASBuild.BLAS.mesh{}", meshHandle.slot),
            ResourceLifetime::RendererPersistent,
            nr::renderer::ownershipDomainFromQueue(context.queue));
        blasResourceByMesh.emplace(meshHandle, resource);
        return resource;
    };

    if (!dirtyBuilds.empty())
    {
        sectionStart = std::chrono::steady_clock::now();
        profile.blasPassFrameCount = 1u;
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
            blasFrameData.geometries.insert(
                blasFrameData.geometries.end(),
                cached.geometries.begin(),
                cached.geometries.end());

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

            blasUses.push_back(use::accelerationStructureBuildWrite(importBlasResource(pending.mesh)));

            auto const vertexResource = detail::importBufferOnce(
                context,
                importedBuffers,
                cached.sceneMesh.vertexBuffer.buffer->get(),
                "ASBuild.SceneVertexAtlas",
                ResourceLifetime::ScenePersistent,
                {
                    BufferUsageIntent::AccelerationStructureBuildInput,
                    BufferUsageIntent::ShaderDeviceAddress,
                });
            importedBuildInputResources.insert(vertexResource);
            if (cached.sceneMesh.hasIndexBuffer())
            {
                auto const indexResource = detail::importBufferOnce(
                    context,
                    importedBuffers,
                    cached.sceneMesh.indexBuffer.buffer->get(),
                    "ASBuild.SceneIndexAtlas",
                    ResourceLifetime::ScenePersistent,
                    {
                        BufferUsageIntent::AccelerationStructureBuildInput,
                        BufferUsageIntent::ShaderDeviceAddress,
                    });
                importedBuildInputResources.insert(indexResource);
            }
        });

        std::ranges::for_each(importedBuildInputResources, [&](GraphResourceHandle resource) {
            blasUses.push_back(use::accelerationStructureBuildInputRead(resource));
        });

        auto const blasFrameDataHandle = context.importFrameData("ASBuild.BLAS.FrameData", std::move(blasFrameData));
        [[maybe_unused]] auto buildBlasPass = context.addPass(
            std::span<const PassResourceUseDesc>{blasUses},
            "ASBuild.BuildBLAS",
            [blasFrameDataHandle](const nr::renderer::PassRecordContext &recordContext) {
                auto const &frameData = recordContext.frameData<detail::BlasBuildFrameData>(blasFrameDataHandle);
                auto batchRecords = detail::makeBatchRecords(frameData);
                nr::rhi::recordBuildBlasBatch(
                    recordContext.commandBuffer->get(),
                    std::span<const nr::rhi::BlasBatchBuildRecordInfo>{batchRecords},
                    frameData.scratchAlignment);
            });
        detail::addElapsedProfileMilliseconds(profile.blasPassEmitMilliseconds, sectionStart);
    }

    sectionStart = std::chrono::steady_clock::now();
    profile.tlasPassFrameCount = 1u;
    auto tlasUses = std::vector<PassResourceUseDesc>{
        use::accelerationStructureBuildInputRead(instanceResource),
        use::orderedAfterPrevious(use::accelerationStructureScratchWrite(scratchResource)),
        use::accelerationStructureBuildWrite(tlasResource),
    };
    auto blasReadResources = std::set<GraphResourceHandle>{};
    std::ranges::for_each(instances, [&](const detail::PlannedInstance &planned) {
        blasReadResources.insert(importBlasResource(planned.mesh));
    });
    std::ranges::for_each(blasReadResources, [&](GraphResourceHandle resource) {
        tlasUses.push_back(use::orderedAfterPrevious(use::accelerationStructureBuildRead(resource)));
    });

    auto const tlasFrameDataHandle = context.importFrameData(
        "ASBuild.TLAS.FrameData",
        detail::TlasBuildFrameData{
            .dst = std::cref(slot.tlas),
            .instanceBuffer = std::cref(slot.instanceBuffer),
            .scratchBuffer = std::cref(slot.scratchBuffer),
            .scratchAddress = slot.scratchBuffer.deviceAddress(),
            .buildInput = tlasInput,
            .options = detail::tlasBuildOptions(),
            .scratchAlignment = runtime.limits.minScratchAlignment,
        });
    [[maybe_unused]] auto buildTlasPass = context.addPass(
        std::span<const PassResourceUseDesc>{tlasUses},
        "ASBuild.BuildTLAS",
        [tlasFrameDataHandle](const nr::renderer::PassRecordContext &recordContext) {
            auto const &frameData = recordContext.frameData<detail::TlasBuildFrameData>(tlasFrameDataHandle);
            nr::rhi::recordBuildTlas(
                recordContext.commandBuffer->get(),
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
    detail::addElapsedProfileMilliseconds(profile.tlasPassEmitMilliseconds, sectionStart);
    finishProfile();
}
} // namespace nr::renderPasses
