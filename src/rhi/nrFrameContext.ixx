export module nr.rhi:frameContext;
import dependency;
import nr.utils;
import std;
import :commandPool;
import :type;

export namespace nr::rhi
{

/**
 * @brief Per-frame resources and synchronization primitives
 *
 * Design Philosophy:
 * - FrameContext = reset boundary
 * - One context per frame-in-flight (typically 2-3)
 * - Owns all per-frame command pools and sync primitives
 * - Reset only when fence is signaled
 *
 * ============================================================================
 * MULTI-THREADED RECORDING WITH FRAME-BEGIN PREBUILD
 * ============================================================================
 *
 * Thread Safety Model:
 * - Primary pools: Main thread only (no synchronization needed)
 * - Secondary pools: Prebuilt at frame-begin, then thread-local access only
 *
 * How Prebuilt Secondary Pools Work:
 *
 * 1. Each FrameContext maintains fixed compile-time slots per queue type
 *    Example: graphicsSecondary_ = array<optional<CommandPool>, maxThreads>
 *
 * 2. beginFrame calls prepareSecondaryPools() on the main thread.
 *    Missing slots [0, maxThreads) are created before worker recording starts.
 *
 * 3. Worker threads call frame.secondary<T>(threadId)
 *    - No locks
 *    - No growth
 *    - O(1) index access with bounds assertions
 *
 * 4. Each thread uses its own pool index (threadId), so no contention
 *    during command recording.
 *
 * Usage Pattern:
 *
 *   // Main thread: dispatch work to thread pool
 *   std::vector<std::future<vk::raii::CommandBuffer>> futures;
 *   for (std::size_t i = 0; i < threadPool.size(); ++i) {
 *       futures.push_back(threadPool.submit([&, threadId = i]() {
 *           // Pools are prebuilt at frame begin; this is lock-free indexed access.
 *           CommandPool& pool = frame.secondary<QueueRole::Graphics>(threadId);
 *           auto cb = pool.allocateSecondary();
 *
 *           // Record commands (no locks needed here!)
 *           CommandRecorder::beginSecondary(cb, inheritanceInfo);
 *           recordDrawCalls(cb, ...);
 *           CommandRecorder::end(cb);
 *
 *           return cb;
 *       }));
 *   }
 *
 *   // Collect results
 *   std::vector<std::reference_wrapper<const vk::raii::CommandBuffer>> secondaries;
 *   for (auto& f : futures) secondaries.push_back(f.get());
 *
 *   // Main thread: build primary and execute secondaries
 *   auto primary = frame.graphicsPrimary().allocatePrimary();
 *   CommandRecorder::beginPrimary(primary);
 *   primary.executeCommands(...);
 *   CommandRecorder::end(primary);
 *
 * ============================================================================
 */
class FrameContext
{
  public:
        /// Compile-time upper bound for worker-secondary pools on amd64 builds.
        inline static constexpr std::uint32_t kMaxSecondaryWorkers = nr::maxThreads;

    /**
     * @brief Configuration for per-queue command pools
     */
    struct PoolConfig
    {
        std::uint32_t queueFamilyIndex;
    };

    /// Default constructor for deferred initialization
    FrameContext() = default;

    /**
     * @brief Construct frame context with queue-specific pools
     * @param device Vulkan device
     * @param graphicsConfig Graphics queue pool configuration
     * @param computeConfig Compute queue pool configuration
     * @param transferConfig Dedicated transfer queue pool configuration
     */
    FrameContext(const vk::raii::Device &device, const PoolConfig &graphicsConfig, const PoolConfig &computeConfig, const PoolConfig &transferConfig)
        : device_(std::ref(device)), graphicsQueueFamily_(graphicsConfig.queueFamilyIndex), computeQueueFamily_(computeConfig.queueFamilyIndex), transferQueueFamily_(transferConfig.queueFamilyIndex)
    {
        // Create fence (signaled initially so first frame doesn't wait)
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        fence_ = vk::raii::Fence(device, fenceInfo);

        // renderFinished semaphore is owned here; imageAvailable is borrowed from
        // the PresentationContext acquire pool and injected at frame-begin.
        vk::SemaphoreCreateInfo semaphoreInfo{};
        renderFinished_ = vk::raii::Semaphore(device, semaphoreInfo);

        // Create primary pools (one per queue type, main thread only).
        // Primary command buffers are retained and individually reset every frame,
        // so pools must allow vkResetCommandBuffer.
        constexpr auto primaryPoolFlags = vk::CommandPoolCreateFlagBits::eTransient |
                                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        graphicsPrimary_ = CommandPool(device, graphicsConfig.queueFamilyIndex, primaryPoolFlags);

        computePrimary_ = CommandPool(device, computeConfig.queueFamilyIndex, primaryPoolFlags);

        transferPrimary_ = CommandPool(device, transferConfig.queueFamilyIndex, primaryPoolFlags);
    }

    // Move-only semantics
    FrameContext(const FrameContext &) = delete;
    FrameContext &operator=(const FrameContext &) = delete;
    FrameContext(FrameContext &&other) noexcept
    {
        moveFrom(std::move(other));
    }

    FrameContext &operator=(FrameContext &&other) noexcept
    {
        if (this != &other)
        {
            moveFrom(std::move(other));
        }
        return *this;
    }

    /**
     * @brief Wait for this frame's fence to be signaled
     * @param timeout Timeout in nanoseconds (default: 10 second)
     * @return true if fence signaled, false if timeout
     */
    [[nodiscard]] bool waitForFence(std::uint64_t timeout = 10'000'000'000)
    {
        auto result = device_->get().waitForFences(*fence_, vk::True, timeout);
        return result == vk::Result::eSuccess;
    }

    /**
     * @brief Reset fence to unsignaled state
     *
     * Must be called before reusing this frame context
     */
    void resetFence()
    {
        device_->get().resetFences(*fence_);
    }

    /**
     * @brief Reset all command pools in this context
     *
     * Only call after fence is signaled. Invalidates all command buffers.
     */
    void resetPools()
    {
        // Retained primary command buffers are individually reset by their owner
        // after this frame's fence is signaled. Reset only transient secondary pools.
        resetPreparedSecondaryPools(graphicsSecondary_, graphicsPreparedSecondaryWorkers_);
        resetPreparedSecondaryPools(computeSecondary_, computePreparedSecondaryWorkers_);
        resetPreparedSecondaryPools(transferSecondary_, transferPreparedSecondaryWorkers_);
    }

    /**
     * @brief Prebuild secondary command pools for the next recording window.
     *
     * Call from frame-begin on the main thread before worker recording starts.
     * Recording-time access (`secondary<T>()`) never grows storage or acquires locks.
     */
    void prepareSecondaryPools(std::uint32_t graphicsWorkerCount = kMaxSecondaryWorkers, std::uint32_t computeWorkerCount = kMaxSecondaryWorkers, std::uint32_t transferWorkerCount = 0)
    {
        graphicsPreparedSecondaryWorkers_ = std::min(graphicsWorkerCount, kMaxSecondaryWorkers);
        computePreparedSecondaryWorkers_ = std::min(computeWorkerCount, kMaxSecondaryWorkers);

        prepareQueueSecondaryPools(graphicsSecondary_, graphicsQueueFamily_, graphicsPreparedSecondaryWorkers_);
        prepareQueueSecondaryPools(computeSecondary_, computeQueueFamily_, computePreparedSecondaryWorkers_);

        transferPreparedSecondaryWorkers_ = std::min(transferWorkerCount, kMaxSecondaryWorkers);
        if (transferPreparedSecondaryWorkers_ > 0)
        {
            prepareQueueSecondaryPools(transferSecondary_, transferQueueFamily_, transferPreparedSecondaryWorkers_);
        }
    }

    /**
     * @brief Get primary command pool for queue
     */
    template <QueueRole T> [[nodiscard]] CommandPool &primary() noexcept
    {
        return primaryPool<T>();
    }

    /**
     * @brief Get secondary command pool for queue (thread-local)
     * @param threadId Thread identifier (0-based, typically from thread pool)
     * @return Reference to thread's secondary pool
    * Access is lock-free and does not allocate at record time.
    * All slots are prepared by prepareSecondaryPools() at frame-begin.
     */
    template <QueueRole T> [[nodiscard]] CommandPool &secondary(std::size_t threadId)
    {
        static_assert(isSupportedQueueRole<T>(), "Unsupported QueueRole template argument");
        const auto preparedWorkers = preparedSecondaryWorkers<T>();
        auto &slots = secondarySlots<T>();
        nrAssert(
            threadId < preparedWorkers,
            std::format("FrameContext::secondary {} threadId {} out of prepared range {}", queueRoleName<T>(), threadId, preparedWorkers));
        nrAssert(slots[threadId].has_value(), secondaryPoolNotPreparedMessage<T>());
        return *slots[threadId];
    }

    // ========== Synchronization primitives ==========

    /**
     * @brief Get the fence for this frame
     * @return Reference to vk::raii::Fence
     */
    [[nodiscard]] const vk::raii::Fence &fence() const noexcept
    {
        return fence_;
    }

    /**
     * @brief Inject the pre-acquired image-available semaphore for this frame.
     *
     * Called by Device::beginFrame() after consuming the pending acquire from
     * PresentationContext. The semaphore is owned by the acquire pool; this frame
     * holds a non-owning pointer valid until Device::beginFrame() calls
     * PresentationContext::returnAcquireSemaphore() on the next cycle.
     */
    void setBorrowedAcquireSemaphore(const vk::raii::Semaphore *semaphore) noexcept
    {
        borrowedAcquireSemaphore_ = semaphore;
    }

    /**
     * @brief Get the image-available semaphore borrowed from the acquire pool.
     */
    [[nodiscard]] const vk::raii::Semaphore &imageAvailable() const noexcept
    {
        nrAssert(borrowedAcquireSemaphore_ != nullptr, "FrameContext::imageAvailable requires a borrowed acquire semaphore (call setBorrowedAcquireSemaphore first).");
        return *borrowedAcquireSemaphore_;
    }

    /**
     * @brief Get render finished semaphore (signaled after rendering for present)
     * @return Reference to vk::raii::Semaphore
     */
    [[nodiscard]] const vk::raii::Semaphore &renderFinished() const noexcept
    {
        return renderFinished_;
    }

    // ========== Status queries ==========

    /**
     * @brief Check if fence is currently signaled
     */
    [[nodiscard]] bool isFenceSignaled() const
    {
        return fence_.getStatus() == vk::Result::eSuccess;
    }

    /**
     * @brief Get number of registered secondary pools
     */
    template <QueueRole T> [[nodiscard]] std::size_t registeredThreads() const noexcept
    {
        return preparedSecondaryWorkers<T>();
    }

  private:
    void moveFrom(FrameContext &&other) noexcept
    {
        device_ = std::move(other.device_);
        graphicsQueueFamily_ = other.graphicsQueueFamily_;
        computeQueueFamily_ = other.computeQueueFamily_;
        transferQueueFamily_ = other.transferQueueFamily_;

        fence_ = std::move(other.fence_);
        borrowedAcquireSemaphore_ = other.borrowedAcquireSemaphore_;
        other.borrowedAcquireSemaphore_ = nullptr;
        renderFinished_ = std::move(other.renderFinished_);

        graphicsPrimary_ = std::move(other.graphicsPrimary_);
        graphicsSecondary_ = std::move(other.graphicsSecondary_);

        computePrimary_ = std::move(other.computePrimary_);
        computeSecondary_ = std::move(other.computeSecondary_);

        transferPrimary_ = std::move(other.transferPrimary_);
        transferSecondary_ = std::move(other.transferSecondary_);

        graphicsPreparedSecondaryWorkers_ = other.graphicsPreparedSecondaryWorkers_;
        computePreparedSecondaryWorkers_ = other.computePreparedSecondaryWorkers_;
        transferPreparedSecondaryWorkers_ = other.transferPreparedSecondaryWorkers_;
    }

    /**
     * @brief Ensure queue-specific secondary pool slots are materialized for [0, workerCount).
     */
    static void resetPreparedSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots, std::uint32_t preparedWorkerCount)
    {
        std::ranges::for_each(slots | std::views::take(preparedWorkerCount), [](std::optional<CommandPool> &pool) {
            if (pool.has_value())
                pool->reset();
        });
    }

    void prepareQueueSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots, std::uint32_t queueFamilyIndex, std::uint32_t workerCount)
    {
        auto slotIndices = std::views::iota(std::uint32_t{0}, workerCount);
        std::ranges::for_each(slotIndices, [&](std::uint32_t slotIndex) {
            if (!slots[slotIndex].has_value())
            {
                slots[slotIndex].emplace(device_->get(), queueFamilyIndex, vk::CommandPoolCreateFlagBits::eTransient);
            }
        });
    }

    template <QueueRole T> [[nodiscard]] static consteval bool isSupportedQueueRole() noexcept
    {
        return T == QueueRole::Graphics || T == QueueRole::Compute || T == QueueRole::Transfer;
    }

    template <QueueRole T> [[nodiscard]] CommandPool &primaryPool() noexcept
    {
        static_assert(isSupportedQueueRole<T>(), "Unsupported QueueRole template argument");
        if constexpr (T == QueueRole::Graphics)
        {
            return graphicsPrimary_;
        }
        else if constexpr (T == QueueRole::Compute)
        {
            return computePrimary_;
        }
        else
        {
            return transferPrimary_;
        }
    }

    template <QueueRole T> [[nodiscard]] std::uint32_t preparedSecondaryWorkers() const noexcept
    {
        if constexpr (T == QueueRole::Graphics)
        {
            return graphicsPreparedSecondaryWorkers_;
        }
        else if constexpr (T == QueueRole::Compute)
        {
            return computePreparedSecondaryWorkers_;
        }
        else
        {
            return transferPreparedSecondaryWorkers_;
        }
    }

    template <QueueRole T> [[nodiscard]] auto &secondarySlots() noexcept
    {
        static_assert(isSupportedQueueRole<T>(), "Unsupported QueueRole template argument");
        if constexpr (T == QueueRole::Graphics)
        {
            return graphicsSecondary_;
        }
        else if constexpr (T == QueueRole::Compute)
        {
            return computeSecondary_;
        }
        else
        {
            return transferSecondary_;
        }
    }

    template <QueueRole T> [[nodiscard]] static constexpr std::string_view queueRoleName() noexcept
    {
        static_assert(isSupportedQueueRole<T>(), "Unsupported QueueRole template argument");
        if constexpr (T == QueueRole::Graphics)
        {
            return "graphics";
        }
        else if constexpr (T == QueueRole::Compute)
        {
            return "compute";
        }
        else
        {
            return "transfer";
        }
    }

    template <QueueRole T> [[nodiscard]] static constexpr std::string_view secondaryPoolNotPreparedMessage() noexcept
    {
        static_assert(isSupportedQueueRole<T>(), "Unsupported QueueRole template argument");
        if constexpr (T == QueueRole::Graphics)
        {
            return "FrameContext::secondary graphics pool slot not prepared";
        }
        else if constexpr (T == QueueRole::Compute)
        {
            return "FrameContext::secondary compute pool slot not prepared";
        }
        else
        {
            return "FrameContext::secondary transfer pool slot not prepared";
        }
    }

  private:
    // Device reference and queue family IDs (used for frame-begin secondary-pool prebuild)
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    std::uint32_t graphicsQueueFamily_ = 0;
    std::uint32_t computeQueueFamily_ = 0;
    std::uint32_t transferQueueFamily_ = 0;

    // Synchronization primitives
    vk::raii::Fence fence_ = {nullptr};
    // Non-owning pointer to a semaphore slot in PresentationContext::acquirePool_.
    // Injected at beginFrame(), valid until returnAcquireSemaphore() at next beginFrame().
    const vk::raii::Semaphore *borrowedAcquireSemaphore_ = nullptr;
    vk::raii::Semaphore renderFinished_ = {nullptr};

    // Graphics queue pools
    CommandPool graphicsPrimary_;
    std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> graphicsSecondary_{};

    // Compute queue pools
    CommandPool computePrimary_;
    std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> computeSecondary_{};

    // Transfer queue pools
    CommandPool transferPrimary_;
    std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> transferSecondary_{};

    std::uint32_t graphicsPreparedSecondaryWorkers_ = 0;
    std::uint32_t computePreparedSecondaryWorkers_ = 0;
    std::uint32_t transferPreparedSecondaryWorkers_ = 0;
};

/**
 * @brief Manager for multiple frame contexts (frame-in-flight)
 *
 * Typical usage:
 *   auto& frame = frameManager.current();
 *   frame.waitForFence();
 *   frame.resetPools();
 *   // ... record commands ...
 *   queue.submit(..., frame.fence());
 */
class FrameManager
{
  public:
    /// Default constructor
    FrameManager() = default;

    /**
     * @brief Construct frame manager with N frames in flight
     * @param device Vulkan device
     * @param frameCount Number of frames in flight
     * @param graphicsConfig Graphics pool configuration
     * @param computeConfig Compute pool configuration
     * @param transferConfig Dedicated transfer pool configuration
     */
    FrameManager(const vk::raii::Device &device, const FrameContext::PoolConfig &graphicsConfig, const FrameContext::PoolConfig &computeConfig, const FrameContext::PoolConfig &transferConfig)
    {
        frames_.reserve(maxFrameInFlight);
        auto frameIndices = std::views::iota(std::uint32_t{0}, maxFrameInFlight);
        std::ranges::for_each(frameIndices, [&](std::uint32_t) {
            frames_.emplace_back(device, graphicsConfig, computeConfig, transferConfig);
        });
    }

    // Move-only semantics
    FrameManager(const FrameManager &) = delete;
    FrameManager &operator=(const FrameManager &) = delete;
    FrameManager(FrameManager &&) noexcept = default;
    FrameManager &operator=(FrameManager &&) noexcept = default;

    /**
     * @brief Get the next frame context (round-robin)
     * @return Reference to next frame context
     */
    void advanceFrame() noexcept
    {
        currentIndex_ = (currentIndex_ + 1) % frames_.size();
    }

    /**
     * @brief Get current frame context (last acquired)
     */
    [[nodiscard]] FrameContext &current() noexcept
    {
        return frames_[currentIndex_];
    }

    /**
     * @brief Get frame context by index
     */
    [[nodiscard]] FrameContext &operator[](std::size_t index) noexcept
    {
        return frames_[index];
    }

    /**
     * @brief Get total number of frame contexts
     */
    [[nodiscard]] std::size_t frameCount() const noexcept
    {
        return frames_.size();
    }

    /**
     * @brief Get current frame index
     */
    [[nodiscard]] std::size_t currentIndex() const noexcept
    {
        return currentIndex_;
    }

    /**
     * @brief Wait for all frames to complete
     */
    void waitAll()
    {
        std::ranges::for_each(frames_ | std::views::all, [](FrameContext &frame) { nrAssert(frame.waitForFence(), std::format("Timeout waiting for frame fence")); });
    }

  private:
    std::vector<FrameContext> frames_;
    std::size_t currentIndex_ = 0;
};

} // namespace nr::rhi
