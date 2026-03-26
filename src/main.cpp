import std;
import dependency;
import nr.renderer;
import nr.renderPasses;

int main()
{
    auto renderer = nr::renderer::Renderer{};

    try
    {
        renderer.initialize(nr::renderer::RendererCreateInfo{
            .appName = "NewbieRenderer",
            .engineName = "NewbieRenderer",
        });

        auto normalView = std::make_shared<nr::renderPasses::NormalViewNode>();
        normalView->input.colorFormat = vk::Format::eR16G16B16A16Sfloat;
        normalView->input.depthFormat = vk::Format::eD32Sfloat;

        auto present = std::make_shared<nr::renderPasses::PresentNode>();

        auto graphSpec = nr::renderer::RendererGraphSpec{};
        graphSpec.nodes = {
            nr::renderer::NodeCreateInfo{
                .runtime = normalView,
                .config = nr::renderer::NodeConfig{
                    .instanceName = "NormalView",
                    .queue = nr::renderer::QueueDomain::Graphics,
                },
            },
            nr::renderer::NodeCreateInfo{
                .runtime = present,
                .config = nr::renderer::NodeConfig{
                    .instanceName = "Present",
                    .queue = nr::renderer::QueueDomain::Compute,
                },
            },
        };

        graphSpec.connections = {
            nr::renderer::NodeConnection{
                .from = nr::renderer::NodePortRef{
                    .nodeName = "NormalView",
                    .portName = "color",
                },
                .to = nr::renderer::NodePortRef{
                    .nodeName = "Present",
                    .portName = "sourceColor",
                },
            },
        };

        graphSpec.submitNodes = {
            nr::renderer::SubmitNodeSpec{
                .debugName = "Main.GraphicsToCompute",
                .kind = nr::renderer::SubmitBoundaryKind::Explicit,
                .afterNodeIndex = 0,
            },
        };

        renderer.installGraph(graphSpec);

        auto frameInput = nr::renderer::RendererFrameInput{};

        while (!renderer.device().presentationContext.windowShouldClose())
        {
            renderer.device().presentationContext.pollEvents();

            auto frameResult = renderer.renderFrame(frameInput);
            if (!frameResult.rendered)
            {
                std::println("[error] renderer returned rendered=false during main loop.");
                renderer.shutdown();
                return 1;
            }

            if (frameResult.presentResult == vk::Result::eErrorOutOfDateKHR ||
                frameResult.presentResult == vk::Result::eSuboptimalKHR)
            {
                renderer.resize();
                continue;
            }

            if (frameResult.presentResult != vk::Result::eSuccess)
            {
                std::println(
                    "[error] unexpected present result: {}",
                    vk::to_string(frameResult.presentResult));
                renderer.shutdown();
                return 1;
            }
        }

        renderer.shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::println("[error] exception in main: {}", error.what());
        renderer.shutdown();
        return 1;
    }
}