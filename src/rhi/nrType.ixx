export module nr.rhi:type;
import std;

/**
 * @file nrType.ixx
 * @brief Centralized type definitions for the RHI module
 *
 * This file contains all enum types and common type aliases used across
 * the RHI module. Centralizing types here:
 * - Prevents circular dependencies between partitions
 * - Provides a single source of truth for type definitions
 * - Simplifies cross-partition type sharing
 *
 * All partitions that need these types should `import :type;`
 */

export namespace nr::rhi
{

/**
 * @brief Physical queue family classification for device initialization
 *
 * Used during device creation to identify and select queue families
 * from the physical device. Maps to Vulkan queue family capabilities.
 *
 * Note: This is a low-level enumeration for hardware capability detection.
 * For runtime queue access, use QueueRole instead.
 */
enum class QueueFamilyKind : std::size_t
{
    graphics,
    compute,
    transfer,
    videoDecode,
    videoEncode,
    opticalFlow,
    size ///< Sentinel value for array sizing
};

/**
 * @brief Logical queue type for runtime command submission
 *
 * Represents the three primary queue types used during rendering:
 * - Graphics: Rendering, compute, and transfer operations
 * - Compute: Async compute workloads
 * - Transfer: DMA transfers
 *
 * Note: This is a high-level abstraction for queue access.
 * For physical queue family selection, see QueueFamilyKind.
 */
enum class QueueRole : unsigned
{
    Graphics,
    Compute,
    Transfer
};

/**
 * @brief Memory usage intent for resource allocation
 *
 * Determines the optimal memory type selection via VMA.
 * Maps to VMA_MEMORY_USAGE_AUTO with appropriate host access flags.
 */
enum class MemoryUsage : unsigned
{
    GpuOnly,  ///< Device-local, no host access (textures, RT targets, BLAS/TLAS)
    CpuToGpu, ///< Host-visible, sequential write (staging uploads, dynamic UBOs)
    GpuToCpu  ///< Host-visible, random read (readback, query results)
};

/**
 * @brief Allocation lifetime and strategy classification
 *
 * Controls which VMA pool a resource is allocated from and
 * how its memory is managed relative to frame boundaries.
 */
enum class AllocationStrategy : unsigned
{
    CrossFrame, ///< Long-lived, survives multiple frames (default pool, standard alloc)
    PerFrame    ///< Single-frame lifetime, allocated from linear pool, bulk-reset each frame
};

struct RayTracingCapabilitySnapshot
{
    std::uint32_t shaderGroupHandleSize = 0;
    std::uint32_t shaderGroupHandleAlignment = 1;
    std::uint32_t shaderGroupBaseAlignment = 1;
    std::uint32_t maxShaderGroupStride = 0;
    std::uint32_t maxRayDispatchInvocationCount = 0;
    std::uint32_t maxRayRecursionDepth = 0;

    std::array<std::uint64_t, 3> maxDispatchDimensions = {0u, 0u, 0u};
};

} // namespace nr::rhi
