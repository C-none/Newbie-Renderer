import std;
import dependency;
import nr.app;
import nr.renderer;
import nr.renderPasses;
import nr.scene;
import nr.load;

namespace
{
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
            .debugName = "Smoke.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 1,
        },
    };

    return graphSpec;
}

[[nodiscard]] std::filesystem::path defaultModelPath()
{
    return std::filesystem::path{NR_PROJECT_ROOT_DIR} /
           "assets" /
           "glTF-Sample-Assets" /
           "Models" /
           "Box" /
           "glTF" /
           "Box.gltf";
}

[[nodiscard]] bool renderOneFrame(
    nr::app::AppSession& app,
    nr::scene::Scene& scene,
    nr::renderer::FrameServices& frameServices,
    const std::shared_ptr<nr::renderPasses::NormalBufferNode>& normalBuffer,
    float deltaSeconds)
{
    auto& renderer = app.renderer();
    auto& presentation = renderer.device().presentationContext;

    if (!frameServices.tryGet<nr::app::UiSystem>().has_value())
    {
        std::println("[error] smoke test failed to resolve UiSystem from FrameServices.");
        return false;
    }

    presentation.pollEvents();
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
        std::println("[error] smoke test frame was not rendered.");
        return false;
    }

    if (frameResult.presentResult == vk::Result::eErrorOutOfDateKHR ||
        frameResult.presentResult == vk::Result::eSuboptimalKHR)
    {
        renderer.resize();
        return renderOneFrame(app, scene, frameServices, normalBuffer, deltaSeconds);
    }

    if (frameResult.presentResult != vk::Result::eSuccess)
    {
        std::println("[error] smoke test present failed with {}.", vk::to_string(frameResult.presentResult));
        return false;
    }

    auto drawData = app.ui().drawData();
    if (!drawData.has_value() || drawData->get().TotalVtxCount <= 0 || drawData->get().CmdListsCount <= 0)
    {
        if (!drawData.has_value())
        {
            std::println("[error] smoke test expected finalized UI draw data, but none was available.");
        }
        else
        {
            std::println(
                "[error] smoke test expected non-empty UI draw data, observed cmdLists={} vertices={} indices={}.",
                drawData->get().CmdListsCount,
                drawData->get().TotalVtxCount,
                drawData->get().TotalIdxCount);
        }
        return false;
    }

    if (frameResult.invokedPassRecordCount < 3u)
    {
        std::println(
            "[error] smoke test expected at least 3 recorded passes, observed {}.",
            frameResult.invokedPassRecordCount);
        return false;
    }

    return true;
}

[[nodiscard]] bool runSmokeTest()
{
    auto app = nr::app::AppSession{};
    try
    {
        app.initialize(nr::renderer::RendererCreateInfo{
            .appName = "NormalBufferUiSmoke",
            .engineName = "NewbieRenderer",
        });

        auto modelPath = defaultModelPath();
        auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
            .sourcePath = modelPath,
            .generateNormals = true,
            .generateTangents = true,
        });
        if (!loadResult.has_value())
        {
            std::println("[error] smoke test failed to load model: {}", loadResult.error().message);
            return false;
        }

        auto& scene = app.createScene();
        auto templateHandle = scene.registerTemplate(loadResult.value());
        if (!templateHandle.valid())
        {
            std::println("[error] smoke test failed to register scene template.");
            return false;
        }

        auto instanceHandle = scene.instantiate(templateHandle);
        if (!instanceHandle.valid())
        {
            std::println("[error] smoke test failed to instantiate scene.");
            return false;
        }

        app.resetCameraFromSceneOrDefault();

        auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
        normalBuffer->input.colorFormat = app.renderer().device().presentationContext.swapchainFormat();
        app.renderer().installGraph(buildNormalBufferGraphSpec(normalBuffer));

        auto frameServices = app.makeFrameServices();
        constexpr auto deltaSeconds = 1.0f / 60.0f;

        normalBuffer->input.displayBackFaces = false;
        if (!renderOneFrame(app, scene, frameServices, normalBuffer, deltaSeconds))
        {
            return false;
        }

        normalBuffer->input.displayBackFaces = true;
        if (!renderOneFrame(app, scene, frameServices, normalBuffer, deltaSeconds))
        {
            return false;
        }

        if (!normalBuffer->input.displayBackFaces)
        {
            std::println("[error] smoke test expected displayBackFaces to stay enabled.");
            return false;
        }

        app.shutdown();
        return true;
    }
    catch (const std::exception& error)
    {
        std::println("[error] smoke test exception: {}", error.what());
        if (app.initialized())
        {
            app.shutdown();
        }
        return false;
    }
}
} // namespace

int main()
{
    return runSmokeTest() ? 0 : 1;
}
