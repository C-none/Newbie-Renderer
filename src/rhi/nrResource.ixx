export module nr.rhi:resource;
import dependency;
import nr.utils;
import :type;
import :vk;
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
 *   nrVmaAllocator.ixx     (VMA wrapper + statistics)
 *   Vulkan Device Layer
 *
 * Buffer: Owns VmaBuffer + named BufferView map + cached BDA
 * Image:  Owns VmaImage  + default ImageView + auxiliary view map
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

class Buffer;
class Image;

// Concepts for resource type identification
template <typename T>
concept BufferLike = std::same_as<std::remove_cvref_t<T>, Buffer>;

template <typename T>
concept ImageLike = std::same_as<std::remove_cvref_t<T>, Image>;

/**
 * @brief Set VK_EXT_debug_utils name on a resource (Buffer or Image)
 *
 * For Image, also names the default view with "_defaultView" suffix.
 */
template <typename Resource>
    requires BufferLike<Resource> || ImageLike<Resource>
void setResourceDebugName(Resource &resource)
{
    if constexpr (isDebugMode)
    {
        nrAssert(resource.valid(), "Cannot set debug name on invalid resource");

        vk::ObjectType objectType;
        std::uint64_t objectHandle;

        if constexpr (BufferLike<Resource>)
        {
            objectType = vk::ObjectType::eBuffer;
            objectHandle = reinterpret_cast<std::uint64_t>(static_cast<VkBuffer>(resource.handle()));
        }
        else if constexpr (ImageLike<Resource>)
        {
            objectType = vk::ObjectType::eImage;
            objectHandle = reinterpret_cast<std::uint64_t>(static_cast<VkImage>(resource.handle()));
        }

        vk::DebugUtilsObjectNameInfoEXT nameInfo{objectType, objectHandle, resource.name_.c_str()};
        try
        {
            resource.device_->get().setDebugUtilsObjectNameEXT(nameInfo);

            // For Image, also name the default view
            if constexpr (ImageLike<Resource>)
            {
                std::string viewName = std::format("{}_defaultView", resource.name_);
                vk::DebugUtilsObjectNameInfoEXT viewNameInfo{vk::ObjectType::eImageView, reinterpret_cast<std::uint64_t>(static_cast<VkImageView>(*resource.defaultView_)), viewName.c_str()};
                resource.device_->get().setDebugUtilsObjectNameEXT(viewNameInfo);
            }
        }
        catch (const vk::SystemError &error)
        {
            nrInfo<LogLevel::error>(std::format(
                "setResourceDebugName failed for '{}': {}",
                resource.name_,
                error.what()));
            nrAssert(false, "setResourceDebugName failed to set a Vulkan debug object name.");
        }
    }
}

/**
 * @brief Set VK_EXT_debug_utils name on a resource view (BufferView or ImageView)
 */
template <typename Resource, typename ViewHandle>
    requires BufferLike<Resource> || ImageLike<Resource>
void setResourceViewDebugName(Resource &resource, ViewHandle view, const std::string &debugName)
{
    if constexpr (!isDebugMode)
        return;

    vk::ObjectType viewType;
    if constexpr (BufferLike<Resource>)
    {
        viewType = vk::ObjectType::eBufferView;
    }
    else if constexpr (ImageLike<Resource>)
    {
        viewType = vk::ObjectType::eImageView;
    }

    vk::DebugUtilsObjectNameInfoEXT nameInfo{viewType, reinterpret_cast<std::uint64_t>(view), debugName.c_str()};
    try
    {
        resource.device_->get().setDebugUtilsObjectNameEXT(nameInfo);
    }
    catch (const vk::SystemError &error)
    {
        nrInfo<LogLevel::error>(std::format(
            "setResourceViewDebugName failed for '{}': {}",
            debugName,
            error.what()));
        nrAssert(false, "setResourceViewDebugName failed to set a Vulkan debug object name.");
    }
}
} // namespace nr::rhi

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
    // Friend declarations for debug naming helpers
    template<typename Resource>
        requires BufferLike<Resource> || ImageLike<Resource>
    friend void setResourceDebugName(Resource& resource);

    template<typename Resource, typename ViewHandle>
        requires BufferLike<Resource> || ImageLike<Resource>
    friend void setResourceViewDebugName(Resource& resource, ViewHandle view, const std::string& debugName);

  public:
    Buffer() = default;

    // Move-only
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    Buffer(Buffer &&) noexcept = default;
    Buffer &operator=(Buffer &&) noexcept = default;

    /**
     * @brief Create a buffer from a vulkan-hpp create info
     *
     * @param allocator   Initialized MemoryAllocator
     * @param device      RAII device (must outlive this buffer for view creation / BDA)
     * @param name        Optional debug name for profiling tools (RenderDoc, PIX, Nsight)
     * @param createInfo  Vulkan buffer create info (size, usage, sharingMode, etc.)
     * @param memoryUsage Memory placement intent (GpuOnly, CpuToGpu, etc.)
     * @param strategy    Allocation lifetime strategy
     * @param frameIndex  Frame index for PerFrame strategy (ignored otherwise)
     * @param name        Optional debug name for profiling tools (RenderDoc, PIX, Nsight)
     * @return Fully initialized Buffer
     */
    [[nodiscard]] inline static Buffer create(const MemoryAllocator &allocator, const vk::raii::Device &device, const vk::BufferCreateInfo &createInfo, std::string_view name, MemoryUsage memoryUsage = MemoryUsage::GpuOnly, AllocationStrategy strategy = AllocationStrategy::CrossFrame,
                                       std::uint32_t frameIndex = 0)
    {
        Buffer result;
        result.device_ = std::cref(device);

        // Store metadata for introspection and cache keying
        result.size_ = createInfo.size;
        result.usage_ = createInfo.usage;
        result.sharingMode_ = createInfo.sharingMode;
        result.flags_ = createInfo.flags;
        result.memoryUsage_ = memoryUsage;
        result.strategy_ = strategy;
        result.name_ = name;

        result.vmaBuffer_ = allocator.allocateBuffer(createInfo, strategy, memoryUsage, frameIndex);

        // Set Vulkan debug object name in debug mode only
        if (!name.empty())
        {
            setResourceDebugName(result);
        }

        return result;
    }
    /// Get the vulkan-hpp buffer handle (non-owning)
    [[nodiscard]] vk::Buffer handle() const noexcept
    {
        return vk::Buffer{vmaBuffer_.handle()};
    }

    /// Get the persistently mapped pointer (nullptr if not host-visible)
    [[nodiscard]] void *mapped() const noexcept
    {
        return vmaBuffer_.mapped();
    }

    /// Get the allocation size in bytes
    [[nodiscard]] vk::DeviceSize size() const noexcept
    {
        return size_;
    }

    /// Get the buffer usage flags
    [[nodiscard]] vk::BufferUsageFlags usage() const noexcept
    {
        return usage_;
    }

    /// Get the sharing mode
    [[nodiscard]] vk::SharingMode sharingMode() const noexcept
    {
        return sharingMode_;
    }

    /// Get the memory usage intent
    [[nodiscard]] MemoryUsage memoryUsage() const noexcept
    {
        return memoryUsage_;
    }

    /// Get the buffer create flags
    [[nodiscard]] vk::BufferCreateFlags flags() const noexcept
    {
        return flags_;
    }

    /// Get the allocation strategy
    [[nodiscard]] AllocationStrategy strategy() const noexcept
    {
        return strategy_;
    }

    /// Check if this buffer holds a valid allocation
    [[nodiscard]] bool valid() const noexcept
    {
        return vmaBuffer_.valid();
    }

    /// Check if the underlying memory is host-visible
    [[nodiscard]] bool isHostVisible() const
    {
        return vmaBuffer_.isHostVisible();
    }

    /**
     * @brief Flush host writes for this buffer range.
     */
    void flush(vk::DeviceSize offset = 0, vk::DeviceSize size = vk::WholeSize) const
    {
        nrAssert(valid(), "Buffer::flush requires a valid buffer");
        vmaBuffer_.flush(static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size));
    }

    /**
     * @brief Invalidate host cache for this buffer range after GPU writes.
     */
    void invalidate(vk::DeviceSize offset = 0, vk::DeviceSize size = vk::WholeSize) const
    {
        nrAssert(valid(), "Buffer::invalidate requires a valid buffer");
        vmaBuffer_.invalidate(static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size));
    }

    /**
     * @brief Get the buffer device address (BDA), cached after first call
     *
     * @return 64-bit device address for shader access
     *
     * Requires the buffer to have SHADER_DEVICE_ADDRESS_BIT usage
     * (automatically added by MemoryAllocator for GpuOnly/CpuToGpu).
     */
    [[nodiscard]] VkDeviceAddress deviceAddress() const
    {
        if (!cachedAddress_.has_value())
        {
            nrAssert(device_.has_value(), "Buffer::deviceAddress requires a valid device reference");
            cachedAddress_ = vmaBuffer_.deviceAddress(device_->get());
        }
        return *cachedAddress_;
    }

    /// Access the underlying VmaBuffer (escape hatch for low-level interop)
    [[nodiscard]] const VmaBuffer &raw() const noexcept
    {
        return vmaBuffer_;
    }

    /// Get the debug name
    [[nodiscard]] const std::string &name() const noexcept
    {
        return name_;
    }

    /**
     * @brief Add a named buffer view for texel buffer usage
     *
     * Creates a VkBufferView and stores it in the named view map.
     * If a view with the same key already exists, it is replaced.
     * In debug mode, sets a Vulkan debug name on the view.
     *
     * @param format   Texel format interpretation
     * @param offset   Byte offset into the buffer (default: 0)
     * @param range    Byte range (default: vk::WholeSize for entire buffer)
     * @param viewName Optional custom key; if empty, auto-generates from parameters
     *                 (e.g. "{bufferName}_fmt44_off0_whole" for R32Sfloat at offset 0)
     * @return Reference to the created RAII vk::raii::BufferView
     */
    std::reference_wrapper<const vk::raii::BufferView> addView(
        vk::Format format,
        vk::DeviceSize offset = 0,
        vk::DeviceSize range = vk::WholeSize,
        std::string_view viewName = "")
    {
        nrAssert(valid(), "Cannot create buffer view on invalid buffer");

        // Generate key from parameters if not provided
        std::string key;
        if (viewName.empty())
        {
            std::string suffix;
            if (range == vk::WholeSize)
                suffix = std::format("fmt{}_off{}_whole", static_cast<std::uint32_t>(format), offset);
            else
                suffix = std::format("fmt{}_off{}_rng{}", static_cast<std::uint32_t>(format), offset, range);
            key = name_.empty() ? suffix : std::format("{}_{}", name_, suffix);
        }
        else
        {
            key = viewName;
        }

        auto existingView = view(key);

        if (existingView)
            return *existingView;

        vk::BufferViewCreateInfo viewInfo{};
        viewInfo.buffer = handle();
        viewInfo.format = format;
        viewInfo.offset = offset;
        viewInfo.range = range;

        auto [it, inserted] = bufferViews_.emplace(
            std::move(key), vk::raii::BufferView{device_->get(), viewInfo});

        // Set Vulkan debug name for the view in debug mode
        setResourceViewDebugName(*this, static_cast<VkBufferView>(*it->second), it->first);

        return std::cref(it->second);
    }

    /**
     * @brief Get a named buffer view
     *
     * @param viewName View key (auto-generated or custom name)
     * @return Reference to the RAII vk::raii::BufferView
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const vk::raii::BufferView>> view(const std::string &viewName) const
    {
        auto it = bufferViews_.find(viewName);
        if (it == bufferViews_.end())
            return std::nullopt;
        return std::cref(it->second);
    }

    /// Check if a named buffer view exists
    [[nodiscard]] bool hasView(const std::string &viewName) const
    {
        return bufferViews_.contains(viewName);
    }

    /**
     * @brief Remove all buffer views
     */
    void clearViews()
    {
        bufferViews_.clear();
    }

    /// Write a single typed value to a mapped buffer
    template <typename T>
        requires(!std::is_pointer_v<T>)
    void write(const T &data, std::size_t offset = 0)
    {
        write(&data, sizeof(T), offset);
    }

    /// Write a span of data to a mapped buffer
    template <typename T> void write(std::span<const T> data, std::size_t offset = 0)
    {
        write(data.data(), data.size_bytes(), offset);
    }

  private:
    /**
     * @brief Write raw data to a mapped buffer
     *
     * @param data     Source data pointer
     * @param dataSize Bytes to copy
     * @param offset   Byte offset into the mapped region
     */
    void write(const void *data, std::size_t dataSize, std::size_t offset = 0)
    {
        nrAssert(mapped() != nullptr, "Buffer::write requires a mapped buffer");
        nrAssert(offset + dataSize <= static_cast<std::size_t>(size()), std::format("Buffer::write out of bounds: offset={}, dataSize={}, bufferSize={}", offset, dataSize, static_cast<std::uint64_t>(size())));
        std::memcpy(static_cast<std::byte *>(mapped()) + offset, data, dataSize);
    }

    VmaBuffer vmaBuffer_;

    // Stored metadata (extracted from vk::BufferCreateInfo at creation)
    vk::DeviceSize size_ = 0;
    vk::BufferUsageFlags usage_ = {};
    vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive;
    vk::BufferCreateFlags flags_ = {};
    MemoryUsage memoryUsage_ = MemoryUsage::GpuOnly;
    AllocationStrategy strategy_ = AllocationStrategy::CrossFrame;

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    mutable std::optional<VkDeviceAddress> cachedAddress_;
    std::map<std::string, vk::raii::BufferView> bufferViews_;  ///< Named view map (destroyed before VmaBuffer)
    std::string name_;  ///< Immutable after creation
};

// =========================================================================
// Image — RAII GPU image with default view and auxiliary view map
// =========================================================================

/**
 * @brief High-level RAII image resource
 *
 * Wraps a VmaImage (owns VkImage + VMA allocation) and adds:
 * - Default vk::raii::ImageView (full mip range, full layer range)
 * - Named auxiliary views (per-mip, per-layer, depth-only, etc.)
 * - vk::Image handle accessor for vulkan-hpp interop
 * - Stored metadata from vk::ImageCreateInfo for cache keying
 *
 * Construction is through the static `create()` factory which accepts
 * a standard vk::ImageCreateInfo plus MemoryUsage.
 * Names are immutable after creation (set once in create()).
 * Destruction cascades: auxiliary views -> default view -> VmaImage -> VMA deallocation.
 */
class Image
{
    // Friend declarations for debug naming helpers
    template<typename Resource>
        requires BufferLike<Resource> || ImageLike<Resource>
    friend void setResourceDebugName(Resource& resource);

    template<typename Resource, typename ViewHandle>
        requires BufferLike<Resource> || ImageLike<Resource>
    friend void setResourceViewDebugName(Resource& resource, ViewHandle view, const std::string& debugName);

  public:
    Image() = default;

    // Move-only
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;
    Image(Image &&) noexcept = default;
    Image &operator=(Image &&) noexcept = default;

    /**
     * @brief Create an image from a vulkan-hpp create info
     *
     * Allocates the VkImage via MemoryAllocator and creates a default
     * ImageView covering all mip levels and array layers. The view type
     * and aspect flags are inferred from the create info.
     *
     * @param allocator    Initialized MemoryAllocator
     * @param device       RAII device (must outlive this image)
     * @param createInfo   Vulkan image create info (standard vulkan-hpp type)
     * @param memoryUsage  Memory placement intent (default: GpuOnly)
     * @param name         Optional debug name for profiling tools (RenderDoc, PIX, Nsight)
     * @return Fully initialized Image with default view
     */
    [[nodiscard]] inline static Image create(const MemoryAllocator &allocator, const vk::raii::Device &device, const vk::ImageCreateInfo &createInfo, std::string_view name, MemoryUsage memoryUsage = MemoryUsage::GpuOnly)
    {
        Image result;
        result.device_ = std::cref(device);

        // Store metadata for introspection and cache keying
        result.imageType_ = createInfo.imageType;
        result.format_ = createInfo.format;
        result.extent_ = createInfo.extent;
        result.mipLevels_ = createInfo.mipLevels;
        result.arrayLayers_ = createInfo.arrayLayers;
        result.samples_ = createInfo.samples;
        result.tiling_ = createInfo.tiling;
        result.usage_ = createInfo.usage;
        result.sharingMode_ = createInfo.sharingMode;
        result.flags_ = createInfo.flags;
        result.memoryUsage_ = memoryUsage;
        result.name_ = name;

        result.vmaImage_ = allocator.allocateImage(createInfo, memoryUsage);

        // Create default view covering all mips and layers
        result.createDefaultView();

        // Set Vulkan debug object names in debug mode only
        if (!name.empty())
        {
            setResourceDebugName(result);
        }

        return result;
    }

    /// Get the vulkan-hpp image handle (non-owning)
    [[nodiscard]] vk::Image handle() const noexcept
    {
        return vk::Image{vmaImage_.handle()};
    }

    /// Get the default RAII image view (full mip range, full layer range)
    [[nodiscard]] const vk::raii::ImageView &view() const noexcept
    {
        return defaultView_;
    }

    /**
     * @brief Get a named auxiliary view
     *
     * @param name View name (e.g., "mip_0", "layer_2", "depthOnly")
     * @return Reference to the RAII vk::raii::ImageView
     */
    [[nodiscard]] std::optional<std::reference_wrapper<const vk::raii::ImageView>> view(const std::string &name) const
    {
        auto it = imageViews_.find(name);
        if (it==imageViews_.end())
            return std::nullopt;
        return std::cref(it->second);
    }

    /// Check if a named auxiliary view exists
    [[nodiscard]] bool hasView(const std::string &name) const
    {
        return imageViews_.contains(name);
    }

    /// Get the image type
    [[nodiscard]] vk::ImageType imageType() const noexcept
    {
        return imageType_;
    }

    /// Get the image format
    [[nodiscard]] vk::Format format() const noexcept
    {
        return format_;
    }

    /// Get the image extent
    [[nodiscard]] vk::Extent3D extent() const noexcept
    {
        return extent_;
    }

    /// Get the number of mip levels
    [[nodiscard]] std::uint32_t mipLevels() const noexcept
    {
        return mipLevels_;
    }

    /// Get the number of array layers
    [[nodiscard]] std::uint32_t arrayLayers() const noexcept
    {
        return arrayLayers_;
    }

    /// Get the sample count
    [[nodiscard]] vk::SampleCountFlagBits samples() const noexcept
    {
        return samples_;
    }

    /// Get the image tiling
    [[nodiscard]] vk::ImageTiling tiling() const noexcept
    {
        return tiling_;
    }

    /// Get the image usage flags
    [[nodiscard]] vk::ImageUsageFlags usage() const noexcept
    {
        return usage_;
    }

    /// Get the sharing mode
    [[nodiscard]] vk::SharingMode sharingMode() const noexcept
    {
        return sharingMode_;
    }

    /// Get the image create flags
    [[nodiscard]] vk::ImageCreateFlags flags() const noexcept
    {
        return flags_;
    }

    /// Get the memory usage intent
    [[nodiscard]] MemoryUsage memoryUsage() const noexcept
    {
        return memoryUsage_;
    }

    /// Check if this image holds a valid allocation
    [[nodiscard]] bool valid() const noexcept
    {
        return vmaImage_.valid();
    }

    /// Get the debug name
    [[nodiscard]] const std::string &name() const noexcept
    {
        return name_;
    }

    /// Access the underlying VmaImage (escape hatch for low-level interop)
    [[nodiscard]] const VmaImage &raw() const noexcept
    {
        return vmaImage_;
    }

    /**
     * @brief Add a named auxiliary image view
     *
     * The image handle is automatically injected into the create info.
     * If a view with the same key already exists, it is replaced.
     * In debug mode, sets a Vulkan debug name on the view.
     *
     * @param viewKey  Unique view key for map lookup (also used as the Vulkan debug name)
     * @param viewInfo View creation parameters (image field is overwritten)
     * @return Non-owning vk::ImageView handle
     */
    std::reference_wrapper<const vk::raii::ImageView> addView(const std::string &viewKey, vk::ImageViewCreateInfo viewInfo)
    {
        nrAssert(valid(), "Cannot create view on invalid image");
        viewInfo.image = handle();
        auto existingView = view(viewKey);
        if (existingView)
            return *existingView;

        auto [it, _] = imageViews_.emplace(viewKey, vk::raii::ImageView{device_->get(), viewInfo});

        setResourceViewDebugName(*this, static_cast<VkImageView>(*it->second), viewKey);

        return std::cref(it->second);
    }

    /**
     * @brief Create a view for a specific mip level range
     *
     * Auto-generates key: "{imageName}_mip_{baseMip}" (or "{imageName}_mip_{baseMip}_{mipCount}").
     * Covers all array layers.
     *
     * @param baseMip   First mip level
     * @param mipCount  Number of mip levels (default: 1)
     * @param viewName  Optional custom key; if empty, auto-generates from parameters
     */
    std::reference_wrapper<const vk::raii::ImageView> addMipView(std::uint32_t baseMip, std::uint32_t mipCount = 1, std::string_view viewName = "")
    {
        std::string key;
        if (viewName.empty())
        {
            std::string suffix = mipCount == 1 ? std::format("mip_{}", baseMip) : std::format("mip_{}_{}", baseMip, mipCount);
            key = name_.empty() ? suffix : std::format("{}_{}", name_, suffix);
        }
        else
        {
            key = viewName;
        }

        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.viewType = inferViewType(imageType_, arrayLayers_);
        viewInfo.format = format_;
        viewInfo.subresourceRange = vk::ImageSubresourceRange{inferAspectFlags(format_), baseMip, mipCount, 0, arrayLayers_};

        return addView(key, viewInfo);
    }

    /**
     * @brief Create a view for a specific array layer range
     *
     * Auto-generates key: "{imageName}_layer_{baseLayer}" (or "{imageName}_layer_{baseLayer}_{layerCount}").
     * Covers all mip levels.
     *
     * @param baseLayer   First array layer
     * @param layerCount  Number of array layers (default: 1)
     * @param viewName    Optional custom key; if empty, auto-generates from parameters
     */
    std::reference_wrapper<const vk::raii::ImageView> addLayerView(std::uint32_t baseLayer, std::uint32_t layerCount = 1, std::string_view viewName = "")
    {
        std::string key;
        if (viewName.empty())
        {
            std::string suffix = layerCount == 1 ? std::format("layer_{}", baseLayer) : std::format("layer_{}_{}", baseLayer, layerCount);
            key = name_.empty() ? suffix : std::format("{}_{}", name_, suffix);
        }
        else
        {
            key = viewName;
        }

        vk::ImageViewCreateInfo viewInfo{};
        // Single layer from a 2D array -> use 2D view type (not array)
        if (imageType_ == vk::ImageType::e2D && layerCount == 1)
            viewInfo.viewType = vk::ImageViewType::e2D;
        else
            viewInfo.viewType = inferViewType(imageType_, layerCount);
        viewInfo.format = format_;
        viewInfo.subresourceRange = vk::ImageSubresourceRange{inferAspectFlags(format_), 0, mipLevels_, baseLayer, layerCount};

        return addView(key, viewInfo);
    }

    /**
     * @brief Create an aspect-specific view for depth-stencil formats
     *
     * Covers all mip levels and array layers.
     * Auto-generates key: "{imageName}_depthOnly" or "{imageName}_stencilOnly".
     *
     * @tparam Aspect vk::ImageAspectFlagBits::eDepth or eStencil
     * @param viewName Optional custom key; if empty, uses auto-generated name
     */
    template <vk::ImageAspectFlagBits Aspect = vk::ImageAspectFlagBits::eDepth>
    vk::ImageView addAspectView(std::string_view viewName = "")
    {
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.viewType = inferViewType(imageType_, arrayLayers_);
        viewInfo.format = format_;
        if constexpr (Aspect == vk::ImageAspectFlagBits::eDepth)
        {
            nrAssert(isDepthFormat(format_), std::format("Image::addAspectView<Depth>: format {} is not a depth format", static_cast<unsigned>(format_)));
            viewInfo.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eDepth, 0, mipLevels_, 0, arrayLayers_};
            std::string key;
            if (viewName.empty())
                key = name_.empty() ? std::string{"depthOnly"} : std::format("{}_depthOnly", name_);
            else
                key = viewName;
            return vk::ImageView{*addView(key, viewInfo).get()};
        }
        else if constexpr (Aspect == vk::ImageAspectFlagBits::eStencil)
        {
            nrAssert(isStencilFormat(format_), std::format("Image::addAspectView<Stencil>: format {} is not a stencil format", static_cast<unsigned>(format_)));
            viewInfo.subresourceRange = vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eStencil, 0, mipLevels_, 0, arrayLayers_};
            std::string key;
            if (viewName.empty())
                key = name_.empty() ? std::string{"stencilOnly"} : std::format("{}_stencilOnly", name_);
            else
                key = viewName;
            return vk::ImageView{*addView(key, viewInfo).get()};
        }
        std::unreachable();
    }

    /**
     * @brief Remove all auxiliary views
     *
     * The default view is preserved.
     */
    void clearViews()
    {
        imageViews_.clear();
    }

  private:
    /**
     * @brief Create the default image view covering all mips and layers
     */
    void createDefaultView()
    {
        nrAssert(valid(), "Cannot create default view on invalid image");
        vk::ImageViewCreateInfo viewInfo{};
        viewInfo.image = handle();
        viewInfo.viewType = inferViewType(imageType_, arrayLayers_);
        viewInfo.format = format_;
        viewInfo.components = vk::ComponentMapping{}; // identity swizzle
        viewInfo.subresourceRange = vk::ImageSubresourceRange{inferAspectFlags(format_), 0, mipLevels_, 0, arrayLayers_};

        defaultView_ = vk::raii::ImageView{device_->get(), viewInfo};
    }

    VmaImage vmaImage_;

    // Stored metadata (extracted from vk::ImageCreateInfo at creation)
    vk::ImageType imageType_ = vk::ImageType::e2D;
    vk::Format format_ = vk::Format::eUndefined;
    vk::Extent3D extent_ = {0, 0, 0};
    std::uint32_t mipLevels_ = 1;
    std::uint32_t arrayLayers_ = 1;
    vk::SampleCountFlagBits samples_ = vk::SampleCountFlagBits::e1;
    vk::ImageTiling tiling_ = vk::ImageTiling::eOptimal;
    vk::ImageUsageFlags usage_ = {};
    vk::SharingMode sharingMode_ = vk::SharingMode::eExclusive;
    vk::ImageCreateFlags flags_ = {};
    MemoryUsage memoryUsage_ = MemoryUsage::GpuOnly;

    vk::raii::ImageView defaultView_ = {nullptr};
    std::map<std::string, vk::raii::ImageView> imageViews_;  ///< Named auxiliary view map (destroyed before VmaImage)
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    std::string name_;  ///< Immutable after creation
};

} // namespace nr::rhi
