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
[[nodiscard]] Buffer Buffer::create(const MemoryAllocator &allocator, const vk::raii::Device &device, const vk::BufferCreateInfo &createInfo, std::string_view name, MemoryUsage memoryUsage, AllocationStrategy strategy,
                                       std::uint32_t frameIndex)
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

        // Set Vulkan debug object name when debugger-facing names are enabled.
        if (!name.empty())
        {
            setResourceDebugName(result);
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

[[nodiscard]] MemoryUsage Buffer::memoryUsage() const noexcept
{
        return memoryUsage_;
    }

[[nodiscard]] vk::BufferCreateFlags Buffer::flags() const noexcept
{
        return flags_;
    }

[[nodiscard]] AllocationStrategy Buffer::strategy() const noexcept
{
        return strategy_;
    }

[[nodiscard]] bool Buffer::valid() const noexcept
{
        return vmaBuffer_.valid();
    }

[[nodiscard]] bool Buffer::isHostVisible() const
{
        return vmaBuffer_.isHostVisible();
    }

void Buffer::flush(vk::DeviceSize offset, vk::DeviceSize size) const
{
        nrAssert(valid(), "Buffer::flush requires a valid buffer");
        vmaBuffer_.flush(static_cast<VkDeviceSize>(offset), static_cast<VkDeviceSize>(size));
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

[[nodiscard]] const VmaBuffer &Buffer::raw() const noexcept
{
        return vmaBuffer_;
    }

[[nodiscard]] const std::string &Buffer::name() const noexcept
{
        return name_;
    }

std::reference_wrapper<const vk::raii::BufferView> Buffer::addView(
        vk::Format format,
        vk::DeviceSize offset,
        vk::DeviceSize range,
        std::string_view viewName)
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

        // Set Vulkan debug name for the view when debugger-facing names are enabled.
        setResourceViewDebugName(*this, static_cast<VkBufferView>(*it->second), it->first);

        return std::cref(it->second);
    }

[[nodiscard]] std::optional<std::reference_wrapper<const vk::raii::BufferView>> Buffer::view(const std::string &viewName) const
{
        auto it = bufferViews_.find(viewName);
        if (it == bufferViews_.end())
            return std::nullopt;
        return std::cref(it->second);
    }

[[nodiscard]] bool Buffer::hasView(const std::string &viewName) const
{
        return bufferViews_.contains(viewName);
    }

void Buffer::clearViews()
{
        bufferViews_.clear();
    }

void Buffer::writeMappedAndFlush(const void *data, std::size_t dataSize, vk::DeviceSize offset)
{
        if (dataSize == 0)
        {
            return;
        }

        nrAssert(data != nullptr, "Buffer::writeMappedAndFlush requires non-null data for non-empty writes.");
        nrAssert(
            offset <= static_cast<vk::DeviceSize>(std::numeric_limits<std::size_t>::max()),
            "Buffer::writeMappedAndFlush offset exceeds host size_t range.");

        auto const hostOffset = static_cast<std::size_t>(offset);
        nrAssert(
            dataSize <= std::numeric_limits<std::size_t>::max() - hostOffset,
            "Buffer::writeMappedAndFlush write range overflows host size_t.");

        write(data, dataSize, hostOffset);
        flush(offset, static_cast<vk::DeviceSize>(dataSize));
    }

void Buffer::write(const void *data, std::size_t dataSize, std::size_t offset)
{
        nrAssert(mapped() != nullptr, "Buffer::write requires a mapped buffer");
        nrAssert(offset + dataSize <= static_cast<std::size_t>(size()), std::format("Buffer::write out of bounds: offset={}, dataSize={}, bufferSize={}", offset, dataSize, static_cast<std::uint64_t>(size())));
        std::memcpy(static_cast<std::byte *>(mapped()) + offset, data, dataSize);
    }

[[nodiscard]] Image Image::create(const MemoryAllocator &allocator, const vk::raii::Device &device, const vk::ImageCreateInfo &createInfo, std::string_view name, MemoryUsage memoryUsage)
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

        // Set Vulkan debug object names when debugger-facing names are enabled.
        if (!name.empty())
        {
            setResourceDebugName(result);
        }

        return result;
    }

[[nodiscard]] vk::Image Image::handle() const noexcept
{
        return vk::Image{vmaImage_.handle()};
    }

[[nodiscard]] const vk::raii::ImageView &Image::view() const noexcept
{
        return defaultView_;
    }

[[nodiscard]] std::optional<std::reference_wrapper<const vk::raii::ImageView>> Image::view(const std::string &name) const
{
        auto it = imageViews_.find(name);
        if (it==imageViews_.end())
            return std::nullopt;
        return std::cref(it->second);
    }

[[nodiscard]] bool Image::hasView(const std::string &name) const
{
        return imageViews_.contains(name);
    }

[[nodiscard]] vk::ImageType Image::imageType() const noexcept
{
        return imageType_;
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

[[nodiscard]] vk::SampleCountFlagBits Image::samples() const noexcept
{
        return samples_;
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

[[nodiscard]] vk::ImageCreateFlags Image::flags() const noexcept
{
        return flags_;
    }

[[nodiscard]] MemoryUsage Image::memoryUsage() const noexcept
{
        return memoryUsage_;
    }

[[nodiscard]] bool Image::valid() const noexcept
{
        return vmaImage_.valid();
    }

[[nodiscard]] const std::string &Image::name() const noexcept
{
        return name_;
    }

[[nodiscard]] const VmaImage &Image::raw() const noexcept
{
        return vmaImage_;
    }

std::reference_wrapper<const vk::raii::ImageView> Image::addView(const std::string &viewKey, vk::ImageViewCreateInfo viewInfo)
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

std::reference_wrapper<const vk::raii::ImageView> Image::addMipView(std::uint32_t baseMip, std::uint32_t mipCount, std::string_view viewName)
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

std::reference_wrapper<const vk::raii::ImageView> Image::addLayerView(std::uint32_t baseLayer, std::uint32_t layerCount, std::string_view viewName)
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

void Image::clearViews()
{
        imageViews_.clear();
    }

void Image::createDefaultView()
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
} // namespace nr::rhi
