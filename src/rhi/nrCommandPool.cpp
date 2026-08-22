module nr.rhi;
import :commandPool;
import dependency.vulkan;
import std;

namespace nr::rhi
{
CommandPool::CommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex,
                         vk::CommandPoolCreateFlags flags)
    : pool_(device, vk::CommandPoolCreateInfo{flags, queueFamilyIndex}), device_(std::ref(device))
{
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
