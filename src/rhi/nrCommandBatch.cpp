module nr.rhi;
import :commandBatch;
import dependency.vulkan;
import std;

namespace nr::rhi
{
[[nodiscard]] const vk::SubmitInfo2& CommandBatch::SubmitInfo2Packet::info() const noexcept
{
            return submitInfo;
        }

void CommandBatch::addCommandBuffer(const vk::raii::CommandBuffer& commandBuffer)
{
        commandBuffers_.push_back(*commandBuffer);
    }

void CommandBatch::addCommandBuffers(std::span<const vk::raii::CommandBuffer> commandBuffers)
{
        commandBuffers_.reserve(commandBuffers_.size() + commandBuffers.size());
        std::ranges::for_each(commandBuffers, [&](const auto& cb) {
            commandBuffers_.push_back(*cb);
        });
    }

void CommandBatch::addWait(const vk::raii::Semaphore& semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value, std::uint32_t deviceIndex)
{
        waitPoints_.push_back(SemaphoreSyncPoint{
            .semaphore = *semaphore,
            .stageMask = stage,
            .value = value,
            .deviceIndex = deviceIndex,
        });
    }

void CommandBatch::addWait(vk::Semaphore semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value, std::uint32_t deviceIndex)
{
        waitPoints_.push_back(SemaphoreSyncPoint{
            .semaphore = semaphore,
            .stageMask = stage,
            .value = value,
            .deviceIndex = deviceIndex,
        });
    }

void CommandBatch::addSignal(const vk::raii::Semaphore& semaphore, std::uint64_t value, std::uint32_t deviceIndex, vk::PipelineStageFlags2 stage)
{
        signalPoints_.push_back(SemaphoreSyncPoint{
            .semaphore = *semaphore,
            .stageMask = stage,
            .value = value,
            .deviceIndex = deviceIndex,
        });
    }

void CommandBatch::addSignal(vk::Semaphore semaphore, std::uint64_t value, std::uint32_t deviceIndex, vk::PipelineStageFlags2 stage)
{
        signalPoints_.push_back(SemaphoreSyncPoint{
            .semaphore = semaphore,
            .stageMask = stage,
            .value = value,
            .deviceIndex = deviceIndex,
        });
    }

void CommandBatch::setFrameBoundary(
        std::uint64_t frameID,
        vk::FrameBoundaryFlagsEXT flags,
        std::span<const vk::Image> images,
        std::span<const vk::Buffer> buffers)
{
        frameBoundary_ = FrameBoundaryMetadata{
            .frameID = frameID,
            .flags = flags,
        };
        frameBoundary_->images.assign(images.begin(), images.end());
        frameBoundary_->buffers.assign(buffers.begin(), buffers.end());
    }

void CommandBatch::clearFrameBoundary() noexcept
{
        frameBoundary_.reset();
    }

[[nodiscard]] bool CommandBatch::hasFrameBoundary() const noexcept
{
        return frameBoundary_.has_value();
    }

[[nodiscard]] std::optional<std::uint64_t> CommandBatch::frameBoundaryFrameID() const noexcept
{
        if (!frameBoundary_.has_value())
        {
            return {};
        }

        return frameBoundary_->frameID;
    }

void CommandBatch::clear() noexcept
{
        commandBuffers_.clear();
        waitPoints_.clear();
        signalPoints_.clear();
        frameBoundary_.reset();
    }

[[nodiscard]] bool CommandBatch::empty() const noexcept
{ return commandBuffers_.empty(); }

[[nodiscard]] std::size_t CommandBatch::commandBufferCount() const noexcept
{ return commandBuffers_.size(); }

[[nodiscard]] CommandBatch::SubmitInfo2Packet CommandBatch::buildSubmitInfo2() const
{
        SubmitInfo2Packet packet{};

        packet.waitInfos.reserve(waitPoints_.size());
        packet.commandBufferInfos.reserve(commandBuffers_.size());
        packet.signalInfos.reserve(signalPoints_.size());

        std::ranges::for_each(waitPoints_, [&](const SemaphoreSyncPoint& waitPoint) {
            packet.waitInfos.push_back(vk::SemaphoreSubmitInfo{
                waitPoint.semaphore,
                waitPoint.value,
                waitPoint.stageMask,
                waitPoint.deviceIndex,
            });
        });

        std::ranges::for_each(commandBuffers_, [&](vk::CommandBuffer commandBuffer) {
            packet.commandBufferInfos.push_back(vk::CommandBufferSubmitInfo{
                commandBuffer,
                0,
            });
        });

        std::ranges::for_each(signalPoints_, [&](const SemaphoreSyncPoint& signalPoint) {
            packet.signalInfos.push_back(vk::SemaphoreSubmitInfo{
                signalPoint.semaphore,
                signalPoint.value,
                signalPoint.stageMask,
                signalPoint.deviceIndex,
            });
        });

        packet.submitInfo = vk::SubmitInfo2{};
        packet.submitInfo.waitSemaphoreInfoCount = static_cast<std::uint32_t>(packet.waitInfos.size());
        packet.submitInfo.pWaitSemaphoreInfos = packet.waitInfos.data();
        packet.submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(packet.commandBufferInfos.size());
        packet.submitInfo.pCommandBufferInfos = packet.commandBufferInfos.data();
        packet.submitInfo.signalSemaphoreInfoCount = static_cast<std::uint32_t>(packet.signalInfos.size());
        packet.submitInfo.pSignalSemaphoreInfos = packet.signalInfos.data();

        if (frameBoundary_.has_value())
        {
            packet.frameBoundaryImages = frameBoundary_->images;
            packet.frameBoundaryBuffers = frameBoundary_->buffers;

            auto const imageCount = static_cast<std::uint32_t>(packet.frameBoundaryImages.size());
            auto const bufferCount = static_cast<std::uint32_t>(packet.frameBoundaryBuffers.size());
            auto const* imageData = packet.frameBoundaryImages.empty() ? nullptr : packet.frameBoundaryImages.data();
            auto const* bufferData = packet.frameBoundaryBuffers.empty() ? nullptr : packet.frameBoundaryBuffers.data();

            packet.frameBoundary.emplace(
                frameBoundary_->flags,
                frameBoundary_->frameID,
                imageCount,
                imageData,
                bufferCount,
                bufferData);
            packet.submitInfo.pNext = std::addressof(*packet.frameBoundary);
        }

        return packet;
    }

CommandBatch& CommandBatch::withCommandBuffer(const vk::raii::CommandBuffer& cb)
{
        addCommandBuffer(cb);
        return *this;
    }

CommandBatch& CommandBatch::withWait(const vk::raii::Semaphore& sem, vk::PipelineStageFlags2 stage, std::uint64_t value, std::uint32_t deviceIndex)
{
        addWait(sem, stage, value, deviceIndex);
        return *this;
    }

CommandBatch& CommandBatch::withSignal(const vk::raii::Semaphore& sem, std::uint64_t value, std::uint32_t deviceIndex, vk::PipelineStageFlags2 stage)
{
        addSignal(sem, value, deviceIndex, stage);
        return *this;
    }
} // namespace nr::rhi

namespace nr::rhi
{
namespace batch
{
[[nodiscard]] CommandBatch single(
    const vk::raii::CommandBuffer& commandBuffer
)
{
    CommandBatch batch{};
    batch.addCommandBuffer(commandBuffer);
    return batch;
}

[[nodiscard]] CommandBatch graphics(
    const vk::raii::CommandBuffer& commandBuffer,
    const vk::raii::Semaphore& imageAvailable,
    const vk::raii::Semaphore& renderFinished
)
{
    CommandBatch batch{};
    batch.addCommandBuffer(commandBuffer);
    batch.addWait(imageAvailable, vk::PipelineStageFlagBits2::eColorAttachmentOutput);
    batch.addSignal(renderFinished);
    return batch;
}
} // namespace batch
} // namespace nr::rhi
