module nr.rhi;
import :accelerationStructure;
import dependency.vulkan;
import nr.utils;
import :type;
import :commandBatch;
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
    nrAssert((storageOffset % 256u) == 0u,
             "AccelerationStructureResource::create requires storageOffset to be 256-byte aligned.");
    nrAssert(storageOffset <= storageBuffer.size() &&
                 accelerationStructureSize <= (storageBuffer.size() - storageOffset),
             "AccelerationStructureResource::create requires storageOffset + accelerationStructureSize to fit inside "
             "storage buffer size.");

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

[[nodiscard]] const Buffer &AccelerationStructureResource::storageBuffer() const
{
    nrAssert(storageBuffer_.has_value(),
             "AccelerationStructureResource::storageBuffer requires valid storage linkage.");
    return storageBuffer_->get();
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
    return device_->get().getAccelerationStructureAddressKHR(addressInfo);
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

[[nodiscard]] BlasGeometryRecord makeBlasTriangleGeometryRecord(const Buffer &geometryBuffer,
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
        .geometryBuffer = std::cref(geometryBuffer),
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
        .updateScratchSize = sizeInfo.updateScratchSize,
    };
}

[[nodiscard]] AsBuildSizes queryBlasBuildSizes(const vk::raii::Device &device, const BlasGeometryLayout &layout,
                                               std::uint32_t primitiveCount, const AsBuildOptions &options,
                                               vk::AccelerationStructureBuildTypeKHR buildType)
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
        .updateScratchSize = sizeInfo.updateScratchSize,
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

    auto message = std::optional<std::string>{};
    auto const indices = std::views::iota(std::size_t{0}, records.size());
    std::ranges::for_each(indices, [&](std::size_t lhsIndex) {
        if (message.has_value())
        {
            return;
        }

        auto const &lhs = records[lhsIndex];
        auto const rhsIndices = indices | std::views::drop(lhsIndex + 1u);
        std::ranges::for_each(rhsIndices, [&](std::size_t rhsIndex) {
            if (message.has_value())
            {
                return;
            }

            auto const &rhs = records[rhsIndex];
            auto const lhsScratch = DeviceAddressRange{
                .buffer = lhs.build.scratchBuffer.handle(),
                .begin = lhs.build.scratchAddress,
                .size = lhs.scratchSize,
            };
            auto const rhsScratch = DeviceAddressRange{
                .buffer = rhs.build.scratchBuffer.handle(),
                .begin = rhs.build.scratchAddress,
                .size = rhs.scratchSize,
            };
            if (rangesOverlap(lhsScratch, rhsScratch))
            {
                message =
                    std::format("BLAS batch records {} and {} use overlapping scratch ranges.", lhsIndex, rhsIndex);
                return;
            }

            auto const lhsStorage = DeviceAddressRange{
                .buffer = lhs.build.dst.storageBuffer().handle(),
                .begin = lhs.build.dst.storageOffset(),
                .size = lhs.build.dst.size(),
            };
            auto const rhsStorage = DeviceAddressRange{
                .buffer = rhs.build.dst.storageBuffer().handle(),
                .begin = rhs.build.dst.storageOffset(),
                .size = rhs.build.dst.size(),
            };
            if (rangesOverlap(lhsStorage, rhsStorage))
            {
                message = std::format("BLAS batch records {} and {} use overlapping destination AS storage ranges.",
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
        buildInfo.mode = detail::toVkBuildMode(record.build.options.mode);
        buildInfo.srcAccelerationStructure =
            record.build.src.has_value() ? record.build.src->get().raw() : vk::AccelerationStructureKHR{};
        buildInfo.dstAccelerationStructure = record.build.dst.raw();
        buildInfo.geometryCount = static_cast<std::uint32_t>(geometries.size());
        buildInfo.pGeometries = geometries.data();
        buildInfo.scratchData.deviceAddress = record.build.scratchAddress;

        buildInfos.push_back(buildInfo);
        rangeInfoPtrGroups.push_back(rangeInfos.data());
    });

    commandBuffer.buildAccelerationStructuresKHR(buildInfos, rangeInfoPtrGroups);
}

void recordBuildBlas(const vk::raii::CommandBuffer &commandBuffer, const BlasBuildRecordInfo &info,
                     vk::DeviceSize scratchAlignment)
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

void recordUpdateBlas(const vk::raii::CommandBuffer &commandBuffer, BlasBuildRecordInfo info,
                      vk::DeviceSize scratchAlignment)
{
    info.options.mode = AsBuildMode::Update;
    recordBuildBlas(commandBuffer, info, scratchAlignment);
}

void recordUpdateTlas(const vk::raii::CommandBuffer &commandBuffer, TlasBuildRecordInfo info,
                      vk::DeviceSize scratchAlignment)
{
    info.options.mode = AsBuildMode::Update;
    recordBuildTlas(commandBuffer, info, scratchAlignment);
}

void recordBuildAccelerationStructuresIndirect(const vk::raii::CommandBuffer &commandBuffer,
                                               std::span<const AsIndirectBuildCommand> commands)
{
    nrAssert(*commandBuffer != nullptr, "recordBuildAccelerationStructuresIndirect requires a valid command buffer.");
    auto diagnostics = detail::validateIndirectBuildCommands(commands);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("recordBuildAccelerationStructuresIndirect invalid input: {}", diagnostics.message));

    auto buildInfos = commands |
                      std::views::transform([](const AsIndirectBuildCommand &command) { return command.buildInfo; }) |
                      std::ranges::to<std::vector>();
    auto indirectDeviceAddresses =
        commands |
        std::views::transform([](const AsIndirectBuildCommand &command) { return command.indirectDeviceAddress; }) |
        std::ranges::to<std::vector<vk::DeviceAddress>>();
    auto indirectStrides =
        commands | std::views::transform([](const AsIndirectBuildCommand &command) { return command.indirectStride; }) |
        std::ranges::to<std::vector<std::uint32_t>>();
    auto maxPrimitiveCountPtrs =
        commands |
        std::views::transform([](const AsIndirectBuildCommand &command) { return command.maxPrimitiveCounts.data(); }) |
        std::ranges::to<std::vector<const std::uint32_t *>>();

    commandBuffer.buildAccelerationStructuresIndirectKHR(buildInfos, indirectDeviceAddresses, indirectStrides,
                                                         maxPrimitiveCountPtrs);
}

[[nodiscard]] vk::CopyAccelerationStructureInfoKHR makeCopyAccelerationStructureInfo(const AsCopyRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyInfo(record);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("makeCopyAccelerationStructureInfo invalid input: {}", diagnostics.message));

    vk::CopyAccelerationStructureInfoKHR info{};
    info.src = record.src.raw();
    info.dst = record.dst.raw();
    info.mode = record.mode;
    return info;
}

[[nodiscard]] vk::CopyAccelerationStructureToMemoryInfoKHR makeCopyAccelerationStructureToMemoryInfo(
    const AsCopyToDeviceMemoryRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyToMemoryInfo(record);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("makeCopyAccelerationStructureToMemoryInfo invalid input: {}", diagnostics.message));

    vk::CopyAccelerationStructureToMemoryInfoKHR info{};
    info.src = record.src.raw();
    info.dst.deviceAddress = record.dstAddress;
    info.mode = record.mode;
    return info;
}

[[nodiscard]] vk::CopyMemoryToAccelerationStructureInfoKHR makeCopyMemoryToAccelerationStructureInfo(
    const AsCopyFromDeviceMemoryRecordInfo &record)
{
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(record);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("makeCopyMemoryToAccelerationStructureInfo invalid input: {}", diagnostics.message));

    vk::CopyMemoryToAccelerationStructureInfoKHR info{};
    info.src.deviceAddress = record.srcAddress;
    info.dst = record.dst.raw();
    info.mode = record.mode;
    return info;
}

void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                     const vk::CopyAccelerationStructureInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyAccelerationStructure requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("recordCopyAccelerationStructure invalid input: {}", diagnostics.message));

    commandBuffer.copyAccelerationStructureKHR(info);
}

void recordCopyAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer, const AsCopyRecordInfo &record)
{
    recordCopyAccelerationStructure(commandBuffer, makeCopyAccelerationStructureInfo(record));
}

void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer,
                                             const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyAccelerationStructureToMemory requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyToMemoryInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("recordCopyAccelerationStructureToMemory invalid input: {}", diagnostics.message));

    commandBuffer.copyAccelerationStructureToMemoryKHR(info);
}

void recordCopyAccelerationStructureToMemory(const vk::raii::CommandBuffer &commandBuffer,
                                             const AsCopyToDeviceMemoryRecordInfo &record)
{
    recordCopyAccelerationStructureToMemory(commandBuffer, makeCopyAccelerationStructureToMemoryInfo(record));
}

void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                             const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    nrAssert(*commandBuffer != nullptr, "recordCopyMemoryToAccelerationStructure requires a valid command buffer.");
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("recordCopyMemoryToAccelerationStructure invalid input: {}", diagnostics.message));

    commandBuffer.copyMemoryToAccelerationStructureKHR(info);
}

void recordCopyMemoryToAccelerationStructure(const vk::raii::CommandBuffer &commandBuffer,
                                             const AsCopyFromDeviceMemoryRecordInfo &record)
{
    recordCopyMemoryToAccelerationStructure(commandBuffer, makeCopyMemoryToAccelerationStructureInfo(record));
}

[[nodiscard]] vk::Result copyAccelerationStructure(const vk::raii::Device &device,
                                                   vk::DeferredOperationKHR deferredOperation,
                                                   const vk::CopyAccelerationStructureInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyAccelerationStructure requires a valid device.");
    auto diagnostics = detail::validateAsCopyInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("copyAccelerationStructure invalid input: {}", diagnostics.message));

    return device.copyAccelerationStructureKHR(deferredOperation, info);
}

[[nodiscard]] vk::Result copyAccelerationStructure(const vk::raii::Device &device,
                                                   vk::DeferredOperationKHR deferredOperation,
                                                   const AsCopyRecordInfo &record)
{
    return copyAccelerationStructure(device, deferredOperation, makeCopyAccelerationStructureInfo(record));
}

[[nodiscard]] vk::Result copyAccelerationStructureToMemory(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyAccelerationStructureToMemory requires a valid device.");
    auto diagnostics = detail::validateAsCopyToMemoryInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("copyAccelerationStructureToMemory invalid input: {}", diagnostics.message));

    return device.copyAccelerationStructureToMemoryKHR(deferredOperation, info);
}

[[nodiscard]] vk::Result copyAccelerationStructureToMemory(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const AsCopyToDeviceMemoryRecordInfo &record)
{
    return copyAccelerationStructureToMemory(device, deferredOperation,
                                             makeCopyAccelerationStructureToMemoryInfo(record));
}

[[nodiscard]] vk::Result copyMemoryToAccelerationStructure(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    nrAssert(*device != nullptr, "copyMemoryToAccelerationStructure requires a valid device.");
    auto diagnostics = detail::validateAsCopyFromMemoryInfo(info);
    nrAssert(diagnostics.isValid,
             detail::formatMessage("copyMemoryToAccelerationStructure invalid input: {}", diagnostics.message));

    return device.copyMemoryToAccelerationStructureKHR(deferredOperation, info);
}

[[nodiscard]] vk::Result copyMemoryToAccelerationStructure(const vk::raii::Device &device,
                                                           vk::DeferredOperationKHR deferredOperation,
                                                           const AsCopyFromDeviceMemoryRecordInfo &record)
{
    return copyMemoryToAccelerationStructure(device, deferredOperation,
                                             makeCopyMemoryToAccelerationStructureInfo(record));
}

[[nodiscard]] vk::raii::DeferredOperationKHR createDeferredOperation(const vk::raii::Device &device)
{
    nrAssert(*device != nullptr, "createDeferredOperation requires a valid device.");
    return device.createDeferredOperationKHR();
}

[[nodiscard]] vk::DeviceSize queryAccelerationStructureCompactedSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(device, accelerationStructure,
                                                              vk::QueryType::eAccelerationStructureCompactedSizeKHR);
}

[[nodiscard]] vk::DeviceSize queryAccelerationStructureSerializationSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(
        device, accelerationStructure, vk::QueryType::eAccelerationStructureSerializationSizeKHR);
}

[[nodiscard]] vk::DeviceSize queryAccelerationStructureDeviceTimelineSize(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<vk::DeviceSize>(device, accelerationStructure,
                                                              vk::QueryType::eAccelerationStructureSizeKHR);
}

[[nodiscard]] std::uint64_t queryAccelerationStructureSerializationBottomLevelPointerCount(
    const vk::raii::Device &device, const AccelerationStructureResource &accelerationStructure)
{
    return queryAccelerationStructureProperty<std::uint64_t>(
        device, accelerationStructure, vk::QueryType::eAccelerationStructureSerializationBottomLevelPointersKHR);
}

void recordWriteAccelerationStructureProperties(const vk::raii::CommandBuffer &commandBuffer,
                                                std::span<const vk::AccelerationStructureKHR> accelerationStructures,
                                                vk::QueryType queryType, vk::QueryPool queryPool,
                                                std::uint32_t firstQuery)
{
    nrAssert(*commandBuffer != nullptr, "recordWriteAccelerationStructureProperties requires a valid command buffer.");
    nrAssert(queryPool != vk::QueryPool{}, "recordWriteAccelerationStructureProperties requires a valid query pool.");
    nrAssert(!accelerationStructures.empty(),
             "recordWriteAccelerationStructureProperties requires at least one acceleration structure.");
    nrAssert(std::ranges::none_of(
                 accelerationStructures,
                 [](vk::AccelerationStructureKHR handle) { return handle == vk::AccelerationStructureKHR{}; }),
             "recordWriteAccelerationStructureProperties requires valid acceleration structure handles.");

    commandBuffer.writeAccelerationStructuresPropertiesKHR(accelerationStructures, queryType, queryPool, firstQuery);
}

void recordWriteAccelerationStructureProperty(const vk::raii::CommandBuffer &commandBuffer,
                                              const AccelerationStructureResource &accelerationStructure,
                                              vk::QueryType queryType, vk::QueryPool queryPool,
                                              std::uint32_t firstQuery)
{
    nrAssert(accelerationStructure.valid(),
             "recordWriteAccelerationStructureProperty requires a valid acceleration structure.");
    auto handles = std::array{accelerationStructure.raw()};
    recordWriteAccelerationStructureProperties(commandBuffer, std::span<const vk::AccelerationStructureKHR>{handles},
                                               queryType, queryPool, firstQuery);
}

void appendAsSubmitIntent(CommandBatch &batch, const AsSubmitIntent &intent)
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
                     "appendAsSubmitIntent requires signalValue > waitValue when waiting and signaling the same "
                     "timeline semaphore in one submit.");
        }
        batch.addSignal(intent.signalSemaphore, intent.signalValue, 0, intent.signalStageMask);
    }
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

[[nodiscard]] vk::BuildAccelerationStructureModeKHR toVkBuildMode(AsBuildMode mode)
{
    return mode == AsBuildMode::Update ? vk::BuildAccelerationStructureModeKHR::eUpdate
                                       : vk::BuildAccelerationStructureModeKHR::eBuild;
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

    if (options.allowUpdateExpected &&
        !hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate))
    {
        return validationFailure(
            "allowUpdateExpected=true requires VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR.");
    }

    if (options.mode == AsBuildMode::Update &&
        !hasBuildFlag(options.buildFlags, vk::BuildAccelerationStructureFlagBitsKHR::eAllowUpdate))
    {
        return validationFailure(
            "Update mode requires VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR in buildFlags.");
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
    if (triangles.maxVertex == 0)
        return validationFailure("Triangle BLAS geometry requires maxVertex > 0.");
    if (triangles.vertexData.deviceAddress == 0)
        return validationFailure("Triangle BLAS geometry requires a non-zero vertex address.");
    if (range.primitiveCount == 0)
        return validationFailure("Triangle BLAS geometry requires primitiveCount > 0.");

    if (triangles.indexType == vk::IndexType::eNoneKHR)
    {
        if (triangles.indexData.deviceAddress != 0)
            return validationFailure(
                "Triangle BLAS geometry requires index address == 0 when indexType is VK_INDEX_TYPE_NONE_KHR.");
    }
    else if (triangles.indexData.deviceAddress == 0)
    {
        return validationFailure(
            "Triangle BLAS geometry requires a non-zero index address when indexType is not VK_INDEX_TYPE_NONE_KHR.");
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

[[nodiscard]] ValidationResult validateBlasGeometryRecord(const BlasGeometryRecord &record)
{
    if (!record.geometryBuffer.get().valid())
        return validationFailure("BLAS geometry record requires a valid geometry input buffer.");
    if ((record.geometryBuffer.get().usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ==
        vk::BufferUsageFlags{})
        return validationFailure("BLAS geometry input buffer must include "
                                 "VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");

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

[[nodiscard]] ValidationResult validateIndirectBuildCommands(std::span<const AsIndirectBuildCommand> commands)
{
    if (commands.empty())
        return validationFailure("Indirect AS build requires at least one command.");

    auto invalidIt = std::ranges::find_if(commands, [](const AsIndirectBuildCommand &command) {
        const auto &buildInfo = command.buildInfo;
        return buildInfo.dstAccelerationStructure == vk::AccelerationStructureKHR{} || buildInfo.geometryCount == 0 ||
               (buildInfo.pGeometries == nullptr && buildInfo.ppGeometries == nullptr) ||
               (buildInfo.pGeometries != nullptr && buildInfo.ppGeometries != nullptr) ||
               buildInfo.scratchData.deviceAddress == 0 || command.indirectDeviceAddress == 0 ||
               command.indirectStride == 0 || (command.indirectDeviceAddress % 4u) != 0 ||
               (command.indirectStride % 4u) != 0 || command.maxPrimitiveCounts.size() != buildInfo.geometryCount;
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
    return validationFailure(
        formatMessage("Indirect AS build maxPrimitiveCounts count ({}) must match geometryCount ({}).",
                      invalidIt->maxPrimitiveCounts.size(), buildInfo.geometryCount));
}

[[nodiscard]] ValidationResult validateAsCopyInfo(const vk::CopyAccelerationStructureInfoKHR &info)
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

[[nodiscard]] ValidationResult validateAsCopyInfo(const AsCopyRecordInfo &info)
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

[[nodiscard]] ValidationResult validateAsCopyToMemoryInfo(const vk::CopyAccelerationStructureToMemoryInfoKHR &info)
{
    if (info.src == vk::AccelerationStructureKHR{})
        return validationFailure("AS serialize requires a valid source acceleration structure.");
    if (info.dst.deviceAddress == 0)
        return validationFailure("AS serialize requires a non-zero destination device address.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eSerialize)
        return validationFailure("AS serialize requires SERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsCopyToMemoryInfo(const AsCopyToDeviceMemoryRecordInfo &info)
{
    if (!info.src.valid())
        return validationFailure("AS serialize requires a valid source acceleration structure.");
    if (info.dstAddress == 0)
        return validationFailure("AS serialize requires a non-zero destination device address.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eSerialize)
        return validationFailure("AS serialize requires SERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsCopyFromMemoryInfo(const vk::CopyMemoryToAccelerationStructureInfoKHR &info)
{
    if (info.src.deviceAddress == 0)
        return validationFailure("AS deserialize requires a non-zero source device address.");
    if (info.dst == vk::AccelerationStructureKHR{})
        return validationFailure("AS deserialize requires a valid destination acceleration structure.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eDeserialize)
        return validationFailure("AS deserialize requires DESERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsCopyFromMemoryInfo(const AsCopyFromDeviceMemoryRecordInfo &info)
{
    if (info.srcAddress == 0)
        return validationFailure("AS deserialize requires a non-zero source device address.");
    if (!info.dst.valid())
        return validationFailure("AS deserialize requires a valid destination acceleration structure.");
    if (info.mode != vk::CopyAccelerationStructureModeKHR::eDeserialize)
        return validationFailure("AS deserialize requires DESERIALIZE mode.");
    return validationSuccess();
}

[[nodiscard]] ValidationResult validateAsBuildInputs(const BlasBuildRecordInfo &info, vk::DeviceSize scratchAlignment)
{
    auto flagDiagnostics = detail::validateBuildFlagCombination(info.options);
    if (!flagDiagnostics.isValid)
        return flagDiagnostics;

    if (!info.dst.valid())
        return validationFailure("BLAS build requires a valid destination acceleration structure.");
    if (info.dst.type() != vk::AccelerationStructureTypeKHR::eBottomLevel)
        return validationFailure(
            "BLAS build destination type must be VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR.");
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
        return validationFailure(
            "BLAS build requires a non-zero index address when indexType is not VK_INDEX_TYPE_NONE_KHR.");
    }

    auto requiredIndexAlignment = detail::indexTypeAlignment(info.geometryLayout.indexType);
    if (requiredIndexAlignment > 0 && (info.geometryInput.indexAddress % requiredIndexAlignment) != 0)
        return validationFailure(
            formatMessage("BLAS index address must be aligned to {} bytes.", requiredIndexAlignment));

    if (info.geometryInput.transformAddress != 0 && (info.geometryInput.transformAddress % 16) != 0)
        return validationFailure("BLAS transform address must be 16-byte aligned when present.");
    if (info.scratchAddress == 0)
        return validationFailure("BLAS build requires non-zero scratch device address.");
    if (scratchAlignment > 0 && (info.scratchAddress % scratchAlignment) != 0)
        return validationFailure(formatMessage("BLAS scratch address must be aligned to {} bytes.", scratchAlignment));
    if ((info.geometryBuffer.usage() & vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR) ==
        vk::BufferUsageFlags{})
        return validationFailure(
            "BLAS geometry buffer must include VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR.");
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
