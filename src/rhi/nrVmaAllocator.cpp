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
{ return buffer != nullptr; }

[[nodiscard]] void *VmaBuffer::mapped() const noexcept
{ return info.pMappedData; }

[[nodiscard]] VkDeviceSize VmaBuffer::size() const noexcept
{ return info.size; }

[[nodiscard]] VkBuffer VmaBuffer::handle() const noexcept
{ return buffer; }

[[nodiscard]] VkDeviceAddress VmaBuffer::deviceAddress(const vk::raii::Device& device) const noexcept
{
        return static_cast<VkDeviceAddress>(device.getBufferAddress(vk::BufferDeviceAddressInfo{vk::Buffer{buffer}}));
    }

[[nodiscard]] VkMemoryPropertyFlags VmaBuffer::memoryProperties() const
{
        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(allocator, allocation, &flags);
        return flags;
    }

[[nodiscard]] bool VmaBuffer::isHostVisible() const
{
        return (memoryProperties() & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
    }

void VmaBuffer::flush(VkDeviceSize offset, VkDeviceSize size) const
{
        if (allocation == nullptr)
            return;
        auto result = vmaFlushAllocation(allocator, allocation, offset, size);
        nrAssert(result == VK_SUCCESS, std::format("vmaFlushAllocation failed: {}", static_cast<int>(result)));
    }

void VmaBuffer::invalidate(VkDeviceSize offset, VkDeviceSize size) const
{
        if (allocation == nullptr)
            return;
        auto result = vmaInvalidateAllocation(allocator, allocation, offset, size);
        nrAssert(result == VK_SUCCESS, std::format("vmaInvalidateAllocation failed: {}", static_cast<int>(result)));
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
{ return image != nullptr; }

[[nodiscard]] VkImage VmaImage::handle() const noexcept
{ return image; }

[[nodiscard]] VkDeviceSize VmaImage::size() const noexcept
{ return info.size; }

[[nodiscard]] VkMemoryPropertyFlags VmaImage::memoryProperties() const
{
        VkMemoryPropertyFlags flags = 0;
        vmaGetAllocationMemoryProperties(allocator, allocation, &flags);
        return flags;
    }

VmaPoolHandle::VmaPoolHandle(VmaAllocator alloc, VmaPool p) : allocator(alloc), pool(p)
{}

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
{ return pool != nullptr; }

[[nodiscard]] VmaPool VmaPoolHandle::handle() const noexcept
{ return pool; }

void VmaPoolHandle::reset()
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

[[nodiscard]] VmaStatistics VmaPoolHandle::statistics() const
{
        VmaStatistics stats{};
        if (pool != nullptr)
        {
            vmaGetPoolStatistics(allocator, pool, &stats);
        }
        return stats;
    }

VmaAllocatorWrapper::VmaAllocatorWrapper(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice, const vk::raii::Device &device)
{
        VmaAllocatorCreateInfo createInfo{};
        createInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT |
                           VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT |
                           VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT |
                           VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
        createInfo.vulkanApiVersion = vk::ApiVersion14;
        createInfo.physicalDevice = *physDevice;
        createInfo.device = *device;
        createInfo.instance = *instance;

        VkResult result = vmaCreateAllocator(&createInfo, &allocator_);
        nrAssert(result == VkResult::VK_SUCCESS, std::format("Failed to create VMA allocator: {}", static_cast<int>(result)));
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

[[nodiscard]] bool VmaAllocatorWrapper::valid() const noexcept
{ return allocator_ != nullptr; }

[[nodiscard]] ::VmaAllocator VmaAllocatorWrapper::handle() const noexcept
{ return allocator_; }

[[nodiscard]] VmaBuffer VmaAllocatorWrapper::createBuffer(const vk::BufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo) const
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

[[nodiscard]] VmaImage VmaAllocatorWrapper::createImage(const vk::ImageCreateInfo &imageInfo, const VmaAllocationCreateInfo &allocInfo) const
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

[[nodiscard]] VmaPoolHandle VmaAllocatorWrapper::createPool(const VmaPoolCreateInfo &poolInfo) const
{
        VmaPool pool = nullptr;
        VkResult result = vmaCreatePool(allocator_, &poolInfo, &pool);
        nrAssert(result == VK_SUCCESS, std::format("vmaCreatePool failed: {}", static_cast<int>(result)));

        return VmaPoolHandle(allocator_, pool, poolInfo);
    }

[[nodiscard]] std::uint32_t VmaAllocatorWrapper::findMemoryTypeIndexForBuffer(const vk::BufferCreateInfo &bufferInfo, const VmaAllocationCreateInfo &allocInfo) const
{
        std::uint32_t memTypeIndex = 0;
        // Convert to C struct for VMA API
        VkBufferCreateInfo cBufferInfo = static_cast<VkBufferCreateInfo>(bufferInfo);
        VkResult result = vmaFindMemoryTypeIndexForBufferInfo(allocator_, &cBufferInfo, &allocInfo, &memTypeIndex);
        nrAssert(result == VK_SUCCESS, std::format("vmaFindMemoryTypeIndexForBufferInfo failed: {}", static_cast<int>(result)));
        return memTypeIndex;
    }

[[nodiscard]] std::uint32_t VmaAllocatorWrapper::findMemoryTypeIndexForImage(const vk::ImageCreateInfo &imageInfo, const VmaAllocationCreateInfo &allocInfo) const
{
        std::uint32_t memTypeIndex = 0;
        // Convert to C struct for VMA API
        VkImageCreateInfo cImageInfo = static_cast<VkImageCreateInfo>(imageInfo);
        VkResult result = vmaFindMemoryTypeIndexForImageInfo(allocator_, &cImageInfo, &allocInfo, &memTypeIndex);
        nrAssert(result == VK_SUCCESS, std::format("vmaFindMemoryTypeIndexForImageInfo failed: {}", static_cast<int>(result)));
        return memTypeIndex;
    }

[[nodiscard]] std::vector<VmaBudget> VmaAllocatorWrapper::getBudgets() const
{
        constexpr std::size_t maxHeaps = 16; // VMA supports up to 16 memory heaps
        std::vector<VmaBudget> budgets(maxHeaps);
        vmaGetHeapBudgets(allocator_, budgets.data());
        return budgets;
    }

[[nodiscard]] VmaTotalStatistics VmaAllocatorWrapper::calculateStatistics() const
{
        VmaTotalStatistics stats{};
        vmaCalculateStatistics(allocator_, &stats);
        return stats;
    }

[[nodiscard]] std::string VmaAllocatorWrapper::dumpStatsJson(bool detailedMap) const
{
        char *statsString = nullptr;
        vmaBuildStatsString(allocator_, &statsString, static_cast<VkBool32>(detailedMap ? 1 : 0));
        std::string result(statsString);
        vmaFreeStatsString(allocator_, statsString);
        return result;
    }
} // namespace nr::rhi
