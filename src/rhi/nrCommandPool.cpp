module nr.rhi;
import :commandPool;
import dependency.vulkan;
import std;

namespace nr::rhi
{
CommandPool::CommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex,
                         vk::CommandPoolCreateFlags flags)
    : device_(std::ref(device))
{
    vk::CommandPoolCreateInfo createInfo{flags, queueFamilyIndex};
    pool_ = vk::raii::CommandPool(device, createInfo);
}

[[nodiscard]] vk::raii::CommandBuffers CommandPool::allocatePrimary(std::uint32_t count)
{
    vk::CommandBufferAllocateInfo allocInfo{*pool_, vk::CommandBufferLevel::ePrimary, count};
    return vk::raii::CommandBuffers(device_->get(), allocInfo);
}

[[nodiscard]] vk::raii::CommandBuffers CommandPool::allocateSecondary(std::uint32_t count)
{
    vk::CommandBufferAllocateInfo allocInfo{*pool_, vk::CommandBufferLevel::eSecondary, count};
    return vk::raii::CommandBuffers(device_->get(), allocInfo);
}

void CommandPool::reset(vk::CommandPoolResetFlags flags)
{
    pool_.reset(flags);
}

[[nodiscard]] bool CommandPool::valid() const noexcept
{
    return device_.has_value() && *pool_ != nullptr;
}
} // namespace nr::rhi
