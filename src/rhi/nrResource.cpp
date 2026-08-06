module nr.rhi;
import :resource;
import dependency.vma;
import dependency.vulkan;
import nr.utils;
import :type;
import :vk;
import :vmaAllocator;
import :memoryAllocator;
import std;

namespace nr::rhi
{
namespace
{
constexpr auto kDefaultViewCompatibleImageUsage =
    vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
    vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eDepthStencilAttachment |
    vk::ImageUsageFlagBits::eTransientAttachment | vk::ImageUsageFlagBits::eInputAttachment |
    vk::ImageUsageFlagBits::eAttachmentFeedbackLoopEXT |
    vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR;

[[maybe_unused]] void setDebugObjectName(const vk::raii::Device &device, vk::ObjectType objectType,
                                         std::uint64_t objectHandle, const std::string &debugName)
{
    auto nameInfo = vk::DebugUtilsObjectNameInfoEXT{objectType, objectHandle, debugName.c_str()};
    try
    {
        device.setDebugUtilsObjectNameEXT(nameInfo);
    }
    catch (const vk::SystemError &error)
    {
        nrInfo<LogLevel::error>(std::format("Failed to set Vulkan debug name '{}': {}", debugName, error.what()));
        nrAssert(false, "Failed to set a Vulkan debug object name.");
    }
}
} // namespace

Buffer &Buffer::operator=(Buffer &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    // Vulkan buffer views must be destroyed before the buffer they reference.
    bufferViews_.clear();
    vmaBuffer_ = std::move(other.vmaBuffer_);
    size_ = other.size_;
    usage_ = other.usage_;
    sharingMode_ = other.sharingMode_;
    device_ = std::move(other.device_);
    cachedAddress_ = std::move(other.cachedAddress_);
    bufferViews_ = std::move(other.bufferViews_);
    name_ = std::move(other.name_);
    return *this;
}

[[nodiscard]] Buffer Buffer::create(const MemoryAllocator &allocator, const vk::raii::Device &device,
                                    const vk::BufferCreateInfo &createInfo, std::string_view name,
                                    MemoryUsage memoryUsage, AllocationStrategy strategy, std::uint32_t frameIndex)
{
    Buffer result;
    result.device_ = std::cref(device);

    // Store metadata for introspection and cache keying
    result.size_ = createInfo.size;
    result.usage_ = createInfo.usage;
    result.sharingMode_ = createInfo.sharingMode;
    result.name_ = name;

    result.vmaBuffer_ = allocator.allocateBuffer(createInfo, strategy, memoryUsage, frameIndex);

    // Set Vulkan debug object name when debugger-facing names are enabled.
    if constexpr (gpuDebugNamesEnabled)
    {
        if (!result.name_.empty())
        {
            nrAssert(result.valid(), "Cannot set a debug name on an invalid buffer.");
            setDebugObjectName(device, vk::ObjectType::eBuffer,
                               reinterpret_cast<std::uint64_t>(static_cast<VkBuffer>(result.handle())), result.name_);
        }
    }

    return result;
}

[[nodiscard]] vk::Buffer Buffer::handle() const noexcept
{
    return vk::Buffer{vmaBuffer_.handle()};
}

[[nodiscard]] void *Buffer::mapped() const noexcept
{
    return vmaBuffer_.mapped();
}

[[nodiscard]] vk::DeviceSize Buffer::size() const noexcept
{
    return size_;
}

[[nodiscard]] vk::BufferUsageFlags Buffer::usage() const noexcept
{
    return usage_;
}

[[nodiscard]] vk::SharingMode Buffer::sharingMode() const noexcept
{
    return sharingMode_;
}

[[nodiscard]] bool Buffer::valid() const noexcept
{
    return vmaBuffer_.valid();
}

void Buffer::invalidate(vk::DeviceSize offset, vk::DeviceSize size) const
{
    nrAssert(valid(), "Buffer::invalidate requires a valid buffer");
    vmaBuffer_.invalidate(static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size));
}

[[nodiscard]] VkDeviceAddress Buffer::deviceAddress() const
{
    if (!cachedAddress_.has_value())
    {
        nrAssert(device_.has_value(), "Buffer::deviceAddress requires a valid device reference");
        cachedAddress_ = vmaBuffer_.deviceAddress(device_->get());
    }
    return *cachedAddress_;
}

std::reference_wrapper<const vk::raii::BufferView> Buffer::addView(vk::Format format, vk::DeviceSize offset,
                                                                   vk::DeviceSize range, std::string_view viewName)
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

    auto existingView = bufferViews_.find(key);
    if (existingView != bufferViews_.end())
    {
        return std::cref(existingView->second);
    }

    vk::BufferViewCreateInfo viewInfo{};
    viewInfo.buffer = handle();
    viewInfo.format = format;
    viewInfo.offset = offset;
    viewInfo.range = range;

    auto it = bufferViews_.emplace(std::move(key), vk::raii::BufferView{device_->get(), viewInfo}).first;

    // Set Vulkan debug name for the view when debugger-facing names are enabled.
    if constexpr (gpuDebugNamesEnabled)
    {
        setDebugObjectName(device_->get(), vk::ObjectType::eBufferView,
                           reinterpret_cast<std::uint64_t>(static_cast<VkBufferView>(*it->second)), it->first);
    }

    return std::cref(it->second);
}

void Buffer::writeMappedAndFlush(const void *data, std::size_t dataSize, vk::DeviceSize offset)
{
    if (dataSize == 0)
    {
        return;
    }

    nrAssert(data != nullptr, "Buffer::writeMappedAndFlush requires non-null data for non-empty writes.");
    nrAssert(offset <= static_cast<vk::DeviceSize>(std::numeric_limits<std::size_t>::max()),
             "Buffer::writeMappedAndFlush offset exceeds host size_t range.");

    auto const hostOffset = static_cast<std::size_t>(offset);
    nrAssert(dataSize <= std::numeric_limits<std::size_t>::max() - hostOffset,
             "Buffer::writeMappedAndFlush write range overflows host size_t.");

    auto const endOffset = hostOffset + dataSize;
    nrAssert(static_cast<vk::DeviceSize>(endOffset) <= size_,
             std::format("Buffer::writeMappedAndFlush out of bounds: offset={}, dataSize={}, bufferSize={}", offset,
                         dataSize, static_cast<std::uint64_t>(size_)));
    auto *mappedData = mapped();
    nrAssert(mappedData != nullptr, "Buffer::writeMappedAndFlush requires a mapped buffer.");
    std::memcpy(static_cast<std::byte *>(mappedData) + hostOffset, data, dataSize);
    vmaBuffer_.flush(static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(dataSize));
}

[[nodiscard]] Image Image::create(const MemoryAllocator &allocator, const vk::raii::Device &device,
                                  const vk::ImageCreateInfo &createInfo, std::string_view name, MemoryUsage memoryUsage)
{
    nrAssert((createInfo.usage & kDefaultViewCompatibleImageUsage) != vk::ImageUsageFlags{},
             "Image::create requires usage compatible with its owned default image view.");

    Image result;
    auto debugName = std::string{name};

    // Store metadata for introspection and cache keying
    result.format_ = createInfo.format;
    result.extent_ = createInfo.extent;
    result.mipLevels_ = createInfo.mipLevels;
    result.arrayLayers_ = createInfo.arrayLayers;
    result.tiling_ = createInfo.tiling;
    result.usage_ = createInfo.usage;
    result.sharingMode_ = createInfo.sharingMode;
    result.vmaImage_ = allocator.allocateImage(createInfo, memoryUsage);

    // Create default view covering all mips and layers
    result.createDefaultView(device, createInfo.imageType);

    // Set Vulkan debug object names when debugger-facing names are enabled.
    if constexpr (gpuDebugNamesEnabled)
    {
        if (!debugName.empty())
        {
            nrAssert(result.valid(), "Cannot set debug names on an invalid image.");
            setDebugObjectName(device, vk::ObjectType::eImage,
                               reinterpret_cast<std::uint64_t>(static_cast<VkImage>(result.handle())), debugName);
            auto defaultViewName = std::format("{}_defaultView", debugName);
            setDebugObjectName(device, vk::ObjectType::eImageView,
                               reinterpret_cast<std::uint64_t>(static_cast<VkImageView>(*result.defaultView_)),
                               defaultViewName);
        }
    }

    return result;
}

Image &Image::operator=(Image &&other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    // Vulkan image views must be destroyed before the image they reference.
    defaultView_ = vk::raii::ImageView{nullptr};
    vmaImage_ = std::move(other.vmaImage_);
    format_ = other.format_;
    extent_ = other.extent_;
    mipLevels_ = other.mipLevels_;
    arrayLayers_ = other.arrayLayers_;
    tiling_ = other.tiling_;
    usage_ = other.usage_;
    sharingMode_ = other.sharingMode_;
    defaultView_ = std::move(other.defaultView_);
    return *this;
}

[[nodiscard]] vk::Image Image::handle() const noexcept
{
    return vk::Image{vmaImage_.handle()};
}

[[nodiscard]] const vk::raii::ImageView &Image::view() const noexcept
{
    return defaultView_;
}

[[nodiscard]] vk::Format Image::format() const noexcept
{
    return format_;
}

[[nodiscard]] vk::Extent3D Image::extent() const noexcept
{
    return extent_;
}

[[nodiscard]] std::uint32_t Image::mipLevels() const noexcept
{
    return mipLevels_;
}

[[nodiscard]] std::uint32_t Image::arrayLayers() const noexcept
{
    return arrayLayers_;
}

[[nodiscard]] vk::ImageTiling Image::tiling() const noexcept
{
    return tiling_;
}

[[nodiscard]] vk::ImageUsageFlags Image::usage() const noexcept
{
    return usage_;
}

[[nodiscard]] vk::SharingMode Image::sharingMode() const noexcept
{
    return sharingMode_;
}

[[nodiscard]] bool Image::valid() const noexcept
{
    return vmaImage_.valid();
}

void Image::createDefaultView(const vk::raii::Device &device, vk::ImageType imageType)
{
    nrAssert(valid(), "Cannot create default view on invalid image");
    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = handle();
    viewInfo.viewType = inferViewType(imageType, arrayLayers_);
    viewInfo.format = format_;
    viewInfo.components = vk::ComponentMapping{}; // identity swizzle
    viewInfo.subresourceRange = vk::ImageSubresourceRange{inferAspectFlags(format_), 0, mipLevels_, 0, arrayLayers_};

    defaultView_ = vk::raii::ImageView{device, viewInfo};
}
} // namespace nr::rhi
