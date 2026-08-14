import dependency.vulkan;
import nr.options;
import nr.pipeline;
import nr.renderer;
import nr.test;
import nr.utils;
import std;

namespace
{
[[nodiscard]] nr::pipeline::PipelineBuildContext graphContext()
{
    return nr::pipeline::PipelineBuildContext{
        .swapchainFormat = vk::Format::eR8G8B8A8Unorm,
        .swapchainExtent = vk::Extent2D{128u, 72u},
    };
}

[[nodiscard]] std::vector<char *> makeArgSpanStorage(std::vector<std::string> &values)
{
    auto output = std::vector<char *>{};
    output.reserve(values.size());
    std::ranges::for_each(values, [&](std::string &value) { output.push_back(value.data()); });
    return output;
}

[[nodiscard]] std::filesystem::path testHistoryPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "test" / "pipeline-model-history.txt";
}

[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto const path = std::filesystem::path{std::string{nr::projectRoot}} / std::move(relativePath);
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));
    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string_view sourceSection(std::string_view contents, std::string_view begin, std::string_view end)
{
    auto const beginPosition = contents.find(begin);
    nr::test::require(beginPosition != std::string_view::npos, std::format("missing section start '{}'", begin));
    auto const endPosition = contents.find(end, beginPosition + begin.size());
    nr::test::require(endPosition != std::string_view::npos, std::format("missing section end '{}'", end));
    return contents.substr(beginPosition, endPosition - beginPosition);
}

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.contains(token), std::string{message});
}

void requireAbsent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(!contents.contains(token), std::string{message});
}

void requireOrdered(std::string_view contents, std::string_view first, std::string_view second,
                    std::string_view message)
{
    auto const firstPosition = contents.find(first);
    auto const secondPosition = contents.find(second);
    nr::test::require(firstPosition != std::string_view::npos && secondPosition != std::string_view::npos &&
                          firstPosition < secondPosition,
                      std::string{message});
}

const nr::test::CaseRegistrar registryCase{
    "pipeline default registry exposes normalview and rtobject", [] {
        auto registry = nr::pipeline::makeDefaultPipelineRegistry();

        nr::test::requireEqual(registry.pipelines().size(), std::size_t{2u});
        nr::test::require(registry.contains(nr::pipeline::normalViewPipelineId));
        nr::test::require(registry.contains(nr::pipeline::rtObjectPipelineId));

        auto normal = registry.find(nr::pipeline::normalViewPipelineId);
        nr::test::require(normal.has_value());
        auto normalGraph = normal->get().buildGraph(graphContext());
        nr::test::requireEqual(normalGraph.nodes.size(), std::size_t{3u});
        nr::test::requireEqual(normalGraph.nodes[0].config.instanceName, std::string{"NormalBuffer"});
        nr::test::requireEqual(normalGraph.nodes[0].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(normalGraph.nodes[1].config.instanceName, std::string{"Ui"});
        nr::test::requireEqual(normalGraph.nodes[1].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(normalGraph.nodes[2].config.instanceName, std::string{"Present"});
        nr::test::requireEqual(normalGraph.nodes[2].config.queue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(normalGraph.submitNodes.size(), std::size_t{1u});
        nr::test::requireEqual(normalGraph.submitNodes[0].afterNodeIndex, std::size_t{1u});
        nr::test::require(!normalGraph.frameResolutionResolver.has_value());
        nr::test::require(normalGraph.frameResolutionOptionRequirements.empty(),
                          "normalview must not declare resolver option requirements");

        auto rtObject = registry.find(nr::pipeline::rtObjectPipelineId);
        nr::test::require(rtObject.has_value());
        auto rtGraph = rtObject->get().buildGraph(graphContext());
        nr::test::requireEqual(rtGraph.nodes.size(), std::size_t{6u});
        nr::test::requireEqual(rtGraph.nodes[0].config.instanceName, std::string{"AccelerationStructureBuild"});
        nr::test::requireEqual(rtGraph.nodes[0].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(rtGraph.nodes[1].config.instanceName, std::string{"LightPrepare"});
        nr::test::requireEqual(rtGraph.nodes[1].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(rtGraph.nodes[2].config.instanceName, std::string{"PathTracing"});
        nr::test::requireEqual(rtGraph.nodes[2].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(rtGraph.nodes[3].config.instanceName, std::string{"Ui"});
        nr::test::requireEqual(rtGraph.nodes[3].config.queue, nr::renderer::QueueDomain::Graphics);
        nr::test::requireEqual(rtGraph.nodes[4].config.instanceName, std::string{"DlssRayReconstruction"});
        nr::test::requireEqual(rtGraph.nodes[4].config.queue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(rtGraph.nodes[5].config.instanceName, std::string{"Present"});
        nr::test::requireEqual(rtGraph.nodes[5].config.queue, nr::renderer::QueueDomain::Compute);
        nr::test::requireEqual(rtGraph.submitNodes.size(), std::size_t{1u});
        nr::test::requireEqual(rtGraph.submitNodes[0].afterNodeIndex, std::size_t{3u});
        nr::test::requireEqual(rtGraph.submitNodes[0].debugName, std::string{"rtobject.GraphicsToCompute"});
        nr::test::requireEqual(rtGraph.cameraJitter.sequence, nr::renderer::RendererCameraJitterSequence::Halton23);
        nr::test::requireEqual(rtGraph.cameraJitter.cycleLength, nr::renderer::kRendererDefaultCameraJitterCycleLength);
        nr::test::require(rtGraph.frameResolutionResolver.has_value());
        auto const expectedDlssResolverOptions = std::array{
            nr::options::optionId(nr::options::keys::dlssEnabled),
            nr::options::optionId(nr::options::keys::dlssQuality),
            nr::options::optionId(nr::options::keys::dlssBypass),
        };
        nr::test::require(std::ranges::equal(rtGraph.frameResolutionOptionRequirements, expectedDlssResolverOptions),
                          "DLSS graph resolver requirements must exactly match enabled, quality, and bypass");

        auto accumulateContext = graphContext();
        accumulateContext.rtPostProcessingMode = nr::pipeline::RtPostProcessingMode::accumulate;
        auto accumulateGraph = rtObject->get().buildGraph(accumulateContext);
        nr::test::requireEqual(accumulateGraph.nodes[4].config.instanceName, std::string{"Accumulate"});
        nr::test::require(!accumulateGraph.frameResolutionResolver.has_value());
        nr::test::require(accumulateGraph.frameResolutionOptionRequirements.empty(),
                          "accumulate must not declare resolver option requirements");
    }};

const nr::test::CaseRegistrar historyCase{
    "model history keeps most recent entries and roundtrips under build", [] {
        auto historyPath = testHistoryPath();
        auto ec = std::error_code{};
        std::filesystem::remove(historyPath, ec);

        auto history = nr::pipeline::ModelHistory{historyPath, 3u};
        auto const first =
            std::filesystem::path{"glTF-Sample-Assets/Models/AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf"};
        auto const second = std::filesystem::path{"glTF-Sample-Assets/Models/ClearCoatTest/glTF/ClearCoatTest.gltf"};
        auto const third = std::filesystem::path{"glTF-Sample-Assets/Models/EnvironmentTest/glTF/EnvironmentTest.gltf"};
        history.noteLoaded(first);
        history.noteLoaded(second);
        history.noteLoaded(first);
        history.noteLoaded(third);
        history.save();

        auto reloaded = nr::pipeline::ModelHistory{historyPath, 3u};
        reloaded.load();

        nr::test::requireEqual(reloaded.entries().size(), std::size_t{3u});
        nr::test::requireEqual(reloaded.entries()[0].filename().string(), std::string{"EnvironmentTest.gltf"});
        nr::test::requireEqual(reloaded.entries()[1].filename().string(), std::string{"AlphaBlendModeTest.gltf"});
        nr::test::requireEqual(reloaded.entries()[2].filename().string(), std::string{"ClearCoatTest.gltf"});
        nr::test::require(reloaded.storagePath().string().contains("\\build\\") ||
                          reloaded.storagePath().string().contains("/build/"));
    }};

const nr::test::CaseRegistrar displayCase{
    "model history display labels are leaf first", [] {
        auto relative = nr::pipeline::displayPathLeafFirst(std::filesystem::path{"assets/Box.gltf"});
        nr::test::requireEqual(relative, std::string{"Box.gltf / assets"});

        auto absolute =
            nr::pipeline::displayPathLeafFirst(std::filesystem::path{"D:/file/prog/Newbie-Renderer/assets/Box.gltf"});
        nr::test::require(absolute.starts_with("Box.gltf / assets / Newbie-Renderer"));
        nr::test::require(absolute.ends_with("D:\\") || absolute.ends_with("D:/"));
    }};

const nr::test::CaseRegistrar cliCase{
    "viewer command line parses model path and pipeline", [] {
        auto defaultOptions = nr::pipeline::parseViewerCommandLine({});
        nr::test::requireEqual(defaultOptions.pipelineId, std::string{"rtobject"});

        auto values = std::vector<std::string>{
            "assets/Box.gltf",
            "--pipeline",
            "rtobject",
        };
        auto argv = makeArgSpanStorage(values);
        auto options = nr::pipeline::parseViewerCommandLine(std::span<char *>{argv.data(), argv.size()});

        nr::test::require(!options.showHelp);
        nr::test::require(options.errorMessage.empty());
        nr::test::requireEqual(options.modelPath.string(), std::string{"assets/Box.gltf"});
        nr::test::requireEqual(options.pipelineId, std::string{"rtobject"});

        auto badValues = std::vector<std::string>{"--unknown"};
        auto badArgv = makeArgSpanStorage(badValues);
        auto badOptions = nr::pipeline::parseViewerCommandLine(std::span<char *>{badArgv.data(), badArgv.size()});
        nr::test::require(!badOptions.errorMessage.empty());

        auto benchmarkValues = std::vector<std::string>{"--benchmark",
                                                        "--warmup-frames",
                                                        "12",
                                                        "--measure-frames",
                                                        "24",
                                                        "--output",
                                                        "build/benchmark",
                                                        "--dlss-quality",
                                                        "ultra-performance"};
        auto benchmarkArgv = makeArgSpanStorage(benchmarkValues);
        auto benchmark =
            nr::pipeline::parseViewerCommandLine(std::span<char *>{benchmarkArgv.data(), benchmarkArgv.size()});
        nr::test::require(benchmark.errorMessage.empty());
        nr::test::require(benchmark.benchmark);
        nr::test::requireEqual(benchmark.warmupFrames, 12u);
        nr::test::requireEqual(benchmark.measureFrames, 24u);
        nr::test::requireEqual(benchmark.dlssQuality, nr::pipeline::RtDlssQuality::ultraPerformance);

        auto incompleteValues = std::vector<std::string>{"--benchmark", "--measure-frames", "0"};
        auto incompleteArgv = makeArgSpanStorage(incompleteValues);
        auto incomplete =
            nr::pipeline::parseViewerCommandLine(std::span<char *>{incompleteArgv.data(), incompleteArgv.size()});
        nr::test::require(!incomplete.errorMessage.empty());

        auto missingUpValues =
            std::vector<std::string>{"--benchmark", "--measure-frames", "1", "--output", "build/benchmark"};
        auto missingUpArgv = makeArgSpanStorage(missingUpValues);
        auto missingUp =
            nr::pipeline::parseViewerCommandLine(std::span<char *>{missingUpArgv.data(), missingUpArgv.size()});
        nr::test::require(!missingUp.errorMessage.empty());

        auto agentValues = std::vector<std::string>{"--interaction", "agent"};
        auto agentArgv = makeArgSpanStorage(agentValues);
        auto agent = nr::pipeline::parseViewerCommandLine(std::span<char *>{agentArgv.data(), agentArgv.size()});
        nr::test::require(agent.errorMessage.empty());
        nr::test::requireEqual(agent.interactionMode, nr::pipeline::ViewerInteractionMode::agent);

        auto luaValues = std::vector<std::string>{
            "--interaction",
            "offline-lua",
            "--script",
            "smoke.lua",
        };
        auto luaArgv = makeArgSpanStorage(luaValues);
        auto lua = nr::pipeline::parseViewerCommandLine(std::span<char *>{luaArgv.data(), luaArgv.size()});
        nr::test::require(lua.errorMessage.empty());
        nr::test::requireEqual(lua.interactionMode, nr::pipeline::ViewerInteractionMode::offlineLua);
        nr::test::requireEqual(lua.automationScript.generic_string(), std::string{"smoke.lua"});

        auto removedSkeletonValues = std::vector<std::string>{"--render-graph-skeleton", "enabled"};
        auto removedSkeletonArgv = makeArgSpanStorage(removedSkeletonValues);
        auto removedSkeleton = nr::pipeline::parseViewerCommandLine(
            std::span<char *>{removedSkeletonArgv.data(), removedSkeletonArgv.size()});
        nr::test::require(!removedSkeleton.errorMessage.empty());
    }};

const nr::test::CaseRegistrar benchmarkBuildGateCase{
    "viewer benchmark execution is gated to Release builds before app initialization", [] {
#if defined(NDEBUG)
        nr::test::require(nr::pipeline::benchmarkExecutionSupported);
#else
        nr::test::require(!nr::pipeline::benchmarkExecutionSupported);
        nr::test::requireEqual(
            nr::pipeline::runViewer(
                nr::pipeline::ViewerRunConfig{
                    .benchmark = true,
                }),
            2);
#endif
    }};

const nr::test::CaseRegistrar defaultEnvironmentCase{
    "viewer default environment selects Kloofendal by extension-free name", [] {
        nr::test::requireEqual(nr::pipeline::defaultEnvironmentMapName(),
                               std::string_view{"kloofendal_48d_partly_cloudy_puresky_8k"});
    }};

const nr::test::CaseRegistrar environmentDiscoveryCase{
    "viewer environment choices are sorted extension-free OpenEXR names", [] {
        auto names = nr::pipeline::discoverEnvironmentMapNames();
        nr::test::require(names.has_value(), names.has_value() ? std::string{} : names.error());
        nr::test::require(!names->empty(), "viewer should discover at least one environment map");

        auto const assetDirectory = std::filesystem::path{std::string{nr::projectRoot}} / "assets" / "envMap";
        nr::test::require(
            std::ranges::all_of(*names,
                                [&](const std::string &name) {
                                    auto statusError = std::error_code{};
                                    auto const sourcePath = assetDirectory / std::format("{}.exr", name);
                                    return !name.ends_with(".exr") &&
                                           std::filesystem::is_regular_file(sourcePath, statusError) && !statusError;
                                }),
            "viewer environment choices must hide extensions and resolve under the fixed assets/envMap prefix");
        nr::test::require(std::ranges::is_sorted(*names),
                          "viewer environment choices should have deterministic name ordering");
        nr::test::require(std::ranges::contains(*names, std::string{"brown_photostudio_02_8k"}),
                          "viewer environment discovery should expose known EXR stems");
    }};

const nr::test::CaseRegistrar graphCatalogPreflightOrderingCase{
    "pipeline validates the combined option catalog before any destructive graph replacement", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const graphMutation =
            sourceSection(pipeline, "if (id == pipelineOption || id == postProcessingOption)",
                          "if (id == nr::options::optionId(nr::options::keys::viewerModelSource))");
        requireOrdered(graphMutation, "auto combinedCatalog = validateSessionAndGraphCatalog(",
                       "installPreparedPipelineGraph(app.renderer(), *prepared, true);",
                       "runtime graph mutation must validate the combined session/graph catalog before entering the "
                       "install helper");
        requireOrdered(graphMutation, "if (!combinedCatalog)",
                       "installPreparedPipelineGraph(app.renderer(), *prepared, true);",
                       "runtime graph mutation must handle combined-catalog rejection before graph teardown can begin");

        auto const initialInstall = sourceSection(pipeline, "auto initialGraph =", "auto webSocketHost =");
        requireOrdered(initialInstall, "auto combinedCatalog = validateSessionAndGraphCatalog(",
                       "installPreparedPipelineGraph(app.renderer(), *initialGraph, false);",
                       "initial graph installation must validate the combined session/graph catalog first");
        requireOrdered(initialInstall, "if (!combinedCatalog)",
                       "installPreparedPipelineGraph(app.renderer(), *initialGraph, false);",
                       "initial combined-catalog failure must exit before graph installation");

        auto const installHelper = sourceSection(pipeline, "void installPreparedPipelineGraph(",
                                                 "[[nodiscard]] bool isRootRelativeOptionPath");
        requireOrdered(installHelper, "renderer.uninstallGraph();", "renderer.installGraph(prepared.spec)",
                       "the destructive helper may uninstall only as part of the already-preflighted install path");
    }};

const nr::test::CaseRegistrar sceneCandidateCommitOrderingCase{
    "model replacement completes detached Scene construction before committing ownership", [] {
        auto const model = readProjectFile("src/pipeline/nrPipelineModel.cpp");
        auto const pipelineInterface = readProjectFile("src/pipeline/exportModule.ixx");
        auto const appSessionInterface = readProjectFile("src/app/nrAppSession.ixx");
        auto const appSession = readProjectFile("src/app/nrAppSession.cpp");
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");

        auto const modelControllerInterface =
            sourceSection(pipelineInterface, "class SceneModelController", "struct ViewerCommandLineOptions");
        auto const publicModelApi = sourceSection(modelControllerInterface, "  public:", "  private:");
        requireAbsent(publicModelApi, "ModelCpuLoad",
                      "the public model API must not expose the CPU-load staging token");
        requireAbsent(publicModelApi, "loadModelCpu(",
                      "the public model API must not expose the CPU-load staging factory");
        requireAbsent(publicModelApi, "commitModel(",
                      "the public model API must not expose the main-thread staging consumer");

        auto const modelCpuToken = sourceSection(modelControllerInterface, "class ModelCpuLoad", "using ModelCpuLoadResult");
        requirePresent(modelCpuToken, "ModelCpuLoad(const ModelCpuLoad &) = delete;",
                       "the model CPU-load token must not be copyable");
        requirePresent(modelCpuToken, "friend class SceneModelController;",
                       "only SceneModelController may construct or consume the private CPU-load token");
        requirePresent(modelCpuToken, "ModelCpuLoad(std::filesystem::path normalizedModelPath,",
                       "the model CPU-load token constructor must remain private");
        requireAbsent(modelCpuToken, "resolvedSourcePath",
                      "the model CPU-load token must not retain an unused duplicate source path");
        requirePresent(modelControllerInterface, "friend int runViewer(ViewerRunConfig config);",
                       "only startup orchestration may access the private CPU-load staging boundary");

        auto const cpuModelLoad = sourceSection(
            model, "[[nodiscard]] SceneModelController::ModelCpuLoadResult SceneModelController::loadModelCpu",
            "[[nodiscard]] ModelLoadReport SceneModelController::commitModel");
        requireOrdered(cpuModelLoad, "nr::nrLog<nr::LogLevel::info, \"PIPELINE\">(\"Loading model:",
                       "auto sceneLoad = nr::load::loadScene(",
                       "model CPU loading must log the resolved source before importing it");
        requireOrdered(cpuModelLoad,
                       "auto neuralMaterialBinding = nr::neuralAppearance::loadBindingRequest(modelAssetRootPath(), normalizedPath);",
                       "if (!neuralMaterialBinding)",
                       "a sidecar/artifact failure must be rejected during detached CPU model loading");
        requireOrdered(cpuModelLoad, "if (!neuralMaterialBinding)", "return ModelCpuLoad{",
                       "only a successfully validated neural sidecar may cross into candidate Scene construction");
        requireAbsent(cpuModelLoad, "app.", "the CPU model phase must not touch AppSession state");

        auto const commitModel = sourceSection(
            model, "[[nodiscard]] ModelLoadReport SceneModelController::commitModel",
            "[[nodiscard]] ModelLoadReport SceneModelController::loadModel");
        requireAbsent(commitModel, "normalizeModelPathForStorage(",
                      "main-thread Scene commit must reuse the normalized path produced by CPU loading");
        requireAbsent(commitModel, "resolveModelAssetPath(",
                      "main-thread Scene commit must not resolve the model path again");
        requireAbsent(commitModel, "nr::load::loadScene(",
                      "main-thread Scene commit must not perform CPU scene loading");
        requireOrdered(commitModel, "auto normalizedModelPath = std::move(loadedModel.normalizedModelPath_);",
                       "auto sceneAsset = std::move(loadedModel.sceneAsset_);",
                       "the main-thread commit must consume both token fields at its entry");
        requireOrdered(commitModel, "auto sceneAsset = std::move(loadedModel.sceneAsset_);",
                       "auto candidate = app.makeSceneCandidate();",
                       "the move-only CPU-load token must be consumed before Scene commit begins");
        requireOrdered(commitModel, "auto candidate = app.makeSceneCandidate();",
                       "auto templateHandle = candidate->registerTemplate(",
                       "template registration must target the detached candidate");
        requireOrdered(commitModel, "auto neuralMaterialBinding = std::move(loadedModel.neuralMaterialBinding_);",
                       "auto candidate = app.makeSceneCandidate();",
                       "main-thread Scene commit must consume the parsed neural material binding before candidate construction");
        requireOrdered(commitModel, "if (!templateHandle.valid())",
                       "auto instanceHandle = candidate->instantiate(templateHandle);",
                       "template failure must return before instantiation");
        requireOrdered(commitModel, "if (!instanceHandle.valid())", "app.commitScene(std::move(candidate));",
                       "instance failure must return before the Scene commit boundary");
        requireOrdered(commitModel, "app.commitScene(std::move(candidate));", "app.resetCameraFromSceneOrDefault();",
                       "camera derivation must observe the newly committed Scene");
        requireAbsent(commitModel, "app.destroyScene();",
                      "model replacement must not destroy the active Scene before candidate success");

        auto const loadModel = sourceSection(
            model, "[[nodiscard]] ModelLoadReport SceneModelController::loadModel",
            "[[nodiscard]] const std::optional<std::filesystem::path> &SceneModelController::currentModelPath");
        requireOrdered(loadModel, "auto loadedModel = loadModelCpu(modelPath);",
                       "return commitModel(app, std::move(*loadedModel), history);",
                       "synchronous model reload must reuse the CPU-load and main-thread-commit split");

        requirePresent(appSessionInterface, "std::unique_ptr<nr::scene::Scene> scene_{};",
                       "AppSession must exclusively own the active Scene");
        auto const commitScene = sourceSection(appSession, "void AppSession::commitScene(", "AppSession::createScene(");
        requirePresent(commitScene, "candidate->usesDevice(renderer_.device())",
                       "Scene commit must validate device affinity through the narrow predicate");
        requireAbsent(commitScene, "candidate->device()",
                      "Scene commit must not require the removed wide Scene device facade");
        requireOrdered(commitScene, "renderer_.device().waitIdle();", "renderer_.resetSceneBinding();",
                       "Scene commit must cross the frame/device boundary before unbinding the old Scene");
        requireOrdered(commitScene, "renderer_.resetSceneBinding();", "scene_.swap(candidate);",
                       "renderer Scene bindings must be cleared before ownership swaps");
        requireOrdered(commitScene, "scene_.swap(candidate);", "candidate.reset();",
                       "the old Scene must be destroyed only after the candidate becomes active");

        auto const modelMutation =
            sourceSection(pipeline, "if (id == nr::options::optionId(nr::options::keys::viewerModelSource))",
                          "if (id == nr::options::optionId(nr::options::keys::viewerEnvironmentSource))");
        requireOrdered(modelMutation, "if (!report.loaded)", "options.commitModelAndCameraReset(",
                       "model and derived camera canonical values may commit only after Scene replacement succeeds");
    }};

const nr::test::CaseRegistrar startupLoadConcurrencyCase{
    "viewer startup overlaps CPU asset loading and commits resources on the main thread", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const environment = readProjectFile("src/pipeline/nrPipelineEnvironment.cpp");
        auto const startupLoad = sourceSection(pipeline, "auto startupLoadPool = nr::threading::StaticThreadPool{};",
                                               "auto const captureSessionId =");

        requirePresent(startupLoad, "startupLoadPool.ensureWorkerCount(2u);",
                       "startup asset loading must use exactly two phase-local workers");
        requireOrdered(startupLoad, "auto environmentLoadFuture = startupLoadPool.submit(",
                       "auto modelLoadFuture = startupLoadPool.submit(",
                       "environment and model CPU loads must be submitted before either result is awaited");
        requireOrdered(startupLoad, "auto environmentLoad = environmentLoadFuture.get();",
                       "detail::commitEnvironmentMap(app.renderer(), std::move(*environmentLoad));",
                       "the main thread may commit the environment only after its CPU future completes");
        requireOrdered(startupLoad, "auto initialModelLoad = modelLoadFuture.get();",
                       "modelController.commitModel(app, std::move(*initialModelLoad), std::ref(history));",
                       "the main thread may commit the Scene only after its CPU future completes");

        auto const cpuEnvironmentLoad = sourceSection(
            environment, "[[nodiscard]] std::expected<EnvironmentMapCpuLoad, std::string> loadEnvironmentMapCpu(",
            "void commitEnvironmentMap(");
        requireAbsent(cpuEnvironmentLoad, "renderer.",
                      "the background environment phase must not touch Renderer state");
        auto const environmentCommit = sourceSection(
            environment, "void commitEnvironmentMap(",
            "[[nodiscard]] std::expected<void, std::string> loadEnvironmentMap(");
        requirePresent(environmentCommit, "renderer.setEnvironmentMap(",
                       "the environment upload must remain in the main-thread commit helper");
    }};

const nr::test::CaseRegistrar renderableFrameGateCase{
    "pipeline verifies renderer readiness before consuming an option frame", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const mainLoop = sourceSection(pipeline, "while (!presentation.windowShouldClose())",
                                            "if (config.benchmark && exitCode == 0");
        requireOrdered(mainLoop, "!app.renderer().initialized() || !app.renderer().graphInstalled()",
                       "app.options().beginRenderableFrame();",
                       "renderer and graph readiness must be proven before detaching a pending mutation");
        requireOrdered(mainLoop, "!presentation.framebufferAvailable()", "app.options().beginRenderableFrame();",
                       "zero-sized presentation iterations must retain the pending mutation");
    }};

const nr::test::CaseRegistrar exitOptionShutdownCase{
    "viewer exit is a shared frame effect that finishes the current frame before shutdown", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const exitMutation = sourceSection(
            pipeline, "if (id == nr::options::optionId(nr::options::keys::viewerExit))",
            "auto const pipelineOption = nr::options::optionId(nr::options::keys::viewerPipelineSelected);");
        requireOrdered(exitMutation, "options.materializeFrameEffect(std::move(mutation))",
                       "emitTerminal(sequence, id, frameIndex, origin, requestId, true);",
                       "exit must consume the same frame-effect mutation contract before terminal success");
        requirePresent(exitMutation, "return MutationFrameResult{.exitRequested = true};",
                       "successful exit execution must report a pipeline-owned stop request");

        auto const mainLoop = sourceSection(pipeline, "while (!presentation.windowShouldClose())",
                                            "if (config.benchmark && exitCode == 0");
        requireOrdered(mainLoop, "stopAfterCurrentFrame = stopAfterCurrentFrame || mutationResult.exitRequested;",
                       "auto frameResult = app.renderer().renderFrame(frameInput);",
                       "viewer exit must allow the accepted renderable frame to finish");
        requireOrdered(mainLoop, "auto frameResult = app.renderer().renderFrame(frameInput);",
                       "if (stopAfterCurrentFrame)",
                       "the exit stop check must occur after current-frame rendering and presentation");
        auto const loopAndShutdown =
            sourceSection(pipeline, "while (!presentation.windowShouldClose())", "return exitCode;");
        requireOrdered(loopAndShutdown, "if (stopAfterCurrentFrame)", "webSocketHost.stop();",
                       "interaction hosts must stop only after the exit frame leaves the main loop");
        requireOrdered(loopAndShutdown, "webSocketHost.stop();", "app.shutdown();",
                       "WebSocket admission must close before AppSession shutdown");
    }};

const nr::test::CaseRegistrar uiCameraAdmissionPriorityCase{
    "all interaction modes render the option snapshot while only human UI may submit", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const camera = readProjectFile("src/app/nrAppCamera.cpp");
        auto const presenterInterface = readProjectFile("src/app/nrOptionUiPresenter.ixx");
        auto const presenter = readProjectFile("src/app/nrOptionUiPresenter.cpp");

        requirePresent(presenterInterface, "enum class OptionUiInteractionPolicy",
                       "the presenter API must make the interaction policy explicit");
        requirePresent(presenterInterface, "OptionUiInteractionPolicy interactionPolicy",
                       "every presenter call must choose interactive or read-only behavior");

        auto const readOnlyDrawing = sourceSection(presenter, "void OptionUiPresenter::drawReadOnlyOption(",
                                                   "bool OptionUiPresenter::drawInteractiveOption(");
        requirePresent(readOnlyDrawing, "ui.beginDisabled(true);",
                       "the read-only mirror must disable every actionable option control");
        requireAbsent(readOnlyDrawing, "draftFor(", "the read-only mirror must not create presenter-local drafts");
        requireAbsent(readOnlyDrawing, "schedule(", "the read-only mirror must not enter mutation scheduling");
        requireAbsent(readOnlyDrawing, "trySchedule(", "the read-only mirror must not call OptionSystem admission");
        requireAbsent(readOnlyDrawing, "itemEditCommitted(", "the read-only mirror must not observe or commit edits");

        auto const allUiModes = sourceSection(
            pipeline, "auto uiResult = nr::app::OptionUiPresentResult{};",
            "auto const cameraOverride = app.camera().buildRendererCameraOverride();");
        requirePresent(allUiModes, "config.interactionMode == ViewerInteractionMode::human",
                       "pipeline must derive UI mutability from the active interaction mode");
        requirePresent(allUiModes, "nr::app::OptionUiInteractionPolicy::readOnly",
                       "agent and offline-lua modes must select the read-only mirror");
        requireOrdered(allUiModes, "uiResult = optionUiPresenter.present(",
                       "auto const uiCaptureState = app.ui().finalizeFrame();",
                       "the app must finalize the UI after presenting the shared snapshot");
        requireOrdered(allUiModes, "auto const uiCaptureState = app.ui().finalizeFrame();",
                       "if (!config.benchmark && uiInteractionPolicy ==",
                       "current-frame UI capture must be frozen before human camera admission");

        auto const humanInput = sourceSection(
            pipeline, "if (!config.benchmark && uiInteractionPolicy == nr::app::OptionUiInteractionPolicy::interactive)",
            "auto const cameraOverride = app.camera().buildRendererCameraOverride();");
        requireOrdered(humanInput, "if (uiResult.mutationAttempted)", "app.camera().discardPresentationInput(",
                       "every UI mutation attempt, including a rejected one, must consume presentation input");
        requireOrdered(humanInput, "app.camera().discardPresentationInput(", "else",
                       "camera admission must remain in the branch opposite a UI mutation attempt");
        requireOrdered(humanInput, "else", "app.camera().tryScheduleFromPresentation(",
                       "camera mutation may be attempted only when UI did not attempt a mutation");
        requirePresent(humanInput, "deltaSeconds, uiCaptureState);",
                       "discarded input must observe the current finalized UI capture state");
        requirePresent(humanInput, "deltaSeconds, uiCaptureState));",
                       "camera admission must observe that same current finalized UI capture state");
        requireAbsent(humanInput, "captureState()",
                      "camera arbitration must not read a cached capture state from an earlier UI frame");

        auto const discardInput = sourceSection(camera, "void AppCamera::discardPresentationInput(",
                                                "nr::options::CameraResetValues AppCamera::optionResetValues()");
        requirePresent(discardInput, "detail::sampleControlInput(",
                       "discarding presentation input must still run the normal cursor sampler");
        requirePresent(discardInput, "cursorTracking_",
                       "discarding presentation input must update the persistent cursor baseline");
    }};

const nr::test::CaseRegistrar uiSectionOrderCase{
    "viewer and frame status lead while CPU and GPU performance always trail", [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const presenter = readProjectFile("src/app/nrOptionUiPresenter.cpp");
        auto const uiInterface = readProjectFile("src/app/nrAppUi.ixx");
        auto const uiSystem = readProjectFile("src/app/nrAppUi.cpp");
        auto const uiNode = readProjectFile("src/renderPasses/Ui/nrUiNode.cpp");
        auto const normalBufferUiSmoke = readProjectFile("test/smoke/app/normalBufferUiSmoke.cpp");
        auto const embeddedTriangle = readProjectFile("test/smoke/app/embeddedTriangle.cpp");

        auto const mainLoop = sourceSection(pipeline, "while (!presentation.windowShouldClose())",
                                            "if (config.benchmark && exitCode == 0");
        requireOrdered(mainLoop, "app.ui().setCameraFrame(app.camera().frame());",
                       "queueFrameStatusSection(app.ui(), presentation);",
                       "frame status must observe the current viewer camera");
        requireOrdered(mainLoop, "queueFrameStatusSection(app.ui(), presentation);", "if (!config.benchmark)",
                       "frame status must be queued in every interaction and benchmark mode");
        requireOrdered(mainLoop, "auto const uiCaptureState = app.ui().finalizeFrame();",
                       "auto frameResult = app.renderer().renderFrame(frameInput);",
                       "the app must finalize UI before renderer graph construction consumes draw data");
        requireOrdered(mainLoop, "auto frameResult = app.renderer().renderFrame(frameInput);",
                       "app.ui().setCpuStatistics(frameResult.cpuStatistics);",
                       "CPU diagnostics published after rendering must describe the last completed frame");
        requireOrdered(mainLoop, "app.ui().setCpuStatistics(frameResult.cpuStatistics);",
                       "app.ui().setGpuPassStatistics(frameResult.gpuPassStatistics);",
                       "completed-frame CPU and GPU diagnostics must be published together");

        requirePresent(presenter, "left.presentation.group != kViewerUiGroup",
                       "the Viewer option group must sort before every ordinary option group");
        requireOrdered(
            presenter, "sectionView.first(leadingSectionCount)", "sectionView.subspan(leadingSectionCount)",
            "Viewer must be the leading section and remaining option groups must trail the queued frame status");

        requireOrdered(uiSystem, "drawAppSections(leadingSections);",
                       "std::ranges::for_each(queuedSections_, drawAppSection);",
                       "UiSystem must draw Viewer before queued frame status");
        requireOrdered(uiSystem, "std::ranges::for_each(queuedSections_, drawAppSection);",
                       "drawAppSections(trailingSections);",
                       "UiSystem must draw remaining option groups after queued frame status");

        auto const performanceSections = sourceSection(uiSystem, "UiCaptureState UiSystem::finalizeFrame()",
                                                       "void UiSystem::queueSection(");
        requireAbsent(performanceSections, "frame.status",
                      "UiSystem must not duplicate frame status in its performance tail");
        requireOrdered(performanceSections, "cpu.performance", "gpu.performance",
                       "CPU performance must precede GPU performance at the absolute UI tail");

        auto const uiFinalization = performanceSections;
        requireOrdered(uiFinalization, "auto const performanceSections = std::array{", "renderSections(",
                       "UiSystem finalization must append the performance tail itself");
        requireOrdered(uiFinalization, "renderSections(", "ImGui::Render();",
                       "all app-owned sections must be drawn before the UI frame is finalized");
        requirePresent(uiFinalization, "return captureState;",
                       "UI finalization must return the capture state produced by that same frame");
        requireAbsent(uiInterface, "captureState()",
                      "UiSystem must not expose a cached capture-state read separate from finalization");
        requireAbsent(uiNode, "renderSections(", "UiNode must not render app-owned UI sections");
        requireAbsent(uiNode, "finalizeFrame(", "UiNode must not finalize the app-owned UI frame");

        auto const normalSmokeFrame = sourceSection(normalBufferUiSmoke, "renderOneFrame(",
                                                    "[[nodiscard]] bool renderUntilSceneDraw(");
        requireOrdered(normalSmokeFrame, "app.ui().beginFrame(", "app.ui().finalizeFrame()",
                       "the NormalBuffer smoke path must close each UI transaction");
        requireOrdered(normalSmokeFrame, "app.ui().finalizeFrame()", "renderer.renderFrame(",
                       "the NormalBuffer smoke path must finalize UI before rendering");
        requirePresent(normalSmokeFrame, "drawData->get().TotalVtxCount <= 0",
                       "the NormalBuffer smoke path must retain non-empty UI draw-data validation");
        requirePresent(normalSmokeFrame, "frameResult.invokedPassRecordCount < 2u",
                       "the NormalBuffer smoke path must retain UI and Present pass validation");

        auto const embeddedLoop = sourceSection(embeddedTriangle, "while (!presentation.windowShouldClose())",
                                                "        app.shutdown();");
        requireOrdered(embeddedLoop, "app.ui().beginFrame(", "app.ui().finalizeFrame()",
                       "the embedded-triangle manual loop must close each UI transaction");
        requireOrdered(embeddedLoop, "app.ui().finalizeFrame()", "renderer.renderFrame(",
                       "the embedded-triangle manual loop must finalize UI before rendering");
    }};

const nr::test::CaseRegistrar uiSystemOwnershipAndFrameStateCase{
    "UiSystem owns queued section strings, its ImGui context, and one explicit frame state", [] {
        auto const uiInterface = readProjectFile("src/app/nrAppUi.ixx");
        auto const ui = readProjectFile("src/app/nrAppUi.cpp");
        auto const presenter = readProjectFile("src/app/nrOptionUiPresenter.cpp");

        requirePresent(uiInterface, "std::string id{};",
                       "queued UI section identity must own its storage");
        requirePresent(uiInterface, "std::string title{};",
                       "queued UI section title must own its storage");
        requireAbsent(uiInterface, "std::string_view id{};",
                      "queued section identity must not borrow temporary text");
        requireAbsent(uiInterface, "std::string_view title{};",
                      "queued section title must not borrow temporary text");
        requirePresent(ui, "queuedSections_.push_back(std::move(section));",
                       "queueSection must move the owning section into frame storage");
        requirePresent(presenter, ".id = title,",
                       "the catalog presenter must copy each group identity into its section");
        requirePresent(presenter, ".title = title,",
                       "the catalog presenter must copy each group title into its section");

        requirePresent(uiInterface, "struct ImGuiContextDeleter",
                       "UiSystem must declare an explicit private ImGui context deleter");
        requirePresent(uiInterface, "std::unique_ptr<ImGuiContext, ImGuiContextDeleter> context_{};",
                       "UiSystem must own its ImGui context through unique_ptr");
        requirePresent(ui, "void UiSystem::ImGuiContextDeleter::operator()(ImGuiContext *context) const noexcept",
                       "the explicit ImGui context deleter must have one implementation");
        requirePresent(ui, "ImGui::DestroyContext(context);",
                       "the unique_ptr deleter must destroy the ImGui context");
        requirePresent(ui, "context_.reset(ImGui::CreateContext());",
                       "initialization must immediately transfer context ownership to unique_ptr");
        requireAbsent(uiInterface, "ImGuiContext *context_",
                      "UiSystem must not retain a raw owning ImGui context pointer");
        requireAbsent(ui, "ImGui::DestroyContext(context_",
                      "shutdown must release the context only through its deleter");

        requirePresent(uiInterface, "enum class FrameState : std::uint8_t",
                       "UiSystem must model frame lifecycle with one explicit state");
        requirePresent(uiInterface, "FrameState frameState_ = FrameState::idle;",
                       "UiSystem must begin in the idle frame state");
        requireAbsent(uiInterface, "frameActive_", "the old active-state boolean must be removed");
        requireAbsent(uiInterface, "frameFinalized_", "the old finalized-state boolean must be removed");
        requireAbsent(ui, "frameActive_", "implementation must not retain the old active-state boolean");
        requireAbsent(ui, "frameFinalized_", "implementation must not retain the old finalized-state boolean");

        auto const shutdown = sourceSection(ui, "void UiSystem::shutdown()", "bool UiSystem::initialized()");
        requireOrdered(shutdown, "frameState_ == FrameState::active", "ImGui::EndFrame();",
                       "shutdown must close an active ImGui frame before destroying its context");
        requireOrdered(shutdown, "ImGui::EndFrame();", "context_.reset();",
                       "active-frame cleanup must precede RAII context release");
        requirePresent(shutdown, "frameState_ = FrameState::idle;",
                       "shutdown must restore the idle state");

        auto const beginFrame = sourceSection(ui, "void UiSystem::beginFrame(",
                                              "UiCaptureState UiSystem::finalizeFrame()");
        requireOrdered(beginFrame, "nrAssert(frameState_ != FrameState::active", "ImGui::NewFrame();",
                       "beginFrame must fail fast on an unfinalized active frame");
        requireAbsent(beginFrame, "ImGui::EndFrame();",
                      "beginFrame must not silently discard an unfinalized frame");
        requirePresent(beginFrame, "frameState_ = FrameState::active;",
                       "a successful begin must publish the active state");

        auto const activeFrameGuard = sourceSection(ui, "void UiSystem::requireActiveFrame(",
                                                    "bool UiSystem::beginSection(");
        requireOrdered(activeFrameGuard, "frameState_ == FrameState::active", "nrAssert(false, \"UiSystem::{}",
                       "the normal widget path must return before constructing failure diagnostics");
        requirePresent(activeFrameGuard, "nrAssert(false, \"UiSystem::{}",
                       "invalid widget access must retain an operation-specific fail-fast diagnostic");

        auto const finalizeFrame = sourceSection(ui, "UiCaptureState UiSystem::finalizeFrame()",
                                                 "void UiSystem::queueSection(");
        requireOrdered(finalizeFrame, "frameState_ == FrameState::active", "ImGui::Render();",
                       "finalization must require the active state before rendering");
        requireOrdered(finalizeFrame, "ImGui::Render();", "frameState_ = FrameState::finalized;",
                       "draw data must be rendered before the finalized state is published");

        auto const drawData = sourceSection(ui, "UiSystem::drawData() const noexcept",
                                            "void UiSystem::setCurrentContext()");
        requirePresent(drawData, "frameState_ != FrameState::finalized",
                       "drawData must explicitly return no data for every state except finalized");

        requireAbsent(uiInterface, "class WindowScope", "the zero-use window scope API must be removed");
        requireAbsent(ui, "UiSystem::WindowScope", "the zero-use window scope implementation must be removed");
        requireAbsent(uiInterface, "WindowScope window(", "the zero-use window entrypoint must be removed");
        requireAbsent(ui, "UiSystem::window(", "the zero-use window implementation must be removed");
        requireAbsent(uiInterface, "endWindow(", "the zero-use window closer must be removed");
        requireAbsent(ui, "UiSystem::endWindow(", "the zero-use window closer implementation must be removed");
        requireAbsent(uiInterface, "void renderSections(std::span<const UiSection> sections",
                      "the unused single-span renderSections overload must be removed");
        requireAbsent(ui, "void UiSystem::renderSections(std::span<const UiSection> sections",
                      "the unused single-span renderSections implementation must be removed");
        requireAbsent(uiInterface, "cpuStatistics() const",
                      "CPU statistics must not remain a public implementation-only getter");
        requireAbsent(uiInterface, "gpuPassStatistics() const",
                      "GPU statistics must not remain a public implementation-only getter");
        requireAbsent(ui, "windowsOpenedThisFrame_", "the single-window path must not retain a window counter");
        requireAbsent(ui, "kUiWindowVerticalStride", "the single-window path must not retain a row stride");
        requirePresent(ui, "ImVec2{detail::kUiWindowMargin, detail::kUiWindowMargin}",
                       "the sole UI window must use the fixed margin position");
    }};

const nr::test::CaseRegistrar cameraMovementSpeedBindingCase{
    "canonical camera movement speed updates only the viewer movement control", [] {
        auto const camera = readProjectFile("src/app/nrAppCamera.cpp");
        auto const snapshotBinding =
            sourceSection(camera, "void AppCamera::syncFromSnapshot(", "bool AppCamera::tryScheduleFromPresentation(");
        requirePresent(camera, "requiredValue(snapshot, nr::options::keys::viewerCameraMovementSpeed.id())",
                       "the camera adapter must require the canonical movement speed wire value");
        requireOrdered(snapshotBinding, "auto controlConfig = viewer_.controlConfig();",
                       "controlConfig.movementSpeed = detail::movementSpeedFromSnapshot(snapshot);",
                       "camera synchronization must preserve the existing look and pitch controls");
        requireOrdered(snapshotBinding, "controlConfig.movementSpeed = detail::movementSpeedFromSnapshot(snapshot);",
                       "viewer_.setControlConfig(controlConfig);",
                       "camera synchronization must install the snapshot movement speed");
    }};

const nr::test::CaseRegistrar verticalWheelUiOnlyCase{
    "vertical wheel input is a per-poll Dear ImGui navigation event only", [] {
        auto const dependencyWindow = readProjectFile("src/extern/dependencyWindow.ixx");
        auto const swapchainInterface = readProjectFile("src/rhi/nrSwapchain.ixx");
        auto const swapchain = readProjectFile("src/rhi/nrSwapchain.cpp");
        auto const ui = readProjectFile("src/app/nrAppUi.cpp");
        auto const camera = readProjectFile("src/app/nrAppCamera.cpp");
        auto const optionModel = readProjectFile("src/options/nrOptionModel.ixx");
        auto const optionSystem = readProjectFile("src/options/nrOptionSystem.cpp");
        auto const websocket = readProjectFile("src/interaction/nrInteractionProtocol.cpp");
        auto const lua = readProjectFile("src/automation/nrOfflineLuaHost.cpp");

        requirePresent(dependencyWindow, "export using ::glfwSetScrollCallback;",
                       "the narrow window dependency must expose GLFW scroll event registration");
        requirePresent(swapchainInterface, "double consumeVerticalScrollOffset() const noexcept;",
                       "PresentationContext must expose one-shot vertical scroll consumption");

        auto const surfaceCreation = sourceSection(swapchain, "void PresentationContext::createSurface(",
                                                   "void PresentationContext::initializeSwapchain(");
        requirePresent(surfaceCreation, "glfwSetScrollCallback(",
                       "PresentationContext must register a GLFW scroll callback");
        requirePresent(surfaceCreation, "verticalScrollOffset_ += yOffset;",
                       "all finite vertical events from one poll must accumulate");
        requireAbsent(surfaceCreation, "xOffset",
                      "horizontal scroll input must be discarded at the presentation boundary");

        auto const polling = sourceSection(swapchain, "void PresentationContext::pollEvents() const",
                                           "bool PresentationContext::keyDown(");
        requireOrdered(polling, "verticalScrollOffset_ = 0.0;", "glfwPollEvents();",
                       "each event poll must discard unconsumed offsets from older presentation iterations");

        auto const consumption =
            sourceSection(swapchain, "double PresentationContext::consumeVerticalScrollOffset() const noexcept",
                          "std::vector<std::uint32_t> PresentationContext::consumeTextInputCodepoints()");
        requirePresent(consumption, "std::exchange(verticalScrollOffset_, 0.0)",
                       "vertical scroll consumption must clear the accumulated delta");

        auto const uiInitialization = sourceSection(ui, "void UiSystem::initialize()", "void UiSystem::shutdown()");
        requirePresent(uiInitialization, "io.FontAllowUserScaling = false;",
                       "Ctrl+wheel window scaling must remain disabled");

        auto const uiFrame =
            sourceSection(ui, "void UiSystem::beginFrame(", "UiCaptureState UiSystem::finalizeFrame()");
        requirePresent(uiFrame, "presentation.consumeVerticalScrollOffset();",
                       "UiSystem must be the sole vertical scroll consumer");
        requirePresent(uiFrame, "io.AddMouseWheelEvent(0.0f,",
                       "UiSystem must forward only the vertical ImGui wheel axis");
        requireOrdered(uiFrame, "io.AddMouseWheelEvent(0.0f,", "ImGui::NewFrame();",
                       "vertical wheel input must reach ImGui before its frame begins");

        auto const mutationOrigins =
            sourceSection(optionModel, "enum class MutationOrigin", "enum class AuthorityMode");
        requireAbsent(mutationOrigins, "wheel", "wheel navigation must not become an OptionSystem mutation origin");
        requireAbsent(camera, "consumeVerticalScrollOffset", "the camera adapter must not consume wheel input");
        requireAbsent(optionSystem, "consumeVerticalScrollOffset",
                      "OptionSystem must remain independent of wheel input");
        requireAbsent(websocket, "consumeVerticalScrollOffset",
                      "WebSocket control must remain independent of wheel input");
        requireAbsent(lua, "consumeVerticalScrollOffset", "offline Lua control must remain independent of wheel input");
    }};
} // namespace
