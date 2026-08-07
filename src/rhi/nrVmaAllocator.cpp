module nr.rhi;
import :vmaAllocator;
import dependency.vma;
import dependency.vulkan;
import nr.utils;
import :type;
import std;

namespace nr::rhi
{
VmaBuffer::VmaBuffer(VmaAllocator alloc, VkBuffer buf, VmaAllocation mem, VmaAllocationInfo allocInfo)
    : allocator(alloc), buffer(buf), allocation(mem), info(allocInfo)
{
}

VmaBuffer::~VmaBuffer()
{
    if (buffer != nullptr)
    {
        vmaDestroyBuffer(allocator, buffer, allocation);
    }
}

VmaBuffer::VmaBuffer(VmaBuffer &&other) noexcept
    : allocator(other.allocator), buffer(other.buffer), allocation(other.allocation), info(other.info)
{
    other.allocator = nullptr;
    other.buffer = nullptr;
    other.allocation = nullptr;
    other.info = {};
}

VmaBuffer &VmaBuffer::operator=(VmaBuffer &&other) noexcept
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

[[nodiscard]] bool VmaBuffer::valid() const noexcept
{
    return buffer != nullptr;
}

[[nodiscard]] void *VmaBuffer::mapped() const noexcept
{
    return info.pMappedData;
}

[[nodiscard]] VkBuffer VmaBuffer::handle() const noexcept
{
    return buffer;
}

[[nodiscard]] VkDeviceAddress VmaBuffer::deviceAddress(const vk::raii::Device &device) const noexcept
{
    return static_cast<VkDeviceAddress>(device.getBufferAddress(vk::BufferDeviceAddressInfo{vk::Buffer{buffer}}));
}

void VmaBuffer::flush(VkDeviceSize offset, VkDeviceSize size) const
{
    if (allocation == nullptr)
        return;
    auto result = vmaFlushAllocation(allocator, allocation, offset, size);
    nrAssert(result == VK_SUCCESS, "vmaFlushAllocation failed: {}", static_cast<int>(result));
}

void VmaBuffer::invalidate(VkDeviceSize offset, VkDeviceSize size) const
{
    if (allocation == nullptr)
        return;
    auto result = vmaInvalidateAllocation(allocator, allocation, offset, size);
    nrAssert(result == VK_SUCCESS, "vmaInvalidateAllocation failed: {}", static_cast<int>(result));
}

VmaImage::VmaImage(VmaAllocator alloc, VkImage img, VmaAllocation mem, VmaAllocationInfo allocInfo)
    : allocator(alloc), image(img), allocation(mem), info(allocInfo)
{
}

VmaImage::~VmaImage()
{
    if (image != nullptr)
    {
        vmaDestroyImage(allocator, image, allocation);
    }
}

VmaImage::VmaImage(VmaImage &&other) noexcept
    : allocator(other.allocator), image(other.image), allocation(other.allocation), info(other.info)
{
    other.image = nullptr;
    other.allocation = nullptr;
    other.info = {};
}

VmaImage &VmaImage::operator=(VmaImage &&other) noexcept
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

[[nodiscard]] bool VmaImage::valid() const noexcept
{
    return image != nullptr;
}

[[nodiscard]] VkImage VmaImage::handle() const noexcept
{
    return image;
}

VmaPoolHandle::VmaPoolHandle(VmaAllocator alloc, VmaPool p) : allocator(alloc), pool(p)
{
}

VmaPoolHandle::VmaPoolHandle(VmaAllocator alloc, VmaPool p, const VmaPoolCreateInfo &info)
    : allocator(alloc), pool(p), createInfo(info)
{
}

VmaPoolHandle::~VmaPoolHandle()
{
    if (pool != nullptr)
    {
        vmaDestroyPool(allocator, pool);
    }
}

VmaPoolHandle::VmaPoolHandle(VmaPoolHandle &&other) noexcept
    : allocator(other.allocator), pool(other.pool), createInfo(std::move(other.createInfo))
{
    other.allocator = nullptr;
    other.pool = nullptr;
    other.createInfo.reset();
}

VmaPoolHandle &VmaPoolHandle::operator=(VmaPoolHandle &&other) noexcept
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

[[nodiscard]] bool VmaPoolHandle::valid() const noexcept
{
    return pool != nullptr;
}

[[nodiscard]] VmaPool VmaPoolHandle::handle() const noexcept
{
    return pool;
}

void VmaPoolHandle::reset()
{
    if (pool != nullptr)
    {
        nrAssert(createInfo.has_value(), "VmaPoolHandle::reset requires pool creation info.");

        vmaDestroyPool(allocator, pool);
        pool = nullptr;

        VmaPool newPool = nullptr;
        VkResult result = vmaCreatePool(allocator, &(*createInfo), &newPool);
        nrAssert(result == VK_SUCCESS, "vmaCreatePool failed: {}", static_cast<int>(result));
        pool = newPool;
    }
}

VmaAllocatorWrapper::VmaAllocatorWrapper(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice,
                                         const vk::raii::Device &device)
{
    VmaAllocatorCreateInfo createInfo{};
    createInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT | VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
                       VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT | VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
    createInfo.vulkanApiVersion = vk::ApiVersion14;
    createInfo.physicalDevice = *physDevice;
    createInfo.device = *device;
    createInfo.instance = *instance;

    VkResult result = vmaCreateAllocator(&createInfo, &allocator_);
    nrAssert(result == VkResult::VK_SUCCESS,
             "Failed to create VMA allocator: {}", static_cast<int>(result));
}

VmaAllocatorWrapper::~VmaAllocatorWrapper()
{
    if (allocator_ != nullptr)
    {
        vmaDestroyAllocator(allocator_);
    }
}

VmaAllocatorWrapper::VmaAllocatorWrapper(VmaAllocatorWrapper &&other) noexcept : allocator_(other.allocator_)
{
    other.allocator_ = nullptr;
}

VmaAllocatorWrapper &VmaAllocatorWrapper::operator=(VmaAllocatorWrapper &&other) noexcept
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

[[nodiscard]] VmaBuffer VmaAllocatorWrapper::createBuffer(const vk::BufferCreateInfo &bufferInfo,
                                                          const VmaAllocationCreateInfo &allocInfo) const
{
    VkBuffer buffer = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo resultInfo{};

    // Convert to C struct for VMA API
    VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    VkResult result = vmaCreateBuffer(allocator_, &cBufferInfo, &allocInfo, &buffer, &allocation, &resultInfo);
    nrAssert(result == VK_SUCCESS, "vmaCreateBuffer failed: {}", static_cast<int>(result));

    return VmaBuffer(allocator_, buffer, allocation, resultInfo);
}

[[nodiscard]] VmaImage VmaAllocatorWrapper::createImage(const vk::ImageCreateInfo &imageInfo,
                                                        const VmaAllocationCreateInfo &allocInfo) const
{
    VkImage image = nullptr;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo resultInfo{};

    // Convert to C struct for VMA API
    VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
    VkResult result = vmaCreateImage(allocator_, &cImageInfo, &allocInfo, &image, &allocation, &resultInfo);
    nrAssert(result == VK_SUCCESS, "vmaCreateImage failed: {}", static_cast<int>(result));

    return VmaImage(allocator_, image, allocation, resultInfo);
}

[[nodiscard]] VmaPoolHandle VmaAllocatorWrapper::createPool(const VmaPoolCreateInfo &poolInfo) const
{
    VmaPool pool = nullptr;
    VkResult result = vmaCreatePool(allocator_, &poolInfo, &pool);
    nrAssert(result == VK_SUCCESS, "vmaCreatePool failed: {}", static_cast<int>(result));

    return VmaPoolHandle(allocator_, pool, poolInfo);
}

[[nodiscard]] std::uint32_t VmaAllocatorWrapper::findMemoryTypeIndexForBuffer(
    const vk::BufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo) const
{
    std::uint32_t memTypeIndex = 0;
    // Convert to C struct for VMA API
    VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
    VkResult result = vmaFindMemoryTypeIndexForBufferInfo(allocator_, &cBufferInfo, &allocInfo, &memTypeIndex);
    nrAssert(result == VK_SUCCESS,
             "vmaFindMemoryTypeIndexForBufferInfo failed: {}", static_cast<int>(result));
    return memTypeIndex;
}

} // namespace nr::rhi
