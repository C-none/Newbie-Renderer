module nr.rhi;
import :memoryAllocator;
import dependency.vma;
import dependency.nsight;
import dependency.vulkan;
import nr.utils;
import :type;
import :vmaAllocator;
import std;

namespace nr::rhi
{
void MemoryAllocator::initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physDevice, const vk::raii::Device &device)
{
        device_ = std::ref(device);
        vma_ = VmaAllocatorWrapper(instance, physDevice, device);

        nsightProfilerActive_ = nr::platform::isNsightInjected() || nsightGraphicsActivityRequested();

        createPerFramePools();
        createStagingPool();

        if (nsightProfilerActive_)
        {
            nrLog(
                LogLevel::error,
                "RHI-DIAG",
                "Nsight Graphics profiling mode detected: applying profiler-safe buffer allocation metadata to avoid VUID-VkMemoryAllocateInfo-pNext-02806. This diagnostic channel is non-fatal.",
                std::source_location::current(),
                false);
            createProfilerSafePool();
        }
    }

[[nodiscard]] VmaBuffer MemoryAllocator::allocateBuffer(vk::DeviceSize size, vk::BufferUsageFlags bufferUsage, AllocationStrategy strategy, MemoryUsage usage, std::uint32_t frameIndex) const
{
        // Add BDA only for device-local buffers. CpuToGpu buffers are host-mapped
        // (UBOs via descriptor sets, vertex/index via bind, staging rings via copy)
        // and none consume a device address, so adding eShaderDeviceAddress would only
        // flip VMA's deviceAccess heuristic and force pure-staging buffers into scarce
        // BAR memory instead of system RAM. Callers that genuinely need an address
        // (e.g. shader binding tables) request eShaderDeviceAddress explicitly.
        if (usage == MemoryUsage::GpuOnly)
        {
            bufferUsage |= vk::BufferUsageFlagBits::eShaderDeviceAddress;
        }

        vk::BufferCreateInfo bufferInfo{};
        bufferInfo.size = size;
        bufferInfo.usage = bufferUsage;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocInfo{};

        switch (strategy)
        {
        case AllocationStrategy::CrossFrame:
            configureCrossFrame(allocInfo, usage);
            break;

        case AllocationStrategy::PerFrame:
            configurePerFrame(allocInfo, frameIndex);
            break;

        case AllocationStrategy::StagingTransient:
            configureStagingTransient(allocInfo);
            break;
        }

        configureProfilerBufferCompatibility(allocInfo);
        return vma_.createBuffer(bufferInfo, allocInfo);
    }

[[nodiscard]] VmaBuffer MemoryAllocator::allocateBuffer(const vk::BufferCreateInfo &createInfo, AllocationStrategy strategy, MemoryUsage usage, std::uint32_t frameIndex) const
{
        // Convert to C struct and delegate
        VkBufferCreateInfo bufferInfo = static_cast<VkBufferCreateInfo>(createInfo);

        // Add BDA only for device-local buffers (see size/usage overload for rationale):
        // CpuToGpu buffers never consume a device address, and auto-adding one would push
        // pure-staging buffers into BAR via VMA's deviceAccess heuristic.
        if (usage == MemoryUsage::GpuOnly)
        {
            bufferInfo.usage |= static_cast<VkBufferUsageFlags>(vk::BufferUsageFlagBits::eShaderDeviceAddress);
        }

        VmaAllocationCreateInfo allocInfo{};

        switch (strategy)
        {
        case AllocationStrategy::CrossFrame:
            configureCrossFrame(allocInfo, usage);
            break;
        case AllocationStrategy::PerFrame:
            configurePerFrame(allocInfo, frameIndex);
            break;
        case AllocationStrategy::StagingTransient:
            configureStagingTransient(allocInfo);
            break;
        }

        configureProfilerBufferCompatibility(allocInfo);
        return vma_.createBuffer(bufferInfo, allocInfo);
    }

[[nodiscard]] VmaImage MemoryAllocator::allocateImage(const vk::ImageCreateInfo &imageInfo, MemoryUsage usage) const
{
        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (usage == MemoryUsage::GpuToCpu)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }
        // GpuOnly: no flags needed — VMA defers dedicated decision to driver query results.

        return vma_.createImage(imageInfo, allocInfo);
    }

void MemoryAllocator::resetFramePool(std::uint32_t frameIndex)
{
        std::uint32_t idx = frameIndex % maxFrameInFlight;
        if (perFramePools_[idx].valid() && perFramePoolDirty_[idx])
        {
            perFramePools_[idx].reset();
            perFramePoolDirty_[idx] = false;
        }
    }

void MemoryAllocator::logBudget() const
{
        auto budgets = vma_.getBudgets();
        for (std::size_t i = 0; i < budgets.size(); ++i)
        {
            const auto &b = budgets[i];
            if (b.statistics.blockCount == 0)
                continue;

            nrInfo(std::format("Heap {}: {:.2f} MiB used / {:.2f} MiB allocated / {:.2f} MiB budget ({} blocks, {} allocs)", i, static_cast<double>(b.statistics.allocationBytes) / (1024.0 * 1024.0), static_cast<double>(b.statistics.blockBytes) / (1024.0 * 1024.0),
                               static_cast<double>(b.budget) / (1024.0 * 1024.0), b.statistics.blockCount, b.statistics.allocationCount));
        }
    }

void MemoryAllocator::logFramePoolStats() const
{
        for (std::uint32_t i = 0; i < maxFrameInFlight; ++i)
        {
            if (!perFramePools_[i].valid())
                continue;
            auto stats = perFramePools_[i].statistics();
            nrInfo(std::format("Frame pool [{}]: {:.2f} KiB used / {:.2f} KiB allocated ({} allocs)", i, static_cast<double>(stats.allocationBytes) / 1024.0, static_cast<double>(stats.blockBytes) / 1024.0, stats.allocationCount));
        }
    }

[[nodiscard]] bool MemoryAllocator::isHostVisible(const VmaBuffer &buffer)
{
        return buffer.isHostVisible();
    }

[[nodiscard]] const VmaAllocatorWrapper &MemoryAllocator::vma() const noexcept
{
        return vma_;
    }

[[nodiscard]] VmaAllocator MemoryAllocator::handle() const noexcept
{
        return vma_.handle();
    }

[[nodiscard]] bool MemoryAllocator::valid() const noexcept
{
        return vma_.valid();
    }

[[nodiscard]] bool MemoryAllocator::nsightGraphicsActivityRequested() noexcept
{
        const auto* value = std::getenv("NR_NSIGHT_GRAPHICS_ACTIVITY");
        if (value == nullptr)
        {
            return false;
        }

        auto text = std::string_view{value};
        return text == "capture" ||
               text == "CAPTURE" ||
               text == "Capture" ||
               text == "trace" ||
               text == "TRACE" ||
               text == "Trace";
    }

void MemoryAllocator::createPerFramePools()
{
        // Find the right memory type for host-visible, sequentially-written buffers
        vk::BufferCreateInfo sampleBuf{};
        sampleBuf.size = 0x10000; // sample size, not actual block size
        sampleBuf.usage = vk::BufferUsageFlagBits::eUniformBuffer | vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eShaderDeviceAddress;

        VmaAllocationCreateInfo sampleAlloc{};
        sampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        sampleAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        std::uint32_t memTypeIndex = vma_.findMemoryTypeIndexForBuffer(sampleBuf, sampleAlloc);

        for (std::uint32_t i = 0; i < maxFrameInFlight; ++i)
        {
            VmaPoolCreateInfo poolInfo{};
            poolInfo.memoryTypeIndex = memTypeIndex;
            poolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
            poolInfo.blockSize = 0;     // Let VMA manage block sizes
            poolInfo.maxBlockCount = 0; // No limit on blocks

            perFramePools_[i] = vma_.createPool(poolInfo);
        }
    }

void MemoryAllocator::createStagingPool()
{
        vk::BufferCreateInfo sampleBuf{};
        sampleBuf.size = 0x10000;
        sampleBuf.usage = vk::BufferUsageFlagBits::eTransferSrc;

        VmaAllocationCreateInfo sampleAlloc{};
        sampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;
        sampleAlloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

        std::uint32_t memTypeIndex = vma_.findMemoryTypeIndexForBuffer(sampleBuf, sampleAlloc);

        VmaPoolCreateInfo poolInfo{};
        poolInfo.memoryTypeIndex = memTypeIndex;
        poolInfo.flags = VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
        poolInfo.blockSize = 0;
        poolInfo.maxBlockCount = 0;

        stagingPool_ = vma_.createPool(poolInfo);
    }

void MemoryAllocator::createProfilerSafePool()
{
        // Representative GpuOnly buffer: device-local, BDA-enabled, broad GPU usage so
        // the query resolves to the same device-local memory type GpuOnly buffers use.
        vk::BufferCreateInfo sampleBuf{};
        sampleBuf.size = 0x10000;
        sampleBuf.usage = vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress;

        VmaAllocationCreateInfo sampleAlloc{};
        sampleAlloc.usage = VMA_MEMORY_USAGE_AUTO;

        std::uint32_t memTypeIndex = vma_.findMemoryTypeIndexForBuffer(sampleBuf, sampleAlloc);

        VmaPoolCreateInfo poolInfo{};
        poolInfo.memoryTypeIndex = memTypeIndex;
        // Explicit (non-zero) block size is what disables dedicated allocation for this pool.
        poolInfo.blockSize = static_cast<VkDeviceSize>(256) * 1024 * 1024;
        poolInfo.minBlockCount = 0;
        poolInfo.maxBlockCount = 0; // unlimited blocks

        profilerSafePool_ = vma_.createPool(poolInfo);
    }

void MemoryAllocator::configureCrossFrame(VmaAllocationCreateInfo &allocInfo, MemoryUsage usage) const
{
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        switch (usage)
        {
        case MemoryUsage::GpuOnly:
            // No explicit flags: VMA selects DEVICE_LOCAL and will use a dedicated
            // VkDeviceMemory only when the driver requires or prefers it for this resource.
            break;

        case MemoryUsage::CpuToGpu:
            // CpuToGpu is contractually host-writable and persistently mapped: every
            // CpuToGpu user maps the allocation (dynamic UBOs, staging rings, etc.).
            // Do NOT set HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT here: that bit lets VMA
            // place the buffer in non-host-visible memory, which silently drops the
            // mapping and breaks every mapped() caller on non-ReBAR configurations.
            //
            // Why a single CpuToGpu is sufficient (no staging/dynamic enum split):
            // With AUTO + SEQUENTIAL_WRITE (and no transfer-instead), VMA forces a
            // HOST_VISIBLE type, then bifurcates on the buffer's *usage* deviceAccess
            // (see vk_mem_alloc.h selection logic):
            //   - usage with GPU read (Uniform/Vertex/Index/SBT) => deviceAccess=true
            //     => DEVICE_LOCAL preferred => lands in BAR (DEVICE_LOCAL|HOST_VISIBLE)
            //     on the ReBAR target: full-bandwidth GPU reads, write-combined CPU writes.
            //   - transfer-only usage (TransferSrc staging ring) => deviceAccess=false
            //     => DEVICE_LOCAL not-preferred => stays in system RAM, leaving scarce
            //     BAR space free.
            // So memory placement is driven by buffer usage, not by enum granularity;
            // one CpuToGpu value already selects the optimal heap per buffer.
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;

        case MemoryUsage::GpuToCpu:
            // Host-cached for CPU read performance
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;

        case MemoryUsage::CpuOnly:
            // Persistently mapped host memory
            allocInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            break;
        }

        // Under Nsight, route GpuOnly buffers through the explicit-block-size pool so VMA
        // performs block sub-allocation instead of a dedicated allocation, avoiding the
        // VkMemoryDedicatedAllocateInfo that conflicts with Nsight's host-pointer import
        // (VUID-VkMemoryAllocateInfo-pNext-02806). No effect on normal runs or host-visible usages.
        if (nsightProfilerActive_ && usage == MemoryUsage::GpuOnly)
        {
            allocInfo.pool = profilerSafePool_.handle();
        }
    }

void MemoryAllocator::configureProfilerBufferCompatibility(VmaAllocationCreateInfo &allocInfo) const noexcept
{
        if (nsightProfilerActive_)
        {
            allocInfo.flags |= VMA_ALLOCATION_CREATE_CAN_ALIAS_BIT;
        }
    }

void MemoryAllocator::configurePerFrame(VmaAllocationCreateInfo &allocInfo, std::uint32_t frameIndex) const
{
        std::uint32_t idx = frameIndex % maxFrameInFlight;
        allocInfo.pool = perFramePools_[idx].handle();
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
        perFramePoolDirty_[idx] = true;
        // pool overrides memory type selection — no usage/flags needed for type
    }

void MemoryAllocator::configureStagingTransient(VmaAllocationCreateInfo &allocInfo) const
{
        allocInfo.pool = stagingPool_.handle();
        allocInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
} // namespace nr::rhi
