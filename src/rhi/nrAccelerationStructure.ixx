export module nr.rhi:accelerationStructure;
import dependency.vulkan;
import nr.utils;
import :type;
import :commandBatch;
import :resource;
import std;

export namespace nr::rhi
{
enum class AsBuildMode : unsigned
{
    Build,
    Update,
};

struct BlasGeometryLayout
{
    vk::Format vertexFormat = vk::Format::eR32G32B32Sfloat;
    vk::DeviceSize vertexStride = 0;
    vk::IndexType indexType = vk::IndexType::eUint32;
    std::uint32_t maxVertex = 0;
    vk::GeometryFlagsKHR geometryFlags{};
};

struct BlasGeometryInput
{
    vk::DeviceAddress vertexAddress = 0;
    vk::DeviceAddress indexAddress = 0;
    vk::DeviceAddress transformAddress = 0;
    std::uint32_t primitiveCount = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t primitiveOffset = 0;
};

struct BlasAabbGeometryInput
{
    vk::DeviceAddress dataAddress = 0;
    vk::DeviceSize stride = sizeof(vk::AabbPositionsKHR);
    std::uint32_t primitiveCount = 0;
    std::uint32_t primitiveOffset = 0;
    vk::GeometryFlagsKHR geometryFlags{};
};

struct BlasGeometryRecord
{
    std::reference_wrapper<const Buffer> geometryBuffer;
    vk::AccelerationStructureGeometryKHR geometry{};
    vk::AccelerationStructureBuildRangeInfoKHR range{};
};

struct TlasBuildInput
{
    vk::DeviceAddress instancesAddress = 0;
    std::uint32_t instanceCount = 0;
    bool arrayOfPointers = false;
};

struct AsBuildOptions
{
    AsBuildMode mode = AsBuildMode::Build;
    vk::BuildAccelerationStructureFlagsKHR buildFlags{};
    bool allowUpdateExpected = false;
};

struct AsSubmitIntent
{
    // Optional queue-submit metadata for AS build orchestration.
    // This struct does not submit work by itself; it is translated into CommandBatch
    // wait/signal entries by appendAsSubmitIntent().
    //
    // Synchronization model:
    // - Use semaphore wait/signal here for cross-queue dependencies such as
    //   Transfer -> Compute(AS build) -> Graphics/Compute(AS consume).
    // - If no queue-family ownership transfer or image layout transition is involved,
    //   the semaphore wait already provides the inter-queue memory dependency.
    // - If the same timeline semaphore is both waited and signaled in one submit,
    //   signalValue must be strictly greater than waitValue.
    vk::Semaphore waitSemaphore{};
    std::uint64_t waitValue = 0;
    vk::PipelineStageFlags2 waitStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
    vk::Semaphore signalSemaphore{};
    std::uint64_t signalValue = 0;
    vk::PipelineStageFlags2 signalStageMask = vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR;
};

struct AsBuildSizes
{
    vk::DeviceSize accelerationStructureSize = 0;
    vk::DeviceSize buildScratchSize = 0;
    vk::DeviceSize updateScratchSize = 0;
};

struct AsIndirectBuildCommand
{
    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    vk::DeviceAddress indirectDeviceAddress = 0;
    std::uint32_t indirectStride = 0;
    std::span<const std::uint32_t> maxPrimitiveCounts{};
};

struct AsBuildLimits
{
    // Device-side limits relevant to AS query/build helpers.
    // Call queryAsBuildLimits() once after device creation and thread the values
    // into validation/record paths. minScratchAlignment should be passed to
    // validateAsBuildInputs()/recordBuild*() so caller-owned scratch addresses
    // satisfy Vulkan alignment requirements.
    vk::DeviceSize minScratchAlignment = 1;
    std::uint64_t maxGeometryCount = 0;
    std::uint64_t maxPrimitiveCount = 0;
    std::uint64_t maxInstanceCount = 0;
};

class AccelerationStructureResource
{
  public:
    AccelerationStructureResource() = default;
    AccelerationStructureResource(const AccelerationStructureResource &) = delete;
    AccelerationStructureResource &operator=(const AccelerationStructureResource &) = delete;
    AccelerationStructureResource(AccelerationStructureResource &&) noexcept = default;
    AccelerationStructureResource &operator=(AccelerationStructureResource &&) noexcept = default;

    [[nodiscard]] static AccelerationStructureResource create(const vk::raii::Device &device,
                                                              const Buffer &storageBuffer, vk::DeviceSize storageOffset,
                                                              vk::DeviceSize accelerationStructureSize,
                                                              vk::AccelerationStructureTypeKHR type,
                                                              std::string_view name = {},
                                                              vk::AccelerationStructureCreateFlagsKHR createFlags = {});

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] vk::AccelerationStructureKHR raw() const noexcept;

    [[nodiscard]] const Buffer &storageBuffer() const;

    [[nodiscard]] vk::DeviceSize storageOffset() const noexcept;

    [[nodiscard]] vk::DeviceSize size() const noexcept;

    [[nodiscard]] vk::AccelerationStructureTypeKHR type() const noexcept;

    [[nodiscard]] vk::DeviceAddress deviceAddress() const;

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    std::optional<std::reference_wrapper<const Buffer>> storageBuffer_{};
    vk::DeviceSize storageOffset_ = 0;
    vk::DeviceSize accelerationStructureSize_ = 0;
    vk::AccelerationStructureTypeKHR type_ = vk::AccelerationStructureTypeKHR::eBottomLevel;
    std::string name_{};
    vk::raii::AccelerationStructureKHR handle_ = {nullptr};
};

struct BlasBuildRecordInfo
{
    const AccelerationStructureResource &dst;
    std::optional<std::reference_wrapper<const AccelerationStructureResource>> src{};
    const Buffer &geometryBuffer;
    const Buffer &scratchBuffer;
    vk::DeviceAddress scratchAddress = 0;
    BlasGeometryLayout geometryLayout{};
    BlasGeometryInput geometryInput{};
    AsBuildOptions options{};
};

struct BlasGeometriesBuildRecordInfo
{
    const AccelerationStructureResource &dst;
    std::optional<std::reference_wrapper<const AccelerationStructureResource>> src{};
    std::span<const BlasGeometryRecord> geometries{};
    const Buffer &scratchBuffer;
    vk::DeviceAddress scratchAddress = 0;
    AsBuildOptions options{};
};

struct BlasBatchBuildRecordInfo
{
    BlasGeometriesBuildRecordInfo build;
    vk::DeviceSize scratchSize = 0;
};

struct TlasBuildRecordInfo
{
    const AccelerationStructureResource &dst;
    std::optional<std::reference_wrapper<const AccelerationStructureResource>> src{};
    const Buffer &instanceBuffer;
    const Buffer &scratchBuffer;
    vk::DeviceAddress scratchAddress = 0;
    TlasBuildInput buildInput{};
    AsBuildOptions options{};
};

struct AsCopyRecordInfo
{
    const AccelerationStructureResource &src;
    const AccelerationStructureResource &dst;
    vk::CopyAccelerationStructureModeKHR mode = vk::CopyAccelerationStructureModeKHR::eClone;
};

struct AsCopyToDeviceMemoryRecordInfo
{
    const AccelerationStructureResource &src;
    vk::DeviceAddress dstAddress = 0;
    vk::CopyAccelerationStructureModeKHR mode = vk::CopyAccelerationStructureModeKHR::eSerialize;
};

struct AsCopyFromDeviceMemoryRecordInfo
{
    vk::DeviceAddress srcAddress = 0;
    const AccelerationStructureResource &dst;
    vk::CopyAccelerationStructureModeKHR mode = vk::CopyAccelerationStructureModeKHR::eDeserialize;
};

} // namespace nr::rhi

namespace nr::rhi::detail
{
struct ValidationResult
{
    bool isValid = false;
    std::string message{};
};

[[nodiscard]] ValidationResult validationSuccess();

[[nodiscard]] ValidationResult validationFailure(std::string message);

template <typename... Args> [[nodiscard]] inline std::string formatMessage(std::string_view format, const Args &...args)
{
    return std::vformat(format, std::make_format_args(args...));
}

[[nodiscard]] vk::BuildAccelerationStructureModeKHR toVkBuildMode(AsBuildMode mode);

[[nodiscard]] bool hasBuildFlag(vk::BuildAccelerationStructureFlagsKHR flags,
                                vk::BuildAccelerationStructureFlagBitsKHR bit);

[[nodiscard]] ValidationResult validateBuildFlagCombination(const AsBuildOptions &options);

[[nodiscard]] vk::DeviceSize indexTypeAlignment(vk::IndexType type);

[[nodiscard]] std::uint32_t geometryPrimitiveCount(const BlasGeometryRecord &record) noexcept;

[[nodiscard]] ValidationResult validateTriangleGeometry(
    const vk::AccelerationStructureGeometryTrianglesDataKHR &triangles,
    const vk::AccelerationStructureBuildRangeInfoKHR &range);

[[nodiscard]] ValidationResult validateAabbGeometry(const vk::AccelerationStructureGeometryAabbsDataKHR &aabbs,
                                                    const vk::AccelerationStructureBuildRangeInfoKHR &range);

[[nodiscard]] ValidationResult validateBlasGeometryRecord(const BlasGeometryRecord &record);

[[nodiscard]] ValidationResult validateBlasGeometryRecords(std::span<const BlasGeometryRecord> geometries);

[[nodiscard]] ValidationResult validateIndirectBuildCommands(std::span<const AsIndirectBuildCommand> commands);

[[nodiscard]] ValidationResult validateAsCopyInfo(const vk::CopyAccelerationStructureInfoKHR &info);

[[nodiscard]] ValidationResult validateAsCopyInfo(const AsCopyRecordInfo &info);

[[nodiscard]] ValidationResult validateAsCopyToMemoryInfo(const vk::CopyAccelerationStructureToMemoryInfoKHR &info);

[[nodiscard]] ValidationResult validateAsCopyToMemoryInfo(const AsCopyToDeviceMemoryRecordInfo &info);

[[nodiscard]] ValidationResult validateAsCopyFromMemoryInfo(const vk::CopyMemoryToAccelerationStructureInfoKHR &info);

[[nodiscard]] ValidationResult validateAsCopyFromMemoryInfo(const AsCopyFromDeviceMemoryRecordInfo &info);

} // namespace nr::rhi::detail

export namespace nr::rhi
{

// Query physical-device AS limits needed by the caller to size scratch buffers,
// validate primitive counts, and choose legal alignment.
[[nodiscard]] AsBuildLimits queryAsBuildLimits(const vk::raii::PhysicalDevice &physicalDevice);

[[nodiscard]] BlasGeometryRecord makeBlasTriangleGeometryRecord(const Buffer &geometryBuffer,
                                                                const BlasGeometryLayout &layout,
                                                                const BlasGeometryInput &input);

[[nodiscard]] BlasGeometryRecord makeBlasAabbGeometryRecord(const Buffer &geometryBuffer,
                                                            const BlasAabbGeometryInput &input);

} // namespace nr::rhi

namespace nr::rhi::detail
{

[[nodiscard]] ValidationResult validateAsBuildInputs(const BlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment);

[[nodiscard]] ValidationResult validateAsBuildInputs(const BlasGeometriesBuildRecordInfo &info,
                                                     vk::DeviceSize scratchAlignment);

[[nodiscard]] ValidationResult validateAsBuildInputs(const TlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment);

} // namespace nr::rhi::detail

export namespace nr::rhi
{

[[nodiscard]] AsBuildSizes queryAccelerationStructureBuildSizes(
    const vk::raii::Device &device, vk::AccelerationStructureTypeKHR type,
    std::span<const vk::AccelerationStructureGeometryKHR> geometries, std::span<const std::uint32_t> maxPrimitiveCounts,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice);

[[nodiscard]] AsBuildSizes queryBlasBuildSizes(
    const vk::raii::Device &device, const BlasGeometryLayout &layout, std::uint32_t primitiveCount,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice);

[[nodiscard]] AsBuildSizes queryBlasBuildSizes(
    const vk::raii::Device &device, std::span<const BlasGeometryRecord> geometries, const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice);

[[nodiscard]] AsBuildSizes queryTlasBuildSizes(
    const vk::raii::Device &device, const TlasBuildInput &input, const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice);

// Record one BLAS build into an existing command buffer.
// This function only records vkCmdBuildAccelerationStructuresKHR and never submits.
//
// NVIDIA RTX guidance (developer.nvidia.com):
// - Static BLAS should usually use PREFER_FAST_TRACE, optionally with
//   ALLOW_COMPACTION when the caller will run post-build compaction.
// - Dynamic BLAS update is suitable for small, coherent deformation. Large
//   deformation, fractured geometry, particles, or primitive-count changes are
//   better handled by rebuilds, often with PREFER_FAST_BUILD.
// - Compaction is not completed by ALLOW_COMPACTION alone. The caller must
//   build, query compacted size after the GPU build finishes, create a new AS,
//   copy with COMPACT, then release the old AS. NVIDIA notes that compacted
//   size is only known after the build.
//
// Synchronization notes:
// - Cross-queue producer/consumer ordering should normally be expressed with
//   semaphores at submit time.
// - A pre-build pipeline barrier is not required solely to make transfer-queue
//   buffer writes visible to this build when a semaphore wait already orders the
//   transfer submission and no queue-family ownership transfer is involved.
// - A barrier is still required for queue-local hazards, e.g. ordering a BLAS
//   build before a dependent TLAS build in the same command list.
// - If source buffers use VK_SHARING_MODE_EXCLUSIVE across different queue
//   families, caller must perform queue-family release/acquire barriers.
void recordBuildBlasGeometries(const vk::raii::CommandBuffer &commandBuffer, const BlasGeometriesBuildRecordInfo &info,
                               vk::DeviceSize scratchAlignment = 1);

void recordBuildBlasBatch(const vk::raii::CommandBuffer &commandBuffer,
                          std::span<const BlasBatchBuildRecordInfo> records, vk::DeviceSize scratchAlignment = 1);

void recordBuildBlas(const vk::raii::CommandBuffer &commandBuffer, const BlasBuildRecordInfo &info,
                     vk::DeviceSize scratchAlignment = 1);

// Record one TLAS build into an existing command buffer.
// This function only records vkCmdBuildAccelerationStructuresKHR and never submits.
//
// NVIDIA RTX guidance (developer.nvidia.com): TLAS builds should usually use
// PREFER_FAST_TRACE and rebuild instead of update; in many scenes update can
// lose enough traversal quality that the cheaper refit is not worth it.
//
// If this TLAS consumes BLAS results produced earlier in the same command list,
// caller must insert a barrier from
//   srcStage = eAccelerationStructureBuildKHR,
//   srcAccess = eAccelerationStructureWriteKHR
// to
//   dstStage = eAccelerationStructureBuildKHR,
//   dstAccess = eAccelerationStructureReadKHR
// before calling recordBuildTlas().
void recordBuildTlas(const vk::raii::CommandBuffer &commandBuffer, const TlasBuildRecordInfo &info,
                     vk::DeviceSize scratchAlignment = 1);

void recordUpdateBlas(const vk::raii::CommandBuffer &commandBuffer, BlasBuildRecordInfo info,
                      vk::DeviceSize scratchAlignment = 1);

void recordUpdateTlas(const vk::raii::CommandBuffer &commandBuffer, TlasBuildRecordInfo info,
                      vk::DeviceSize scratchAlignment = 1);

void recordBuildAccelerationStructuresIndirect(const vk::raii::CommandBuffer &commandBuffer,
                                               std::span<const AsIndirectBuildCommand> commands);

[[nodiscard]] vk::CopyAccelerationStructureInfoKHR makeCopyAccelerationStructureInfo(const AsCopyRecordInfo &record);

[[nodiscard]] vk::CopyAccelerationStructureToMemoryInfoKHR makeCopyAccelerationStructureToMemoryInfo(
    const AsCopyToDeviceMemoryRecordInfo &record);

[[nodiscard]] vk::CopyMemoryToAccelerationStructureInfoKHR makeCopyMemoryToAccelerationStructureInfo(
    const AsCopyFromDeviceMemoryRecordInfo &record);

void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                     const vk::CopyAccelerationStructureInfoKHR &info);

void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const AsCopyRecordInfo &record);

void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer,
                                             const vk::CopyAccelerationStructureToMemoryInfoKHR &info);

void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer,
                                             const AsCopyToDeviceMemoryRecordInfo &record);

void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                             const vk::CopyMemoryToAccelerationStructureInfoKHR &info);

void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                             const AsCopyFromDeviceMemoryRecordInfo &record);

[[nodiscard]] vk::Result copyAccelerationStructure(const vk::raii::Device &device,
                                                   vk::DeferredOperationKHR deferredOperation,
                                                   const vk::CopyAccelerationStructureInfoKHR &info);

[[nodiscard]] vk::Result copyAccelerationStructure(const vk::raii::Device &device,
                                                   vk::DeferredOperationKHR deferredOperation,
                                                   const AsCopyRecordInfo &record);

[[nodiscard]] vk::Result copyAccelerationStructureToMemory(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const vk::CopyAccelerationStructureToMemoryInfoKHR &info);

[[nodiscard]] vk::Result copyAccelerationStructureToMemory(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const AsCopyToDeviceMemoryRecordInfo &record);

[[nodiscard]] vk::Result copyMemoryToAccelerationStructure(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const vk::CopyMemoryToAccelerationStructureInfoKHR &info);

[[nodiscard]] vk::Result copyMemoryToAccelerationStructure(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const AsCopyFromDeviceMemoryRecordInfo &record);

[[nodiscard]] vk::raii::DeferredOperationKHR createDeferredOperation(const vk::raii::Device &device);

template <typename T>
[[nodiscard]] inline std::vector<T> queryAccelerationStructureProperties(
    const vk::raii::Device &device, std::span<const vk::AccelerationStructureKHR> accelerationStructures,
    vk::QueryType queryType)
{
    nrAssert(*device != nullptr, "queryAccelerationStructureProperties requires a valid device.");
    nrAssert(!accelerationStructures.empty(),
             "queryAccelerationStructureProperties requires at least one acceleration structure.");
    nrAssert(std::ranges::none_of(
                 accelerationStructures,
                 [](vk::AccelerationStructureKHR handle) { return handle == vk::AccelerationStructureKHR{}; }),
             "queryAccelerationStructureProperties requires valid acceleration structure handles.");

    return device.writeAccelerationStructuresPropertiesKHR<T>(accelerationStructures, queryType,
                                                              sizeof(T) * accelerationStructures.size(), sizeof(T));
}

template <typename T>
[[nodiscard]] inline T queryAccelerationStructureProperty(const vk::raii::Device &device,
                                                          const AccelerationStructureResource &accelerationStructure,
                                                          vk::QueryType queryType)
{
    nrAssert(accelerationStructure.valid(),
             "queryAccelerationStructureProperty requires a valid acceleration structure.");
    auto handles = std::array{accelerationStructure.raw()};
    auto values = queryAccelerationStructureProperties<T>(
        device, std::span<const vk::AccelerationStructureKHR>{handles}, queryType);
    nrAssert(values.size() == 1, "queryAccelerationStructureProperty expected one result.");
    return values.front();
}

[[nodiscard]] vk::DeviceSize queryAccelerationStructureCompactedSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure);

[[nodiscard]] vk::DeviceSize queryAccelerationStructureSerializationSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure);

[[nodiscard]] vk::DeviceSize queryAccelerationStructureDeviceTimelineSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure);

[[nodiscard]] std::uint64_t queryAccelerationStructureSerializationBottomLevelPointerCount(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure);

void recordWriteAccelerationStructureProperties(const vk::raii::CommandBuffer &commandBuffer,
                                                std::span<const vk::AccelerationStructureKHR> accelerationStructures,
                                                vk::QueryType queryType, vk::QueryPool queryPool,
                                                std::uint32_t firstQuery);

void recordWriteAccelerationStructureProperty(const vk::raii::CommandBuffer &commandBuffer,
                                              const AccelerationStructureResource &accelerationStructure,
                                              vk::QueryType queryType, vk::QueryPool queryPool,
                                              std::uint32_t firstQuery);

// Append AS-related wait/signal metadata into an existing CommandBatch.
//
// Intended usage:
// 1. Caller records one or more AS build command buffers.
// 2. Caller fills AsSubmitIntent with cross-queue semaphore requirements.
// 3. Caller calls appendAsSubmitIntent(batch, intent).
// 4. Caller submits batch through QueueManager/GpuQueue.
//
// This helper does not create barriers and does not submit queues. Use it for
// inter-queue ordering only. Queue-local memory hazards, queue-family ownership
// transfers, and image layout transitions must still be expressed with the
// appropriate barrier commands in command buffers.
void appendAsSubmitIntent(CommandBatch &batch, const AsSubmitIntent &intent);
} // namespace nr::rhi
