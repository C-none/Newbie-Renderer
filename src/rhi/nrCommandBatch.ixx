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
 *   batch.addWait(waitSemaphore, vk::PipelineStageFlagBits2::eAllCommands, waitValue);
 *   batch.addSignal(signalSemaphore, signalValue);
 *   queue.submit(std::move(batch), std::cref(fence));
 */
class CommandBatch
{
  public:
    /**
     * @brief Construct empty batch
     */
    CommandBatch() noexcept = default;

    // A batch describes one submission and must be consumed explicitly.
    CommandBatch(const CommandBatch &) = delete;
    CommandBatch &operator=(const CommandBatch &) = delete;
    CommandBatch(CommandBatch &&) noexcept = default;
    CommandBatch &operator=(CommandBatch &&) noexcept = default;

    /**
     * @brief Add a command buffer to the batch
     * @param commandBuffer RAII command buffer (handle extracted automatically)
     */
    void addCommandBuffer(const vk::raii::CommandBuffer &commandBuffer);

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
        * This does not perform image layout transitions. Queue-family ownership
        * barriers are still required unless the device transfer policy, such as
        * VK_KHR_maintenance9, makes the specific transfer optional.
     */
    void addWait(const vk::raii::Semaphore &semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value = 0,
                 std::uint32_t deviceIndex = 0);

    /**
     * @brief Add a raw wait semaphore sync point.
     */
    void addWait(vk::Semaphore semaphore, vk::PipelineStageFlags2 stage, std::uint64_t value = 0,
                 std::uint32_t deviceIndex = 0);

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
    void addSignal(const vk::raii::Semaphore &semaphore, std::uint64_t value = 0, std::uint32_t deviceIndex = 0,
                   vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands);

    /**
     * @brief Add a raw signal semaphore sync point.
     */
    void addSignal(vk::Semaphore semaphore, std::uint64_t value = 0, std::uint32_t deviceIndex = 0,
                   vk::PipelineStageFlags2 stage = vk::PipelineStageFlagBits2::eAllCommands);

    /**
     * @brief Attach optional VK_EXT_frame_boundary metadata to the next submit.
     *
     * The extension is debugger-facing metadata. CommandBatch owns copies of the
     * image/buffer handle arrays so a submit-time FrameBoundaryEXT view can safely
     * expose pNext pointers for the duration of queue submission.
     */
    void setFrameBoundary(std::uint64_t frameID, vk::FrameBoundaryFlagsEXT flags = {},
                          std::span<const vk::Image> images = {}, std::span<const vk::Buffer> buffers = {});

    [[nodiscard]] std::optional<std::uint64_t> frameBoundaryFrameID() const noexcept;

    [[nodiscard]] std::optional<vk::FrameBoundaryEXT> frameBoundarySubmitInfo() const;

    [[nodiscard]] vk::SubmitInfo2 submitInfo2View(const vk::FrameBoundaryEXT *frameBoundary = nullptr) const noexcept;

  private:
    struct FrameBoundaryMetadata
    {
        std::uint64_t frameID = 0;
        vk::FrameBoundaryFlagsEXT flags{};
        std::vector<vk::Image> images{};
        std::vector<vk::Buffer> buffers{};
    };

    std::vector<vk::CommandBufferSubmitInfo> commandBufferInfos_;
    std::vector<vk::SemaphoreSubmitInfo> waitInfos_;
    std::vector<vk::SemaphoreSubmitInfo> signalInfos_;
    std::optional<FrameBoundaryMetadata> frameBoundary_;
};

} // namespace nr::rhi
