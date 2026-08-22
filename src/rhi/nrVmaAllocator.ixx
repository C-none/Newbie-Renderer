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
 * - VmaResource<Kind>:    NTTP RAII wrapper for VkBuffer/VkImage + VmaAllocation pair
 * - VmaBuffer/VmaImage:   aliases for VmaResource<Buffer> / VmaResource<Image>
 * - VmaPoolHandle:        RAII wrapper for VmaPool
 * - VmaAllocatorWrapper:  RAII wrapper for VmaAllocator + resource creation
 *
 * All types are move-only, following the project's RAII conventions.
 * VMA is internally thread-safe by default (no EXTERNALLY_SYNCHRONIZED).
 *
 * Higher-level strategy allocation is in nrMemoryAllocator.ixx (:memoryAllocator).
 */

export namespace nr::rhi
{

// =========================================================================
// VmaResource — NTTP RAII resource + allocation pair
// =========================================================================

enum class VmaResourceKind
{
    Buffer,
    Image,
};

namespace detail
{
template <VmaResourceKind Kind>
struct VmaResourceTraits;

template <>
struct VmaResourceTraits<VmaResourceKind::Buffer>
{
    using HandleType = VkBuffer;
    static void destroy(VmaAllocator allocator, VkBuffer handle, VmaAllocation allocation)
    {
        vmaDestroyBuffer(allocator, handle, allocation);
    }
};

template <>
struct VmaResourceTraits<VmaResourceKind::Image>
{
    using HandleType = VkImage;
    static void destroy(VmaAllocator allocator, VkImage handle, VmaAllocation allocation)
    {
        vmaDestroyImage(allocator, handle, allocation);
    }
};
} // namespace detail

/**
 * @brief RAII wrapper owning a VMA-managed resource and its VmaAllocation
 *
 * @tparam Kind VmaResourceKind::Buffer or VmaResourceKind::Image
 *
 * Destruction calls vmaDestroyBuffer / vmaDestroyImage (selected by Kind)
 * which frees both the Vulkan handle and the underlying device memory.
 * Cached VmaAllocationInfo provides zero-cost access to mapped pointer and size.
 */
template <VmaResourceKind Kind>
struct VmaResource
{
    using HandleType = typename detail::VmaResourceTraits<Kind>::HandleType;

    VmaAllocator allocator = nullptr;
    HandleType resource = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo info{};

    VmaResource() = default;

    VmaResource(VmaAllocator alloc, HandleType res, VmaAllocation mem, VmaAllocationInfo allocInfo)
        : allocator(alloc), resource(res), allocation(mem), info(allocInfo)
    {
    }

    ~VmaResource()
    {
        if (resource != nullptr)
        {
            detail::VmaResourceTraits<Kind>::destroy(allocator, resource, allocation);
        }
    }

    // Move-only
    VmaResource(const VmaResource &) = delete;
    VmaResource &operator=(const VmaResource &) = delete;

    VmaResource(VmaResource &&other) noexcept
        : allocator(other.allocator), resource(other.resource), allocation(other.allocation), info(other.info)
    {
        other.allocator = nullptr;
        other.resource = nullptr;
        other.allocation = nullptr;
        other.info = {};
    }

    VmaResource &operator=(VmaResource &&other) noexcept
    {
        if (this != &other)
        {
            if (resource != nullptr)
            {
                detail::VmaResourceTraits<Kind>::destroy(allocator, resource, allocation);
            }
            allocator = other.allocator;
            resource = other.resource;
            allocation = other.allocation;
            info = other.info;
            other.allocator = nullptr;
            other.resource = nullptr;
            other.allocation = nullptr;
            other.info = {};
        }
        return *this;
    }

    /// Check if this resource holds a valid allocation
    [[nodiscard]] bool valid() const noexcept
    {
        return resource != nullptr;
    }

    /// Get the persistently mapped pointer (nullptr if not mapped)
    [[nodiscard]] void *mapped() const noexcept
        requires(Kind == VmaResourceKind::Buffer)
    {
        return info.pMappedData;
    }

    /// Get the raw resource handle
    [[nodiscard]] HandleType handle() const noexcept
    {
        return resource;
    }

    /**
     * @brief Get the buffer device address (BDA)
     * @param device The Vulkan device (needed for vkGetBufferDeviceAddress)
     * @return 64-bit device address for shader access
     *
     * Requires the buffer to have been created with
     * VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.
     */
    [[nodiscard]] VkDeviceAddress deviceAddress(const vk::raii::Device &device) const noexcept
        requires(Kind == VmaResourceKind::Buffer)
    {
        return static_cast<VkDeviceAddress>(device.getBufferAddress(vk::BufferDeviceAddressInfo{vk::Buffer{resource}}));
    }

    /**
     * @brief Flush host writes to make them visible to the device.
     *
     * VMA internally handles non-coherent atom-size alignment.
     */
    void flush(VkDeviceSize offset = 0, VkDeviceSize size = std::numeric_limits<VkDeviceSize>::max()) const
        requires(Kind == VmaResourceKind::Buffer)
    {
        if (allocation == nullptr)
            return;
        auto result = vmaFlushAllocation(allocator, allocation, offset, size);
        nrAssert(result == VK_SUCCESS, "vmaFlushAllocation failed: {}", static_cast<int>(result));
    }

    /**
     * @brief Invalidate host cache to make device writes visible to the CPU.
     *
     * VMA internally handles non-coherent atom-size alignment.
     */
    void invalidate(VkDeviceSize offset = 0, VkDeviceSize size = std::numeric_limits<VkDeviceSize>::max()) const
        requires(Kind == VmaResourceKind::Buffer)
    {
        if (allocation == nullptr)
            return;
        auto result = vmaInvalidateAllocation(allocator, allocation, offset, size);
        nrAssert(result == VK_SUCCESS, "vmaInvalidateAllocation failed: {}", static_cast<int>(result));
    }
};

using VmaBuffer = VmaResource<VmaResourceKind::Buffer>;
using VmaImage  = VmaResource<VmaResourceKind::Image>;

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

};

// =========================================================================
// VmaAllocatorWrapper — RAII VmaAllocator + resource creation
// =========================================================================

/**
 * @brief RAII wrapper around VmaAllocator
 *
 * Responsibilities:
 * - Create and destroy VmaAllocator with correct Vulkan 1.4 flags
 * - Provide thin passthrough methods for buffer/image/pool creation
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

  private:
    VmaAllocator allocator_ = nullptr;
};

} // namespace nr::rhi
