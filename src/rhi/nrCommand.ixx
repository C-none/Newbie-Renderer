module;
export module nr.rhi:command;
import dependency;
import std;

export namespace nr::rhi {

/**
 * @brief Lightweight helper for command buffer recording
 * 
 * Design Philosophy:
 * - Stateless utility class (no state stored)
 * - Thin wrapper around vkBeginCommandBuffer / vkEndCommandBuffer
 * - Works exclusively with vk::raii::CommandBuffer
 * - Does NOT own command buffers
 * - Does NOT know about pools or queues
 * 
 * Usage:
 *   auto cb = pool.allocatePrimaryRaii();
 *   CommandRecorder::beginPrimary(cb);
 *   // ... vkCmd* calls ...
 *   CommandRecorder::end(cb);
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
    ) {
        vk::CommandBufferBeginInfo beginInfo{flags};
        commandBuffer.begin(beginInfo);
    }

    /**
     * @brief Begin recording secondary command buffer
     * @param commandBuffer RAII command buffer to begin
     * @param inheritanceInfo Render pass inheritance information
     * @param flags Optional begin flags
     * 
     * Secondary command buffers inherit state from primary and can be
     * executed via vkCmdExecuteCommands
     */
    static void beginSecondary(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue
    ) {
        vk::CommandBufferBeginInfo beginInfo{flags, &inheritanceInfo};
        commandBuffer.begin(beginInfo);
    }

    /**
     * @brief Begin secondary with no inheritance
     * @param commandBuffer RAII command buffer to begin
     * @param flags Optional begin flags
     */
    static void beginSecondaryGeneral(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferUsageFlags flags = {}
    ) {
        vk::CommandBufferInheritanceInfo inheritanceInfo{};
        vk::CommandBufferBeginInfo beginInfo{flags, &inheritanceInfo};
        commandBuffer.begin(beginInfo);
    }

    /**
     * @brief End command buffer recording
     * @param commandBuffer RAII command buffer to end
     */
    static void end(const vk::raii::CommandBuffer& commandBuffer) {
        commandBuffer.end();
    }

    /**
     * @brief Reset single command buffer
     * @param commandBuffer RAII command buffer to reset
     * @param flags Reset flags (e.g., RELEASE_RESOURCES)
     * 
     * Note: Command buffer must have been allocated with RESET_COMMAND_BUFFER flag
     */
    static void reset(
        const vk::raii::CommandBuffer& commandBuffer,
        vk::CommandBufferResetFlags flags = {}
    ) {
        commandBuffer.reset(flags);
    }

    /**
     * @brief Execute secondary command buffers in a primary buffer
     * @param primary RAII primary command buffer (must be in recording state)
     * @param secondaries Span of RAII secondary command buffers to execute
     * 
     * Merges multiple secondary command buffers into the primary buffer.
     * This is the standard Vulkan way to combine independently recorded
     * secondary buffers (e.g., from parallel worker threads).
     * 
     * Example:
     *   std::vector<vk::raii::CommandBuffer> secondaries = {sec1, sec2};
     *   CommandRecorder::executeSecondaries(primary, secondaries);
     */
    static void executeSecondaries(
        const vk::raii::CommandBuffer& primary,
        std::span<const vk::raii::CommandBuffer> secondaries
    ) {
        if (!secondaries.empty()) {
            std::vector<vk::CommandBuffer> handles;
            handles.reserve(secondaries.size());
            for (const auto& sec : secondaries) {
                handles.push_back(*sec);
            }
            (*primary).executeCommands(handles);
        }
    }
};

/**
 * @brief RAII wrapper for command buffer RECORDING scope (begin/end)
 * 
 * IMPORTANT: This is different from vk::raii::CommandBuffer!
 * - vk::raii::CommandBuffer: Manages MEMORY lifetime (allocate/free)
 * - ScopedCommandBuffer: Manages RECORDING scope (begin/end)
 * 
 * Usage with RAII command buffer:
 *   vk::raii::CommandBuffer cb = pool.allocatePrimaryRaii(); // RAII for memory
 *   {
 *       ScopedCommandBuffer scoped(cb, flags);  // RAII for recording
 *       // ... vkCmd* calls ...
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
    ) : commandBuffer_(commandBuffer)
    {
        CommandRecorder::beginPrimary(commandBuffer, flags);
    }

    /**
     * @brief Begin secondary command buffer, end on destruction
     * @param commandBuffer RAII command buffer
     * @param inheritanceInfo Render pass inheritance information
     * @param flags Recording flags
     */
    ScopedCommandBuffer(
        const vk::raii::CommandBuffer& commandBuffer,
        const vk::CommandBufferInheritanceInfo& inheritanceInfo,
        vk::CommandBufferUsageFlags flags = vk::CommandBufferUsageFlagBits::eRenderPassContinue
    ) : commandBuffer_(commandBuffer)
    {
        CommandRecorder::beginSecondary(commandBuffer, inheritanceInfo, flags);
    }

    ~ScopedCommandBuffer() {
        // Reference is always valid (bound at construction)
        commandBuffer_.end();
    }

    // Non-copyable, non-movable (RAII lifetime bound to scope)
    ScopedCommandBuffer(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer& operator=(const ScopedCommandBuffer&) = delete;
    ScopedCommandBuffer(ScopedCommandBuffer&&) = delete;
    ScopedCommandBuffer& operator=(ScopedCommandBuffer&&) = delete;

    /**
     * @brief Get the wrapped command buffer for recording commands
     */
    [[nodiscard]] const vk::raii::CommandBuffer& get() const noexcept { return commandBuffer_; }

    /**
     * @brief Implicit conversion to vk::CommandBuffer for convenience
     */
    [[nodiscard]] operator vk::CommandBuffer() const noexcept { return *commandBuffer_; }

private:
    const vk::raii::CommandBuffer& commandBuffer_;
};

/**
 * @brief Helper functions for common command buffer patterns
 * 
 * These are convenience functions that combine begin/record/end in one call.
 * Use when you don't need exception safety (no early returns or throws).
 * For exception safety, prefer ScopedCommandBuffer.
 */
namespace cmd {

/**
 * @brief Execute a lambda with automatic begin/end
 * @param commandBuffer RAII command buffer to record into
 * @param flags Begin flags
 * @param recordFunc Lambda taking vk::CommandBuffer (raw handle for recording)
 * 
 * Note: NOT exception-safe! If recordFunc throws, end() won't be called.
 * For exception safety, use ScopedCommandBuffer instead:
 *   {
 *       ScopedCommandBuffer scoped(cb, flags);
 *       // ... your code, can throw ...
 *   }
 * 
 * Example:
 *   cmd::record(cb, {}, [](vk::CommandBuffer cmd) {
 *       cmd.copyBuffer(...);
 *   });
 */
template<typename Func>
void record(
    const vk::raii::CommandBuffer& commandBuffer,
    vk::CommandBufferUsageFlags flags,
    Func&& recordFunc
) {
    CommandRecorder::beginPrimary(commandBuffer, flags);
    std::forward<Func>(recordFunc)(*commandBuffer);
    CommandRecorder::end(commandBuffer);
}

/**
 * @brief Execute a lambda for secondary buffer with inheritance
 * @param commandBuffer RAII command buffer
 * @param inheritanceInfo Render pass inheritance
 * @param flags Begin flags
 * @param recordFunc Lambda taking vk::CommandBuffer
 */
template<typename Func>
void recordSecondary(
    const vk::raii::CommandBuffer& commandBuffer,
    const vk::CommandBufferInheritanceInfo& inheritanceInfo,
    vk::CommandBufferUsageFlags flags,
    Func&& recordFunc
) {
    CommandRecorder::beginSecondary(commandBuffer, inheritanceInfo, flags);
    std::forward<Func>(recordFunc)(*commandBuffer);
    CommandRecorder::end(commandBuffer);
}

/**
 * @brief Record primary buffer that executes secondary command buffers
 * @param primary RAII primary command buffer
 * @param secondaries Span of RAII secondary command buffers
 * @param flags Begin flags
 * @param beforeExecute Optional callback called before executeCommands
 * 
 * This is a convenience function that combines the common pattern of:
 * 1. Begin recording primary
 * 2. (optionally) Record other commands
 * 3. Execute secondary buffers
 * 4. End recording
 * 
 * Usage:
 *   std::vector<vk::raii::CommandBuffer> secondaries = {sec1, sec2};
 *   cmd::recordWithSecondaries(primary, secondaries, {}, [](vk::CommandBuffer cb) {
 *       cb.setViewport(...);
 *       cb.setScissor(...);
 *   });
 */
template<typename Func = decltype([](vk::CommandBuffer) {})>
void recordWithSecondaries(
    const vk::raii::CommandBuffer& primary,
    std::span<const vk::raii::CommandBuffer> secondaries,
    vk::CommandBufferUsageFlags flags = {},
    Func&& beforeExecute = [](vk::CommandBuffer) {}
) {
    CommandRecorder::beginPrimary(primary, flags);
    std::forward<Func>(beforeExecute)(*primary);
    CommandRecorder::executeSecondaries(primary, secondaries);
    CommandRecorder::end(primary);
}

} // namespace cmd

} // namespace nr::rhi
