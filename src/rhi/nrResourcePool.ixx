export module nr.rhi:resourcePool;
import dependency.vulkan;
import nr.utils;
import :type;
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
 *   nrVmaAllocator.ixx     (VMA ownership wrapper)
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
    ResourceFactory(const MemoryAllocator &allocator, const vk::raii::Device &device);

    ResourceFactory(const ResourceFactory &) = delete;
    ResourceFactory &operator=(const ResourceFactory &) = delete;
    ResourceFactory(ResourceFactory &&) noexcept = default;
    ResourceFactory &operator=(ResourceFactory &&) noexcept = default;

    [[nodiscard]] const MemoryAllocator &allocator() const noexcept;

    [[nodiscard]] const vk::raii::Device &device() const noexcept;

    [[nodiscard]] Buffer createBuffer(const vk::BufferCreateInfo &createInfo,
                                      MemoryUsage memoryUsage = MemoryUsage::GpuOnly, std::string_view name = "") const;

    [[nodiscard]] Image createImage(const vk::ImageCreateInfo &createInfo,
                                    MemoryUsage memoryUsage = MemoryUsage::GpuOnly, std::string_view name = "") const;

  private:
    std::reference_wrapper<const MemoryAllocator> allocator_;
    std::reference_wrapper<const vk::raii::Device> device_;
};

// =========================================================================
// ResourcePool — frame-local arena only
// =========================================================================

/**
 * @brief High-level resource pool with per-frame scratch allocations
 *
 * Lifecycle:
 * - Constructed by Device after MemoryAllocator and vk::raii::Device creation
 * - resetFrame() called each frame after fence signal
 * - Must be destroyed before MemoryAllocator and Device
 */
class ResourcePool
{
  public:
    /**
     * @brief Construct the resource pool
     *
     * @param factory  Resource factory borrowing the allocator and device (must outlive this pool)
     */
    explicit ResourcePool(const ResourceFactory &factory);

    // Move-only
    ResourcePool(const ResourcePool &) = delete;
    ResourcePool &operator=(const ResourcePool &) = delete;
    ResourcePool(ResourcePool &&) noexcept = default;
    ResourcePool &operator=(ResourcePool &&) noexcept = default;

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
    Buffer &allocateTransientBuffer(const vk::BufferCreateInfo &createInfo, MemoryUsage memoryUsage,
                                    std::uint32_t frameIndex, std::string_view name = "");

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
    Image &allocateTransientImage(const vk::ImageCreateInfo &createInfo, MemoryUsage memoryUsage,
                                  std::uint32_t frameIndex, std::string_view name = "");

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
    void resetFrame(std::uint32_t frameIndex);

  private:
    // -----------------------------------------------------------------
    // Members
    // -----------------------------------------------------------------

    std::reference_wrapper<const ResourceFactory> factory_;

    // Per-frame transient resources (bulk-reset each frame)
    std::array<std::deque<Buffer>, maxFrameInFlight> frameBuffers_;
    std::array<std::deque<Image>, maxFrameInFlight> frameImages_;
};

} // namespace nr::rhi
