import std;
import dependency;
import nr.renderPasses;
import nr.test;

namespace
{
const nr::test::CaseRegistrar asBuildInputCase{
    "renderpasses AS build input defaults to one hit SBT record",
    [] {
        auto input = nr::renderPasses::AccelerationStructureBuildNodeInput{};
        nr::test::requireEqual(input.hitShaderBindingTableRecordCount, 1u);
        nr::test::require(input.unusedFrameRetireLatency > 0u, "AS build cache retirement latency should stay positive");
    }};
} // namespace
