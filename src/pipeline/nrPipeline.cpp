module nr.pipeline;
import dependency.vulkan;

import nr.app;
import nr.load;
import nr.renderer;
import nr.renderPasses;
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
};

struct ViewerControlState
{
    std::string activePipelineId{std::string{normalViewPipelineId}};
    std::filesystem::path activeModelPath{};
    std::string modelInput{};
    std::string statusMessage{};
    ViewerPendingRequests pending{};
};

[[nodiscard]] std::string pipelineDisplayName(const RenderPipelineDesc& desc)
{
    return desc.displayName.empty() ? desc.id : desc.displayName;
}

[[nodiscard]] std::string pathStorageKey(const std::filesystem::path& path)
{
    auto key = normalizeModelPathForStorage(path).string();
    std::ranges::transform(key, key.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return key;
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildNormalViewGraph(const PipelineBuildContext& context)
{
    auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();

    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = context.swapchainFormat;

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
            .debugName = "normalview.GraphicsToCompute",
            .afterNodeIndex = 1u,
        },
    };
    return graphSpec;
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildRtObjectGraph(const PipelineBuildContext& context)
{
    auto asBuild = std::make_shared<nr::renderPasses::AccelerationStructureBuildNode>();
    auto lightPrepare = std::make_shared<nr::renderPasses::LightPrepareNode>();
    auto rayTrace = std::make_shared<nr::renderPasses::PathTracingNode>();
    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto accumulate = std::make_shared<nr::renderPasses::AccumulateNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    present->input.format = context.swapchainFormat;

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.cameraJitter = nr::renderer::RendererCameraJitterConfig{
        .sequence = nr::renderer::RendererCameraJitterSequence::Halton23,
        .cycleLength = nr::renderer::kRendererDefaultCameraJitterCycleLength,
    };
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = asBuild,
            .config = nr::renderer::NodeConfig{
                .instanceName = "ASBuild",
                .queue = nr::renderer::QueueDomain::Graphics,
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = lightPrepare,
            .config = nr::renderer::NodeConfig{
                .instanceName = "LightPrepare",
                .queue = nr::renderer::QueueDomain::Graphics,
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = rayTrace,
            .config = nr::renderer::NodeConfig{
                .instanceName = "PathTracing",
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
            .runtime = accumulate,
            .config = nr::renderer::NodeConfig{
                .instanceName = "Accumulate",
                .queue = nr::renderer::QueueDomain::Compute,
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
            .debugName = "rtobject.GraphicsToCompute",
            .afterNodeIndex = 3u,
        },
    };
    return graphSpec;
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

    auto const activeKey = pathStorageKey(controls.activeModelPath);
    std::ranges::for_each(history.entries(), [&](const std::filesystem::path& entry) {
        auto const selected = pathStorageKey(entry) == activeKey;
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

[[nodiscard]] bool handlePresentResult(nr::app::AppSession& app, vk::Result presentResult)
{
    auto& renderer = app.renderer();
    if (nr::rhi::PresentationContext::needsSwapchainRecreate(presentResult))
    {
        auto& presentation = renderer.device().presentationContext;
        if (presentation.framebufferAvailable() && !presentation.hasPendingAcquire())
        {
            renderer.resize();
        }
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

[[nodiscard]] bool RenderPipelineRegistry::registerPipeline(RenderPipelineDesc desc)
{
    if (desc.id.empty() || !desc.buildGraph || indexById_.contains(desc.id))
    {
        return false;
    }

    if (desc.displayName.empty())
    {
        desc.displayName = desc.id;
    }

    auto const index = pipelines_.size();
    indexById_.emplace(desc.id, index);
    pipelines_.push_back(std::move(desc));
    return true;
}

[[nodiscard]] std::optional<std::reference_wrapper<const RenderPipelineDesc>> RenderPipelineRegistry::find(
    std::string_view id) const noexcept
{
    auto const it = indexById_.find(std::string{id});
    if (it == indexById_.end())
    {
        return std::nullopt;
    }

    nrAssert(it->second < pipelines_.size(), "RenderPipelineRegistry index is out of range.");
    return std::cref(pipelines_[it->second]);
}

[[nodiscard]] std::span<const RenderPipelineDesc> RenderPipelineRegistry::pipelines() const noexcept
{
    return std::span<const RenderPipelineDesc>{pipelines_.data(), pipelines_.size()};
}

[[nodiscard]] bool RenderPipelineRegistry::empty() const noexcept
{
    return pipelines_.empty();
}

[[nodiscard]] bool RenderPipelineRegistry::contains(std::string_view id) const noexcept
{
    return find(id).has_value();
}

void registerDefaultPipelines(RenderPipelineRegistry& registry)
{
    [[maybe_unused]] auto const normalRegistered = registry.registerPipeline(RenderPipelineDesc{
        .id = std::string{normalViewPipelineId},
        .displayName = std::string{normalViewPipelineId},
        .buildGraph = buildNormalViewGraph,
    });
    nr::nrAssert(normalRegistered, "registerDefaultPipelines failed to register normalview.");

    [[maybe_unused]] auto const rtRegistered = registry.registerPipeline(RenderPipelineDesc{
        .id = std::string{rtObjectPipelineId},
        .displayName = std::string{rtObjectPipelineId},
        .buildGraph = buildRtObjectGraph,
    });
    nr::nrAssert(rtRegistered, "registerDefaultPipelines failed to register rtobject.");
}

[[nodiscard]] RenderPipelineRegistry makeDefaultPipelineRegistry()
{
    auto registry = RenderPipelineRegistry{};
    registerDefaultPipelines(registry);
    return registry;
}

[[nodiscard]] std::filesystem::path defaultModelPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "assets" / "glTF-Sample-Assets" / "Models" /
           "Sponza" / "glTF" / "Sponza.gltf";
}

[[nodiscard]] std::filesystem::path modelHistoryFilePath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "app" / "model-history.txt";
}

[[nodiscard]] std::filesystem::path normalizeModelPathForStorage(const std::filesystem::path& path)
{
    if (path.empty())
    {
        return {};
    }

    auto ec = std::error_code{};
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

[[nodiscard]] std::string displayPathLeafFirst(const std::filesystem::path& path)
{
    auto normalized = path.lexically_normal();
    auto parts = std::vector<std::string>{};
    std::ranges::for_each(normalized.relative_path(), [&](const std::filesystem::path& part) {
        auto text = part.string();
        if (!text.empty() && text != ".")
        {
            parts.push_back(std::move(text));
        }
    });

    if (parts.empty())
    {
        return path.string();
    }

    std::ranges::reverse(parts);
    auto root = normalized.root_path().string();
    if (!root.empty())
    {
        parts.push_back(std::move(root));
    }

    auto output = std::string{};
    auto indices = std::views::iota(std::size_t{0u}, parts.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (!output.empty())
        {
            output += " / ";
        }
        output += parts[index];
    });
    return output;
}

ModelHistory::ModelHistory(std::filesystem::path storagePath, std::size_t maxEntries)
    : storagePath_(std::move(storagePath))
    , maxEntries_(std::max<std::size_t>(1u, maxEntries))
{
}

void ModelHistory::load()
{
    entries_.clear();

    auto input = std::ifstream{storagePath_};
    if (!input)
    {
        return;
    }

    auto line = std::string{};
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        auto normalized = normalizeModelPathForStorage(std::filesystem::path{line});
        if (normalized.empty())
        {
            continue;
        }
        auto duplicate = std::ranges::any_of(entries_, [&](const std::filesystem::path& entry) {
            return sameStoredPath(entry, normalized);
        });
        if (!duplicate)
        {
            entries_.push_back(std::move(normalized));
        }
        trimToLimit();
    }
}

void ModelHistory::save() const
{
    auto ec = std::error_code{};
    std::filesystem::create_directories(storagePath_.parent_path(), ec);
    if (ec)
    {
        nr::nrLog(
            nr::LogLevel::warning,
            "PIPELINE",
            std::format("Failed to create model history directory '{}': {}", storagePath_.parent_path().string(), ec.message()));
        return;
    }

    auto output = std::ofstream{storagePath_, std::ios::trunc};
    if (!output)
    {
        nr::nrLog(
            nr::LogLevel::warning,
            "PIPELINE",
            std::format("Failed to write model history '{}'.", storagePath_.string()));
        return;
    }

    std::ranges::for_each(entries_, [&](const std::filesystem::path& entry) {
        output << entry.string() << '\n';
    });
}

void ModelHistory::noteLoaded(const std::filesystem::path& path)
{
    auto normalized = normalizeModelPathForStorage(path);
    if (normalized.empty())
    {
        return;
    }

    std::erase_if(entries_, [&](const std::filesystem::path& entry) {
        return sameStoredPath(entry, normalized);
    });
    entries_.insert(entries_.begin(), std::move(normalized));
    trimToLimit();
}

[[nodiscard]] std::span<const std::filesystem::path> ModelHistory::entries() const noexcept
{
    return std::span<const std::filesystem::path>{entries_.data(), entries_.size()};
}

[[nodiscard]] const std::filesystem::path& ModelHistory::storagePath() const noexcept
{
    return storagePath_;
}

[[nodiscard]] bool ModelHistory::sameStoredPath(
    const std::filesystem::path& lhs,
    const std::filesystem::path& rhs) const
{
    return pathStorageKey(lhs) == pathStorageKey(rhs);
}

void ModelHistory::trimToLimit()
{
    if (entries_.size() > maxEntries_)
    {
        entries_.resize(maxEntries_);
    }
}

[[nodiscard]] ModelLoadReport SceneModelController::loadModel(
    nr::app::AppSession& app,
    const std::filesystem::path& modelPath,
    std::optional<std::reference_wrapper<ModelHistory>> history)
{
    auto normalizedPath = normalizeModelPathForStorage(modelPath);
    if (normalizedPath.empty())
    {
        return ModelLoadReport{
            .message = "Model path is empty.",
        };
    }

    auto ec = std::error_code{};
    if (!std::filesystem::exists(normalizedPath, ec) || ec)
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Model file not found: {}", normalizedPath.string()),
        };
    }

    nr::nrLog(nr::LogLevel::info, "PIPELINE", std::format("Loading model: {}", normalizedPath.string()));
    auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = normalizedPath,
    });
    if (!loadResult.has_value())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Failed to load model: {}", loadResult.error().message),
        };
    }

    auto& sceneAsset = loadResult.value();
    auto& scene = app.createScene();
    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!templateHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = "Failed to register scene template.",
        };
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!instanceHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = "Failed to instantiate scene.",
        };
    }

    app.resetCameraFromSceneOrDefault();
    currentModelPath_ = normalizedPath;

    if (history.has_value())
    {
        history->get().noteLoaded(normalizedPath);
        history->get().save();
    }

    auto message = std::format(
        "Loaded: {} meshes, {} vertices, {} indices, {} lights",
        sceneAsset.stats.meshCount,
        sceneAsset.stats.vertexCount,
        sceneAsset.stats.indexCount,
        sceneAsset.stats.lightCount);
    nr::nrLog(nr::LogLevel::info, "PIPELINE", message);
    return ModelLoadReport{
        .loaded = true,
        .modelPath = normalizedPath,
        .message = std::move(message),
    };
}

[[nodiscard]] const std::optional<std::filesystem::path>& SceneModelController::currentModelPath() const noexcept
{
    return currentModelPath_;
}

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

    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = config.appName,
        .engineName = config.engineName,
    });

    auto history = ModelHistory{};
    history.load();

    auto modelController = SceneModelController{};
    auto controls = ViewerControlState{
        .activePipelineId = config.initialPipelineId,
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

        if (!handlePresentResult(app, frameResult.presentResult))
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
