module nr.rhi;
import :resourcePool;
import dependency.vulkan;
import nr.utils;
import :type;
import :vk;
import :resource;
import :memoryAllocator;
import std;

namespace nr::rhi
{
void ResourceFactory::initialize(const MemoryAllocator &allocator, const vk::raii::Device &device)
{
        allocator_ = std::cref(allocator);
        device_ = std::cref(device);
    }

[[nodiscard]] bool ResourceFactory::valid() const noexcept
{
        return device_.has_value();
    }

[[nodiscard]] Buffer ResourceFactory::createBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage, std::string_view name) const
{
        nrAssert(valid(), "ResourceFactory::createBuffer: factory not initialized");
        return Buffer::create(*allocator_, device_->get(), createInfo, name, memoryUsage, AllocationStrategy::CrossFrame);
    }

[[nodiscard]] Image ResourceFactory::createImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage, std::string_view name) const
{
        nrAssert(valid(), "ResourceFactory::createImage: factory not initialized");
        return Image::create(*allocator_, device_->get(), createInfo, name, memoryUsage);
    }

void ResourcePool::initialize(const MemoryAllocator &allocator, const vk::raii::Device &device)
{
        allocator_ = std::cref(allocator);
        device_ = std::cref(device);
    }

[[nodiscard]] bool ResourcePool::valid() const noexcept
{
        return device_.has_value();
    }

Buffer &ResourcePool::allocateTransientBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage, std::uint32_t frameIndex, std::string_view name)
{
        nrAssert(valid(), "ResourcePool::allocateTransientBuffer: pool not initialized");

        std::uint32_t idx = frameIndex % maxFrameInFlight;
        frameBuffers_[idx].emplace_back(Buffer::create(*allocator_, device_->get(), createInfo, name, memoryUsage, AllocationStrategy::PerFrame, frameIndex));
        return frameBuffers_[idx].back();
    }

Image &ResourcePool::allocateTransientImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage, std::uint32_t frameIndex, std::string_view name)
{
        nrAssert(valid(), "ResourcePool::allocateTransientImage: pool not initialized");

        std::uint32_t idx = frameIndex % maxFrameInFlight;
        frameImages_[idx].emplace_back(Image::create(*allocator_, device_->get(), createInfo, name, memoryUsage));
        return frameImages_[idx].back();
    }

void ResourcePool::resetFrame(std::uint32_t frameIndex)
{
        std::uint32_t idx = frameIndex % maxFrameInFlight;

        // RAII destruction cascades to VMA + ImageViews
        frameBuffers_[idx].clear();
        frameImages_[idx].clear();
    }
} // namespace nr::rhi
