module nr.rhi;
import :accelerationStructure;
import dependency.vulkan;
import nr.utils;
import :type;
import :resource;
import std;

namespace nr::rhi
{
[[nodiscard]] AccelerationStructureResource AccelerationStructureResource::create(
    const vk::raii::Device &device, const Buffer &storageBuffer, vk::DeviceSize storageOffset,
    vk::DeviceSize accelerationStructureSize, vk::AccelerationStructureTypeKHR type, std::string_view name,
    vk::AccelerationStructureCreateFlagsKHR createFlags)
{
    nrAssert(storageBuffer.valid(), "AccelerationStructureResource::create requires a valid storage buffer.");
    nrAssert(accelerationStructureSize > 0,
             "AccelerationStructureResource::create requires accelerationStructureSize > 0.");
    nrAssert((storageBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR) !=
                 vk::BufferUsageFlags{},
             "AccelerationStructureResource::create requires VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR.");
    nrAssert((storageBuffer.usage() & vk::BufferUsageFlagBits::eShaderDeviceAddress) != vk::BufferUsageFlags{},
             "AccelerationStructureResource::create requires VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT.");
    nrAssert((storageOffset % 256u) == 0u,
             "AccelerationStructureResource::create requires storageOffset to be 256-byte aligned.");
    nrAssert(storageOffset <= storageBuffer.size() &&
                 accelerationStructureSize <= (storageBuffer.size() - storageOffset),
             "AccelerationStructureResource::create requires storageOffset + accelerationStructureSize to fit inside "
             "storage buffer size.");

    AccelerationStructureResource result;
    result.device_ = std::cref(device);
    result.storageBufferHandle_ = storageBuffer.handle();
    result.storageSharingMode_ = storageBuffer.sharingMode();
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

    if constexpr (gpuDebugNamesEnabled)
    {
        if (!result.name_.empty())
        {
            vk::DebugUtilsObjectNameInfoEXT objectNameInfo{};
            objectNameInfo.objectType = vk::ObjectType::eAccelerationStructureKHR;
            const auto rawHandle = static_cast<VkAccelerationStructureKHR>(*result.handle_);
            static_assert(sizeof(rawHandle) == sizeof(std::uint64_t),
                          "VkAccelerationStructureKHR handle size must match std::uint64_t for debug naming.");
            objectNameInfo.objectHandle = std::bit_cast<std::uint64_t>(rawHandle);
            objectNameInfo.pObjectName = result.name_.c_str();
            try
            {
                result.device_->get().setDebugUtilsObjectNameEXT(objectNameInfo);
            }
            catch (const vk::SystemError &error)
            {
                auto errorText = std::string_view{error.what()};
                nrInfo<LogLevel::error>(
                    std::vformat("AccelerationStructureResource::create failed to set debug name '{}': {}",
                                 std::make_format_args(result.name_, errorText)));
                nrAssert(false, "AccelerationStructureResource::create failed to set a Vulkan debug object name.");
            }
        }
    }

    return result;
}

[[nodiscard]] bool AccelerationStructureResource::valid() const noexcept
{
    return *handle_ != nullptr;
}

[[nodiscard]] vk::AccelerationStructureKHR AccelerationStructureResource::raw() const noexcept
{
    return valid() ? *handle_ : vk::AccelerationStructureKHR{};
}

[[nodiscard]] vk::Buffer AccelerationStructureResource::storageBufferHandle() const noexcept
{
    return storageBufferHandle_;
}

[[nodiscard]] vk::SharingMode AccelerationStructureResource::storageSharingMode() const noexcept
{
    return storageSharingMode_;
}

[[nodiscard]] vk::DeviceSize AccelerationStructureResource::storageOffset() const noexcept
{
    return storageOffset_;
}

[[nodiscard]] vk::DeviceSize AccelerationStructureResource::size() const noexcept
{
    return accelerationStructureSize_;
}

[[nodiscard]] vk::AccelerationStructureTypeKHR AccelerationStructureResource::type() const noexcept
{
    return type_;
}

[[nodiscard]] vk::DeviceAddress AccelerationStructureResource::deviceAddress() const
{
    nrAssert(valid(), "AccelerationStructureResource::deviceAddress requires a valid handle.");
    nrAssert(device_.has_value(), "AccelerationStructureResource::deviceAddress requires a valid device reference.");
    vk::AccelerationStructureDeviceAddressInfoKHR addressInfo{};
    addressInfo.accelerationStructure = raw();
    auto const address = device_->get().getAccelerationStructureAddressKHR(addressInfo);
    nrAssert(address != 0u, "AccelerationStructureResource::deviceAddress received a null Vulkan device address.");
    return address;
}

[[nodiscard]] AsBuildLimits queryAsBuildLimits(const vk::raii::PhysicalDevice &physicalDevice)
{
    auto properties2 =
        physicalDevice
            .getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();
    const auto &asProps = properties2.get<vk::PhysicalDeviceAccelerationStructurePropertiesKHR>();

    return AsBuildLimits{
        .minScratchAlignment = static_cast<vk::DeviceSize>(asProps.minAccelerationStructureScratchOffsetAlignment),
        .maxGeometryCount = asProps.maxGeometryCount,
        .maxPrimitiveCount = asProps.maxPrimitiveCount,
        .maxInstanceCount = asProps.maxInstanceCount,
    };
}

[[nodiscard]] BlasGeometryRecord makeBlasTriangleGeometryRecord(const BlasTriangleGeometryBuffers &buffers,
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
        .dataBuffer = buffers.vertex,
        .indexBuffer = buffers.index,
        .transformBuffer = buffers.transform,
        .geometry = geometry,
        .range = range,
    };
}

[[nodiscard]] BlasGeometryRecord makeBlasAabbGeometryRecord(const Buffer &geometryBuffer,
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
        .dataBuffer = std::cref(geometryBuffer),
        .geometry = geometry,
        .range = range,
    };
}

[[nodiscard]] AsBuildSizes queryAccelerationStructureBuildSizes(
    const vk::raii::Device &device, vk::AccelerationStructureTypeKHR type,
    std::span<const vk::AccelerationStructureGeometryKHR> geometries, std::span<const std::uint32_t> maxPrimitiveCounts,
    const AsBuildOptions &options, vk::AccelerationStructureBuildTypeKHR buildType)
{
    nrAssert(!geometries.empty(), "queryAccelerationStructureBuildSizes requires at least one geometry.");
    nrAssert(geometries.size() == maxPrimitiveCounts.size(),
             "queryAccelerationStructureBuildSizes requires one max primitive count per geometry.");

    auto flagDiagnostics = detail::validateBuildFlagCombination(options);
    nrAssert(flagDiagnostics.isValid, detail::formatMessage("queryAccelerationStructureBuildSizes invalid options: {}",
                                                            flagDiagnostics.message));

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
    };
}

[[nodiscard]] AsBuildSizes queryBlasBuildSizes(const vk::raii::Device &device, const BlasGeometryLayout &layout,
                                               std::uint32_t primitiveCount, const AsBuildOptions &options,
                                               vk::AccelerationStructureBuildTypeKHR buildType)
{
    nrAssert(layout.vertexStride > 0, "queryBlasBuildSizes requires vertexStride > 0.");
    nrAssert(primitiveCount > 0, "queryBlasBuildSizes requires primitiveCount > 0.");
    if (layout.indexType == vk::IndexType::eNoneKHR)
    {
        auto const lastVertex = static_cast<std::uint64_t>(primitiveCount) * 3u - 1u;
        nrAssert(lastVertex <= layout.maxVertex,
                 "queryBlasBuildSizes non-indexed geometry requires maxVertex to cover every consumed vertex.");
    }

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
    return queryAccelerationStructureBuildSizes(device, vk::AccelerationStructureTypeKHR::eBottomLevel,
                                                std::span<const vk::AccelerationStructureGeometryKHR>{&geometry, 1},
                                                std::span<const std::uint32_t>{primitiveCounts}, options, buildType);
}

[[nodiscard]] AsBuildSizes queryBlasBuildSizes(const vk::raii::Device &device,
                                               std::span<const BlasGeometryRecord> geometries,
                                               const AsBuildOptions &options,
                                               vk::AccelerationStructureBuildTypeKHR buildType)
{
    auto geometryDiagnostics = detail::validateBlasGeometryRecords(geometries);
    nrAssert(geometryDiagnostics.isValid,
             detail::formatMessage("queryBlasBuildSizes invalid geometry: {}", geometryDiagnostics.message));

    auto vkGeometries = geometries |
                        std::views::transform([](const BlasGeometryRecord &record) { return record.geometry; }) |
                        std::ranges::to<std::vector>();
    auto primitiveCounts =
        geometries |
        std::views::transform([](const BlasGeometryRecord &record) { return detail::geometryPrimitiveCount(record); }) |
        std::ranges::to<std::vector>();

    return queryAccelerationStructureBuildSizes(device, vk::AccelerationStructureTypeKHR::eBottomLevel,
                                                std::span<const vk::AccelerationStructureGeometryKHR>{vkGeometries},
                                                std::span<const std::uint32_t>{primitiveCounts}, options, buildType);
}

[[nodiscard]] AsBuildSizes queryTlasBuildSizes(const vk::raii::Device &device, const TlasBuildInput &input,
                                               const AsBuildOptions &options,
                                               vk::AccelerationStructureBuildTypeKHR buildType)
{
    nrAssert(input.instanceCount > 0, "queryTlasBuildSizes requires instanceCount > 0.");

    auto flagDiagnostics = detail::validateBuildFlagCombination(options);
    nrAssert(flagDiagnostics.isValid,
             detail::formatMessage("queryTlasBuildSizes invalid options: {}", flagDiagnostics.message));

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
    };
}

namespace
{
struct DeviceAddressRange
{
    vk::Buffer buffer{};
    vk::DeviceSize begin = 0;
    vk::DeviceSize size = 0;
};

[[nodiscard]] bool rangeEnd(vk::DeviceSize begin, vk::DeviceSize size, vk::DeviceSize &end) noexcept
{
    if (size > std::numeric_limits<vk::DeviceSize>::max() - begin)
    {
        return false;
    }
    end = begin + size;
    return true;
}

[[nodiscard]] bool rangesOverlap(const DeviceAddressRange &lhs, const DeviceAddressRange &rhs) noexcept
{
    if (lhs.buffer != rhs.buffer)
    {
        return false;
    }

    auto lhsEnd = vk::DeviceSize{};
    auto rhsEnd = vk::DeviceSize{};
    if (!rangeEnd(lhs.begin, lhs.size, lhsEnd) || !rangeEnd(rhs.begin, rhs.size, rhsEnd))
    {
        return true;
    }

    return lhs.begin < rhsEnd && rhs.begin < lhsEnd;
}

[[nodiscard]] nr::rhi::detail::ValidationResult validateBuildRangeAliasFree(
    std::span<const BlasBatchBuildRecordInfo> records, vk::DeviceSize scratchAlignment)
{
    if (records.empty())
    {
        return nr::rhi::detail::validationFailure("BLAS batch build requires at least one record.");
    }

    auto invalidIt = std::ranges::find_if(records, [&](const BlasBatchBuildRecordInfo &record) {
        auto diagnostics = nr::rhi::detail::validateAsBuildInputs(record.build, scratchAlignment);
        return !diagnostics.isValid || record.scratchSize == 0u;
    });
    if (invalidIt != records.end())
    {
        auto diagnostics = nr::rhi::detail::validateAsBuildInputs(invalidIt->build, scratchAlignment);
        if (!diagnostics.isValid)
        {
            return diagnostics;
        }
        return nr::rhi::detail::validationFailure("BLAS batch build requires scratchSize > 0 for every record.");
    }

    struct BuildRanges
    {
        DeviceAddressRange scratch{};
        DeviceAddressRange destination{};
    };

    auto ranges = std::vector<BuildRanges>{};
    ranges.reserve(records.size());
    auto message = std::optional<std::string>{};
    auto const indices = std::views::iota(std::size_t{0}, records.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (message.has_value())
        {
            return;
        }

        auto const &record = records[index];
        auto const scratchBase = record.build.scratchBuffer.deviceAddress();
        if (scratchBase == 0u || record.build.scratchAddress < scratchBase)
        {
            message = std::format("BLAS batch record {} scratch address is outside its declared buffer.", index);
            return;
        }
        auto const scratchOffset = record.build.scratchAddress - scratchBase;
        if (scratchOffset > record.build.scratchBuffer.size() ||
            record.scratchSize > record.build.scratchBuffer.size() - scratchOffset)
        {
            message = std::format("BLAS batch record {} scratch range exceeds its declared buffer.", index);
            return;
        }

        ranges.push_back(BuildRanges{
            .scratch = DeviceAddressRange{
                .buffer = record.build.scratchBuffer.handle(),
                .begin = scratchOffset,
                .size = record.scratchSize,
            },
            .destination = DeviceAddressRange{
                .buffer = record.build.dst.storageBufferHandle(),
                .begin = record.build.dst.storageOffset(),
                .size = record.build.dst.size(),
            },
        });
    });

    if (message.has_value())
    {
        return nr::rhi::detail::validationFailure(std::move(*message));
    }

    std::ranges::for_each(indices, [&](std::size_t lhsIndex) {
        if (message.has_value())
        {
            return;
        }

        auto const &lhs = ranges[lhsIndex];
        if (rangesOverlap(lhs.destination, lhs.scratch))
        {
            message = std::format("BLAS batch record {} aliases destination storage with scratch storage.", lhsIndex);
            return;
        }
        auto const rhsIndices = indices | std::views::drop(lhsIndex + 1u);
        std::ranges::for_each(rhsIndices, [&](std::size_t rhsIndex) {
            if (message.has_value())
            {
                return;
            }

            auto const &rhs = ranges[rhsIndex];
            if (rangesOverlap(lhs.scratch, rhs.scratch))
            {
                message =
                    std::format("BLAS batch records {} and {} use overlapping scratch ranges.", lhsIndex, rhsIndex);
            }
            else if (rangesOverlap(lhs.destination, rhs.destination))
            {
                message = std::format("BLAS batch records {} and {} use overlapping destination AS storage ranges.",
                                      lhsIndex, rhsIndex);
            }
            else if (rangesOverlap(lhs.destination, rhs.scratch) ||
                     rangesOverlap(rhs.destination, lhs.scratch))
            {
                message = std::format("BLAS batch records {} and {} alias AS destination and scratch storage.",
                                      lhsIndex, rhsIndex);
            }
        });
    });

    if (message.has_value())
    {
        return nr::rhi::detail::validationFailure(std::move(*message));
    }

    return nr::rhi::detail::validationSuccess();
}
} // namespace

void recordBuildBlasGeometries(const vk::raii::CommandBuffer &commandBuffer, const BlasGeometriesBuildRecordInfo &info,
                               vk::DeviceSize scratchAlignment)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildBlasGeometries requires a valid command buffer.");
    auto diagnostics = detail::validateAsBuildInputs(info, scratchAlignment);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("recordBuildBlasGeometries invalid input: {}", diagnostics.message));

    auto geometries = info.geometries |
                      std::views::transform([](const BlasGeometryRecord &record) { return record.geometry; }) |
                      std::ranges::to<std::vector>();
    auto rangeInfos = info.geometries |
                      std::views::transform([](const BlasGeometryRecord &record) { return record.range; }) |
                      std::ranges::to<std::vector>();

    vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
    buildInfo.flags = info.options.buildFlags;
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.dstAccelerationStructure = info.dst.raw();
    buildInfo.geometryCount = static_cast<std::uint32_t>(geometries.size());
    buildInfo.pGeometries = geometries.data();
    buildInfo.scratchData.deviceAddress = info.scratchAddress;

    auto buildInfos = std::array{buildInfo};
    auto rangeInfoPtrGroups = std::array<const vk::AccelerationStructureBuildRangeInfoKHR *, 1>{rangeInfos.data()};
    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrGroups);
}

void recordBuildBlasBatch(const vk::raii::CommandBuffer &commandBuffer,
                          std::span<const BlasBatchBuildRecordInfo> records, vk::DeviceSize scratchAlignment)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildBlasBatch requires a valid command buffer.");
    auto diagnostics = validateBuildRangeAliasFree(records, scratchAlignment);
    nrAssert(diagnostics.isValid, detail::formatMessage("recordBuildBlasBatch invalid input: {}", diagnostics.message));

    auto geometryGroups = std::vector<std::vector<vk::AccelerationStructureGeometryKHR>>{};
    auto rangeInfoGroups = std::vector<std::vector<vk::AccelerationStructureBuildRangeInfoKHR>>{};
    auto buildInfos = std::vector<vk::AccelerationStructureBuildGeometryInfoKHR>{};
    auto rangeInfoPtrGroups = std::vector<const vk::AccelerationStructureBuildRangeInfoKHR *>{};

    geometryGroups.reserve(records.size());
    rangeInfoGroups.reserve(records.size());
    buildInfos.reserve(records.size());
    rangeInfoPtrGroups.reserve(records.size());

    std::ranges::for_each(records, [&](const BlasBatchBuildRecordInfo &record) {
        auto &geometries = geometryGroups.emplace_back(
            record.build.geometries |
            std::views::transform([](const BlasGeometryRecord &geometryRecord) { return geometryRecord.geometry; }) |
            std::ranges::to<std::vector>());
        auto &rangeInfos = rangeInfoGroups.emplace_back(
            record.build.geometries |
            std::views::transform([](const BlasGeometryRecord &geometryRecord) { return geometryRecord.range; }) |
            std::ranges::to<std::vector>());

        vk::AccelerationStructureBuildGeometryInfoKHR buildInfo{};
        buildInfo.type = vk::AccelerationStructureTypeKHR::eBottomLevel;
        buildInfo.flags = record.build.options.buildFlags;
        buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
        buildInfo.dstAccelerationStructure = record.build.dst.raw();
        buildInfo.geometryCount = static_cast<std::uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();
        buildInfo.scratchData.deviceAddress = record.build.scratchAddress;

        buildInfos.push_back(buildInfo);
        rangeInfoPtrGroups.push_back(rangeInfos.data());
    });

    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrGroups);
}

void recordBuildTlas(const vk::raii::CommandBuffer &commandBuffer, const TlasBuildRecordInfo &info,
                     vk::DeviceSize scratchAlignment)
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
    buildInfo.mode = vk::BuildAccelerationStructureModeKHR::eBuild;
    buildInfo.dstAccelerationStructure = info.dst.raw();
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.scratchData.deviceAddress = info.scratchAddress;

    auto buildInfos = std::array{buildInfo};
    auto rangeInfos = std::array{rangeInfo};
    auto rangeInfoPtrs = std::array<const vk::AccelerationStructureBuildRangeInfoKHR *, 1>{rangeInfos.data()};
    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrs);
}

} // namespace nr::rhi

namespace nr::rhi::detail
{
[[nodiscard]] ValidationResult validationSuccess()
{
    return ValidationResult{
        .isValid = true,
    };
}

[[nodiscard]] ValidationResult validationFailure(std::string message)
{
    return ValidationResult{
        .message = std::move(message),
    };
}

[[nodiscard]] bool hasBuildFlag(vk::BuildAccelerationStructureFlagsKHR flags,
                                vk::BuildAccelerationStructureFlagBitsKHR bit)
{
    return (flags & bit) != vk::BuildAccelerationStructureFlagsKHR{};
}

[[nodiscard]] ValidationResult validateBuildFlagCombination(const AsBuildOptions &options)
{
    if (hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastBuild) &&
        hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::ePreferFastTrace))
    {
        return validationFailure("AsBuildOptions.buildFlags cannot combine PREFER_FAST_BUILD and PREFER_FAST_TRACE.");
    }

    if (hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate) ||
        hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowCompaction))
    {
        return validationFailure("AS update and compaction policies are outside the current RHI build contract.");
    }

    return validationSuccess();
}

[[nodiscard]] vk::DeviceSize indexTypeAlignment(vk::IndexType type)
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

[[nodiscard]] std::uint32_t geometryPrimitiveCount(const BlasGeometryRecord &record) noexcept
{
    return record.range.primitiveCount;
}

[[nodiscard]] ValidationResult validateTriangleGeometry(
    const vk::AccelerationStructureGeometryTrianglesDataKHR &triangles,
    const vk::AccelerationStructureBuildRangeInfoKHR &range)
{
    if (triangles.vertexStride == 0)
        return validationFailure("Triangle BLAS geometry requires vertexStride > 0.");
    if (triangles.vertexData.deviceAddress == 0)
        return validationFailure("Triangle BLAS geometry requires a non-zero vertex address.");
    if (range.primitiveCount == 0)
        return validationFailure("Triangle BLAS geometry requires primitiveCount > 0.");

    if (triangles.indexType == vk::IndexType::eNoneKHR)
    {
        if (triangles.indexData.deviceAddress != 0)
            return validationFailure(
                "Triangle BLAS geometry requires index address == 0 when indexType is VK_INDEX_TYPE_NONE_KHR.");

        auto const lastVertex = static_cast<std::uint64_t>(range.firstVertex) +
                                static_cast<std::uint64_t>(range.primitiveCount) * 3u - 1u;
        if (lastVertex > triangles.maxVertex)
            return validationFailure(
                "Non-indexed triangle BLAS geometry requires maxVertex to cover every consumed vertex.");
    }
    else if (triangles.indexData.deviceAddress == 0)
    {
        return validationFailure(
            "Triangle BLAS geometry requires a non-zero index address when indexType is not VK_INDEX_TYPE_NONE_KHR.");
    }
    else if (range.firstVertex > triangles.maxVertex)
    {
        return validationFailure("Indexed triangle BLAS geometry requires firstVertex <= maxVertex.");
    }

    auto requiredIndexAlignment = detail::indexTypeAlignment(triangles.indexType);
    if (requiredIndexAlignment > 0 && (triangles.indexData.deviceAddress % requiredIndexAlignment) != 0)
        return validationFailure(
            formatMessage("Triangle BLAS index address must be aligned to {} bytes.", requiredIndexAlignment));

    if (triangles.transformData.deviceAddress != 0 && (triangles.transformData.deviceAddress % 16) != 0)
        return validationFailure("Triangle BLAS transform address must be 16-byte aligned when present.");

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAabbGeometry(const vk::AccelerationStructureGeometryAabbsDataKHR &aabbs,
                                                    const vk::AccelerationStructureBuildRangeInfoKHR &range)
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

[[nodiscard]] ValidationResult validateGeometryBuffer(std::string_view label, const Buffer &buffer)
{
    if (!buffer.valid())
        return validationFailure(formatMessage("{} requires a valid buffer.", label));

    auto const requiredUsage = vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR |
                               vk::BufferUsageFlagBits::eShaderDeviceAddress;
    if ((buffer.usage() & requiredUsage) != requiredUsage)
        return validationFailure(
            formatMessage("{} requires AS-build-input and shader-device-address usage.", label));

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateGeometryBufferRange(std::string_view label, const Buffer &buffer,
                                                           vk::DeviceAddress address, vk::DeviceSize relativeOffset,
                                                           vk::DeviceSize byteSize)
{
    auto bufferValidation = validateGeometryBuffer(label, buffer);
    if (!bufferValidation.isValid)
        return bufferValidation;
    if (address == 0u || byteSize == 0u)
        return validationFailure(formatMessage("{} requires a non-empty device-address range.", label));

    auto const baseAddress = buffer.deviceAddress();
    if (baseAddress == 0u || address < baseAddress)
        return validationFailure(formatMessage("{} address precedes its declared buffer.", label));

    auto const addressOffset = address - baseAddress;
    if (addressOffset > buffer.size() || relativeOffset > buffer.size() - addressOffset)
        return validationFailure(formatMessage("{} offset exceeds its declared buffer.", label));
    auto const rangeOffset = addressOffset + relativeOffset;
    if (byteSize > buffer.size() - rangeOffset)
        return validationFailure(formatMessage("{} range exceeds its declared buffer.", label));

    return validationSuccess();
}

[[nodiscard]] std::optional<vk::DeviceSize> checkedGeometryByteSize(std::uint64_t count,
                                                                    vk::DeviceSize stride) noexcept
{
    if (stride != 0u && count > std::numeric_limits<vk::DeviceSize>::max() / stride)
        return std::nullopt;
    return static_cast<vk::DeviceSize>(count) * stride;
}

[[nodiscard]] ValidationResult validateBlasGeometryRecord(const BlasGeometryRecord &record)
{
    switch (record.geometry.geometryType)
    {
    case vk::GeometryTypeKHR::eTriangles:
    {
        auto const &triangles = record.geometry.geometry.triangles;
        auto geometryValidation = validateTriangleGeometry(triangles, record.range);
        if (!geometryValidation.isValid)
            return geometryValidation;

        auto const indexed = triangles.indexType != vk::IndexType::eNoneKHR;
        auto const firstVertexBytes = checkedGeometryByteSize(record.range.firstVertex, triangles.vertexStride);
        if (!firstVertexBytes.has_value())
            return validationFailure("Triangle BLAS vertex range offset overflows VkDeviceSize.");

        auto vertexOffset = *firstVertexBytes;
        auto vertexCount = indexed
                               ? static_cast<std::uint64_t>(triangles.maxVertex) - record.range.firstVertex + 1u
                               : static_cast<std::uint64_t>(record.range.primitiveCount) * 3u;
        if (!indexed)
        {
            if (record.range.primitiveOffset > std::numeric_limits<vk::DeviceSize>::max() - vertexOffset)
                return validationFailure("Triangle BLAS vertex range offset overflows VkDeviceSize.");
            vertexOffset += record.range.primitiveOffset;
        }

        auto vertexBytes = checkedGeometryByteSize(vertexCount, triangles.vertexStride);
        if (!vertexBytes.has_value())
            return validationFailure("Triangle BLAS vertex range size overflows VkDeviceSize.");
        auto vertexValidation = validateGeometryBufferRange("Triangle BLAS vertex data", record.dataBuffer.get(),
                                                            triangles.vertexData.deviceAddress, vertexOffset,
                                                            *vertexBytes);
        if (!vertexValidation.isValid)
            return vertexValidation;

        if (indexed)
        {
            if (!record.indexBuffer.has_value())
                return validationFailure("Indexed triangle BLAS geometry requires an index-buffer provenance.");
            auto const indexStride = indexTypeAlignment(triangles.indexType);
            auto indexBytes = checkedGeometryByteSize(static_cast<std::uint64_t>(record.range.primitiveCount) * 3u,
                                                      indexStride);
            if (!indexBytes.has_value())
                return validationFailure("Triangle BLAS index range size overflows VkDeviceSize.");
            auto indexValidation = validateGeometryBufferRange(
                "Triangle BLAS index data", record.indexBuffer->get(), triangles.indexData.deviceAddress,
                record.range.primitiveOffset, *indexBytes);
            if (!indexValidation.isValid)
                return indexValidation;
        }
        else if (record.indexBuffer.has_value())
        {
            return validationFailure("Non-indexed triangle BLAS geometry must not retain an index buffer.");
        }

        if (triangles.transformData.deviceAddress != 0u)
        {
            if (!record.transformBuffer.has_value())
                return validationFailure("Triangle BLAS transform data requires transform-buffer provenance.");
            auto transformValidation = validateGeometryBufferRange(
                "Triangle BLAS transform data", record.transformBuffer->get(), triangles.transformData.deviceAddress,
                record.range.transformOffset, sizeof(vk::TransformMatrixKHR));
            if (!transformValidation.isValid)
                return transformValidation;
        }
        else if (record.transformBuffer.has_value())
        {
            return validationFailure("Triangle BLAS geometry must not retain an unused transform buffer.");
        }

        return validationSuccess();
    }
    case vk::GeometryTypeKHR::eAabbs:
    {
        auto const &aabbs = record.geometry.geometry.aabbs;
        auto geometryValidation = validateAabbGeometry(aabbs, record.range);
        if (!geometryValidation.isValid)
            return geometryValidation;
        if (record.indexBuffer.has_value() || record.transformBuffer.has_value())
            return validationFailure("AABB BLAS geometry accepts only its data-buffer provenance.");
        auto aabbBytes = checkedGeometryByteSize(record.range.primitiveCount, aabbs.stride);
        if (!aabbBytes.has_value())
            return validationFailure("AABB BLAS data range size overflows VkDeviceSize.");
        return validateGeometryBufferRange("AABB BLAS data", record.dataBuffer.get(), aabbs.data.deviceAddress,
                                           record.range.primitiveOffset, *aabbBytes);
    }
    default:
        return validationFailure("BLAS geometry record supports only triangle and AABB geometry.");
    }
}

[[nodiscard]] ValidationResult validateBlasGeometryRecords(std::span<const BlasGeometryRecord> geometries)
{
    if (geometries.empty())
        return validationFailure("BLAS build requires at least one geometry record.");

    auto invalidIt = std::ranges::find_if(
        geometries, [](const BlasGeometryRecord &record) { return !validateBlasGeometryRecord(record).isValid; });
    if (invalidIt != std::ranges::end(geometries))
        return validateBlasGeometryRecord(*invalidIt);

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsBuildInputs(const BlasGeometriesBuildRecordInfo &info,
                                                     vk::DeviceSize scratchAlignment)
{
    auto flagDiagnostics = detail::validateBuildFlagCombination(info.options);
    if (!flagDiagnostics.isValid)
        return flagDiagnostics;

    if (!info.dst.valid())
        return validationFailure("BLAS build requires a valid destination acceleration structure.");
    if (info.dst.type() != vk::AccelerationStructureTypeKHR::eBottomLevel)
        return validationFailure(
            "BLAS build destination type must be VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR.");
    if (!info.scratchBuffer.valid())
        return validationFailure("BLAS build requires a valid scratch buffer.");
    auto const requiredScratchUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    if ((info.scratchBuffer.usage() & requiredScratchUsage) != requiredScratchUsage)
        return validationFailure("BLAS scratch buffer requires storage-buffer and shader-device-address usage.");
    if (info.scratchAddress == 0)
        return validationFailure("BLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("BLAS scratch address must be aligned to {} bytes.", scratchAlignment));

    auto geometryDiagnostics = detail::validateBlasGeometryRecords(info.geometries);
    if (!geometryDiagnostics.isValid)
        return geometryDiagnostics;

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsBuildInputs(const TlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment)
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
        return validationFailure(
            formatMessage("TLAS instances address must be aligned to {} bytes.", instanceAddressAlignment));
    if (info.scratchAddress == 0)
        return validationFailure("TLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("TLAS scratch address must be aligned to {} bytes.", scratchAlignment));
    if ((info.instanceBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ==
        vk::BufferUsageFlags{})
        return validationFailure(
            "TLAS instance buffer must include VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");
    auto const requiredScratchUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    if ((info.scratchBuffer.usage() & requiredScratchUsage) != requiredScratchUsage)
        return validationFailure("TLAS scratch buffer requires storage-buffer and shader-device-address usage.");
    return validationSuccess();
}
} // namespace nr::rhi::detail
