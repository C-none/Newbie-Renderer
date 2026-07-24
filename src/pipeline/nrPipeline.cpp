module nr.pipeline;
import dependency.vulkan;

import nr.app;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;

namespace nr::pipeline
{
namespace
{
struct ViewerPendingRequests
{
    std::optional<std::string> pipelineId{};
    std::optional<std::filesystem::path> modelPath{};
    std::optional<std::filesystem::path> environmentMapPath{};
    std::optional<RtPostProcessingMode> rtPostProcessingMode{};
};

struct PipelineUiComponent
{
    std::function<std::vector<nr::app::UiSection>()> buildSections{};
};

struct ViewerControlState
{
    std::string activePipelineId{std::string{defaultPipelineId}};
    std::filesystem::path activeModelPath{};
    std::filesystem::path activeEnvironmentMapPath{};
    std::vector<EnvironmentMapAsset> environmentMapAssets{};
    std::string modelInput{};
    std::string statusMessage{};
    RtPostProcessingMode rtPostProcessingMode = RtPostProcessingMode::dlssRayReconstruction;
    std::optional<PipelineUiComponent> activePipelineUi{};
    ViewerPendingRequests pending{};
};

[[nodiscard]] std::string pipelineDisplayName(const RenderPipelineDesc& desc)
{
    return desc.displayName.empty() ? desc.id : desc.displayName;
}

[[nodiscard]] bool installPipeline(
    nr::renderer::Renderer& renderer,
    const RenderPipelineRegistry& registry,
    std::string_view pipelineId,
    ViewerControlState& controls)
{
    auto pipeline = registry.find(pipelineId);
    if (!pipeline.has_value())
    {
        controls.statusMessage = std::format("Unknown pipeline: {}", pipelineId);
        nr::nrLog(nr::LogLevel::error, "PIPELINE", controls.statusMessage);
        return false;
    }

    auto& presentation = renderer.device().presentationContext;
    auto graphSpec = pipeline->get().buildGraph(PipelineBuildContext{
        .swapchainFormat = presentation.swapchainFormat(),
        .swapchainExtent = presentation.swapchainExtent(),
        .rtPostProcessingMode = controls.rtPostProcessingMode,
    });
    if (graphSpec.nodes.empty())
    {
        controls.statusMessage = std::format("Pipeline '{}' produced an empty graph.", pipelineId);
        nr::nrLog(nr::LogLevel::error, "PIPELINE", controls.statusMessage);
        return false;
    }

    auto const replacingInstalledGraph = renderer.graphInstalled();
    if (replacingInstalledGraph)
    {
        renderer.uninstallGraph();
        nr::rhi::ShaderService::instance().reloadSession();
    }
    renderer.installGraph(graphSpec);

    controls.activePipelineId = std::string{pipelineId};
    if (pipelineId == rtObjectPipelineId)
    {
        controls.activePipelineUi = PipelineUiComponent{
            .buildSections = [&controls] {
                return detail::buildRtObjectUi(
                    controls.rtPostProcessingMode,
                    controls.pending.rtPostProcessingMode);
            },
        };
    }
    else
    {
        controls.activePipelineUi.reset();
    }
    return true;
}

void drawPipelineCombo(
    nr::app::UiSystem& ui,
    const RenderPipelineRegistry& registry,
    ViewerControlState& controls)
{
    auto activeLabel = controls.activePipelineId;
    if (auto active = registry.find(controls.activePipelineId); active.has_value())
    {
        activeLabel = pipelineDisplayName(active->get());
    }

    if (!ui.beginCombo("Pipeline", activeLabel))
    {
        return;
    }

    std::ranges::for_each(registry.pipelines(), [&](const RenderPipelineDesc& pipeline) {
        auto const selected = pipeline.id == controls.activePipelineId;
        if (ui.selectable(pipelineDisplayName(pipeline), selected))
        {
            controls.pending.pipelineId = pipeline.id;
        }
        if (selected)
        {
            ui.setItemDefaultFocus();
        }
    });
    ui.endCombo();
}

void drawModelHistoryCombo(
    nr::app::UiSystem& ui,
    const ModelHistory& history,
    ViewerControlState& controls)
{
    auto preview = controls.activeModelPath.empty()
                       ? std::string{"No model"}
                       : displayPathLeafFirst(controls.activeModelPath);
    if (!ui.beginCombo("Model History", preview))
    {
        return;
    }

    auto const activeKey = detail::normalizedModelPathKey(controls.activeModelPath);
    std::ranges::for_each(history.entries(), [&](const std::filesystem::path& entry) {
        auto const selected = detail::normalizedModelPathKey(entry) == activeKey;
        auto const label = std::format("{}##{}", displayPathLeafFirst(entry), entry.string());
        if (ui.selectable(label, selected))
        {
            controls.pending.modelPath = entry;
            controls.modelInput = entry.string();
        }
        if (selected)
        {
            ui.setItemDefaultFocus();
        }
    });
    ui.endCombo();
}

void drawEnvironmentMapCombo(
    nr::app::UiSystem& ui,
    ViewerControlState& controls)
{
    auto const activeAsset = std::ranges::find(
        controls.environmentMapAssets,
        controls.activeEnvironmentMapPath,
        &EnvironmentMapAsset::sourcePath);
    auto const preview = activeAsset != controls.environmentMapAssets.end()
                             ? activeAsset->displayName
                             : controls.activeEnvironmentMapPath.stem().string();
    if (!ui.beginCombo("Environment Map", preview))
    {
        return;
    }

    std::ranges::for_each(controls.environmentMapAssets, [&](const EnvironmentMapAsset& asset) {
        auto const selected = asset.sourcePath == controls.activeEnvironmentMapPath;
        auto const label = std::format("{}##{}", asset.displayName, asset.sourcePath.filename().string());
        if (ui.selectable(label, selected) && !selected)
        {
            controls.pending.environmentMapPath = asset.sourcePath;
        }
        if (selected)
        {
            ui.setItemDefaultFocus();
        }
    });
    ui.endCombo();
}

void queueViewerControls(
    nr::app::UiSystem& ui,
    const RenderPipelineRegistry& registry,
    const ModelHistory& history,
    ViewerControlState& controls)
{
    ui.queueSection(nr::app::UiSection{
        .id = "viewer.controls",
        .title = "Viewer",
        .draw = [&](nr::app::UiSystem& sectionUi) {
            drawPipelineCombo(sectionUi, registry, controls);
            drawEnvironmentMapCombo(sectionUi, controls);
            drawModelHistoryCombo(sectionUi, history, controls);
            static_cast<void>(sectionUi.inputText("Model Path", controls.modelInput));
            if (sectionUi.button("Load"))
            {
                controls.pending.modelPath = std::filesystem::path{controls.modelInput};
            }
            if (!controls.statusMessage.empty())
            {
                sectionUi.separator();
                sectionUi.text(controls.statusMessage);
            }
        },
    });

    if (controls.activePipelineUi.has_value() && controls.activePipelineUi->buildSections)
    {
        auto sections = controls.activePipelineUi->buildSections();
        std::ranges::for_each(sections, [&](nr::app::UiSection& section) {
            ui.queueSection(std::move(section));
        });
    }
}

[[nodiscard]] bool processPendingRequests(
    nr::app::AppSession& app,
    const RenderPipelineRegistry& registry,
    ModelHistory& history,
    SceneModelController& modelController,
    ViewerControlState& controls)
{
    auto success = true;
    if (controls.pending.pipelineId.has_value())
    {
        if (*controls.pending.pipelineId != controls.activePipelineId)
        {
            success = installPipeline(app.renderer(), registry, *controls.pending.pipelineId, controls) && success;
        }
        controls.pending.pipelineId.reset();
    }

    if (controls.pending.rtPostProcessingMode.has_value())
    {
        auto const requestedMode = *controls.pending.rtPostProcessingMode;
        controls.pending.rtPostProcessingMode.reset();
        if (controls.activePipelineId == rtObjectPipelineId && requestedMode != controls.rtPostProcessingMode)
        {
            auto const previousMode = controls.rtPostProcessingMode;
            controls.rtPostProcessingMode = requestedMode;
            if (!installPipeline(app.renderer(), registry, rtObjectPipelineId, controls))
            {
                controls.rtPostProcessingMode = previousMode;
                success = false;
            }
        }
    }

    if (controls.pending.environmentMapPath.has_value())
    {
        auto environmentLoad = detail::loadEnvironmentMap(
            app.renderer(),
            *controls.pending.environmentMapPath);
        if (environmentLoad)
        {
            controls.activeEnvironmentMapPath = environmentLoad->sourcePath;
            controls.statusMessage = std::format(
                "Loaded environment map: {}",
                environmentLoad->displayName);
        }
        else
        {
            controls.statusMessage = environmentLoad.error();
            nr::nrLog(nr::LogLevel::error, "PIPELINE", controls.statusMessage);
            success = false;
        }
        controls.pending.environmentMapPath.reset();
    }

    if (controls.pending.modelPath.has_value())
    {
        auto report = modelController.loadModel(app, *controls.pending.modelPath, std::ref(history));
        if (report.loaded)
        {
            controls.activeModelPath = report.modelPath;
            controls.modelInput = report.modelPath.string();
        }
        controls.statusMessage = std::move(report.message);
        success = report.loaded && success;
        controls.pending.modelPath.reset();
    }

    return success;
}

[[nodiscard]] bool handlePresentResult(vk::Result presentResult)
{
    if (nr::rhi::PresentationContext::needsSwapchainRecreate(presentResult))
    {
        return true;
    }

    if (presentResult != vk::Result::eSuccess)
    {
        nr::nrLog(
            nr::LogLevel::error,
            "PIPELINE",
            std::format("Unexpected present result: {}", vk::to_string(presentResult)));
        return false;
    }

    return true;
}
} // namespace

[[nodiscard]] ViewerCommandLineOptions parseViewerCommandLine(std::span<char*> args)
{
    auto options = ViewerCommandLineOptions{};
    auto tokens = args |
                  std::views::transform([](const char* token) {
                      return std::string_view{token};
                  }) |
                  std::ranges::to<std::vector>();

    auto consumed = std::vector<bool>(tokens.size(), false);
    auto indices = std::views::iota(std::size_t{0u}, tokens.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (consumed[index] || !options.errorMessage.empty())
        {
            return;
        }

        auto const token = tokens[index];
        if (token == "--help" || token == "-h")
        {
            options.showHelp = true;
            return;
        }

        if (token == "--pipeline")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size())
            {
                options.errorMessage = "--pipeline requires a value.";
                return;
            }
            options.pipelineId = std::string{tokens[valueIndex]};
            consumed[valueIndex] = true;
            return;
        }

        if (token.starts_with("-"))
        {
            options.errorMessage = std::format("Unknown argument: {}", token);
            return;
        }

        if (!options.modelPath.empty())
        {
            options.errorMessage = std::format("Unexpected extra model path: {}", token);
            return;
        }
        options.modelPath = std::filesystem::path{token};
    });

    if (options.modelPath.empty())
    {
        options.modelPath = defaultModelPath();
    }
    return options;
}

void printViewerUsage(std::string_view executableName)
{
    std::println("Usage:");
    std::println("  {} [model_path] [--pipeline normalview|rtobject]", executableName);
    std::println("  {} --help", executableName);
    std::println("Controls:");
    std::println("  Move: W/S/A/D/Q/E");
    std::println("  Rotate: hold mouse left or right button and move cursor");
    std::println("");
    std::println("If no model_path is provided, the default Sponza model is loaded.");
}

[[nodiscard]] int runViewer(ViewerRunConfig config)
{
    auto registry = makeDefaultPipelineRegistry();
    if (!registry.contains(config.initialPipelineId))
    {
        nr::nrLog(
            nr::LogLevel::error,
            "PIPELINE",
            std::format("Unknown initial pipeline: {}", config.initialPipelineId));
        return 2;
    }

    if (config.initialModelPath.empty())
    {
        config.initialModelPath = defaultModelPath();
    }
    if (config.initialEnvironmentMapPath.empty())
    {
        config.initialEnvironmentMapPath = defaultEnvironmentMapPath();
    }

    auto environmentMapAssets = discoverEnvironmentMapAssets();
    if (!environmentMapAssets)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", environmentMapAssets.error());
        return 1;
    }

    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = config.appName,
        .engineName = config.engineName,
    });

    auto environmentLoad = detail::loadEnvironmentMap(
        app.renderer(),
        config.initialEnvironmentMapPath);
    if (!environmentLoad)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", environmentLoad.error());
        app.shutdown();
        return 1;
    }

    auto history = ModelHistory{};
    history.load();

    auto modelController = SceneModelController{};
    auto controls = ViewerControlState{
        .activePipelineId = config.initialPipelineId,
        .activeEnvironmentMapPath = environmentLoad->sourcePath,
        .environmentMapAssets = std::move(*environmentMapAssets),
        .modelInput = normalizeModelPathForStorage(config.initialModelPath).string(),
    };

    auto initialLoad = modelController.loadModel(app, config.initialModelPath, std::ref(history));
    if (!initialLoad.loaded)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", initialLoad.message);
        app.shutdown();
        return 1;
    }
    controls.activeModelPath = initialLoad.modelPath;
    controls.modelInput = initialLoad.modelPath.string();
    controls.statusMessage = initialLoad.message;

    if (!installPipeline(app.renderer(), registry, config.initialPipelineId, controls))
    {
        app.shutdown();
        return 2;
    }

    auto frameServices = app.makeFrameServices();
    auto& presentation = app.renderer().device().presentationContext;
    auto previousTick = std::chrono::steady_clock::now();
    auto exitCode = 0;
    auto framebufferWasUnavailable = false;

    while (!presentation.windowShouldClose())
    {
        static_cast<void>(processPendingRequests(app, registry, history, modelController, controls));

        presentation.pollEvents();
        auto now = std::chrono::steady_clock::now();
        auto deltaSeconds = std::chrono::duration<float>(now - previousTick).count();
        previousTick = now;

        if (!presentation.framebufferAvailable())
        {
            framebufferWasUnavailable = true;
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
            continue;
        }

        if (framebufferWasUnavailable)
        {
            app.renderer().resize();
            framebufferWasUnavailable = false;
        }

        app.ui().beginFrame(presentation, deltaSeconds);
        queueViewerControls(app.ui(), registry, history, controls);
        app.camera().updateFromPresentation(presentation, deltaSeconds, app.ui().captureState());
        app.ui().setCameraFrame(app.camera().frame());
        auto const cameraOverride = app.camera().buildRendererCameraOverride();

        auto frameInput = nr::renderer::RendererFrameInput{
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .sceneExtractInput = nr::scene::SceneExtractInput{},
            .cameraOverride = cameraOverride,
            .frameServices = std::ref(frameServices),
        };
        if (auto scene = app.tryScene(); scene.has_value())
        {
            frameInput.scene = std::ref(scene->get());
        }

        auto frameResult = app.renderer().renderFrame(frameInput);
        if (frameResult.rendered)
        {
            app.ui().setCpuStatistics(frameResult.cpuStatistics);
            app.ui().setGpuPassStatistics(frameResult.gpuPassStatistics);
        }

        if (!frameResult.rendered)
        {
            nr::nrLog(nr::LogLevel::error, "PIPELINE", "Renderer returned rendered=false during main loop.");
            exitCode = 1;
            break;
        }

        if (!handlePresentResult(frameResult.presentResult))
        {
            exitCode = 1;
            break;
        }
    }

    app.shutdown();
    return exitCode;
}

[[nodiscard]] int runViewerFromCommandLine(std::span<char*> args)
{
    auto options = parseViewerCommandLine(args);
    if (options.showHelp)
    {
        printViewerUsage("main");
        return 0;
    }

    if (!options.errorMessage.empty())
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", options.errorMessage);
        printViewerUsage("main");
        return 2;
    }

    return runViewer(ViewerRunConfig{
        .initialModelPath = options.modelPath,
        .initialPipelineId = options.pipelineId,
        .appName = "NewbieRenderer",
        .engineName = "NewbieRenderer",
    });
}
} // namespace nr::pipeline
