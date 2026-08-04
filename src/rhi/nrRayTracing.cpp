module nr.rhi;
import :rayTracing;
import dependency.vulkan;
import :type;
import :pipeline;
import :resourcePool;
import nr.utils;
import std;

namespace nr::rhi
{
[[nodiscard]] vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    nrAssert(alignment > 0, "alignUp requires alignment > 0.");
    const auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value + (alignment - remainder);
}

[[nodiscard]] std::uint32_t alignUp(std::uint32_t value, std::uint32_t alignment)
{
    nrAssert(alignment > 0, "alignUp requires alignment > 0.");
    const auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value + (alignment - remainder);
}

[[nodiscard]] ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableLayoutDesc &desc)
{
    auto validation = rt_detail::validateShaderBindingTableLayoutDesc(desc);
    nrAssert(validation.isValid,
             rt_detail::formatMessage("makeShaderBindingTableBuildPlan invalid desc: {}", validation.message));

    auto normalizedDesc = desc;
    normalizedDesc.raygen.stride = rt_detail::effectiveStride(normalizedDesc.raygen, normalizedDesc.capabilities);
    normalizedDesc.miss.stride = rt_detail::effectiveStride(normalizedDesc.miss, normalizedDesc.capabilities);
    normalizedDesc.hit.stride = rt_detail::effectiveStride(normalizedDesc.hit, normalizedDesc.capabilities);
    normalizedDesc.callable.stride = rt_detail::effectiveStride(normalizedDesc.callable, normalizedDesc.capabilities);

    auto sectionPlan = rt_detail::buildSectionPlan(normalizedDesc);

    auto totalSize = vk::DeviceSize{0};
    std::ranges::for_each(sectionPlan, [&](const ShaderBindingTableBuildPlanSection &section) {
        totalSize = std::max(totalSize, section.offset + section.size);
    });

    totalSize = alignUp(totalSize, static_cast<vk::DeviceSize>(normalizedDesc.capabilities.shaderGroupBaseAlignment));

    return ShaderBindingTableBuildPlan{
        .totalSize = totalSize,
        .handleSize = normalizedDesc.capabilities.shaderGroupHandleSize,
        .handleAlignment = normalizedDesc.capabilities.shaderGroupHandleAlignment,
        .baseAlignment = normalizedDesc.capabilities.shaderGroupBaseAlignment,
        .raygen = sectionPlan[0],
        .miss = sectionPlan[1],
        .hit = sectionPlan[2],
        .callable = sectionPlan[3],
    };
}

[[nodiscard]] ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableBuildDesc &desc)
{
    auto validation = rt_detail::validateShaderBindingTableBuildDesc(desc);
    nrAssert(validation.isValid,
             rt_detail::formatMessage("makeShaderBindingTableBuildPlan invalid desc: {}", validation.message));

    return makeShaderBindingTableBuildPlan(ShaderBindingTableLayoutDesc{
        .capabilities = desc.capabilities,
        .pipelineGroupCount = desc.pipeline.shaderGroupCount(),
        .raygen = desc.raygen,
        .miss = desc.miss,
        .hit = desc.hit,
        .callable = desc.callable,
    });
}

[[nodiscard]] ShaderBindingTable ShaderBindingTable::create(const ResourceFactory &resourceFactory,
                                                            const ShaderBindingTableBuildDesc &desc)
{
    auto validation = rt_detail::validateShaderBindingTableBuildDesc(desc);
    nrAssert(validation.isValid,
             rt_detail::formatMessage("ShaderBindingTable::create invalid desc: {}", validation.message));

    auto plan = makeShaderBindingTableBuildPlan(desc);
    nrAssert(plan.totalSize > 0, "ShaderBindingTable::create requires totalSize > 0.");

    auto packSection = [&](std::span<std::uint8_t> tableBytes,
                           const ShaderBindingTableBuildPlanSection &plannedSection) {
        auto sectionRecordCount = rt_detail::recordCount(plannedSection.section);
        if (sectionRecordCount == 0)
        {
            return;
        }

        auto copyRecord = [&](std::uint32_t recordIndex, std::uint32_t shaderGroupIndex,
                              std::span<const std::uint8_t> recordData) {
            auto dstOffset =
                plannedSection.offset + (static_cast<vk::DeviceSize>(recordIndex) * plannedSection.section.stride);
            auto dstStart = static_cast<std::size_t>(dstOffset);
            nrAssert(dstStart + static_cast<std::size_t>(plan.handleSize) <= tableBytes.size(),
                     "ShaderBindingTable::create destination handle copy range overflow.");
            nrAssert(dstStart + static_cast<std::size_t>(plan.handleSize) + recordData.size() <= tableBytes.size(),
                     "ShaderBindingTable::create destination record data copy range overflow.");

            auto handle = desc.pipeline.shaderGroupHandles(shaderGroupIndex, 1, plan.handleSize);
            nrAssert(handle.size() == static_cast<std::size_t>(plan.handleSize),
                     "ShaderBindingTable::create expected one shader group handle.");

            auto dstHandleView = tableBytes.subspan(dstStart, plan.handleSize);
            std::ranges::copy(handle, dstHandleView.begin());

            if (!recordData.empty())
            {
                auto dstDataView =
                    tableBytes.subspan(dstStart + static_cast<std::size_t>(plan.handleSize), recordData.size());
                std::ranges::copy(recordData, dstDataView.begin());
            }
        };

        if (!plannedSection.section.records.empty())
        {
            auto recordIndices = std::views::iota(std::uint32_t{0}, sectionRecordCount);
            std::ranges::for_each(recordIndices, [&](std::uint32_t recordIndex) {
                const auto &record = plannedSection.section.records[recordIndex];
                copyRecord(recordIndex, record.groupIndex, record.data);
            });
            return;
        }

        auto handles =
            desc.pipeline.shaderGroupHandles(plannedSection.section.firstGroup, sectionRecordCount, plan.handleSize);
        auto groupIndices = std::views::iota(std::uint32_t{0}, sectionRecordCount);
        std::ranges::for_each(groupIndices, [&](std::uint32_t groupIndex) {
            auto dstOffset =
                plannedSection.offset + (static_cast<vk::DeviceSize>(groupIndex) * plannedSection.section.stride);
            auto srcOffset = static_cast<std::size_t>(groupIndex) * static_cast<std::size_t>(plan.handleSize);

            auto dstStart = static_cast<std::size_t>(dstOffset);
            nrAssert(dstStart + static_cast<std::size_t>(plan.handleSize) <= tableBytes.size(),
                     "ShaderBindingTable::create destination copy range overflow.");
            nrAssert(srcOffset + static_cast<std::size_t>(plan.handleSize) <= handles.size(),
                     "ShaderBindingTable::create source handle range overflow.");

            auto srcView = std::span<const std::uint8_t>(handles).subspan(srcOffset, plan.handleSize);
            auto dstView = tableBytes.subspan(dstStart, plan.handleSize);
            std::ranges::copy(srcView, dstView.begin());
        });
    };

    auto tableBytes = std::vector<std::uint8_t>(static_cast<std::size_t>(plan.totalSize), std::uint8_t{0});
    packSection(tableBytes, plan.raygen);
    packSection(tableBytes, plan.miss);
    packSection(tableBytes, plan.hit);
    packSection(tableBytes, plan.callable);

    vk::BufferCreateInfo bufferInfo{};
    bufferInfo.size = plan.totalSize;
    bufferInfo.usage = vk::BufferUsageFlagBits::eShaderBindingTableKHR | vk::BufferUsageFlagBits::eShaderDeviceAddress;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    auto buffer = resourceFactory.createBuffer(bufferInfo, MemoryUsage::CpuToGpu, desc.debugName);
    nrAssert(buffer.valid(), "ShaderBindingTable::create failed to allocate SBT buffer.");
    nrAssert(buffer.mapped() != nullptr, "ShaderBindingTable::create requires a host-visible mapped SBT buffer.");

    buffer.writeMappedAndFlush(std::span<const std::uint8_t>(tableBytes));

    ShaderBindingTable sbt;
    auto baseAddress = buffer.deviceAddress();
    sbt.buffer_ = std::move(buffer);

    sbt.raygenRegion_ = rt_detail::buildRegion(baseAddress, plan.raygen);
    sbt.missRegion_ = rt_detail::buildRegion(baseAddress, plan.miss);
    sbt.hitRegion_ = rt_detail::buildRegion(baseAddress, plan.hit);
    sbt.callableRegion_ = rt_detail::buildRegion(baseAddress, plan.callable);

    auto baseAlignment = static_cast<vk::DeviceSize>(desc.capabilities.shaderGroupBaseAlignment);
    auto checkRegionAlignment = [&](std::string_view label, const vk::StridedDeviceAddressRegionKHR &region) {
        if (region.deviceAddress == 0)
        {
            return;
        }
        nrAssert((region.deviceAddress % baseAlignment) == 0,
                 rt_detail::formatMessage(
                     "ShaderBindingTable::create {} deviceAddress is not shaderGroupBaseAlignment aligned.", label));
    };

    checkRegionAlignment("raygen", sbt.raygenRegion_);
    checkRegionAlignment("miss", sbt.missRegion_);
    checkRegionAlignment("hit", sbt.hitRegion_);
    checkRegionAlignment("callable", sbt.callableRegion_);

    return sbt;
}

[[nodiscard]] bool ShaderBindingTable::valid() const noexcept
{
    return buffer_.valid();
}

[[nodiscard]] const Buffer &ShaderBindingTable::buffer() const noexcept
{
    return buffer_;
}

[[nodiscard]] const vk::StridedDeviceAddressRegionKHR &ShaderBindingTable::raygenRegion() const noexcept
{
    return raygenRegion_;
}

[[nodiscard]] const vk::StridedDeviceAddressRegionKHR &ShaderBindingTable::missRegion() const noexcept
{
    return missRegion_;
}

[[nodiscard]] const vk::StridedDeviceAddressRegionKHR &ShaderBindingTable::hitRegion() const noexcept
{
    return hitRegion_;
}

[[nodiscard]] const vk::StridedDeviceAddressRegionKHR &ShaderBindingTable::callableRegion() const noexcept
{
    return callableRegion_;
}

[[nodiscard]] ShaderBindingTable::Regions ShaderBindingTable::regions() const noexcept
{
    return Regions{
        .raygen = raygenRegion_,
        .miss = missRegion_,
        .hit = hitRegion_,
        .callable = callableRegion_,
    };
}

[[nodiscard]] vk::TraceRaysIndirectCommand2KHR ShaderBindingTable::traceRaysIndirectCommand2(
    TraceRaysDimensions dimensions) const noexcept
{
    auto regions = this->regions();
    vk::TraceRaysIndirectCommand2KHR command{};
    command.raygenShaderRecordAddress = regions.raygen.deviceAddress;
    command.raygenShaderRecordSize = regions.raygen.size;
    command.missShaderBindingTableAddress = regions.miss.deviceAddress;
    command.missShaderBindingTableSize = regions.miss.size;
    command.missShaderBindingTableStride = regions.miss.stride;
    command.hitShaderBindingTableAddress = regions.hit.deviceAddress;
    command.hitShaderBindingTableSize = regions.hit.size;
    command.hitShaderBindingTableStride = regions.hit.stride;
    command.callableShaderBindingTableAddress = regions.callable.deviceAddress;
    command.callableShaderBindingTableSize = regions.callable.size;
    command.callableShaderBindingTableStride = regions.callable.stride;
    command.width = dimensions.width;
    command.height = dimensions.height;
    command.depth = dimensions.depth;
    return command;
}

[[nodiscard]] vk::TraceRaysIndirectCommand2KHR makeTraceRaysIndirectCommand2(
    const ShaderBindingTable &shaderBindingTable, TraceRaysDimensions dimensions)
{
    nrAssert(shaderBindingTable.valid(), "makeTraceRaysIndirectCommand2 requires a valid shader binding table.");
    return shaderBindingTable.traceRaysIndirectCommand2(dimensions);
}

void setRayTracingPipelineStackSize(const vk::raii::CommandBuffer &commandBuffer, std::uint32_t pipelineStackSize)
{
    nrAssert(*commandBuffer != nullptr, "setRayTracingPipelineStackSize requires a valid command buffer.");
    nrAssert(pipelineStackSize > 0, "setRayTracingPipelineStackSize requires pipelineStackSize > 0.");
    commandBuffer.setRayTracingPipelineStackSizeKHR(pipelineStackSize);
}

void applyRayTracingPipelineStackSize(const vk::raii::CommandBuffer &commandBuffer, const RayTracingPipeline &pipeline,
                                      std::optional<std::uint32_t> pipelineStackSize)
{
    auto diagnostics = rt_detail::validatePipelineStackSize(pipeline, pipelineStackSize);
    nrAssert(diagnostics.isValid,
             rt_detail::formatMessage("applyRayTracingPipelineStackSize invalid state: {}", diagnostics.message));

    if (pipelineStackSize.has_value())
    {
        setRayTracingPipelineStackSize(commandBuffer, *pipelineStackSize);
    }
}

void traceRays(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysDesc &desc,
               const RayTracingCapabilitySnapshot &capabilities)
{
    nrAssert(*commandBuffer != nullptr, "traceRays requires a valid command buffer.");
    nrAssert(desc.pipeline.valid(), "traceRays requires a valid ray tracing pipeline.");
    nrAssert(desc.shaderBindingTable.valid(), "traceRays requires a valid shader binding table.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer,
             "traceRays requires a queue family that supports compute operations.");

    auto diagnostics = rt_detail::validateTraceRaysDispatch(desc.dimensions, capabilities);
    nrAssert(diagnostics.isValid, rt_detail::formatMessage("traceRays invalid dispatch: {}", diagnostics.message));

    auto regions = desc.shaderBindingTable.regions();
    nrAssert(regions.raygen.size == regions.raygen.stride, "traceRays requires raygen SBT region size == stride.");

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline.raw());
    applyRayTracingPipelineStackSize(commandBuffer, desc.pipeline, desc.pipelineStackSize);
    commandBuffer.traceRaysKHR(regions.raygen, regions.miss, regions.hit, regions.callable, desc.dimensions.width,
                               desc.dimensions.height, desc.dimensions.depth);
}

void traceRaysIndirect(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysIndirectDesc &desc,
                       const RayTracingCapabilitySnapshot &capabilities)
{
    nrAssert(*commandBuffer != nullptr, "traceRaysIndirect requires a valid command buffer.");
    nrAssert(desc.pipeline.valid(), "traceRaysIndirect requires a valid ray tracing pipeline.");
    nrAssert(desc.shaderBindingTable.valid(), "traceRaysIndirect requires a valid shader binding table.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer,
             "traceRaysIndirect requires a queue family that supports compute operations.");

    auto diagnostics = rt_detail::validateTraceRaysIndirect(desc.indirectDeviceAddress, capabilities);
    nrAssert(diagnostics.isValid,
             rt_detail::formatMessage("traceRaysIndirect invalid arguments: {}", diagnostics.message));

    auto regions = desc.shaderBindingTable.regions();
    nrAssert(regions.raygen.size == regions.raygen.stride,
             "traceRaysIndirect requires raygen SBT region size == stride.");

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline.raw());
    applyRayTracingPipelineStackSize(commandBuffer, desc.pipeline, desc.pipelineStackSize);
    commandBuffer.traceRaysIndirectKHR(regions.raygen, regions.miss, regions.hit, regions.callable,
                                       desc.indirectDeviceAddress);
}

void traceRaysIndirect2(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysIndirect2Desc &desc,
                        const RayTracingCapabilitySnapshot &capabilities)
{
    nrAssert(*commandBuffer != nullptr, "traceRaysIndirect2 requires a valid command buffer.");
    nrAssert(desc.pipeline.valid(), "traceRaysIndirect2 requires a valid ray tracing pipeline.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer,
             "traceRaysIndirect2 requires a queue family that supports compute operations.");

    auto diagnostics = rt_detail::validateTraceRaysIndirect2(desc.indirectDeviceAddress, capabilities);
    nrAssert(diagnostics.isValid,
             rt_detail::formatMessage("traceRaysIndirect2 invalid arguments: {}", diagnostics.message));

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline.raw());
    applyRayTracingPipelineStackSize(commandBuffer, desc.pipeline, desc.pipelineStackSize);
    commandBuffer.traceRaysIndirect2KHR(desc.indirectDeviceAddress);
}
} // namespace nr::rhi

namespace nr::rhi::rt_detail
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

[[nodiscard]] std::uint32_t recordCount(const ShaderBindingTableSectionDesc &section)
{
    if (!section.records.empty())
    {
        nrAssert(section.records.size() <= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()),
                 "SBT section record count exceeds uint32_t range.");
        return static_cast<std::uint32_t>(section.records.size());
    }

    return section.groupCount;
}

[[nodiscard]] std::size_t maxRecordDataSize(const ShaderBindingTableSectionDesc &section)
{
    if (section.records.empty())
    {
        return 0;
    }

    auto sizes = section.records |
                 std::views::transform([](const ShaderBindingTableRecordDesc &record) { return record.data.size(); });
    return std::ranges::max(sizes);
}

[[nodiscard]] std::uint32_t effectiveStride(const ShaderBindingTableSectionDesc &section,
                                            const RayTracingCapabilitySnapshot &capabilities)
{
    if (recordCount(section) == 0)
    {
        return 0;
    }

    if (section.stride != 0)
    {
        return section.stride;
    }

    auto minimumStride = static_cast<vk::DeviceSize>(capabilities.shaderGroupHandleSize) +
                         static_cast<vk::DeviceSize>(maxRecordDataSize(section));
    nrAssert(minimumStride <= static_cast<vk::DeviceSize>(std::numeric_limits<std::uint32_t>::max()),
             "SBT record stride exceeds uint32_t range.");
    return static_cast<std::uint32_t>(
        alignUp(minimumStride, static_cast<vk::DeviceSize>(capabilities.shaderGroupHandleAlignment)));
}

[[nodiscard]] ValidationResult validateSection(std::string_view label, const ShaderBindingTableSectionDesc &section,
                                               std::uint32_t effectiveSectionStride,
                                               const RayTracingCapabilitySnapshot &capabilities,
                                               std::uint32_t pipelineGroupCount)
{
    const auto sectionRecordCount = recordCount(section);
    if (sectionRecordCount == 0)
    {
        return validationSuccess();
    }

    if (!section.records.empty() && section.groupCount != 0 && section.groupCount != sectionRecordCount)
    {
        return validationFailure(
            formatMessage("{} groupCount ({}) must be 0 or match records.size() ({}) when records are provided.", label,
                          section.groupCount, sectionRecordCount));
    }

    auto requiredStride = static_cast<std::uint64_t>(capabilities.shaderGroupHandleSize) +
                          static_cast<std::uint64_t>(maxRecordDataSize(section));
    if (requiredStride > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return validationFailure(formatMessage("{} record payload makes stride exceed uint32_t range.", label));
    }

    if (effectiveSectionStride < requiredStride)
    {
        return validationFailure(
            formatMessage("{} stride ({}) must be >= shaderGroupHandleSize + max record payload ({}).", label,
                          effectiveSectionStride, requiredStride));
    }

    if ((effectiveSectionStride % capabilities.shaderGroupHandleAlignment) != 0)
    {
        return validationFailure(formatMessage("{} stride ({}) must be aligned to shaderGroupHandleAlignment ({}).",
                                               label, effectiveSectionStride, capabilities.shaderGroupHandleAlignment));
    }

    if (effectiveSectionStride > capabilities.maxShaderGroupStride)
    {
        return validationFailure(formatMessage("{} stride ({}) exceeds maxShaderGroupStride ({}).", label,
                                               effectiveSectionStride, capabilities.maxShaderGroupStride));
    }

    if (section.records.empty())
    {
        auto groupEnd = static_cast<std::uint64_t>(section.firstGroup) + static_cast<std::uint64_t>(section.groupCount);
        if (groupEnd > static_cast<std::uint64_t>(pipelineGroupCount))
        {
            return validationFailure(formatMessage("{} group range [{}..{}) exceeds pipeline group count ({}).", label,
                                                   section.firstGroup, groupEnd, pipelineGroupCount));
        }
    }
    else
    {
        auto invalidRecordIt = std::ranges::find_if(section.records, [&](const ShaderBindingTableRecordDesc &record) {
            return record.groupIndex >= pipelineGroupCount;
        });
        if (invalidRecordIt != std::ranges::end(section.records))
        {
            return validationFailure(formatMessage("{} record group index ({}) exceeds pipeline group count ({}).",
                                                   label, invalidRecordIt->groupIndex, pipelineGroupCount));
        }
    }

    return validationSuccess();
}

[[nodiscard]] vk::DeviceSize sectionSize(const ShaderBindingTableSectionDesc &section)
{
    return static_cast<vk::DeviceSize>(recordCount(section)) * static_cast<vk::DeviceSize>(section.stride);
}

[[nodiscard]] std::array<ShaderBindingTableBuildPlanSection, 4> buildSectionPlan(
    const ShaderBindingTableLayoutDesc &desc)
{
    auto sections = std::array<ShaderBindingTableBuildPlanSection, 4>{
        ShaderBindingTableBuildPlanSection{.section = desc.raygen},
        ShaderBindingTableBuildPlanSection{.section = desc.miss},
        ShaderBindingTableBuildPlanSection{.section = desc.hit},
        ShaderBindingTableBuildPlanSection{.section = desc.callable},
    };

    auto runningOffset = vk::DeviceSize{0};
    std::ranges::for_each(sections, [&](ShaderBindingTableBuildPlanSection &plannedSection) {
        if (recordCount(plannedSection.section) == 0)
        {
            plannedSection.offset = 0;
            plannedSection.size = 0;
            plannedSection.section.stride = 0;
            return;
        }

        runningOffset = alignUp(runningOffset, static_cast<vk::DeviceSize>(desc.capabilities.shaderGroupBaseAlignment));
        plannedSection.offset = runningOffset;
        plannedSection.size = sectionSize(plannedSection.section);
        runningOffset += plannedSection.size;
    });

    return sections;
}

[[nodiscard]] vk::StridedDeviceAddressRegionKHR buildRegion(vk::DeviceAddress baseAddress,
                                                            const ShaderBindingTableBuildPlanSection &section)
{
    if (recordCount(section.section) == 0)
    {
        return vk::StridedDeviceAddressRegionKHR{};
    }

    vk::StridedDeviceAddressRegionKHR region{};
    region.deviceAddress = baseAddress + section.offset;
    region.stride = section.section.stride;
    region.size = section.size;
    return region;
}

[[nodiscard]] ValidationResult validateShaderBindingTableBuildDesc(const ShaderBindingTableBuildDesc &desc)
{
    if (!desc.pipeline.valid())
    {
        return validationFailure("ShaderBindingTableBuildDesc requires a valid ray tracing pipeline.");
    }

    auto layoutDesc = ShaderBindingTableLayoutDesc{
        .capabilities = desc.capabilities,
        .pipelineGroupCount = desc.pipeline.shaderGroupCount(),
        .raygen = desc.raygen,
        .miss = desc.miss,
        .hit = desc.hit,
        .callable = desc.callable,
    };

    return validateShaderBindingTableLayoutDesc(layoutDesc);
}

[[nodiscard]] ValidationResult validateShaderBindingTableLayoutDesc(const ShaderBindingTableLayoutDesc &desc)
{
    if (desc.capabilities.shaderGroupHandleSize == 0)
    {
        return validationFailure("shaderGroupHandleSize must be > 0.");
    }

    if (desc.capabilities.shaderGroupHandleAlignment == 0)
    {
        return validationFailure("shaderGroupHandleAlignment must be > 0.");
    }

    if (desc.capabilities.shaderGroupBaseAlignment == 0)
    {
        return validationFailure("shaderGroupBaseAlignment must be > 0.");
    }

    if (desc.capabilities.maxShaderGroupStride == 0)
    {
        return validationFailure("maxShaderGroupStride must be > 0.");
    }

    if (desc.pipelineGroupCount == 0)
    {
        return validationFailure("pipelineGroupCount must be > 0.");
    }

    if (rt_detail::recordCount(desc.raygen) != 1)
    {
        return validationFailure("raygen section must contain exactly one record so size == stride.");
    }

    auto raygen = desc.raygen;
    auto miss = desc.miss;
    auto hit = desc.hit;
    auto callable = desc.callable;

    raygen.stride = rt_detail::effectiveStride(raygen, desc.capabilities);
    miss.stride = rt_detail::effectiveStride(miss, desc.capabilities);
    hit.stride = rt_detail::effectiveStride(hit, desc.capabilities);
    callable.stride = rt_detail::effectiveStride(callable, desc.capabilities);

    auto groupCount = desc.pipelineGroupCount;

    auto raygenValidation = rt_detail::validateSection("raygen", raygen, raygen.stride, desc.capabilities, groupCount);
    if (!raygenValidation.isValid)
    {
        return raygenValidation;
    }

    auto missValidation = rt_detail::validateSection("miss", miss, miss.stride, desc.capabilities, groupCount);
    if (!missValidation.isValid)
    {
        return missValidation;
    }

    auto hitValidation = rt_detail::validateSection("hit", hit, hit.stride, desc.capabilities, groupCount);
    if (!hitValidation.isValid)
    {
        return hitValidation;
    }

    auto callableValidation =
        rt_detail::validateSection("callable", callable, callable.stride, desc.capabilities, groupCount);
    if (!callableValidation.isValid)
    {
        return callableValidation;
    }

    auto raygenSize = rt_detail::sectionSize(raygen);
    if (raygenSize != raygen.stride)
    {
        return validationFailure("raygen section requires size == stride.");
    }

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateTraceRaysDispatch(const TraceRaysDimensions &dimensions,
                                                         const RayTracingCapabilitySnapshot &capabilities)
{
    if (dimensions.width == 0 || dimensions.height == 0 || dimensions.depth == 0)
    {
        return validationFailure("traceRays dimensions must all be > 0.");
    }

    auto dispatchWidth = static_cast<std::uint64_t>(dimensions.width);
    auto dispatchHeight = static_cast<std::uint64_t>(dimensions.height);
    auto dispatchDepth = static_cast<std::uint64_t>(dimensions.depth);

    if (capabilities.maxDispatchDimensions[0] > 0 && dispatchWidth > capabilities.maxDispatchDimensions[0])
    {
        return validationFailure(formatMessage("traceRays width ({}) exceeds max dispatch width ({}).",
                                               dimensions.width, capabilities.maxDispatchDimensions[0]));
    }

    if (capabilities.maxDispatchDimensions[1] > 0 && dispatchHeight > capabilities.maxDispatchDimensions[1])
    {
        return validationFailure(formatMessage("traceRays height ({}) exceeds max dispatch height ({}).",
                                               dimensions.height, capabilities.maxDispatchDimensions[1]));
    }

    if (capabilities.maxDispatchDimensions[2] > 0 && dispatchDepth > capabilities.maxDispatchDimensions[2])
    {
        return validationFailure(formatMessage("traceRays depth ({}) exceeds max dispatch depth ({}).",
                                               dimensions.depth, capabilities.maxDispatchDimensions[2]));
    }

    auto invocationCount = dispatchWidth * dispatchHeight * dispatchDepth;
    if (capabilities.maxRayDispatchInvocationCount > 0 && invocationCount > capabilities.maxRayDispatchInvocationCount)
    {
        return validationFailure(
            formatMessage("traceRays invocation count ({}) exceeds maxRayDispatchInvocationCount ({}).",
                          invocationCount, capabilities.maxRayDispatchInvocationCount));
    }

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateTraceRaysIndirect(vk::DeviceAddress indirectDeviceAddress,
                                                         const RayTracingCapabilitySnapshot &capabilities)
{
    if (!capabilities.rayTracingPipelineTraceRaysIndirect)
    {
        return validationFailure("traceRaysIndirect requires rayTracingPipelineTraceRaysIndirect feature.");
    }

    if (indirectDeviceAddress == 0)
    {
        return validationFailure("traceRaysIndirect requires a non-zero indirect device address.");
    }

    if ((indirectDeviceAddress % 4u) != 0)
    {
        return validationFailure("traceRaysIndirect indirect device address must be 4-byte aligned.");
    }

    return validationSuccess();
}

[[nodiscard]] ValidationResult validateTraceRaysIndirect2(vk::DeviceAddress indirectDeviceAddress,
                                                          const RayTracingCapabilitySnapshot &capabilities)
{
    if (!capabilities.rayTracingMaintenance1)
    {
        return validationFailure("traceRaysIndirect2 requires rayTracingMaintenance1 feature.");
    }

    if (!capabilities.rayTracingPipelineTraceRaysIndirect2)
    {
        return validationFailure("traceRaysIndirect2 requires rayTracingPipelineTraceRaysIndirect2 feature.");
    }

    if (indirectDeviceAddress == 0)
    {
        return validationFailure("traceRaysIndirect2 requires a non-zero indirect device address.");
    }

    if ((indirectDeviceAddress % 4u) != 0)
    {
        return validationFailure("traceRaysIndirect2 indirect device address must be 4-byte aligned.");
    }

    return validationSuccess();
}

[[nodiscard]] ValidationResult validatePipelineStackSize(const RayTracingPipeline &pipeline,
                                                         std::optional<std::uint32_t> pipelineStackSize)
{
    if (pipeline.dynamicPipelineStackSize())
    {
        if (!pipelineStackSize.has_value())
        {
            return validationFailure(
                "Ray tracing pipeline uses dynamic stack size; pipelineStackSize must be provided before trace.");
        }
        if (*pipelineStackSize == 0)
        {
            return validationFailure("Ray tracing pipeline stack size must be > 0.");
        }
        return validationSuccess();
    }

    if (pipelineStackSize.has_value())
    {
        return validationFailure(
            "pipelineStackSize can only be provided for pipelines created with dynamicPipelineStackSize=true.");
    }

    return validationSuccess();
}
} // namespace nr::rhi::rt_detail
