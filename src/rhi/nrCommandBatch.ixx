module;
export module nr.rhi:commandBatch;
import dependency;
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
 *   batch.addWait(imageAvailable, vk::PipelineStageFlagBits::eColorAttachmentOutput);
 *   batch.addSignal(renderFinished);
 *   queue.submit(batch, &fence);
 */
class CommandBatch {
public:
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
    void addCommandBuffer(const vk::raii::CommandBuffer& commandBuffer) {
        commandBuffers_.push_back(*commandBuffer);
    }

    /**
     * @brief Add multiple command buffers to the batch
     * @param commandBuffers Span of RAII command buffers
     */
    void addCommandBuffers(std::span<const vk::raii::CommandBuffer> commandBuffers) {
        commandBuffers_.reserve(commandBuffers_.size() + commandBuffers.size());
        for (const auto& cb : commandBuffers) {
            commandBuffers_.push_back(*cb);
        }
    }

    /**
     * @brief Add a wait semaphore with pipeline stage
     * @param semaphore RAII semaphore to wait on
     * @param stage Pipeline stage to wait at
     * 
     * Queue will wait for semaphore before executing commands
     */
    void addWait(const vk::raii::Semaphore& semaphore, vk::PipelineStageFlags stage) {
        waitSemaphores_.push_back(*semaphore);
        waitStages_.push_back(stage);
    }

    /**
     * @brief Add a signal semaphore
     * @param semaphore RAII semaphore to signal after execution
     * 
     * Queue will signal semaphore when commands complete
     */
    void addSignal(const vk::raii::Semaphore& semaphore) {
        signalSemaphores_.push_back(*semaphore);
    }

    /**
     * @brief Clear all command buffers and synchronization
     * 
     * Resets batch to empty state
     */
    void clear() noexcept {
        commandBuffers_.clear();
        waitSemaphores_.clear();
        waitStages_.clear();
        signalSemaphores_.clear();
    }

    /**
     * @brief Check if batch is empty (no command buffers)
     */
    [[nodiscard]] bool empty() const noexcept { return commandBuffers_.empty(); }

    /**
     * @brief Get number of command buffers in batch
     */
    [[nodiscard]] size_t commandBufferCount() const noexcept { return commandBuffers_.size(); }

    /**
     * @brief Build a SubmitInfo from this batch
     * @return vk::SubmitInfo ready for queue submission
     * 
     * CommandBatch is a passive data container.
     * Use batch::submit() for actual submission.
     */
    [[nodiscard]] vk::SubmitInfo buildSubmitInfo() const noexcept {
        return vk::SubmitInfo{waitSemaphores_, waitStages_, commandBuffers_, signalSemaphores_};
    }

    // ========== Builder-style interface ==========

    /**
     * @brief Builder-style: add command buffer
     */
    CommandBatch& withCommandBuffer(const vk::raii::CommandBuffer& cb) {
        addCommandBuffer(cb);
        return *this;
    }

    /**
     * @brief Builder-style: add wait semaphore
     */
    CommandBatch& withWait(const vk::raii::Semaphore& sem, vk::PipelineStageFlags stage) {
        addWait(sem, stage);
        return *this;
    }

    /**
     * @brief Builder-style: add signal semaphore
     */
    CommandBatch& withSignal(const vk::raii::Semaphore& sem) {
        addSignal(sem);
        return *this;
    }

private:
    // Store raw handles internally (non-owning view extracted from RAII objects)
    std::vector<vk::CommandBuffer> commandBuffers_;
    std::vector<vk::Semaphore> waitSemaphores_;
    std::vector<vk::PipelineStageFlags> waitStages_;
    std::vector<vk::Semaphore> signalSemaphores_;
};

/**
 * @brief Helper functions for common batch operations
 */
namespace batch {

/**
 * @brief Create a simple single-command batch
 * @param commandBuffer RAII command buffer
 */
[[nodiscard]] inline CommandBatch single(
    const vk::raii::CommandBuffer& commandBuffer
) {
    CommandBatch batch{};
    batch.addCommandBuffer(commandBuffer);
    return batch;
}

/**
 * @brief Create graphics batch with typical present sync
 * @param commandBuffer RAII graphics command buffer
 * @param imageAvailable RAII semaphore from swapchain acquire
 * @param renderFinished RAII semaphore to signal for present
 */
[[nodiscard]] inline CommandBatch graphics(
    const vk::raii::CommandBuffer& commandBuffer,
    const vk::raii::Semaphore& imageAvailable,
    const vk::raii::Semaphore& renderFinished
) {
    CommandBatch batch{};
    batch.addCommandBuffer(commandBuffer);
    batch.addWait(imageAvailable, vk::PipelineStageFlagBits::eColorAttachmentOutput);
    batch.addSignal(renderFinished);
    return batch;
}

} // namespace batch

} // namespace nr::rhi
