export module nr.rhi:sync;
import dependency.vulkan;

import nr.utils;
import std;

export namespace nr::rhi::sync
{

/**
 * @brief Create a timeline semaphore with an initial counter value.
 *
 * Recommended for cross-queue pipelines such as:
 * Transfer(upload geometry/instance buffers) -> Compute(build AS) -> Graphics/Compute(consume AS).
 * Timeline semaphores express this dependency chain with one monotonically
 * increasing counter instead of many binary semaphores.
 */
[[nodiscard]] inline vk::raii::Semaphore createTimelineSemaphore(const vk::raii::Device& device, std::uint64_t initialValue = 0)
{
    vk::SemaphoreTypeCreateInfo typeInfo{
        vk::SemaphoreType::eTimeline,
        initialValue,
    };
    vk::SemaphoreCreateInfo createInfo{};
    createInfo.pNext = &typeInfo;
    return vk::raii::Semaphore(device, createInfo);
}

/**
 * @brief Query current timeline semaphore counter value.
 */
[[nodiscard]] inline std::uint64_t timelineValue(const vk::raii::Semaphore& timelineSemaphore)
{
    return timelineSemaphore.getCounterValue();
}

/**
 * @brief Wait until timeline semaphore reaches at least @p value.
 */
[[nodiscard]] inline bool waitTimeline(const vk::raii::Device& device, const vk::raii::Semaphore& timelineSemaphore, std::uint64_t value, std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max())
{
    auto semaphoreHandle = static_cast<vk::Semaphore>(*timelineSemaphore);
    vk::SemaphoreWaitInfo waitInfo{};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &semaphoreHandle;
    waitInfo.pValues = &value;
    auto result = device.waitSemaphores(waitInfo, timeout);
    return result == vk::Result::eSuccess;
}

} // namespace nr::rhi::sync
