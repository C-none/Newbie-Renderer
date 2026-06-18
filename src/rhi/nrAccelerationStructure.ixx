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

    [[nodiscard]] static AccelerationStructureResource create(
        const vk::raii::Device &device,
        const Buffer &storageBuffer,
        vk::DeviceSize storageOffset,
        vk::DeviceSize accelerationStructureSize,
        vk::AccelerationStructureTypeKHR type,
        std::string_view name = {},
        vk::AccelerationStructureCreateFlagsKHR createFlags = {})
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
        createInfo.createFlags = createFlags;
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

[[nodiscard]] inline std::uint32_t geometryPrimitiveCount(const BlasGeometryRecord &record) noexcept
{
    return record.range.primitiveCount;
}

[[nodiscard]] inline ValidationResult validateTriangleGeometry(const vk::AccelerationStructureGeometryTrianglesDataKHR &triangles, const vk::AccelerationStructureBuildRangeInfoKHR &range)
{
    if (triangles.vertexStride == 0)
        return validationFailure("Triangle BLAS geometry requires vertexStride > 0.");
    if (triangles.maxVertex == 0)
        return validationFailure("Triangle BLAS geometry requires maxVertex > 0.");
    if (triangles.vertexData.deviceAddress == 0)
        return validationFailure("Triangle BLAS geometry requires a non-zero vertex address.");
    if (range.primitiveCount == 0)
        return validationFailure("Triangle BLAS geometry requires primitiveCount > 0.");

    if (triangles.indexType == vk::IndexType::eNoneKHR)
    {
        if (triangles.indexData.deviceAddress != 0)
            return validationFailure("Triangle BLAS geometry requires index address == 0 when indexType is VK_INDEX_TYPE_NONE_KHR.");
    }
    else if (triangles.indexData.deviceAddress == 0)
    {
        return validationFailure("Triangle BLAS geometry requires a non-zero index address when indexType is not VK_INDEX_TYPE_NONE_KHR.");
    }

    auto requiredIndexAlignment = detail::indexTypeAlignment(triangles.indexType);
    if (requiredIndexAlignment > 0 && (triangles.indexData.deviceAddress % requiredIndexAlignment) != 0)
        return validationFailure(formatMessage("Triangle BLAS index address must be aligned to {} bytes.", requiredIndexAlignment));

    if (triangles.transformData.deviceAddress != 0 && (triangles.transformData.deviceAddress % 16) != 0)
        return validationFailure("Triangle BLAS transform address must be 16-byte aligned when present.");

    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAabbGeometry(const vk::AccelerationStructureGeometryAabbsDataKHR &aabbs, const vk::AccelerationStructureBuildRangeInfoKHR &range)
{
    if (aabbs.data.deviceAddress == 0)
        return validationFailure("AABB BLAS geometry requires a non-zero AABB data address.");
    if (aabbs.stride < sizeof(vk::AabbPositionsKHR))
        return validationFailure("AABB BLAS geometry stride must be at least sizeof(VkAabbPositionsKHR).");
    if ((aabbs.stride % 8) != 0)
        return validationFailure("AABB BLAS geometry stride must be 8-byte aligned.");
    if (range.primitiveCount == 0)
        return validationFailure("AABB BLAS geometry requires primitiveCount > 0.");

    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateBlasGeometryRecord(const BlasGeometryRecord &record)
{
    if (!record.geometryBuffer.get().valid())
        return validationFailure("BLAS geometry record requires a valid geometry input buffer.");
    if ((record.geometryBuffer.get().usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) == vk::BufferUsageFlags{})
        return validationFailure("BLAS geometry input buffer must include VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");

    switch (record.geometry.geometryType)
    {
    case vk::GeometryTypeKHR::eTriangles:
        return validateTriangleGeometry(record.geometry.geometry.triangles, record.range);
    case vk::GeometryTypeKHR::eAabbs:
        return validateAabbGeometry(record.geometry.geometry.aabbs, record.range);
    default:
        return validationFailure("BLAS geometry record supports only triangle and AABB geometry.");
    }
}

[[nodiscard]] inline ValidationResult validateBlasGeometryRecords(std::span<const BlasGeometryRecord> geometries)
{
    if (geometries.empty())
        return validationFailure("BLAS build requires at least one geometry record.");

    auto invalidIt = std::ranges::find_if(geometries, [](const BlasGeometryRecord &record) {
        return !validateBlasGeometryRecord(record).isValid;
    });
    if (invalidIt != std::ranges::end(geometries))
        return validateBlasGeometryRecord(*invalidIt);

    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateIndirectBuildCommands(std::span<const AsIndirectBuildCommand> commands)
{
    if (commands.empty())
        return validationFailure("Indirect AS build requires at least one command.");

    auto invalidIt = std::ranges::find_if(commands, [](const AsIndirectBuildCommand &command) {
        const auto &buildInfo = command.buildInfo;
        return buildInfo.dstAccelerationStructure == vk::AccelerationStructureKHR{} ||
               buildInfo.geometryCount == 0 ||
               (buildInfo.pGeometries == nullptr && buildInfo.ppGeometries == nullptr) ||
               (buildInfo.pGeometries != nullptr && buildInfo.ppGeometries != nullptr) ||
               buildInfo.scratchData.deviceAddress == 0 ||
               command.indirectDeviceAddress == 0 ||
               command.indirectStride == 0 ||
               (command.indirectDeviceAddress % 4u) != 0 ||
               (command.indirectStride % 4u) != 0 ||
               command.maxPrimitiveCounts.size() != buildInfo.geometryCount;
    });

    if (invalidIt == std::ranges::end(commands))
        return validationSuccess();

    const auto &buildInfo = invalidIt->buildInfo;
    if (buildInfo.dstAccelerationStructure == vk::AccelerationStructureKHR{})
        return validationFailure("Indirect AS build requires a valid destination acceleration structure handle.");
    if (buildInfo.geometryCount == 0)
        return validationFailure("Indirect AS build requires geometryCount > 0.");
    if (buildInfo.pGeometries == nullptr && buildInfo.ppGeometries == nullptr)
        return validationFailure("Indirect AS build requires pGeometries or ppGeometries.");
    if (buildInfo.pGeometries != nullptr && buildInfo.ppGeometries != nullptr)
        return validationFailure("Indirect AS build must not set both pGeometries and ppGeometries.");
    if (buildInfo.scratchData.deviceAddress == 0)
        return validationFailure("Indirect AS build requires non-zero scratch device address.");
    if (invalidIt->indirectDeviceAddress == 0)
        return validationFailure("Indirect AS build requires a non-zero indirect device address.");
    if (invalidIt->indirectStride == 0)
        return validationFailure("Indirect AS build requires indirectStride > 0.");
    if ((invalidIt->indirectDeviceAddress % 4u) != 0)
        return validationFailure("Indirect AS build indirect device address must be 4-byte aligned.");
    if ((invalidIt->indirectStride % 4u) != 0)
        return validationFailure("Indirect AS build indirect stride must be 4-byte aligned.");
    return validationFailure(formatMessage(
        "Indirect AS build maxPrimitiveCounts count ({}) must match geometryCount ({}).",
        invalidIt->maxPrimitiveCounts.size(),
        buildInfo.geometryCount));
}

[[nodiscard]] inline ValidationResult validateAsCopyInfo(const vk::CopyAccelerationStructureInfoKHR &info)
{
    if (info.src == vk::AccelerationStructureKHR{})
        return validationFailure("AS copy requires a valid source acceleration structure.");
    if (info.dst == vk::AccelerationStructureKHR{})
        return validationFailure("AS copy requires a valid destination acceleration structure.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eClone &&
        info.mode != vk::CopyAccelerationStructureModeKHR::eCompact)
        return validationFailure("AS copy supports only CLONE and COMPACT modes.");
    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsCopyInfo(const AsCopyRecordInfo &info)
{
    if (!info.src.valid())
        return validationFailure("AS copy requires a valid source acceleration structure.");
    if (!info.dst.valid())
        return validationFailure("AS copy requires a valid destination acceleration structure.");
    if (info.src.type() != info.dst.type())
        return validationFailure("AS copy requires matching source/destination acceleration structure types.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eClone &&
        info.mode != vk::CopyAccelerationStructureModeKHR::eCompact)
        return validationFailure("AS copy supports only CLONE and COMPACT modes.");
    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsCopyToMemoryInfo(const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    if (info.src == vk::AccelerationStructureKHR{})
        return validationFailure("AS serialize requires a valid source acceleration structure.");
    if (info.dst.deviceAddress == 0)
        return validationFailure("AS serialize requires a non-zero destination device address.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eSerialize)
        return validationFailure("AS serialize requires SERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsCopyToMemoryInfo(const AsCopyToDeviceMemoryRecordInfo &info)
{
    if (!info.src.valid())
        return validationFailure("AS serialize requires a valid source acceleration structure.");
    if (info.dstAddress == 0)
        return validationFailure("AS serialize requires a non-zero destination device address.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eSerialize)
        return validationFailure("AS serialize requires SERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsCopyFromMemoryInfo(const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    if (info.src.deviceAddress == 0)
        return validationFailure("AS deserialize requires a non-zero source device address.");
    if (info.dst == vk::AccelerationStructureKHR{})
        return validationFailure("AS deserialize requires a valid destination acceleration structure.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eDeserialize)
        return validationFailure("AS deserialize requires DESERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] inline ValidationResult validateAsCopyFromMemoryInfo(const AsCopyFromDeviceMemoryRecordInfo &info)
{
    if (info.srcAddress == 0)
        return validationFailure("AS deserialize requires a non-zero source device address.");
    if (!info.dst.valid())
        return validationFailure("AS deserialize requires a valid destination acceleration structure.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eDeserialize)
        return validationFailure("AS deserialize requires DESERIALIZE mode.");
    return validationSuccess();
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

[[nodiscard]] inline BlasGeometryRecord makeBlasTriangleGeometryRecord(
    const Buffer &geometryBuffer,
    const BlasGeometryLayout &layout,
    const BlasGeometryInput &input)
{
    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.vertexFormat = layout.vertexFormat;
    triangles.vertexData.deviceAddress = input.vertexAddress;
    triangles.vertexStride = layout.vertexStride;
    triangles.maxVertex = layout.maxVertex;
    triangles.indexType = layout.indexType;
    triangles.indexData.deviceAddress = input.indexAddress;
    triangles.transformData.deviceAddress = input.transformAddress;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    geometry.geometry.triangles = triangles;
    geometry.flags = layout.geometryFlags;

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = input.primitiveCount;
    range.primitiveOffset = input.primitiveOffset;
    range.firstVertex = input.firstVertex;

    return BlasGeometryRecord{
        .geometryBuffer = std::cref(geometryBuffer),
        .geometry = geometry,
        .range = range,
    };
}

[[nodiscard]] inline BlasGeometryRecord makeBlasAabbGeometryRecord(
    const Buffer &geometryBuffer,
    const BlasAabbGeometryInput &input)
{
    vk::AccelerationStructureGeometryAabbsDataKHR aabbs{};
    aabbs.data.deviceAddress = input.dataAddress;
    aabbs.stride = input.stride;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eAabbs;
    geometry.geometry.aabbs = aabbs;
    geometry.flags = input.geometryFlags;

    vk::AccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = input.primitiveCount;
    range.primitiveOffset = input.primitiveOffset;

    return BlasGeometryRecord{
        .geometryBuffer = std::cref(geometryBuffer),
        .geometry = geometry,
        .range = range,
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

[[nodiscard]] inline ValidationResult validateAsBuildInputs(const BlasGeometriesBuildRecordInfo &info, vk::DeviceSize scratchAlignment)
{
    auto flagDiagnostics = detail::validateBuildFlagCombination(info.options);
    if (!flagDiagnostics.isValid)
        return flagDiagnostics;

    if (!info.dst.valid())
        return validationFailure("BLAS build requires a valid destination acceleration structure.");
    if (info.dst.type() != vk::AccelerationStructureTypeKHR::eBottomLevel)
        return validationFailure("BLAS build destination type must be VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR.");
    if (!info.scratchBuffer.valid())
        return validationFailure("BLAS build requires a valid scratch buffer.");
    if ((info.scratchBuffer.usage() & vk::BufferUsageFlagBits::eStorageBuffer) == vk::BufferUsageFlags{})
        return validationFailure("BLAS scratch buffer must include VK_BUFFER_USAGE_STORAGE_BUFFER_BIT.");
    if (info.scratchAddress == 0)
        return validationFailure("BLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("BLAS scratch address must be aligned to {} bytes.", scratchAlignment));

    auto geometryDiagnostics = detail::validateBlasGeometryRecords(info.geometries);
    if (!geometryDiagnostics.isValid)
        return geometryDiagnostics;

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

[[nodiscard]] inline AsBuildSizes queryAccelerationStructureBuildSizes(
    const vk::raii::Device &device,
    vk::AccelerationStructureTypeKHR type,
    std::span<const vk::AccelerationStructureGeometryKHR> geometries,
    std::span<const std::uint32_t> maxPrimitiveCounts,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice)
{
    nrAssert(!geometries.empty(), "queryAccelerationStructureBuildSizes requires at least one geometry.");
    nrAssert(geometries.size() == maxPrimitiveCounts.size(), "queryAccelerationStructureBuildSizes requires one max primitive count per geometry.");

    auto flagDiagnostics = detail::validateBuildFlagCombination(options);
    nrAssert(flagDiagnostics.isValid, detail::formatMessage("queryAccelerationStructureBuildSizes invalid options: {}", flagDiagnostics.message));

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = type;
    buildInfo.flags = options.buildFlags;
    // vkGetAccelerationStructureBuildSizesKHR ignores mode/src/dst and runtime addresses.
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.geometryCount = static_cast<std::uint32_t>(geometries.size());
    buildInfo.pGeometries = geometries.data();

    auto sizeInfo = device.getAccelerationStructureBuildSizesKHR(buildType, buildInfo, maxPrimitiveCounts);
    return AsBuildSizes{
        .accelerationStructureSize = sizeInfo.accelerationStructureSize,
        .buildScratchSize = sizeInfo.buildScratchSize,
        .updateScratchSize = sizeInfo.updateScratchSize,
    };
}

[[nodiscard]] inline AsBuildSizes queryBlasBuildSizes(
    const vk::raii::Device &device,
    const BlasGeometryLayout &layout,
    std::uint32_t primitiveCount,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice)
{
    nrAssert(layout.vertexStride > 0, "queryBlasBuildSizes requires vertexStride > 0.");
    nrAssert(layout.maxVertex > 0, "queryBlasBuildSizes requires maxVertex > 0.");
    nrAssert(primitiveCount > 0, "queryBlasBuildSizes requires primitiveCount > 0.");

    vk::AccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.vertexFormat = layout.vertexFormat;
    triangles.vertexStride = layout.vertexStride;
    triangles.indexType = layout.indexType;
    triangles.maxVertex = layout.maxVertex;

    vk::AccelerationStructureGeometryKHR geometry{};
    geometry.geometryType = vk::GeometryTypeKHR::eTriangles;
    geometry.geometry.triangles = triangles;
    geometry.flags = layout.geometryFlags;

    auto primitiveCounts = std::array{primitiveCount};
    return queryAccelerationStructureBuildSizes(
        device,
        vk::AccelerationStructureTypeKHR::eBottomLevel,
        std::span<const vk::AccelerationStructureGeometryKHR>{&geometry, 1},
        std::span<const std::uint32_t>{primitiveCounts},
        options,
        buildType);
}

[[nodiscard]] inline AsBuildSizes queryBlasBuildSizes(
    const vk::raii::Device &device,
    std::span<const BlasGeometryRecord> geometries,
    const AsBuildOptions &options = {},
    vk::AccelerationStructureBuildTypeKHR buildType = vk::AccelerationStructureBuildTypeKHR::eDevice)
{
    auto geometryDiagnostics = detail::validateBlasGeometryRecords(geometries);
    nrAssert(geometryDiagnostics.isValid, detail::formatMessage("queryBlasBuildSizes invalid geometry: {}", geometryDiagnostics.message));

    auto vkGeometries = geometries |
                        std::views::transform([](const BlasGeometryRecord &record) { return record.geometry; }) |
                        std::ranges::to<std::vector>();
    auto primitiveCounts = geometries |
                           std::views::transform([](const BlasGeometryRecord &record) { return detail::geometryPrimitiveCount(record); }) |
                           std::ranges::to<std::vector>();

    return queryAccelerationStructureBuildSizes(
        device,
        vk::AccelerationStructureTypeKHR::eBottomLevel,
        std::span<const vk::AccelerationStructureGeometryKHR>{vkGeometries},
        std::span<const std::uint32_t>{primitiveCounts},
        options,
        buildType);
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
inline void recordBuildBlasGeometries(const vk::raii::CommandBuffer &commandBuffer, const BlasGeometriesBuildRecordInfo &info, vk::DeviceSize scratchAlignment = 1)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildBlasGeometries requires a valid command buffer.");
    auto diagnostics = detail::validateAsBuildInputs(info, scratchAlignment);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordBuildBlasGeometries invalid input: {}", diagnostics.message));

    auto geometries = info.geometries |
                      std::views::transform([](const BlasGeometryRecord &record) { return record.geometry; }) |
                      std::ranges::to<std::vector>();
    auto rangeInfos = info.geometries |
                      std::views::transform([](const BlasGeometryRecord &record) { return record.range; }) |
                      std::ranges::to<std::vector>();

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = info.options.buildFlags;
    buildInfo.mode = detail::toVkBuildMode(info.options.mode);
    buildInfo.srcAccelerationStructure = info.src.has_value() ? info.src->get().raw() : vk::AccelerationStructureKHR{};
    buildInfo.dstAccelerationStructure = info.dst.raw();
    buildInfo.geometryCount = static_cast<std::uint32_t>(geometries.size());
    buildInfo.pGeometries = geometries.data();
    buildInfo.scratchData.deviceAddress = info.scratchAddress;

    auto buildInfos = std::array{buildInfo};
    auto rangeInfoPtrGroups = std::array<const vk::AccelerationStructureBuildRangeInfoKHR *, 1>{rangeInfos.data()};
    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrGroups);
}

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

inline void recordBuildAccelerationStructuresIndirect(const vk::raii::CommandBuffer &commandBuffer, std::span<const AsIndirectBuildCommand> commands)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildAccelerationStructuresIndirect requires a valid command buffer.");
    auto diagnostics = detail::validateIndirectBuildCommands(commands);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordBuildAccelerationStructuresIndirect invalid input: {}", diagnostics.message));

    auto buildInfos = commands |
                      std::views::transform([](const AsIndirectBuildCommand &command) { return command.buildInfo; }) |
                      std::ranges::to<std::vector>();
    auto indirectDeviceAddresses = commands |
                                   std::views::transform([](const AsIndirectBuildCommand &command) { return command.indirectDeviceAddress; }) |
                                   std::ranges::to<std::vector<vk::DeviceAddress>>();
    auto indirectStrides = commands |
                           std::views::transform([](const AsIndirectBuildCommand &command) { return command.indirectStride; }) |
                           std::ranges::to<std::vector<std::uint32_t>>();
    auto maxPrimitiveCountPtrs = commands |
                                 std::views::transform([](const AsIndirectBuildCommand &command) { return command.maxPrimitiveCounts.data(); }) |
                                 std::ranges::to<std::vector<const std::uint32_t *>>();

    commandBuffer.buildAccelerationStructuresIndirectKHR(buildInfos, indirectDeviceAddresses, indirectStrides, maxPrimitiveCountPtrs);
}

[[nodiscard]] inline vk::CopyAccelerationStructureInfoKHR makeCopyAccelerationStructureInfo(const AsCopyRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyInfo(record);
    nrAssert(diagnostics.isValid, detail::formatMessage("makeCopyAccelerationStructureInfo invalid input: {}", diagnostics.message));

    vk::CopyAccelerationStructureInfoKHR info{};
    info.src = record.src.raw();
    info.dst = record.dst.raw();
    info.mode = record.mode;
    return info;
}

[[nodiscard]] inline vk::CopyAccelerationStructureToMemoryInfoKHR makeCopyAccelerationStructureToMemoryInfo(const AsCopyToDeviceMemoryRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyToMemoryInfo(record);
    nrAssert(diagnostics.isValid, detail::formatMessage("makeCopyAccelerationStructureToMemoryInfo invalid input: {}", diagnostics.message));

    vk::CopyAccelerationStructureToMemoryInfoKHR info{};
    info.src = record.src.raw();
    info.dst.deviceAddress = record.dstAddress;
    info.mode = record.mode;
    return info;
}

[[nodiscard]] inline vk::CopyMemoryToAccelerationStructureInfoKHR makeCopyMemoryToAccelerationStructureInfo(const AsCopyFromDeviceMemoryRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(record);
    nrAssert(diagnostics.isValid, detail::formatMessage("makeCopyMemoryToAccelerationStructureInfo invalid input: {}", diagnostics.message));

    vk::CopyMemoryToAccelerationStructureInfoKHR info{};
    info.src.deviceAddress = record.srcAddress;
    info.dst = record.dst.raw();
    info.mode = record.mode;
    return info;
}

inline void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const vk::CopyAccelerationStructureInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyAccelerationStructure requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordCopyAccelerationStructure invalid input: {}", diagnostics.message));

    commandBuffer.copyAccelerationStructureKHR(info);
}

inline void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const AsCopyRecordInfo &record)
{
    recordCopyAccelerationStructure(commandBuffer, makeCopyAccelerationStructureInfo(record));
}

inline void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer, const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyAccelerationStructureToMemory requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyToMemoryInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordCopyAccelerationStructureToMemory invalid input: {}", diagnostics.message));

    commandBuffer.copyAccelerationStructureToMemoryKHR(info);
}

inline void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer, const AsCopyToDeviceMemoryRecordInfo &record)
{
    recordCopyAccelerationStructureToMemory(commandBuffer, makeCopyAccelerationStructureToMemoryInfo(record));
}

inline void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyMemoryToAccelerationStructure requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordCopyMemoryToAccelerationStructure invalid input: {}", diagnostics.message));

    commandBuffer.copyMemoryToAccelerationStructureKHR(info);
}

inline void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const AsCopyFromDeviceMemoryRecordInfo &record)
{
    recordCopyMemoryToAccelerationStructure(commandBuffer, makeCopyMemoryToAccelerationStructureInfo(record));
}

[[nodiscard]] inline vk::Result copyAccelerationStructure(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const vk::CopyAccelerationStructureInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyAccelerationStructure requires a valid device.");
    auto diagnostics = detail::validateAsCopyInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("copyAccelerationStructure invalid input: {}", diagnostics.message));

    return device.copyAccelerationStructureKHR(deferredOperation, info);
}

[[nodiscard]] inline vk::Result copyAccelerationStructure(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const AsCopyRecordInfo &record)
{
    return copyAccelerationStructure(device, deferredOperation, makeCopyAccelerationStructureInfo(record));
}

[[nodiscard]] inline vk::Result copyAccelerationStructureToMemory(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyAccelerationStructureToMemory requires a valid device.");
    auto diagnostics = detail::validateAsCopyToMemoryInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("copyAccelerationStructureToMemory invalid input: {}", diagnostics.message));

    return device.copyAccelerationStructureToMemoryKHR(deferredOperation, info);
}

[[nodiscard]] inline vk::Result copyAccelerationStructureToMemory(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const AsCopyToDeviceMemoryRecordInfo &record)
{
    return copyAccelerationStructureToMemory(device, deferredOperation, makeCopyAccelerationStructureToMemoryInfo(record));
}

[[nodiscard]] inline vk::Result copyMemoryToAccelerationStructure(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyMemoryToAccelerationStructure requires a valid device.");
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(info);
    nrAssert(diagnostics.isValid, detail::formatMessage("copyMemoryToAccelerationStructure invalid input: {}", diagnostics.message));

    return device.copyMemoryToAccelerationStructureKHR(deferredOperation, info);
}

[[nodiscard]] inline vk::Result copyMemoryToAccelerationStructure(
    const vk::raii::Device &device,
    vk::DeferredOperationKHR deferredOperation,
    const AsCopyFromDeviceMemoryRecordInfo &record)
{
    return copyMemoryToAccelerationStructure(device, deferredOperation, makeCopyMemoryToAccelerationStructureInfo(record));
}

[[nodiscard]] inline vk::raii::DeferredOperationKHR createDeferredOperation(const vk::raii::Device &device)
{
    nrAssert(*device != nullptr, "createDeferredOperation requires a valid device.");
    return device.createDeferredOperationKHR();
}

template <typename T>
[[nodiscard]] inline std::vector<T> queryAccelerationStructureProperties(
    const vk::raii::Device &device,
    std::span<const vk::AccelerationStructureKHR> accelerationStructures,
    vk::QueryType queryType)
{
    nrAssert(*device != nullptr, "queryAccelerationStructureProperties requires a valid device.");
    nrAssert(!accelerationStructures.empty(), "queryAccelerationStructureProperties requires at least one acceleration structure.");
    nrAssert(std::ranges::none_of(accelerationStructures, [](vk::AccelerationStructureKHR handle) { return handle == vk::AccelerationStructureKHR{}; }),
             "queryAccelerationStructureProperties requires valid acceleration structure handles.");

    return device.writeAccelerationStructuresPropertiesKHR<T>(
        accelerationStructures,
        queryType,
        sizeof(T) * accelerationStructures.size(),
        sizeof(T));
}

template <typename T>
[[nodiscard]] inline T queryAccelerationStructureProperty(
    const vk::raii::Device &device,
    const AccelerationStructureResource &accelerationStructure,
    vk::QueryType queryType)
{
    nrAssert(accelerationStructure.valid(), "queryAccelerationStructureProperty requires a valid acceleration structure.");
    auto handles = std::array{accelerationStructure.raw()};
    auto values = queryAccelerationStructureProperties<T>(
        device,
        std::span<const vk::AccelerationStructureKHR>{handles},
        queryType);
    nrAssert(values.size() == 1, "queryAccelerationStructureProperty expected one result.");
    return values.front();
}

[[nodiscard]] inline vk::DeviceSize queryAccelerationStructureCompactedSize(
    const vk::raii::Device &device,
    const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(
        device,
        accelerationStructure,
        vk::QueryType::eAccelerationStructureCompactedSizeKHR);
}

[[nodiscard]] inline vk::DeviceSize queryAccelerationStructureSerializationSize(
    const vk::raii::Device &device,
    const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(
        device,
        accelerationStructure,
        vk::QueryType::eAccelerationStructureSerializationSizeKHR);
}

[[nodiscard]] inline vk::DeviceSize queryAccelerationStructureDeviceTimelineSize(
    const vk::raii::Device &device,
    const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(
        device,
        accelerationStructure,
        vk::QueryType::eAccelerationStructureSizeKHR);
}

[[nodiscard]] inline std::uint64_t queryAccelerationStructureSerializationBottomLevelPointerCount(
    const vk::raii::Device &device,
    const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<std::uint64_t>(
        device,
        accelerationStructure,
        vk::QueryType::eAccelerationStructureSerializationBottomLevelPointersKHR);
}

inline void recordWriteAccelerationStructureProperties(
    const vk::raii::CommandBuffer &commandBuffer,
    std::span<const vk::AccelerationStructureKHR> accelerationStructures,
    vk::QueryType queryType,
    vk::QueryPool queryPool,
    std::uint32_t firstQuery)
{
    nrAssert(*commandBuffer != nullptr, "recordWriteAccelerationStructureProperties requires a valid command buffer.");
    nrAssert(queryPool != vk::QueryPool{}, "recordWriteAccelerationStructureProperties requires a valid query pool.");
    nrAssert(!accelerationStructures.empty(), "recordWriteAccelerationStructureProperties requires at least one acceleration structure.");
    nrAssert(std::ranges::none_of(accelerationStructures, [](vk::AccelerationStructureKHR handle) { return handle == vk::AccelerationStructureKHR{}; }),
             "recordWriteAccelerationStructureProperties requires valid acceleration structure handles.");

    commandBuffer.writeAccelerationStructuresPropertiesKHR(accelerationStructures, queryType, queryPool, firstQuery);
}

inline void recordWriteAccelerationStructureProperty(
    const vk::raii::CommandBuffer &commandBuffer,
    const AccelerationStructureResource &accelerationStructure,
    vk::QueryType queryType,
    vk::QueryPool queryPool,
    std::uint32_t firstQuery)
{
    nrAssert(accelerationStructure.valid(), "recordWriteAccelerationStructureProperty requires a valid acceleration structure.");
    auto handles = std::array{accelerationStructure.raw()};
    recordWriteAccelerationStructureProperties(
        commandBuffer,
        std::span<const vk::AccelerationStructureKHR>{handles},
        queryType,
        queryPool,
        firstQuery);
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
