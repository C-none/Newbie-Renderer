export module nr.rhi:vmaAllocator;
import dependency.vma;
import dependency.vulkan;
import nr.utils;
import :type;
import std;

/**
 * @file nrVmaAllocator.ixx
 * @brief Thin RAII wrappers around VulkanMemoryAllocator (VMA)
 *
 * This is the lowest layer of the memory management stack:
 * - VmaBuffer:            RAII wrapper for VkBuffer + VmaAllocation pair
 * - VmaImage:             RAII wrapper for VkImage  + VmaAllocation pair
 * - VmaPoolHandle:        RAII wrapper for VmaPool
 * - VmaAllocatorWrapper:  RAII wrapper for VmaAllocator + statistics APIs
 *
 * All types are move-only, following the project's RAII conventions.
 * VMA is internally thread-safe by default (no EXTERNALLY_SYNCHRONIZED).
 *
 * Higher-level strategy allocation is in nrMemoryAllocator.ixx (:memoryAllocator).
 */

export namespace nr::rhi
{

// =========================================================================
// VmaBuffer — RAII buffer + allocation pair
// =========================================================================

/**
 * @brief RAII wrapper owning a VkBuffer and its VmaAllocation
 *
 * Destruction calls vmaDestroyBuffer which frees both the Vulkan buffer
 * handle and the underlying device memory. Cached VmaAllocationInfo
 * provides zero-cost access to mapped pointer and size.
 */
struct VmaBuffer
{
    VmaAllocator allocator = nullptr;
    VkBuffer buffer = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};

    VmaBuffer() = default;

    VmaBuffer(VmaAllocator alloc, VkBuffer buf, VmaAllocation mem, VmaAllocationInfo allocInfo);

    ~VmaBuffer();

    // Move-only
    VmaBuffer(const VmaBuffer &) = delete;
    VmaBuffer &operator=(const VmaBuffer &) = delete;

    VmaBuffer(VmaBuffer &&other) noexcept;

    VmaBuffer &operator=(VmaBuffer &&other) noexcept;

    /// Check if this buffer holds a valid allocation
    [[nodiscard]] bool valid() const noexcept;

    /// Get the persistently mapped pointer (nullptr if not mapped)
    [[nodiscard]] void *mapped() const noexcept;

    /// Get the allocation size in bytes
    [[nodiscard]] VkDeviceSize size() const noexcept;

    /// Get the raw VkBuffer handle
    [[nodiscard]] VkBuffer handle() const noexcept;

    /**
     * @brief Get the buffer device address (BDA)
     * @param device The Vulkan device (needed for vkGetBufferDeviceAddress)
     * @return 64-bit device address for shader access
     *
     * Requires the buffer to have been created with
     * VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
     */
    [[nodiscard]] VkDeviceAddress deviceAddress(const vk::raii::Device &device) const noexcept;

    /**
     * @brief Query the actual memory property flags for this allocation
     * @return Vulkan memory property flags (HOST_VISIBLE, DEVICE_LOCAL, etc.)
     */
    [[nodiscard]] VkMemoryPropertyFlags memoryProperties() const;

    /// Check if the underlying memory is host-visible
    [[nodiscard]] bool isHostVisible() const;

    /**
     * @brief Flush host writes to make them visible to the device.
     *
     * VMA internally handles non-coherent atom-size alignment.
     */
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = std::numeric_limits<VkDeviceSize>::max()) const;

    /**
     * @brief Invalidate host cache to make device writes visible to the CPU.
     *
     * VMA internally handles non-coherent atom-size alignment.
     */
    void invalidate(VkDeviceSize offset = 0, VkDeviceSize size = std::numeric_limits<VkDeviceSize>::max()) const;
};

// =========================================================================
// VmaImage — RAII image + allocation pair
// =========================================================================

/**
 * @brief RAII wrapper owning a VkImage and its VmaAllocation
 *
 * Destruction calls vmaDestroyImage. Images are typically GPU-only
 * and do not expose mapped pointer access.
 */
struct VmaImage
{
    VmaAllocator allocator = nullptr;
    VkImage image = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};

    VmaImage() = default;

    VmaImage(VmaAllocator alloc, VkImage img, VmaAllocation mem, VmaAllocationInfo allocInfo);

    ~VmaImage();

    // Move-only
    VmaImage(const VmaImage &) = delete;
    VmaImage &operator=(const VmaImage &) = delete;

    VmaImage(VmaImage &&other) noexcept;

    VmaImage &operator=(VmaImage &&other) noexcept;

    /// Check if this image holds a valid allocation
    [[nodiscard]] bool valid() const noexcept;

    /// Get the raw VkImage handle
    [[nodiscard]] VkImage handle() const noexcept;

    /// Get the allocation size in bytes
    [[nodiscard]] VkDeviceSize size() const noexcept;

    /// Query memory property flags
    [[nodiscard]] VkMemoryPropertyFlags memoryProperties() const;
};

// =========================================================================
// VmaPoolHandle — RAII wrapper for VmaPool
// =========================================================================

/**
 * @brief RAII wrapper for a VMA custom allocation pool
 *
 * All allocations from this pool must be freed before the pool is destroyed.
 * Supports linear allocation algorithm for per-frame ring-buffer patterns.
 */
struct VmaPoolHandle
{
    VmaAllocator allocator = nullptr;
    VmaPool pool = nullptr;
    std::optional<VmaPoolCreateInfo> createInfo{};

    VmaPoolHandle() = default;

    VmaPoolHandle(VmaAllocator alloc, VmaPool p);

    VmaPoolHandle(VmaAllocator alloc, VmaPool p, const VmaPoolCreateInfo &info);

    ~VmaPoolHandle();

    // Move-only
    VmaPoolHandle(const VmaPoolHandle &) = delete;
    VmaPoolHandle &operator=(const VmaPoolHandle &) = delete;

    VmaPoolHandle(VmaPoolHandle &&other) noexcept;

    VmaPoolHandle &operator=(VmaPoolHandle &&other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] VmaPool handle() const noexcept;

    /**
     * @brief Reset all allocations in this pool (linear pools only)
     *
     * For pools created with VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
     * VMA 3.3 removed vmaResetPool, so we recreate the pool to free all allocations.
     * Ideal for per-frame transient resource pools.
     */
    void reset();

    /// Get pool statistics (fast, safe to call per-frame)
    [[nodiscard]] VmaStatistics statistics() const;
};

// =========================================================================
// VmaAllocatorWrapper — RAII VmaAllocator + creation/statistics
// =========================================================================

/**
 * @brief RAII wrapper around VmaAllocator
 *
 * Responsibilities:
 * - Create and destroy VmaAllocator with correct Vulkan 1.4 flags
 * - Provide thin passthrough methods for buffer/image/pool creation
 * - Expose budget and statistics queries
 *
 * Thread Safety:
 * - All allocation/deallocation calls are internally synchronized by VMA
 * - Individual VmaAllocation handles require external synchronization
 *
 * This class does NOT own Vulkan device/instance — they must outlive it.
 */
class VmaAllocatorWrapper
{
  public:
    VmaAllocatorWrapper() = default;

    /**
     * @brief Initialize VMA allocator for a Vulkan 1.4 device
     *
     * @param instance    RAII instance (must outlive this allocator)
     * @param physDevice  RAII physical device (must outlive this allocator)
     * @param device      RAII device (must outlive this allocator)
     *
     * Enables flags for:
     * - Buffer device address (core 1.2)
     * - Maintenance4 (core 1.3)
     * - Maintenance5 (core 1.3)
     * - EXT memory budget query integration
     */
    VmaAllocatorWrapper(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice,
                        const vk::raii::Device &device);

    ~VmaAllocatorWrapper();

    // Move-only
    VmaAllocatorWrapper(const VmaAllocatorWrapper &) = delete;
    VmaAllocatorWrapper &operator=(const VmaAllocatorWrapper &) = delete;

    VmaAllocatorWrapper(VmaAllocatorWrapper &&other) noexcept;

    VmaAllocatorWrapper &operator=(VmaAllocatorWrapper &&other) noexcept;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] VmaAllocator handle() const noexcept;

    // =====================================================================
    // Resource Creation
    // =====================================================================

    /**
     * @brief Create a VkBuffer with VMA allocation
    * @param bufferInfo  Vulkan buffer create info (Vulkan-hpp type)
     * @param allocInfo   VMA allocation create info (usage, flags, priority, pool)
     * @return RAII VmaBuffer owning both the buffer and allocation
     */
    [[nodiscard]] VmaBuffer createBuffer(const vk::BufferCreateInfo &bufferInfo,
                                         const VmaAllocationCreateInfo &allocInfo) const;

    /**
     * @brief Create a VkImage with VMA allocation
    * @param imageInfo   Vulkan image create info (Vulkan-hpp type)
     * @param allocInfo   VMA allocation create info
     * @return RAII VmaImage owning both the image and allocation
     */
    [[nodiscard]] VmaImage createImage(const vk::ImageCreateInfo &imageInfo,
                                       const VmaAllocationCreateInfo &allocInfo) const;

    /**
     * @brief Create a custom VMA pool
     * @param poolInfo  Pool creation parameters (memory type, algorithm, block size)
     * @return RAII VmaPoolHandle
     */
    [[nodiscard]] VmaPoolHandle createPool(const VmaPoolCreateInfo &poolInfo) const;

    /**
     * @brief Find the memory type index suitable for a buffer configuration
    * @param bufferInfo  Sample buffer create info (Vulkan-hpp type)
     * @param allocInfo   Desired allocation properties
     * @return Memory type index for VmaPoolCreateInfo
     */
    [[nodiscard]] std::uint32_t findMemoryTypeIndexForBuffer(const vk::BufferCreateInfo &bufferInfo,
                                                             const VmaAllocationCreateInfo &allocInfo) const;

    /**
     * @brief Find the memory type index suitable for an image configuration
    * @param imageInfo   Sample image create info (Vulkan-hpp type)
     * @param allocInfo   Desired allocation properties
     * @return Memory type index for VmaPoolCreateInfo
     */
    [[nodiscard]] std::uint32_t findMemoryTypeIndexForImage(const vk::ImageCreateInfo &imageInfo,
                                                            const VmaAllocationCreateInfo &allocInfo) const;

    // =====================================================================
    // Statistics & Budget
    // =====================================================================

    /**
     * @brief Get per-heap memory budgets (fast, safe to call every frame)
     *
     * Returns budget info for each memory heap including:
     * - Allocated block bytes and allocation bytes
     * - OS-reported usage and budget (with VK_EXT_memory_budget)
     */
    [[nodiscard]] std::vector<VmaBudget> getBudgets() const;

    /**
     * @brief Calculate comprehensive statistics (slow, debug only)
     *
     * Traverses all internal data structures. Use sparingly.
     */
    [[nodiscard]] VmaTotalStatistics calculateStatistics() const;

    /**
     * @brief Dump VMA statistics as JSON string
     *
     * Produces a detailed JSON report for visualization and debugging tools.
     * @param detailedMap Include per-allocation details
     * @return JSON string (heap-allocated, returned by value)
     */
    [[nodiscard]] std::string dumpStatsJson(bool detailedMap = true) const;

  private:
    VmaAllocator allocator_ = nullptr;
};

} // namespace nr::rhi
