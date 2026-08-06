module nr.rhi;
import :resourceOps;
import dependency.vulkan;
import std;
import :command;
import :commandBatch;
import :commandPool;
import :queue;
import :resource;
import :resourcePool;
import :sync;
import :vk;

namespace nr::rhi::ops
{
namespace
{
constexpr vk::DeviceSize kRingAllocationAlignment = 16u;

[[nodiscard]] std::uint64_t checkedAdd(std::uint64_t lhs, std::uint64_t rhs, std::string_view operation)
{
    nrAssert(rhs <= std::numeric_limits<std::uint64_t>::max() - lhs,
             std::format("{} overflows a 64-bit size.", operation));
    return lhs + rhs;
}

[[nodiscard]] std::uint64_t checkedMultiply(std::uint64_t lhs, std::uint64_t rhs, std::string_view operation)
{
    nrAssert(lhs == 0u || rhs <= std::numeric_limits<std::uint64_t>::max() / lhs,
             std::format("{} overflows a 64-bit size.", operation));
    return lhs * rhs;
}

[[nodiscard]] constexpr QueueAccessScope accelerationStructureBuildWriteScope() noexcept
{
    return QueueAccessScope{
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
        .access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    };
}

[[nodiscard]] constexpr QueueAccessScope accelerationStructureCopyWriteScope() noexcept
{
    return QueueAccessScope{
        .stages = vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR,
        .access = vk::AccessFlagBits2::eAccelerationStructureWriteKHR,
    };
}

[[nodiscard]] constexpr QueueAccessScope rayTracingShaderAccelerationStructureReadScope() noexcept
{
    return QueueAccessScope{
        .stages = vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        .access = vk::AccessFlagBits2::eAccelerationStructureReadKHR,
    };
}

[[nodiscard]] vk::MemoryBarrier2 makeAccelerationStructureBarrier(const QueueAccessScope &srcScope,
                                                                  const QueueAccessScope &dstScope)
{
    nrAssert(srcScope.valid() && dstScope.valid(),
             "makeAccelerationStructureBarrier requires valid source/destination scopes.");
    return vk::MemoryBarrier2{
        srcScope.stages, srcScope.access, dstScope.stages, dstScope.access, nullptr,
    };
}
} // namespace

[[nodiscard]] vk::ImageSubresourceRange fullSubresourceRange(const Image &image)
{
    return vk::ImageSubresourceRange{
        inferAspectFlags(image.format()), 0, image.mipLevels(), 0, image.arrayLayers(),
    };
}

[[nodiscard]] vk::BufferMemoryBarrier2 makeBufferBarrier(const Buffer &buffer, vk::BufferMemoryBarrier2 barrier)
{
    barrier.buffer = buffer.handle();
    return barrier;
}

[[nodiscard]] vk::ImageMemoryBarrier2 makeImageBarrier(const Image &image, vk::ImageMemoryBarrier2 barrier)
{
    if (barrier.subresourceRange.aspectMask == vk::ImageAspectFlags{})
    {
        barrier.subresourceRange = fullSubresourceRange(image);
    }
    barrier.image = image.handle();
    return barrier;
}

[[nodiscard]] vk::ImageMemoryBarrier2 makeImageLayoutTransitionBarrier(
    const Image &image, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::PipelineStageFlags2 srcStages,
    vk::AccessFlags2 srcAccess, vk::PipelineStageFlags2 dstStages, vk::AccessFlags2 dstAccess)
{
    return makeImageBarrier(image, vk::ImageMemoryBarrier2{
                                       srcStages,
                                       srcAccess,
                                       dstStages,
                                       dstAccess,
                                       oldLayout,
                                       newLayout,
                                       kIgnoredQueueFamilyIndex,
                                       kIgnoredQueueFamilyIndex,
                                       vk::Image{},
                                       {},
                                       nullptr,
                                   });
}

[[nodiscard]] bool QueueAccessScope::valid() const noexcept
{
    return stages != vk::PipelineStageFlags2{};
}

[[nodiscard]] vk::MemoryBarrier2 makeAccelerationStructureBuildToTraceReadBarrier()
{
    return makeAccelerationStructureBarrier(accelerationStructureBuildWriteScope(),
                                            rayTracingShaderAccelerationStructureReadScope());
}

[[nodiscard]] vk::MemoryBarrier2 makeAccelerationStructureCopyToTraceReadBarrier()
{
    return makeAccelerationStructureBarrier(accelerationStructureCopyWriteScope(),
                                            rayTracingShaderAccelerationStructureReadScope());
}

[[nodiscard]] bool QueueOwnershipWait::valid() const noexcept
{
    return semaphore != vk::Semaphore{};
}

[[nodiscard]] bool QueueOwnershipTransfer::valid() const noexcept
{
    return srcQueueFamilyIndex != kIgnoredQueueFamilyIndex && dstQueueFamilyIndex != kIgnoredQueueFamilyIndex &&
           srcQueueFamilyIndex != dstQueueFamilyIndex && release.valid() && acquire.valid();
}

[[nodiscard]] bool QueueOwnershipTransfer::hasWait() const noexcept
{
    return waitSemaphore != vk::Semaphore{};
}

[[nodiscard]] QueueOwnershipTransfer makeQueueOwnershipTransfer(std::uint32_t srcQueueFamilyIndex,
                                                                std::uint32_t dstQueueFamilyIndex,
                                                                const QueueAccessScope &releaseScope,
                                                                const QueueAccessScope &acquireScope,
                                                                std::optional<QueueOwnershipWait> wait)
{
    nrAssert(releaseScope.valid() && acquireScope.valid(),
             "makeQueueOwnershipTransfer requires non-empty release/acquire stage masks.");

    auto transfer = QueueOwnershipTransfer{
        .srcQueueFamilyIndex = srcQueueFamilyIndex,
        .dstQueueFamilyIndex = dstQueueFamilyIndex,
        .release = releaseScope,
        .acquire = acquireScope,
    };

    if (wait.has_value())
    {
        nrAssert(wait->valid(),
                 "makeQueueOwnershipTransfer requires a valid wait semaphore when wait payload is provided.");
        transfer.waitSemaphore = wait->semaphore;
        transfer.waitValue = wait->value;
    }

    return transfer;
}

[[nodiscard]] bool BufferUploadOwnershipPlan::isSameQueueFamily() const noexcept
{
    return !acquireToTransfer.has_value() &&
           releaseToDestination.srcQueueFamilyIndex == releaseToDestination.dstQueueFamilyIndex;
}

[[nodiscard]] bool BufferUploadOwnershipPlan::valid(std::uint32_t transferQueueFamilyIndex) const noexcept
{
    // Same-queue-family plans have a different validation path
    if (isSameQueueFamily())
    {
        return releaseToDestination.srcQueueFamilyIndex == transferQueueFamilyIndex &&
               releaseToDestination.release.valid() && releaseToDestination.acquire.valid();
    }

    auto outgoingValid =
        releaseToDestination.valid() && releaseToDestination.srcQueueFamilyIndex == transferQueueFamilyIndex;

    auto incomingValid =
        !acquireToTransfer.has_value() || (acquireToTransfer->valid() && acquireToTransfer->hasWait() &&
                                           acquireToTransfer->dstQueueFamilyIndex == transferQueueFamilyIndex);

    return outgoingValid && incomingValid;
}

[[nodiscard]] const vk::DependencyInfo &DependencyInfoPacket::dependencyInfo() const noexcept
{
    return info;
}

void BarrierBatch::clear()
{
    dependencyFlags_ = {};
    memoryBarriers_.clear();
    bufferBarriers_.clear();
    imageBarriers_.clear();
}

[[nodiscard]] bool BarrierBatch::empty() const noexcept
{
    return memoryBarriers_.empty() && bufferBarriers_.empty() && imageBarriers_.empty();
}

void BarrierBatch::addDependencyFlags(vk::DependencyFlags flags) noexcept
{
    dependencyFlags_ |= flags;
}

void BarrierBatch::addBuffer(const Buffer &buffer, vk::BufferMemoryBarrier2 barrier)
{
    add(makeBufferBarrier(buffer, std::move(barrier)));
}

void BarrierBatch::addImage(const Image &image, vk::ImageMemoryBarrier2 barrier)
{
    add(makeImageBarrier(image, std::move(barrier)));
}

[[nodiscard]] DependencyInfoPacket BarrierBatch::buildDependencyInfo() const
{
    DependencyInfoPacket packet{};
    packet.memoryBarriers = memoryBarriers_;
    packet.bufferBarriers = bufferBarriers_;
    packet.imageBarriers = imageBarriers_;

    packet.info = vk::DependencyInfo{};
    packet.info.dependencyFlags = dependencyFlags_;
    packet.info.memoryBarrierCount = static_cast<std::uint32_t>(packet.memoryBarriers.size());
    packet.info.pMemoryBarriers = packet.memoryBarriers.data();
    packet.info.bufferMemoryBarrierCount = static_cast<std::uint32_t>(packet.bufferBarriers.size());
    packet.info.pBufferMemoryBarriers = packet.bufferBarriers.data();
    packet.info.imageMemoryBarrierCount = static_cast<std::uint32_t>(packet.imageBarriers.size());
    packet.info.pImageMemoryBarriers = packet.imageBarriers.data();
    return packet;
}

void pipelineBarrier(const vk::raii::CommandBuffer &commandBuffer, const BarrierBatch &barriers)
{
    auto packet = barriers.buildDependencyInfo();
    commandBuffer.pipelineBarrier2(packet.dependencyInfo());
}

[[nodiscard]] vk::BufferCopy2 toBufferCopy2(vk::BufferCopy region)
{
    return vk::BufferCopy2{region.srcOffset, region.dstOffset, region.size};
}

[[nodiscard]] vk::BufferImageCopy2 toBufferImageCopy2(vk::BufferImageCopy region)
{
    return vk::BufferImageCopy2{
        region.bufferOffset,     region.bufferRowLength, region.bufferImageHeight,
        region.imageSubresource, region.imageOffset,     region.imageExtent,
    };
}

[[nodiscard]] vk::ImageCopy2 toImageCopy2(vk::ImageCopy region)
{
    return vk::ImageCopy2{
        region.srcSubresource, region.srcOffset, region.dstSubresource, region.dstOffset, region.extent,
    };
}

void copyBuffer2(const vk::raii::CommandBuffer &commandBuffer, vk::Buffer src, vk::Buffer dst, vk::BufferCopy2 region)
{
    auto copyInfo = vk::CopyBufferInfo2{src, dst, 1u, &region};
    commandBuffer.copyBuffer2(copyInfo);
}

void copyBufferToImage2(const vk::raii::CommandBuffer &commandBuffer, vk::Buffer src, vk::Image dst,
                        vk::ImageLayout dstLayout, vk::BufferImageCopy2 region)
{
    auto copyInfo = vk::CopyBufferToImageInfo2{src, dst, dstLayout, 1u, &region};
    commandBuffer.copyBufferToImage2(copyInfo);
}

void copyImageToBuffer2(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::ImageLayout srcLayout,
                        vk::Buffer dst, vk::BufferImageCopy2 region)
{
    auto copyInfo = vk::CopyImageToBufferInfo2{src, srcLayout, dst, 1u, &region};
    commandBuffer.copyImageToBuffer2(copyInfo);
}

void copyImage2(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::ImageLayout srcLayout, vk::Image dst,
                vk::ImageLayout dstLayout, vk::ImageCopy2 region)
{
    auto copyInfo = vk::CopyImageInfo2{src, srcLayout, dst, dstLayout, 1u, &region};
    commandBuffer.copyImage2(copyInfo);
}

[[nodiscard]] vk::BufferImageCopy normalizeBufferImageCopyRegion(const Image &image, vk::BufferImageCopy region)
{
    nrAssert(image.valid(), "Buffer-image copies require a valid image.");
    if (region.imageSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.imageSubresource.aspectMask = inferAspectFlags(image.format());
    }
    if (region.imageSubresource.layerCount == 0u)
    {
        region.imageSubresource.layerCount = 1u;
    }

    nrAssert(region.bufferOffset == 0u,
             "Upload/readback ring buffer-image copies require a payload-relative bufferOffset of zero.");
    auto const supportedAspects = inferAspectFlags(image.format());
    nrAssert((region.imageSubresource.aspectMask & supportedAspects) == region.imageSubresource.aspectMask,
             "Buffer-image copy aspect mask is incompatible with the image format.");
    nrAssert(region.imageSubresource.mipLevel < image.mipLevels(),
             "Buffer-image copy mip level exceeds the image mip count.");
    nrAssert(region.imageSubresource.baseArrayLayer <= image.arrayLayers() &&
                 region.imageSubresource.layerCount <=
                     image.arrayLayers() - region.imageSubresource.baseArrayLayer,
             "Buffer-image copy array-layer range exceeds the image layer count.");

    auto const mipLevel = region.imageSubresource.mipLevel;
    auto mipDimension = [mipLevel](std::uint32_t dimension) {
        return mipLevel < std::numeric_limits<std::uint32_t>::digits ? std::max(1u, dimension >> mipLevel) : 1u;
    };
    auto const mipExtent = vk::Extent3D{
        mipDimension(image.extent().width),
        mipDimension(image.extent().height),
        mipDimension(image.extent().depth),
    };
    nrAssert(region.imageOffset.x >= 0 && region.imageOffset.y >= 0 && region.imageOffset.z >= 0,
             "Buffer-image copy offsets must be non-negative for upload/readback ring operations.");
    auto const offset = vk::Extent3D{
        static_cast<std::uint32_t>(region.imageOffset.x),
        static_cast<std::uint32_t>(region.imageOffset.y),
        static_cast<std::uint32_t>(region.imageOffset.z),
    };
    nrAssert(offset.width <= mipExtent.width && offset.height <= mipExtent.height && offset.depth <= mipExtent.depth,
             "Buffer-image copy offset exceeds the selected mip extent.");
    if (region.imageExtent == vk::Extent3D{})
    {
        region.imageExtent = vk::Extent3D{
            mipExtent.width - offset.width,
            mipExtent.height - offset.height,
            mipExtent.depth - offset.depth,
        };
    }
    nrAssert(region.imageExtent.width > 0u && region.imageExtent.height > 0u && region.imageExtent.depth > 0u,
             "Buffer-image copy extent must be non-zero.");
    nrAssert(region.imageExtent.width <= mipExtent.width - offset.width &&
                 region.imageExtent.height <= mipExtent.height - offset.height &&
                 region.imageExtent.depth <= mipExtent.depth - offset.depth,
             "Buffer-image copy extent exceeds the selected mip bounds.");
    nrAssert(region.bufferRowLength == 0u || region.bufferRowLength >= region.imageExtent.width,
             "Buffer-image copy row length must cover the copied width.");
    nrAssert(region.bufferImageHeight == 0u || region.bufferImageHeight >= region.imageExtent.height,
             "Buffer-image copy image height must cover the copied height.");
    return region;
}

[[nodiscard]] vk::DeviceSize linearBufferImageCopySize(const vk::BufferImageCopy &region, vk::DeviceSize elementSize)
{
    const auto rowLength = region.bufferRowLength > 0 ? region.bufferRowLength : region.imageExtent.width;
    const auto imageHeight = region.bufferImageHeight > 0 ? region.bufferImageHeight : region.imageExtent.height;
    const auto layerCount = std::max(1u, region.imageSubresource.layerCount);

    auto byteSize = checkedMultiply(rowLength, imageHeight, "linear image row count");
    byteSize = checkedMultiply(byteSize, region.imageExtent.depth, "linear image depth size");
    byteSize = checkedMultiply(byteSize, layerCount, "linear image layer size");
    byteSize = checkedMultiply(byteSize, elementSize, "linear image byte size");
    return std::max<vk::DeviceSize>(1u, byteSize);
}

[[nodiscard]] std::vector<LinearImageUploadChunk> planLinearImageUploadChunks(const vk::BufferImageCopy &region,
                                                                              vk::DeviceSize elementSize,
                                                                              vk::DeviceSize ringCapacity)
{
    nrAssert(region.bufferOffset == 0u, "planLinearImageUploadChunks requires payload-relative bufferOffset 0.");
    nrAssert(elementSize > 0u, "planLinearImageUploadChunks requires a non-zero element size.");
    nrAssert(ringCapacity > 0u, "planLinearImageUploadChunks requires a non-zero ring capacity.");
    nrAssert(region.imageExtent.width > 0u && region.imageExtent.height > 0u && region.imageExtent.depth > 0u,
             "planLinearImageUploadChunks requires a non-empty image extent.");
    nrAssert(region.imageSubresource.layerCount > 0u, "planLinearImageUploadChunks requires at least one image layer.");

    auto const rowLength = region.bufferRowLength > 0u ? region.bufferRowLength : region.imageExtent.width;
    auto const imageHeight = region.bufferImageHeight > 0u ? region.bufferImageHeight : region.imageExtent.height;
    nrAssert(rowLength >= region.imageExtent.width,
             "planLinearImageUploadChunks bufferRowLength must cover the copied image width.");
    nrAssert(imageHeight >= region.imageExtent.height,
             "planLinearImageUploadChunks bufferImageHeight must cover the copied image height.");
    auto const rowStride = checkedMultiply(rowLength, elementSize, "linear image row stride");
    nrAssert(rowStride <= ringCapacity,
             std::format("planLinearImageUploadChunks row size ({} bytes) exceeds upload ring capacity ({} bytes).",
                         rowStride, ringCapacity));

    auto const rowsPerChunk = static_cast<std::uint32_t>(
        std::min<vk::DeviceSize>(region.imageExtent.height, std::max<vk::DeviceSize>(1u, ringCapacity / rowStride)));
    auto const chunksPerSlice = 1u + (region.imageExtent.height - 1u) / rowsPerChunk;
    auto const sliceCount = checkedMultiply(region.imageSubresource.layerCount, region.imageExtent.depth,
                                            "linear image upload slice count");
    auto const chunkCount = checkedMultiply(sliceCount, chunksPerSlice, "linear image upload chunk count");
    nrAssert(std::in_range<std::size_t>(chunkCount),
             "linear image upload chunk count exceeds the host container size range.");
    auto chunks = std::vector<LinearImageUploadChunk>{};
    nrAssert(chunkCount <= chunks.max_size(), "linear image upload chunk count exceeds vector::max_size().");
    chunks.reserve(static_cast<std::size_t>(chunkCount));

    auto const layers = std::views::iota(std::uint32_t{0u}, region.imageSubresource.layerCount);
    std::ranges::for_each(layers, [&](std::uint32_t layerIndex) {
        auto const depthSlices = std::views::iota(std::uint32_t{0u}, region.imageExtent.depth);
        std::ranges::for_each(depthSlices, [&](std::uint32_t depthIndex) {
            auto const chunkIndices = std::views::iota(std::uint32_t{0u}, chunksPerSlice);
            std::ranges::for_each(chunkIndices, [&](std::uint32_t chunkIndex) {
                auto const firstRow = chunkIndex * rowsPerChunk;
                auto const rowCount = std::min(rowsPerChunk, region.imageExtent.height - firstRow);
                auto const linearSliceIndex =
                    checkedAdd(checkedMultiply(layerIndex, region.imageExtent.depth,
                                               "linear image upload layer offset"),
                               depthIndex, "linear image upload depth offset");
                auto const sourceRowIndex =
                    checkedAdd(checkedMultiply(linearSliceIndex, imageHeight, "linear image upload slice stride"),
                               firstRow, "linear image upload row offset");
                auto const sourceOffset =
                    checkedMultiply(sourceRowIndex, rowStride, "linear image upload source offset");
                auto const byteSize = checkedMultiply(rowCount, rowStride, "linear image upload chunk size");

                auto copyRegion = region;
                copyRegion.bufferOffset = 0u;
                copyRegion.bufferImageHeight = 0u;
                nrAssert(layerIndex <= std::numeric_limits<std::uint32_t>::max() -
                                          copyRegion.imageSubresource.baseArrayLayer,
                         "linear image upload array-layer offset overflows uint32_t.");
                copyRegion.imageSubresource.baseArrayLayer += layerIndex;
                copyRegion.imageSubresource.layerCount = 1u;
                auto const imageOffsetY = static_cast<std::int64_t>(copyRegion.imageOffset.y) + firstRow;
                auto const imageOffsetZ = static_cast<std::int64_t>(copyRegion.imageOffset.z) + depthIndex;
                nrAssert(std::in_range<std::int32_t>(imageOffsetY) && std::in_range<std::int32_t>(imageOffsetZ),
                         "linear image upload chunk offset exceeds the Vulkan int32_t range.");
                copyRegion.imageOffset.y = static_cast<std::int32_t>(imageOffsetY);
                copyRegion.imageOffset.z = static_cast<std::int32_t>(imageOffsetZ);
                copyRegion.imageExtent.height = rowCount;
                copyRegion.imageExtent.depth = 1u;

                chunks.push_back(LinearImageUploadChunk{
                    .sourceOffset = sourceOffset,
                    .byteSize = byteSize,
                    .copyRegion = copyRegion,
                });
            });
        });
    });
    return chunks;
}

[[nodiscard]] vk::ImageCopy normalizeImageCopyRegion(vk::Extent3D srcExtent, vk::ImageAspectFlags srcAspect,
                                                     vk::Extent3D dstExtent, vk::ImageAspectFlags dstAspect,
                                                     vk::ImageCopy region)
{
    auto effectiveSrcAspect = srcAspect == vk::ImageAspectFlags{} ? vk::ImageAspectFlagBits::eColor : srcAspect;
    auto effectiveDstAspect = dstAspect == vk::ImageAspectFlags{} ? vk::ImageAspectFlagBits::eColor : dstAspect;

    if (region.srcSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.srcSubresource = vk::ImageSubresourceLayers{effectiveSrcAspect, 0, 0, 1};
    }
    if (region.dstSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.dstSubresource = vk::ImageSubresourceLayers{effectiveDstAspect, 0, 0, 1};
    }
    if (region.extent == vk::Extent3D{})
    {
        region.extent = vk::Extent3D{
            std::min(srcExtent.width, dstExtent.width),
            std::min(srcExtent.height, dstExtent.height),
            std::min(srcExtent.depth, dstExtent.depth),
        };
    }

    return region;
}

void copyImageToImage(const vk::raii::CommandBuffer &commandBuffer, vk::Image src, vk::Extent3D srcExtent,
                      vk::ImageAspectFlags srcAspect, vk::Image dst, vk::Extent3D dstExtent,
                      vk::ImageAspectFlags dstAspect, vk::ImageLayout srcLayout, vk::ImageLayout dstLayout,
                      const vk::ImageCopy &region)
{
    auto effectiveRegion = normalizeImageCopyRegion(srcExtent, srcAspect, dstExtent, dstAspect, region);
    copyImage2(commandBuffer, src, srcLayout, dst, dstLayout, toImageCopy2(effectiveRegion));
}

[[nodiscard]] bool BufferUploadTicket::valid() const noexcept
{
    return buffer.has_value() && size > 0 && signalValue > 0;
}

[[nodiscard]] bool ImageUploadTicket::valid() const noexcept
{
    return image.has_value() && layout != vk::ImageLayout::eUndefined && signalValue > 0;
}

[[nodiscard]] bool ReadbackSyncScope::valid() const noexcept
{
    return stages != vk::PipelineStageFlags2{};
}

[[nodiscard]] bool ReadbackSyncPlan::valid() const noexcept
{
    return preCopy.valid() && postCopy.valid();
}

UploadReadbackContext::UploadReadbackContext(const vk::raii::Device &device, ResourceFactory &resourceFactory,
                                             QueueManager &queueManager,
                                             QueueFamilyTransferPolicy queueFamilyTransferPolicy,
                                             vk::DeviceSize uploadRingSize, vk::DeviceSize readbackRingSize)
    : device_(std::cref(device)), queueManager_(std::ref(queueManager)),
      queueFamilyTransferPolicy_(std::move(queueFamilyTransferPolicy)), uploadCapacity_(uploadRingSize),
      readbackCapacity_(readbackRingSize),
      transferPool_(device, queueManager.transfer().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient),
      graphicsReadbackPool_(device, queueManager.graphics().queueFamilyIndex(),
                            vk::CommandPoolCreateFlagBits::eTransient),
      computeReadbackPool_(device, queueManager.compute().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient)
{
    nrAssert(uploadRingSize >= kRingAllocationAlignment && readbackRingSize >= kRingAllocationAlignment &&
                 uploadRingSize % kRingAllocationAlignment == 0u &&
                 readbackRingSize % kRingAllocationAlignment == 0u,
             std::format("UploadReadbackContext ring capacities must be multiples of {} bytes and at least that "
                         "large (upload={}, readback={}).",
                         kRingAllocationAlignment, uploadRingSize, readbackRingSize));

    vk::BufferCreateInfo uploadInfo{};
    uploadInfo.size = uploadRingSize;
    uploadInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
    uploadInfo.sharingMode = vk::SharingMode::eExclusive;
    uploadRing_ = resourceFactory.createBuffer(uploadInfo, MemoryUsage::CpuToGpu, "upload_ring");

    auto readbackQueueFamilies = uniqueReadbackQueueFamilies(std::array{
        queueManager.graphics().queueFamilyIndex(),
        queueManager.compute().queueFamilyIndex(),
    });
    nrAssert(!readbackQueueFamilies.empty(),
             "UploadReadbackContext requires at least one valid queue family for readback ring setup.");

    vk::BufferCreateInfo readbackInfo{};
    readbackInfo.size = readbackRingSize;
    readbackInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
    if (readbackQueueFamilies.size() == 1)
    {
        readbackInfo.sharingMode = vk::SharingMode::eExclusive;
    }
    else
    {
        readbackInfo.sharingMode = vk::SharingMode::eConcurrent;
        readbackInfo.queueFamilyIndexCount = static_cast<std::uint32_t>(readbackQueueFamilies.size());
        readbackInfo.pQueueFamilyIndices = readbackQueueFamilies.data();
    }
    readbackRing_ = resourceFactory.createBuffer(readbackInfo, MemoryUsage::GpuToCpu, "readback_ring");

    uploadTimeline_ = sync::createTimelineSemaphore(device, 0u);
    readbackTimeline_ = sync::createTimelineSemaphore(device, 0u);
}

UploadReadbackContext::~UploadReadbackContext()
{
    if (*uploadTimeline_ != nullptr && nextUploadSignalValue_ > 1u)
    {
        waitTimelineValue(uploadTimeline_, nextUploadSignalValue_ - 1u);
    }
    if (*readbackTimeline_ != nullptr && nextReadbackSignalValue_ > 1u)
    {
        waitTimelineValue(readbackTimeline_, nextReadbackSignalValue_ - 1u);
    }
}

[[nodiscard]] bool UploadReadbackContext::valid() const noexcept
{
    return uploadRing_.valid() && readbackRing_.valid() && transferPool_.valid() && graphicsReadbackPool_.valid() &&
           computeReadbackPool_.valid() && *uploadTimeline_ != nullptr && *readbackTimeline_ != nullptr;
}

[[nodiscard]] const vk::raii::Semaphore &UploadReadbackContext::uploadTimelineSemaphore() const noexcept
{
    return uploadTimeline_;
}

void UploadReadbackContext::reclaimCompletedUploads()
{
    reclaimQueue(uploadInFlight_, uploadReclaimCursor_, queryTimelineValue(uploadTimeline_));
}

void UploadReadbackContext::waitUploadComplete(std::uint64_t signalValue)
{
    if (signalValue == 0)
    {
        signalValue = nextUploadSignalValue_ > 1 ? (nextUploadSignalValue_ - 1) : 0;
    }

    if (signalValue == 0)
    {
        return;
    }

    waitTimelineValue(uploadTimeline_, signalValue);
    reclaimQueue(uploadInFlight_, uploadReclaimCursor_, queryTimelineValue(uploadTimeline_));
}

void UploadReadbackContext::recordAcquireBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                                 const BufferUploadTicket &ticket) const
{
    nrAssert(ticket.valid(), "UploadReadbackContext::recordAcquireBarrier requires a valid ticket.");
    if (!ticket.ownership.has_value())
    {
        return;
    }

    BarrierBatch acquireBarriers{};
    acquireBarriers.add(makeBufferOwnershipTransferBarrier<OwnershipBarrierPhase::Acquire>(
        ticket.buffer->get(), *ticket.ownership, ticket.dstOffset, ticket.size));
    pipelineBarrier(commandBuffer, acquireBarriers);
}

void UploadReadbackContext::recordImageAcquireBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                                      const ImageUploadTicket &ticket) const
{
    nrAssert(ticket.valid(), "UploadReadbackContext::recordImageAcquireBarrier requires a valid ticket.");
    if (!ticket.ownership.has_value())
    {
        return;
    }

    BarrierBatch acquireBarriers{};
    acquireBarriers.add(makeImageOwnershipTransferBarrier<OwnershipBarrierPhase::Acquire>(
        ticket.image->get(), vk::ImageLayout::eTransferDstOptimal, ticket.layout, *ticket.ownership));
    pipelineBarrier(commandBuffer, acquireBarriers);
}

[[nodiscard]] BufferUploadTicket UploadReadbackContext::uploadBuffer(std::span<const std::byte> data, const Buffer &dst,
                                                                     vk::DeviceSize dstOffset,
                                                                     const BufferUploadOwnershipPlan &ownership)
{
    nrAssert(valid(), "UploadReadbackContext::uploadBuffer requires a valid context.");
    nrAssert(!data.empty(), "UploadReadbackContext::uploadBuffer requires non-empty data.");

    const auto transferQueueFamilyIndex = queueManager_->get().transfer().queueFamilyIndex();
    nrAssert(ownership.valid(transferQueueFamilyIndex),
             "UploadReadbackContext::uploadBuffer requires a valid ownership plan for the transfer queue.");
    nrAssert(dst.sharingMode() == vk::SharingMode::eExclusive,
             "UploadReadbackContext::uploadBuffer with ownership requires an exclusive-sharing buffer.");
    const auto omitReleaseOwnership = queueFamilyTransferPolicy_.canOmitBufferQueueFamilyTransfer(
        ownership.releaseToDestination.srcQueueFamilyIndex, ownership.releaseToDestination.dstQueueFamilyIndex);
    return uploadBufferCore(data, dst, dstOffset,
                            BufferUploadControl{
                                .ownership = std::cref(ownership),
                                .omitReleaseOwnership = omitReleaseOwnership,
                                .omitAcquireToTransferOwnership =
                                    ownership.acquireToTransfer.has_value() &&
                                    queueFamilyTransferPolicy_.canOmitBufferQueueFamilyTransfer(
                                        ownership.acquireToTransfer->srcQueueFamilyIndex,
                                        ownership.acquireToTransfer->dstQueueFamilyIndex),
                                .ticketCarriesOwnership = !ownership.isSameQueueFamily() && !omitReleaseOwnership,
                            });
}

[[nodiscard]] BufferUploadTicket UploadReadbackContext::uploadBufferCore(std::span<const std::byte> data,
                                                                         const Buffer &dst,
                                                                         vk::DeviceSize dstOffset,
                                                                         const BufferUploadControl &control)
{
    nrAssert(dst.valid(), "UploadReadbackContext::uploadBuffer requires a valid destination buffer.");
    nrAssert(std::in_range<vk::DeviceSize>(data.size_bytes()),
             "UploadReadbackContext::uploadBuffer payload exceeds the Vulkan device-size range.");
    const auto totalSize = static_cast<vk::DeviceSize>(data.size_bytes());
    nrAssert(dstOffset <= dst.size() && totalSize <= dst.size() - dstOffset,
             "UploadReadbackContext::uploadBuffer destination range exceeds buffer size.");
    nrAssert((dst.usage() & vk::BufferUsageFlagBits::eTransferDst) != vk::BufferUsageFlags{},
             "UploadReadbackContext::uploadBuffer destination buffer must include eTransferDst usage.");
    nrAssert(uploadRing_.mapped() != nullptr,
             "UploadReadbackContext::uploadBuffer requires a persistently mapped upload ring.");

    auto remainingSize = totalSize;
    auto uploadedSize = vk::DeviceSize{0};
    auto lastSignalValue = std::uint64_t{0};

    while (remainingSize > 0)
    {
        const auto chunkSize = std::min(remainingSize, uploadCapacity_);
        auto allocation = reserveUpload(chunkSize);

        uploadRing_.writeMappedAndFlush(
            std::span<const std::byte>{
                data.data() + static_cast<std::size_t>(uploadedSize),
                static_cast<std::size_t>(chunkSize),
            },
            allocation.offset);

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto &commandBuffer = commandBuffers.front();

        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            if (uploadedSize == 0 && control.ownership.has_value() &&
                control.ownership->get().acquireToTransfer.has_value() && !control.omitAcquireToTransferOwnership)
            {
                BarrierBatch transferAcquireBarrier{};
                transferAcquireBarrier.add(makeBufferOwnershipTransferBarrier<OwnershipBarrierPhase::Acquire>(
                    dst, *control.ownership->get().acquireToTransfer, dstOffset, totalSize));
                pipelineBarrier(commandBuffer, transferAcquireBarrier);
            }

            // The Vulkan submission itself performs the required host->device
            // domain operation after flush; no extra buffer barrier is needed here.
            vk::BufferCopy copyRegion{};
            copyRegion.srcOffset = allocation.offset;
            copyRegion.dstOffset = dstOffset + uploadedSize;
            copyRegion.size = chunkSize;
            copyBuffer2(commandBuffer, uploadRing_.handle(), dst.handle(), toBufferCopy2(copyRegion));

            if (remainingSize == chunkSize)
            {
                BarrierBatch releaseBarrier{};
                if (control.ownership.has_value() && control.ownership->get().isSameQueueFamily())
                {
                    const auto &releaseToDestination = control.ownership->get().releaseToDestination;
                    releaseBarrier.add(makeBufferBarrier(dst, vk::BufferMemoryBarrier2{
                                                                  releaseToDestination.release.stages,
                                                                  releaseToDestination.release.access,
                                                                  releaseToDestination.acquire.stages,
                                                                  releaseToDestination.acquire.access,
                                                                  kIgnoredQueueFamilyIndex,
                                                                  kIgnoredQueueFamilyIndex,
                                                                  vk::Buffer{},
                                                                  dstOffset,
                                                                  totalSize,
                                                                  nullptr,
                                                              }));
                }
                else if (control.ownership.has_value() && !control.omitReleaseOwnership)
                {
                    releaseBarrier.add(makeBufferOwnershipTransferBarrier<OwnershipBarrierPhase::Release>(
                        dst, control.ownership->get().releaseToDestination, dstOffset, totalSize));
                }
                if (!releaseBarrier.empty())
                {
                    pipelineBarrier(commandBuffer, releaseBarrier);
                }
            }
        }
        CommandRecorder::end(commandBuffer);

        auto acquireToTransferWait = std::optional<std::reference_wrapper<const QueueOwnershipTransfer>>{};
        if (uploadedSize == 0 && control.ownership.has_value() &&
            control.ownership->get().acquireToTransfer.has_value())
        {
            acquireToTransferWait = std::cref(*control.ownership->get().acquireToTransfer);
        }

        const auto signalValue =
            submitUploadCommandBuffers(std::move(commandBuffers), allocation, acquireToTransferWait);

        lastSignalValue = signalValue;
        uploadedSize += chunkSize;
        remainingSize -= chunkSize;
    }

    return BufferUploadTicket{
        .buffer = std::cref(dst),
        .dstOffset = dstOffset,
        .size = totalSize,
        .signalValue = lastSignalValue,
        .ownership = control.ticketCarriesOwnership
                         ? std::optional<QueueOwnershipTransfer>{control.ownership->get().releaseToDestination}
                         : std::optional<QueueOwnershipTransfer>{},
    };
}

[[nodiscard]] BufferUploadTicket UploadReadbackContext::uploadBuffer(std::span<const std::byte> data, const Buffer &dst,
                                                                     vk::DeviceSize dstOffset)
{
    nrAssert(valid(), "UploadReadbackContext::uploadBuffer requires a valid context.");
    nrAssert(!data.empty(), "UploadReadbackContext::uploadBuffer requires non-empty data.");
    nrAssert(dst.sharingMode() == vk::SharingMode::eConcurrent,
             "UploadReadbackContext::uploadBuffer without ownership is only valid for concurrent-sharing buffers.");
    return uploadBufferCore(data, dst, dstOffset, BufferUploadControl{});
}

[[nodiscard]] ImageUploadTicket UploadReadbackContext::uploadImage(std::span<const std::byte> data, const Image &dst,
                                                                   vk::ImageLayout sourceLayout,
                                                                   vk::ImageLayout destinationLayout,
                                                                   const BufferUploadOwnershipPlan &ownership,
                                                                   const vk::BufferImageCopy &region)
{
    nrAssert(valid(), "UploadReadbackContext::uploadImage requires a valid context.");
    nrAssert(!data.empty(), "UploadReadbackContext::uploadImage requires non-empty data.");
    nrAssert(destinationLayout != vk::ImageLayout::eUndefined,
             "UploadReadbackContext::uploadImage requires a valid destination layout.");

    const auto transferQueueFamilyIndex = queueManager_->get().transfer().queueFamilyIndex();
    nrAssert(ownership.valid(transferQueueFamilyIndex),
             "UploadReadbackContext::uploadImage requires a valid ownership plan for the transfer queue.");
    nrAssert(dst.sharingMode() == vk::SharingMode::eExclusive,
             "UploadReadbackContext::uploadImage with ownership requires an exclusive-sharing image.");
    const auto omitAcquireToTransferOwnership =
        ownership.acquireToTransfer.has_value() &&
        queueFamilyTransferPolicy_.canOmitImageQueueFamilyTransfer(
            dst, ownership.acquireToTransfer->srcQueueFamilyIndex, ownership.acquireToTransfer->dstQueueFamilyIndex);
    const auto omitReleaseOwnership = queueFamilyTransferPolicy_.canOmitImageQueueFamilyTransfer(
        dst, ownership.releaseToDestination.srcQueueFamilyIndex, ownership.releaseToDestination.dstQueueFamilyIndex);
    const auto ticketCarriesOwnership = !ownership.isSameQueueFamily() && !omitReleaseOwnership;
    nrAssert((dst.usage() & vk::ImageUsageFlagBits::eTransferDst) != vk::ImageUsageFlags{},
             "UploadReadbackContext::uploadImage destination image must include eTransferDst usage.");
    nrAssert(uploadRing_.mapped() != nullptr,
             "UploadReadbackContext::uploadImage requires a persistently mapped upload ring.");

    auto effectiveRegion = normalizeBufferImageCopyRegion(dst, region);
    const auto payloadSize = linearBufferImageCopySize(effectiveRegion, bytesPerPixel(dst.format()));

    nrAssert(std::in_range<vk::DeviceSize>(data.size_bytes()),
             "UploadReadbackContext::uploadImage payload exceeds the Vulkan device-size range.");
    nrAssert(static_cast<vk::DeviceSize>(data.size_bytes()) == payloadSize,
             std::format("UploadReadbackContext::uploadImage payload size mismatch: data={} bytes, expected={} bytes.",
                         static_cast<vk::DeviceSize>(data.size_bytes()), payloadSize));
    auto const chunks = planLinearImageUploadChunks(effectiveRegion, bytesPerPixel(dst.format()), uploadCapacity_);
    nrAssert(!chunks.empty(), "UploadReadbackContext::uploadImage requires at least one upload chunk.");

    auto signalValue = std::uint64_t{0u};
    auto const chunkIndices = std::views::iota(std::size_t{0u}, chunks.size());
    std::ranges::for_each(chunkIndices, [&](std::size_t chunkIndex) {
        auto const &chunk = chunks[chunkIndex];
        nrAssert(chunk.sourceOffset <= payloadSize && chunk.byteSize <= payloadSize - chunk.sourceOffset,
                 "UploadReadbackContext::uploadImage chunk exceeds the source payload.");

        auto allocation = reserveUpload(chunk.byteSize);
        uploadRing_.writeMappedAndFlush(
            data.subspan(static_cast<std::size_t>(chunk.sourceOffset), static_cast<std::size_t>(chunk.byteSize)),
            allocation.offset);

        auto commandBuffers = transferPool_.allocatePrimary(1);
        auto &commandBuffer = commandBuffers.front();
        CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);

        auto const isFirstChunk = chunkIndex == 0u;
        auto const isLastChunk = chunkIndex + 1u == chunks.size();
        if (isFirstChunk)
        {
            if (ownership.acquireToTransfer.has_value() && !omitAcquireToTransferOwnership)
            {
                BarrierBatch transferAcquireBarrier{};
                transferAcquireBarrier.add(makeImageOwnershipTransferBarrier<OwnershipBarrierPhase::Acquire>(
                    dst, sourceLayout, vk::ImageLayout::eTransferDstOptimal, *ownership.acquireToTransfer));
                pipelineBarrier(commandBuffer, transferAcquireBarrier);
            }
            else
            {
                BarrierBatch toTransferDstBarrier{};
                toTransferDstBarrier.add(makeImageBarrier(dst, vk::ImageMemoryBarrier2{
                                                                   vk::PipelineStageFlagBits2::eTopOfPipe,
                                                                   vk::AccessFlags2{},
                                                                   vk::PipelineStageFlagBits2::eTransfer,
                                                                   vk::AccessFlagBits2::eTransferWrite,
                                                                   sourceLayout,
                                                                   vk::ImageLayout::eTransferDstOptimal,
                                                                   kIgnoredQueueFamilyIndex,
                                                                   kIgnoredQueueFamilyIndex,
                                                                   vk::Image{},
                                                                   {},
                                                                   nullptr,
                                                               }));
                pipelineBarrier(commandBuffer, toTransferDstBarrier);
            }
        }

        auto copyRegion = chunk.copyRegion;
        copyRegion.bufferOffset = allocation.offset;
        copyBufferToImage2(commandBuffer, uploadRing_.handle(), dst.handle(), vk::ImageLayout::eTransferDstOptimal,
                           toBufferImageCopy2(copyRegion));

        if (isLastChunk)
        {
            BarrierBatch releaseBarrier{};
            if (ownership.isSameQueueFamily())
            {
                releaseBarrier.add(makeImageBarrier(dst, vk::ImageMemoryBarrier2{
                                                             ownership.releaseToDestination.release.stages,
                                                             ownership.releaseToDestination.release.access,
                                                             ownership.releaseToDestination.acquire.stages,
                                                             ownership.releaseToDestination.acquire.access,
                                                             vk::ImageLayout::eTransferDstOptimal,
                                                             destinationLayout,
                                                             kIgnoredQueueFamilyIndex,
                                                             kIgnoredQueueFamilyIndex,
                                                             vk::Image{},
                                                             {},
                                                             nullptr,
                                                         }));
            }
            else if (omitReleaseOwnership)
            {
                releaseBarrier.add(makeImageBarrier(dst, vk::ImageMemoryBarrier2{
                                                             ownership.releaseToDestination.release.stages,
                                                             ownership.releaseToDestination.release.access,
                                                             vk::PipelineStageFlagBits2::eTransfer,
                                                             vk::AccessFlags2{},
                                                             vk::ImageLayout::eTransferDstOptimal,
                                                             destinationLayout,
                                                             kIgnoredQueueFamilyIndex,
                                                             kIgnoredQueueFamilyIndex,
                                                             vk::Image{},
                                                             {},
                                                             nullptr,
                                                         }));
            }
            else
            {
                releaseBarrier.add(makeImageOwnershipTransferBarrier<OwnershipBarrierPhase::Release>(
                    dst, vk::ImageLayout::eTransferDstOptimal, destinationLayout, ownership.releaseToDestination));
            }
            pipelineBarrier(commandBuffer, releaseBarrier);
        }
        CommandRecorder::end(commandBuffer);

        auto acquireToTransferWait = std::optional<std::reference_wrapper<const QueueOwnershipTransfer>>{};
        if (isFirstChunk && ownership.acquireToTransfer.has_value())
        {
            acquireToTransferWait = std::cref(*ownership.acquireToTransfer);
        }
        signalValue = submitUploadCommandBuffers(std::move(commandBuffers), allocation, acquireToTransferWait);
    });

    return ImageUploadTicket{
        .image = std::cref(dst),
        .layout = destinationLayout,
        .signalValue = signalValue,
        .ownership = ticketCarriesOwnership ? std::optional<QueueOwnershipTransfer>{ownership.releaseToDestination}
                                            : std::optional<QueueOwnershipTransfer>{},
    };
}

[[nodiscard]] ReadbackTicket UploadReadbackContext::readbackBuffer(const Buffer &src, vk::DeviceSize srcOffset,
                                                                   vk::DeviceSize size, QueueRole queueRole,
                                                                   const ReadbackSyncPlan &syncPlan)
{
    nrAssert(valid(), "UploadReadbackContext::readbackBuffer requires a valid context.");
    nrAssert(isReadbackQueueRoleSupported(queueRole),
             "UploadReadbackContext::readbackBuffer supports only graphics/compute queue roles.");
    nrAssert(size > 0, "UploadReadbackContext::readbackBuffer requires size > 0.");
    nrAssert(syncPlan.valid(), "UploadReadbackContext::readbackBuffer requires non-empty pre/post stage masks.");
    nrAssert(src.valid(), "UploadReadbackContext::readbackBuffer requires a valid source buffer.");
    nrAssert(srcOffset <= src.size() && size <= src.size() - srcOffset,
             "UploadReadbackContext::readbackBuffer source range exceeds buffer size.");
    nrAssert((src.usage() & vk::BufferUsageFlagBits::eTransferSrc) != vk::BufferUsageFlags{},
             "UploadReadbackContext::readbackBuffer source buffer must include eTransferSrc usage.");
    nrAssert(readbackRing_.mapped() != nullptr,
             "UploadReadbackContext::readbackBuffer requires a persistently mapped readback ring.");

    auto route = readbackRouteFor(queueRole);
    auto &readbackQueue = route.queue.get();
    auto &readbackPool = route.pool.get();

    auto allocation = reserveReadback(size);

    auto commandBuffers = readbackPool.allocatePrimary(1);
    auto &commandBuffer = commandBuffers.front();
    CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        BarrierBatch preCopyBarrier{};
        preCopyBarrier.add(makeBufferBarrier(src, vk::BufferMemoryBarrier2{
                                                      syncPlan.preCopy.stages,
                                                      syncPlan.preCopy.access,
                                                      vk::PipelineStageFlagBits2::eTransfer,
                                                      vk::AccessFlagBits2::eTransferRead,
                                                      kIgnoredQueueFamilyIndex,
                                                      kIgnoredQueueFamilyIndex,
                                                      vk::Buffer{},
                                                      srcOffset,
                                                      size,
                                                      nullptr,
                                                  }));
        pipelineBarrier(commandBuffer, preCopyBarrier);

        recordReadbackRingToTransferWriteBarrier(commandBuffer, allocation.offset, size);

        vk::BufferCopy copyRegion{};
        copyRegion.srcOffset = srcOffset;
        copyRegion.dstOffset = allocation.offset;
        copyRegion.size = size;
        copyBuffer2(commandBuffer, src.handle(), readbackRing_.handle(), toBufferCopy2(copyRegion));

        recordReadbackRingHostVisibilityBarrier(commandBuffer, allocation.offset, size);

        BarrierBatch postCopyBarrier{};
        postCopyBarrier.add(makeBufferBarrier(src, vk::BufferMemoryBarrier2{
                                                       vk::PipelineStageFlagBits2::eTransfer,
                                                       vk::AccessFlagBits2::eTransferRead,
                                                       syncPlan.postCopy.stages,
                                                       syncPlan.postCopy.access,
                                                       kIgnoredQueueFamilyIndex,
                                                       kIgnoredQueueFamilyIndex,
                                                       vk::Buffer{},
                                                       srcOffset,
                                                       size,
                                                       nullptr,
                                                   }));
        pipelineBarrier(commandBuffer, postCopyBarrier);
    }
    CommandRecorder::end(commandBuffer);

    const auto signalValue = submitReadbackCommandBuffers(readbackQueue, std::move(commandBuffers), allocation);
    return ReadbackTicket{*this, allocation.offset, size, signalValue};
}

[[nodiscard]] ReadbackTicket UploadReadbackContext::readbackImage(const Image &src, vk::ImageLayout sourceLayout,
                                                                  QueueRole queueRole, const ReadbackSyncPlan &syncPlan,
                                                                  const vk::BufferImageCopy &region)
{
    nrAssert(valid(), "UploadReadbackContext::readbackImage requires a valid context.");
    nrAssert(isReadbackQueueRoleSupported(queueRole),
             "UploadReadbackContext::readbackImage supports only graphics/compute queue roles.");
    nrAssert(sourceLayout != vk::ImageLayout::eUndefined,
             "UploadReadbackContext::readbackImage requires a defined source layout.");
    nrAssert(syncPlan.valid(), "UploadReadbackContext::readbackImage requires non-empty pre/post stage masks.");
    nrAssert((src.usage() & vk::ImageUsageFlagBits::eTransferSrc) != vk::ImageUsageFlags{},
             "UploadReadbackContext::readbackImage source image must include eTransferSrc usage.");
    nrAssert(readbackRing_.mapped() != nullptr,
             "UploadReadbackContext::readbackImage requires a persistently mapped readback ring.");

    auto route = readbackRouteFor(queueRole);
    auto &readbackQueue = route.queue.get();
    auto &readbackPool = route.pool.get();

    auto effectiveRegion = normalizeBufferImageCopyRegion(src, region);
    nrAssert(effectiveRegion.bufferRowLength == 0u && effectiveRegion.bufferImageHeight == 0u,
             "UploadReadbackContext::readbackImage supports only tightly packed readback regions.");
    const auto readbackSize = linearBufferImageCopySize(effectiveRegion, bytesPerPixel(src.format()));
    auto allocation = reserveReadback(readbackSize);

    const auto &layers = effectiveRegion.imageSubresource;
    auto subresourceRange = vk::ImageSubresourceRange{
        layers.aspectMask, layers.mipLevel, 1u, layers.baseArrayLayer, layers.layerCount,
    };

    auto commandBuffers = readbackPool.allocatePrimary(1);
    auto &commandBuffer = commandBuffers.front();
    CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        BarrierBatch preCopyBarrier{};
        preCopyBarrier.add(makeImageBarrier(src, vk::ImageMemoryBarrier2{
                                                     syncPlan.preCopy.stages,
                                                     syncPlan.preCopy.access,
                                                     vk::PipelineStageFlagBits2::eTransfer,
                                                     vk::AccessFlagBits2::eTransferRead,
                                                     sourceLayout,
                                                     vk::ImageLayout::eTransferSrcOptimal,
                                                     kIgnoredQueueFamilyIndex,
                                                     kIgnoredQueueFamilyIndex,
                                                     vk::Image{},
                                                     subresourceRange,
                                                     nullptr,
                                                 }));
        pipelineBarrier(commandBuffer, preCopyBarrier);

        recordReadbackRingToTransferWriteBarrier(commandBuffer, allocation.offset, readbackSize);

        effectiveRegion.bufferOffset = allocation.offset;
        copyImageToBuffer2(commandBuffer, src.handle(), vk::ImageLayout::eTransferSrcOptimal, readbackRing_.handle(),
                           toBufferImageCopy2(effectiveRegion));

        recordReadbackRingHostVisibilityBarrier(commandBuffer, allocation.offset, readbackSize);

        BarrierBatch restoreBarrier{};
        restoreBarrier.add(makeImageBarrier(src, vk::ImageMemoryBarrier2{
                                                     vk::PipelineStageFlagBits2::eTransfer,
                                                     vk::AccessFlagBits2::eTransferRead,
                                                     syncPlan.postCopy.stages,
                                                     syncPlan.postCopy.access,
                                                     vk::ImageLayout::eTransferSrcOptimal,
                                                     sourceLayout,
                                                     kIgnoredQueueFamilyIndex,
                                                     kIgnoredQueueFamilyIndex,
                                                     vk::Image{},
                                                     subresourceRange,
                                                     nullptr,
                                                 }));
        pipelineBarrier(commandBuffer, restoreBarrier);
    }
    CommandRecorder::end(commandBuffer);

    const auto signalValue = submitReadbackCommandBuffers(readbackQueue, std::move(commandBuffers), allocation);
    return ReadbackTicket{*this, allocation.offset, readbackSize, signalValue};
}

[[nodiscard]] std::vector<std::byte> UploadReadbackContext::readbackBytes(ReadbackTicket &ticket)
{
    nrAssert(ticket.context_.has_value() && std::addressof(ticket.context_->get()) == this,
             "UploadReadbackContext::readbackBytes requires a ticket issued by this context.");
    nrAssert(ticket.signalValue_ > 0, "UploadReadbackContext::readbackBytes requires a live ticket.");
    nrAssert(ticket.offset_ <= readbackCapacity_ && ticket.size_ <= readbackCapacity_ - ticket.offset_,
             "UploadReadbackContext::readbackBytes ticket range exceeds the readback ring.");
    nrAssert(std::in_range<std::size_t>(ticket.offset_) && std::in_range<std::size_t>(ticket.size_),
             "UploadReadbackContext::readbackBytes ticket range exceeds the host address range.");

    auto spilled = spilledReadbacks_.find(ticket.signalValue_);
    if (spilled != spilledReadbacks_.end())
    {
        nrAssert(spilled->second.size() == static_cast<std::size_t>(ticket.size_),
                 "UploadReadbackContext::readbackBytes spilled ticket size mismatch.");
        auto data = std::move(spilled->second);
        spilledReadbacks_.erase(spilled);
        ticket.context_.reset();
        ticket.signalValue_ = 0;
        return data;
    }

    auto batch = std::ranges::find(readbackInFlight_, ticket.signalValue_, &InFlightBatch::signalValue);
    nrAssert(batch != readbackInFlight_.end(),
             "UploadReadbackContext::readbackBytes ticket is stale or has already been consumed.");
    nrAssert(!batch->ringReclaimable && batch->offset == ticket.offset_ && batch->size == ticket.size_,
             "UploadReadbackContext::readbackBytes ticket does not match its issued ring allocation.");

    waitTimelineValue(readbackTimeline_, ticket.signalValue_);
    readbackRing_.invalidate(ticket.offset_, ticket.size_);

    std::vector<std::byte> data(static_cast<std::size_t>(ticket.size_));
    auto *src = static_cast<const std::byte *>(readbackRing_.mapped()) + static_cast<std::size_t>(ticket.offset_);
    std::memcpy(data.data(), src, data.size());

    batch->ringReclaimable = true;
    ticket.context_.reset();
    ticket.signalValue_ = 0;
    reclaimQueue(readbackInFlight_, readbackReclaimCursor_, queryTimelineValue(readbackTimeline_));
    return data;
}

[[nodiscard]] std::uint64_t UploadReadbackContext::consumeNextUploadSignalValue()
{
    auto value = nextUploadSignalValue_;
    nrAssert(value != std::numeric_limits<std::uint64_t>::max(),
             "UploadReadbackContext timeline signal value overflow.");
    ++nextUploadSignalValue_;
    return value;
}

[[nodiscard]] std::uint64_t UploadReadbackContext::consumeNextReadbackSignalValue()
{
    auto value = nextReadbackSignalValue_;
    nrAssert(value != std::numeric_limits<std::uint64_t>::max(),
             "UploadReadbackContext readback timeline signal value overflow.");
    ++nextReadbackSignalValue_;
    return value;
}

[[nodiscard]] UploadReadbackContext::ReadbackRoute UploadReadbackContext::readbackRouteFor(QueueRole queueRole)
{
    nrAssert(isReadbackQueueRoleSupported(queueRole),
             "UploadReadbackContext::readbackRouteFor supports only graphics/compute queue roles.");

    if (queueRole == QueueRole::Graphics)
    {
        return ReadbackRoute{std::ref(queueManager_->get().graphics()), std::ref(graphicsReadbackPool_)};
    }

    return ReadbackRoute{std::ref(queueManager_->get().compute()), std::ref(computeReadbackPool_)};
}

[[nodiscard]] std::uint64_t UploadReadbackContext::submitUploadCommandBuffers(
    vk::raii::CommandBuffers commandBuffers, const RingAllocation &allocation,
    std::optional<std::reference_wrapper<const QueueOwnershipTransfer>> acquireToTransferWait)
{
    const auto &commandBuffer = commandBuffers.front();
    const auto signalValue = consumeNextUploadSignalValue();

    CommandBatch batch{};
    if (acquireToTransferWait.has_value())
    {
        const auto &wait = acquireToTransferWait->get();
        batch.addWait(wait.waitSemaphore, wait.acquire.stages, wait.waitValue);
    }
    batch.addCommandBuffer(commandBuffer);
    batch.addSignal(uploadTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eAllCommands);
    queueManager_->get().transfer().submit(std::move(batch));

    addInFlight(uploadInFlight_, allocation, signalValue, true, std::move(commandBuffers));
    return signalValue;
}

[[nodiscard]] std::uint64_t UploadReadbackContext::submitReadbackCommandBuffers(GpuQueue &readbackQueue,
                                                                                vk::raii::CommandBuffers commandBuffers,
                                                                                const RingAllocation &allocation)
{
    const auto &commandBuffer = commandBuffers.front();
    const auto signalValue = consumeNextReadbackSignalValue();

    CommandBatch batch{};
    if (signalValue > 1)
    {
        batch.addWait(readbackTimeline_, vk::PipelineStageFlagBits2::eTransfer, signalValue - 1);
    }
    batch.addCommandBuffer(commandBuffer);
    batch.addSignal(readbackTimeline_, signalValue, 0, vk::PipelineStageFlagBits2::eAllCommands);
    readbackQueue.submit(std::move(batch));

    addInFlight(readbackInFlight_, allocation, signalValue, false, std::move(commandBuffers));
    return signalValue;
}

[[nodiscard]] std::vector<std::uint32_t> UploadReadbackContext::uniqueReadbackQueueFamilies(
    std::array<std::uint32_t, 2> queueFamilies)
{
    std::ranges::sort(queueFamilies);
    auto uniqueRange = std::ranges::unique(queueFamilies);
    return std::vector<std::uint32_t>(queueFamilies.begin(), uniqueRange.begin());
}

[[nodiscard]] bool UploadReadbackContext::isReadbackQueueRoleSupported(QueueRole queueRole) noexcept
{
    return queueRole == QueueRole::Graphics || queueRole == QueueRole::Compute;
}

void UploadReadbackContext::recordReadbackRingToTransferWriteBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                                                     vk::DeviceSize offset, vk::DeviceSize size) const
{
    BarrierBatch ringBarrier{};
    ringBarrier.add(makeBufferBarrier(readbackRing_, vk::BufferMemoryBarrier2{
                                                         vk::PipelineStageFlagBits2::eHost,
                                                         vk::AccessFlagBits2::eHostRead,
                                                         vk::PipelineStageFlagBits2::eTransfer,
                                                         vk::AccessFlagBits2::eTransferWrite,
                                                         kIgnoredQueueFamilyIndex,
                                                         kIgnoredQueueFamilyIndex,
                                                         vk::Buffer{},
                                                         offset,
                                                         size,
                                                         nullptr,
                                                     }));
    pipelineBarrier(commandBuffer, ringBarrier);
}

void UploadReadbackContext::recordReadbackRingHostVisibilityBarrier(const vk::raii::CommandBuffer &commandBuffer,
                                                                    vk::DeviceSize offset, vk::DeviceSize size) const
{
    BarrierBatch ringBarrier{};
    ringBarrier.add(makeBufferBarrier(readbackRing_, vk::BufferMemoryBarrier2{
                                                         vk::PipelineStageFlagBits2::eTransfer,
                                                         vk::AccessFlagBits2::eTransferWrite,
                                                         vk::PipelineStageFlagBits2::eHost,
                                                         vk::AccessFlagBits2::eHostRead,
                                                         kIgnoredQueueFamilyIndex,
                                                         kIgnoredQueueFamilyIndex,
                                                         vk::Buffer{},
                                                         offset,
                                                         size,
                                                         nullptr,
                                                     }));
    pipelineBarrier(commandBuffer, ringBarrier);
}

[[nodiscard]] std::uint64_t UploadReadbackContext::alignUp(std::uint64_t value, std::uint64_t alignment)
{
    if (alignment <= 1)
        return value;
    nrAssert((alignment & (alignment - 1)) == 0,
             std::format("UploadReadbackContext::alignUp requires power-of-2 alignment, got {}", alignment));
    return checkedAdd(value, alignment - 1u, "UploadReadbackContext ring alignment") & ~(alignment - 1u);
}

[[nodiscard]] vk::DeviceSize UploadReadbackContext::bytesPerPixel(vk::Format format)
{
    switch (format)
    {
    case vk::Format::eR8Unorm:
    case vk::Format::eR8Snorm:
    case vk::Format::eR8Uint:
    case vk::Format::eR8Sint:
        return 1;
    case vk::Format::eR8G8Unorm:
    case vk::Format::eR8G8Snorm:
    case vk::Format::eR8G8Uint:
    case vk::Format::eR8G8Sint:
    case vk::Format::eR16Sfloat:
    case vk::Format::eR16Uint:
    case vk::Format::eR16Sint:
        return 2;
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eB8G8R8A8Srgb:
    case vk::Format::eR16G16Sfloat:
    case vk::Format::eR16G16Uint:
    case vk::Format::eR16G16Sint:
    case vk::Format::eR32Sfloat:
    case vk::Format::eR32Uint:
    case vk::Format::eR32Sint:
        return 4;
    case vk::Format::eR16G16B16A16Sfloat:
    case vk::Format::eR16G16B16A16Uint:
    case vk::Format::eR16G16B16A16Sint:
    case vk::Format::eR32G32Sfloat:
    case vk::Format::eR32G32Uint:
    case vk::Format::eR32G32Sint:
        return 8;
    case vk::Format::eR32G32B32A32Sfloat:
    case vk::Format::eR32G32B32A32Uint:
    case vk::Format::eR32G32B32A32Sint:
        return 16;
    default:
        nrAssert(false,
                 std::format("UploadReadbackContext::readbackImage unsupported format for readback size estimation: {}",
                             vk::to_string(format)));
        return 0;
    }
}

[[nodiscard]] std::uint64_t UploadReadbackContext::queryTimelineValue(
    const vk::raii::Semaphore &timelineSemaphore) const
{
    return sync::timelineValue(timelineSemaphore);
}

void UploadReadbackContext::waitTimelineValue(const vk::raii::Semaphore &timelineSemaphore,
                                              std::uint64_t targetValue) const
{
    auto ok = sync::waitTimeline(device_->get(), timelineSemaphore, targetValue);
    nrAssert(ok, "UploadReadbackContext::waitTimelineValue failed while waiting timeline semaphore.");
}

void UploadReadbackContext::reclaimQueue(std::deque<InFlightBatch> &queue, std::uint64_t &reclaimCursor,
                                         std::uint64_t completedValue)
{
    while (!queue.empty() && queue.front().ringReclaimable && queue.front().signalValue <= completedValue)
    {
        reclaimCursor = queue.front().end;
        queue.pop_front();
    }
}

[[nodiscard]] UploadReadbackContext::RingAllocation UploadReadbackContext::reserveUpload(vk::DeviceSize size)
{
    return reserveRing(size, uploadCapacity_, uploadWriteCursor_, uploadReclaimCursor_, uploadInFlight_,
                       uploadTimeline_, false);
}

void UploadReadbackContext::spillReadback(InFlightBatch &batch)
{
    nrAssert(!batch.ringReclaimable, "UploadReadbackContext cannot spill an already reclaimable readback.");
    nrAssert(batch.offset <= readbackCapacity_ && batch.size <= readbackCapacity_ - batch.offset,
             "UploadReadbackContext spill range exceeds the readback ring.");
    nrAssert(std::in_range<std::size_t>(batch.offset) && std::in_range<std::size_t>(batch.size),
             "UploadReadbackContext spill range exceeds the host address range.");

    readbackRing_.invalidate(batch.offset, batch.size);
    auto bytes = std::vector<std::byte>(static_cast<std::size_t>(batch.size));
    auto *source = static_cast<const std::byte *>(readbackRing_.mapped()) + static_cast<std::size_t>(batch.offset);
    std::memcpy(bytes.data(), source, bytes.size());
    auto [_, inserted] = spilledReadbacks_.emplace(batch.signalValue, std::move(bytes));
    nrAssert(inserted, "UploadReadbackContext readback signal was spilled more than once.");
    batch.ringReclaimable = true;
}

[[nodiscard]] UploadReadbackContext::RingAllocation UploadReadbackContext::reserveReadback(vk::DeviceSize size)
{
    return reserveRing(size, readbackCapacity_, readbackWriteCursor_, readbackReclaimCursor_, readbackInFlight_,
                       readbackTimeline_, true);
}

[[nodiscard]] UploadReadbackContext::RingAllocation UploadReadbackContext::reserveRing(
    vk::DeviceSize size, vk::DeviceSize capacity, std::uint64_t &writeCursor, std::uint64_t &reclaimCursor,
    std::deque<InFlightBatch> &queue, const vk::raii::Semaphore &timelineSemaphore,
    bool requiresCpuConsumption)
{
    nrAssert(capacity > 0u, "UploadReadbackContext ring capacity must be non-zero.");
    nrAssert(size > 0u && size <= capacity,
             std::format("UploadReadbackContext ring allocation size ({} bytes) exceeds ring capacity ({} bytes). "
                         "Consider increasing the ring buffer size or using chunked uploads.",
                         size, capacity));

    reclaimQueue(queue, reclaimCursor, queryTimelineValue(timelineSemaphore));

    auto tryReserve = [&]() -> std::optional<RingAllocation> {
        std::uint64_t candidate = alignUp(writeCursor, kRingAllocationAlignment);
        std::uint64_t cap = static_cast<std::uint64_t>(capacity);

        auto ringOffset = static_cast<vk::DeviceSize>(candidate % cap);
        if (size > capacity - ringOffset)
        {
            candidate = alignUp(checkedAdd(candidate, cap - ringOffset, "UploadReadbackContext ring wrap"),
                                kRingAllocationAlignment);
            ringOffset = 0;
        }

        std::uint64_t end = checkedAdd(candidate, static_cast<std::uint64_t>(size),
                                       "UploadReadbackContext ring allocation end");
        nrAssert(end >= reclaimCursor, "UploadReadbackContext ring cursor ordering is invalid.");
        if (end - reclaimCursor > cap)
        {
            return std::nullopt;
        }
        return RingAllocation{candidate, end, ringOffset};
    };

    if (auto allocation = tryReserve())
    {
        writeCursor = allocation->end;
        return *allocation;
    }

    // Wait for pending transfers to complete and try to reclaim space
    while (!queue.empty())
    {
        waitTimelineValue(timelineSemaphore, queue.front().signalValue);
        if (requiresCpuConsumption && !queue.front().ringReclaimable)
        {
            spillReadback(queue.front());
        }
        reclaimQueue(queue, reclaimCursor, queryTimelineValue(timelineSemaphore));
        if (auto allocation = tryReserve())
        {
            writeCursor = allocation->end;
            return *allocation;
        }
    }

    // If queue is empty, reset cursors to start fresh from the beginning
    // This handles the edge case where cursors have drifted far apart
    if (queue.empty())
    {
        writeCursor = 0;
        reclaimCursor = 0;
        if (auto allocation = tryReserve())
        {
            writeCursor = allocation->end;
            return *allocation;
        }
    }

    nrAssert(false,
             std::format("UploadReadbackContext failed to reserve ring allocation of {} bytes with capacity {} bytes.",
                         size, capacity));
    return RingAllocation{};
}

void UploadReadbackContext::addInFlight(std::deque<InFlightBatch> &queue, const RingAllocation &allocation,
                                        std::uint64_t signalValue, bool ringReclaimable,
                                        vk::raii::CommandBuffers commandBuffers)
{
    if (!queue.empty())
    {
        nrAssert(signalValue > queue.back().signalValue, "Timeline signal values must be strictly increasing.");
    }
    queue.push_back(InFlightBatch{
        allocation.end,
        signalValue,
        allocation.offset,
        static_cast<vk::DeviceSize>(allocation.end - allocation.begin),
        ringReclaimable,
        std::move(commandBuffers),
    });
}
} // namespace nr::rhi::ops

namespace nr::rhi::ops
{
namespace detail
{
[[nodiscard]] vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingAttachmentDesc &desc)
{
    vk::RenderingAttachmentInfo info{};
    info.imageView = desc.imageView;
    info.imageLayout = desc.imageLayout;
    info.resolveMode = desc.resolveMode;
    info.resolveImageView = desc.resolveImageView;
    info.resolveImageLayout = desc.resolveImageLayout;
    info.loadOp = desc.loadOp;
    info.storeOp = desc.storeOp;
    info.clearValue = desc.clearValue;
    return info;
}

[[nodiscard]] vk::RenderingAttachmentInfo makeRenderingAttachmentInfo(const RenderingDepthStencilAttachmentDesc &desc)
{
    vk::RenderingAttachmentInfo info{};
    info.imageView = desc.imageView;
    info.imageLayout = desc.imageLayout;
    info.resolveMode = desc.resolveMode;
    info.resolveImageView = desc.resolveImageView;
    info.resolveImageLayout = desc.resolveImageLayout;
    info.loadOp = desc.depthLoadOp;
    info.storeOp = desc.depthStoreOp;
    info.clearValue = vk::ClearValue{desc.clearValue};
    return info;
}
} // namespace detail
} // namespace nr::rhi::ops
