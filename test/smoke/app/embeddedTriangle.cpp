import std;
import dependency;
import nr.app;
import nr.renderer;
import nr.renderPasses;

namespace
{
void printUsage()
{
    std::println("Usage:");
    std::println("  embeddedTriangle");
    std::println("  embeddedTriangle --help");
    std::println("Controls:");
    std::println("  Move: W/S/A/D/Q/E");
    std::println("  Rotate: hold mouse left or right button and move cursor");
}

[[nodiscard]] bool hasFlag(std::span<char*> args, std::string_view expected)
{
    return std::ranges::any_of(args, [expected](const char* token) {
        return token != nullptr && std::string_view{token} == expected;
    });
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildMainGraphSpec(
    const std::shared_ptr<nr::renderPasses::EmbeddedTriangleNode>& embeddedTriangle)
{
    if (!embeddedTriangle)
    {
        return {};
    }

    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = embeddedTriangle,
            .config = nr::renderer::NodeConfig{
                .instanceName = "EmbeddedTriangle",
                .queue = nr::renderer::QueueDomain::Graphics,
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = ui,
            .config = nr::renderer::NodeConfig{
                .instanceName = "Ui",
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
                .nodeName = "EmbeddedTriangle",
                .portName = "color",
            },
            .to = nr::renderer::NodePortRef{
                .nodeName = "Present",
                .portName = "sourceColor",
            },
        },
        nr::renderer::NodeConnection{
            .from = nr::renderer::NodePortRef{
                .nodeName = "Ui",
                .portName = "uiBuffer",
            },
            .to = nr::renderer::NodePortRef{
                .nodeName = "Present",
                .portName = "uiBuffer",
            },
        },
    };

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "Main.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 1,
        },
    };

    return graphSpec;
}

[[nodiscard]] int runMain()
{
    auto app = nr::app::AppSession{};
    auto exitCode = 0;

    try
    {
        app.initialize(nr::renderer::RendererCreateInfo{
            .appName = "EmbeddedTriangle",
            .engineName = "NewbieRenderer",
        });

        auto& renderer = app.renderer();
        {
            auto& presentation = renderer.device().presentationContext;
            auto embeddedTriangle = std::make_shared<nr::renderPasses::EmbeddedTriangleNode>();
            embeddedTriangle->input.colorFormat = presentation.swapchainFormat();
            auto defaultCameraView = nr::app::AppCameraDefaultView{};
            defaultCameraView.lens.farPlane = 100.0f;
            app.resetCameraFromSceneOrDefault(defaultCameraView);

            if (exitCode == 0)
            {
                renderer.installGraph(buildMainGraphSpec(embeddedTriangle));
                auto frameServices = app.makeFrameServices();

                auto previousTick = std::chrono::steady_clock::now();

                while (!presentation.windowShouldClose())
                {
                    presentation.pollEvents();

                    auto now = std::chrono::steady_clock::now();
                    auto deltaSeconds = std::chrono::duration<float>(now - previousTick).count();
                    previousTick = now;
                    app.ui().beginFrame(presentation, deltaSeconds);
                    app.camera().updateFromPresentation(presentation, deltaSeconds, app.ui().captureState());
                    embeddedTriangle->input.viewProjection =
                        app.camera().buildRendererCameraOverride().frameConstants.viewProjection;

                    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
                        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
                        .frameServices = std::ref(frameServices),
                    });
                    if (!frameResult.rendered)
                    {
                        std::println("[error] renderer returned rendered=false during main loop.");
                        exitCode = 1;
                        break;
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
                        exitCode = 1;
                        break;
                    }
                }
            }
        }

        app.shutdown();
        return exitCode;
    }
    catch (const std::exception& error)
    {
        std::println("[error] exception in main: {}", error.what());
        if (app.initialized())
        {
            app.shutdown();
        }
        return 1;
    }
}
} // namespace

int main(int argc, char** argv)
{
    auto args = std::span<char*>{};
    if (argc > 1)
    {
        args = std::span<char*>{argv + 1, static_cast<std::size_t>(argc - 1)};
    }

    if (hasFlag(args, "--help") || hasFlag(args, "-h"))
    {
        printUsage();
        return 0;
    }

    if (!args.empty())
    {
        std::println("[error] unknown argument: {}", std::string_view{args.front()});
        printUsage();
        return 2;
    }

    return runMain();
}
