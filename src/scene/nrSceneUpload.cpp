module nr.scene;
import :scene;
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
namespace
{
[[nodiscard]] vk::DeviceSize sceneGeometryAtlasRangeEnd(const detail::SceneGeometryAtlasReusableRange &range) noexcept
{
    nrAssert(range.byteSize <= std::numeric_limits<vk::DeviceSize>::max() - range.byteOffset,
             "Scene geometry atlas reusable range overflowed.");
    return range.byteOffset + range.byteSize;
}

[[nodiscard]] vk::DeviceSize alignSceneGeometryAtlasOffset(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    nrAssert(alignment > 0u, "Scene geometry atlas allocation requires nonzero alignment.");
    auto const remainder = value % alignment;
    if (remainder == 0u)
    {
        return value;
    }

    auto const padding = alignment - remainder;
    nrAssert(padding <= std::numeric_limits<vk::DeviceSize>::max() - value,
             "Scene geometry atlas aligned offset overflowed.");
    return value + padding;
}

void validateSceneGeometryAtlasReusableRanges(std::span<const detail::SceneGeometryAtlasReusableRange> reusableRanges,
                                              vk::DeviceSize highWaterBytes) noexcept
{
    auto const invalidRange = std::ranges::find_if(reusableRanges, [&](const auto &range) {
        return range.byteSize == 0u || sceneGeometryAtlasRangeEnd(range) > highWaterBytes;
    });
    nrAssert(invalidRange == reusableRanges.end(),
             "Scene geometry atlas reusable ranges must be non-empty and stay below the high-water mark.");

    auto const uncoalescedPair = std::ranges::adjacent_find(reusableRanges, [](const auto &left, const auto &right) {
        return sceneGeometryAtlasRangeEnd(left) >= right.byteOffset;
    });
    nrAssert(uncoalescedPair == reusableRanges.end(),
             "Scene geometry atlas reusable ranges must be sorted, disjoint, and coalesced.");
}
} // namespace

void Scene::uploadPending()
{
    if (!uploadContextAvailable())
    {
        return;
    }

    auto &uploadContext = device_.uploadReadback();
    reapSubmittedGeometryAtlasGrowWork();
    reapSubmittedGraphicsSyncWork();

    if (uploadBudgetState_.frameSerial != currentFrame_.frameSerial)
    {
        uploadBudgetState_ = UploadBudgetState{
            .remaining = uploadBudgetBytesPerFrame_,
            .frameSerial = currentFrame_.frameSerial,
        };
    }

    uploadQueuedMeshAssets(uploadContext, uploadBudgetState_);
    uploadQueuedTextureAssets(uploadContext, uploadBudgetState_);
    submitPendingGraphicsSyncBatches(uploadContext);
}

[[nodiscard]] bool Scene::uploadContextAvailable() const noexcept
{
    return *device_.device != nullptr && device_.uploadReadbackContext_.has_value();
}

[[nodiscard]] bool Scene::geometryAtlasReadyForUse() const noexcept
{
    return submittedGeometryAtlasGrowWork_.empty();
}

[[nodiscard]] bool Scene::uploadBudgetAllows(const UploadBudgetState &budget, std::size_t bytesNeeded) noexcept
{
    return bytesNeeded > 0u && (bytesNeeded <= budget.remaining || !budget.consumed);
}

void Scene::recordBudgetedUpload(UploadBudgetState &budget, std::size_t bytesUploaded) noexcept
{
    nrAssert(uploadBudgetAllows(budget, bytesUploaded),
             "Scene budget accounting requires a previously permitted non-empty upload.");
    nrAssert(bytesUploaded <= std::numeric_limits<std::size_t>::max() - uploadBytesThisFrame_,
             "Scene per-frame upload byte accounting overflowed.");

    budget.remaining = bytesUploaded <= budget.remaining ? budget.remaining - bytesUploaded : 0u;
    budget.consumed = true;
    uploadBytesThisFrame_ += bytesUploaded;
}

[[nodiscard]] nr::rhi::ops::BufferUploadOwnershipPlan Scene::makeTransferToGraphicsUploadPlan(
    vk::PipelineStageFlags2 acquireStages, vk::AccessFlags2 acquireAccess) const
{
    auto const queueFamilies = device_.queueManager.familyIndices();
    nrAssert(queueFamilies.transfer != queueFamilies.graphics,
             "Scene upload requires distinct transfer and graphics queue families.");
    return nr::rhi::ops::makeTransferUploadOwnershipPlan(queueFamilies.transfer, queueFamilies.graphics,
                                                         nr::rhi::ops::QueueAccessScope{
                                                             .stages = acquireStages,
                                                             .access = acquireAccess,
                                                         });
}

[[nodiscard]] std::size_t Scene::meshUploadBytes(const nr::resource::Mesh &mesh) noexcept
{
    auto vertexBytes = std::span{mesh.vertices}.size_bytes();
    auto indexBytes = std::span{mesh.indices}.size_bytes();
    return vertexBytes + indexBytes;
}

[[nodiscard]] std::optional<std::reference_wrapper<const nr::resource::ImageLevel>> Scene::firstTextureLevelWithPixels(
    const nr::resource::Texture &texture)
{
    auto withPixels = std::ranges::find_if(texture.levels,
                                           [](const nr::resource::ImageLevel &level) { return !level.bytes.empty(); });

    if (withPixels == texture.levels.end())
    {
        return std::nullopt;
    }

    return std::cref(*withPixels);
}

[[nodiscard]] std::size_t Scene::textureUploadBytes(const nr::resource::Texture &texture) noexcept
{
    auto const sourceLevel = firstTextureLevelWithPixels(texture);
    return sourceLevel.has_value() ? sourceLevel->get().bytes.size() : 0u;
}

[[nodiscard]] std::uint32_t Scene::checkedDeviceSizeToUint32(vk::DeviceSize value, std::string_view label)
{
    nrAssert(value <= std::numeric_limits<std::uint32_t>::max(), "{} value {} exceeds uint32_t range.", label,
             value);
    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::vector<std::uint32_t> Scene::makeGeometryAtlasQueueFamilyIndices() const
{
    auto const queueFamilies = device_.queueManager.familyIndices();
    auto families = std::vector<std::uint32_t>{
        queueFamilies.transfer,
        queueFamilies.graphics,
        queueFamilies.compute,
    };
    std::ranges::sort(families);
    auto uniqueTail = std::ranges::unique(families);
    families.erase(uniqueTail.begin(), uniqueTail.end());
    return families;
}

[[nodiscard]] vk::BufferCreateInfo Scene::makeGeometryAtlasBufferCreateInfo(
    vk::DeviceSize size, vk::BufferUsageFlags bindingUsage, std::span<const std::uint32_t> queueFamilyIndices) noexcept
{
    auto createInfo = vk::BufferCreateInfo{};
    createInfo.size = size;
    createInfo.usage = vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eTransferSrc |
                       vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress |
                       vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR | bindingUsage;
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

[[nodiscard]] vk::DeviceSize Scene::grownGeometryAtlasCapacity(vk::DeviceSize currentCapacity,
                                                               vk::DeviceSize requiredCapacity) noexcept
{
    constexpr auto minCapacity = vk::DeviceSize{1024u * 1024u};
    auto const maxCapacity = std::numeric_limits<vk::DeviceSize>::max();
    auto const doubledCapacity = currentCapacity > (maxCapacity / 2u) ? maxCapacity : currentCapacity * 2u;
    return std::max({requiredCapacity, doubledCapacity, minCapacity});
}

[[nodiscard]] vk::DeviceSize Scene::geometryAtlasReusableBytes(
    std::span<const detail::SceneGeometryAtlasReusableRange> reusableRanges, vk::DeviceSize highWaterBytes) noexcept
{
    validateSceneGeometryAtlasReusableRanges(reusableRanges, highWaterBytes);
    return std::ranges::fold_left(reusableRanges, vk::DeviceSize{0}, [](vk::DeviceSize total, const auto &range) {
        nrAssert(range.byteSize <= std::numeric_limits<vk::DeviceSize>::max() - total,
                 "Scene geometry atlas reusable byte accounting overflowed.");
        return total + range.byteSize;
    });
}

void Scene::releaseGeometryAtlasRange(std::vector<detail::SceneGeometryAtlasReusableRange> &reusableRanges,
                                      detail::SceneGeometryAtlasReusableRange releasedRange)
{
    nrAssert(releasedRange.byteSize > 0u, "Scene geometry atlas cannot release an empty range.");
    validateSceneGeometryAtlasReusableRanges(reusableRanges, std::numeric_limits<vk::DeviceSize>::max());

    auto next = std::ranges::lower_bound(reusableRanges, releasedRange.byteOffset, {},
                                         &detail::SceneGeometryAtlasReusableRange::byteOffset);
    if (next != reusableRanges.begin())
    {
        auto previous = std::prev(next);
        auto const previousEnd = sceneGeometryAtlasRangeEnd(*previous);
        nrAssert(previousEnd <= releasedRange.byteOffset,
                 "Scene geometry atlas range release overlaps an existing reusable range.");
        if (previousEnd == releasedRange.byteOffset)
        {
            releasedRange.byteOffset = previous->byteOffset;
            nrAssert(releasedRange.byteSize <= std::numeric_limits<vk::DeviceSize>::max() - previous->byteSize,
                     "Scene geometry atlas reusable range merge overflowed.");
            releasedRange.byteSize += previous->byteSize;
            next = reusableRanges.erase(previous);
        }
    }

    auto const releasedEnd = sceneGeometryAtlasRangeEnd(releasedRange);
    if (next != reusableRanges.end())
    {
        nrAssert(releasedEnd <= next->byteOffset,
                 "Scene geometry atlas range release overlaps an existing reusable range.");
        if (releasedEnd == next->byteOffset)
        {
            nrAssert(next->byteSize <= std::numeric_limits<vk::DeviceSize>::max() - releasedRange.byteSize,
                     "Scene geometry atlas reusable range merge overflowed.");
            releasedRange.byteSize += next->byteSize;
            next = reusableRanges.erase(next);
        }
    }

    reusableRanges.insert(next, releasedRange);
    validateSceneGeometryAtlasReusableRanges(reusableRanges, std::numeric_limits<vk::DeviceSize>::max());
}

[[nodiscard]] Scene::GeometryAtlasRangeAllocationPlan Scene::planGeometryAtlasRangeAllocation(
    std::span<const detail::SceneGeometryAtlasReusableRange> reusableRanges, vk::DeviceSize highWaterBytes,
    vk::DeviceSize byteSize, vk::DeviceSize alignment)
{
    validateSceneGeometryAtlasReusableRanges(reusableRanges, highWaterBytes);
    auto plan = GeometryAtlasRangeAllocationPlan{
        .highWaterBytes = highWaterBytes,
        .reusableRanges = {reusableRanges.begin(), reusableRanges.end()},
    };
    if (byteSize == 0u)
    {
        return plan;
    }

    auto selectedRange = std::ranges::find_if(plan.reusableRanges, [&](const auto &range) {
        auto const alignedOffset = alignSceneGeometryAtlasOffset(range.byteOffset, alignment);
        auto const rangeEnd = sceneGeometryAtlasRangeEnd(range);
        return alignedOffset <= rangeEnd && byteSize <= rangeEnd - alignedOffset;
    });
    if (selectedRange != plan.reusableRanges.end())
    {
        auto const selected = *selectedRange;
        plan.byteOffset = alignSceneGeometryAtlasOffset(selected.byteOffset, alignment);
        nrAssert(byteSize <= std::numeric_limits<vk::DeviceSize>::max() - plan.byteOffset,
                 "Scene geometry atlas reusable allocation overflowed.");
        auto const allocationEnd = plan.byteOffset + byteSize;
        auto const selectedEnd = sceneGeometryAtlasRangeEnd(selected);
        auto const prefixBytes = plan.byteOffset - selected.byteOffset;
        auto const suffixBytes = selectedEnd - allocationEnd;

        auto insertion = plan.reusableRanges.erase(selectedRange);
        if (suffixBytes > 0u)
        {
            insertion = plan.reusableRanges.insert(insertion, detail::SceneGeometryAtlasReusableRange{
                                                                  .byteOffset = allocationEnd,
                                                                  .byteSize = suffixBytes,
                                                              });
        }
        if (prefixBytes > 0u)
        {
            plan.reusableRanges.insert(insertion, detail::SceneGeometryAtlasReusableRange{
                                                      .byteOffset = selected.byteOffset,
                                                      .byteSize = prefixBytes,
                                                  });
        }

        validateSceneGeometryAtlasReusableRanges(plan.reusableRanges, plan.highWaterBytes);
        return plan;
    }

    plan.byteOffset = alignSceneGeometryAtlasOffset(highWaterBytes, alignment);
    if (plan.byteOffset > highWaterBytes)
    {
        releaseGeometryAtlasRange(plan.reusableRanges, detail::SceneGeometryAtlasReusableRange{
                                                           .byteOffset = highWaterBytes,
                                                           .byteSize = plan.byteOffset - highWaterBytes,
                                                       });
    }
    nrAssert(byteSize <= std::numeric_limits<vk::DeviceSize>::max() - plan.byteOffset,
             "Scene geometry atlas high-water allocation overflowed.");
    plan.highWaterBytes = plan.byteOffset + byteSize;
    validateSceneGeometryAtlasReusableRanges(plan.reusableRanges, plan.highWaterBytes);
    return plan;
}

void Scene::releaseGeometryAtlasAllocation(const detail::MeshGeometryAtlasAllocation &allocation)
{
    constexpr auto vertexStride = vk::DeviceSize{sizeof(nr::resource::Vertex)};
    constexpr auto indexStride = vk::DeviceSize{sizeof(std::uint32_t)};

    nrAssert(allocation.vertexCount > 0u, "Scene geometry atlas cannot release an empty vertex allocation.");
    auto const vertexBytes = static_cast<vk::DeviceSize>(allocation.vertexCount) * vertexStride;
    nrAssert(allocation.vertexByteOffset % vertexStride == 0u &&
                 allocation.vertexByteOffset / vertexStride == allocation.vertexBase,
             "Scene geometry atlas vertex allocation metadata is inconsistent.");
    nrAssert(allocation.vertexByteOffset <= geometryAtlas_.vertexHighWaterBytes &&
                 vertexBytes <= geometryAtlas_.vertexHighWaterBytes - allocation.vertexByteOffset,
             "Scene geometry atlas vertex release exceeds the high-water mark.");
    releaseGeometryAtlasRange(geometryAtlas_.vertexReusableRanges, detail::SceneGeometryAtlasReusableRange{
                                                                       .byteOffset = allocation.vertexByteOffset,
                                                                       .byteSize = vertexBytes,
                                                                   });

    if (allocation.indexCount == 0u)
    {
        return;
    }

    auto const indexBytes = static_cast<vk::DeviceSize>(allocation.indexCount) * indexStride;
    nrAssert(allocation.indexByteOffset % indexStride == 0u &&
                 allocation.indexByteOffset / indexStride == allocation.indexBase,
             "Scene geometry atlas index allocation metadata is inconsistent.");
    nrAssert(allocation.indexByteOffset <= geometryAtlas_.indexHighWaterBytes &&
                 indexBytes <= geometryAtlas_.indexHighWaterBytes - allocation.indexByteOffset,
             "Scene geometry atlas index release exceeds the high-water mark.");
    releaseGeometryAtlasRange(geometryAtlas_.indexReusableRanges, detail::SceneGeometryAtlasReusableRange{
                                                                      .byteOffset = allocation.indexByteOffset,
                                                                      .byteSize = indexBytes,
                                                                  });
}

[[nodiscard]] detail::MeshGeometryAtlasAllocation Scene::reserveGeometryAtlasAllocation(
    const nr::resource::Mesh &mesh, nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto vertexBytes = std::as_bytes(std::span{mesh.vertices});
    auto indexBytes = std::as_bytes(std::span{mesh.indices});
    nrAssert(!vertexBytes.empty(), "Scene geometry atlas allocation requires vertex data.");

    constexpr auto vertexStride = vk::DeviceSize{sizeof(nr::resource::Vertex)};
    constexpr auto indexStride = vk::DeviceSize{sizeof(std::uint32_t)};

    auto vertexPlan =
        planGeometryAtlasRangeAllocation(geometryAtlas_.vertexReusableRanges, geometryAtlas_.vertexHighWaterBytes,
                                         static_cast<vk::DeviceSize>(vertexBytes.size_bytes()), vertexStride);
    auto indexPlan =
        planGeometryAtlasRangeAllocation(geometryAtlas_.indexReusableRanges, geometryAtlas_.indexHighWaterBytes,
                                         static_cast<vk::DeviceSize>(indexBytes.size_bytes()), indexStride);
    ensureGeometryAtlasCapacity(vertexPlan.highWaterBytes, indexPlan.highWaterBytes, uploadContext);

    geometryAtlas_.vertexHighWaterBytes = vertexPlan.highWaterBytes;
    geometryAtlas_.indexHighWaterBytes = indexPlan.highWaterBytes;
    geometryAtlas_.vertexReusableRanges = std::move(vertexPlan.reusableRanges);
    geometryAtlas_.indexReusableRanges = std::move(indexPlan.reusableRanges);

    auto allocation = detail::MeshGeometryAtlasAllocation{
        .vertexByteOffset = vertexPlan.byteOffset,
        .indexByteOffset = indexPlan.byteOffset,
        .vertexBase =
            checkedDeviceSizeToUint32(vertexPlan.byteOffset / vertexStride, "Scene geometry atlas vertex base"),
        .indexBase = checkedDeviceSizeToUint32(indexPlan.byteOffset / indexStride, "Scene geometry atlas index base"),
        .vertexCount =
            checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.vertices.size()), "Scene mesh vertex count"),
        .indexCount =
            checkedDeviceSizeToUint32(static_cast<vk::DeviceSize>(mesh.indices.size()), "Scene mesh index count"),
        .backingGenerationAtAllocation = geometryAtlas_.backingGeneration,
    };
    return allocation;
}

void Scene::ensureGeometryAtlasCapacity(vk::DeviceSize requiredVertexBytes, vk::DeviceSize requiredIndexBytes,
                                        nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    auto const growVertex = requiredVertexBytes > geometryAtlas_.vertexCapacityBytes;
    auto const growIndex = requiredIndexBytes > geometryAtlas_.indexCapacityBytes;
    if (!growVertex && !growIndex)
    {
        return;
    }

    auto const copyExistingVertexPrefix =
        growVertex && geometryAtlas_.vertexBuffer.valid() && geometryAtlas_.vertexHighWaterBytes > 0u;
    auto const copyExistingIndexPrefix =
        growIndex && geometryAtlas_.indexBuffer.valid() && geometryAtlas_.indexHighWaterBytes > 0u;
    if ((copyExistingVertexPrefix || copyExistingIndexPrefix) && !pendingGraphicsSyncBatches_.empty())
    {
        // Consume ticket references before moving the atlas Buffer wrappers. The
        // graphics queue waits on the upload timeline, so this remains CPU-nonblocking.
        submitPendingGraphicsSyncBatches(uploadContext);
    }

    auto oldVertexBuffer = std::optional<nr::rhi::Buffer>{};
    auto oldIndexBuffer = std::optional<nr::rhi::Buffer>{};
    auto oldVertexHighWaterBytes = vk::DeviceSize{0};
    auto oldIndexHighWaterBytes = vk::DeviceSize{0};

    if (growVertex)
    {
        oldVertexHighWaterBytes = geometryAtlas_.vertexHighWaterBytes;
        if (geometryAtlas_.vertexBuffer.valid())
        {
            oldVertexBuffer.emplace(std::move(geometryAtlas_.vertexBuffer));
        }

        geometryAtlas_.vertexCapacityBytes =
            grownGeometryAtlasCapacity(geometryAtlas_.vertexCapacityBytes, requiredVertexBytes);
        auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
        geometryAtlas_.vertexBuffer = device_.resourceFactory.createBuffer(
            makeGeometryAtlasBufferCreateInfo(geometryAtlas_.vertexCapacityBytes,
                                              vk::BufferUsageFlagBits::eVertexBuffer,
                                              std::span<const std::uint32_t>{atlasQueueFamilies}),
            nr::rhi::MemoryUsage::GpuOnly, "scene_geometry_vertex_atlas");
        nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene failed to create vertex geometry atlas buffer.");
        nrAssert(geometryAtlas_.backingGeneration.vertex != std::numeric_limits<std::uint64_t>::max(),
                 "Scene vertex geometry atlas backing generation overflowed.");
        ++geometryAtlas_.backingGeneration.vertex;
    }

    if (growIndex && requiredIndexBytes > 0u)
    {
        oldIndexHighWaterBytes = geometryAtlas_.indexHighWaterBytes;
        if (geometryAtlas_.indexBuffer.valid())
        {
            oldIndexBuffer.emplace(std::move(geometryAtlas_.indexBuffer));
        }

        geometryAtlas_.indexCapacityBytes =
            grownGeometryAtlasCapacity(geometryAtlas_.indexCapacityBytes, requiredIndexBytes);
        auto atlasQueueFamilies = makeGeometryAtlasQueueFamilyIndices();
        geometryAtlas_.indexBuffer = device_.resourceFactory.createBuffer(
            makeGeometryAtlasBufferCreateInfo(geometryAtlas_.indexCapacityBytes, vk::BufferUsageFlagBits::eIndexBuffer,
                                              std::span<const std::uint32_t>{atlasQueueFamilies}),
            nr::rhi::MemoryUsage::GpuOnly, "scene_geometry_index_atlas");
        nrAssert(geometryAtlas_.indexBuffer.valid(), "Scene failed to create index geometry atlas buffer.");
        nrAssert(geometryAtlas_.backingGeneration.index != std::numeric_limits<std::uint64_t>::max(),
                 "Scene index geometry atlas backing generation overflowed.");
        ++geometryAtlas_.backingGeneration.index;
    }

    submitGeometryAtlasGrowCopy(std::move(oldVertexBuffer), oldVertexHighWaterBytes, std::move(oldIndexBuffer),
                                oldIndexHighWaterBytes);
}

void Scene::submitGeometryAtlasGrowCopy(std::optional<nr::rhi::Buffer> oldVertexBuffer,
                                        vk::DeviceSize oldVertexHighWaterBytes,
                                        std::optional<nr::rhi::Buffer> oldIndexBuffer,
                                        vk::DeviceSize oldIndexHighWaterBytes)
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
        retiredBuffers.vertexBuffer.valid() && geometryAtlas_.vertexBuffer.valid() && oldVertexHighWaterBytes > 0u;
    auto const copyIndexPrefix =
        retiredBuffers.indexBuffer.valid() && geometryAtlas_.indexBuffer.valid() && oldIndexHighWaterBytes > 0u;

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
    growWork.commandBuffers.emplace(growWork.commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1));
    auto &commandBuffer = growWork.commandBuffers->front();

    commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    {
        auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};

        if (copyVertexPrefix)
        {
            auto copyRegion = vk::BufferCopy{};
            copyRegion.size = oldVertexHighWaterBytes;
            nr::rhi::ops::copyBuffer2(commandBuffer, growWork.retiredBuffers.vertexBuffer.handle(),
                                      geometryAtlas_.vertexBuffer.handle(), nr::rhi::ops::toBufferCopy2(copyRegion));
            postCopyBarriers.addBuffer(geometryAtlas_.vertexBuffer, vk::BufferMemoryBarrier2{
                                                                        vk::PipelineStageFlagBits2::eTransfer,
                                                                        vk::AccessFlagBits2::eTransferWrite,
                                                                        vk::PipelineStageFlagBits2::eAllCommands,
                                                                        vk::AccessFlagBits2::eMemoryRead,
                                                                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                        vk::Buffer{},
                                                                        0,
                                                                        oldVertexHighWaterBytes,
                                                                        nullptr,
                                                                    });
        }

        if (copyIndexPrefix)
        {
            auto copyRegion = vk::BufferCopy{};
            copyRegion.size = oldIndexHighWaterBytes;
            nr::rhi::ops::copyBuffer2(commandBuffer, growWork.retiredBuffers.indexBuffer.handle(),
                                      geometryAtlas_.indexBuffer.handle(), nr::rhi::ops::toBufferCopy2(copyRegion));
            postCopyBarriers.addBuffer(geometryAtlas_.indexBuffer, vk::BufferMemoryBarrier2{
                                                                       vk::PipelineStageFlagBits2::eTransfer,
                                                                       vk::AccessFlagBits2::eTransferWrite,
                                                                       vk::PipelineStageFlagBits2::eAllCommands,
                                                                       vk::AccessFlagBits2::eMemoryRead,
                                                                       nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                       nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                                                       vk::Buffer{},
                                                                       0,
                                                                       oldIndexHighWaterBytes,
                                                                       nullptr,
                                                                   });
        }

        nr::rhi::ops::pipelineBarrier(commandBuffer, postCopyBarriers);
    }
    commandBuffer.end();

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
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eTimeout,
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
    queueUploadsFor(std::span{textureHandles_}, textures_);
}

void Scene::reapRetiredGpuVersions()
{
    auto const serial = currentFrame_.frameSerial;
    auto expired = [serial](const auto &retiredVersion) { return retiredVersion.retireAfterSerial <= serial; };

    auto reclaimExpiredMeshPayloads = [&](std::vector<detail::RetiredMeshGpuPayload> &retiredPayloads) {
        std::erase_if(retiredPayloads, [&](const detail::RetiredMeshGpuPayload &retiredPayload) {
            if (!expired(retiredPayload))
            {
                return false;
            }

            releaseGeometryAtlasAllocation(retiredPayload.payload.atlas);
            return true;
        });
    };

    forEachLiveRecord(std::span{meshHandles_}, meshes_, [&](nr::resource::MeshHandle, MeshAssetRecord &record) {
        reclaimExpiredMeshPayloads(record.retiredGpu);
    });
    reapRetiredFor(std::span{textureHandles_}, textures_, expired);

    reclaimExpiredMeshPayloads(retiredMeshPayloadGraveyard_);
    std::erase_if(retiredTexturePayloadGraveyard_, expired);
    std::erase_if(retiredGeometryAtlasBuffers_, expired);
}

[[nodiscard]] std::size_t Scene::pixelFormatByteSize(nr::resource::PixelFormat format)
{
    switch (format)
    {
    case vk::Format::eR8Unorm:
        return 1;
    case vk::Format::eR8G8Unorm:
        return 2;
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
        return 4;
    case vk::Format::eR16G16B16A16Sfloat:
        return 8;
    case vk::Format::eR32G32B32A32Sfloat:
        return 16;
    default:
        break;
    }

    nrAssert(false, "Unsupported vk::Format '{}' for Scene texture byte-size calculation.",
             static_cast<std::uint32_t>(format));
    return 4;
}

[[nodiscard]] std::vector<std::byte> Scene::makeFallbackTextureBytes(nr::resource::PixelFormat format)
{
    auto const byteSize = pixelFormatByteSize(format);
    return std::vector<std::byte>(byteSize, std::byte{0xFF});
}

void Scene::uploadMeshAsset(nr::resource::MeshHandle handle, MeshAssetRecord &record,
                            nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    nrAssert(record.gpuState == GpuResidencyState::uploadQueued,
             "Scene mesh upload requires the queued residency state.");
    auto vertexBytes = std::as_bytes(std::span{record.cpu.vertices});
    auto indexBytes = std::as_bytes(std::span{record.cpu.indices});
    nrAssert(!vertexBytes.empty(), "Queued Scene mesh upload requires validated non-empty vertex data.");

    auto payload = detail::MeshGpuPayload{};
    payload.atlas = reserveGeometryAtlasAllocation(record.cpu, uploadContext);
    nrAssert(geometryAtlas_.vertexBuffer.valid(), "Scene mesh upload requires a valid vertex geometry atlas.");

    auto pendingBatch = PendingGraphicsSyncBatch{};
    pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::mesh);
    pendingBatch.targetGpuVersion = record.cpuVersion;

    auto vertexTicket =
        uploadContext.uploadBuffer(vertexBytes, geometryAtlas_.vertexBuffer, payload.atlas.vertexByteOffset);
    nrAssert(vertexTicket.valid(), "Scene mesh vertex upload requires a valid RHI upload ticket.");

    auto indexTicket = std::optional<nr::rhi::ops::BufferUploadTicket>{};
    if (!indexBytes.empty())
    {
        nrAssert(geometryAtlas_.indexBuffer.valid(),
                 "Scene indexed mesh upload requires a valid index geometry atlas.");
        auto uploadedIndexTicket =
            uploadContext.uploadBuffer(indexBytes, geometryAtlas_.indexBuffer, payload.atlas.indexByteOffset);
        nrAssert(uploadedIndexTicket.valid(), "Scene mesh index upload requires a valid RHI upload ticket.");

        indexTicket = std::move(uploadedIndexTicket);
    }

    queueRetiredPayload(record.gpu, record.retiredGpu);
    record.gpu = std::move(payload);
    record.gpuState = GpuResidencyState::waitingGraphicsSync;

    // Tickets store Buffer references; keep them bound to the scene-owned atlas buffers.
    vertexTicket.buffer = std::cref(geometryAtlas_.vertexBuffer);
    pendingBatch.bufferTickets.push_back(vertexTicket);

    if (indexTicket.has_value())
    {
        nrAssert(geometryAtlas_.indexBuffer.valid(),
                 "Scene indexed mesh ticket rebinding requires a valid index geometry atlas.");
        indexTicket->buffer = std::cref(geometryAtlas_.indexBuffer);
        pendingBatch.bufferTickets.push_back(*indexTicket);
    }

    pendingGraphicsSyncBatches_.push_back(std::move(pendingBatch));
}

void Scene::uploadTextureAsset(nr::resource::TextureHandle handle, TextureAssetRecord &record,
                               nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    nrAssert(record.gpuState == GpuResidencyState::uploadQueued,
             "Scene texture upload requires the queued residency state.");
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
    nrAssert(!imageData.empty(), "Scene texture upload requires non-empty source image data.");

    auto imageCreateInfo = vk::ImageCreateInfo{};
    imageCreateInfo.imageType = record.cpu.dimension;
    imageCreateInfo.format = record.cpu.format;
    imageCreateInfo.extent = levelExtent;
    imageCreateInfo.mipLevels = 1;
    imageCreateInfo.arrayLayers = 1;
    imageCreateInfo.samples = vk::SampleCountFlagBits::e1;
    imageCreateInfo.tiling = vk::ImageTiling::eOptimal;
    imageCreateInfo.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
    imageCreateInfo.sharingMode = vk::SharingMode::eExclusive;
    imageCreateInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto payload = detail::TextureGpuPayload{};
    payload.image = device_.resourceFactory.createImage(imageCreateInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                        std::format("scene_texture_{}_gpu", handle.slot));
    payload.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    nrAssert(payload.image.valid(), "Scene texture upload requires a valid destination image.");

    auto uploadPlan =
        makeTransferToGraphicsUploadPlan(vk::PipelineStageFlagBits2::eAllCommands, vk::AccessFlagBits2::eShaderRead);
    auto uploadTicket =
        uploadContext.uploadImage(imageData, payload.image, vk::ImageLayout::eUndefined, payload.layout, uploadPlan);
    nrAssert(uploadTicket.valid(), "Scene texture upload requires a valid RHI upload ticket.");

    queueRetiredPayload(record.gpu, record.retiredGpu);
    record.gpu = std::move(payload);
    record.gpuState = GpuResidencyState::waitingGraphicsSync;

    // uploadTicket stores an Image reference; rebind to the record-owned payload after move.
    uploadTicket.image = std::cref(record.gpu->image);

    auto pendingBatch = PendingGraphicsSyncBatch{};
    pendingBatch.asset = makeGpuHandleRef(handle, GpuAssetKind::texture);
    pendingBatch.targetGpuVersion = record.cpuVersion;
    pendingBatch.imageTickets.push_back(uploadTicket);
    pendingGraphicsSyncBatches_.push_back(std::move(pendingBatch));
}

void Scene::uploadQueuedMeshAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, UploadBudgetState &budget)
{
    forEachLiveRecord(std::span{meshHandles_}, meshes_, [&](nr::resource::MeshHandle handle, MeshAssetRecord &record) {
        if (record.gpuState != GpuResidencyState::uploadQueued)
        {
            return;
        }

        auto const bytesNeeded = meshUploadBytes(record.cpu);
        nrAssert(bytesNeeded > 0u, "Queued Scene mesh upload requires a validated non-empty payload.");
        if (!uploadBudgetAllows(budget, bytesNeeded))
        {
            return;
        }

        uploadMeshAsset(handle, record, uploadContext);
        recordBudgetedUpload(budget, bytesNeeded);
    });
}

void Scene::uploadQueuedTextureAssets(nr::rhi::ops::UploadReadbackContext &uploadContext, UploadBudgetState &budget)
{
    forEachLiveRecord(std::span{textureHandles_}, textures_,
                      [&](nr::resource::TextureHandle handle, TextureAssetRecord &record) {
                          if (record.gpuState != GpuResidencyState::uploadQueued)
                          {
                              return;
                          }

                          auto bytesNeeded = textureUploadBytes(record.cpu);
                          if (bytesNeeded == 0)
                          {
                              bytesNeeded = pixelFormatByteSize(record.cpu.format);
                          }

                          nrAssert(bytesNeeded > 0u, "Queued Scene texture upload requires a non-empty payload.");
                          if (!uploadBudgetAllows(budget, bytesNeeded))
                          {
                              return;
                          }

                          uploadTextureAsset(handle, record, uploadContext);
                          recordBudgetedUpload(budget, bytesNeeded);
                      });
}

void Scene::markGraphicsSyncCompletionResident(const GraphicsSyncCompletion &completion)
{
    switch (completion.asset.kind)
    {
    case GpuAssetKind::mesh: {
        auto handle = nr::resource::MeshHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = meshes_.tryGet(handle); record != nullptr)
        {
            nrAssert(record->gpuState == GpuResidencyState::waitingGraphicsSync,
                     "Scene mesh completion requires the waiting graphics-sync residency state.");
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            commitMutation(SceneRevisionMutation::meshResident);
            collectUnusedMeshAsset(handle);
        }
        break;
    }
    case GpuAssetKind::texture: {
        auto handle = nr::resource::TextureHandle{completion.asset.slot, completion.asset.generation};
        if (auto *record = textures_.tryGet(handle); record != nullptr)
        {
            nrAssert(record->gpuState == GpuResidencyState::waitingGraphicsSync,
                     "Scene texture completion requires the waiting graphics-sync residency state.");
            record->gpuState = GpuResidencyState::resident;
            record->gpuVersion = completion.targetGpuVersion;
            if (record->cpuVersion <= completion.targetGpuVersion)
            {
                discardUploadSourceIfConfigured(*record);
            }
            commitMutation(SceneRevisionMutation::textureResident);
            collectUnusedTextureAsset(handle);
        }
        break;
    }
    default:
        break;
    }
}

[[nodiscard]] std::uint64_t Scene::maxUploadSignalValue(const PendingGraphicsSyncBatch &batch) noexcept
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

void Scene::reapSubmittedGraphicsSyncWork()
{
    auto completed = std::vector<GraphicsSyncCompletion>{};
    std::erase_if(submittedGraphicsSyncWork_, [&](SubmittedGraphicsSyncWork &work) {
        nrAssert(*work.fence != nullptr, "Scene submitted graphics sync work requires a valid fence.");
        auto result = device_.device.waitForFences(*work.fence, vk::True, 0u);
        nrAssert(result == vk::Result::eSuccess || result == vk::Result::eTimeout,
                 "Scene failed querying graphics sync fence status.");
        if (result != vk::Result::eSuccess || !submittedGeometryAtlasGrowWork_.empty())
        {
            return false;
        }

        std::ranges::move(work.completions, std::back_inserter(completed));
        return true;
    });

    std::ranges::for_each(
        completed, [&](const GraphicsSyncCompletion &completion) { markGraphicsSyncCompletionResident(completion); });
    if (!completed.empty())
    {
        compactDeadAssetHandles();
    }
}

void Scene::submitPendingGraphicsSyncBatches(nr::rhi::ops::UploadReadbackContext &uploadContext)
{
    if (pendingGraphicsSyncBatches_.empty())
    {
        return;
    }

    uploadContext.reclaimCompletedUploads();
    auto batches = std::exchange(pendingGraphicsSyncBatches_, {});
    auto maxSignalValue = std::uint64_t{0};
    std::ranges::for_each(batches, [&](const PendingGraphicsSyncBatch &batch) {
        maxSignalValue = std::max(maxSignalValue, maxUploadSignalValue(batch));
    });
    nrAssert(maxSignalValue > 0u, "Scene staged graphics sync requires a valid upload timeline signal.");

    auto syncWork = SubmittedGraphicsSyncWork{};
    syncWork.completions = batches | std::views::transform([](const PendingGraphicsSyncBatch &batch) {
                               return GraphicsSyncCompletion{
                                   .asset = batch.asset,
                                   .targetGpuVersion = batch.targetGpuVersion,
                               };
                           }) |
                           std::ranges::to<std::vector>();

    auto syncSubmission = nr::rhi::CommandBatch{};
    syncSubmission.addWait(uploadContext.uploadTimelineSemaphore(), vk::PipelineStageFlagBits2::eAllCommands,
                           maxSignalValue);

    syncWork.commandPool = nr::rhi::CommandPool{
        device_.device,
        device_.queueManager.graphics().queueFamilyIndex(),
        vk::CommandPoolCreateFlagBits::eTransient,
    };
    syncWork.commandBuffers.emplace(syncWork.commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1));
    auto &syncCommandBuffer = syncWork.commandBuffers->front();

    syncCommandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    std::ranges::for_each(batches, [&](const PendingGraphicsSyncBatch &batch) {
        std::ranges::for_each(batch.bufferTickets, [&](const nr::rhi::ops::BufferUploadTicket &ticket) {
            uploadContext.recordAcquireBarrier(syncCommandBuffer, ticket);
        });
        std::ranges::for_each(batch.imageTickets, [&](const nr::rhi::ops::ImageUploadTicket &ticket) {
            uploadContext.recordImageAcquireBarrier(syncCommandBuffer, ticket);
        });
    });
    syncCommandBuffer.end();
    syncSubmission.addCommandBuffer(syncCommandBuffer);

    syncWork.fence = vk::raii::Fence(device_.device, vk::FenceCreateInfo{});
    device_.queueManager.graphics().submit(std::move(syncSubmission), std::cref(syncWork.fence));
    submittedGraphicsSyncWork_.push_back(std::move(syncWork));
}

void Scene::subtractSaturating(std::size_t &value, std::size_t delta) noexcept
{
    value = value >= delta ? value - delta : 0u;
}

void Scene::compactDeadAssetHandles()
{
    compactDeadHandles(meshHandles_, meshes_);
    compactDeadHandles(textureHandles_, textures_);
}

void Scene::collectUnusedMeshAsset(nr::resource::MeshHandle handle)
{
    collectUnusedAsset(handle, meshes_, retiredMeshPayloadGraveyard_);
}

void Scene::collectUnusedMaterialAsset(nr::resource::MaterialHandle handle)
{
    collectUnusedCpuAsset(handle, materials_);
}

void Scene::collectUnusedTextureAsset(nr::resource::TextureHandle handle)
{
    collectUnusedAsset(handle, textures_, retiredTexturePayloadGraveyard_);
}

void Scene::collectUnusedCameraAsset(nr::resource::CameraAssetHandle handle)
{
    collectUnusedCpuAsset(handle, cameras_);
}

void Scene::collectUnusedLightAsset(nr::resource::LightAssetHandle handle)
{
    collectUnusedCpuAsset(handle, lights_);
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

} // namespace nr::scene
