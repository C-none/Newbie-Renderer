module;
export module nr.rhi:sync;

import dependency;
import nr.utils;
import std;

export namespace nr::rhi::sync
{

/**
 * @brief Create a binary semaphore.
 */
[[nodiscard]] inline vk::raii::Semaphore createSemaphore(const vk::raii::Device& device)
{
    return vk::raii::Semaphore(device, vk::SemaphoreCreateInfo{});
}

/**
 * @brief Create a timeline semaphore with an initial counter value.
 */
[[nodiscard]] inline vk::raii::Semaphore createTimelineSemaphore(const vk::raii::Device& device, uint64_t initialValue = 0)
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
[[nodiscard]] inline uint64_t timelineValue(const vk::raii::Device& device, vk::Semaphore timelineSemaphore)
{
    uint64_t value = 0;
    auto result = vkGetSemaphoreCounterValue(
        static_cast<VkDevice>(*device),
        static_cast<VkSemaphore>(timelineSemaphore),
        &value);
    nrAssert(result == VK_SUCCESS, std::format("vkGetSemaphoreCounterValue failed: {}", static_cast<int>(result)));
    return value;
}

/**
 * @brief Wait until timeline semaphore reaches at least @p value.
 */
[[nodiscard]] inline bool waitTimeline(const vk::raii::Device& device, vk::Semaphore timelineSemaphore, uint64_t value, uint64_t timeout = std::numeric_limits<uint64_t>::max())
{
    VkSemaphore rawSemaphore = static_cast<VkSemaphore>(timelineSemaphore);
    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.flags = 0;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &rawSemaphore;
    waitInfo.pValues = &value;
    auto result = vkWaitSemaphores(static_cast<VkDevice>(*device), &waitInfo, timeout);
    return result == VK_SUCCESS;
}

} // namespace nr::rhi::sync
