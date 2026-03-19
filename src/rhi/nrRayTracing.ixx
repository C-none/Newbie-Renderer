module;
export module nr.rhi:rayTracing;
import dependency;
import :type;
import :pipeline;
import :resourcePool;
import nr.utils;
import std;

export namespace nr::rhi
{
struct ShaderBindingTableSectionDesc
{
    uint32_t firstGroup = 0;
    uint32_t groupCount = 0;
    uint32_t stride = 0;
};

struct ShaderBindingTableBuildDesc
{
    const RayTracingPipeline *pipeline = nullptr;
    RayTracingCapabilitySnapshot capabilities{};
    ShaderBindingTableSectionDesc raygen{.firstGroup = 0, .groupCount = 1, .stride = 0};
    ShaderBindingTableSectionDesc miss{};
    ShaderBindingTableSectionDesc hit{};
    ShaderBindingTableSectionDesc callable{};
    std::string debugName = "rt_sbt";
};

struct ShaderBindingTableLayoutDesc
{
    RayTracingCapabilitySnapshot capabilities{};
    uint32_t pipelineGroupCount = 0;
    ShaderBindingTableSectionDesc raygen{.firstGroup = 0, .groupCount = 1, .stride = 0};
    ShaderBindingTableSectionDesc miss{};
    ShaderBindingTableSectionDesc hit{};
    ShaderBindingTableSectionDesc callable{};
};

using ShaderBindingTableBuildDiagnostics = ValidationDiagnostics;

struct ShaderBindingTableBuildPlanSection
{
    ShaderBindingTableSectionDesc section{};
    vk::DeviceSize offset = 0;
    vk::DeviceSize size = 0;
};

struct ShaderBindingTableBuildPlan
{
    vk::DeviceSize totalSize = 0;
    uint32_t handleSize = 0;
    uint32_t handleAlignment = 1;
    uint32_t baseAlignment = 1;
    ShaderBindingTableBuildPlanSection raygen{};
    ShaderBindingTableBuildPlanSection miss{};
    ShaderBindingTableBuildPlanSection hit{};
    ShaderBindingTableBuildPlanSection callable{};
};

struct TraceRaysDimensions
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
};

using TraceRaysDispatchDiagnostics = ValidationDiagnostics;

class ShaderBindingTable;

struct TraceRaysDesc
{
    const RayTracingPipeline *pipeline = nullptr;
    const ShaderBindingTable *shaderBindingTable = nullptr;
    TraceRaysDimensions dimensions{};
    QueueRole recordingQueueRole = QueueRole::Compute;
};

struct TraceRaysIndirectDesc
{
    const RayTracingPipeline *pipeline = nullptr;
    const ShaderBindingTable *shaderBindingTable = nullptr;
    vk::DeviceAddress indirectDeviceAddress = 0;
    QueueRole recordingQueueRole = QueueRole::Compute;
};

[[nodiscard]] inline vk::DeviceSize alignUp(vk::DeviceSize value, vk::DeviceSize alignment)
{
    nrAssert(alignment > 0, "alignUp requires alignment > 0.");
    const auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value + (alignment - remainder);
}

[[nodiscard]] inline uint32_t alignUp(uint32_t value, uint32_t alignment)
{
    nrAssert(alignment > 0, "alignUp requires alignment > 0.");
    const auto remainder = value % alignment;
    if (remainder == 0)
    {
        return value;
    }
    return value + (alignment - remainder);
}

namespace rt_detail
{
[[nodiscard]] inline uint32_t effectiveStride(const ShaderBindingTableSectionDesc &section, const RayTracingCapabilitySnapshot &capabilities)
{
    if (section.groupCount == 0)
    {
        return 0;
    }

    if (section.stride != 0)
    {
        return section.stride;
    }

    return alignUp(capabilities.shaderGroupHandleSize, capabilities.shaderGroupHandleAlignment);
}

[[nodiscard]] inline ShaderBindingTableBuildDiagnostics validateSection(
    std::string_view label,
    const ShaderBindingTableSectionDesc &section,
    uint32_t effectiveSectionStride,
    const RayTracingCapabilitySnapshot &capabilities,
    uint32_t pipelineGroupCount)
{
    if (section.groupCount == 0)
    {
        return makeValidationSuccess();
    }

    if (effectiveSectionStride < capabilities.shaderGroupHandleSize)
    {
        return makeValidationFailure(std::format("{} stride ({}) must be >= shaderGroupHandleSize ({}).", label, effectiveSectionStride, capabilities.shaderGroupHandleSize));
    }

    if ((effectiveSectionStride % capabilities.shaderGroupHandleAlignment) != 0)
    {
        return makeValidationFailure(std::format("{} stride ({}) must be aligned to shaderGroupHandleAlignment ({}).", label, effectiveSectionStride, capabilities.shaderGroupHandleAlignment));
    }

    if (effectiveSectionStride > capabilities.maxShaderGroupStride)
    {
        return makeValidationFailure(std::format("{} stride ({}) exceeds maxShaderGroupStride ({}).", label, effectiveSectionStride, capabilities.maxShaderGroupStride));
    }

    auto groupEnd = static_cast<uint64_t>(section.firstGroup) + static_cast<uint64_t>(section.groupCount);
    if (groupEnd > static_cast<uint64_t>(pipelineGroupCount))
    {
        return makeValidationFailure(std::format("{} group range [{}..{}) exceeds pipeline group count ({}).", label, section.firstGroup, groupEnd, pipelineGroupCount));
    }

    return makeValidationSuccess();
}

[[nodiscard]] inline vk::DeviceSize sectionSize(const ShaderBindingTableSectionDesc &section)
{
    return static_cast<vk::DeviceSize>(section.groupCount) * static_cast<vk::DeviceSize>(section.stride);
}

[[nodiscard]] inline std::array<ShaderBindingTableBuildPlanSection, 4> buildSectionPlan(const ShaderBindingTableLayoutDesc &desc)
{
    auto sections = std::array<ShaderBindingTableBuildPlanSection, 4>{
        ShaderBindingTableBuildPlanSection{.section = desc.raygen},
        ShaderBindingTableBuildPlanSection{.section = desc.miss},
        ShaderBindingTableBuildPlanSection{.section = desc.hit},
        ShaderBindingTableBuildPlanSection{.section = desc.callable},
    };

    auto runningOffset = vk::DeviceSize{0};
    std::ranges::for_each(sections, [&](ShaderBindingTableBuildPlanSection &plannedSection) {
        if (plannedSection.section.groupCount == 0)
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

[[nodiscard]] inline vk::StridedDeviceAddressRegionKHR buildRegion(vk::DeviceAddress baseAddress, const ShaderBindingTableBuildPlanSection &section)
{
    if (section.section.groupCount == 0)
    {
        return vk::StridedDeviceAddressRegionKHR{};
    }

    vk::StridedDeviceAddressRegionKHR region{};
    region.deviceAddress = baseAddress + section.offset;
    region.stride = section.section.stride;
    region.size = section.size;
    return region;
}
} // namespace rt_detail

[[nodiscard]] inline ShaderBindingTableBuildDiagnostics validateShaderBindingTableLayoutDesc(const ShaderBindingTableLayoutDesc &desc);

[[nodiscard]] inline ShaderBindingTableBuildDiagnostics validateShaderBindingTableBuildDesc(const ShaderBindingTableBuildDesc &desc)
{
    if (desc.pipeline == nullptr || !desc.pipeline->valid())
    {
        return makeValidationFailure("ShaderBindingTableBuildDesc requires a valid ray tracing pipeline.");
    }

    auto layoutDesc = ShaderBindingTableLayoutDesc{
        .capabilities = desc.capabilities,
        .pipelineGroupCount = desc.pipeline->shaderGroupCount(),
        .raygen = desc.raygen,
        .miss = desc.miss,
        .hit = desc.hit,
        .callable = desc.callable,
    };

    return validateShaderBindingTableLayoutDesc(layoutDesc);
}

[[nodiscard]] inline ShaderBindingTableBuildDiagnostics validateShaderBindingTableLayoutDesc(const ShaderBindingTableLayoutDesc &desc)
{
    if (desc.capabilities.shaderGroupHandleSize == 0)
    {
        return makeValidationFailure("shaderGroupHandleSize must be > 0.");
    }

    if (desc.capabilities.shaderGroupHandleAlignment == 0)
    {
        return makeValidationFailure("shaderGroupHandleAlignment must be > 0.");
    }

    if (desc.capabilities.shaderGroupBaseAlignment == 0)
    {
        return makeValidationFailure("shaderGroupBaseAlignment must be > 0.");
    }

    if (desc.capabilities.maxShaderGroupStride == 0)
    {
        return makeValidationFailure("maxShaderGroupStride must be > 0.");
    }

    if (desc.pipelineGroupCount == 0)
    {
        return makeValidationFailure("pipelineGroupCount must be > 0.");
    }

    if (desc.raygen.groupCount != 1)
    {
        return makeValidationFailure("raygen section must contain exactly one group so size == stride.");
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

    auto callableValidation = rt_detail::validateSection("callable", callable, callable.stride, desc.capabilities, groupCount);
    if (!callableValidation.isValid)
    {
        return callableValidation;
    }

    auto raygenSize = rt_detail::sectionSize(raygen);
    if (raygenSize != raygen.stride)
    {
        return makeValidationFailure("raygen section requires size == stride.");
    }

    return makeValidationSuccess();
}

[[nodiscard]] inline ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableLayoutDesc &desc)
{
    auto validation = validateShaderBindingTableLayoutDesc(desc);
    nrAssert(validation.isValid, std::format("makeShaderBindingTableBuildPlan invalid desc: {}", validation.message));

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

[[nodiscard]] inline ShaderBindingTableBuildPlan makeShaderBindingTableBuildPlan(const ShaderBindingTableBuildDesc &desc)
{
    auto validation = validateShaderBindingTableBuildDesc(desc);
    nrAssert(validation.isValid, std::format("makeShaderBindingTableBuildPlan invalid desc: {}", validation.message));

    return makeShaderBindingTableBuildPlan(ShaderBindingTableLayoutDesc{
        .capabilities = desc.capabilities,
        .pipelineGroupCount = desc.pipeline->shaderGroupCount(),
        .raygen = desc.raygen,
        .miss = desc.miss,
        .hit = desc.hit,
        .callable = desc.callable,
    });
}

class ShaderBindingTable
{
  public:
    struct Regions
    {
        vk::StridedDeviceAddressRegionKHR raygen{};
        vk::StridedDeviceAddressRegionKHR miss{};
        vk::StridedDeviceAddressRegionKHR hit{};
        vk::StridedDeviceAddressRegionKHR callable{};
    };

    ShaderBindingTable() = default;
    ShaderBindingTable(const ShaderBindingTable &) = delete;
    ShaderBindingTable &operator=(const ShaderBindingTable &) = delete;
    ShaderBindingTable(ShaderBindingTable &&) noexcept = default;
    ShaderBindingTable &operator=(ShaderBindingTable &&) noexcept = default;

    [[nodiscard]] static ShaderBindingTable create(const ResourceFactory &resourceFactory, const ShaderBindingTableBuildDesc &desc)
    {
        auto validation = validateShaderBindingTableBuildDesc(desc);
        nrAssert(validation.isValid, std::format("ShaderBindingTable::create invalid desc: {}", validation.message));

        auto plan = makeShaderBindingTableBuildPlan(desc);
        nrAssert(plan.totalSize > 0, "ShaderBindingTable::create requires totalSize > 0.");

        auto packSection = [&](std::span<uint8_t> tableBytes, const ShaderBindingTableBuildPlanSection &plannedSection) {
            if (plannedSection.section.groupCount == 0)
            {
                return;
            }

            auto handles = desc.pipeline->shaderGroupHandles(plannedSection.section.firstGroup, plannedSection.section.groupCount, plan.handleSize);
            auto groupIndices = std::views::iota(uint32_t{0}, plannedSection.section.groupCount);
            std::ranges::for_each(groupIndices, [&](uint32_t groupIndex) {
                auto dstOffset = plannedSection.offset + (static_cast<vk::DeviceSize>(groupIndex) * plannedSection.section.stride);
                auto srcOffset = static_cast<size_t>(groupIndex) * static_cast<size_t>(plan.handleSize);

                auto dstStart = static_cast<size_t>(dstOffset);
                nrAssert(dstStart + static_cast<size_t>(plan.handleSize) <= tableBytes.size(), "ShaderBindingTable::create destination copy range overflow.");
                nrAssert(srcOffset + static_cast<size_t>(plan.handleSize) <= handles.size(), "ShaderBindingTable::create source handle range overflow.");

                auto srcView = std::span<const uint8_t>(handles).subspan(srcOffset, plan.handleSize);
                auto dstView = tableBytes.subspan(dstStart, plan.handleSize);
                std::ranges::copy(srcView, dstView.begin());
            });
        };

        auto tableBytes = std::vector<uint8_t>(static_cast<size_t>(plan.totalSize), uint8_t{0});
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

        buffer.write(std::span<const uint8_t>(tableBytes));
        buffer.flush(0, plan.totalSize);

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
            nrAssert((region.deviceAddress % baseAlignment) == 0, std::format("ShaderBindingTable::create {} deviceAddress is not shaderGroupBaseAlignment aligned.", label));
        };

        checkRegionAlignment("raygen", sbt.raygenRegion_);
        checkRegionAlignment("miss", sbt.missRegion_);
        checkRegionAlignment("hit", sbt.hitRegion_);
        checkRegionAlignment("callable", sbt.callableRegion_);

        return sbt;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return buffer_.valid();
    }

    [[nodiscard]] const Buffer &buffer() const noexcept
    {
        return buffer_;
    }

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &raygenRegion() const noexcept
    {
        return raygenRegion_;
    }

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &missRegion() const noexcept
    {
        return missRegion_;
    }

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &hitRegion() const noexcept
    {
        return hitRegion_;
    }

    [[nodiscard]] const vk::StridedDeviceAddressRegionKHR &callableRegion() const noexcept
    {
        return callableRegion_;
    }

    [[nodiscard]] Regions regions() const noexcept
    {
        return Regions{
            .raygen = raygenRegion_,
            .miss = missRegion_,
            .hit = hitRegion_,
            .callable = callableRegion_,
        };
    }

  private:
    Buffer buffer_{};
    vk::StridedDeviceAddressRegionKHR raygenRegion_{};
    vk::StridedDeviceAddressRegionKHR missRegion_{};
    vk::StridedDeviceAddressRegionKHR hitRegion_{};
    vk::StridedDeviceAddressRegionKHR callableRegion_{};
};

[[nodiscard]] inline TraceRaysDispatchDiagnostics validateTraceRaysDispatch(const TraceRaysDimensions &dimensions, const RayTracingCapabilitySnapshot &capabilities)
{
    if (dimensions.width == 0 || dimensions.height == 0 || dimensions.depth == 0)
    {
        return makeValidationFailure("traceRays dimensions must all be > 0.");
    }

    auto dispatchWidth = static_cast<uint64_t>(dimensions.width);
    auto dispatchHeight = static_cast<uint64_t>(dimensions.height);
    auto dispatchDepth = static_cast<uint64_t>(dimensions.depth);

    if (capabilities.maxDispatchDimensions[0] > 0 && dispatchWidth > capabilities.maxDispatchDimensions[0])
    {
        return makeValidationFailure(std::format("traceRays width ({}) exceeds max dispatch width ({}).", dimensions.width, capabilities.maxDispatchDimensions[0]));
    }

    if (capabilities.maxDispatchDimensions[1] > 0 && dispatchHeight > capabilities.maxDispatchDimensions[1])
    {
        return makeValidationFailure(std::format("traceRays height ({}) exceeds max dispatch height ({}).", dimensions.height, capabilities.maxDispatchDimensions[1]));
    }

    if (capabilities.maxDispatchDimensions[2] > 0 && dispatchDepth > capabilities.maxDispatchDimensions[2])
    {
        return makeValidationFailure(std::format("traceRays depth ({}) exceeds max dispatch depth ({}).", dimensions.depth, capabilities.maxDispatchDimensions[2]));
    }

    auto invocationCount = dispatchWidth * dispatchHeight * dispatchDepth;
    if (capabilities.maxRayDispatchInvocationCount > 0 && invocationCount > capabilities.maxRayDispatchInvocationCount)
    {
        return makeValidationFailure(std::format(
            "traceRays invocation count ({}) exceeds maxRayDispatchInvocationCount ({}).",
            invocationCount,
            capabilities.maxRayDispatchInvocationCount));
    }

    return makeValidationSuccess();
}

[[nodiscard]] inline TraceRaysDispatchDiagnostics validateTraceRaysIndirect(vk::DeviceAddress indirectDeviceAddress, const RayTracingCapabilitySnapshot &capabilities)
{
    if (!capabilities.rayTracingPipelineTraceRaysIndirect)
    {
        return makeValidationFailure("traceRaysIndirect requires rayTracingPipelineTraceRaysIndirect feature.");
    }

    if (indirectDeviceAddress == 0)
    {
        return makeValidationFailure("traceRaysIndirect requires a non-zero indirect device address.");
    }

    if ((indirectDeviceAddress % 4u) != 0)
    {
        return makeValidationFailure("traceRaysIndirect indirect device address must be 4-byte aligned.");
    }

    return makeValidationSuccess();
}

inline void traceRays(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysDesc &desc, const RayTracingCapabilitySnapshot &capabilities)
{
    nrAssert(*commandBuffer != nullptr, "traceRays requires a valid command buffer.");
    nrAssert(desc.pipeline != nullptr && desc.pipeline->valid(), "traceRays requires a valid ray tracing pipeline.");
    nrAssert(desc.shaderBindingTable != nullptr && desc.shaderBindingTable->valid(), "traceRays requires a valid shader binding table.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer, "traceRays requires a queue family that supports compute operations.");

    auto diagnostics = validateTraceRaysDispatch(desc.dimensions, capabilities);
    nrAssert(diagnostics.isValid, std::format("traceRays invalid dispatch: {}", diagnostics.message));

    auto regions = desc.shaderBindingTable->regions();
    nrAssert(regions.raygen.size == regions.raygen.stride, "traceRays requires raygen SBT region size == stride.");

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline->raw());
    commandBuffer.traceRaysKHR(
        regions.raygen,
        regions.miss,
        regions.hit,
        regions.callable,
        desc.dimensions.width,
        desc.dimensions.height,
        desc.dimensions.depth);
}

inline void traceRaysIndirect(const vk::raii::CommandBuffer &commandBuffer, const TraceRaysIndirectDesc &desc, const RayTracingCapabilitySnapshot &capabilities)
{
    nrAssert(*commandBuffer != nullptr, "traceRaysIndirect requires a valid command buffer.");
    nrAssert(desc.pipeline != nullptr && desc.pipeline->valid(), "traceRaysIndirect requires a valid ray tracing pipeline.");
    nrAssert(desc.shaderBindingTable != nullptr && desc.shaderBindingTable->valid(), "traceRaysIndirect requires a valid shader binding table.");
    nrAssert(desc.recordingQueueRole != QueueRole::Transfer, "traceRaysIndirect requires a queue family that supports compute operations.");

    auto diagnostics = validateTraceRaysIndirect(desc.indirectDeviceAddress, capabilities);
    nrAssert(diagnostics.isValid, std::format("traceRaysIndirect invalid arguments: {}", diagnostics.message));

    auto regions = desc.shaderBindingTable->regions();
    nrAssert(regions.raygen.size == regions.raygen.stride, "traceRaysIndirect requires raygen SBT region size == stride.");

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eRayTracingKHR, desc.pipeline->raw());
    commandBuffer.traceRaysIndirectKHR(
        regions.raygen,
        regions.miss,
        regions.hit,
        regions.callable,
        desc.indirectDeviceAddress);
}

} // namespace nr::rhi
