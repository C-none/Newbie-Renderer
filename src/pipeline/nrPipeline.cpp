module nr.pipeline;
import dependency.vulkan;

import nr.app;
import nr.automation;
import nr.interaction;
import nr.options;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;

namespace nr::pipeline
{
namespace
{
struct PreparedPipelineGraph
{
    nr::renderer::RendererGraphSpec spec{};
    std::shared_ptr<const nr::options::OptionCatalog> optionCatalog{};
};

struct MutationFrameResult
{
    std::optional<nr::options::FrameEffect> effect{};
};

[[nodiscard]] RtPostProcessingMode postProcessingMode(std::string_view value) noexcept
{
    return value == "accumulate" ? RtPostProcessingMode::accumulate : RtPostProcessingMode::dlssRayReconstruction;
}

[[nodiscard]] std::expected<PreparedPipelineGraph, std::string> preparePipelineGraph(nr::renderer::Renderer &renderer, const RenderPipelineRegistry &registry, std::string_view pipelineId, RtPostProcessingMode mode, RtDlssQuality dlssQuality, std::string_view captureSessionId)
{
    auto pipeline = registry.find(pipelineId);
    if (!pipeline.has_value())
    {
        return std::unexpected(std::format("Unknown pipeline: {}", pipelineId));
    }

    auto &presentation = renderer.device().presentationContext;
    auto graphSpec = pipeline->get().buildGraph(PipelineBuildContext{
        .swapchainFormat = presentation.swapchainFormat(),
        .swapchainExtent = presentation.swapchainExtent(),
        .rtPostProcessingMode = mode,
        .rtDlssQuality = dlssQuality,
        .captureSessionId = std::string{captureSessionId},
    });
    if (graphSpec.nodes.empty())
    {
        return std::unexpected(std::format("Pipeline '{}' produced an empty graph.", pipelineId));
    }

    auto const preflight = renderer.preflightGraph(graphSpec);
    if (!preflight)
    {
        return std::unexpected(preflight.message);
    }
    nrAssert(preflight.optionCatalog != nullptr, "Successful renderer graph preflight must publish an option catalog.");
    return PreparedPipelineGraph{
        .spec = std::move(graphSpec),
        .optionCatalog = preflight.optionCatalog,
    };
}

void installPreparedPipelineGraph(nr::renderer::Renderer &renderer, const PreparedPipelineGraph &prepared, bool reloadShaderSession)
{
    if (reloadShaderSession && renderer.graphInstalled())
    {
        renderer.uninstallGraph();
        nr::rhi::ShaderService::instance().reloadSession();
    }
    nrAssert(renderer.installGraph(prepared.spec), "Renderer graph initialization failed after the destructive graph replacement barrier.");
}

[[nodiscard]] std::filesystem::path projectRelativePath(const std::filesystem::path &path)
{
    auto ec = std::error_code{};
    auto const root = std::filesystem::canonical(std::filesystem::path{std::string{nr::projectRoot}}, ec);
    nrAssert(!ec, "Failed to canonicalize the project root.");
    auto const canonical = std::filesystem::canonical(path, ec);
    nrAssert(!ec, "Failed to canonicalize a fixed project asset path.");
    return canonical.lexically_relative(root).lexically_normal();
}

[[nodiscard]] bool isRootRelativeOptionPath(std::string_view value)
{
    auto const path = std::filesystem::path{value};
    return !path.empty() && !path.is_absolute() && !path.has_root_name() && !path.has_root_directory();
}

[[nodiscard]] std::string makeCaptureSessionId()
{
    auto const ticks = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return std::format("run_{}", ticks);
}

[[nodiscard]] std::shared_ptr<const nr::options::OptionCatalog> buildSessionCatalog(const RenderPipelineRegistry &registry, std::string selectedPipeline, std::string modelSource, std::string environmentSource, const nr::app::AppSession &app)
{
    auto pipelineIds = registry.pipelines() | std::views::transform([](const RenderPipelineDesc &pipeline) { return pipeline.id; }) | std::ranges::to<std::vector>();
    auto const camera = app.camera().optionResetValues();
    auto definitions = nr::options::makeSessionDefinitions(nr::options::SessionDefinitionSeed{
        .pipelineIds = std::move(pipelineIds),
        .selectedPipeline = std::move(selectedPipeline),
        .modelSource = std::move(modelSource),
        .environmentSource = std::move(environmentSource),
        .postProcessingMode = "dlss_ray_reconstruction",
        .fullscreen = app.renderer().device().presentationContext.fullscreenEnabled(),
        .cameraPose = camera.pose,
        .verticalFovDegrees = camera.verticalFovDegrees,
        .clipPlanes = camera.clipPlanes,
    });
    auto builder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(definitions, [&](nr::options::OptionDefinition &definition) { static_cast<void>(builder.add(std::move(definition))); });
    auto built = builder.build();
    nrAssert(built.valid(), "The fixed session option catalog must pass preflight.");
    return std::move(built.catalog);
}

[[nodiscard]] std::expected<void, std::string> validateSessionAndGraphCatalog(const nr::options::OptionCatalog &catalogWithSessionDefinitions, const nr::options::OptionCatalog &graphCatalog)
{
    auto builder = nr::options::OptionCatalogBuilder{};
    std::ranges::for_each(catalogWithSessionDefinitions.definitions() | std::views::filter([](const auto &entry) { return entry.second.scope == nr::options::OptionScope::session; }), [&](const auto &entry) { static_cast<void>(builder.add(entry.second)); });
    std::ranges::for_each(graphCatalog.definitions(), [&](const auto &entry) { static_cast<void>(builder.add(entry.second)); });
    auto combined = builder.build();
    if (combined.valid())
    {
        return {};
    }

    auto const &issue = combined.issues.front();
    return std::unexpected(issue.id ? std::format("Combined option catalog rejected '{}': {}", issue.id->value(), issue.detail) : std::format("Combined option catalog rejected: {}", issue.detail));
}

[[nodiscard]] nr::options::OptionAvailabilityMap fullyAvailable(const nr::options::OptionCatalog &session, const nr::options::OptionCatalog &graph)
{
    auto availability = nr::options::OptionAvailabilityMap{};
    auto add = [&](const nr::options::OptionCatalog &catalog) {
        std::ranges::for_each(catalog.definitions(), [&](auto const &entry) {
            availability.emplace(entry.first, nr::options::OptionAvailability{
                                                  .available = true,
                                                  .reason = {},
                                              });
        });
    };
    add(session);
    add(graph);
    return availability;
}

void emitTerminal(std::uint64_t sequence, const nr::options::OptionId &id, std::uint64_t frameIndex, nr::options::MutationOrigin origin, const std::optional<std::string> &requestId, bool succeeded, std::optional<std::string> reason = {})
{
    auto record = nr::options::OptionMachineRecord{
        .sequence = sequence,
        .id = id,
        .phase = nr::options::OptionLogPhase::terminal,
        .status = succeeded ? nr::options::OptionLogStatus::succeeded : nr::options::OptionLogStatus::failed,
        .frameIndex = frameIndex,
        .origin = origin,
        .requestId = requestId,
        .reason = std::move(reason),
    };
    if (succeeded)
    {
        nr::options::emitMachineRecord(record);
    }
    else
    {
        nr::options::emitMachineRecord<nr::LogLevel::error>(record);
    }
}

[[nodiscard]] MutationFrameResult executeMutation(nr::app::AppSession &app, const RenderPipelineRegistry &registry, ModelHistory &history, SceneModelController &modelController, RtDlssQuality initialDlssQuality, std::string_view captureSessionId,
                                                  const nr::options::OptionFrameSnapshot &previousSnapshot, std::uint64_t frameIndex, nr::options::ScheduledMutation mutation)
{
    auto &options = app.options();
    auto const sequence = mutation.sequence();
    auto const id = mutation.request().id;
    auto const origin = mutation.request().origin;
    auto const requestId = mutation.request().requestId;
    auto fail = [&](std::string reason) {
        static_cast<void>(options.discardMutation(std::move(mutation)));
        emitTerminal(sequence, id, frameIndex, origin, requestId, false, std::move(reason));
        return MutationFrameResult{};
    };

    auto const validation = options.validateForExecution(mutation);
    if (validation != nr::options::ScheduleRejectReason::none)
    {
        return fail(std::string{nr::options::wireName(validation)});
    }

    auto const pipelineOption = nr::options::optionId(nr::options::keys::viewerPipelineSelected);
    auto const postProcessingOption = nr::options::optionId(nr::options::keys::viewerRtPostProcessingMode);
    if (id == pipelineOption || id == postProcessingOption)
    {
        auto const *currentPipeline = previousSnapshot.find(nr::options::keys::viewerPipelineSelected);
        auto const *currentMode = previousSnapshot.find(nr::options::keys::viewerRtPostProcessingMode);
        nrAssert(currentPipeline != nullptr && currentMode != nullptr, "Graph mutation requires the session pipeline options.");
        auto pipelineId = id == pipelineOption ? std::get<std::string>(mutation.request().value.storage) : *currentPipeline;
        auto modeName = id == postProcessingOption ? std::get<std::string>(mutation.request().value.storage) : *currentMode;
        if (id == postProcessingOption && pipelineId != rtObjectPipelineId)
        {
            return fail("unavailable");
        }

        auto prepared = preparePipelineGraph(app.renderer(), registry, pipelineId, postProcessingMode(modeName), initialDlssQuality, captureSessionId);
        if (!prepared)
        {
            return fail(std::move(prepared.error()));
        }
        auto combinedCatalog = validateSessionAndGraphCatalog(*previousSnapshot.catalog, *prepared->optionCatalog);
        if (!combinedCatalog)
        {
            return fail(std::move(combinedCatalog.error()));
        }
        installPreparedPipelineGraph(app.renderer(), *prepared, true);
        auto committed = options.commitGraphReplacement(std::move(mutation), prepared->optionCatalog);
        nrAssert(committed.committed, "Option graph commit failed after the destructive graph replacement barrier.");
        emitTerminal(sequence, id, frameIndex, origin, requestId, true);
        return {};
    }

    if (id == nr::options::optionId(nr::options::keys::viewerModelSource))
    {
        auto const &source = std::get<std::string>(mutation.request().value.storage);
        if (!isRootRelativeOptionPath(source))
        {
            return fail("model_source_must_be_assets_root_relative");
        }
        auto report = modelController.loadModel(app, source, std::ref(history));
        if (!report.loaded)
        {
            return fail(std::move(report.message));
        }
        auto committed = options.commitModelAndCameraReset(std::move(mutation), app.camera().optionResetValues());
        nrAssert(committed.committed, "Model and derived camera option commit failed after Scene commit.");
        emitTerminal(sequence, id, frameIndex, origin, requestId, true);
        return {};
    }

    if (id == nr::options::optionId(nr::options::keys::viewerEnvironmentSource))
    {
        auto const &source = std::get<std::string>(mutation.request().value.storage);
        if (!isRootRelativeOptionPath(source))
        {
            return fail("environment_source_must_be_project_root_relative");
        }
        auto loaded = detail::loadEnvironmentMap(app.renderer(), source);
        if (!loaded)
        {
            return fail(std::move(loaded.error()));
        }
    }
    else if (id == nr::options::optionId(nr::options::keys::viewerWindowFullscreen))
    {
        app.renderer().device().presentationContext.setFullscreen(std::get<bool>(mutation.request().value.storage));
    }

    auto const *definition = previousSnapshot.catalog->find(id);
    nrAssert(definition != nullptr, "Validated option mutation lost its definition.");
    if (definition->lifetime == nr::options::OptionValueLifetime::frameEffect)
    {
        auto materialized = options.materializeFrameEffect(std::move(mutation));
        if (!materialized.effect)
        {
            emitTerminal(sequence, id, frameIndex, origin, requestId, false, std::string{nr::options::wireName(materialized.reason)});
            return {};
        }
        return MutationFrameResult{.effect = std::move(materialized.effect)};
    }

    auto committed = options.commitCanonical(std::move(mutation));
    if (!committed.committed)
    {
        emitTerminal(sequence, id, frameIndex, origin, requestId, false, std::string{nr::options::wireName(committed.reason)});
        return {};
    }
    emitTerminal(sequence, id, frameIndex, origin, requestId, true);
    return {};
}

[[nodiscard]] nr::options::OptionAvailabilityMap collectAvailability(nr::app::AppSession &app, const nr::options::OptionFrameSnapshot &collectionSnapshot)
{
    auto availability = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(collectionSnapshot.catalog->definitions(), [&](auto const &entry) {
        availability.emplace(entry.first, nr::options::OptionAvailability{
                                              .available = true,
                                              .reason = {},
                                          });
    });
    auto const *pipeline = collectionSnapshot.find(nr::options::keys::viewerPipelineSelected);
    if (pipeline != nullptr && *pipeline != rtObjectPipelineId)
    {
        availability.insert_or_assign(nr::options::optionId(nr::options::keys::viewerRtPostProcessingMode), nr::options::OptionAvailability{
                                                                                                                .reason = "pipeline_not_rtobject",
                                                                                                            });
    }
    app.renderer().collectOptionAvailability(collectionSnapshot, availability);
    return availability;
}

[[nodiscard]] bool handlePresentResult(vk::Result presentResult)
{
    if (nr::rhi::PresentationContext::needsSwapchainRecreate(presentResult))
    {
        return true;
    }

    if (presentResult != vk::Result::eSuccess)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", std::format("Unexpected present result: {}", vk::to_string(presentResult)));
        return false;
    }

    return true;
}
} // namespace

[[nodiscard]] ViewerCommandLineOptions parseViewerCommandLine(std::span<char *> args)
{
    auto options = ViewerCommandLineOptions{};
    auto tokens = args | std::views::transform([](const char *token) { return std::string_view{token}; }) | std::ranges::to<std::vector>();

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

        if (token == "--interaction")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size())
            {
                options.errorMessage = "--interaction requires human, agent, or offline-lua.";
                return;
            }
            auto const value = tokens[valueIndex];
            if (value == "human")
            {
                options.interactionMode = ViewerInteractionMode::human;
            }
            else if (value == "agent")
            {
                options.interactionMode = ViewerInteractionMode::agent;
            }
            else if (value == "offline-lua")
            {
                options.interactionMode = ViewerInteractionMode::offlineLua;
            }
            else
            {
                options.errorMessage = std::format("Unknown interaction mode: {}", value);
                return;
            }
            consumed[valueIndex] = true;
            return;
        }

        if (token == "--script")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size() || tokens[valueIndex].empty())
            {
                options.errorMessage = "--script requires an automation-root-relative .lua path.";
                return;
            }
            options.automationScript = std::filesystem::path{tokens[valueIndex]};
            consumed[valueIndex] = true;
            return;
        }

        if (token == "--benchmark")
        {
            options.benchmark = true;
            return;
        }

        auto parseCount = [&](std::string_view option, std::uint32_t &destination) {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size())
            {
                options.errorMessage = std::format("{} requires a non-negative integer value.", option);
                return;
            }
            auto value = std::uint32_t{};
            auto const [end, parseError] = std::from_chars(tokens[valueIndex].data(), tokens[valueIndex].data() + tokens[valueIndex].size(), value);
            if (parseError != std::errc{} || end != tokens[valueIndex].data() + tokens[valueIndex].size())
            {
                options.errorMessage = std::format("{} requires a non-negative integer value.", option);
                return;
            }
            destination = value;
            consumed[valueIndex] = true;
        };
        if (token == "--warmup-frames")
        {
            parseCount(token, options.warmupFrames);
            return;
        }
        if (token == "--measure-frames")
        {
            parseCount(token, options.measureFrames);
            return;
        }
        if (token == "--output")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size() || tokens[valueIndex].empty())
            {
                options.errorMessage = "--output requires a directory value.";
                return;
            }
            options.outputDirectory = std::filesystem::path{tokens[valueIndex]};
            consumed[valueIndex] = true;
            return;
        }
        if (token == "--dlss-quality")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size())
            {
                options.errorMessage = "--dlss-quality requires 'ultra-performance'.";
                return;
            }
            if (tokens[valueIndex] != "ultra-performance")
            {
                options.errorMessage = "--dlss-quality currently accepts only 'ultra-performance'.";
                return;
            }
            options.dlssQuality = RtDlssQuality::ultraPerformance;
            consumed[valueIndex] = true;
            return;
        }
        if (token == "--render-graph-skeleton")
        {
            auto const valueIndex = index + 1u;
            if (valueIndex >= tokens.size())
            {
                options.errorMessage =
                    "--render-graph-skeleton requires 'legacy' or 'enabled'.";
                return;
            }
            if (tokens[valueIndex] == "legacy")
            {
                options.benchmarkRenderGraphSkeletonMode =
                    nr::renderer::RenderGraphSkeletonMode::Legacy;
            }
            else if (tokens[valueIndex] == "enabled")
            {
                options.benchmarkRenderGraphSkeletonMode =
                    nr::renderer::RenderGraphSkeletonMode::Enabled;
            }
            else
            {
                options.errorMessage =
                    "--render-graph-skeleton accepts only 'legacy' or 'enabled'.";
                return;
            }
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
    if (options.benchmark && (options.measureFrames == 0u || options.outputDirectory.empty()))
    {
        options.errorMessage = "--benchmark requires --measure-frames N (N > 0) and --output <directory>.";
    }
    if (options.benchmark && options.dlssQuality != RtDlssQuality::ultraPerformance)
    {
        options.errorMessage = "--benchmark requires --dlss-quality ultra-performance.";
    }
    if (options.interactionMode == ViewerInteractionMode::offlineLua && options.automationScript.empty())
    {
        options.errorMessage = "--interaction offline-lua requires --script <path.lua>.";
    }
    if (options.interactionMode != ViewerInteractionMode::offlineLua && !options.automationScript.empty())
    {
        options.errorMessage = "--script is valid only with --interaction offline-lua.";
    }
    if (!options.benchmark &&
        options.benchmarkRenderGraphSkeletonMode.has_value())
    {
        options.errorMessage =
            "--render-graph-skeleton is available only with --benchmark.";
    }
    return options;
}

void printViewerUsage(std::string_view executableName)
{
    std::println("Usage:");
    std::println("  {} [model_path] [--pipeline normalview|rtobject]", executableName);
    std::println("  {} [model_path] --interaction agent", executableName);
    std::println("  {} [model_path] --interaction offline-lua --script <path.lua>", executableName);
    std::println(
        "  {} --benchmark --warmup-frames N --measure-frames N --output <directory> "
        "[--dlss-quality ultra-performance] "
        "[--render-graph-skeleton legacy|enabled]",
        executableName);
    std::println("  {} --help", executableName);
    std::println("Controls:");
    std::println("  Move: W/S/A/D/Q/E");
    std::println("  Rotate: hold mouse left or right button and move cursor");
    std::println("");
    std::println("If no model_path is provided, the default Sponza model is loaded.");
}

[[nodiscard]] int runViewer(ViewerRunConfig config)
{
    if (config.benchmark && !benchmarkExecutionSupported)
    {
        nr::nrLog(
            nr::LogLevel::error,
            "PIPELINE",
            "--benchmark requires an optimized Release executable with validation disabled.");
        return 2;
    }

    auto registry = makeDefaultPipelineRegistry();
    if (!registry.contains(config.initialPipelineId))
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", std::format("Unknown initial pipeline: {}", config.initialPipelineId));
        return 2;
    }
    if (config.benchmark && config.initialPipelineId != rtObjectPipelineId)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", "--benchmark is currently supported only by the rtobject pipeline.");
        return 2;
    }
    if (config.benchmark &&
        config.benchmarkRenderGraphSkeletonMode ==
            nr::renderer::RenderGraphSkeletonMode::Differential)
    {
        nr::nrLog(
            nr::LogLevel::error,
            "PIPELINE",
            "--benchmark does not support Differential RenderGraph Skeleton timing.");
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

    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = config.appName,
        .engineName = config.engineName,
    });
    auto const authorityMode = [&] {
        switch (config.interactionMode)
        {
        case ViewerInteractionMode::human:
            return nr::options::AuthorityMode::human;
        case ViewerInteractionMode::agent:
            return nr::options::AuthorityMode::agent;
        case ViewerInteractionMode::offlineLua:
            return nr::options::AuthorityMode::offlineLua;
        }
        std::unreachable();
    }();
    nrAssert(app.options().setAuthorityMode(authorityMode), "Option authority mode must be selected before session initialization.");

    auto environmentLoad = detail::loadEnvironmentMap(app.renderer(), config.initialEnvironmentMapPath);
    if (!environmentLoad)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", environmentLoad.error());
        app.shutdown();
        return 1;
    }

    auto history = ModelHistory{};
    history.load();

    auto modelController = SceneModelController{};
    auto initialLoad = modelController.loadModel(app, config.initialModelPath, std::ref(history));
    if (!initialLoad.loaded)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", initialLoad.message);
        app.shutdown();
        return 1;
    }

    auto const captureSessionId = makeCaptureSessionId();
    auto initialGraph = preparePipelineGraph(app.renderer(), registry, config.initialPipelineId, RtPostProcessingMode::dlssRayReconstruction, config.dlssQuality, captureSessionId);
    if (!initialGraph)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", initialGraph.error());
        app.shutdown();
        return 2;
    }

    auto sessionCatalog = buildSessionCatalog(registry, config.initialPipelineId, initialLoad.modelPath.generic_string(), projectRelativePath(environmentLoad->sourcePath).generic_string(), app);
    auto combinedCatalog = validateSessionAndGraphCatalog(*sessionCatalog, *initialGraph->optionCatalog);
    if (!combinedCatalog)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", combinedCatalog.error());
        app.shutdown();
        return 2;
    }
    installPreparedPipelineGraph(app.renderer(), *initialGraph, false);

    auto initialAvailability = fullyAvailable(*sessionCatalog, *initialGraph->optionCatalog);
    auto initializedOptions = app.options().initializeSession(std::move(sessionCatalog), initialGraph->optionCatalog, initialAvailability);
    if (!initializedOptions.committed)
    {
        nr::nrLog(nr::LogLevel::error, "PIPELINE", std::format("Failed to initialize OptionSystem: {}", initializedOptions.detail));
        app.shutdown();
        return 2;
    }

    auto webSocketHost = nr::interaction::OptionWebSocketHost{app.options()};
    if (config.interactionMode == ViewerInteractionMode::agent)
    {
        auto started = webSocketHost.start();
        if (!started.started)
        {
            nr::nrLog(nr::LogLevel::error, "PIPELINE", std::format("Failed to start the option WebSocket host: {}", started.detail));
            app.shutdown();
            return 2;
        }
    }

    auto luaHost = nr::automation::OfflineLuaHost{};
    if (config.interactionMode == ViewerInteractionMode::offlineLua)
    {
        auto started = luaHost.start(app.options(), config.automationScript);
        if (!started.started)
        {
            nr::nrLog(nr::LogLevel::error, "PIPELINE", std::format("Failed to start offline Lua automation: {}", started.detail));
            webSocketHost.stop();
            app.shutdown();
            return 2;
        }
    }

    if (config.benchmark)
    {
        app.renderer().configureRenderGraphSkeletonMode(
            config.benchmarkRenderGraphSkeletonMode);
        app.renderer().configureBenchmark(nr::renderer::RendererBenchmarkConfig{
            .enabled = true,
            .warmupFrames = config.warmupFrames,
            .measureFrames = config.measureFrames,
            .outputDirectory = config.outputDirectory,
            .dlssQuality = "ultra-performance",
            .modelPath = initialLoad.modelPath.string(),
            .pipelineId = config.initialPipelineId,
            .renderGraphSkeletonMode =
                config.benchmarkRenderGraphSkeletonMode,
            .commandLine = config.commandLine,
        });
    }

    auto frameServices = app.makeFrameServices();
    auto &presentation = app.renderer().device().presentationContext;
    auto previousTick = std::chrono::steady_clock::now();
    auto exitCode = 0;
    auto framebufferWasUnavailable = false;
    auto optionUiPresenter = nr::app::OptionUiPresenter{};
    auto stopAfterCurrentFrame = false;

    while (!presentation.windowShouldClose())
    {
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

        if (!app.renderer().initialized() || !app.renderer().graphInstalled())
        {
            nr::nrLog(nr::LogLevel::error, "PIPELINE", "Renderable viewer iteration requires an initialized renderer and installed graph.");
            exitCode = 1;
            break;
        }

        auto previousSnapshot = app.options().snapshot();
        nrAssert(previousSnapshot != nullptr, "Renderable viewer iteration requires a published option snapshot.");
        auto frameStart = app.options().beginRenderableFrame();
        nrAssert(frameStart.has_value(), "Renderable viewer iteration failed to close OptionSystem admission.");
        auto mutationResult = MutationFrameResult{};
        if (frameStart->mutation.has_value())
        {
            mutationResult = executeMutation(app, registry, history, modelController, config.dlssQuality, captureSessionId, *previousSnapshot, frameStart->frameIndex, std::move(*frameStart->mutation));
        }

        auto collectionSnapshot = app.options().snapshotForCollection(mutationResult.effect);
        nrAssert(collectionSnapshot != nullptr, "OptionSystem failed to provide its closed-gate collection snapshot.");
        auto availability = collectAvailability(app, *collectionSnapshot);
        auto optionSnapshot = app.options().publishRenderableFrame(availability, std::move(mutationResult.effect));
        nrAssert(optionSnapshot != nullptr, "OptionSystem failed to publish the renderable frame snapshot.");

        if (config.interactionMode == ViewerInteractionMode::offlineLua)
        {
            auto luaFrame = luaHost.resume(optionSnapshot);
            if (luaFrame.status == nr::automation::OfflineLuaFrameStatus::completed)
            {
                stopAfterCurrentFrame = true;
            }
            else if (luaFrame.status == nr::automation::OfflineLuaFrameStatus::failed || luaFrame.status == nr::automation::OfflineLuaFrameStatus::notStarted)
            {
                nr::nrLog(nr::LogLevel::error, "LUA", luaFrame.detail.empty() ? "Offline Lua automation stopped unexpectedly." : luaFrame.detail);
                exitCode = 1;
                stopAfterCurrentFrame = true;
            }
        }

        app.ui().beginFrame(presentation, deltaSeconds);
        app.camera().syncFromSnapshot(*optionSnapshot, presentation);
        if (!config.benchmark)
        {
            auto const uiInteractionPolicy = config.interactionMode == ViewerInteractionMode::human ? nr::app::OptionUiInteractionPolicy::interactive : nr::app::OptionUiInteractionPolicy::readOnly;
            auto uiResult = optionUiPresenter.present(app.ui(), app.options(), optionSnapshot, uiInteractionPolicy);
            if (uiInteractionPolicy == nr::app::OptionUiInteractionPolicy::interactive)
            {
                if (uiResult.mutationAttempted)
                {
                    app.camera().discardPresentationInput(presentation, deltaSeconds, app.ui().captureState());
                }
                else
                {
                    static_cast<void>(app.camera().tryScheduleFromPresentation(app.options(), *optionSnapshot, presentation, deltaSeconds, app.ui().captureState()));
                }
            }
        }
        app.ui().setCameraFrame(app.camera().frame());
        auto const cameraOverride = app.camera().buildRendererCameraOverride();

        auto frameInput = nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(*optionSnapshot),
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
        if (config.benchmark && app.renderer().benchmarkComplete())
        {
            break;
        }
        if (stopAfterCurrentFrame)
        {
            break;
        }
    }

    if (config.benchmark && exitCode == 0 && !app.renderer().finalizeBenchmark())
    {
        exitCode = 1;
    }
    luaHost.stop();
    webSocketHost.stop();
    app.shutdown();
    return exitCode;
}

[[nodiscard]] int runViewerFromCommandLine(std::span<char *> args)
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

    auto logSession = nr::RotatingNdjsonLogSession::start({});
    if (!logSession)
    {
        nr::nrLog(nr::LogLevel::error, "LOG", std::format("Failed to start rotating NDJSON logs: {}", logSession.error()));
        return 2;
    }

    auto commandLine = std::string{};
    std::ranges::for_each(args, [&](const char *argument) {
        if (!commandLine.empty())
        {
            commandLine += ' ';
        }
        commandLine += argument;
    });
    return runViewer(ViewerRunConfig{
        .initialModelPath = options.modelPath,
        .initialPipelineId = options.pipelineId,
        .appName = "NewbieRenderer",
        .engineName = "NewbieRenderer",
        .benchmark = options.benchmark,
        .warmupFrames = options.warmupFrames,
        .measureFrames = options.measureFrames,
        .outputDirectory = options.outputDirectory,
        .dlssQuality = options.dlssQuality,
        .interactionMode = options.interactionMode,
        .automationScript = options.automationScript,
        .benchmarkRenderGraphSkeletonMode =
            options.benchmarkRenderGraphSkeletonMode.value_or(
                nr::renderer::RenderGraphSkeletonMode::Enabled),
        .commandLine = std::move(commandLine),
    });
}
} // namespace nr::pipeline
