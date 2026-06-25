module;
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

export module dependency.vma;
export import dependency.vulkan;

export using ::VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
export using ::VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
export using ::VMA_ALLOCATION_CREATE_MAPPED_BIT;
export using ::VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
export using ::VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
export using ::VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
export using ::VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
export using ::VMA_MEMORY_USAGE_AUTO;
export using ::VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
export using ::VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
export using ::VmaAllocation;
export using ::VmaAllocationCreateInfo;
export using ::VmaAllocationInfo;
export using ::VmaAllocator;
export using ::VmaAllocatorCreateInfo;
export using ::VmaBudget;
export using ::VmaPool;
export using ::VmaPoolCreateInfo;
export using ::VmaStatistics;
export using ::VmaTotalStatistics;
export using ::vmaBuildStatsString;
export using ::vmaCalculateStatistics;
export using ::vmaCreateAllocator;
export using ::vmaCreateBuffer;
export using ::vmaCreateImage;
export using ::vmaCreatePool;
export using ::vmaDestroyAllocator;
export using ::vmaDestroyBuffer;
export using ::vmaDestroyImage;
export using ::vmaDestroyPool;
export using ::vmaFindMemoryTypeIndexForBufferInfo;
export using ::vmaFindMemoryTypeIndexForImageInfo;
export using ::vmaFlushAllocation;
export using ::vmaFreeStatsString;
export using ::vmaGetAllocationMemoryProperties;
export using ::vmaGetHeapBudgets;
export using ::vmaGetPoolStatistics;
export using ::vmaInvalidateAllocation;
