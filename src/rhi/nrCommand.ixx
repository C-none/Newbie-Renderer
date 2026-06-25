export module nr.rhi:command;
import dependency.vulkan;
import nr.utils;
import std;

export namespace nr::rhi {

/**
 * @brief Lightweight helper for command buffer recording
 * 
 * Design Philosophy:
 * - Stateless utility class (no state stored)
 * - Thin wrapper around vk::raii::CommandBuffer begin/end
 * - Works exclusively with vk::raii::CommandBuffer
 * - Does NOT own command buffers
 * - Does NOT know about pools or queues
 * 
 * Usage:
 *   auto cb = pool.allocatePrimary();
 *   CommandRecorder::beginPrimary(cb.front());
 *   // ... vk::raii::CommandBuffer member calls ...
 *   CommandRecorder::end(cb.front());
 */
class CommandRecorder {
public:
    // Stateless - all methods are static

    /**
     * @brief Begin recording primary command buffer
     * @param commandBuffer RAII command buffer to begin
     * @param flags Optional begin flags (e.g., ONE_TIME_SUBMIT)
     */
    static void beginPrimary(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferUsageFlags flags = {}
    );

    /**
     * @brief Begin recording secondary command buffer
     * @param commandBuffer RAII command buffer to begin
     * @param inheritanceInfo Render pass inheritance information
     * @param flags Begin flags provided by caller
     * 
     * Secondary command buffers inherit state from primary and can be
     * executed through vk::raii::CommandBuffer::executeCommands.
     */
    static void beginSecondary(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags={}
    );

    /**
     * @brief End command buffer recording
     * @param commandBuffer RAII command buffer to end
     */
    static void end(const vk::raii::CommandBuffer& commandBuffer);
};

/**
 * @brief RAII wrapper for command buffer RECORDING scope (begin/end)
 * 
 * IMPORTANT: This is different from vk::raii::CommandBuffer!
 * - vk::raii::CommandBuffer: Manages MEMORY lifetime (allocate/free)
 * - ScopedCommandBuffer: Manages RECORDING scope (begin/end)
 * 
 * Usage with RAII command buffer:
 *   auto commandBuffers = pool.allocatePrimary(1); // RAII for memory
 *   const auto& cb = commandBuffers.front();
 *   {
 *       ScopedCommandBuffer scoped(cb, flags);  // RAII for recording
 *       // ... vk::raii::CommandBuffer member calls ...
 *   } // Automatically calls end()
 *   // cb still valid, can be submitted
 */
class ScopedCommandBuffer {
public:
    /**
     * @brief Begin primary command buffer, end on destruction
     * @param commandBuffer RAII command buffer (manages memory lifetime)
     * @param flags Recording flags (e.g., ONE_TIME_SUBMIT for single use)
     * 
     * Usage - combines both RAII types:
     *   vk::raii::CommandBuffer cb = ...;  // memory RAII
     *   ScopedCommandBuffer scoped(cb);    // recording RAII
     */
    explicit ScopedCommandBuffer(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferUsageFlags flags = {}
    );

    /**
     * @brief Begin secondary command buffer, end on destruction
     * @param commandBuffer RAII command buffer
     * @param inheritanceInfo Render pass inheritance information
     * @param flags Recording flags provided by caller
     */
    ScopedCommandBuffer(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags={}
    );

    ~ScopedCommandBuffer();

    // Non-copyable, non-movable (RAII lifetime bound to scope)
    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer(ScopedCommandBuffer&&) = delete;
    ScopedCommandBuffer& operator=(ScopedCommandBuffer&&) = delete;

    /**
     * @brief Get the wrapped command buffer for recording commands
     */
    [[nodiscard]] const vk::raii::CommandBuffer& get() const noexcept;

private:
    const vk::raii::CommandBuffer& commandBuffer_;
};

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
class ScopedCommandBufferDebugLabel {
public:
    ScopedCommandBufferDebugLabel(const vk::raii::CommandBuffer& commandBuffer, std::string_view label);

    ~ScopedCommandBufferDebugLabel();

    // Non-copyable, non-movable (RAII lifetime bound to scope)
    ScopedCommandBufferDebugLabel(const ScopedCommandBufferDebugLabel&) = delete;
    ScopedCommandBufferDebugLabel& operator=(const ScopedCommandBufferDebugLabel&) = delete;
    ScopedCommandBufferDebugLabel(ScopedCommandBufferDebugLabel&&) = delete;
    ScopedCommandBufferDebugLabel& operator=(ScopedCommandBufferDebugLabel&&) = delete;

    void close();

private:
    std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer_{};
    std::string label_{};
    bool active_ = false;
};

} // namespace nr::rhi
