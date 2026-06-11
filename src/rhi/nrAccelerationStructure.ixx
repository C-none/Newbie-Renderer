export module nr.rhi:accelerationStructure;
import dependency;
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

    [[nodiscard]] static AccelerationStructureResource create(
        const vk::raii::Device &device,
        const Buffer &storageBuffer,
        vk::DeviceSize storageOffset,
        vk::DeviceSize accelerationStructureSize,
        vk::AccelerationStructureTypeKHR type,
        std::string_view name = {})
    {
        nrAssert(storageBuffer.valid(), "AccelerationStructureResource::create requires a valid storage buffer.");
        nrAssert(accelerationStructureSize > 0, "AccelerationStructureResource::create requires accelerationStructureSize > 0.");
        nrAssert((storageBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR) != vk::BufferUsageFlags{},
                 "AccelerationStructureResource::create requires VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR.");
        nrAssert(storageOffset <= storageBuffer.size() && accelerationStructureSize <= (storageBuffer.size() - storageOffset),
             "AccelerationStructureResource::create requires storageOffset + accelerationStructureSize to fit inside storage buffer size.");

        AccelerationStructureResource result;
        result.device_ = std::cref(device);
        result.storageBuffer_ = std::cref(storageBuffer);
        result.storageOffset_ = storageOffset;
        result.accelerationStructureSize_ = accelerationStructureSize;
        result.type_ = type;
        result.name_ = name;

        vk::AccelerationStructureCreateInfoKHR createInfo{};
        createInfo.buffer = storageBuffer.handle();
        createInfo.offset = storageOffset;
        createInfo.size = accelerationStructureSize;
        createInfo.type = type;
        result.handle_ = vk::raii::AccelerationStructureKHR(device, createInfo);

        if constexpr (isDebugMode)
        {
            if (!result.name_.empty())
            {
                vk::DebugUtilsObjectNameInfoEXT objectNameInfo{};
                objectNameInfo.objectType = vk::ObjectType::eAccelerationStructureKHR;
                const auto rawHandle = static_cast<VkAccelerationStructureKHR>(*result.handle_);
                static_assert(sizeof(rawHandle) == sizeof(std::uint64_t), "VkAccelerationStructureKHR handle size must match std::uint64_t for debug naming.");
                objectNameInfo.objectHandle = std::bit_cast<std::uint64_t>(rawHandle);
                objectNameInfo.pObjectName = result.name_.c_str();
                try
                {
                    result.device_->get().setDebugUtilsObjectNameEXT(objectNameInfo);
                }
                catch (const vk::SystemError &error)
                {
                    auto errorText = std::string_view{error.what()};
                    nrInfo<LogLevel::error>(std::vformat(
                        "AccelerationStructureResource::create failed to set debug name '{}': {}",
                        std::make_format_args(result.name_, errorText)));
                    nrAssert(false, "AccelerationStructureResource::create failed to set a Vulkan debug object name.");
                }
            }
        }

        return result;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return *handle_ != nullptr;
    }

    [[nodiscard]] vk::AccelerationStructureKHR raw() const noexcept
    {
        return valid() ? *handle_ : vk::AccelerationStructureKHR{};
    }

    [[nodiscard]] const Buffer &storageBuffer() const
    {
        nrAssert(storageBuffer_.has_value(), "AccelerationStructureResource::storageBuffer requires valid storage linkage.");
        return storageBuffer_->get();
    }

    [[nodiscard]] vk::DeviceSize storageOffset() const noexcept
    {
        return storageOffset_;
    }

    [[nodiscard]] vk::DeviceSize size() const noexcept
    {
        return accelerationStructureSize_;
    }

    [[nodiscard]] vk::AccelerationStructureTypeKHR type() const noexcept
    {
        return type_;
    }

    [[nodiscard]] vk::DeviceAddress deviceAddress() const
    {
        nrAssert(valid(), "AccelerationStructureResource::deviceAddress requires a valid handle.");
        nrAssert(device_.has_value(), "AccelerationStructureResource::deviceAddress requires a valid device reference.");
        vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
        addressInfo.accelerationStructure = raw();
        return device_->get().getAccelerationStructureAddressKHR(addressInfo);
    }

  private:
    std::optional<std::reference_wrapper<const vk::raii::Device>> device_;
    std::optional<std::reference_wrapper<const Buffer>> storageBuffer_;
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

} // namespace nr::rhi

namespace nr::rhi::detail
{
struct ValidationResult
{
    bool isValid = false;
    std::string message{};
};

[[nodiscard]] inline ValidationResult validationSuccess()
{
    return ValidationResult{
        .isValid = true,
        .message = {},
    };
}

[[nodiscard]] inline ValidationResult validationFailure(std::string message)
{
    return ValidationResult{
        .isValid = false,
        .message = std::move(message),
    };
}

template <typename... Args>
[[nodiscard]] inline std::string formatMessage(std::string_view format, const Args &...args)
{
    return std::vformat(format, std::make_format_args(args...));
}

[[nodiscard]] inline vk::BuildAccelerationStructureModeKHR toVkBuildMode(AsBuildMode mode)
{
    return mode == AsBuildMode::Update ? vk::BuildAccelerationStructureModeKHR::eUpdate : vk::BuildAccelerationStructureModeKHR::eBuild;
}

[[nodiscard]] inline bool hasBuildFlag(vk::BuildAccelerationStructureFlagsKHR flags, vk::BuildAccelerationStructureFlagBitsKHR bit)
{
    return (flags & bit) != vk::BuildAccelerationStructureFlagsKHR{};
}

[[nodiscard]] inline ValidationResult validateBuildFlagCombination(const AsBuildOptions &options)
{
    if (hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild) &&
        hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace))
    {
        return validationFailure("AsBuildOptions.buildFlags cannot combine PREFER_FAST_BUILD and PREFER_FAST_TRACE.");
    }

    if (options.allowUpdateExpected && !hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate))
    {
        return validationFailure("allowUpdateExpected=true requires VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR.");
    }

    if (options.mode == AsBuildMode::Update && !hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate))
    {
        return validationFailure("Update mode requires VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR in buildFlags.");
    }

    return validationSuccess();
}

[[nodiscard]] inline vk::DeviceSize indexTypeAlignment(vk::IndexType type)
{
    switch (type)
    {
    case vk::IndexType::eUint16:
        return 2;
    case vk::IndexType::eUint32:
        return 4;
    default:
        return 0;
    }
}

} // namespace nr::rhi::detail

export namespace nr::rhi
{

// Query physical-device AS limits needed by the caller to size scratch buffers,
// validate primitive counts, and choose legal alignment.
[[nodiscard]] inline AsBuildLimits queryAsBuildLimits(const vk::raii::PhysicalDevice &physicalDevice)
{
    auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    const auto &asProps = properties2.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

    return AsBuildLimits{
        .minScratchAlignment = static_cast<vk::DeviceSize>(asProps.minAccelerationStructureScratchOffsetAlignment),
        .maxGeometryCount = asProps.maxGeometryCount,
        .maxPrimitiveCount = asProps.maxPrimitiveCount,
        .maxInstanceCount = asProps.maxInstanceCount,
    };
}

} // namespace nr::rhi

namespace nr::rhi::detail
{

[[nodiscard]] inline ValidationResult validateAsBuildInputs(const BlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment)
{
    auto flagDiagnostics = detail::validateBuildFlagCombination(info.options);
    if (!flagDiagnostics.isValid)
        return flagDiagnostics;

    if (!info.dst.valid())
        return validationFailure("BLAS build requires a valid destination acceleration structure.");
    if (info.dst.type() != vk::AccelerationStructureTypeKHR::eBottomLevel)
        return validationFailure("BLAS build destination type must be VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR.");
    if (!info.geometryBuffer.valid())
        return validationFailure("BLAS build requires a valid geometry input buffer.");
    if (!info.scratchBuffer.valid())
        return validationFailure("BLAS build requires a valid scratch buffer.");
    if (info.geometryLayout.vertexStride == 0)
        return validationFailure("BLAS build requires vertexStride > 0.");
    if (info.geometryLayout.maxVertex == 0)
        return validationFailure("BLAS build requires maxVertex > 0.");
    if (info.geometryInput.primitiveCount == 0)
        return validationFailure("BLAS build requires primitiveCount > 0.");
    if (info.geometryInput.vertexAddress == 0)
        return validationFailure("BLAS build requires a non-zero vertex address.");

    if (info.geometryLayout.indexType == vk::IndexType::eNoneKHR)
    {
        if (info.geometryInput.indexAddress != 0)
            return validationFailure("BLAS build requires indexAddress == 0 when indexType is VK_INDEX_TYPE_NONE_KHR.");
    }
    else if (info.geometryInput.indexAddress == 0)
    {
        return validationFailure("BLAS build requires a non-zero index address when indexType is not VK_INDEX_TYPE_NONE_KHR.");
    }

    auto requiredIndexAlignment = detail::indexTypeAlignment(info.geometryLayout.indexType);
    if (requiredIndexAlignment > 0 && (info.geometryInput.indexAddress % requiredIndexAlignment) != 0)
        return validationFailure(formatMessage("BLAS index address must be aligned to {} bytes.", requiredIndexAlignment));

    if (info.geometryInput.transformAddress != 0 && (info.geometryInput.transformAddress % 16) != 0)
        return validationFailure("BLAS transform address must be 16-byte aligned when present.");
    if (info.scratchAddress == 0)
        return validationFailure("BLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("BLAS scratch address must be aligned to {} bytes.", scratchAlignment));
    if ((info.geometryBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) == vk::BufferUsageFlags{})
        return validationFailure("BLAS geometry buffer must include VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");
    if ((info.scratchBuffer.usage() & vk::BufferUsageFlagBits::eStorageBuffer) == vk::BufferUsageFlags{})
        return validationFailure("BLAS scratch buffer must include VK_BUFFER_USAGE_STORAGE_BUFFER_BIT.");
    if (info.options.mode == AsBuildMode::Update)
    {
        if (!info.options.allowUpdateExpected)
            return validationFailure("BLAS update mode requires allowUpdateExpected=true.");
        if (!info.src.has_value() || !info.src->get().valid())
            return validationFailure("BLAS update mode requires a valid source acceleration structure.");
        if (info.src->get().type() != info.dst.type())
            return validationFailure("BLAS update mode requires matching source/destination AS types.");
    }

    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsBuildInputs(const TlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment)
{
    auto flagDiagnostics = detail::validateBuildFlagCombination(info.options);
    if (!flagDiagnostics.isValid)
        return flagDiagnostics;

    if (!info.dst.valid())
        return validationFailure("TLAS build requires a valid destination acceleration structure.");
    if (info.dst.type() != vk::AccelerationStructureTypeKHR::eTopLevel)
        return validationFailure("TLAS build destination type must be VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR.");
    if (!info.instanceBuffer.valid())
        return validationFailure("TLAS build requires a valid instance input buffer.");
    if (!info.scratchBuffer.valid())
        return validationFailure("TLAS build requires a valid scratch buffer.");
    if (info.buildInput.instanceCount == 0)
        return validationFailure("TLAS build requires instanceCount > 0.");
    if (info.buildInput.instancesAddress == 0)
        return validationFailure("TLAS build requires non-zero instances device address.");
    auto instanceAddressAlignment = info.buildInput.arrayOfPointers ? vk::DeviceSize{8} : vk::DeviceSize{16};
    if ((info.buildInput.instancesAddress % instanceAddressAlignment) != 0)
        return validationFailure(formatMessage("TLAS instances address must be aligned to {} bytes.", instanceAddressAlignment));
    if (info.scratchAddress == 0)
        return validationFailure("TLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("TLAS scratch address must be aligned to {} bytes.", scratchAlignment));
    if ((info.instanceBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) == vk::BufferUsageFlags{})
        return validationFailure("TLAS instance buffer must include VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");
    if ((info.scratchBuffer.usage() & vk::BufferUsageFlagBits::eStorageBuffer) == vk::BufferUsageFlags{})
        return validationFailure("TLAS scratch buffer must include VK_BUFFER_USAGE_STORAGE_BUFFER_BIT.");
    if (info.options.mode == AsBuildMode::Update)
    {
        if (!info.options.allowUpdateExpected)
            return validationFailure("TLAS update mode requires allowUpdateExpected=true.");
        if (!info.src.has_value() || !info.src->get().valid())
            return validationFailure("TLAS update mode requires a valid source acceleration structure.");
        if (info.src->get().type() != info.dst.type())
            return validationFailure("TLAS update mode requires matching source/destination AS types.");
    }

    return validationSuccess();
}

} // namespace nr::rhi::detail

export namespace nr::rhi
{

[[nodiscard]] inline AsBuildSizes queryBlasBuildSizes(
    const vk::raii::Device &device,
    const BlasGeometryLayout &layout,
    std::uint32_t primitiveCount,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice)
{
    nrAssert(primitiveCount > 0, "queryBlasBuildSizes requires primitiveCount > 0.");
    nrAssert(layout.vertexStride > 0, "queryBlasBuildSizes requires vertexStride > 0.");
    nrAssert(layout.maxVertex > 0, "queryBlasBuildSizes requires maxVertex > 0.");

    auto flagDiagnostics = detail::validateBuildFlagCombination(options);
    nrAssert(flagDiagnostics.isValid, detail::formatMessage("queryBlasBuildSizes invalid options: {}", flagDiagnostics.message));

    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.vertexFormat = layout.vertexFormat;
    triangles.vertexStride = layout.vertexStride;
    triangles.indexType = layout.indexType;
    triangles.maxVertex = layout.maxVertex;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    geometry.geometry.triangles = triangles;
    geometry.flags = layout.geometryFlags;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = options.buildFlags;
    // vkGetAccelerationStructureBuildSizesKHR ignores mode/src/dst and runtime addresses.
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCounts = std::array{primitiveCount};
    auto sizeInfo = device.getAccelerationStructureBuildSizesKHR(buildType, buildInfo, primitiveCounts);
    return AsBuildSizes{
        .accelerationStructureSize = sizeInfo.accelerationStructureSize,
        .buildScratchSize = sizeInfo.buildScratchSize,
        .updateScratchSize = sizeInfo.updateScratchSize,
    };
}

[[nodiscard]] inline AsBuildSizes queryTlasBuildSizes(
    const vk::raii::Device &device,
    const TlasBuildInput &input,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice)
{
    nrAssert(input.instanceCount > 0, "queryTlasBuildSizes requires instanceCount > 0.");

    auto flagDiagnostics = detail::validateBuildFlagCombination(options);
    nrAssert(flagDiagnostics.isValid, detail::formatMessage("queryTlasBuildSizes invalid options: {}", flagDiagnostics.message));

    vk::AccelerationStructureGeometryInstancesDataKHR instances{};
    instances.arrayOfPointers = input.arrayOfPointers ? vk::True : vk::False;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eInstances;
    geometry.geometry.instances = instances;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    buildInfo.flags = options.buildFlags;
    // vkGetAccelerationStructureBuildSizesKHR ignores mode/src/dst and runtime addresses.
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    auto primitiveCounts = std::array{input.instanceCount};
    auto sizeInfo = device.getAccelerationStructureBuildSizesKHR(buildType, buildInfo, primitiveCounts);
    return AsBuildSizes{
        .accelerationStructureSize = sizeInfo.accelerationStructureSize,
        .buildScratchSize = sizeInfo.buildScratchSize,
        .updateScratchSize = sizeInfo.updateScratchSize,
    };
}

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
inline void recordBuildBlas(const vk::raii::CommandBuffer &commandBuffer, const BlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment = 1)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildBlas requires a valid command buffer.");
    auto diagnostics = detail::validateAsBuildInputs(info, scratchAlignment);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordBuildBlas invalid input: {}", diagnostics.message));

    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.vertexFormat = info.geometryLayout.vertexFormat;
    triangles.vertexData.deviceAddress = info.geometryInput.vertexAddress;
    triangles.vertexStride = info.geometryLayout.vertexStride;
    triangles.maxVertex = info.geometryLayout.maxVertex;
    triangles.indexType = info.geometryLayout.indexType;
    triangles.indexData.deviceAddress = info.geometryInput.indexAddress;
    triangles.transformData.deviceAddress = info.geometryInput.transformAddress;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    geometry.geometry.triangles = triangles;
    geometry.flags = info.geometryLayout.geometryFlags;

    vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = info.geometryInput.primitiveCount;
    rangeInfo.primitiveOffset = info.geometryInput.primitiveOffset;
    rangeInfo.firstVertex = info.geometryInput.firstVertex;
    rangeInfo.transformOffset = 0;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = info.options.buildFlags;
    buildInfo.mode = detail::toVkBuildMode(info.options.mode);
    buildInfo.srcAccelerationStructure = info.src.has_value() ? info.src->get().raw() : vk::AccelerationStructureKHR{};
    buildInfo.dstAccelerationStructure = info.dst.raw();
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = info.scratchAddress;

    auto buildInfos = std::array{buildInfo};
    auto rangeInfos = std::array{rangeInfo};
    auto rangeInfoPtrs = std::array<const vk::AccelerationStructureBuildRangeInfoKHR *, 1>{rangeInfos.data()};
    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrs);
}

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
inline void recordBuildTlas(const vk::raii::CommandBuffer &commandBuffer, const TlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment = 1)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildTlas requires a valid command buffer.");
    auto diagnostics = detail::validateAsBuildInputs(info, scratchAlignment);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordBuildTlas invalid input: {}", diagnostics.message));

    vk::AccelerationStructureGeometryInstancesDataKHR instances{};
    instances.arrayOfPointers = info.buildInput.arrayOfPointers ? vk::True : vk::False;
    instances.data.deviceAddress = info.buildInput.instancesAddress;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eInstances;
    geometry.geometry.instances = instances;

    vk::AccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = info.buildInput.instanceCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eTopLevel;
    buildInfo.flags = info.options.buildFlags;
    buildInfo.mode = detail::toVkBuildMode(info.options.mode);
    buildInfo.srcAccelerationStructure = info.src.has_value() ? info.src->get().raw() : vk::AccelerationStructureKHR{};
    buildInfo.dstAccelerationStructure = info.dst.raw();
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = info.scratchAddress;

    auto buildInfos = std::array{buildInfo};
    auto rangeInfos = std::array{rangeInfo};
    auto rangeInfoPtrs = std::array<const vk::AccelerationStructureBuildRangeInfoKHR *, 1>{rangeInfos.data()};
    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrs);
}

inline void recordUpdateBlas(const vk::raii::CommandBuffer &commandBuffer, BlasBuildRecordInfo info, vk::DeviceSize scratchAlignment = 1)
{
    info.options.mode = AsBuildMode::Update;
    recordBuildBlas(commandBuffer, info, scratchAlignment);
}

inline void recordUpdateTlas(const vk::raii::CommandBuffer &commandBuffer, TlasBuildRecordInfo info, vk::DeviceSize scratchAlignment = 1)
{
    info.options.mode = AsBuildMode::Update;
    recordBuildTlas(commandBuffer, info, scratchAlignment);
}

inline void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &, vk::CopyAccelerationStructureInfoKHR)
{
    nrAssert(false, "recordCopyAccelerationStructure is reserved for a future stage.");
}

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
inline void appendAsSubmitIntent(CommandBatch &batch, const AsSubmitIntent &intent)
{
    if (intent.waitSemaphore)
    {
        batch.addWait(intent.waitSemaphore, intent.waitStageMask, intent.waitValue, 0);
    }

    if (intent.signalSemaphore)
    {
        if (intent.waitSemaphore == intent.signalSemaphore && (intent.waitValue != 0 || intent.signalValue != 0))
        {
            nrAssert(intent.signalValue > intent.waitValue,
                     "appendAsSubmitIntent requires signalValue > waitValue when waiting and signaling the same timeline semaphore in one submit.");
        }
        batch.addSignal(intent.signalSemaphore, intent.signalValue, 0, intent.signalStageMask);
    }
}
} // namespace nr::rhi
