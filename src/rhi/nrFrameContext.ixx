module;
export module nr.rhi:frameContext;
import dependency;
import nr.utils;
import std;
import :commandPool;

export namespace nr::rhi {

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
 * MULTI-THREADED RECORDING WITH DYNAMIC THREAD REGISTRATION
 * ============================================================================
 * 
 * Thread Safety Model:
 * - Primary pools: Main thread only (no synchronization needed)
 * - Secondary pools: Thread-local with dynamic registration
 * 
 * How Dynamic Vector with Thread Registration Works:
 * 
 * 1. Each FrameContext maintains a vector of secondary CommandPools per queue type
 *    Example: graphicsSecondary_ = vector<CommandPool>
 * 
 * 2. When a worker thread needs a secondary pool, it calls:
 *    frame.graphicsSecondary(threadId)
 * 
 * 3. The method checks if threadId >= vector.size():
 *    - If YES: Acquire mutex, resize vector, create new pool(s)
 *    - If NO: Return existing pool (no lock needed for access)
 * 
 * 4. Each thread uses its own pool index (threadId), so no contention
 *    during actual command recording - only during first-time registration.
 * 
 * Usage Pattern:
 * 
 *   // Main thread: dispatch work to thread pool
 *   std::vector<std::future<vk::CommandBuffer>> futures;
 *   for (size_t i = 0; i < threadPool.size(); ++i) {
 *       futures.push_back(threadPool.submit([&, threadId = i]() {
 *           // First access auto-registers this thread
 *           CommandPool& pool = frame.graphicsSecondary(threadId);
 *           vk::CommandBuffer cb = pool.allocateSecondary();
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
 *   std::vector<vk::CommandBuffer> secondaries;
 *   for (auto& f : futures) secondaries.push_back(f.get());
 *   
 *   // Main thread: build primary and execute secondaries
 *   vk::CommandBuffer primary = frame.graphicsPrimary().allocatePrimary();
 *   CommandRecorder::beginPrimary(primary);
 *   vkCmdExecuteCommands(primary, secondaries.size(), secondaries.data());
 *   CommandRecorder::end(primary);
 * 
 * ============================================================================
 */
class FrameContext {
public:
    /**
     * @brief Configuration for per-queue command pools
     */
    struct PoolConfig {
        uint32_t queueFamilyIndex;
        uint32_t maxThreads = nr::maxThreads; // Initial capacity for thread-local secondary pools
    };

    /// Default constructor for deferred initialization
    FrameContext() = default;

    /**
     * @brief Construct frame context with queue-specific pools
     * @param device Vulkan device
     * @param graphicsConfig Graphics queue pool configuration
     * @param computeConfig Compute queue pool configuration
     * @param transferConfig Transfer queue pool configuration (optional)
     */
    FrameContext(
        const vk::raii::Device& device,
        const PoolConfig& graphicsConfig,
        const PoolConfig& computeConfig,
        const std::optional<PoolConfig>& transferConfig = std::nullopt
    ) : device_(std::ref(device))
      , graphicsQueueFamily_(graphicsConfig.queueFamilyIndex)
      , computeQueueFamily_(computeConfig.queueFamilyIndex)
    {
        // Create fence (signaled initially so first frame doesn't wait)
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        fence_ = vk::raii::Fence(device, fenceInfo);

        // Create semaphores for frame synchronization
        vk::SemaphoreCreateInfo semaphoreInfo{};
        imageAvailable_ = vk::raii::Semaphore(device, semaphoreInfo);
        renderFinished_ = vk::raii::Semaphore(device, semaphoreInfo);

        // Create primary pools (one per queue type, main thread only)
        // Use TRANSIENT flag for better performance with short-lived buffers
        graphicsPrimary_ = CommandPool(
            device, 
            graphicsConfig.queueFamilyIndex,
            vk::CommandPoolCreateFlagBits::eTransient
        );
        
        computePrimary_ = CommandPool(
            device,
            computeConfig.queueFamilyIndex,
            vk::CommandPoolCreateFlagBits::eTransient
        );

        // Pre-allocate secondary pool vectors with initial capacity
        graphicsSecondary_.reserve(graphicsConfig.maxThreads);
        computeSecondary_.reserve(computeConfig.maxThreads);

        // Handle optional transfer queue
        if (transferConfig) {
            transferQueueFamily_ = transferConfig->queueFamilyIndex;
            transferPrimary_.emplace(
                device,
                transferConfig->queueFamilyIndex,
                vk::CommandPoolCreateFlagBits::eTransient
            );
            transferSecondary_.reserve(transferConfig->maxThreads);
        }
    }

    // Move-only semantics
    FrameContext(const FrameContext&) = delete;
    FrameContext& operator=(const FrameContext&) = delete;
    FrameContext(FrameContext &&other) noexcept
        : fence_(std::move(other.fence_)), imageAvailable_(std::move(other.imageAvailable_)), renderFinished_(std::move(other.renderFinished_)), 
        graphicsPrimary_(std::move(other.graphicsPrimary_)), graphicsSecondary_(std::move(other.graphicsSecondary_)) ,graphicsQueueFamily_(other.graphicsQueueFamily_),
        computePrimary_(std::move(other.computePrimary_)), computeSecondary_(std::move(other.computeSecondary_)),computeQueueFamily_(other.computeQueueFamily_), 
        transferPrimary_(std::move(other.transferPrimary_)), transferSecondary_(std::move(other.transferSecondary_)) , transferQueueFamily_(other.transferQueueFamily_), 
        device_(std::move(other.device_))
    {}

    FrameContext &operator=(FrameContext &&other) noexcept
    {
        if (this != &other)
        {
            fence_ = std::move(other.fence_);
            imageAvailable_ = std::move(other.imageAvailable_);
            renderFinished_ = std::move(other.renderFinished_);
            graphicsPrimary_ = std::move(other.graphicsPrimary_);
            graphicsSecondary_ = std::move(other.graphicsSecondary_);
            graphicsQueueFamily_ = other.graphicsQueueFamily_;
            computePrimary_ = std::move(other.computePrimary_);
            computeSecondary_ = std::move(other.computeSecondary_);
            computeQueueFamily_ = other.computeQueueFamily_;
            transferPrimary_ = std::move(other.transferPrimary_);
            transferSecondary_ = std::move(other.transferSecondary_);
            transferQueueFamily_ = other.transferQueueFamily_;
            device_ = std::move(other.device_);
        }
        return *this;
    }

    /**
     * @brief Wait for this frame's fence to be signaled
     * @param timeout Timeout in nanoseconds (default: 10 second)
     * @return true if fence signaled, false if timeout
     */
    [[nodiscard]] bool waitForFence(uint64_t timeout = 10'000'000'000)
    {
        auto result = device_->get().waitForFences(*fence_, vk::True, timeout);
        return result == vk::Result::eSuccess;
    }

    /**
     * @brief Reset fence to unsignaled state
     * 
     * Must be called before reusing this frame context
     */
    void resetFence() {
        device_->get().resetFences(*fence_);
    }

    /**
     * @brief Reset all command pools in this context
     * 
     * Only call after fence is signaled. Invalidates all command buffers.
     */
    void resetPools() {
        // Reset primary pools
        graphicsPrimary_.reset();
        computePrimary_.reset();
        if (transferPrimary_) {
            transferPrimary_->reset();
        }

        // Reset all registered secondary pools
        // No lock needed here - reset only called from main thread after fence
        for (auto& pool : graphicsSecondary_) {
            pool.reset();
        }
        for (auto& pool : computeSecondary_) {
            pool.reset();
        }
        for (auto& pool : transferSecondary_) {
            pool.reset();
        }
    }

    /**
     * @brief Get primary command pool for graphics queue
     */
    [[nodiscard]] CommandPool& graphicsPrimary() noexcept { return graphicsPrimary_; }

    /**
     * @brief Get secondary command pool for graphics queue (thread-local)
     * @param threadId Thread identifier (0-based, typically from thread pool)
     * @return Reference to thread's secondary pool
     * 
     * =========================================================================
     * THREAD REGISTRATION MECHANISM (Key Implementation Detail)
     * =========================================================================
     * 
     * This method implements dynamic thread registration:
     * 
     * 1. Fast path (no lock): If threadId < vector.size(), pool already exists
     *    - Direct array access, no synchronization overhead
     * 
     * 2. Slow path (with lock): If threadId >= vector.size()
     *    - Acquire mutex (only blocks other new registrations)
     *    - Resize vector to accommodate new threadId
     *    - Create new CommandPool for each new slot
     *    - Release mutex
     * 
     * Why this is efficient:
     * - After initial registration, all accesses are lock-free
     * - Each thread has its own pool, no contention during recording
     * - Pools are created lazily, only when thread actually needs one
     * =========================================================================
     */
    [[nodiscard]] CommandPool& graphicsSecondary(size_t threadId) {
        return getOrCreateSecondaryPool(
            graphicsSecondary_,
            graphicsSecondaryMutex_,
            threadId,
            graphicsQueueFamily_
        );
    }

    /**
     * @brief Get primary command pool for compute queue
     */
    [[nodiscard]] CommandPool& computePrimary() noexcept { return computePrimary_; }

    /**
     * @brief Get secondary command pool for compute queue (thread-local)
     * @param threadId Thread identifier (0-based)
     * @return Reference to thread's secondary pool
     */
    [[nodiscard]] CommandPool& computeSecondary(size_t threadId) {
        return getOrCreateSecondaryPool(
            computeSecondary_,
            computeSecondaryMutex_,
            threadId,
            computeQueueFamily_
        );
    }

    /**
     * @brief Get primary command pool for transfer queue (if available)
     */
    [[nodiscard]] CommandPool* transferPrimary() noexcept {
        return transferPrimary_ ? &(*transferPrimary_) : nullptr;
    }

    /**
     * @brief Get secondary command pool for transfer queue (if available)
     */
    [[nodiscard]] CommandPool* transferSecondary(size_t threadId) {
        if (!transferPrimary_) return nullptr;
        return &getOrCreateSecondaryPool(
            transferSecondary_,
            transferSecondaryMutex_,
            threadId,
            transferQueueFamily_
        );
    }

    // ========== Synchronization primitives ==========

    /**
     * @brief Get the fence for this frame
     * @return Reference to vk::raii::Fence
     */
    [[nodiscard]] const vk::raii::Fence& fence() const noexcept { return fence_; }

    /**
     * @brief Get image available semaphore (signaled by swapchain acquire)
     * @return Reference to vk::raii::Semaphore
     */
    [[nodiscard]] const vk::raii::Semaphore& imageAvailable() const noexcept { return imageAvailable_; }

    /**
     * @brief Get render finished semaphore (signaled after rendering for present)
     * @return Reference to vk::raii::Semaphore
     */
    [[nodiscard]] const vk::raii::Semaphore& renderFinished() const noexcept { return renderFinished_; }

    // ========== Status queries ==========

    /**
     * @brief Check if fence is currently signaled
     */
    [[nodiscard]] bool isFenceSignaled() const {
        return fence_.getStatus() == vk::Result::eSuccess;
    }

    /**
     * @brief Get number of registered graphics secondary pools
     */
    [[nodiscard]] size_t registeredGraphicsThreads() const noexcept {
        return graphicsSecondary_.size();
    }

    /**
     * @brief Get number of registered compute secondary pools
     */
    [[nodiscard]] size_t registeredComputeThreads() const noexcept {
        return computeSecondary_.size();
    }

private:
    /**
     * @brief Core implementation of dynamic thread registration
     * 
     * This is the heart of the multi-threaded command recording system.
     * 
     * @param pools Vector of pools to access/grow
     * @param mutex Mutex protecting the vector resize operation
     * @param threadId Index of the requesting thread
     * @param queueFamilyIndex Queue family for new pool creation
     * @return Reference to the thread's dedicated pool
     */
    CommandPool& getOrCreateSecondaryPool(
        std::vector<CommandPool>& pools,
        std::mutex& mutex,
        size_t threadId,
        uint32_t queueFamilyIndex
    ) {
        // =====================================================================
        // FAST PATH: Pool already exists for this threadId
        // =====================================================================
        // This check is done WITHOUT acquiring the lock.
        // Safe because:
        // 1. We only read size(), which is atomic for most implementations
        // 2. Once a pool is created at index i, it's never moved or deleted
        // 3. Vector only grows, never shrinks
        if (threadId < pools.size()) {
            return pools[threadId];
        }

        // =====================================================================
        // SLOW PATH: Need to register new thread(s)
        // =====================================================================
        // Acquire lock to safely resize the vector
        std::lock_guard lock{mutex};

        // Double-check after acquiring lock (another thread might have grown it)
        if (threadId < pools.size()) {
            return pools[threadId];
        }

        // Grow vector to accommodate new threadId
        // Create pools for ALL indices up to threadId (handles sparse registration)
        size_t oldSize = pools.size();
        pools.reserve(threadId + 1);
        
        for (size_t i = oldSize; i <= threadId; ++i) {
            // Each secondary pool uses TRANSIENT flag for short-lived buffers
            pools.emplace_back(device_->get(),
                queueFamilyIndex,
                vk::CommandPoolCreateFlagBits::eTransient
            );
        }

        return pools[threadId];
    }

private:
    // Synchronization primitives (frame-owned)
    vk::raii::Fence fence_ = {nullptr};
    vk::raii::Semaphore imageAvailable_ = {nullptr};
    vk::raii::Semaphore renderFinished_ = {nullptr};

    // Graphics queue pools
    CommandPool graphicsPrimary_;
    std::vector<CommandPool> graphicsSecondary_;
    std::mutex graphicsSecondaryMutex_;
    uint32_t graphicsQueueFamily_ = 0;

    // Compute queue pools
    CommandPool computePrimary_;
    std::vector<CommandPool> computeSecondary_;
    std::mutex computeSecondaryMutex_;
    uint32_t computeQueueFamily_ = 0;

    // Transfer queue pools (optional)
    std::optional<CommandPool> transferPrimary_;
    std::vector<CommandPool> transferSecondary_;
    std::mutex transferSecondaryMutex_;
    uint32_t transferQueueFamily_ = 0;

    // Device reference for pool creation
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
};

/**
 * @brief Manager for multiple frame contexts (frame-in-flight)
 * 
 * Typical usage:
 *   auto& frame = frameManager.acquireNext();
 *   frame.waitForFence();
 *   frame.resetPools();
 *   // ... record commands ...
 *   queue.submit(..., frame.fence());
 */
class FrameManager {
public:
    /// Default constructor
    FrameManager() = default;

    /**
     * @brief Construct frame manager with N frames in flight
     * @param device Vulkan device
     * @param frameCount Number of frames in flight (typically 2 or 3)
     * @param graphicsConfig Graphics pool configuration
     * @param computeConfig Compute pool configuration
     * @param transferConfig Transfer pool configuration (optional)
     */
    FrameManager(
        const vk::raii::Device& device,
        const FrameContext::PoolConfig& graphicsConfig,
        const FrameContext::PoolConfig& computeConfig,
        const std::optional<FrameContext::PoolConfig>& transferConfig = std::nullopt
    ) {
        frames_.reserve(maxFrameInFlight);
        for (uint32_t i = 0; i < maxFrameInFlight; ++i)
        {
            frames_.emplace_back(device, graphicsConfig, computeConfig, transferConfig);
        }
    }

    // Move-only semantics
    FrameManager(const FrameManager&) = delete;
    FrameManager& operator=(const FrameManager&) = delete;
    FrameManager(FrameManager&&) noexcept = default;
    FrameManager& operator=(FrameManager&&) noexcept = default;

    /**
     * @brief Get the next frame context (round-robin)
     * @return Reference to next frame context
     */
    [[nodiscard]] FrameContext& acquireNext() noexcept {
        currentIndex_ = (currentIndex_ + 1) % frames_.size();
        return frames_[currentIndex_];
    }

    /**
     * @brief Get current frame context (last acquired)
     */
    [[nodiscard]] FrameContext& current() noexcept {
        return frames_[currentIndex_];
    }

    /**
     * @brief Get frame context by index
     */
    [[nodiscard]] FrameContext& operator[](size_t index) noexcept {
        return frames_[index];
    }

    /**
     * @brief Get total number of frame contexts
     */
    [[nodiscard]] size_t frameCount() const noexcept { return frames_.size(); }

    /**
     * @brief Get current frame index
     */
    [[nodiscard]] size_t currentIndex() const noexcept { return currentIndex_; }

    /**
     * @brief Wait for all frames to complete
     */
    void waitAll() {
        for (auto& frame : frames_) {
            nrAssert(frame.waitForFence(), std::format("Timeout waiting for frame fence"));
        }
    }

private:
    std::vector<FrameContext> frames_;
    size_t currentIndex_ = 0;
};

} // namespace nr::rhi
