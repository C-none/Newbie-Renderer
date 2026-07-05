module nr.rhi;
import :commandBatch;
import dependency.vulkan;
import std;

namespace nr::rhi
{
void CommandBatch::addCommandBuffer(const vk::raii::CommandBuffer& commandBuffer)
{
        commandBufferInfos_.push_back(vk::CommandBufferSubmitInfo{*commandBuffer, 0});
    }

void CommandBatch::addCommandBuffers(std::span<const vk::raii::CommandBuffer> commandBuffers)
{
        commandBufferInfos_.reserve(commandBufferInfos_.size() + commandBuffers.size());
        std::ranges::for_each(commandBuffers, [&](const auto& cb) {
            commandBufferInfos_.push_back(vk::CommandBufferSubmitInfo{*cb, 0});
        });
    }

void CommandBatch::addWait(const vk::raii::Semaphore& semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value, std::uint32_t deviceIndex)
{
        addWait(*semaphore, stage, value, deviceIndex);
    }

void CommandBatch::addWait(vk::Semaphore semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value, std::uint32_t deviceIndex)
{
        waitInfos_.push_back(vk::SemaphoreSubmitInfo{semaphore, value, stage, deviceIndex});
    }

void CommandBatch::addSignal(const vk::raii::Semaphore& semaphore, std::uint64_t value, std::uint32_t deviceIndex, vk::PipelineStageFlags2 stage)
{
        addSignal(*semaphore, value, deviceIndex, stage);
    }

void CommandBatch::addSignal(vk::Semaphore semaphore, std::uint64_t value, std::uint32_t deviceIndex, vk::PipelineStageFlags2 stage)
{
        signalInfos_.push_back(vk::SemaphoreSubmitInfo{semaphore, value, stage, deviceIndex});
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
        commandBufferInfos_.clear();
        waitInfos_.clear();
        signalInfos_.clear();
        frameBoundary_.reset();
    }

[[nodiscard]] bool CommandBatch::empty() const noexcept
{ return commandBufferInfos_.empty(); }

[[nodiscard]] std::size_t CommandBatch::commandBufferCount() const noexcept
{ return commandBufferInfos_.size(); }

[[nodiscard]] std::optional<vk::FrameBoundaryEXT> CommandBatch::frameBoundarySubmitInfo() const
{
        if (!frameBoundary_.has_value())
        {
            return {};
        }

        auto const imageCount = static_cast<std::uint32_t>(frameBoundary_->images.size());
        auto const bufferCount = static_cast<std::uint32_t>(frameBoundary_->buffers.size());
        auto const* imageData = frameBoundary_->images.empty() ? nullptr : frameBoundary_->images.data();
        auto const* bufferData = frameBoundary_->buffers.empty() ? nullptr : frameBoundary_->buffers.data();
        return vk::FrameBoundaryEXT{
            frameBoundary_->flags,
            frameBoundary_->frameID,
            imageCount,
            imageData,
            bufferCount,
            bufferData,
        };
    }

[[nodiscard]] vk::SubmitInfo2 CommandBatch::submitInfo2View(const vk::FrameBoundaryEXT* frameBoundary) const noexcept
{
        auto submitInfo = vk::SubmitInfo2{};
        submitInfo.waitSemaphoreInfoCount = static_cast<std::uint32_t>(waitInfos_.size());
        submitInfo.pWaitSemaphoreInfos = waitInfos_.empty() ? nullptr : waitInfos_.data();
        submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(commandBufferInfos_.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos_.empty() ? nullptr : commandBufferInfos_.data();
        submitInfo.signalSemaphoreInfoCount = static_cast<std::uint32_t>(signalInfos_.size());
        submitInfo.pSignalSemaphoreInfos = signalInfos_.empty() ? nullptr : signalInfos_.data();
        submitInfo.pNext = frameBoundary;
        return submitInfo;
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
} // namespace batch
} // namespace nr::rhi
