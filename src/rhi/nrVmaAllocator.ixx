module;
export module nr.rhi:vmaAllocator;
import dependency;
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
    VmaAllocator  allocator  = nullptr;
    VkBuffer buffer = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};

    VmaBuffer() = default;

    VmaBuffer(VmaAllocator alloc, VkBuffer buf, VmaAllocation mem, VmaAllocationInfo allocInfo)
        : allocator(alloc), buffer(buf), allocation(mem), info(allocInfo)
    {
    }

    ~VmaBuffer()
    {
        if (buffer != nullptr)
        {
            vmaDestroyBuffer(allocator, buffer, allocation);
        }
    }

    // Move-only
    VmaBuffer(const VmaBuffer &) = delete;
    VmaBuffer &operator=(const VmaBuffer &) = delete;

    VmaBuffer(VmaBuffer &&other) noexcept
        : allocator(other.allocator), buffer(other.buffer), allocation(other.allocation), info(other.info)
    {
        other.allocator = nullptr;
        other.buffer = nullptr;
        other.allocation = nullptr;
        other.info = {};
    }

    VmaBuffer &operator=(VmaBuffer &&other) noexcept
    {
        if (this != &other)
        {
            if (buffer != nullptr)
            {
                vmaDestroyBuffer(allocator, buffer, allocation);
            }
            allocator = other.allocator;
            buffer = other.buffer;
            allocation = other.allocation;
            info = other.info;
            other.allocator = nullptr;
            other.buffer = nullptr;
            other.allocation = nullptr;
            other.info = {};
        }
        return *this;
    }

    /// Check if this buffer holds a valid allocation
    [[nodiscard]] bool valid() const noexcept { return buffer != nullptr; }

    /// Get the persistently mapped pointer (nullptr if not mapped)
    [[nodiscard]] void *mapped() const noexcept { return info.pMappedData; }

    /// Get the allocation size in bytes
    [[nodiscard]] VkDeviceSize size() const noexcept { return info.size; }

    /// Get the raw VkBuffer handle
    [[nodiscard]] VkBuffer handle() const noexcept { return buffer; }

    /**
     * @brief Get the buffer device address (BDA)
     * @param device The Vulkan device (needed for vkGetBufferDeviceAddress)
     * @return 64-bit device address for shader access
     *
     * Requires the buffer to have been created with
     * VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
     */
    [[nodiscard]] VkDeviceAddress deviceAddress(VkDevice device) const noexcept
    {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        return vkGetBufferDeviceAddress(device, &addressInfo);
    }

    /**
     * @brief Query the actual memory property flags for this allocation
     * @return Vulkan memory property flags (HOST_VISIBLE, DEVICE_LOCAL, etc.)
     */
    [[nodiscard]] VkMemoryPropertyFlags memoryProperties() const
    {
        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(allocator, allocation, &flags);
        return flags;
    }

    /// Check if the underlying memory is host-visible
    [[nodiscard]] bool isHostVisible() const
    {
        return (memoryProperties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }
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
    VmaAllocator  allocator  = nullptr;
    VkImage       image      = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};

    VmaImage() = default;

    VmaImage(VmaAllocator alloc, VkImage img, VmaAllocation mem, VmaAllocationInfo allocInfo)
        : allocator(alloc), image(img), allocation(mem), info(allocInfo)
    {
    }

    ~VmaImage()
    {
        if (image != nullptr)
        {
            vmaDestroyImage(allocator, image, allocation);
        }
    }

    // Move-only
    VmaImage(const VmaImage &) = delete;
    VmaImage &operator=(const VmaImage &) = delete;

    VmaImage(VmaImage &&other) noexcept
        : allocator(other.allocator), image(other.image), allocation(other.allocation), info(other.info)
    {
        other.image = nullptr;
        other.allocation = nullptr;
        other.info = {};
    }

    VmaImage &operator=(VmaImage &&other) noexcept
    {
        if (this != &other)
        {
            if (image != nullptr)
            {
                vmaDestroyImage(allocator, image, allocation);
            }
            allocator = other.allocator;
            image = other.image;
            allocation = other.allocation;
            info = other.info;
            other.image = nullptr;
            other.allocation = nullptr;
            other.info = {};
        }
        return *this;
    }

    /// Check if this image holds a valid allocation
    [[nodiscard]] bool valid() const noexcept { return image != nullptr; }

    /// Get the raw VkImage handle
    [[nodiscard]] VkImage handle() const noexcept { return image; }

    /// Get the allocation size in bytes
    [[nodiscard]] VkDeviceSize size() const noexcept { return info.size; }

    /// Query memory property flags
    [[nodiscard]] VkMemoryPropertyFlags memoryProperties() const
    {
        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(allocator, allocation, &flags);
        return flags;
    }
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
    VmaPool      pool      = nullptr;
    std::optional<VmaPoolCreateInfo> createInfo{};

    VmaPoolHandle() = default;

    VmaPoolHandle(VmaAllocator alloc, VmaPool p) : allocator(alloc), pool(p) {}

    VmaPoolHandle(VmaAllocator alloc, VmaPool p, const VmaPoolCreateInfo &info)
        : allocator(alloc), pool(p), createInfo(info)
    {
    }

    ~VmaPoolHandle()
    {
        if (pool != nullptr)
        {
            vmaDestroyPool(allocator, pool);
        }
    }

    // Move-only
    VmaPoolHandle(const VmaPoolHandle &) = delete;
    VmaPoolHandle &operator=(const VmaPoolHandle &) = delete;

    VmaPoolHandle(VmaPoolHandle &&other) noexcept
        : allocator(other.allocator), pool(other.pool), createInfo(std::move(other.createInfo))
    {
        other.allocator = nullptr;
        other.pool = nullptr;
        other.createInfo.reset();
    }

    VmaPoolHandle &operator=(VmaPoolHandle &&other) noexcept
    {
        if (this != &other)
        {
            if (pool != nullptr)
            {
                vmaDestroyPool(allocator, pool);
            }
            allocator = other.allocator;
            pool = other.pool;
            createInfo = std::move(other.createInfo);
            other.allocator = nullptr;
            other.pool = nullptr;
            other.createInfo.reset();
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return pool != nullptr; }
    [[nodiscard]] VmaPool handle() const noexcept { return pool; }

    /**
     * @brief Reset all allocations in this pool (linear pools only)
     *
     * For pools created with VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT,
     * VMA 3.3 removed vmaResetPool, so we recreate the pool to free all allocations.
     * Ideal for per-frame transient resource pools.
     */
    void reset()
    {
        if (pool != nullptr)
        {
            nrAssert(createInfo.has_value(), std::format("VmaPoolHandle::reset requires pool creation info."));

            vmaDestroyPool(allocator, pool);
            pool = nullptr;

            VmaPool newPool = nullptr;
            VkResult result = vmaCreatePool(allocator, &(*createInfo), &newPool);
            nrAssert(result == VK_SUCCESS, std::format("vmaCreatePool failed: {}", static_cast<int>(result)));
            pool = newPool;
        }
    }

    /// Get pool statistics (fast, safe to call per-frame)
    [[nodiscard]] VmaStatistics statistics() const
    {
        VmaStatistics stats{};
        if (pool != nullptr)
        {
            vmaGetPoolStatistics(allocator, pool, &stats);
        }
        return stats;
    }
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
     * - Dedicated allocations (core 1.1)
     * - Bind memory 2 (core 1.1)
     * - Buffer device address (core 1.2)
     * - Maintenance4 (core 1.3)
     * - Maintenance5 (core 1.3)
     */
    VmaAllocatorWrapper(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice, const vk::raii::Device &device)
    {
        VmaVulkanFunctions vulkanFunctions{};
        vulkanFunctions.vkGetInstanceProcAddr = instance.getDispatcher()->vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = device.getDispatcher()->vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo createInfo{};
        createInfo.flags = VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT | VMA_ALLOCATOR_CREATE_KHR_BIND_MEMORY2_BIT | VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
        createInfo.vulkanApiVersion = vk::ApiVersion14;
        createInfo.physicalDevice = *physDevice;
        createInfo.device = *device;
        createInfo.instance = *instance;
        createInfo.pVulkanFunctions = &vulkanFunctions;

        VkResult result = vmaCreateAllocator(&createInfo, &allocator_);
        nrAssert(result == VkResult::VK_SUCCESS, std::format("Failed to create VMA allocator: {}", static_cast<int>(result)));
    }

    ~VmaAllocatorWrapper()
    {
        if (allocator_ != nullptr)
        {
            vmaDestroyAllocator(allocator_);
        }
    }

    // Move-only
    VmaAllocatorWrapper(const VmaAllocatorWrapper &) = delete;
    VmaAllocatorWrapper &operator=(const VmaAllocatorWrapper &) = delete;

    VmaAllocatorWrapper(VmaAllocatorWrapper &&other) noexcept : allocator_(other.allocator_)
    {
        other.allocator_ = nullptr;
    }

    VmaAllocatorWrapper &operator=(VmaAllocatorWrapper &&other) noexcept
    {
        if (this != &other)
        {
            if (allocator_ != nullptr)
            {
                vmaDestroyAllocator(allocator_);
            }
            allocator_ = other.allocator_;
            other.allocator_ = nullptr;
        }
        return *this;
    }

    [[nodiscard]] bool valid() const noexcept { return allocator_ != nullptr; }
    [[nodiscard]] VmaAllocator handle() const noexcept { return allocator_; }

    // =====================================================================
    // Resource Creation
    // =====================================================================

    /**
     * @brief Create a VkBuffer with VMA allocation
    * @param bufferInfo  Vulkan buffer create info (Vulkan-hpp type)
     * @param allocInfo   VMA allocation create info (usage, flags, priority, pool)
     * @return RAII VmaBuffer owning both the buffer and allocation
     */
    [[nodiscard]] VmaBuffer createBuffer(const vk::BufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo) const
    {
        VkBuffer buffer = nullptr;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo resultInfo{};

        // Convert to C struct for VMA API
        VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkResult result = vmaCreateBuffer(allocator_, &cBufferInfo, &allocInfo, &buffer, &allocation, &resultInfo);
        nrAssert(result == VK_SUCCESS, std::format("vmaCreateBuffer failed: {}", static_cast<int>(result)));

        return VmaBuffer(allocator_, buffer, allocation, resultInfo);
    }

    /**
     * @brief Create a VkImage with VMA allocation
    * @param imageInfo   Vulkan image create info (Vulkan-hpp type)
     * @param allocInfo   VMA allocation create info
     * @return RAII VmaImage owning both the image and allocation
     */
    [[nodiscard]] VmaImage createImage(const vk::ImageCreateInfo &imageInfo, const VmaAllocationCreateInfo &allocInfo) const
    {
        VkImage image = nullptr;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo resultInfo{};

        // Convert to C struct for VMA API
        VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkResult result = vmaCreateImage(allocator_, &cImageInfo, &allocInfo, &image, &allocation, &resultInfo);
        nrAssert(result == VK_SUCCESS, std::format("vmaCreateImage failed: {}", static_cast<int>(result)));

        return VmaImage(allocator_, image, allocation, resultInfo);
    }

    /**
     * @brief Create a custom VMA pool
     * @param poolInfo  Pool creation parameters (memory type, algorithm, block size)
     * @return RAII VmaPoolHandle
     */
    [[nodiscard]] VmaPoolHandle createPool(const VmaPoolCreateInfo &poolInfo) const
    {
        VmaPool pool = nullptr;
        VkResult result = vmaCreatePool(allocator_, &poolInfo, &pool);
        nrAssert(result == VK_SUCCESS, std::format("vmaCreatePool failed: {}", static_cast<int>(result)));

        return VmaPoolHandle(allocator_, pool, poolInfo);
    }

    /**
     * @brief Find the memory type index suitable for a buffer configuration
    * @param bufferInfo  Sample buffer create info (Vulkan-hpp type)
     * @param allocInfo   Desired allocation properties
     * @return Memory type index for VmaPoolCreateInfo
     */
    [[nodiscard]] uint32_t findMemoryTypeIndexForBuffer(const vk::BufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo) const
    {
        uint32_t memTypeIndex = 0;
        // Convert to C struct for VMA API
        VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkResult result = vmaFindMemoryTypeIndexForBufferInfo(allocator_, &cBufferInfo, &allocInfo, &memTypeIndex);
        nrAssert(result == VK_SUCCESS, std::format("vmaFindMemoryTypeIndexForBufferInfo failed: {}", static_cast<int>(result)));
        return memTypeIndex;
    }

    /**
     * @brief Find the memory type index suitable for an image configuration
    * @param imageInfo   Sample image create info (Vulkan-hpp type)
     * @param allocInfo   Desired allocation properties
     * @return Memory type index for VmaPoolCreateInfo
     */
    [[nodiscard]] uint32_t findMemoryTypeIndexForImage(const vk::ImageCreateInfo &imageInfo, const VmaAllocationCreateInfo &allocInfo) const
    {
        uint32_t memTypeIndex = 0;
        // Convert to C struct for VMA API
        VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkResult result = vmaFindMemoryTypeIndexForImageInfo(allocator_, &cImageInfo, &allocInfo, &memTypeIndex);
        nrAssert(result == VK_SUCCESS, std::format("vmaFindMemoryTypeIndexForImageInfo failed: {}", static_cast<int>(result)));
        return memTypeIndex;
    }

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
    [[nodiscard]] std::vector<VmaBudget> getBudgets() const
    {
        constexpr size_t maxHeaps = 16; // VMA supports up to 16 memory heaps
        std::vector<VmaBudget> budgets(maxHeaps);
        vmaGetHeapBudgets(allocator_, budgets.data());
        return budgets;
    }

    /**
     * @brief Calculate comprehensive statistics (slow, debug only)
     *
     * Traverses all internal data structures. Use sparingly.
     */
    [[nodiscard]] VmaTotalStatistics calculateStatistics() const
    {
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator_, &stats);
        return stats;
    }

    /**
     * @brief Dump VMA statistics as JSON string
     *
     * Produces a detailed JSON report for visualization and debugging tools.
     * @param detailedMap Include per-allocation details
     * @return JSON string (heap-allocated, returned by value)
     */
    [[nodiscard]] std::string dumpStatsJson(bool detailedMap = true) const
    {
        char *statsString = nullptr;
        vmaBuildStatsString(allocator_, &statsString, static_cast<VkBool32>(detailedMap ? 1 : 0));
        std::string result(statsString);
        vmaFreeStatsString(allocator_, statsString);
        return result;
    }

  private:
    VmaAllocator allocator_ = nullptr;
};

} // namespace nr::rhi
