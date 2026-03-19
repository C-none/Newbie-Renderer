import std;
import nr.rhi;

namespace
{
[[nodiscard]] bool testCompileTimeContracts()
{
    static_assert(std::is_invocable_r_v<std::optional<std::string>, decltype(&nr::rhi::validateRayTracingPipelineDesc), const nr::rhi::RayTracingPipelineDesc &>);

    static_assert(std::is_invocable_r_v<
                  nr::rhi::ShaderBindingTableBuildDiagnostics,
                  decltype(&nr::rhi::validateShaderBindingTableLayoutDesc),
                  const nr::rhi::ShaderBindingTableLayoutDesc &>);

    static_assert(std::is_invocable_r_v<
                  nr::rhi::ShaderBindingTableBuildPlan,
                  decltype(static_cast<nr::rhi::ShaderBindingTableBuildPlan (*)(const nr::rhi::ShaderBindingTableLayoutDesc &)>(&nr::rhi::makeShaderBindingTableBuildPlan)),
                  const nr::rhi::ShaderBindingTableLayoutDesc &>);

    static_assert(std::is_invocable_r_v<
                  nr::rhi::TraceRaysDispatchDiagnostics,
                  decltype(&nr::rhi::validateTraceRaysDispatch),
                  const nr::rhi::TraceRaysDimensions &,
                  const nr::rhi::RayTracingCapabilitySnapshot &>);

    static_assert(std::is_invocable_r_v<
                  nr::rhi::TraceRaysDispatchDiagnostics,
                  decltype(&nr::rhi::validateTraceRaysIndirect),
                  vk::DeviceAddress,
                  const nr::rhi::RayTracingCapabilitySnapshot &>);

    return true;
}

[[nodiscard]] bool testPipelineDescValidation()
{
    nr::rhi::RayTracingPipelineDesc defaultDesc{};
    auto defaultValidation = nr::rhi::validateRayTracingPipelineDesc(defaultDesc);
    if (defaultValidation.has_value())
    {
        std::println("[error] default RT desc should be valid but got: {}", *defaultValidation);
        return false;
    }

    nr::rhi::RayTracingPipelineDesc libraryDesc{};
    libraryDesc.createAsLibrary = true;
    libraryDesc.libraryInterface = nr::rhi::RayTracingPipelineLibraryInterfaceDesc{
        .maxPipelineRayPayloadSize = 64,
        .maxPipelineRayHitAttributeSize = 32,
    };

    auto libraryValidation = nr::rhi::validateRayTracingPipelineDesc(libraryDesc);
    if (libraryValidation.has_value())
    {
        std::println("[error] library RT desc should be valid but got: {}", *libraryValidation);
        return false;
    }

    nr::rhi::RayTracingPipelineDesc linkDesc{};
    linkDesc.libraryInterface = nr::rhi::RayTracingPipelineLibraryInterfaceDesc{
        .maxPipelineRayPayloadSize = 48,
        .maxPipelineRayHitAttributeSize = 24,
    };

    auto fakeRawPipeline = std::bit_cast<VkPipeline>(std::uintptr_t{1});
    linkDesc.linkedLibraries.push_back(vk::Pipeline{fakeRawPipeline});

    auto linkValidation = nr::rhi::validateRayTracingPipelineDesc(linkDesc);
    if (linkValidation.has_value())
    {
        std::println("[error] linked-library RT desc should be valid but got: {}", *linkValidation);
        return false;
    }

    nr::rhi::RayTracingPipelineDesc invalidLibraryDesc{};
    invalidLibraryDesc.createAsLibrary = true;

    auto invalidValidation = nr::rhi::validateRayTracingPipelineDesc(invalidLibraryDesc);
    if (!invalidValidation.has_value())
    {
        std::println("[error] expected library desc validation failure when libraryInterface is missing.");
        return false;
    }

    return true;
}

[[nodiscard]] bool testSbtLayoutPlan()
{
    nr::rhi::RayTracingCapabilitySnapshot capabilities{};
    capabilities.shaderGroupHandleSize = 32;
    capabilities.shaderGroupHandleAlignment = 32;
    capabilities.shaderGroupBaseAlignment = 64;
    capabilities.maxShaderGroupStride = 256;

    nr::rhi::ShaderBindingTableLayoutDesc layoutDesc{};
    layoutDesc.capabilities = capabilities;
    layoutDesc.pipelineGroupCount = 8;
    layoutDesc.raygen = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 0, .groupCount = 1, .stride = 0};
    layoutDesc.miss = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 1, .groupCount = 2, .stride = 0};
    layoutDesc.hit = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 3, .groupCount = 3, .stride = 64};
    layoutDesc.callable = nr::rhi::ShaderBindingTableSectionDesc{.firstGroup = 6, .groupCount = 2, .stride = 0};

    auto validation = nr::rhi::validateShaderBindingTableLayoutDesc(layoutDesc);
    if (!validation.isValid)
    {
        std::println("[error] expected valid SBT layout desc but got: {}", validation.message);
        return false;
    }

    auto plan = nr::rhi::makeShaderBindingTableBuildPlan(layoutDesc);

    if (plan.raygen.section.stride != 32 || plan.raygen.size != 32)
    {
        std::println("[error] unexpected raygen plan values: stride={}, size={}", plan.raygen.section.stride, plan.raygen.size);
        return false;
    }

    if (plan.raygen.offset != 0 || plan.miss.offset != 64 || plan.hit.offset != 128 || plan.callable.offset != 320)
    {
        std::println(
            "[error] unexpected section offsets: raygen={}, miss={}, hit={}, callable={}",
            plan.raygen.offset,
            plan.miss.offset,
            plan.hit.offset,
            plan.callable.offset);
        return false;
    }

    if (plan.totalSize != 384)
    {
        std::println("[error] unexpected total SBT size: {}", plan.totalSize);
        return false;
    }

    if (plan.raygen.size != plan.raygen.section.stride)
    {
        std::println("[error] raygen section violated size == stride.");
        return false;
    }

    return true;
}

[[nodiscard]] bool testTraceDispatchValidation()
{
    nr::rhi::RayTracingCapabilitySnapshot capabilities{};
    capabilities.maxDispatchDimensions = {1024, 1024, 64};
    capabilities.maxRayDispatchInvocationCount = 1024 * 1024;
    capabilities.rayTracingPipelineTraceRaysIndirect = true;

    auto validDispatch = nr::rhi::validateTraceRaysDispatch(nr::rhi::TraceRaysDimensions{.width = 128, .height = 64, .depth = 1}, capabilities);
    if (!validDispatch.isValid)
    {
        std::println("[error] valid trace dispatch rejected: {}", validDispatch.message);
        return false;
    }

    auto oversizeWidth = nr::rhi::validateTraceRaysDispatch(nr::rhi::TraceRaysDimensions{.width = 2048, .height = 1, .depth = 1}, capabilities);
    if (oversizeWidth.isValid)
    {
        std::println("[error] expected width validation failure.");
        return false;
    }

    auto tooManyInvocations = nr::rhi::validateTraceRaysDispatch(nr::rhi::TraceRaysDimensions{.width = 1024, .height = 1024, .depth = 2}, capabilities);
    if (tooManyInvocations.isValid)
    {
        std::println("[error] expected invocation-count validation failure.");
        return false;
    }

    auto validIndirect = nr::rhi::validateTraceRaysIndirect(vk::DeviceAddress{8}, capabilities);
    if (!validIndirect.isValid)
    {
        std::println("[error] valid indirect trace arguments rejected: {}", validIndirect.message);
        return false;
    }

    auto unalignedIndirect = nr::rhi::validateTraceRaysIndirect(vk::DeviceAddress{10}, capabilities);
    if (unalignedIndirect.isValid)
    {
        std::println("[error] expected indirect address alignment failure.");
        return false;
    }

    capabilities.rayTracingPipelineTraceRaysIndirect = false;
    auto unsupportedIndirect = nr::rhi::validateTraceRaysIndirect(vk::DeviceAddress{8}, capabilities);
    if (unsupportedIndirect.isValid)
    {
        std::println("[error] expected feature-gated indirect validation failure.");
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!testCompileTimeContracts())
    {
        std::println("[FAIL] compile-time RT contracts failed");
        return 1;
    }

    if (!testPipelineDescValidation())
    {
        std::println("[FAIL] RT pipeline desc validation test failed");
        return 2;
    }

    if (!testSbtLayoutPlan())
    {
        std::println("[FAIL] SBT layout planning test failed");
        return 3;
    }

    if (!testTraceDispatchValidation())
    {
        std::println("[FAIL] trace dispatch validation test failed");
        return 4;
    }

    std::println("[OK] RT P0 contract tests passed");
    return 0;
}
