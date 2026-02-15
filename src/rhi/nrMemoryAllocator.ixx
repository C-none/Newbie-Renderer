module;
export module nr.rhi:memoryAllocator;
import dependency;
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
 * - Transient:   Immediate staging uploads (dedicated staging pool)
 *
 * All GPU buffers automatically include VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
 * for Buffer Device Address (BDA) support.
 *
 * Thread Safety:
 * - allocateBuffer/allocateImage are thread-safe (VMA internally synchronized)
 * - resetFramePool must only be called when the frame's fence is signaled
 */

export namespace nr::rhi
{

/// Threshold above which GPU-only buffers get dedicated allocations (4 MiB)
inline constexpr vk::DeviceSize kDedicatedAllocationThreshold = 4u * 1024u * 1024u;

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
     * 3. Staging pool for Transient uploads
     */
    void initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice, const vk::raii::Device &device)
    {
        device_ = std::ref(device);
        vma_ = VmaAllocatorWrapper(instance, physDevice, device);

        createPerFramePools();
        createStagingPool();
    }

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
    [[nodiscard]] VmaBuffer allocateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, AllocationStrategy strategy = AllocationStrategy::CrossFrame, MemoryUsage usage = MemoryUsage::GpuOnly, uint32_t frameIndex = 0) const
    {
        // Add BDA support by default for all non-staging buffers
        if (usage == MemoryUsage::GpuOnly || usage == MemoryUsage::CpuToGpu)
        {
            bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        }

        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = size;
        bufferInfo.usage = bufferUsage;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocInfo{};

        switch (strategy)
        {
        case AllocationStrategy::CrossFrame:
            configureCrossFrame(allocInfo, usage, size);
            break;

        case AllocationStrategy::PerFrame:
            configurePerFrame(allocInfo, frameIndex);
            break;

        case AllocationStrategy::Transient:
            configureTransient(allocInfo);
            break;
        }

        return vma_.createBuffer(bufferInfo, allocInfo);
    }

    /**
     * @brief Allocate a buffer from Vulkan-hpp style create info
     *
     * Overload accepting vk::BufferCreateInfo for integration with
     * Vulkan-hpp code paths. Adds BDA flag automatically.
     */
    [[nodiscard]] VmaBuffer allocateBuffer(const vk::BufferCreateInfo &createInfo, AllocationStrategy strategy = AllocationStrategy::CrossFrame, MemoryUsage usage = MemoryUsage::GpuOnly, uint32_t frameIndex = 0) const
    {
        // Convert to C struct and delegate
        VkBufferCreateInfo bufferInfo = static_cast<VkBufferCreateInfo>(createInfo);

        // Add BDA for GPU-visible buffers
        if (usage == MemoryUsage::GpuOnly || usage == MemoryUsage::CpuToGpu)
        {
            bufferInfo.usage |= static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eShaderDeviceAddress);
        }

        VmaAllocationCreateInfo allocInfo{};

        switch (strategy)
        {
        case AllocationStrategy::CrossFrame:
            configureCrossFrame(allocInfo, usage, bufferInfo.size);
            break;
        case AllocationStrategy::PerFrame:
            configurePerFrame(allocInfo, frameIndex);
            break;
        case AllocationStrategy::Transient:
            configureTransient(allocInfo);
            break;
        }

        return vma_.createBuffer(bufferInfo, allocInfo);
    }

    // =====================================================================
    // Image Allocation (always CrossFrame)
    // =====================================================================

    /**
     * @brief Allocate a GPU image with VMA
     *
     * Images are always CrossFrame — per-frame image pools are not
     * typically useful. Uses DEDICATED_MEMORY_BIT for large images
     * and render targets.
     *
    * @param imageInfo   Vulkan image create info (Vulkan-hpp type)
     * @param usage       Memory usage (typically GpuOnly)
     * @return RAII VmaImage
     */
    [[nodiscard]] VmaImage allocateImage(const vk::ImageCreateInfo &imageInfo, MemoryUsage usage = MemoryUsage::GpuOnly) const
    {
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        // Dedicate large images (render targets, large textures)
        vk::DeviceSize estimatedSize = static_cast<vk::DeviceSize>(imageInfo.extent.width) * imageInfo.extent.height * imageInfo.extent.depth * imageInfo.arrayLayers * 4; // rough estimate
        if (estimatedSize >= kDedicatedAllocationThreshold)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
        }

        // Apply host access for non-GPU-only images (e.g., readback)
        if (usage == MemoryUsage::GpuToCpu)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        return vma_.createImage(imageInfo, allocInfo);
    }

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
    void resetFramePool(uint32_t frameIndex)
    {
        uint32_t idx = frameIndex % maxFrameInFlight;
        if (perFramePools_[idx].valid())
        {
            perFramePools_[idx].reset();
        }
    }

    // =====================================================================
    // Query & Diagnostics
    // =====================================================================

    /**
     * @brief Log per-heap budget information
     *
     * Outputs allocation and budget stats for each memory heap.
     * Safe to call every frame (uses fast VMA budget query).
     */
    void logBudget() const
    {
        auto budgets = vma_.getBudgets();
        for (size_t i = 0; i < budgets.size(); ++i)
        {
            const auto &b = budgets[i];
            if (b.statistics.blockCount == 0)
                continue;

            nrInfo(std::format("Heap {}: {:.2f} MiB used / {:.2f} MiB allocated / {:.2f} MiB budget ({} blocks, {} allocs)", i, static_cast<double>(b.statistics.allocationBytes) / (1024.0 * 1024.0), static_cast<double>(b.statistics.blockBytes) / (1024.0 * 1024.0),
                               static_cast<double>(b.budget) / (1024.0 * 1024.0), b.statistics.blockCount, b.statistics.allocationCount));
        }
    }

    /**
     * @brief Log per-frame pool statistics
     */
    void logFramePoolStats() const
    {
        for (uint32_t i = 0; i < maxFrameInFlight; ++i)
        {
            if (!perFramePools_[i].valid())
                continue;
            auto stats = perFramePools_[i].statistics();
            nrInfo(std::format("Frame pool [{}]: {:.2f} KiB used / {:.2f} KiB allocated ({} allocs)", i, static_cast<double>(stats.allocationBytes) / 1024.0, static_cast<double>(stats.blockBytes) / 1024.0, stats.allocationCount));
        }
    }

    /**
     * @brief Check if a buffer's memory is host-visible
     *
     * Useful for the "try ReBAR, fall back to staging" pattern:
     * allocate with CpuToGpu + CrossFrame, then check if direct write is possible.
     */
    [[nodiscard]] static bool isHostVisible(const VmaBuffer &buffer)
    {
        return buffer.isHostVisible();
    }

    /// Get the underlying VMA allocator wrapper (for advanced usage)
    [[nodiscard]] const VmaAllocatorWrapper &vma() const noexcept
    {
        return vma_;
    }

    /// Get raw VMA allocator handle (for interop)
    [[nodiscard]] VmaAllocator handle() const noexcept
    {
        return vma_.handle();
    }

    /// Check if initialized
    [[nodiscard]] bool valid() const noexcept
    {
        return vma_.valid();
    }

  private:
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
    void createPerFramePools()
    {
        // Find the right memory type for host-visible, sequentially-written buffers
        vk::BufferCreateInfo sampleBuf{};
        sampleBuf.size = 0x10000; // sample size, not actual block size
        sampleBuf.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress;

        VmaAllocationCreateInfo sampleAlloc{};
        sampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        sampleAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        uint32_t memTypeIndex = vma_.findMemoryTypeIndexForBuffer(sampleBuf, sampleAlloc);

        for (uint32_t i = 0; i < maxFrameInFlight; ++i)
        {
            VmaPoolCreateInfo poolInfo{};
            poolInfo.memoryTypeIndex = memTypeIndex;
            poolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
            poolInfo.blockSize = 0;     // Let VMA manage block sizes
            poolInfo.maxBlockCount = 0; // No limit on blocks

            perFramePools_[i] = vma_.createPool(poolInfo);
        }
    }

    /**
     * @brief Create staging pool for Transient strategy
     *
     * Optimized for short-lived CPU->GPU transfer buffers.
     * Uses linear allocation for fast alloc/free patterns.
     */
    void createStagingPool()
    {
        vk::BufferCreateInfo sampleBuf{};
        sampleBuf.size = 0x10000;
        sampleBuf.usage = vk::BufferUsageFlagBits::eTransferSrc;

        VmaAllocationCreateInfo sampleAlloc{};
        sampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        sampleAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        uint32_t memTypeIndex = vma_.findMemoryTypeIndexForBuffer(sampleBuf, sampleAlloc);

        VmaPoolCreateInfo poolInfo{};
        poolInfo.memoryTypeIndex = memTypeIndex;
        poolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
        poolInfo.blockSize = 0;
        poolInfo.maxBlockCount = 0;

        stagingPool_ = vma_.createPool(poolInfo);
    }

    // -----------------------------------------------------------------
    // Strategy configuration helpers
    // -----------------------------------------------------------------

    /**
     * @brief Configure VMA allocation info for CrossFrame strategy
     *
     * Long-lived resources in the default VMA pool.
     * - GpuOnly:  DEVICE_LOCAL, dedicated for large buffers
     * - CpuToGpu: Tries ReBAR (HOST_VISIBLE + DEVICE_LOCAL) first,
     *             falls back to DEVICE_LOCAL with staging transfer
     * - GpuToCpu: HOST_VISIBLE + HOST_CACHED for CPU reads
     * - CpuOnly:  HOST_VISIBLE + MAPPED
     */
    static void configureCrossFrame(VmaAllocationCreateInfo &allocInfo, MemoryUsage usage, vk::DeviceSize size)
    {
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        switch (usage)
        {
        case MemoryUsage::GpuOnly:
            // Large GPU-only buffers get dedicated allocations
            if (size >= kDedicatedAllocationThreshold)
            {
                allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
            }
            break;

        case MemoryUsage::CpuToGpu:
            // Try ReBAR first, fall back to device-local + staging
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;

        case MemoryUsage::GpuToCpu:
            // Host-cached for CPU read performance
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;

        case MemoryUsage::CpuOnly:
            // Persistently mapped host memory
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            break;
        }
    }

    /**
     * @brief Configure VMA allocation info for PerFrame strategy
     *
     * Allocates from the frame-specific linear pool for O(1) bulk reset.
     * Always host-visible and mapped for CPU writes.
     */
    void configurePerFrame(VmaAllocationCreateInfo &allocInfo, uint32_t frameIndex) const
    {
        uint32_t idx = frameIndex % maxFrameInFlight;
        allocInfo.pool = perFramePools_[idx].handle();
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        // pool overrides memory type selection — no usage/flags needed for type
    }

    /**
     * @brief Configure VMA allocation info for Transient strategy
     *
     * Allocates from the staging pool. Host-visible, sequentially written.
     */
    void configureTransient(VmaAllocationCreateInfo &allocInfo) const
    {
        allocInfo.pool = stagingPool_.handle();
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------
    VmaAllocatorWrapper vma_;
    std::array<VmaPoolHandle, maxFrameInFlight> perFramePools_;
    VmaPoolHandle stagingPool_;
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
};

} // namespace nr::rhi
