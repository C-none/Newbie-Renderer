import std;
import dependency.vulkan;
import nr.rhi;
import nr.renderer;
import nr.utils;

namespace
{
using Runtime = nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>;

[[nodiscard]] std::shared_ptr<Runtime> initializedRuntime(nr::rhi::Device &device)
{
    auto &shaderService = nr::rhi::ShaderService::instance();
    shaderService.configure();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/accumulate"},
    });
    nr::nrAssert(program.valid(), "PipelineRuntime failure probe requires the accumulate program.");

    auto runtime = std::make_shared<Runtime>();
    runtime->initialize(device.pipeline().createComputePipeline(program, {}, 64u, {}, "PipelineRuntime.FailureProbe"));
    return runtime;
}

void probeInvalidHandle()
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_pipeline_runtime_failure_probe.invalid", "NewbieRenderer");
    auto runtime = initializedRuntime(device);
    static_cast<void>(runtime->bindingSetsForFrame({}, 0u));
}

void probeStaleHandle()
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_pipeline_runtime_failure_probe.stale", "NewbieRenderer");
    auto runtime = initializedRuntime(device);
    auto const owner = runtime->passBinding("FailureProbe.Node", 0u);
    runtime->clearBindingSets();
    static_cast<void>(runtime->bindingSetsForFrame(owner, 0u));
}

void probeForeignHandle()
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_pipeline_runtime_failure_probe.foreign", "NewbieRenderer");
    auto ownerRuntime = initializedRuntime(device);
    auto otherRuntime = initializedRuntime(device);
    auto const foreignOwner = ownerRuntime->passBinding("FailureProbe.Foreign", 0u);
    static_cast<void>(otherRuntime->bindingSetsForFrame(foreignOwner, 0u));
}

template <typename TCallback> void withBuildContext(std::shared_ptr<Runtime> runtime, TCallback callback)
{
    auto bindlessCache = nr::renderer::BindlessImageTableCache{};
    auto globals = nr::renderer::FrameGlobalResources{
        .bindlessImageTableCache = std::ref(bindlessCache),
    };
    auto namedResources = std::map<std::string, nr::renderer::GraphResourceHandle>{};
    auto namedFrameData = std::map<std::string, nr::renderer::GraphFrameDataHandle>{};
    auto graphBuilder = nr::renderer::RenderGraphBuilder{};
    auto const node = graphBuilder.addNode("FailureProbe.Node", nr::renderer::QueueDomain::Compute);
    auto buildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(graphBuilder),
        .nodeHandle = node,
        .queue = nr::renderer::QueueDomain::Compute,
        .runtimeName = "FailureProbe.Node",
        .globalResources = std::cref(globals),
        .frameResources = std::ref(namedResources),
        .frameDataResources = std::ref(namedFrameData),
    };
    callback(graphBuilder, buildContext, globals, namedResources, namedFrameData, std::move(runtime));
}

void probeMissingRuntime()
{
    withBuildContext(std::shared_ptr<Runtime>{}, [](auto &, nr::renderer::NodeBuildContext &buildContext, auto &,
                                                    auto &, auto &, std::shared_ptr<Runtime> runtime) {
        auto pass = nr::renderer::ComputePassBuilder{
            buildContext,
            "FailureProbe.MissingRuntime",
            std::move(runtime),
        };
        static_cast<void>(pass);
    });
}

void probeColdEmptyPrepare()
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_pipeline_runtime_failure_probe.cold-empty-prepare", "NewbieRenderer");
    withBuildContext(initializedRuntime(device), [](auto &, nr::renderer::NodeBuildContext &buildContext, auto &,
                                                    auto &, auto &, std::shared_ptr<Runtime> runtime) {
        auto pass = nr::renderer::ComputePassBuilder{
            buildContext,
            "FailureProbe.ColdEmptyPrepare",
            std::move(runtime),
        };
        pass.prepare({});
    });
}

void probePatchEmptyPrepare()
{
    auto device = nr::rhi::Device{};
    device.initialize("nr_pipeline_runtime_failure_probe.patch-empty-prepare", "NewbieRenderer");
    withBuildContext(initializedRuntime(device),
                     [](nr::renderer::RenderGraphBuilder &graphBuilder, nr::renderer::NodeBuildContext &buildContext,
                        nr::renderer::FrameGlobalResources &globals, auto &namedResources, auto &namedFrameData,
                        std::shared_ptr<Runtime> runtime) {
                         static_cast<void>(buildContext.addPass({}, "FailureProbe.Placeholder",
                                                                [](const nr::renderer::PassRecordContext &) {}));
                         auto patchContext = nr::renderer::RenderGraphSkeletonPatchContext{
                             graphBuilder.mutableFrame(),
                             nr::renderer::RenderGraphSkeletonNodePatchLayout{
                                 .queue = nr::renderer::QueueDomain::Compute,
                                 .passCount = 1u,
                             },
                             namedResources,
                             namedFrameData,
                             std::addressof(globals),
                             "FailureProbe.Node",
                         };
                         auto patch = nr::renderer::ComputePassPatchBuilder{
                             patchContext,
                             0u,
                             "FailureProbe.PatchEmptyPrepare",
                             std::move(runtime),
                         };
                         patch.prepare({});
                     });
}
} // namespace

int main(int argc, char **argv)
{
    nr::nrAssert(argc == 2, "nr_pipeline_runtime_failure_probe requires one scenario argument.");
    auto const scenario = std::string_view{argv[1]};
    if (scenario == "uninitialized")
    {
        auto runtime = Runtime{};
        static_cast<void>(runtime.bindingSetsForFrame({}, 0u));
        return 0;
    }
    if (scenario == "invalid-handle")
    {
        probeInvalidHandle();
        return 0;
    }
    if (scenario == "stale-handle")
    {
        probeStaleHandle();
        return 0;
    }
    if (scenario == "foreign-handle")
    {
        probeForeignHandle();
        return 0;
    }
    if (scenario == "missing-runtime")
    {
        probeMissingRuntime();
        return 0;
    }
    if (scenario == "cold-empty-prepare")
    {
        probeColdEmptyPrepare();
        return 0;
    }
    if (scenario == "patch-empty-prepare")
    {
        probePatchEmptyPrepare();
        return 0;
    }

    nr::nrAssert(false, "nr_pipeline_runtime_failure_probe received an unknown scenario.");
    return 0;
}
