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
[[nodiscard]] ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableLayoutDesc &desc)
{
    auto validation = rt_detail::validateShaderBindingTableLayoutDesc(desc);
    nrAssert(validation.isValid, "makeShaderBindingTableBuildPlan invalid desc: {}", validation.message);

    auto normalizedDesc = desc;
    normalizedDesc.raygen.stride = rt_detail::effectiveStride(normalizedDesc.raygen, normalizedDesc.capabilities);
    normalizedDesc.miss.stride = rt_detail::effectiveStride(normalizedDesc.miss, normalizedDesc.capabilities);
    normalizedDesc.hit.stride = rt_detail::effectiveStride(normalizedDesc.hit, normalizedDesc.capabilities);
    normalizedDesc.callable.stride = rt_detail::effectiveStride(normalizedDesc.callable, normalizedDesc.capabilities);

    auto sectionPlan = rt_detail::buildSectionPlan(normalizedDesc);

    auto totalSize = vk::DeviceSize{0};
    std::ranges::for_each(sectionPlan, [&](const ShaderBindingTableBuildPlanSection &section) {
        totalSize = std::max(totalSize, nr::checkedAdd(section.offset, section.size, "SBT section end"));
    });

    totalSize = nr::checkedAlignUp(totalSize,
                               static_cast<vk::DeviceSize>(normalizedDesc.capabilities.shaderGroupBaseAlignment),
                               "SBT total size alignment");

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
    nrAssert(validation.isValid, "makeShaderBindingTableBuildPlan invalid desc: {}", validation.message);

    return makeShaderBindingTableBuildPlan(ShaderBindingTableLayoutDesc{
        .capabilities = desc.pipeline.capabilities(),
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
    nrAssert(validation.isValid, "ShaderBindingTable::create invalid desc: {}", validation.message);

    auto plan = makeShaderBindingTableBuildPlan(desc);
    nrAssert(plan.totalSize > 0, "ShaderBindingTable::create requires totalSize > 0.");

    auto packSection = [&](std::span<std::uint8_t> tableBytes, const ShaderBindingTableSectionDesc &section,
                           const ShaderBindingTableBuildPlanSection &plannedSection) {
        auto sectionRecordCount = rt_detail::recordCount(section);
        nrAssert(sectionRecordCount == plannedSection.recordCount,
                 "ShaderBindingTable::create source section no longer matches its value build plan.");
        if (sectionRecordCount == 0)
        {
            return;
        }

        auto copyRecord = [&](std::uint32_t recordIndex, std::uint32_t shaderGroupIndex,
                              std::span<const std::uint8_t> recordData) {
            auto const recordOffset = nr::checkedMultiply(static_cast<vk::DeviceSize>(recordIndex),
                                                      static_cast<vk::DeviceSize>(plannedSection.stride),
                                                      "SBT record offset");
            auto dstOffset = nr::checkedAdd(plannedSection.offset, recordOffset, "SBT record destination");
            auto dstStart = static_cast<std::size_t>(dstOffset);
            nrAssert(dstStart + static_cast<std::size_t>(plan.handleSize) <= tableBytes.size(),
                     "ShaderBindingTable::create destination handle copy range overflow.");
            nrAssert(dstStart + static_cast<std::size_t>(plan.handleSize) + recordData.size() <= tableBytes.size(),
                     "ShaderBindingTable::create destination record data copy range overflow.");

            auto handle = desc.pipeline.shaderGroupHandles(shaderGroupIndex, 1);
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

        if (!section.records.empty())
        {
            auto recordIndices = std::views::iota(std::uint32_t{0}, sectionRecordCount);
            std::ranges::for_each(recordIndices, [&](std::uint32_t recordIndex) {
                const auto &record = section.records[recordIndex];
                copyRecord(recordIndex, record.groupIndex, record.data);
            });
            return;
        }

        auto handles = desc.pipeline.shaderGroupHandles(plannedSection.firstGroup, sectionRecordCount);
        auto groupIndices = std::views::iota(std::uint32_t{0}, sectionRecordCount);
        std::ranges::for_each(groupIndices, [&](std::uint32_t groupIndex) {
            auto const recordOffset = nr::checkedMultiply(static_cast<vk::DeviceSize>(groupIndex),
                                                      static_cast<vk::DeviceSize>(plannedSection.stride),
                                                      "SBT group record offset");
            auto dstOffset = nr::checkedAdd(plannedSection.offset, recordOffset, "SBT group record destination");
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

    nrAssert(plan.totalSize <= static_cast<vk::DeviceSize>(std::numeric_limits<std::size_t>::max()),
             "ShaderBindingTable::create total size exceeds host address space.");
    auto tableBytes = std::vector<std::uint8_t>(static_cast<std::size_t>(plan.totalSize), std::uint8_t{0});
    packSection(tableBytes, desc.raygen, plan.raygen);
    packSection(tableBytes, desc.miss, plan.miss);
    packSection(tableBytes, desc.hit, plan.hit);
    packSection(tableBytes, desc.callable, plan.callable);

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
    sbt.pipelineIdentity_ = desc.pipeline.identity();
    sbt.buffer_ = std::move(buffer);

    sbt.raygenRegion_ = rt_detail::buildRegion(baseAddress, plan.raygen);
    sbt.missRegion_ = rt_detail::buildRegion(baseAddress, plan.miss);
    sbt.hitRegion_ = rt_detail::buildRegion(baseAddress, plan.hit);
    sbt.callableRegion_ = rt_detail::buildRegion(baseAddress, plan.callable);

    auto baseAlignment = static_cast<vk::DeviceSize>(desc.pipeline.capabilities().shaderGroupBaseAlignment);
    auto checkRegionAlignment = [&](std::string_view label, const vk::StridedDeviceAddressRegionKHR &region) {
        if (region.deviceAddress == 0)
        {
            return;
        }
        nrAssert((region.deviceAddress % baseAlignment) == 0,
                 "ShaderBindingTable::create {} deviceAddress is not shaderGroupBaseAlignment aligned.", label);
    };

    checkRegionAlignment("raygen", sbt.raygenRegion_);
    checkRegionAlignment("miss", sbt.missRegion_);
    checkRegionAlignment("hit", sbt.hitRegion_);
    checkRegionAlignment("callable", sbt.callableRegion_);

    return sbt;
}

[[nodiscard]] bool ShaderBindingTable::valid() const noexcept
{
    return static_cast<bool>(pipelineIdentity_) && buffer_.valid();
}

[[nodiscard]] RayTracingPipelineIdentity ShaderBindingTable::pipelineIdentity() const noexcept
{
    return pipelineIdentity_;
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

void traceRays(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysDesc &desc)
{
    nrAssert(*commandBuffer != nullptr, "traceRays requires a valid command buffer.");
    nrAssert(desc.pipeline.valid(), "traceRays requires a valid ray tracing pipeline.");
    nrAssert(desc.shaderBindingTable.valid(), "traceRays requires a valid shader binding table.");
    nrAssert(desc.shaderBindingTable.pipelineIdentity() == desc.pipeline.identity(),
             "traceRays requires an SBT created from the bound ray tracing pipeline.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer,
             "traceRays requires a queue family that supports compute operations.");

    auto diagnostics = rt_detail::validateTraceRaysDispatch(desc.dimensions, desc.pipeline.capabilities());
    nrAssert(diagnostics.isValid, "traceRays invalid dispatch: {}", diagnostics.message);

    auto regions = desc.shaderBindingTable.regions();
    nrAssert(regions.raygen.size == regions.raygen.stride, "traceRays requires raygen SBT region size == stride.");

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline.raw());
    commandBuffer.traceRaysKHR(regions.raygen, regions.miss, regions.hit, regions.callable, desc.dimensions.width,
                               desc.dimensions.height, desc.dimensions.depth);
}
} // namespace nr::rhi

namespace nr::rhi::rt_detail
{
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

    auto minimumStride = nr::checkedAdd(static_cast<vk::DeviceSize>(capabilities.shaderGroupHandleSize),
                                    static_cast<vk::DeviceSize>(maxRecordDataSize(section)),
                                    "SBT minimum record stride");
    nrAssert(minimumStride <= static_cast<vk::DeviceSize>(std::numeric_limits<std::uint32_t>::max()),
             "SBT record stride exceeds uint32_t range.");
    return static_cast<std::uint32_t>(
        nr::checkedAlignUp(minimumStride, static_cast<vk::DeviceSize>(capabilities.shaderGroupHandleAlignment),
                       "SBT record stride alignment"));
}

[[nodiscard]] ValidationResult validateSection(std::string_view label, const ShaderBindingTableSectionDesc &section,
                                               std::uint32_t effectiveSectionStride,
                                               const RayTracingCapabilitySnapshot &capabilities,
                                               std::uint32_t pipelineGroupCount)
{
    const auto sectionRecordCount = recordCount(section);
    if (sectionRecordCount == 0)
    {
        return nr::rhi::detail::validationSuccess();
    }

    if (!section.records.empty() && section.groupCount != 0 && section.groupCount != sectionRecordCount)
    {
        return nr::rhi::detail::validationFailure(
            nr::rhi::detail::formatMessage("{} groupCount ({}) must be 0 or match records.size() ({}) when records are provided.", label,
                          section.groupCount, sectionRecordCount));
    }

    auto requiredStride = static_cast<std::uint64_t>(capabilities.shaderGroupHandleSize) +
                          static_cast<std::uint64_t>(maxRecordDataSize(section));
    if (requiredStride > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("{} record payload makes stride exceed uint32_t range.", label));
    }

    if (effectiveSectionStride < requiredStride)
    {
        return nr::rhi::detail::validationFailure(
            nr::rhi::detail::formatMessage("{} stride ({}) must be >= shaderGroupHandleSize + max record payload ({}).", label,
                          effectiveSectionStride, requiredStride));
    }

    if ((effectiveSectionStride % capabilities.shaderGroupHandleAlignment) != 0)
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("{} stride ({}) must be aligned to shaderGroupHandleAlignment ({}).",
                                               label, effectiveSectionStride, capabilities.shaderGroupHandleAlignment));
    }

    if (effectiveSectionStride > capabilities.maxShaderGroupStride)
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("{} stride ({}) exceeds maxShaderGroupStride ({}).", label,
                                               effectiveSectionStride, capabilities.maxShaderGroupStride));
    }

    if (section.records.empty())
    {
        auto groupEnd = static_cast<std::uint64_t>(section.firstGroup) + static_cast<std::uint64_t>(section.groupCount);
        if (groupEnd > static_cast<std::uint64_t>(pipelineGroupCount))
        {
            return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("{} group range [{}..{}) exceeds pipeline group count ({}).", label,
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
            return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("{} record group index ({}) exceeds pipeline group count ({}).",
                                                   label, invalidRecordIt->groupIndex, pipelineGroupCount));
        }
    }

    return nr::rhi::detail::validationSuccess();
}

[[nodiscard]] vk::DeviceSize sectionSize(const ShaderBindingTableSectionDesc &section)
{
    return nr::checkedMultiply(static_cast<vk::DeviceSize>(recordCount(section)),
                           static_cast<vk::DeviceSize>(section.stride), "SBT section size");
}

[[nodiscard]] std::array<ShaderBindingTableBuildPlanSection, 4> buildSectionPlan(
    const ShaderBindingTableLayoutDesc &desc)
{
    auto sourceSections = std::array{
        std::cref(desc.raygen),
        std::cref(desc.miss),
        std::cref(desc.hit),
        std::cref(desc.callable),
    };
    auto sections = std::array<ShaderBindingTableBuildPlanSection, 4>{};

    auto runningOffset = vk::DeviceSize{0};
    auto indices = std::views::iota(std::size_t{0}, sections.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const &source = sourceSections[index].get();
        auto &plannedSection = sections[index];
        plannedSection.firstGroup = source.firstGroup;
        plannedSection.recordCount = recordCount(source);
        plannedSection.stride = source.stride;
        if (plannedSection.recordCount == 0)
        {
            plannedSection.stride = 0;
            return;
        }

        runningOffset = nr::checkedAlignUp(runningOffset,
                                       static_cast<vk::DeviceSize>(desc.capabilities.shaderGroupBaseAlignment),
                                       "SBT section base alignment");
        plannedSection.offset = runningOffset;
        plannedSection.size = sectionSize(source);
        runningOffset = nr::checkedAdd(runningOffset, plannedSection.size, "SBT accumulated size");
    });

    return sections;
}

[[nodiscard]] vk::StridedDeviceAddressRegionKHR buildRegion(vk::DeviceAddress baseAddress,
                                                            const ShaderBindingTableBuildPlanSection &section)
{
    if (section.recordCount == 0)
    {
        return vk::StridedDeviceAddressRegionKHR{};
    }

    vk::StridedDeviceAddressRegionKHR region{};
    region.deviceAddress = nr::checkedAdd(baseAddress, section.offset, "SBT region device address");
    region.stride = section.stride;
    region.size = section.size;
    return region;
}

[[nodiscard]] ValidationResult validateShaderBindingTableBuildDesc(const ShaderBindingTableBuildDesc &desc)
{
    if (!desc.pipeline.valid())
    {
        return nr::rhi::detail::validationFailure("ShaderBindingTableBuildDesc requires a valid ray tracing pipeline.");
    }

    auto layoutDesc = ShaderBindingTableLayoutDesc{
        .capabilities = desc.pipeline.capabilities(),
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
        return nr::rhi::detail::validationFailure("shaderGroupHandleSize must be > 0.");
    }

    if (desc.capabilities.shaderGroupHandleAlignment == 0)
    {
        return nr::rhi::detail::validationFailure("shaderGroupHandleAlignment must be > 0.");
    }

    if (desc.capabilities.shaderGroupBaseAlignment == 0)
    {
        return nr::rhi::detail::validationFailure("shaderGroupBaseAlignment must be > 0.");
    }

    if (desc.capabilities.maxShaderGroupStride == 0)
    {
        return nr::rhi::detail::validationFailure("maxShaderGroupStride must be > 0.");
    }

    if (desc.pipelineGroupCount == 0)
    {
        return nr::rhi::detail::validationFailure("pipelineGroupCount must be > 0.");
    }

    if (recordCount(desc.raygen) != 1)
    {
        return nr::rhi::detail::validationFailure("raygen section must contain exactly one record so size == stride.");
    }

    auto raygen = desc.raygen;
    auto miss = desc.miss;
    auto hit = desc.hit;
    auto callable = desc.callable;

    raygen.stride = effectiveStride(raygen, desc.capabilities);
    miss.stride = effectiveStride(miss, desc.capabilities);
    hit.stride = effectiveStride(hit, desc.capabilities);
    callable.stride = effectiveStride(callable, desc.capabilities);

    auto groupCount = desc.pipelineGroupCount;

    auto raygenValidation = validateSection("raygen", raygen, raygen.stride, desc.capabilities, groupCount);
    if (!raygenValidation.isValid)
    {
        return raygenValidation;
    }

    auto missValidation = validateSection("miss", miss, miss.stride, desc.capabilities, groupCount);
    if (!missValidation.isValid)
    {
        return missValidation;
    }

    auto hitValidation = validateSection("hit", hit, hit.stride, desc.capabilities, groupCount);
    if (!hitValidation.isValid)
    {
        return hitValidation;
    }

    auto callableValidation =
        validateSection("callable", callable, callable.stride, desc.capabilities, groupCount);
    if (!callableValidation.isValid)
    {
        return callableValidation;
    }

    auto raygenSize = sectionSize(raygen);
    if (raygenSize != raygen.stride)
    {
        return nr::rhi::detail::validationFailure("raygen section requires size == stride.");
    }

    return nr::rhi::detail::validationSuccess();
}

[[nodiscard]] ValidationResult validateTraceRaysDispatch(const TraceRaysDimensions &dimensions,
                                                         const RayTracingCapabilitySnapshot &capabilities)
{
    if (dimensions.width == 0 || dimensions.height == 0 || dimensions.depth == 0)
    {
        return nr::rhi::detail::validationFailure("traceRays dimensions must all be > 0.");
    }

    auto dispatchWidth = static_cast<std::uint64_t>(dimensions.width);
    auto dispatchHeight = static_cast<std::uint64_t>(dimensions.height);
    auto dispatchDepth = static_cast<std::uint64_t>(dimensions.depth);

    if (capabilities.maxDispatchDimensions[0] > 0 && dispatchWidth > capabilities.maxDispatchDimensions[0])
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("traceRays width ({}) exceeds max dispatch width ({}).",
                                               dimensions.width, capabilities.maxDispatchDimensions[0]));
    }

    if (capabilities.maxDispatchDimensions[1] > 0 && dispatchHeight > capabilities.maxDispatchDimensions[1])
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("traceRays height ({}) exceeds max dispatch height ({}).",
                                               dimensions.height, capabilities.maxDispatchDimensions[1]));
    }

    if (capabilities.maxDispatchDimensions[2] > 0 && dispatchDepth > capabilities.maxDispatchDimensions[2])
    {
        return nr::rhi::detail::validationFailure(nr::rhi::detail::formatMessage("traceRays depth ({}) exceeds max dispatch depth ({}).",
                                               dimensions.depth, capabilities.maxDispatchDimensions[2]));
    }

    auto invocationCount = dispatchWidth * dispatchHeight * dispatchDepth;
    if (capabilities.maxRayDispatchInvocationCount > 0 && invocationCount > capabilities.maxRayDispatchInvocationCount)
    {
        return nr::rhi::detail::validationFailure(
            nr::rhi::detail::formatMessage("traceRays invocation count ({}) exceeds maxRayDispatchInvocationCount ({}).",
                          invocationCount, capabilities.maxRayDispatchInvocationCount));
    }

    return nr::rhi::detail::validationSuccess();
}

} // namespace nr::rhi::rt_detail
