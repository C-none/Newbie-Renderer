import std;
import dependency;
import nr.renderer;
import nr.renderPasses;

namespace
{
struct CursorSampleState
{
    bool hasPrevious = false;
    glm::dvec2 previous{0.0, 0.0};
};

inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;
inline constexpr int kKeyW = 'W';
inline constexpr int kKeyS = 'S';
inline constexpr int kKeyA = 'A';
inline constexpr int kKeyD = 'D';
inline constexpr int kKeyQ = 'Q';
inline constexpr int kKeyE = 'E';

void printUsage()
{
    std::println("Usage:");
    std::println("  main");
    std::println("  main --help");
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

[[nodiscard]] float sanitizeDeltaSeconds(float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
    {
        return 1.0f / 60.0f;
    }
    return std::clamp(deltaSeconds, 1.0f / 240.0f, 0.1f);
}

[[nodiscard]] nr::renderer::ViewerCameraControlInput sampleInteractiveCameraInput(
    const auto& presentation,
    float deltaSeconds,
    CursorSampleState& cursorState)
{
    auto cursor = presentation.cursorPosition();
    auto rotateActive = presentation.mouseButtonDown(kMouseButtonLeft) ||
                        presentation.mouseButtonDown(kMouseButtonRight);

    auto cursorDelta = glm::vec2{0.0f, 0.0f};
    if (rotateActive)
    {
        if (cursorState.hasPrevious)
        {
            cursorDelta = glm::vec2{
                static_cast<float>(cursor.x - cursorState.previous.x),
                static_cast<float>(cursor.y - cursorState.previous.y),
            };
        }
        cursorState.previous = cursor;
        cursorState.hasPrevious = true;
    }
    else
    {
        cursorState.previous = cursor;
        cursorState.hasPrevious = false;
    }

    return nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = deltaSeconds,
        .moveForward = presentation.keyDown(kKeyW),
        .moveBackward = presentation.keyDown(kKeyS),
        .moveLeft = presentation.keyDown(kKeyA),
        .moveRight = presentation.keyDown(kKeyD),
        .moveUp = presentation.keyDown(kKeyE),
        .moveDown = presentation.keyDown(kKeyQ),
        .rotateActive = rotateActive,
        .cursorDelta = cursorDelta,
    };
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildMainGraphSpec(
    const std::shared_ptr<nr::renderPasses::EmbeddedTriangleNode>& embeddedTriangle)
{
    if (!embeddedTriangle)
    {
        return {};
    }

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
    };

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "Main.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 0,
        },
    };

    return graphSpec;
}

[[nodiscard]] int runMain()
{
    auto renderer = nr::renderer::Renderer{};
    auto exitCode = 0;

    try
    {
        renderer.initialize(nr::renderer::RendererCreateInfo{
            .appName = "NewbieRenderer",
            .engineName = "NewbieRenderer",
        });

        {
            auto& presentation = renderer.device().presentationContext;
            auto embeddedTriangle = std::make_shared<nr::renderPasses::EmbeddedTriangleNode>();
            embeddedTriangle->input.colorFormat = presentation.swapchainFormat();

            if (exitCode == 0)
            {
                auto viewerCamera = nr::renderer::ViewerPerspectiveCamera{};
                auto initialExtent = presentation.swapchainExtent();
                viewerCamera.setViewportExtent(glm::uvec2{initialExtent.width, initialExtent.height});
                viewerCamera.setLens(nr::renderer::ViewerPerspectiveLens{
                    .verticalFovRadians = glm::radians(60.0f),
                    .nearPlane = 0.1f,
                    .farPlane = 100.0f,
                });
                viewerCamera.setPoseFromLookAt(glm::vec3{0.0f, 0.0f, 3.0f}, glm::vec3{0.0f, 0.0f, 0.0f});

                renderer.installGraph(buildMainGraphSpec(embeddedTriangle));

                auto cursorState = CursorSampleState{};
                auto previousTick = std::chrono::steady_clock::now();

                while (!presentation.windowShouldClose())
                {
                    presentation.pollEvents();

                    auto now = std::chrono::steady_clock::now();
                    auto deltaSeconds = std::chrono::duration<float>(now - previousTick).count();
                    previousTick = now;
                    deltaSeconds = sanitizeDeltaSeconds(deltaSeconds);

                    auto extent = presentation.swapchainExtent();
                    viewerCamera.setViewportExtent(glm::uvec2{extent.width, extent.height});
                    viewerCamera.applyControl(sampleInteractiveCameraInput(presentation, deltaSeconds, cursorState));
                    embeddedTriangle->input.viewProjection = viewerCamera.frame().viewProjection;

                    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
                        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
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

            renderer.device().waitIdle();
        }

        renderer.shutdown();
        return exitCode;
    }
    catch (const std::exception& error)
    {
        std::println("[error] exception in main: {}", error.what());
        if (renderer.initialized())
        {
            renderer.device().waitIdle();
            renderer.shutdown();
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
