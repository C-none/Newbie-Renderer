import std;
import dependency.vulkan;
import nr.rhi;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] nr::rhi::RayTracingCapabilitySnapshot testCapabilities()
{
    return nr::rhi::RayTracingCapabilitySnapshot{
        .rayTracingMaintenance1 = true,
        .rayTracingPipelineTraceRaysIndirect = true,
        .rayTracingPipelineTraceRaysIndirect2 = true,
        .shaderGroupHandleSize = 32,
        .shaderGroupHandleAlignment = 16,
        .shaderGroupBaseAlignment = 64,
        .maxShaderGroupStride = 128,
        .maxRayDispatchInvocationCount = 1024,
        .maxRayRecursionDepth = 2,
        .maxDispatchDimensions = {64, 64, 1},
    };
}

const nr::test::CaseRegistrar alignCase{
    "rhi rt alignUp handles device-size and uint32 inputs", [] {
        nr::test::requireEqual(nr::rhi::alignUp(vk::DeviceSize{0}, vk::DeviceSize{16}), vk::DeviceSize{0});
        nr::test::requireEqual(nr::rhi::alignUp(vk::DeviceSize{17}, vk::DeviceSize{16}), vk::DeviceSize{32});
        nr::test::requireEqual(nr::rhi::alignUp(std::uint32_t{33}, std::uint32_t{32}), std::uint32_t{64});
    }};

const nr::test::CaseRegistrar deferredHostWorkerPolicyCase{
    "rhi rt deferred host creation rejects incompatible early-return policy", [] {
        auto defaultDesc = nr::rhi::RayTracingPipelineDesc{};
        nr::test::require(!nr::rhi::validateRayTracingPipelineDesc(defaultDesc).has_value());

        auto earlyReturnDesc = nr::rhi::RayTracingPipelineDesc{};
        earlyReturnDesc.flags = vk::PipelineCreateFlagBits::eEarlyReturnOnFailure;
        nr::test::require(nr::rhi::validateRayTracingPipelineDesc(earlyReturnDesc).has_value());
    }};

const nr::test::CaseRegistrar sbtPlanCase{
    "rhi rt SBT layout plan aligns sections and record payloads", [] {
        auto payloadA = std::array<std::uint8_t, 12>{};
        auto payloadB = std::array<std::uint8_t, 20>{};
        auto missRecords = std::array{
            nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = 1, .data = std::span<const std::uint8_t>{payloadA}},
            nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = 2, .data = std::span<const std::uint8_t>{payloadB}},
        };

        auto desc = nr::rhi::ShaderBindingTableLayoutDesc{
            .capabilities = testCapabilities(),
            .pipelineGroupCount = 4,
            .raygen = nr::rhi::ShaderBindingTableSectionDesc{.groupCount = 1},
            .miss = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 1, .records = missRecords},
            .hit = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 3, .groupCount = 1},
        };

        auto plan = nr::rhi::makeShaderBindingTableBuildPlan(desc);
        nr::test::requireEqual(plan.handleSize, 32u);
        nr::test::requireEqual(plan.handleAlignment, 16u);
        nr::test::requireEqual(plan.baseAlignment, 64u);
        nr::test::requireEqual(plan.raygen.offset, vk::DeviceSize{0});
        nr::test::requireEqual(plan.raygen.section.stride, 32u);
        nr::test::requireEqual(plan.raygen.size, vk::DeviceSize{32});
        nr::test::requireEqual(plan.miss.offset, vk::DeviceSize{64});
        nr::test::requireEqual(plan.miss.section.stride, 64u);
        nr::test::requireEqual(plan.miss.size, vk::DeviceSize{128});
        nr::test::requireEqual(plan.hit.offset, vk::DeviceSize{192});
        nr::test::requireEqual(plan.hit.section.stride, 32u);
        nr::test::requireEqual(plan.totalSize, vk::DeviceSize{256});
    }};

const nr::test::CaseRegistrar sbtRepeatedGroupRecordsCase{
    "rhi rt SBT explicit records preserve repeated shader group indices", [] {
        auto hitRecords = std::array{
            nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = 2},
            nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = 2},
            nr::rhi::ShaderBindingTableRecordDesc{.groupIndex = 3},
        };

        auto desc = nr::rhi::ShaderBindingTableLayoutDesc{
            .capabilities = testCapabilities(),
            .pipelineGroupCount = 4,
            .raygen = nr::rhi::ShaderBindingTableSectionDesc{.groupCount = 1},
            .hit = nr::rhi::ShaderBindingTableSectionDesc{.records = hitRecords},
        };

        auto plan = nr::rhi::makeShaderBindingTableBuildPlan(desc);
        nr::test::requireEqual(plan.hit.section.records.size(), std::size_t{3u});
        nr::test::requireEqual(plan.hit.section.records[0].groupIndex, 2u);
        nr::test::requireEqual(plan.hit.section.records[1].groupIndex, 2u);
        nr::test::requireEqual(plan.hit.section.records[2].groupIndex, 3u);
        nr::test::requireEqual(plan.hit.section.stride, 32u);
        nr::test::requireEqual(plan.hit.size, vk::DeviceSize{96});
    }};
} // namespace
