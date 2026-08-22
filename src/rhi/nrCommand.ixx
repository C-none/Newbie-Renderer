export module nr.rhi:command;
import dependency.vulkan;
import nr.utils;
import std;
import :commandBatch;
import :commandPool;
import :queue;

export namespace nr::rhi
{

/**
 * @brief Semaphore wait applied before one-shot command execution.
 *
 * A default-constructed plan carries a null semaphore and requests no wait.
 */
struct OneShotSyncPlan
{
    vk::Semaphore waitSemaphore{};
    vk::PipelineStageFlags2 waitStage = vk::PipelineStageFlagBits2::eAllCommands;
    std::uint64_t waitValue = 0;
};

/**
 * @brief Record a primary command buffer with eOneTimeSubmit, submit it, and block until completion.
 *
 * Owns the transient command pool and the binary fence. Fence completion implies
 * the entire one-shot queue submission finished, so no post-completion cleanup
 * depends on a broader device/queue idle state.
 */
template <std::invocable<const vk::raii::CommandBuffer &> TRecord>
void submitOneShot(const vk::raii::Device &device, GpuQueue &queue, const OneShotSyncPlan &sync, TRecord &&record)
{
    auto commandPool = CommandPool{device, queue.queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient};
    auto commandBuffers = commandPool.allocate<vk::CommandBufferLevel::ePrimary>(1u);
    nrAssert(!commandBuffers.empty(), "submitOneShot failed to allocate a command buffer.");
    auto &commandBuffer = commandBuffers.front();

    commandBuffer.begin(vk::CommandBufferBeginInfo{vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
    std::invoke(std::forward<TRecord>(record), commandBuffer);
    commandBuffer.end();

    auto fence = vk::raii::Fence(device, vk::FenceCreateInfo{});
    if (sync.waitSemaphore == vk::Semaphore{})
    {
        queue.submit(commandBuffer, std::cref(fence));
    }
    else
    {
        auto batch = CommandBatch{};
        batch.addWait(sync.waitSemaphore, sync.waitStage, sync.waitValue);
        batch.addCommandBuffer(commandBuffer);
        queue.submit(std::move(batch), std::cref(fence));
    }

    auto const waitResult = device.waitForFences(*fence, vk::True, std::numeric_limits<std::uint64_t>::max());
    nrAssert(waitResult == vk::Result::eSuccess, "submitOneShot failed waiting for one-shot command completion.");
}

/**
 * @brief RAII wrapper for command buffer debug label scope
 * 
 * Automatically calls beginDebugUtilsLabelEXT on construction and
 * endDebugUtilsLabelEXT on destruction. Useful for GPU debuggers
 * like RenderDoc to group commands with named labels.
 * 
 * Usage:
 *   {
 *       ScopedCommandBufferDebugLabel label(cb, "Shadow Pass");
 *       // ... shadow pass commands ...
 *   } // Automatically ends debug label
 */
class ScopedCommandBufferDebugLabel
{
  public:
    ScopedCommandBufferDebugLabel(const vk::raii::CommandBuffer &commandBuffer, std::string_view label);

    ~ScopedCommandBufferDebugLabel();

    // Non-copyable, non-movable (RAII lifetime bound to scope)
    ScopedCommandBufferDebugLabel(const ScopedCommandBufferDebugLabel &) = delete;
    ScopedCommandBufferDebugLabel &operator=(const ScopedCommandBufferDebugLabel &) = delete;
    ScopedCommandBufferDebugLabel(ScopedCommandBufferDebugLabel &&) = delete;
    ScopedCommandBufferDebugLabel &operator=(ScopedCommandBufferDebugLabel &&) = delete;

    void close();

  private:
    std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer_;
    std::string label_;
    bool active_ = false;
};

} // namespace nr::rhi
