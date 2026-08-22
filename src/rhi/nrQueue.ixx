export module nr.rhi:queue;
import dependency.vulkan;
import nr.utils;
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
    /**
     * @brief Construct queue wrapper from device and queue family
     * @param device Vulkan device
     * @param queueFamilyIndex Queue family index
     */
    GpuQueue(const vk::raii::Device &device, std::uint32_t queueFamilyIndex);

    // Move-only semantics
    GpuQueue(const GpuQueue &) = delete;
    GpuQueue &operator=(const GpuQueue &) = delete;
    GpuQueue(GpuQueue &&) noexcept = default;
    GpuQueue &operator=(GpuQueue &&) noexcept = default;

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
    void submit(const vk::raii::CommandBuffer &commandBuffer,
                std::optional<std::reference_wrapper<const vk::raii::Fence>> fence = std::nullopt);

    /**
     * @param batch Command batch to submit
     * @param fence RAII fence to signal (optional)
     *
     * This is the PRIMARY submission interface for synchronized batches:
     * - Consumes a one-shot CommandBatch description
     * - Callers must pass std::move(batch) so reuse is explicit and visible
     * - CommandBatch internally stores handles, avoiding repeated extraction while building
     * 
     * Performance characteristics:
     * - Build: add*() appends submit-ready Vulkan-Hpp structures
     * - Submit: creates only a stack SubmitInfo2 view and optional frame-boundary view
     * - Memory: no per-submit submit-packet vectors are allocated
     * 
     * Usage:
     *   CommandBatch batch;               // Build a one-shot submission
     *   batch.addCommandBuffer(cb);       // Build submission
     *   queue.submit(std::move(batch), fence);
     */
    void submit(CommandBatch &&batch,
                std::optional<std::reference_wrapper<const vk::raii::Fence>> fence = std::nullopt);

    /**
     * @brief Wait for all operations on this queue to complete
     *
     * Blocks until queue is idle. Use for synchronization points.
     */
    void waitIdle();

    /**
     * @brief Get the queue family index
     */
    [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept;

    /**
     * @brief Get the underlying vk::raii::Queue (for direct access if needed)
     */
    [[nodiscard]] const vk::raii::Queue &handle() const noexcept;

  private:
    vk::raii::Queue queue_;
    std::uint32_t queueFamilyIndex_;
};

/**
 * @brief Queue family indices for the three runtime queue roles.
 */
struct QueueFamilyIndices
{
    std::uint32_t graphics = 0;
    std::uint32_t compute = 0;
    std::uint32_t transfer = 0;
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
 *   queueManager.waitAllIdle();
 */
class QueueManager
{
  public:
    /**
     * @brief Construct queue manager with pre-created queues
     * @param graphics Graphics queue
     * @param compute Compute queue
     * @param transfer Transfer queue
     */
    QueueManager(GpuQueue graphics, GpuQueue compute, GpuQueue transfer);

    // Move-only semantics
    QueueManager(const QueueManager &) = delete;
    QueueManager &operator=(const QueueManager &) = delete;
    QueueManager(QueueManager &&) noexcept = default;
    QueueManager &operator=(QueueManager &&) noexcept = default;

    [[nodiscard]] GpuQueue &graphics();
    [[nodiscard]] const GpuQueue &graphics() const;

    [[nodiscard]] GpuQueue &compute();
    [[nodiscard]] const GpuQueue &compute() const;

    [[nodiscard]] GpuQueue &transfer();
    [[nodiscard]] const GpuQueue &transfer() const;

    /**
     * @brief Fetch the queue family indices of all three roles in one call.
     */
    [[nodiscard]] QueueFamilyIndices familyIndices() const noexcept;

    /**
     * @brief Resolve a QueueRole to its queue.
     */
    [[nodiscard]] GpuQueue &forRole(QueueRole role);
    [[nodiscard]] const GpuQueue &forRole(QueueRole role) const;

    /**
     * @brief Wait for all queues to become idle
     */
    void waitAllIdle();

  private:
    GpuQueue graphics_;
    GpuQueue compute_;
    GpuQueue transfer_;
};

} // namespace nr::rhi
