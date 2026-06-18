export module nr.rhi:resourcePool;
import dependency;
import nr.utils;
import :type;
import :vk;
import :resource;
import :memoryAllocator;
import std;

/**
 * @file nrResourcePool.ixx
 * @brief Frame-local resource arena and persistent resource factory
 *
 * Sits at the top of the memory management stack:
 *
 *   nrResourcePool.ixx   <- this file (frame-local arena + persistent factory)
 *   nrResource.ixx         (Buffer/Image RAII wrappers)
 *   nrMemoryAllocator.ixx  (strategy-based allocation)
 *   nrVmaAllocator.ixx     (VMA wrapper + statistics)
 *   Vulkan Device Layer
 *
 * Resource management mode:
 *
 * **Transient (per-frame):** Scratch buffers and images allocated from
 *    linear pools. Bulk-reset each frame after fence signal. Zero CPU
 *    overhead on deallocation.
 *
 * Persistent caller-owned Buffer/Image creation is provided via ResourceFactory.
 *
 * Thread Safety:
 * - Transient allocation methods are NOT thread-safe (one writer per frame).
 * - resetFrame() must be externally synchronized per frame index.
 */

export namespace nr::rhi
{

class ResourceFactory
{
  public:
    ResourceFactory() = default;

    ResourceFactory(const ResourceFactory &) = delete;
    ResourceFactory &operator=(const ResourceFactory &) = delete;
    ResourceFactory(ResourceFactory &&) noexcept = default;
    ResourceFactory &operator=(ResourceFactory &&) noexcept = default;

    void initialize(const MemoryAllocator &allocator, const vk::raii::Device &device)
    {
        allocator_ = std::cref(allocator);
        device_ = std::cref(device);
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return device_.has_value();
    }

    [[nodiscard]] Buffer createBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage = MemoryUsage::GpuOnly, std::string_view name = "") const
    {
        nrAssert(valid(), "ResourceFactory::createBuffer: factory not initialized");
        return Buffer::create(*allocator_, device_->get(), createInfo, name, memoryUsage, AllocationStrategy::CrossFrame);
    }

    [[nodiscard]] Image createImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage = MemoryUsage::GpuOnly, std::string_view name = "") const
    {
        nrAssert(valid(), "ResourceFactory::createImage: factory not initialized");
        return Image::create(*allocator_, device_->get(), createInfo, name, memoryUsage);
    }

  private:
    std::optional<std::reference_wrapper<const MemoryAllocator>> allocator_{};
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
};

// =========================================================================
// ResourcePool — frame-local arena only
// =========================================================================

/**
 * @brief High-level resource pool with per-frame scratch allocations
 *
 * Lifecycle:
 * - Initialized by Device::initialize() after MemoryAllocator and vk::raii::Device creation
 * - resetFrame() called each frame after fence signal
 * - Must be destroyed before MemoryAllocator and Device
 */
class ResourcePool
{
  public:
    ResourcePool() = default;

    // Move-only
    ResourcePool(const ResourcePool &) = delete;
    ResourcePool &operator=(const ResourcePool &) = delete;
    ResourcePool(ResourcePool &&) noexcept = default;
    ResourcePool &operator=(ResourcePool &&) noexcept = default;

    /**
     * @brief Initialize the resource pool
     *
     * @param allocator  Initialized MemoryAllocator (must outlive this pool)
     * @param device     RAII device (must outlive this pool)
     */
    void initialize(const MemoryAllocator &allocator, const vk::raii::Device &device)
    {
        allocator_ = std::cref(allocator);
        device_ = std::cref(device);
    }

    /// Check if initialized
    [[nodiscard]] bool valid() const noexcept
    {
        return device_.has_value();
    }

    /**
     * @brief Allocate a per-frame transient buffer
     *
     * The buffer is owned by the pool and destroyed on resetFrame().
     * Strategy is forced to PerFrame.
     *
     * @param createInfo   Vulkan buffer create info
     * @param memoryUsage  Memory placement intent
     * @param frameIndex   Current frame index
     * @param name         Optional debug name for profiling tools
     * @return Non-owning reference to the allocated buffer (valid until resetFrame)
     */
    Buffer &allocateTransientBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage, std::uint32_t frameIndex, std::string_view name = "")
    {
        nrAssert(valid(), "ResourcePool::allocateTransientBuffer: pool not initialized");

        std::uint32_t idx = frameIndex % maxFrameInFlight;
        frameBuffers_[idx].emplace_back(Buffer::create(*allocator_, device_->get(), createInfo, name, memoryUsage, AllocationStrategy::PerFrame, frameIndex));
        return frameBuffers_[idx].back();
    }

    /**
     * @brief Allocate a per-frame transient image
     *
     * The image is owned by the pool and destroyed on resetFrame().
     * Note: Images are always CrossFrame in MemoryAllocator, but the
     * pool manages their lifetime scoped to the frame.
     *
     * @param createInfo   Vulkan image create info
     * @param memoryUsage  Memory placement intent
     * @param frameIndex   Current frame index
     * @param name         Optional debug name for profiling tools
     * @return Non-owning reference to the allocated image (valid until resetFrame)
     */
    Image &allocateTransientImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage, std::uint32_t frameIndex, std::string_view name = "")
    {
        nrAssert(valid(), "ResourcePool::allocateTransientImage: pool not initialized");

        std::uint32_t idx = frameIndex % maxFrameInFlight;
        frameImages_[idx].emplace_back(Image::create(*allocator_, device_->get(), createInfo, name, memoryUsage));
        return frameImages_[idx].back();
    }

    // =====================================================================
    // Frame Lifecycle
    // =====================================================================

    /**
     * @brief Reset all transient resources for the given frame
     *
     * Destroys all per-frame buffers and images. Must be called after the
     * frame's fence is signaled (all GPU work complete).
     *
     * NOT thread-safe — caller must ensure no concurrent access to this frame.
     */
    void resetFrame(std::uint32_t frameIndex)
    {
        std::uint32_t idx = frameIndex % maxFrameInFlight;

        // RAII destruction cascades to VMA + ImageViews
        frameBuffers_[idx].clear();
        frameImages_[idx].clear();
    }
  private:
    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------

    std::optional<std::reference_wrapper<const MemoryAllocator>> allocator_{};
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};

    // Per-frame transient resources (bulk-reset each frame)
    std::array<std::deque<Buffer>, maxFrameInFlight> frameBuffers_;
    std::array<std::deque<Image>, maxFrameInFlight> frameImages_;
};

} // namespace nr::rhi
