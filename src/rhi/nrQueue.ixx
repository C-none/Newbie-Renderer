export module nr.rhi:queue;
import dependency;
import std;
import :commandBatch;
import :type;

export namespace nr::rhi
{

/**
 * @brief Thin wrapper around vk::raii::Queue for command submission
 *
 * Responsibilities:
 * - Submit command buffers with synchronization
 * - Works exclusively with vk::raii:: objects
 * - Pure submission interface
 *
 * Design Philosophy:
 * - Queue only for submit + sync
 * - All scheduling/orchestration happens outside
 * - RDG-ready: future render graphs will call the same submit()
 * 
 * Submit pattern follows synchronization2 (`vk::SubmitInfo2`) convention:
 *   queue.submit2(vk::SubmitInfo2(...), fence);
 */
class GpuQueue
{
  public:
    /// Default constructor for deferred initialization
    GpuQueue() = default;

    /**
     * @brief Construct queue wrapper from device and queue family
     * @param device Vulkan device
     * @param queueFamilyIndex Queue family index
     * @param type Logical queue role classification
     */
    GpuQueue(const vk::raii::Device& device, std::uint32_t queueFamilyIndex, QueueRole type) 
        : queue_(device.getQueue(queueFamilyIndex, queueIndex_))
        , queueFamilyIndex_(queueFamilyIndex)
        , type_(type)
    {
    }

    // Move-only semantics
    GpuQueue(const GpuQueue&) = delete;
    GpuQueue& operator=(const GpuQueue&) = delete;
    GpuQueue(GpuQueue&&) noexcept = default;
    GpuQueue& operator=(GpuQueue&&) noexcept = default;

    /**
     * @brief Submit single command buffer (convenience overload)
     * @param commandBuffer Single RAII command buffer
     * @param fence RAII fence to signal (optional)
     * 
     * Zero-copy fast path for single command buffer submission.
     * For complex submissions with sync, use CommandBatch.
     * 
    * Uses synchronization2 submit path:
    *   queue.submit2(vk::SubmitInfo2(...), fence);
     */
    void submit(
        const vk::raii::CommandBuffer& commandBuffer,
        std::optional<std::reference_wrapper<const vk::raii::Fence>> fence = std::nullopt
    )
    {
        std::array<vk::CommandBufferSubmitInfo, 1> commandBufferInfos{
            vk::CommandBufferSubmitInfo{*commandBuffer, 0},
        };

        vk::SubmitInfo2 submitInfo{};
        submitInfo.commandBufferInfoCount = static_cast<std::uint32_t>(commandBufferInfos.size());
        submitInfo.pCommandBufferInfos = commandBufferInfos.data();

        queue_.submit2(submitInfo, fence ? *fence.value().get() : vk::Fence{});
    }

    /**
     * @param batch Command batch to submit
     * @param fence RAII fence to signal (optional)
     *
     * This is the PRIMARY submission interface:
     * - Zero-copy submission (no temporary allocations)
     * - Reusable for render loops (call batch.clear() and rebuild)
     * - CommandBatch internally stores handles, avoiding repeated extraction
     * 
     * Performance characteristics:
     * - First use: O(n) handle extraction in batch.add*()
    * - Reuse: O(1) submission via buildSubmitInfo2()
     * - Memory: Single allocation in CommandBatch, reused across frames
     * 
     * Usage:
     *   CommandBatch batch;               // Construct once
     *   batch.addCommandBuffer(cb);       // Build submission
     *   queue.submit(batch, fence);       // Zero-copy submit
     *   batch.clear();                    // Reuse next frame
     */
    void submit(
        const CommandBatch& batch,
        std::optional<std::reference_wrapper<const vk::raii::Fence>> fence = std::nullopt
    )
    {
        auto submitPacket = batch.buildSubmitInfo2();
        vk::Fence fenceHandle = fence ? *fence.value().get() : vk::Fence{};
        queue_.submit2(submitPacket.info(), fenceHandle);
    }

    /**
     * @brief Wait for all operations on this queue to complete
     *
     * Blocks until queue is idle. Use for synchronization points.
     */
    void waitIdle()
    {
        queue_.waitIdle();
    }

    /**
     * @brief Get the queue type classification
     */
    [[nodiscard]] QueueRole type() const noexcept
    {
        return type_;
    }

    /**
     * @brief Get the queue family index
     */
    [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept
    {
        return queueFamilyIndex_;
    }

    /**
     * @brief Get the underlying vk::raii::Queue (for direct access if needed)
     */
    [[nodiscard]] const vk::raii::Queue& handle() const noexcept
    {
        return queue_;
    }

    /**
     * @brief Check if queue is valid (initialized)
     */
    [[nodiscard]] bool valid() const noexcept
    {
        return *queue_ != nullptr;
    }

  private:
    std::uint32_t queueIndex_ = 0;
    vk::raii::Queue queue_ = {nullptr};
    std::uint32_t queueFamilyIndex_ = 0;
    QueueRole type_ = QueueRole::Graphics;
};

/**
 * @brief Centralized queue access manager
 *
 * Responsibilities:
 * - Provide typed access to queues (graphics, compute, transfer)
 * - Created and owned by Device
 * - Does NOT create queues (Device does that)
 *
 * Usage:
 *   queueManager.graphics().submit(commandBuffer);
 *   queueManager.graphics().submitAndWait(device, commandBuffer);
 */
class QueueManager
{
  public:
    /// Default constructor for deferred initialization
    QueueManager() = default;

    /**
     * @brief Construct queue manager with pre-created queues
     * @param graphics Graphics queue
     * @param compute Compute queue
     * @param transfer Transfer queue
     */
    QueueManager(GpuQueue graphics, GpuQueue compute, GpuQueue transfer) 
        : graphics_(std::move(graphics))
        , compute_(std::move(compute))
        , transfer_(std::move(transfer))
    {
    }

    // Move-only semantics
    QueueManager(const QueueManager&) = delete;
    QueueManager& operator=(const QueueManager&) = delete;
    QueueManager(QueueManager&&) noexcept = default;
    QueueManager& operator=(QueueManager&&) noexcept = default;

    [[nodiscard]] GpuQueue& graphics() { return graphics_; }
    [[nodiscard]] const GpuQueue& graphics() const { return graphics_; }

    [[nodiscard]] GpuQueue& compute() { return compute_; }
    [[nodiscard]] const GpuQueue& compute() const { return compute_; }

    [[nodiscard]] GpuQueue& transfer() { return transfer_; }
    [[nodiscard]] const GpuQueue& transfer() const { return transfer_; }

    /**
     * @brief Get queue by type
     */
    template <QueueRole T>
    [[nodiscard]] GpuQueue& get() noexcept
    {
        if constexpr (T == QueueRole::Graphics)
            return graphics_;
        else if constexpr (T == QueueRole::Compute)
            return compute_;
        else if constexpr (T == QueueRole::Transfer)
            return transfer_;
        std::unreachable();
    }

    /**
     * @brief Check if a specific queue type is available
     */
    template <QueueRole T>
    [[nodiscard]] bool hasQueue() const noexcept
    {
        if constexpr (T == QueueRole::Graphics)
            return graphics_.valid();
        else if constexpr (T == QueueRole::Compute)
            return compute_.valid();
        else if constexpr (T == QueueRole::Transfer)
            return transfer_.valid();
        return false;
    }

    /**
     * @brief Wait for all queues to become idle
     */
    void waitAllIdle()
    {
        if (graphics_.valid()) graphics_.waitIdle();
        if (compute_.valid()) compute_.waitIdle();
        if (transfer_.valid()) transfer_.waitIdle();
    }

  private:
    GpuQueue graphics_;
    GpuQueue compute_;
    GpuQueue transfer_;
};

} // namespace nr::rhi
