import dependency;
import std;
import nr.app;
import nr.renderer;
import nr.renderPasses;
import nr.scene;
import nr.load;

namespace
{
void printUsage()
{
    std::println("Usage:");
    std::println("  main [model_path]");
    std::println("  main --help");
    std::println("Controls:");
    std::println("  Move: W/S/A/D/Q/E");
    std::println("  Rotate: hold mouse left or right button and move cursor");
    std::println("");
    std::println("If no model_path is provided, the default Box model is loaded.");
}

[[nodiscard]] bool hasFlag(std::span<char*> args, std::string_view expected)
{
    return std::ranges::any_of(args, [expected](const char* token) {
        return token != nullptr && std::string_view{token} == expected;
    });
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildNormalBufferGraphSpec(
    const std::shared_ptr<nr::renderPasses::NormalBufferNode>& normalBuffer)
{
    if (!normalBuffer)
    {
        return {};
    }

    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = normalBuffer,
            .config = nr::renderer::NodeConfig{
                .instanceName = "NormalBuffer",
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
                .nodeName = "NormalBuffer",
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

[[nodiscard]] std::filesystem::path resolveModelPath(std::span<char*> args)
{
    // Check for model path argument
    for (const auto* arg : args)
    {
        if (arg == nullptr)
        {
            continue;
        }
        std::string_view argView{arg};
        if (argView.starts_with("-"))
        {
            continue;
        }
        return std::filesystem::path{argView};
    }

    // Default: use Box model from glTF sample assets
    return std::filesystem::path{NR_PROJECT_ROOT_DIR} / "assets" / "glTF-Sample-Assets" / "Models" / "Box" / "glTF" / "Box.gltf";
}

[[nodiscard]] int runMain(const std::filesystem::path& modelPath)
{
    auto app = nr::app::AppSession{};

    try
    {
        app.initialize(nr::renderer::RendererCreateInfo{
            .appName = "NormalBufferViewer",
            .engineName = "NewbieRenderer",
        });

        auto& renderer = app.renderer();
        auto exitCode = [&]() -> int {
            std::println("[info] Loading model: {}", modelPath.string());
            auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
                .sourcePath = modelPath,
                .generateNormals = true,
                .generateTangents = true,
            });

            if (!loadResult.has_value())
            {
                std::println("[error] Failed to load model: {}", loadResult.error().message);
                return 1;
            }

            auto& sceneAsset = loadResult.value();
            std::println("[info] Model loaded: {} meshes, {} vertices, {} indices",
                         sceneAsset.stats.meshCount,
                         sceneAsset.stats.vertexCount,
                         sceneAsset.stats.indexCount);

            auto& scene = app.createScene();

            auto templateHandle = scene.registerTemplate(sceneAsset);
            if (!templateHandle.valid())
            {
                std::println("[error] Failed to register scene template.");
                return 1;
            }

            auto instanceHandle = scene.instantiate(templateHandle);
            if (!instanceHandle.valid())
            {
                std::println("[error] Failed to instantiate scene.");
                return 1;
            }

            app.resetCameraFromSceneOrDefault();

            [[maybe_unused]] auto extractProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
                .debugName = "NormalBuffer.Extract",
                .domain = nr::scene::ScenePacketDomain::rasterDraw,
                .requireReadyForDomain = true,
                .requireActiveInstances = true,
            });

            auto exitCode = 0;
            {
                auto& presentation = renderer.device().presentationContext;
                auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
                normalBuffer->input.colorFormat = presentation.swapchainFormat();

                renderer.installGraph(buildNormalBufferGraphSpec(normalBuffer));
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

                    auto const cameraOverride = app.camera().buildRendererCameraOverride();
                    normalBuffer->input.view = cameraOverride.frameConstants.view;
                    normalBuffer->input.projection = cameraOverride.frameConstants.projection;
                    normalBuffer->input.viewProjection = cameraOverride.frameConstants.viewProjection;

                    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
                        .scene = std::ref(scene),
                        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
                        .sceneExtractInput = nr::scene::SceneExtractInput{},
                        .cameraOverride = cameraOverride,
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

            return exitCode;
        }();

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

    auto modelPath = resolveModelPath(args);
    if (!std::filesystem::exists(modelPath))
    {
        std::println("[error] Model file not found: {}", modelPath.string());
        return 1;
    }

    return runMain(modelPath);
}
