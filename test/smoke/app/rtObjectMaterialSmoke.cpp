import std;
import dependency;
import nr.app;
import nr.load;
import nr.options;
import nr.renderer;
import nr.renderPasses;
import nr.rhi;
import nr.scene;
import nr.utils;

namespace
{
[[nodiscard]] nr::options::OptionFrameSnapshot makeDefaultSnapshot(
    const nr::renderer::RendererGraphPreflightResult& preflight)
{
    auto values = nr::options::OptionValueMap{};
    auto availability = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(preflight.optionCatalog->definitions(), [&](auto const& entry) {
        values.emplace(entry.first, entry.second.defaultValue);
        availability.emplace(
            entry.first,
            nr::options::OptionAvailability{.available = true, .reason = {}});
    });
    return nr::options::OptionFrameSnapshot{
        .catalog = preflight.optionCatalog,
        .values = std::move(values),
        .availability = std::move(availability),
        .frameIndex = 1u,
        .revision = 1u,
        .graphGeneration = 1u,
        .bindingEpoch = 1u,
        .snapshotToken = "rtobject-material-snapshot",
    };
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildRtObjectGraphSpec(vk::Format swapchainFormat)
{
    auto asBuild = std::make_shared<nr::renderPasses::AccelerationStructureBuildNode>();
    auto lightPrepare = std::make_shared<nr::renderPasses::LightPrepareNode>();
    auto rayTrace = std::make_shared<nr::renderPasses::PathTracingNode>();
    auto dlssRayReconstruction = std::make_shared<nr::renderPasses::DlssRayReconstructionNode>();
    auto dlssResolutionController = std::make_shared<nr::renderPasses::DlssRayReconstructionResolutionController>();
    dlssRayReconstruction->setResolutionController(dlssResolutionController);
    dlssRayReconstruction->input.enabled = true;
    dlssRayReconstruction->input.create.quality = nr::rhi::DlssQuality::Dlaa;
    dlssRayReconstruction->input.create.depthType = nr::rhi::DlssDepthType::Hardware;
    dlssRayReconstruction->input.evaluate.visualizeMotionVectors = true;
    dlssRayReconstruction->input.outputColorKey = std::string{nr::renderer::frameResource::presentSourceColor};

    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = swapchainFormat;

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.cameraJitter = nr::renderer::RendererCameraJitterConfig{
        .sequence = nr::renderer::RendererCameraJitterSequence::Halton23,
        .cycleLength = nr::renderer::kRendererDefaultCameraJitterCycleLength,
    };
    graphSpec.frameResolutionResolver = [controller = std::move(dlssResolutionController)](
                                            nr::rhi::Device& device,
                                            vk::Extent2D displayExtent,
                                            const nr::options::OptionFrameSnapshot& snapshot) {
        auto query = nr::renderPasses::DlssRayReconstructionResolutionController::OptimalSettingsQuery{[&device](nr::rhi::DlssDimensions targetSize, nr::rhi::DlssQuality quality) { return device.dlssContext()->optimalSettings(targetSize, quality); }};
        return controller->resolve(
            nr::renderPasses::dlssResolutionRequestFromSnapshot(snapshot),
            displayExtent,
            query);
    };
    graphSpec.frameResolutionOptionRequirements = {
        nr::options::optionId(nr::options::keys::dlssEnabled),
        nr::options::optionId(nr::options::keys::dlssQuality),
        nr::options::optionId(nr::options::keys::dlssBypass),
    };
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
            .runtime = dlssRayReconstruction,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "DlssRayReconstruction",
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
            .debugName = "RtObjectMaterialSmoke.GraphicsToCompute",
            .afterNodeIndex = 2u,
        },
    };

    return graphSpec;
}

[[nodiscard]] std::filesystem::path toyCarPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "assets" / "glTF-Sample-Assets" / "Models" / "ToyCar" / "glTF" / "ToyCar.gltf";
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::scene::Scene>> loadToyCar(nr::app::AppSession &app)
{
    auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = toyCarPath(),
    });
    if (!loadResult.has_value())
    {
        std::println("[error] rtobject material smoke failed to load ToyCar: {}", loadResult.error().message);
        return std::nullopt;
    }

    auto &scene = app.createScene();
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
    nr::scene::Scene& scene,
    const nr::options::OptionFrameSnapshot& optionSnapshot,
    std::optional<nr::renderer::RendererCameraOverride> cameraOverride = {})
{
    auto &renderer = app.renderer();
    auto &presentation = renderer.device().presentationContext;

    presentation.pollEvents();
    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .optionSnapshot = std::cref(optionSnapshot),
        .scene = std::ref(scene),
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
        .sceneExtractInput = nr::scene::SceneExtractInput{},
        .cameraOverride = cameraOverride.value_or(app.camera().buildRendererCameraOverride()),
    });

    if (!frameResult.rendered)
    {
        std::println("[error] rtobject material smoke frame was not rendered.");
        return std::nullopt;
    }

    if (frameResult.presentResult == vk::Result::eErrorOutOfDateKHR || frameResult.presentResult == vk::Result::eSuboptimalKHR)
    {
        renderer.resize();
        return renderOneFrame(app, scene, optionSnapshot, std::move(cameraOverride));
    }

    if (frameResult.presentResult != vk::Result::eSuccess)
    {
        std::println("[error] rtobject material smoke present failed with {}.", vk::to_string(frameResult.presentResult));
        return std::nullopt;
    }

    return frameResult;
}

[[nodiscard]] bool renderUntilRtSceneReady(
    nr::app::AppSession& app,
    nr::scene::Scene& scene,
    const nr::options::OptionFrameSnapshot& optionSnapshot)
{
    auto failed = false;
    auto observedRtFrame = false;
    auto const attempts = std::views::iota(0, 12);
    std::ranges::for_each(attempts, [&](int) {
        if (failed || observedRtFrame)
        {
            return;
        }

        auto frameResult = renderOneFrame(app, scene, optionSnapshot);
        if (!frameResult.has_value())
        {
            failed = true;
            return;
        }

        observedRtFrame = frameResult->sceneTlasPacketCount > 0u && frameResult->invokedPassPrepareCount >= 3u && frameResult->invokedPassRecordCount >= 6u;
    });

    if (failed)
    {
        return false;
    }

    if (!observedRtFrame)
    {
        std::println("[error] rtobject material smoke did not observe ready ToyCar TLAS packets, LightPrepare prepare, and RT/MV-debug graph passes.");
        return false;
    }

    return true;
}

[[nodiscard]] bool renderCameraMotionFrames(
    nr::app::AppSession& app,
    nr::scene::Scene& scene,
    const nr::options::OptionFrameSnapshot& optionSnapshot)
{
    auto movedCamera = app.camera().viewer();
    auto const previousPosition = movedCamera.frame().position;
    movedCamera.applyControl(nr::renderer::ViewerCameraControlInput{
        .deltaSeconds = 1.0f / 30.0f,
        .moveRight = true,
    });
    auto const movedPosition = movedCamera.frame().position;
    if (glm::length(movedPosition - previousPosition) <= std::numeric_limits<float>::epsilon())
    {
        std::println("[error] rtobject material smoke failed to move the test camera.");
        return false;
    }

    if (!renderOneFrame(
            app,
            scene,
            optionSnapshot,
            movedCamera.buildRendererCameraOverride()).has_value())
    {
        std::println("[error] rtobject material smoke failed while rendering camera motion.");
        return false;
    }

    if (!renderOneFrame(
            app,
            scene,
            optionSnapshot,
            movedCamera.buildRendererCameraOverride()).has_value())
    {
        std::println("[error] rtobject material smoke failed on the stationary frame after camera motion.");
        return false;
    }
    return true;
}

[[nodiscard]] bool verifySkeletonReuse(const nr::renderer::RenderGraphSkeletonCacheStatistics &before, const nr::renderer::RenderGraphSkeletonCacheStatistics &after)
{
    if (after.missCount <= before.missCount)
    {
        std::println("[error] rtobject material smoke did not observe a cold RenderGraph Skeleton miss.");
        return false;
    }

    if (after.hitCount <= before.hitCount)
    {
        std::println("[error] rtobject material smoke did not observe a patch-only RenderGraph Skeleton hit.");
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

        auto &presentation = app.renderer().device().presentationContext;
        auto graphSpec = buildRtObjectGraphSpec(presentation.swapchainFormat());
        auto const preflight = app.renderer().preflightGraph(graphSpec);
        if (!preflight || !app.renderer().installGraph(graphSpec))
        {
            return false;
        }
        auto const optionSnapshot = makeDefaultSnapshot(preflight);

        auto const skeletonStatisticsBefore = app.renderer().renderGraphSkeletonStatistics();
        if (!renderUntilRtSceneReady(app, scene->get(), optionSnapshot) ||
            !renderCameraMotionFrames(app, scene->get(), optionSnapshot))
        {
            return false;
        }
        auto const skeletonStatisticsAfter = app.renderer().renderGraphSkeletonStatistics();
        return verifySkeletonReuse(skeletonStatisticsBefore, skeletonStatisticsAfter);
    }();

    app.shutdown();
    return success;
}
} // namespace

int main()
{
    return runSmokeTest() ? 0 : 1;
}
