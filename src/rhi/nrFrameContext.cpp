module nr.rhi;
import :frameContext;
import dependency.vulkan;
import nr.utils;
import std;
import :commandPool;
import :type;

namespace nr::rhi
{
FrameContext::FrameContext(const vk::raii::Device &device, const PoolConfig &graphicsConfig,
                           const PoolConfig &computeConfig, const PoolConfig &transferConfig)
    : device_(device), graphicsQueueFamily_(graphicsConfig.queueFamilyIndex),
      computeQueueFamily_(computeConfig.queueFamilyIndex), transferQueueFamily_(transferConfig.queueFamilyIndex),
      fence_(device, vk::FenceCreateInfo{vk::FenceCreateFlagBits::eSignaled}),
      // Primary command buffers are retained and individually reset every frame,
      // so pools must allow vkResetCommandBuffer.
      graphicsPrimary_(device, graphicsConfig.queueFamilyIndex,
                       vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer),
      computePrimary_(device, computeConfig.queueFamilyIndex,
                      vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer),
      transferPrimary_(device, transferConfig.queueFamilyIndex,
                       vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
{
}

[[nodiscard]] bool FrameContext::waitForFence(std::uint64_t timeout)
{
    return device_.get().waitForFences(*fence_, vk::True, timeout) == vk::Result::eSuccess;
}

void FrameContext::resetFence()
{
    device_.get().resetFences(*fence_);
}

void FrameContext::resetPools()
{
    // Retained primary command buffers are individually reset by their owner
    // after this frame's fence is signaled. Reset only transient secondary pools.
    resetPreparedSecondaryPools(graphicsSecondary_, graphicsPreparedSecondaryWorkers_);
    resetPreparedSecondaryPools(computeSecondary_, computePreparedSecondaryWorkers_);
    resetPreparedSecondaryPools(transferSecondary_, transferPreparedSecondaryWorkers_);
}

void FrameContext::prepareSecondaryPools(std::uint32_t graphicsWorkerCount, std::uint32_t computeWorkerCount,
                                         std::uint32_t transferWorkerCount)
{
    graphicsPreparedSecondaryWorkers_ = std::min(graphicsWorkerCount, kMaxSecondaryWorkers);
    computePreparedSecondaryWorkers_ = std::min(computeWorkerCount, kMaxSecondaryWorkers);

    prepareQueueSecondaryPools(graphicsSecondary_, graphicsQueueFamily_, graphicsPreparedSecondaryWorkers_);
    prepareQueueSecondaryPools(computeSecondary_, computeQueueFamily_, computePreparedSecondaryWorkers_);

    transferPreparedSecondaryWorkers_ = std::min(transferWorkerCount, kMaxSecondaryWorkers);
    if (transferPreparedSecondaryWorkers_ > 0)
    {
        prepareQueueSecondaryPools(transferSecondary_, transferQueueFamily_, transferPreparedSecondaryWorkers_);
    }
}

[[nodiscard]] const vk::raii::Fence &FrameContext::fence() const noexcept
{
    return fence_;
}

void FrameContext::resetPreparedSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots,
                                               std::uint32_t preparedWorkerCount)
{
    std::ranges::for_each(slots | std::views::take(preparedWorkerCount), [](std::optional<CommandPool> &pool) {
        if (pool.has_value())
            pool->reset();
    });
}

void FrameContext::prepareQueueSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots,
                                              std::uint32_t queueFamilyIndex, std::uint32_t workerCount)
{
    auto slotIndices = std::views::iota(std::uint32_t{0}, workerCount);
    std::ranges::for_each(slotIndices, [&](std::uint32_t slotIndex) {
        if (!slots[slotIndex].has_value())
        {
            slots[slotIndex].emplace(device_.get(), queueFamilyIndex,
                                     vk::CommandPoolCreateFlagBits::eTransient |
                                         vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
        }
    });
}
namespace
{
template <std::size_t... Indices>
[[nodiscard]] std::array<FrameContext, nr::maxFrameInFlight> makeFrameContexts(
    const vk::raii::Device &device, const FrameContext::PoolConfig &graphicsConfig,
    const FrameContext::PoolConfig &computeConfig, const FrameContext::PoolConfig &transferConfig,
    std::index_sequence<Indices...>)
{
    return std::array<FrameContext, nr::maxFrameInFlight>{
        ((void)Indices, FrameContext(device, graphicsConfig, computeConfig, transferConfig))...};
}
} // namespace

FrameManager::FrameManager(const vk::raii::Device &device, const FrameContext::PoolConfig &graphicsConfig,
                           const FrameContext::PoolConfig &computeConfig,
                           const FrameContext::PoolConfig &transferConfig)
    : frames_(makeFrameContexts(device, graphicsConfig, computeConfig, transferConfig,
                                std::make_index_sequence<nr::maxFrameInFlight>{}))
{
}

void FrameManager::advanceFrame() noexcept
{
    currentIndex_ = (currentIndex_ + 1) % frames_.size();
}

[[nodiscard]] FrameContext &FrameManager::current() noexcept
{
    return frames_[currentIndex_];
}

[[nodiscard]] std::size_t FrameManager::currentIndex() const noexcept
{
    return currentIndex_;
}

void FrameManager::waitAll()
{
    std::ranges::for_each(frames_, [](FrameContext &frame) {
        nrAssert(frame.waitForFence(), "Timeout waiting for frame fence");
    });
}
} // namespace nr::rhi
