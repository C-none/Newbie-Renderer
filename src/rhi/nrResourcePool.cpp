module nr.rhi;
import :resourcePool;
import dependency.vulkan;
import nr.utils;
import :type;
import :resource;
import :memoryAllocator;
import std;

namespace nr::rhi
{
ResourceFactory::ResourceFactory(const MemoryAllocator &allocator, const vk::raii::Device &device)
    : allocator_(allocator), device_(device)
{
}

[[nodiscard]] const MemoryAllocator &ResourceFactory::allocator() const noexcept
{
    return allocator_.get();
}

[[nodiscard]] const vk::raii::Device &ResourceFactory::device() const noexcept
{
    return device_.get();
}

[[nodiscard]] Buffer ResourceFactory::createBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage,
                                                   std::string_view name) const
{
    return Buffer::create(allocator_.get(), device_.get(), createInfo, name, memoryUsage,
                          AllocationStrategy::CrossFrame);
}

[[nodiscard]] Image ResourceFactory::createImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage,
                                                 std::string_view name) const
{
    return Image::create(allocator_.get(), device_.get(), createInfo, name, memoryUsage);
}

ResourcePool::ResourcePool(const ResourceFactory &factory) : factory_(factory)
{
}

Buffer &ResourcePool::allocateTransientBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage,
                                              std::uint32_t frameIndex, std::string_view name)
{
    std::uint32_t idx = frameIndex % maxFrameInFlight;
    frameBuffers_[idx].emplace_back(Buffer::create(factory_.get().allocator(), factory_.get().device(), createInfo,
                                                   name, memoryUsage, AllocationStrategy::PerFrame, frameIndex));
    return frameBuffers_[idx].back();
}

Image &ResourcePool::allocateTransientImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage,
                                            std::uint32_t frameIndex, std::string_view name)
{
    std::uint32_t idx = frameIndex % maxFrameInFlight;
    frameImages_[idx].emplace_back(
        Image::create(factory_.get().allocator(), factory_.get().device(), createInfo, name, memoryUsage));
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
