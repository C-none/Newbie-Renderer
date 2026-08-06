export module nr.rhi:resource;
import dependency.vma;
import dependency.vulkan;
import :type;
import :vmaAllocator;
import :memoryAllocator;
import std;

/**
 * @file nrResource.ixx
 * @brief RAII wrappers for GPU Buffer and Image resources
 *
 * Sits above MemoryAllocator in the memory management stack:
 *
 *   nrResource.ixx       <- this file (Buffer/Image with views & metadata)
 *   nrMemoryAllocator.ixx  (strategy-based allocation)
 *   nrVmaAllocator.ixx     (VMA ownership wrapper)
 *   Vulkan Device Layer
 *
 * Buffer: Owns VmaBuffer + named BufferView map + cached BDA
 * Image:  Owns VmaImage + its default ImageView
 *
 * Both accept vulkan-hpp create infos directly (vk::BufferCreateInfo,
 * vk::ImageCreateInfo) plus MemoryUsage / AllocationStrategy as
 * separate parameters, staying close to the native Vulkan API.
 *
 * All types are move-only and follow the project's RAII conventions.
 * Creation goes through static factory methods.
 */

export namespace nr::rhi
{

// =========================================================================
// Buffer — RAII GPU buffer with named view map and cached device address
// =========================================================================

/**
 * @brief High-level RAII buffer resource
 *
 * Wraps a VmaBuffer (owns VkBuffer + VMA allocation) and adds:
 * - Cached buffer device address (BDA) for shader access
 * - Named buffer view map (for texel buffer usage)
 * - vk::Buffer handle accessor for vulkan-hpp interop
 * - Stored metadata (size, usage, sharingMode) for cache keying
 *
 * Construction is through the static `create()` factory.
 * Names are immutable after creation (set once in create()).
 * Destruction cascades: BufferViews -> VmaBuffer -> VMA deallocation.
 */
class Buffer
{
  public:
    Buffer() = default;

    // Move-only
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&) noexcept = default;
    Buffer &operator=(Buffer &&other) noexcept;

    /**
     * @brief Create a buffer from a vulkan-hpp create info
     *
     * @param allocator   Initialized MemoryAllocator
     * @param device      RAII device (must outlive this buffer for view creation / BDA)
     * @param createInfo  Vulkan buffer create info (size, usage, sharingMode, etc.)
     * @param name        Optional debug name for profiling tools (RenderDoc, PIX, Nsight)
     * @param memoryUsage Memory placement intent (GpuOnly, CpuToGpu, etc.)
     * @param strategy    Allocation lifetime strategy
     * @param frameIndex  Frame index for PerFrame strategy (ignored otherwise)
     * @return Fully initialized Buffer
     */
    [[nodiscard]] static Buffer create(const MemoryAllocator &allocator, const vk::raii::Device &device,
                                       const vk::BufferCreateInfo &createInfo, std::string_view name,
                                       MemoryUsage memoryUsage = MemoryUsage::GpuOnly,
                                       AllocationStrategy strategy = AllocationStrategy::CrossFrame,
                                       std::uint32_t frameIndex = 0);
    /// Get the vulkan-hpp buffer handle (non-owning)
    [[nodiscard]] vk::Buffer handle() const noexcept;

    /// Get the persistently mapped pointer (nullptr if not host-visible)
    [[nodiscard]] void *mapped() const noexcept;

    /// Get the allocation size in bytes
    [[nodiscard]] vk::DeviceSize size() const noexcept;

    /// Get the buffer usage flags
    [[nodiscard]] vk::BufferUsageFlags usage() const noexcept;

    /// Get the sharing mode
    [[nodiscard]] vk::SharingMode sharingMode() const noexcept;

    /// Check if this buffer holds a valid allocation
    [[nodiscard]] bool valid() const noexcept;

    /**
     * @brief Invalidate host cache for this buffer range after GPU writes.
     */
    void invalidate(vk::DeviceSize offset = 0, vk::DeviceSize size = vk::WholeSize) const;

    /**
     * @brief Get the buffer device address (BDA), cached after first call
     *
     * @return 64-bit device address for shader access
     *
     * Requires the buffer to have SHADER_DEVICE_ADDRESS_BIT usage
     * (automatically added by MemoryAllocator for GpuOnly; CpuToGpu callers
     * that need an address must request eShaderDeviceAddress explicitly).
     */
    [[nodiscard]] VkDeviceAddress deviceAddress() const;

    /**
     * @brief Add a named buffer view for texel buffer usage
     *
     * Creates a VkBufferView and stores it in the named view map.
     * If a view with the same key already exists, it is reused.
     * When GPU debug names are enabled, sets a Vulkan debug name on the view.
     *
     * @param format   Texel format interpretation
     * @param offset   Byte offset into the buffer (default: 0)
     * @param range    Byte range (default: vk::WholeSize for entire buffer)
     * @param viewName Optional custom key; if empty, auto-generates from parameters
     *                 (e.g. "{bufferName}_fmt44_off0_whole" for R32Sfloat at offset 0)
     * @return Reference to the created RAII vk::raii::BufferView
     */
    std::reference_wrapper<const vk::raii::BufferView> addView(vk::Format format, vk::DeviceSize offset = 0,
                                                               vk::DeviceSize range = vk::WholeSize,
                                                               std::string_view viewName = "");

    /// Write a single typed value to a mapped buffer and flush it.
    template <typename T>
        requires(!std::is_pointer_v<T> && std::is_trivially_copyable_v<T>)
    void writeMappedAndFlush(const T &data, vk::DeviceSize offset = 0)
    {
        writeMappedAndFlush(std::addressof(data), sizeof(T), offset);
    }

    /// Write a span of data to a mapped buffer and flush it.
    template <typename T>
        requires std::is_trivially_copyable_v<T>
    void writeMappedAndFlush(std::span<const T> data, vk::DeviceSize offset = 0)
    {
        writeMappedAndFlush(data.data(), data.size_bytes(), offset);
    }

  private:
    void writeMappedAndFlush(const void *data, std::size_t dataSize, vk::DeviceSize offset);

    VmaBuffer vmaBuffer_;

    // Stored metadata (extracted from vk::BufferCreateInfo at creation)
    vk::DeviceSize size_ = 0;
    vk::BufferUsageFlags usage_ = {};
    vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive;
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    mutable std::optional<VkDeviceAddress> cachedAddress_{};
    std::map<std::string, vk::raii::BufferView> bufferViews_; ///< Named view map (destroyed before VmaBuffer)
    std::string name_;                                        ///< Immutable after creation
};

// =========================================================================
// Image — RAII GPU image with a default view
// =========================================================================

/**
 * @brief High-level RAII image resource
 *
 * Wraps a VmaImage (owns VkImage + VMA allocation) and adds:
 * - Default vk::raii::ImageView (full mip range, full layer range)
 * - vk::Image handle accessor for vulkan-hpp interop
 * - Stored metadata from vk::ImageCreateInfo for cache keying
 *
 * Construction is through the static `create()` factory which accepts
 * a standard vk::ImageCreateInfo plus MemoryUsage.
 * Optional Vulkan debug names are applied during creation.
 * Destruction cascades: default view -> VmaImage -> VMA deallocation.
 */
class Image
{
  public:
    Image() = default;

    // Move-only
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&) noexcept = default;
    Image &operator=(Image &&other) noexcept;

    /**
     * @brief Create an image from a vulkan-hpp create info
     *
     * Allocates the VkImage via MemoryAllocator and creates a default
     * ImageView covering all mip levels and array layers. The view type
     * and aspect flags are inferred from the create info.
     *
     * @param allocator    Initialized MemoryAllocator
     * @param device       RAII device (must outlive this image)
     * @param createInfo   Vulkan image create info; usage must support the owned default view
     * @param name         Optional debug name for profiling tools (RenderDoc, PIX, Nsight)
     * @param memoryUsage  Memory placement intent (default: GpuOnly)
     * @return Fully initialized Image with default view
     */
    [[nodiscard]] static Image create(const MemoryAllocator &allocator, const vk::raii::Device &device,
                                      const vk::ImageCreateInfo &createInfo, std::string_view name,
                                      MemoryUsage memoryUsage = MemoryUsage::GpuOnly);

    /// Get the vulkan-hpp image handle (non-owning)
    [[nodiscard]] vk::Image handle() const noexcept;

    /// Get the default RAII image view (full mip range, full layer range)
    [[nodiscard]] const vk::raii::ImageView &view() const noexcept;

    /// Get the image format
    [[nodiscard]] vk::Format format() const noexcept;

    /// Get the image extent
    [[nodiscard]] vk::Extent3D extent() const noexcept;

    /// Get the number of mip levels
    [[nodiscard]] std::uint32_t mipLevels() const noexcept;

    /// Get the number of array layers
    [[nodiscard]] std::uint32_t arrayLayers() const noexcept;

    /// Get the image tiling
    [[nodiscard]] vk::ImageTiling tiling() const noexcept;

    /// Get the image usage flags
    [[nodiscard]] vk::ImageUsageFlags usage() const noexcept;

    /// Get the sharing mode
    [[nodiscard]] vk::SharingMode sharingMode() const noexcept;

    /// Check if this image holds a valid allocation
    [[nodiscard]] bool valid() const noexcept;

  private:
    /**
     * @brief Create the default image view covering all mips and layers
     */
    void createDefaultView(const vk::raii::Device &device, vk::ImageType imageType);

    VmaImage vmaImage_;

    // Stored metadata (extracted from vk::ImageCreateInfo at creation)
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent3D extent_ = {0, 0, 0};
    std::uint32_t mipLevels_ = 1;
    std::uint32_t arrayLayers_ = 1;
    vk::ImageTiling tiling_ = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags usage_ = {};
    vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive;
    vk::raii::ImageView defaultView_ = {nullptr};
};

} // namespace nr::rhi
