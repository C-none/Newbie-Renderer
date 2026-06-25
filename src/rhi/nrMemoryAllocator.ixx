export module nr.rhi:memoryAllocator;
import dependency.vma;
import dependency.nsight;
import dependency.vulkan;
import nr.utils;
import :type;
import :vmaAllocator;
import std;

/**
 * @file nrMemoryAllocator.ixx
 * @brief High-level strategy-based memory allocator built on VMA
 *
 * Sits above VmaAllocatorWrapper and provides semantic allocation methods
 * that abstract away VMA flag combinations. Three allocation strategies:
 *
 * - CrossFrame:  Standard long-lived allocations (default VMA pool)
 * - PerFrame:    Short-lived per-frame resources (linear pool, bulk-reset)
 * - StagingTransient: Immediate staging uploads (dedicated staging pool)
 *
 * GpuOnly buffers automatically include VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
 * for Buffer Device Address (BDA) support. Host-mapped CpuToGpu buffers do not, so
 * pure-staging buffers are not pushed into BAR by VMA's deviceAccess heuristic.
 *
 * Thread Safety:
 * - allocateBuffer/allocateImage are thread-safe (VMA internally synchronized)
 * - resetFramePool must only be called when the frame's fence is signaled
 */

export namespace nr::rhi
{


/**
 * @brief Strategy-based memory allocator for the rendering pipeline
 *
 * Owns the VMA allocator instance and manages specialized pools:
 * - Per-frame linear pools for O(1) bulk deallocation per frame
 * - A staging pool optimized for CPU->GPU transfers
 *
 * Lifecycle:
 * - Constructed after vk::raii::Device creation
 * - Initialized by Device::initialize() before ResourcePool
 * - Must be destroyed before vk::raii::Device (reverse member order handles this)
 * - resetFramePool() called each frame after fence signal
 */
class MemoryAllocator
{
  public:
    MemoryAllocator() = default;

    // Move-only
    MemoryAllocator(const MemoryAllocator &) = delete;
    MemoryAllocator &operator=(const MemoryAllocator &) = delete;
    MemoryAllocator(MemoryAllocator &&) noexcept = default;
    MemoryAllocator &operator=(MemoryAllocator &&) noexcept = default;

    /**
     * @brief Initialize the memory allocator and create internal pools
     *
     * @param instance    Vulkan RAII instance
     * @param physDevice  Vulkan RAII physical device
     * @param device      Vulkan RAII device
     *
     * Creates:
     * 1. VMA allocator with Vulkan 1.4
     * 2. Per-frame linear pools (one per frame-in-flight) for PerFrame strategy
    * 3. Staging pool for StagingTransient uploads
     */
    void initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice, const vk::raii::Device &device);

    // =====================================================================
    // Strategy-Based Buffer Allocation
    // =====================================================================

    /**
     * @brief Allocate a buffer with semantic strategy and usage
     *
     * @param size          Buffer size in bytes
    * @param bufferUsage   Vulkan buffer usage flags (vk::BufferUsageFlags)
     *                      SHADER_DEVICE_ADDRESS_BIT is added automatically for GPU buffers
     * @param strategy      Allocation lifetime strategy
     * @param usage         Memory usage intent (GPU-only, CPU-to-GPU, etc.)
     * @param frameIndex    Frame index for PerFrame strategy (mod maxFrameInFlight)
     * @return RAII VmaBuffer
     */
    [[nodiscard]] VmaBuffer allocateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, AllocationStrategy strategy = AllocationStrategy::CrossFrame, MemoryUsage usage = MemoryUsage::GpuOnly, std::uint32_t frameIndex = 0) const;

    /**
     * @brief Allocate a buffer from Vulkan-hpp style create info
     *
     * Overload accepting vk::BufferCreateInfo for integration with
     * Vulkan-hpp code paths. Adds BDA flag automatically.
     */
    [[nodiscard]] VmaBuffer allocateBuffer(const vk::BufferCreateInfo &createInfo, AllocationStrategy strategy = AllocationStrategy::CrossFrame, MemoryUsage usage = MemoryUsage::GpuOnly, std::uint32_t frameIndex = 0) const;

    // =====================================================================
    // Image Allocation (always CrossFrame)
    // =====================================================================

    /**
     * @brief Allocate a GPU image with VMA
     *
     * Images are always CrossFrame — per-frame image pools are not
     * typically useful. VMA automatically uses dedicated allocation when the
     * driver reports requiresDedicatedAllocation/prefersDedicatedAllocation
     * (common for render targets and large textures on NVIDIA). No explicit
     * DEDICATED_MEMORY_BIT is set here. Unlike GpuOnly buffers, images are not
     * rerouted under Nsight; the observed VUID-VkMemoryAllocateInfo-pNext-02806
     * conflicts came from buffers, so only the buffer path is mitigated.
     *
     * @param imageInfo   Vulkan image create info (Vulkan-hpp type)
     * @param usage       Memory usage (typically GpuOnly)
     * @return RAII VmaImage
     */
    [[nodiscard]] VmaImage allocateImage(const vk::ImageCreateInfo &imageInfo, MemoryUsage usage = MemoryUsage::GpuOnly) const;

    /**
     * @brief Allocate a GPU image from Vulkan-hpp create info
     */

    // =====================================================================
    // Frame Lifecycle
    // =====================================================================

    /**
     * @brief Reset the per-frame linear pool for the given frame index
     *
     * Frees all allocations from the pool in O(1). Must only be called
     * when the frame's fence is signaled (all GPU work for this frame complete).
     *
     * @param frameIndex  Frame index (mod maxFrameInFlight)
     */
    void resetFramePool(std::uint32_t frameIndex);

    // =====================================================================
    // Query & Diagnostics
    // =====================================================================

    /**
     * @brief Log per-heap budget information
     *
     * Outputs allocation and budget stats for each memory heap.
     * Safe to call every frame (uses fast VMA budget query).
     */
    void logBudget() const;

    /**
     * @brief Log per-frame pool statistics
     */
    void logFramePoolStats() const;

    /**
     * @brief Check if a buffer's memory is host-visible
     *
     * Useful for the "try ReBAR, fall back to staging" pattern:
     * allocate with CpuToGpu + CrossFrame, then check if direct write is possible.
     */
    [[nodiscard]] static bool isHostVisible(const VmaBuffer &buffer);

    /// Get the underlying VMA allocator wrapper (for advanced usage)
    [[nodiscard]] const VmaAllocatorWrapper &vma() const noexcept;

    /// Get raw VMA allocator handle (for interop)
    [[nodiscard]] VmaAllocator handle() const noexcept;

    /// Check if initialized
    [[nodiscard]] bool valid() const noexcept;

  private:
    [[nodiscard]] static bool nsightGraphicsActivityRequested() noexcept;

    // -----------------------------------------------------------------
    // Internal pool creation
    // -----------------------------------------------------------------

    /**
     * @brief Create per-frame linear pools for PerFrame strategy
     *
     * Each pool uses VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT for
     * O(1) bulk deallocation (free-all-at-once pattern).
     * One pool per frame-in-flight, sized for typical per-frame scratch work.
     */
    void createPerFramePools();

    /**
    * @brief Create staging pool for StagingTransient strategy
     *
     * Optimized for short-lived CPU->GPU transfer buffers.
     * Uses linear allocation for fast alloc/free patterns.
     */
    void createStagingPool();

    /**
     * @brief Create a device-local pool with an explicit block size for Nsight runs.
     *
     * VMA disables dedicated allocation for any pool that has an explicit block size
     * (vk_mem_alloc.h: canAllocateDedicated is false when the block vector
     * HasExplicitBlockSize()). Routing GpuOnly buffers here therefore stops VMA from
     * attaching VkMemoryDedicatedAllocateInfo, which the host-pointer import injected by
     * Nsight conflicts with (VUID-VkMemoryAllocateInfo-pNext-02806). Created only while
     * Nsight is intercepting; normal runs keep the driver-preferred dedicated path.
     *
     * Allocations larger than the block size cannot be served from this pool. GpuOnly
     * buffers in this engine stay well below the block size, and this path is only active
     * under profiling, so the looser placement is acceptable.
     */
    void createProfilerSafePool();

    // -----------------------------------------------------------------
    // Strategy configuration helpers
    // -----------------------------------------------------------------

    /**
     * @brief Configure VMA allocation info for CrossFrame strategy
     *
     * Long-lived resources in the default VMA pool.
     * - GpuOnly:  DEVICE_LOCAL. VMA automatically handles dedicated allocation when
     *             the driver reports requiresDedicatedAllocation/prefersDedicatedAllocation
     *             via vkGetBufferMemoryRequirements2. No explicit DEDICATED_MEMORY_BIT is
     *             set here, so normal runs keep the driver-preferred dedicated allocation.
     *             Under Nsight (nsightProfilerActive_) the buffer is instead routed to
     *             profilerSafePool_ below: VMA disables dedicated allocation for explicit
     *             block-size pools, so no VkMemoryDedicatedAllocateInfo is emitted and it
     *             cannot conflict with the VkImportMemoryHostPointerInfoEXT Nsight injects
     *             (VUID-VkMemoryAllocateInfo-pNext-02806).
     * - CpuToGpu: HOST_VISIBLE, persistently mapped (ReBAR HOST_VISIBLE+DEVICE_LOCAL
     *             when available, otherwise plain HOST_VISIBLE). Always mappable.
     * - GpuToCpu: HOST_VISIBLE + HOST_CACHED for CPU reads
     * - CpuOnly:  HOST_VISIBLE + MAPPED
     */
    void configureCrossFrame(VmaAllocationCreateInfo &allocInfo, MemoryUsage usage) const;

    /**
     * @brief Avoid buffer-specific dedicated-allocation metadata while Nsight is intercepting allocations.
     *
     * Nsight Graphics capture can inject VkImportMemoryHostPointerInfoEXT into vkAllocateMemory.
     * Vulkan forbids that host-pointer import from sharing a pNext chain with
     * VkMemoryDedicatedAllocateInfo when the dedicated structure names a buffer. VMA's
     * CAN_ALIAS flag keeps any fallback dedicated allocation from adding that buffer
     * handle, covering host-visible rings as well as GpuOnly buffers.
     */
    void configureProfilerBufferCompatibility(VmaAllocationCreateInfo &allocInfo) const noexcept;

    /**
     * @brief Configure VMA allocation info for PerFrame strategy
     *
     * Allocates from the frame-specific linear pool for O(1) bulk reset.
     * Always host-visible and mapped for CPU writes.
     */
    void configurePerFrame(VmaAllocationCreateInfo &allocInfo, std::uint32_t frameIndex) const;

    /**
    * @brief Configure VMA allocation info for StagingTransient strategy
     *
     * Allocates from the staging pool. Host-visible, sequentially written.
     */
    void configureStagingTransient(VmaAllocationCreateInfo &allocInfo) const;

    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------
    VmaAllocatorWrapper vma_;
    std::array<VmaPoolHandle, maxFrameInFlight> perFramePools_;
    mutable std::array<bool, maxFrameInFlight> perFramePoolDirty_{};
    VmaPoolHandle stagingPool_;
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};

    // True when NVIDIA Nsight Graphics is intercepting this process. When set,
    // all buffer allocations suppress buffer-specific dedicated-allocation metadata.
    // GpuOnly CrossFrame buffers also route through profilerSafePool_ to keep the
    // original profiling placement policy.
    bool nsightProfilerActive_ = false;
    VmaPoolHandle profilerSafePool_;
};

} // namespace nr::rhi
