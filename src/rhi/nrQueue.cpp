module nr.rhi;
import :queue;
import dependency.vulkan;
import std;
import :commandBatch;
import :type;

namespace nr::rhi
{
GpuQueue::GpuQueue(const vk::raii::Device& device, std::uint32_t queueFamilyIndex, QueueRole type) 
        : queue_(device.getQueue(queueFamilyIndex, queueIndex_))
        , queueFamilyIndex_(queueFamilyIndex)
        , type_(type)
{
    }

void GpuQueue::submit(
        const vk::raii::CommandBuffer& commandBuffer,
        std::optional<std::reference_wrapper<const vk::raii::Fence>> fence)
{
        std::array<vk::CommandBufferSubmitInfo, 1> commandBufferInfos{
            vk::CommandBufferSubmitInfo{*commandBuffer, 0},
        };

        vk::SubmitInfo2 submitInfo{};
        submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(commandBufferInfos.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos.data();

        queue_.submit2(submitInfo, fence ? *fence.value().get() : vk::Fence{});
    }

void GpuQueue::submit(
        const CommandBatch& batch,
        std::optional<std::reference_wrapper<const vk::raii::Fence>> fence)
{
        auto submitPacket = batch.buildSubmitInfo2();
        vk::Fence fenceHandle = fence ? *fence.value().get() : vk::Fence{};
        queue_.submit2(submitPacket.info(), fenceHandle);
    }

void GpuQueue::waitIdle()
{
        queue_.waitIdle();
    }

[[nodiscard]] QueueRole GpuQueue::type() const noexcept
{
        return type_;
    }

[[nodiscard]] std::uint32_t GpuQueue::queueFamilyIndex() const noexcept
{
        return queueFamilyIndex_;
    }

[[nodiscard]] const vk::raii::Queue& GpuQueue::handle() const noexcept
{
        return queue_;
    }

[[nodiscard]] bool GpuQueue::valid() const noexcept
{
        return *queue_ != nullptr;
    }

QueueManager::QueueManager(GpuQueue graphics, GpuQueue compute, GpuQueue transfer) 
        : graphics_(std::move(graphics))
        , compute_(std::move(compute))
        , transfer_(std::move(transfer))
{
    }

[[nodiscard]] GpuQueue& QueueManager::graphics()
{ return graphics_; }

[[nodiscard]] const GpuQueue& QueueManager::graphics() const
{ return graphics_; }

[[nodiscard]] GpuQueue& QueueManager::compute()
{ return compute_; }

[[nodiscard]] const GpuQueue& QueueManager::compute() const
{ return compute_; }

[[nodiscard]] GpuQueue& QueueManager::transfer()
{ return transfer_; }

[[nodiscard]] const GpuQueue& QueueManager::transfer() const
{ return transfer_; }

void QueueManager::waitAllIdle()
{
        if (graphics_.valid()) graphics_.waitIdle();
        if (compute_.valid()) compute_.waitIdle();
        if (transfer_.valid()) transfer_.waitIdle();
    }
} // namespace nr::rhi
