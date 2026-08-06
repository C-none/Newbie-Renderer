export module nr.rhi:commandPool;
import dependency.vulkan;
import std;

export namespace nr::rhi
{

/**
 * @brief RAII wrapper for Vulkan command pool
 *
 * Responsibilities:
 * - Allocate primary and secondary command buffers
 * - Reset the entire pool (recycles all buffers)
 * - Does NOT know about queue types (assigned at creation)
 * - Does NOT know about frame context (used by FrameContext)
 *
 * Returns vk::raii::CommandBuffer exclusively:
 * - Caller owns the buffer lifetime
 * - Buffer automatically freed when RAII object destroyed
 * - Pool reset() invalidates all buffers from this pool
 *
 * Lifecycle:
 * - Created per-frame, per-queue, per-level
 * - Reset when frame fence is signaled
 */
class CommandPool
{
  public:
    /// Default constructor for deferred initialization
    CommandPool() = default;

    /**
     * @brief Construct command pool for specific queue family
     * @param device Vulkan device
     * @param queueFamilyIndex Queue family this pool belongs to
     * @param flags Pool creation flags (e.g., TRANSIENT, RESET_COMMAND_BUFFER)
     */
    CommandPool(const vk::raii::Device &device, std::uint32_t queueFamilyIndex, vk::CommandPoolCreateFlags flags = {});

    // Move-only RAII semantics (following vulkan-hpp pattern)
    CommandPool(const CommandPool &) = delete;
    CommandPool &operator=(const CommandPool &) = delete;
    CommandPool(CommandPool &&) noexcept = default;
    CommandPool &operator=(CommandPool &&) noexcept = default;

    /**
     * @brief Allocate multiple primary command buffers
     * @param count Number of buffers to allocate
     * @return RAII command buffers container
     */
    [[nodiscard]] vk::raii::CommandBuffers allocatePrimary(std::uint32_t count = 1);

    /**
     * @brief Allocate multiple secondary command buffers
     * @param count Number of buffers to allocate
     * @return RAII command buffers container
     */
    [[nodiscard]] vk::raii::CommandBuffers allocateSecondary(std::uint32_t count = 1);

    /**
     * @brief Reset all command buffers in this pool
     * @param flags Reset flags (e.g., RELEASE_RESOURCES)
     *
     * Warning: Invalidates ALL command buffers allocated from this pool!
     * Only call when all submitted command buffers have finished execution.
     */
    void reset(vk::CommandPoolResetFlags flags = {});

    /**
     * @brief Check if pool is valid (initialized)
     */
    [[nodiscard]] bool valid() const noexcept;

  private:
    vk::raii::CommandPool pool_ = {nullptr};
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
};

} // namespace nr::rhi
