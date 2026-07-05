import std;
import dependency;
import nr.app;
import nr.renderer;
import nr.renderPasses;
import nr.scene;
import nr.load;
import nr.utils;

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
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = ui,
            .config = nr::renderer::NodeConfig{
                .instanceName = "Ui",
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

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "Smoke.GraphicsToCompute",
            .afterNodeIndex = 1,
        },
    };

    return graphSpec;
}

[[nodiscard]] std::filesystem::path defaultModelPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           "assets" /
           "glTF-Sample-Assets" /
           "Models" /
           "Box" /
           "glTF" /
           "Box.gltf";
}

[[nodiscard]] std::filesystem::path replacementModelPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           "assets" /
           "glTF-Sample-Assets" /
           "Models" /
           "Triangle" /
           "glTF" /
           "Triangle.gltf";
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> loadModelIntoApp(
    nr::app::AppSession& app,
    const std::filesystem::path& modelPath,
    std::string_view label)
{
    auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = modelPath,
    });
    if (!loadResult.has_value())
    {
        std::println("[error] smoke test failed to load {} model: {}", label, loadResult.error().message);
        return std::nullopt;
    }

    auto& scene = app.createScene();
    auto templateHandle = scene.registerTemplate(loadResult.value());
    if (!templateHandle.valid())
    {
        std::println("[error] smoke test failed to register {} scene template.", label);
        return std::nullopt;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!instanceHandle.valid())
    {
        std::println("[error] smoke test failed to instantiate {} scene.", label);
        return std::nullopt;
    }

    app.resetCameraFromSceneOrDefault();
    return std::ref(scene);
}

[[nodiscard]] std::optional<nr::renderer::RendererFrameResult> renderOneFrame(
    nr::app::AppSession& app,
    nr::scene::Scene& scene,
    nr::renderer::FrameServices& frameServices,
    float deltaSeconds)
{
    auto& renderer = app.renderer();
    auto& presentation = renderer.device().presentationContext;

    if (!frameServices.tryGet<nr::app::UiSystem>().has_value())
    {
        std::println("[error] smoke test failed to resolve UiSystem from FrameServices.");
        return std::nullopt;
    }

    presentation.pollEvents();
    app.ui().beginFrame(presentation, deltaSeconds);
    app.camera().updateFromPresentation(presentation, deltaSeconds, app.ui().captureState());
    app.ui().setCameraFrame(app.camera().frame());

    auto const cameraOverride = app.camera().buildRendererCameraOverride();

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
        return std::nullopt;
    }

    if (frameResult.presentResult == vk::Result::eErrorOutOfDateKHR ||
        frameResult.presentResult == vk::Result::eSuboptimalKHR)
    {
        renderer.resize();
        return renderOneFrame(app, scene, frameServices, deltaSeconds);
    }

    if (frameResult.presentResult != vk::Result::eSuccess)
    {
        std::println("[error] smoke test present failed with {}.", vk::to_string(frameResult.presentResult));
        return std::nullopt;
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
        return std::nullopt;
    }

    if (frameResult.invokedPassRecordCount < 3u)
    {
        std::println(
            "[error] smoke test expected at least 3 recorded passes, observed {}.",
            frameResult.invokedPassRecordCount);
        return std::nullopt;
    }

    return frameResult;
}

[[nodiscard]] bool renderUntilSceneDraw(
    nr::app::AppSession& app,
    nr::scene::Scene& scene,
    nr::renderer::FrameServices& frameServices,
    float deltaSeconds,
    std::string_view label)
{
    auto failed = false;
    auto observedDraw = false;
    auto const attempts = std::views::iota(0, 8);
    std::ranges::for_each(attempts, [&](int) {
        if (failed || observedDraw)
        {
            return;
        }

        auto frameResult = renderOneFrame(app, scene, frameServices, deltaSeconds);
        if (!frameResult.has_value())
        {
            failed = true;
            return;
        }

        observedDraw = frameResult->sceneRasterPacketCount > 0u &&
                       frameResult->sceneBridgeDrawCount > 0u;
    });

    if (failed)
    {
        return false;
    }

    if (!observedDraw)
    {
        std::println("[error] smoke test did not observe a rendered scene draw for {}.", label);
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

        auto firstScene = loadModelIntoApp(app, defaultModelPath(), "initial");
        if (!firstScene.has_value())
        {
            return false;
        }

        auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
        app.renderer().installGraph(buildNormalBufferGraphSpec(normalBuffer));

        auto frameServices = app.makeFrameServices();
        constexpr auto deltaSeconds = 1.0f / 60.0f;

        if (!renderUntilSceneDraw(app, firstScene->get(), frameServices, deltaSeconds, "initial model"))
        {
            return false;
        }

        auto secondScene = loadModelIntoApp(app, replacementModelPath(), "replacement");
        if (!secondScene.has_value())
        {
            return false;
        }

        if (!renderUntilSceneDraw(app, secondScene->get(), frameServices, deltaSeconds, "replacement model"))
        {
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
