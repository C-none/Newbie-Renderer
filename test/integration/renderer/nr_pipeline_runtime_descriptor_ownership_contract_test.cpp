import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.test;

namespace
{
using Runtime = nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>;
using PassBindingHandle = Runtime::PassBindingHandle;

[[nodiscard]] nr::rhi::SlangProgram compileOwnershipProgram(std::filesystem::path sourcePath)
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::move(sourcePath),
    });
    nr::test::require(program.valid(), "pipeline runtime ownership contract requires a valid shader program");
    return program;
}

const nr::test::CaseRegistrar pipelineRuntimeDescriptorOwnershipCase{
    "pipeline runtime isolates descriptor state by pass owner and frame slot", [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_pipeline_runtime_descriptor_ownership_contract_test", "NewbieRenderer");
        auto program = compileOwnershipProgram(std::filesystem::path{"renderer/accumulate"});
        auto runtime = std::make_shared<Runtime>();
        runtime->initialize(device.pipeline().createComputePipeline(program, {}, 64u, {}, "PipelineRuntime.Ownership"));

        auto const passA = runtime->passBinding("Ownership.Node", 0u);
        auto const passARepeat = runtime->passBinding("Ownership.Node", 0u);
        auto const passB = runtime->passBinding("Ownership.Node", 1u);
        nr::test::require(passA.valid() && passB.valid(), "pass owners should be valid after pipeline initialization");
        nr::test::require(passA == passARepeat,
                          "the same runtime name and node-local ordinal should reuse one stable pass owner");
        nr::test::require(passA != passB, "different logical passes should receive different pass owners");

        auto const passASlot0 = runtime->bindingSetsForFrame(passA, 0u);
        auto const passBSlot0 = runtime->bindingSetsForFrame(passB, 0u);
        auto const passASlot1 = runtime->bindingSetsForFrame(passA, 1u);
        nr::test::require(!passASlot0.empty() && !passBSlot0.empty() && !passASlot1.empty(),
                          "the accumulate pipeline should allocate reflected descriptor sets for every owner slot");
        nr::test::require(passASlot0.front().raw() != passBSlot0.front().raw(),
                          "two passes in one runtime/frame slot must own different descriptor sets");
        nr::test::require(passASlot0.front().raw() != passASlot1.front().raw(),
                          "one pass must own different descriptor sets in different frame slots");

        auto &passACacheSlot0 = runtime->descriptorWriteCacheForFrame(passA, 0u);
        auto &passBCacheSlot0 = runtime->descriptorWriteCacheForFrame(passB, 0u);
        auto &passACacheSlot1 = runtime->descriptorWriteCacheForFrame(passA, 1u);
        nr::test::require(std::addressof(passACacheSlot0) != std::addressof(passBCacheSlot0),
                          "two pass owners must not share a descriptor write cache in one frame slot");
        nr::test::require(std::addressof(passACacheSlot0) != std::addressof(passACacheSlot1),
                          "descriptor write caches must be isolated between frame slots");

        auto twoTableProgram = compileOwnershipProgram(std::filesystem::path{"test/renderer/bindlessTwoTableContract"});
        auto twoTableRuntime = Runtime{};
        twoTableRuntime.initializeDeferred(
            device.pipeline().createComputePipeline(twoTableProgram, {}, 64u, {}, "PipelineRuntime.TwoTable"));
        auto const twoTableOwner = twoTableRuntime.passBinding("Ownership.TwoTable", 0u);
        nr::test::require(twoTableRuntime.ensureBindingSetsForFrame(twoTableOwner, 0u, {{1u, 4u}}),
                          "the first variable descriptor set should allocate owner binding sets");
        nr::test::require(twoTableRuntime.ensureBindingSetsForFrame(twoTableOwner, 0u, {{2u, 6u}}),
                          "adding a second variable descriptor set should reallocate once");
        nr::test::require(!twoTableRuntime.ensureBindingSetsForFrame(twoTableOwner, 0u, {{1u, 4u}}),
                          "re-ensuring the first table should preserve the second table count without churn");
        nr::test::require(!twoTableRuntime.ensureBindingSetsForFrame(twoTableOwner, 0u, {{2u, 6u}}),
                          "re-ensuring the second table should preserve the complete merged count map");

        auto bindlessCache = nr::renderer::BindlessImageTableCache{};
        auto globals = nr::renderer::FrameGlobalResources{
            .bindlessImageTableCache = std::ref(bindlessCache),
        };
        auto namedResources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
        auto namedFrameData = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
        auto graphBuilder = nr::renderer::RenderGraphBuilder{};
        auto const node = graphBuilder.addNode("Ownership.Node", nr::renderer::QueueDomain::Compute);
        auto buildContext = nr::renderer::NodeBuildContext{
            .graphBuilder = std::ref(graphBuilder),
            .nodeHandle = node,
            .queue = nr::renderer::QueueDomain::Compute,
            .runtimeName = "Ownership.Node",
            .globalResources = std::cref(globals),
            .frameResources = std::ref(namedResources),
            .frameDataResources = std::ref(namedFrameData),
        };

        auto coldOwner = std::optional<PassBindingHandle>{};
        auto coldPass = nr::renderer::ComputePassBuilder{buildContext, "Ownership.Compute", runtime};
        static_cast<void>(
            coldPass
                .prepare([&](const nr::renderer::PassPrepareContext &, PassBindingHandle owner) { coldOwner = owner; })
                .record([](const nr::renderer::ComputePassRecordContext &) {})
                .build());
        graphBuilder.frame().passes.front().prepare(nr::renderer::PassPrepareContext{});
        nr::test::require(coldOwner.has_value() && *coldOwner == passA,
                          "cold builder should derive its owner from runtime name and node-local pass ordinal");

        auto patchContext = nr::renderer::RenderGraphSkeletonPatchContext{
            graphBuilder.mutableFrame(),
            nr::renderer::RenderGraphSkeletonNodePatchLayout{
                .queue = nr::renderer::QueueDomain::Compute,
                .passCount = 1u,
            },
            namedResources,
            namedFrameData,
            std::addressof(globals),
            "Ownership.Node",
        };
        auto patchOwner = std::optional<PassBindingHandle>{};
        auto patchPass = nr::renderer::ComputePassPatchBuilder{patchContext, 0u, "Ownership.Compute.Patched", runtime};
        patchPass
            .prepare([&](const nr::renderer::PassPrepareContext &, PassBindingHandle owner) { patchOwner = owner; })
            .record([](const nr::renderer::ComputePassRecordContext &) {})
            .patch();
        graphBuilder.frame().passes.front().prepare(nr::renderer::PassPrepareContext{});
        nr::test::require(patchOwner.has_value() && *patchOwner == passA,
                          "skeleton patch should reuse the cold pass owner without allocating another identity");

        auto repeatedPatchOwner = std::optional<PassBindingHandle>{};
        auto repeatedPatchPass =
            nr::renderer::ComputePassPatchBuilder{patchContext, 0u, "Ownership.Compute.RepeatedPatch", runtime};
        repeatedPatchPass
            .prepare(
                [&](const nr::renderer::PassPrepareContext &, PassBindingHandle owner) { repeatedPatchOwner = owner; })
            .record([](const nr::renderer::ComputePassRecordContext &) {})
            .patch();
        graphBuilder.frame().passes.front().prepare(nr::renderer::PassPrepareContext{});
        nr::test::require(repeatedPatchOwner.has_value() && *repeatedPatchOwner == passA,
                          "repeated skeleton patching should preserve the existing pass owner");

        device.waitIdle();
    }};
} // namespace
