module nr.pipeline;

import nr.renderer;
import nr.renderPasses;
import nr.utils;
import std;

namespace nr::pipeline::detail
{
namespace
{
[[nodiscard]] nr::renderer::RendererGraphSpec buildNormalViewGraph(const PipelineBuildContext &context)
{
    auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();

    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = context.swapchainFormat;
    present->input.screenshot.sessionId = context.captureSessionId;

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = normalBuffer,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "NormalBuffer",
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
            .debugName = "normalview.GraphicsToCompute",
            .afterNodeIndex = 1u,
        },
    };
    return graphSpec;
}
} // namespace

void registerNormalViewPipeline(RenderPipelineRegistry &registry)
{
    [[maybe_unused]] auto const registered = registry.registerPipeline(RenderPipelineDesc{
        .id = std::string{normalViewPipelineId},
        .displayName = std::string{normalViewPipelineId},
        .buildGraph = buildNormalViewGraph,
    });
    nr::nrAssert(registered, "registerNormalViewPipeline failed to register normalview.");
}
} // namespace nr::pipeline::detail
