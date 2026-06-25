export module nr.rhi:commandBatch;
import dependency.vulkan;
import std;

export namespace nr::rhi
{

/**
 * @brief Submission unit for command buffers with synchronization
 * 
 * Design Philosophy:
 * - CommandBatch = passive data container (submission contract)
 * - Aggregates command buffers and sync primitives
 * - Works exclusively with vk::raii:: objects
 * - Queue selection handled by caller (GpuQueue)
 * - Future: RDG will generate these automatically
 * - Present: Manually constructed by application
 * 
 * Usage Pattern:
 *   CommandBatch batch{};
 *   batch.addCommandBuffer(primaryCB);
 *   batch.addWait(imageAvailable, vk::PipelineStageFlagBits2::eColorAttachmentOutput);
 *   batch.addSignal(renderFinished);
 *   queue.submit(batch, &fence);
 */
class CommandBatch {
public:
    struct SemaphoreSyncPoint {
        vk::Semaphore semaphore = vk::Semaphore{};
        vk::PipelineStageFlags2 stageMask = vk::PipelineStageFlagBits2::eAllCommands;
        std::uint64_t value = 0;
        std::uint32_t deviceIndex = 0;
    };

    struct SubmitInfo2Packet {
        std::vector<vk::SemaphoreSubmitInfo> waitInfos;
        std::vector<vk::CommandBufferSubmitInfo> commandBufferInfos;
        std::vector<vk::SemaphoreSubmitInfo> signalInfos;
        std::vector<vk::Image> frameBoundaryImages;
        std::vector<vk::Buffer> frameBoundaryBuffers;
        std::optional<vk::FrameBoundaryEXT> frameBoundary;
        vk::SubmitInfo2 submitInfo{};

        [[nodiscard]] const vk::SubmitInfo2& info() const noexcept;
    };

    /**
     * @brief Construct empty batch
     */
    CommandBatch() noexcept = default;

    // Copyable and movable (contains only handles and vectors)
    CommandBatch(const CommandBatch&) = default;
    CommandBatch& operator=(const CommandBatch&) = default;
    CommandBatch(CommandBatch&&) noexcept = default;
    CommandBatch& operator=(CommandBatch&&) noexcept = default;

    /**
     * @brief Add a command buffer to the batch
     * @param commandBuffer RAII command buffer (handle extracted automatically)
     */
    void addCommandBuffer(const vk::raii::CommandBuffer& commandBuffer);

    /**
     * @brief Add multiple command buffers to the batch
     * @param commandBuffers Span of RAII command buffers
     */
    void addCommandBuffers(std::span<const vk::raii::CommandBuffer> commandBuffers);

    /**
     * @brief Add a wait semaphore with pipeline stage
     * @param semaphore RAII semaphore to wait on
     * @param stage Pipeline stage to wait at
     * 
        * Queue will wait for semaphore before executing commands.
        *
        * For cross-queue synchronization, this wait participates in the memory
        * dependency created by the matching semaphore signal. In other words, a
        * transfer-queue write followed by a semaphore signal, then a compute-queue
        * wait at `eAccelerationStructureBuildKHR`, is normally sufficient to make
        * the transfer writes visible to AS build reads.
        *
        * This does not replace queue-family ownership transfer barriers for
        * VK_SHARING_MODE_EXCLUSIVE resources, and does not perform image layout
        * transitions.
     */
    void addWait(const vk::raii::Semaphore& semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value = 0, std::uint32_t deviceIndex = 0);

    /**
     * @brief Add a raw wait semaphore sync point.
     */
    void addWait(vk::Semaphore semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value = 0, std::uint32_t deviceIndex = 0);

    /**
     * @brief Add a signal semaphore
     * @param semaphore RAII semaphore to signal after execution
     * 
        * Queue will signal semaphore when commands complete.
        *
        * Together with a matching wait in a later submission, this forms an
        * inter-queue execution+memory dependency. For timeline semaphores, caller
        * must keep signaled values strictly increasing across submissions.
     */
    void addSignal(const vk::raii::Semaphore& semaphore, std::uint64_t value = 0, std::uint32_t deviceIndex = 0, vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands);

    /**
     * @brief Add a raw signal semaphore sync point.
     */
    void addSignal(vk::Semaphore semaphore, std::uint64_t value = 0, std::uint32_t deviceIndex = 0, vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands);

    /**
     * @brief Attach optional VK_EXT_frame_boundary metadata to the next submit.
     *
     * The extension is debugger-facing metadata. CommandBatch owns copies of the
     * image/buffer handle arrays so SubmitInfo2Packet can safely expose pNext views.
     */
    void setFrameBoundary(
        std::uint64_t frameID,
        vk::FrameBoundaryFlagsEXT flags = {},
        std::span<const vk::Image> images = {},
        std::span<const vk::Buffer> buffers = {});

    void clearFrameBoundary() noexcept;

    [[nodiscard]] bool hasFrameBoundary() const noexcept;

    [[nodiscard]] std::optional<std::uint64_t> frameBoundaryFrameID() const noexcept;

    /**
     * @brief Clear all command buffers and synchronization
     * 
     * Resets batch to empty state
     */
    void clear() noexcept;

    /**
     * @brief Check if batch is empty (no command buffers)
     */
    [[nodiscard]] bool empty() const noexcept;

    /**
     * @brief Get number of command buffers in batch
     */
    [[nodiscard]] std::size_t commandBufferCount() const noexcept;

    /**
    * @brief Build a SubmitInfo2 packet from this batch.
    * @return Packet owning submit arrays and `vk::SubmitInfo2` view.
     * 
     * CommandBatch is a passive data container.
     * Use batch::submit() for actual submission.
     */
    [[nodiscard]] SubmitInfo2Packet buildSubmitInfo2() const;

    // ========== Builder-style interface ==========

    /**
     * @brief Builder-style: add command buffer
     */
    CommandBatch& withCommandBuffer(const vk::raii::CommandBuffer& cb);

    /**
     * @brief Builder-style: add wait semaphore
     */
    CommandBatch& withWait(const vk::raii::Semaphore& sem, vk::PipelineStageFlags2 stage, std::uint64_t value = 0, std::uint32_t deviceIndex = 0);

    /**
     * @brief Builder-style: add signal semaphore
     */
    CommandBatch& withSignal(const vk::raii::Semaphore& sem, std::uint64_t value = 0, std::uint32_t deviceIndex = 0, vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands);

private:
    struct FrameBoundaryMetadata
    {
        std::uint64_t frameID = 0;
        vk::FrameBoundaryFlagsEXT flags{};
        std::vector<vk::Image> images{};
        std::vector<vk::Buffer> buffers{};
    };

    // Store raw handles internally (non-owning view extracted from RAII objects)
    std::vector<vk::CommandBuffer> commandBuffers_;
    std::vector<SemaphoreSyncPoint> waitPoints_;
    std::vector<SemaphoreSyncPoint> signalPoints_;
    std::optional<FrameBoundaryMetadata> frameBoundary_;
};

/**
 * @brief Helper functions for common batch operations
 */
namespace batch {

/**
 * @brief Create a simple single-command batch
 * @param commandBuffer RAII command buffer
 */
[[nodiscard]] CommandBatch single(
    const vk::raii::CommandBuffer& commandBuffer
);

/**
 * @brief Create graphics batch with typical present sync
 * @param commandBuffer RAII graphics command buffer
 * @param imageAvailable RAII semaphore from swapchain acquire
 * @param renderFinished RAII semaphore to signal for present
 */
[[nodiscard]] CommandBatch graphics(
    const vk::raii::CommandBuffer& commandBuffer,
    const vk::raii::Semaphore& imageAvailable,
    const vk::raii::Semaphore& renderFinished
);

} // namespace batch

} // namespace nr::rhi
