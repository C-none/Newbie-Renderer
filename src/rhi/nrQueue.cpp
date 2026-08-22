module nr.rhi;
import :queue;
import dependency.vulkan;
import nr.utils;
import std;
import :commandBatch;
import :type;

namespace nr::rhi
{
GpuQueue::GpuQueue(const vk::raii::Device &device, std::uint32_t queueFamilyIndex)
    : queue_(device.getQueue(queueFamilyIndex, 0)), queueFamilyIndex_(queueFamilyIndex)
{
}

void GpuQueue::submit(const vk::raii::CommandBuffer &commandBuffer,
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

void GpuQueue::submit(CommandBatch &&batch, std::optional<std::reference_wrapper<const vk::raii::Fence>> fence)
{
    auto frameBoundary = batch.frameBoundarySubmitInfo();
    auto submitInfo = batch.submitInfo2View(frameBoundary.has_value() ? std::addressof(*frameBoundary) : nullptr);
    vk::Fence fenceHandle = fence ? *fence.value().get() : vk::Fence{};
    queue_.submit2(submitInfo, fenceHandle);
}

void GpuQueue::waitIdle()
{
    queue_.waitIdle();
}

[[nodiscard]] std::uint32_t GpuQueue::queueFamilyIndex() const noexcept
{
    return queueFamilyIndex_;
}

[[nodiscard]] const vk::raii::Queue &GpuQueue::handle() const noexcept
{
    return queue_;
}

QueueManager::QueueManager(GpuQueue graphics, GpuQueue compute, GpuQueue transfer)
    : graphics_(std::move(graphics)), compute_(std::move(compute)), transfer_(std::move(transfer))
{
}

[[nodiscard]] GpuQueue &QueueManager::graphics()
{
    return graphics_;
}

[[nodiscard]] const GpuQueue &QueueManager::graphics() const
{
    return graphics_;
}

[[nodiscard]] GpuQueue &QueueManager::compute()
{
    return compute_;
}

[[nodiscard]] const GpuQueue &QueueManager::compute() const
{
    return compute_;
}

[[nodiscard]] GpuQueue &QueueManager::transfer()
{
    return transfer_;
}

[[nodiscard]] const GpuQueue &QueueManager::transfer() const
{
    return transfer_;
}

[[nodiscard]] QueueFamilyIndices QueueManager::familyIndices() const noexcept
{
    return QueueFamilyIndices{
        .graphics = graphics_.queueFamilyIndex(),
        .compute = compute_.queueFamilyIndex(),
        .transfer = transfer_.queueFamilyIndex(),
    };
}

[[nodiscard]] GpuQueue &QueueManager::forRole(QueueRole role)
{
    return const_cast<GpuQueue &>(std::as_const(*this).forRole(role));
}

[[nodiscard]] const GpuQueue &QueueManager::forRole(QueueRole role) const
{
    switch (role)
    {
    case QueueRole::Graphics:
        return graphics_;
    case QueueRole::Compute:
        return compute_;
    case QueueRole::Transfer:
        return transfer_;
    }
    nrAssert(false, "QueueManager::forRole received an unknown QueueRole.");
}

void QueueManager::waitAllIdle()
{
    graphics_.waitIdle();
    compute_.waitIdle();
    transfer_.waitIdle();
}
} // namespace nr::rhi
