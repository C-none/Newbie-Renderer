import std;
import dependency;
import nr.app;
import nr.load;
import nr.renderer;
import nr.renderPasses;
import nr.scene;
import nr.utils;

namespace
{
[[nodiscard]] nr::renderer::RendererGraphSpec buildRtObjectGraphSpec(vk::Format swapchainFormat)
{
    auto asBuild = std::make_shared<nr::renderPasses::AccelerationStructureBuildNode>();
    auto lightPrepare = std::make_shared<nr::renderPasses::LightPrepareNode>();
    auto rayTrace = std::make_shared<nr::renderPasses::PathTracingNode>();

    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = swapchainFormat;

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = asBuild,
            .config = nr::renderer::NodeConfig{
                .instanceName = "AccelerationStructureBuild",
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = lightPrepare,
            .config = nr::renderer::NodeConfig{
                .instanceName = "LightPrepare",
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = rayTrace,
            .config = nr::renderer::NodeConfig{
                .instanceName = "PathTracing",
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
            .debugName = "RtObjectMaterialSmoke.GraphicsToCompute",
            .afterNodeIndex = 2u,
        },
    };

    return graphSpec;
}

[[nodiscard]] std::filesystem::path toyCarPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           "assets" /
           "glTF-Sample-Assets" /
           "Models" /
           "ToyCar" /
           "glTF" /
           "ToyCar.gltf";
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> loadToyCar(nr::app::AppSession& app)
{
    auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = toyCarPath(),
    });
    if (!loadResult.has_value())
    {
        std::println("[error] rtobject material smoke failed to load ToyCar: {}", loadResult.error().message);
        return std::nullopt;
    }

    auto& scene = app.createScene();
    auto templateHandle = scene.registerTemplate(loadResult.value());
    if (!templateHandle.valid())
    {
        std::println("[error] rtobject material smoke failed to register ToyCar scene template.");
        return std::nullopt;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!instanceHandle.valid())
    {
        std::println("[error] rtobject material smoke failed to instantiate ToyCar scene.");
        return std::nullopt;
    }

    app.resetCameraFromSceneOrDefault();
    return std::ref(scene);
}

[[nodiscard]] std::optional<nr::renderer::RendererFrameResult> renderOneFrame(
    nr::app::AppSession& app,
    nr::scene::Scene& scene)
{
    auto& renderer = app.renderer();
    auto& presentation = renderer.device().presentationContext;

    presentation.pollEvents();
    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .scene = std::ref(scene),
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
        .sceneExtractInput = nr::scene::SceneExtractInput{},
        .cameraOverride = app.camera().buildRendererCameraOverride(),
    });

    if (!frameResult.rendered)
    {
        std::println("[error] rtobject material smoke frame was not rendered.");
        return std::nullopt;
    }

    if (frameResult.presentResult == vk::Result::eErrorOutOfDateKHR ||
        frameResult.presentResult == vk::Result::eSuboptimalKHR)
    {
        renderer.resize();
        return renderOneFrame(app, scene);
    }

    if (frameResult.presentResult != vk::Result::eSuccess)
    {
        std::println("[error] rtobject material smoke present failed with {}.", vk::to_string(frameResult.presentResult));
        return std::nullopt;
    }

    return frameResult;
}

[[nodiscard]] bool renderUntilRtSceneReady(nr::app::AppSession& app, nr::scene::Scene& scene)
{
    auto failed = false;
    auto observedRtFrame = false;
    auto const attempts = std::views::iota(0, 12);
    std::ranges::for_each(attempts, [&](int) {
        if (failed || observedRtFrame)
        {
            return;
        }

        auto frameResult = renderOneFrame(app, scene);
        if (!frameResult.has_value())
        {
            failed = true;
            return;
        }

        observedRtFrame = frameResult->sceneTlasPacketCount > 0u &&
                          frameResult->invokedPassPrepareCount >= 2u &&
                          frameResult->invokedPassRecordCount >= 4u;
    });

    if (failed)
    {
        return false;
    }

    if (!observedRtFrame)
    {
        std::println("[error] rtobject material smoke did not observe ready ToyCar TLAS packets, LightPrepare prepare, and RT graph passes.");
        return false;
    }

    return true;
}

[[nodiscard]] bool runSmokeTest()
{
    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = "RtObjectMaterialSmoke",
        .engineName = "NewbieRenderer",
    });

    auto success = [&] {
        auto scene = loadToyCar(app);
        if (!scene.has_value())
        {
            return false;
        }

        auto& presentation = app.renderer().device().presentationContext;
        auto graphSpec = buildRtObjectGraphSpec(presentation.swapchainFormat());
        app.renderer().installGraph(graphSpec);

        return renderUntilRtSceneReady(app, scene->get());
    }();

    app.shutdown();
    return success;
}
} // namespace

int main()
{
    return runSmokeTest() ? 0 : 1;
}
