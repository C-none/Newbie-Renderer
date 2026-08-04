module nr.pipeline;

import nr.app;
import nr.options;
import nr.renderer;
import nr.renderPasses;
import nr.rhi;
import nr.utils;
import std;

namespace nr::pipeline::detail
{
namespace
{
[[nodiscard]] nr::renderer::RendererGraphSpec buildRtObjectGraph(const PipelineBuildContext &context)
{
    auto asBuild = std::make_shared<nr::renderPasses::AccelerationStructureBuildNode>();
    auto lightPrepare = std::make_shared<nr::renderPasses::LightPrepareNode>();
    auto rayTrace = std::make_shared<nr::renderPasses::PathTracingNode>();
    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto postProcessing = std::shared_ptr<nr::renderer::NodeRuntime>{};
    auto postProcessingName = std::string{};
    auto dlssResolutionController = std::shared_ptr<nr::renderPasses::DlssRayReconstructionResolutionController>{};
    switch (context.rtPostProcessingMode)
    {
    case RtPostProcessingMode::accumulate:
        postProcessing = std::make_shared<nr::renderPasses::AccumulateNode>();
        postProcessingName = "Accumulate";
        break;
    case RtPostProcessingMode::dlssRayReconstruction: {
        auto dlssRayReconstruction = std::make_shared<nr::renderPasses::DlssRayReconstructionNode>();
        dlssResolutionController = std::make_shared<nr::renderPasses::DlssRayReconstructionResolutionController>();
        dlssRayReconstruction->setResolutionController(dlssResolutionController);
        dlssRayReconstruction->input.enabled = true;
        dlssRayReconstruction->input.create.quality = context.rtDlssQuality == RtDlssQuality::ultraPerformance
                                                          ? nr::rhi::DlssQuality::UltraPerformance
                                                          : nr::rhi::DlssQuality::Dlaa;
        dlssRayReconstruction->input.create.depthType = nr::rhi::DlssDepthType::Hardware;
        dlssRayReconstruction->input.outputColorKey = std::string{nr::renderer::frameResource::presentSourceColor};
        postProcessing = std::move(dlssRayReconstruction);
        postProcessingName = "DlssRayReconstruction";
        break;
    }
    }
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = context.swapchainFormat;
    present->input.screenshot.sessionId = context.captureSessionId;

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.cameraJitter = nr::renderer::RendererCameraJitterConfig{
        .sequence = nr::renderer::RendererCameraJitterSequence::Halton23,
        .cycleLength = nr::renderer::kRendererDefaultCameraJitterCycleLength,
    };
    if (dlssResolutionController)
    {
        graphSpec.frameResolutionResolver =
            [controller = dlssResolutionController](nr::rhi::Device &device, vk::Extent2D displayExtent,
                                                    const nr::options::OptionFrameSnapshot &snapshot) {
                auto query = nr::renderPasses::DlssRayReconstructionResolutionController::OptimalSettingsQuery{
                    [&device](nr::rhi::DlssDimensions targetSize, nr::rhi::DlssQuality quality) {
                        return device.dlssContext()->optimalSettings(targetSize, quality);
                    }};
                return controller->resolve(nr::renderPasses::dlssResolutionRequestFromSnapshot(snapshot), displayExtent,
                                           query);
            };
        graphSpec.frameResolutionOptionRequirements = {
            nr::options::optionId(nr::options::keys::dlssEnabled),
            nr::options::optionId(nr::options::keys::dlssQuality),
            nr::options::optionId(nr::options::keys::dlssBypass),
        };
    }
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = asBuild,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "AccelerationStructureBuild",
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = lightPrepare,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "LightPrepare",
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = rayTrace,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "PathTracing",
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = ui,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "Ui",
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = postProcessing,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = std::move(postProcessingName),
                    .queue = nr::renderer::QueueDomain::Compute,
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = present,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "Present",
                    .queue = nr::renderer::QueueDomain::Compute,
                },
        },
    };
    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "rtobject.GraphicsToCompute",
            .afterNodeIndex = 3u,
        },
    };
    return graphSpec;
}
} // namespace

void registerRtObjectPipeline(RenderPipelineRegistry &registry)
{
    [[maybe_unused]] auto const registered = registry.registerPipeline(RenderPipelineDesc{
        .id = std::string{rtObjectPipelineId},
        .displayName = std::string{rtObjectPipelineId},
        .buildGraph = buildRtObjectGraph,
    });
    nr::nrAssert(registered, "registerRtObjectPipeline failed to register rtobject.");
}

} // namespace nr::pipeline::detail
