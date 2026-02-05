module;
export module nr.rhi:queue;
import dependency;
import std;
import :commandBatch;

namespace nr::rhi
{

/**
 * @brief Minimal queue type enumeration for command submission
 *
 * Maps to the three most common queue families:
 * - Graphics: Rendering, compute, transfer
 * - Compute: Async compute workloads
 * - Transfer: DMA transfers
 */
export enum class QueueType
{
    Graphics,
    Compute,
    Transfer
};

/**
 * @brief Thin wrapper around vk::raii::Queue for command submission
 *
 * Responsibilities:
 * - Submit command buffers with synchronization
 * - Works exclusively with vk::raii:: objects
 * - Pure submission interface
 *
 * Design Philosophy:
 * - Queue只负责submit + sync
 * - All scheduling/orchestration happens outside
 * - RDG-ready: future render graphs will call the same submit()
 * 
 * Submit pattern follows vulkan-hpp RAII convention:
 *   queue.submit(vk::SubmitInfo(..., *commandBuffer), fence);
 */
export class GpuQueue
{
  public:
    /// Default constructor for deferred initialization
    GpuQueue() : type_(QueueType::Graphics)
    {
    }

    /**
     * @brief Construct queue wrapper from device and queue family
     * @param device Vulkan device
     * @param queueFamilyIndex Queue family index
     * @param type Logical queue type classification
     */
    GpuQueue(const vk::raii::Device& device, uint32_t queueFamilyIndex, QueueType type) 
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
     * @brief Submit command buffers with RAII synchronization objects
     * @param commandBuffers RAII command buffers
     * @param waitSemaphores RAII semaphores to wait on
     * @param waitStages Pipeline stages to wait at
     * @param signalSemaphores RAII semaphores to signal
     * @param fence RAII fence to signal (optional)
     * 
     * Following vulkan-hpp RAII convention:
     *   queue.submit(vk::SubmitInfo(nullptr, nullptr, *commandBuffer), fence);
     */
    void submit(
        std::span<const vk::raii::CommandBuffer> commandBuffers,
        std::span<const vk::raii::Semaphore> waitSemaphores,
        std::span<const vk::PipelineStageFlags> waitStages,
        std::span<const vk::raii::Semaphore> signalSemaphores,
        const vk::raii::Fence* fence = nullptr
    )
    {
        // Extract raw handles from RAII objects (vulkan-hpp pattern)
        std::vector<vk::CommandBuffer> cbHandles;
        cbHandles.reserve(commandBuffers.size());
        for (const auto& cb : commandBuffers) {
            cbHandles.push_back(*cb);
        }

        std::vector<vk::Semaphore> waitHandles;
        waitHandles.reserve(waitSemaphores.size());
        for (const auto& sem : waitSemaphores) {
            waitHandles.push_back(*sem);
        }

        std::vector<vk::Semaphore> signalHandles;
        signalHandles.reserve(signalSemaphores.size());
        for (const auto& sem : signalSemaphores) {
            signalHandles.push_back(*sem);
        }

        vk::Fence fenceHandle = fence ? **fence : vk::Fence{};

        vk::SubmitInfo submitInfo{waitHandles, waitStages, cbHandles, signalHandles};
        queue_.submit(submitInfo, fenceHandle);
    }

    /**
     * @brief Submit single command buffer (convenience overload)
     * @param commandBuffer Single RAII command buffer
     * @param fence RAII fence to signal (optional)
     * 
     * Follows vulkan-hpp RAII_Samples pattern:
     *   queue.submit(vk::SubmitInfo(nullptr, nullptr, *commandBuffer), fence);
     */
    void submit(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::raii::Fence* fence = nullptr
    )
    {
        vk::SubmitInfo submitInfo{nullptr, nullptr, *commandBuffer};
        queue_.submit(submitInfo, fence ? **fence : vk::Fence{});
    }

    /**
     * @brief Submit a CommandBatch
     * @param batch Command batch to submit
     * @param fence RAII fence to signal (optional)
     *
     * Queue is the executor; CommandBatch is passive data.
     */
    void submit(
        const CommandBatch& batch,
        const vk::raii::Fence* fence = nullptr
    )
    {
        vk::Fence fenceHandle = fence ? **fence : vk::Fence{};
        queue_.submit(batch.buildSubmitInfo(), fenceHandle);
    }

    /**
     * @brief Submit and wait for completion (blocking)
     * @param device Device for fence creation
     * @param commandBuffer Command buffer to submit
     * 
     * Follows vulkan-hpp RAII_Samples submitAndWait pattern:
     *   vk::raii::Fence fence(device, vk::FenceCreateInfo());
     *   queue.submit(vk::SubmitInfo(nullptr, nullptr, *commandBuffer), fence);
     *   device.waitForFences({fence}, VK_TRUE, timeout);
     */
    void submitAndWait(
        const vk::raii::Device& device,
        const vk::raii::CommandBuffer& commandBuffer,
        uint64_t timeout = std::numeric_limits<uint64_t>::max()
    )
    {
        vk::raii::Fence fence(device, vk::FenceCreateInfo{});
        queue_.submit(vk::SubmitInfo{nullptr, nullptr, *commandBuffer}, fence);
        while (vk::Result::eTimeout == device.waitForFences({fence}, vk::True, timeout))
            ;
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
    [[nodiscard]] QueueType type() const noexcept
    {
        return type_;
    }

    /**
     * @brief Get the queue family index
     */
    [[nodiscard]] uint32_t queueFamilyIndex() const noexcept
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
    vk::raii::Queue queue_ = {nullptr};
    uint32_t queueFamilyIndex_ = 0;
    uint32_t queueIndex_ = 0;
    QueueType type_;
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
export class QueueManager
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
    [[nodiscard]] GpuQueue& get(QueueType type)
    {
        switch (type)
        {
        case QueueType::Graphics:
            return graphics_;
        case QueueType::Compute:
            return compute_;
        case QueueType::Transfer:
            return transfer_;
        }
        std::unreachable();
    }

    /**
     * @brief Check if a specific queue type is available
     */
    [[nodiscard]] bool hasQueue(QueueType type) const noexcept
    {
        switch (type)
        {
        case QueueType::Graphics:
            return graphics_.valid();
        case QueueType::Compute:
            return compute_.valid();
        case QueueType::Transfer:
            return transfer_.valid();
        }
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
