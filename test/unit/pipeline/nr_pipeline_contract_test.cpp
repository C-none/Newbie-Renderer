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

[[nodiscard]] std::vector<char*> makeArgSpanStorage(std::vector<std::string>& values)
{
    auto output = std::vector<char*>{};
    output.reserve(values.size());
    std::ranges::for_each(values, [&](std::string& value) {
        output.push_back(value.data());
    });
    return output;
}

[[nodiscard]] std::filesystem::path testHistoryPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "test" / "pipeline-model-history.txt";
}

[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto const path =
        std::filesystem::path{std::string{nr::projectRoot}} / std::move(relativePath);
    auto file = std::ifstream{path};
    nr::test::require(
        file.good(),
        std::format("failed to open {}", path.generic_string()));
    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string_view sourceSection(
    std::string_view contents,
    std::string_view begin,
    std::string_view end)
{
    auto const beginPosition = contents.find(begin);
    nr::test::require(
        beginPosition != std::string_view::npos,
        std::format("missing section start '{}'", begin));
    auto const endPosition = contents.find(end, beginPosition + begin.size());
    nr::test::require(
        endPosition != std::string_view::npos,
        std::format("missing section end '{}'", end));
    return contents.substr(beginPosition, endPosition - beginPosition);
}

void requirePresent(
    std::string_view contents,
    std::string_view token,
    std::string_view message)
{
    nr::test::require(contents.contains(token), std::string{message});
}

void requireAbsent(
    std::string_view contents,
    std::string_view token,
    std::string_view message)
{
    nr::test::require(!contents.contains(token), std::string{message});
}

void requireOrdered(
    std::string_view contents,
    std::string_view first,
    std::string_view second,
    std::string_view message)
{
    auto const firstPosition = contents.find(first);
    auto const secondPosition = contents.find(second);
    nr::test::require(
        firstPosition != std::string_view::npos &&
            secondPosition != std::string_view::npos &&
            firstPosition < secondPosition,
        std::string{message});
}

const nr::test::CaseRegistrar registryCase{
    "pipeline default registry exposes normalview and rtobject",
    [] {
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
        nr::test::require(
            normalGraph.frameResolutionOptionRequirements.empty(),
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
        nr::test::require(
            std::ranges::equal(
                rtGraph.frameResolutionOptionRequirements,
                expectedDlssResolverOptions),
            "DLSS graph resolver requirements must exactly match enabled, quality, and bypass");

        auto accumulateContext = graphContext();
        accumulateContext.rtPostProcessingMode = nr::pipeline::RtPostProcessingMode::accumulate;
        auto accumulateGraph = rtObject->get().buildGraph(accumulateContext);
        nr::test::requireEqual(accumulateGraph.nodes[4].config.instanceName, std::string{"Accumulate"});
        nr::test::require(!accumulateGraph.frameResolutionResolver.has_value());
        nr::test::require(
            accumulateGraph.frameResolutionOptionRequirements.empty(),
            "accumulate must not declare resolver option requirements");
    }};

const nr::test::CaseRegistrar historyCase{
    "model history keeps most recent entries and roundtrips under build",
    [] {
        auto historyPath = testHistoryPath();
        auto ec = std::error_code{};
        std::filesystem::remove(historyPath, ec);

        auto history = nr::pipeline::ModelHistory{historyPath, 3u};
        auto const first =
            std::filesystem::path{"glTF-Sample-Assets/Models/AlphaBlendModeTest/glTF/AlphaBlendModeTest.gltf"};
        auto const second =
            std::filesystem::path{"glTF-Sample-Assets/Models/ClearCoatTest/glTF/ClearCoatTest.gltf"};
        auto const third =
            std::filesystem::path{"glTF-Sample-Assets/Models/EnvironmentTest/glTF/EnvironmentTest.gltf"};
        history.noteLoaded(first);
        history.noteLoaded(second);
        history.noteLoaded(first);
        history.noteLoaded(third);
        history.save();

        auto reloaded = nr::pipeline::ModelHistory{historyPath, 3u};
        reloaded.load();

        nr::test::requireEqual(reloaded.entries().size(), std::size_t{3u});
        nr::test::requireEqual(
            reloaded.entries()[0].filename().string(),
            std::string{"EnvironmentTest.gltf"});
        nr::test::requireEqual(
            reloaded.entries()[1].filename().string(),
            std::string{"AlphaBlendModeTest.gltf"});
        nr::test::requireEqual(
            reloaded.entries()[2].filename().string(),
            std::string{"ClearCoatTest.gltf"});
        nr::test::require(reloaded.storagePath().string().contains("\\build\\") ||
                          reloaded.storagePath().string().contains("/build/"));
    }};

const nr::test::CaseRegistrar displayCase{
    "model history display labels are leaf first",
    [] {
        auto relative = nr::pipeline::displayPathLeafFirst(std::filesystem::path{"assets/Box.gltf"});
        nr::test::requireEqual(relative, std::string{"Box.gltf / assets"});

        auto absolute = nr::pipeline::displayPathLeafFirst(
            std::filesystem::path{"D:/file/prog/Newbie-Renderer/assets/Box.gltf"});
        nr::test::require(absolute.starts_with("Box.gltf / assets / Newbie-Renderer"));
        nr::test::require(absolute.ends_with("D:\\") || absolute.ends_with("D:/"));
    }};

const nr::test::CaseRegistrar cliCase{
    "viewer command line parses model path and pipeline",
    [] {
        auto defaultOptions = nr::pipeline::parseViewerCommandLine({});
        nr::test::requireEqual(defaultOptions.pipelineId, std::string{"rtobject"});

        auto values = std::vector<std::string>{
            "assets/Box.gltf",
            "--pipeline",
            "rtobject",
        };
        auto argv = makeArgSpanStorage(values);
        auto options = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{argv.data(), argv.size()});

        nr::test::require(!options.showHelp);
        nr::test::require(options.errorMessage.empty());
        nr::test::requireEqual(options.modelPath.string(), std::string{"assets/Box.gltf"});
        nr::test::requireEqual(options.pipelineId, std::string{"rtobject"});
        nr::test::require(
            !options.benchmarkRenderGraphSkeletonMode.has_value());

        auto badValues = std::vector<std::string>{"--unknown"};
        auto badArgv = makeArgSpanStorage(badValues);
        auto badOptions = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{badArgv.data(), badArgv.size()});
        nr::test::require(!badOptions.errorMessage.empty());

        auto benchmarkValues = std::vector<std::string>{
            "--benchmark",
            "--warmup-frames",
            "12",
            "--measure-frames",
            "24",
            "--output",
            "build/benchmark",
            "--dlss-quality",
            "ultra-performance",
            "--render-graph-skeleton",
            "legacy"};
        auto benchmarkArgv = makeArgSpanStorage(benchmarkValues);
        auto benchmark = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{benchmarkArgv.data(), benchmarkArgv.size()});
        nr::test::require(benchmark.errorMessage.empty());
        nr::test::require(benchmark.benchmark);
        nr::test::requireEqual(benchmark.warmupFrames, 12u);
        nr::test::requireEqual(benchmark.measureFrames, 24u);
        nr::test::requireEqual(benchmark.dlssQuality, nr::pipeline::RtDlssQuality::ultraPerformance);
        nr::test::requireEqual(
            benchmark.benchmarkRenderGraphSkeletonMode,
            std::optional{nr::renderer::RenderGraphSkeletonMode::Legacy});

        auto incompleteValues = std::vector<std::string>{"--benchmark", "--measure-frames", "0"};
        auto incompleteArgv = makeArgSpanStorage(incompleteValues);
        auto incomplete = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{incompleteArgv.data(), incompleteArgv.size()});
        nr::test::require(!incomplete.errorMessage.empty());

        auto missingUpValues = std::vector<std::string>{"--benchmark", "--measure-frames", "1", "--output", "build/benchmark"};
        auto missingUpArgv = makeArgSpanStorage(missingUpValues);
        auto missingUp = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{missingUpArgv.data(), missingUpArgv.size()});
        nr::test::require(!missingUp.errorMessage.empty());

        auto agentValues = std::vector<std::string>{"--interaction", "agent"};
        auto agentArgv = makeArgSpanStorage(agentValues);
        auto agent = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{agentArgv.data(), agentArgv.size()});
        nr::test::require(agent.errorMessage.empty());
        nr::test::requireEqual(
            agent.interactionMode,
            nr::pipeline::ViewerInteractionMode::agent);

        auto luaValues = std::vector<std::string>{
            "--interaction",
            "offline-lua",
            "--script",
            "smoke.lua",
        };
        auto luaArgv = makeArgSpanStorage(luaValues);
        auto lua = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{luaArgv.data(), luaArgv.size()});
        nr::test::require(lua.errorMessage.empty());
        nr::test::requireEqual(
            lua.interactionMode,
            nr::pipeline::ViewerInteractionMode::offlineLua);
        nr::test::requireEqual(
            lua.automationScript.generic_string(),
            std::string{"smoke.lua"});

        auto invalidSkeletonValues = std::vector<std::string>{
            "--benchmark",
            "--measure-frames",
            "1",
            "--output",
            "build/benchmark",
            "--dlss-quality",
            "ultra-performance",
            "--render-graph-skeleton",
            "differential"};
        auto invalidSkeletonArgv =
            makeArgSpanStorage(invalidSkeletonValues);
        auto invalidSkeleton = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{
                invalidSkeletonArgv.data(),
                invalidSkeletonArgv.size()});
        nr::test::require(!invalidSkeleton.errorMessage.empty());

        auto nonBenchmarkSkeletonValues = std::vector<std::string>{
            "--render-graph-skeleton",
            "enabled"};
        auto nonBenchmarkSkeletonArgv =
            makeArgSpanStorage(nonBenchmarkSkeletonValues);
        auto nonBenchmarkSkeleton = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{
                nonBenchmarkSkeletonArgv.data(),
                nonBenchmarkSkeletonArgv.size()});
        nr::test::require(!nonBenchmarkSkeleton.errorMessage.empty());
    }};

const nr::test::CaseRegistrar benchmarkBuildGateCase{
    "viewer benchmark execution is gated to Release builds before app initialization",
    [] {
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
    "viewer default environment selects the Kloofendal OpenEXR asset",
    [] {
        auto const path = nr::pipeline::defaultEnvironmentMapPath();
        nr::test::requireEqual(
            path.filename().string(),
            std::string{"kloofendal_48d_partly_cloudy_puresky_8k.exr"});
        nr::test::requireEqual(path.parent_path(), nr::pipeline::environmentMapAssetDirectoryPath());
        auto statusError = std::error_code{};
        nr::test::require(
            std::filesystem::is_regular_file(path, statusError) && !statusError,
            "viewer default environment asset should exist");
    }};

const nr::test::CaseRegistrar environmentDiscoveryCase{
    "viewer environment choices are sorted direct OpenEXR assets with extension-free labels",
    [] {
        auto assets = nr::pipeline::discoverEnvironmentMapAssets();
        nr::test::require(
            assets.has_value(),
            assets.has_value() ? std::string{} : assets.error());
        nr::test::require(!assets->empty(), "viewer should discover at least one environment map");

        auto const assetDirectory = nr::pipeline::environmentMapAssetDirectoryPath();
        nr::test::require(
            std::ranges::all_of(*assets, [&](const nr::pipeline::EnvironmentMapAsset& asset) {
                auto equivalentError = std::error_code{};
                auto const directChild = std::filesystem::equivalent(
                    asset.sourcePath.parent_path(),
                    assetDirectory,
                    equivalentError);
                return !equivalentError &&
                       directChild &&
                       asset.sourcePath.extension() == ".exr" &&
                       asset.displayName == asset.sourcePath.stem().string();
            }),
            "viewer environment choices must stay within assets/envMap and hide file extensions");
        nr::test::require(
            std::ranges::is_sorted(*assets, {}, &nr::pipeline::EnvironmentMapAsset::displayName),
            "viewer environment choices should have deterministic display-name ordering");
    }};

const nr::test::CaseRegistrar graphCatalogPreflightOrderingCase{
    "pipeline validates the combined option catalog before any destructive graph replacement",
    [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const graphMutation = sourceSection(
            pipeline,
            "if (id == pipelineOption || id == postProcessingOption)",
            "if (id == nr::options::optionId(nr::options::keys::viewerModelSource))");
        requireOrdered(
            graphMutation,
            "auto combinedCatalog = validateSessionAndGraphCatalog(",
            "installPreparedPipelineGraph(app.renderer(), *prepared, true);",
            "runtime graph mutation must validate the combined session/graph catalog before entering the install helper");
        requireOrdered(
            graphMutation,
            "if (!combinedCatalog)",
            "installPreparedPipelineGraph(app.renderer(), *prepared, true);",
            "runtime graph mutation must handle combined-catalog rejection before graph teardown can begin");

        auto const initialInstall = sourceSection(
            pipeline,
            "auto initialGraph = preparePipelineGraph(",
            "auto webSocketHost = nr::interaction::OptionWebSocketHost");
        requireOrdered(
            initialInstall,
            "auto combinedCatalog = validateSessionAndGraphCatalog(",
            "installPreparedPipelineGraph(app.renderer(), *initialGraph, false);",
            "initial graph installation must validate the combined session/graph catalog first");
        requireOrdered(
            initialInstall,
            "if (!combinedCatalog)",
            "installPreparedPipelineGraph(app.renderer(), *initialGraph, false);",
            "initial combined-catalog failure must exit before graph installation");

        auto const installHelper = sourceSection(
            pipeline,
            "void installPreparedPipelineGraph(",
            "[[nodiscard]] std::filesystem::path projectRelativePath");
        requireOrdered(
            installHelper,
            "renderer.uninstallGraph();",
            "renderer.installGraph(prepared.spec)",
            "the destructive helper may uninstall only as part of the already-preflighted install path");
    }};

const nr::test::CaseRegistrar sceneCandidateCommitOrderingCase{
    "model replacement completes detached Scene construction before committing ownership",
    [] {
        auto const model = readProjectFile("src/pipeline/nrPipelineModel.cpp");
        auto const appSessionInterface = readProjectFile("src/app/nrAppSession.ixx");
        auto const appSession = readProjectFile("src/app/nrAppSession.cpp");
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");

        auto const loadModel = sourceSection(
            model,
            "[[nodiscard]] ModelLoadReport SceneModelController::loadModel",
            "[[nodiscard]] const std::optional<std::filesystem::path> &SceneModelController::currentModelPath");
        requireOrdered(
            loadModel,
            "auto loadResult = nr::load::loadScene(",
            "auto candidate = app.makeSceneCandidate();",
            "model decoding must finish before detached Scene allocation");
        requireOrdered(
            loadModel,
            "auto candidate = app.makeSceneCandidate();",
            "auto templateHandle = candidate->registerTemplate(sceneAsset);",
            "template registration must target the detached candidate");
        requireOrdered(
            loadModel,
            "if (!templateHandle.valid())",
            "auto instanceHandle = candidate->instantiate(templateHandle);",
            "template failure must return before instantiation");
        requireOrdered(
            loadModel,
            "if (!instanceHandle.valid())",
            "app.commitScene(std::move(candidate));",
            "instance failure must return before the Scene commit boundary");
        requireOrdered(
            loadModel,
            "app.commitScene(std::move(candidate));",
            "app.resetCameraFromSceneOrDefault();",
            "camera derivation must observe the newly committed Scene");
        requireAbsent(
            loadModel,
            "app.destroyScene();",
            "model replacement must not destroy the active Scene before candidate success");

        requirePresent(
            appSessionInterface,
            "std::unique_ptr<nr::scene::Scene> scene_{};",
            "AppSession must exclusively own the active Scene");
        auto const commitScene = sourceSection(
            appSession,
            "void AppSession::commitScene(",
            "nr::scene::Scene& AppSession::createScene");
        requireOrdered(
            commitScene,
            "renderer_.device().waitIdle();",
            "renderer_.resetSceneBinding();",
            "Scene commit must cross the frame/device boundary before unbinding the old Scene");
        requireOrdered(
            commitScene,
            "renderer_.resetSceneBinding();",
            "scene_.swap(candidate);",
            "renderer Scene bindings must be cleared before ownership swaps");
        requireOrdered(
            commitScene,
            "scene_.swap(candidate);",
            "candidate.reset();",
            "the old Scene must be destroyed only after the candidate becomes active");

        auto const modelMutation = sourceSection(
            pipeline,
            "if (id == nr::options::optionId(nr::options::keys::viewerModelSource))",
            "if (id == nr::options::optionId(nr::options::keys::viewerEnvironmentSource))");
        requireOrdered(
            modelMutation,
            "if (!report.loaded)",
            "options.commitModelAndCameraReset(",
            "model and derived camera canonical values may commit only after Scene replacement succeeds");
    }};

const nr::test::CaseRegistrar renderableFrameGateCase{
    "pipeline verifies renderer readiness before consuming an option frame",
    [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const mainLoop = sourceSection(
            pipeline,
            "while (!presentation.windowShouldClose())",
            "if (config.benchmark && exitCode == 0");
        requireOrdered(
            mainLoop,
            "!app.renderer().initialized() || !app.renderer().graphInstalled()",
            "app.options().beginRenderableFrame();",
            "renderer and graph readiness must be proven before detaching a pending mutation");
        requireOrdered(
            mainLoop,
            "!presentation.framebufferAvailable()",
            "app.options().beginRenderableFrame();",
            "zero-sized presentation iterations must retain the pending mutation");
    }};

const nr::test::CaseRegistrar uiCameraAdmissionPriorityCase{
    "all interaction modes render the option snapshot while only human UI may submit",
    [] {
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const camera = readProjectFile("src/app/nrAppCamera.cpp");
        auto const presenterInterface = readProjectFile("src/app/nrOptionUiPresenter.ixx");
        auto const presenter = readProjectFile("src/app/nrOptionUiPresenter.cpp");

        requirePresent(
            presenterInterface,
            "enum class OptionUiInteractionPolicy",
            "the presenter API must make the interaction policy explicit");
        requirePresent(
            presenterInterface,
            "OptionUiInteractionPolicy interactionPolicy",
            "every presenter call must choose interactive or read-only behavior");

        auto const readOnlyDrawing = sourceSection(
            presenter,
            "void OptionUiPresenter::drawReadOnlyOption(",
            "bool OptionUiPresenter::drawInteractiveOption(");
        requirePresent(
            readOnlyDrawing,
            "ui.beginDisabled(true);",
            "the read-only mirror must disable every actionable option control");
        requireAbsent(
            readOnlyDrawing,
            "draftFor(",
            "the read-only mirror must not create presenter-local drafts");
        requireAbsent(
            readOnlyDrawing,
            "schedule(",
            "the read-only mirror must not enter mutation scheduling");
        requireAbsent(
            readOnlyDrawing,
            "trySchedule(",
            "the read-only mirror must not call OptionSystem admission");
        requireAbsent(
            readOnlyDrawing,
            "itemEditCommitted(",
            "the read-only mirror must not observe or commit edits");

        auto const allUiModes = sourceSection(
            pipeline,
            "if (!config.benchmark)",
            "app.ui().setCameraFrame(app.camera().frame());");
        requirePresent(
            allUiModes,
            "config.interactionMode == ViewerInteractionMode::human",
            "pipeline must derive UI mutability from the active interaction mode");
        requirePresent(
            allUiModes,
            "nr::app::OptionUiInteractionPolicy::readOnly",
            "agent and offline-lua modes must select the read-only mirror");
        requireOrdered(
            allUiModes,
            "auto uiResult = optionUiPresenter.present(",
            "if (uiInteractionPolicy == nr::app::OptionUiInteractionPolicy::interactive)",
            "all modes must render the shared snapshot before only human mode enters admission");

        auto const humanInput = sourceSection(
            pipeline,
            "if (uiInteractionPolicy == nr::app::OptionUiInteractionPolicy::interactive)",
            "app.ui().setCameraFrame(app.camera().frame());");
        requireOrdered(
            humanInput,
            "if (uiResult.mutationAttempted)",
            "app.camera().discardPresentationInput(",
            "every UI mutation attempt, including a rejected one, must consume presentation input");
        requireOrdered(
            humanInput,
            "app.camera().discardPresentationInput(",
            "else",
            "camera admission must remain in the branch opposite a UI mutation attempt");
        requireOrdered(
            humanInput,
            "else",
            "app.camera().tryScheduleFromPresentation(",
            "camera mutation may be attempted only when UI did not attempt a mutation");

        auto const discardInput = sourceSection(
            camera,
            "void AppCamera::discardPresentationInput(",
            "nr::options::CameraResetValues AppCamera::optionResetValues()");
        requirePresent(
            discardInput,
            "detail::sampleControlInput(",
            "discarding presentation input must still run the normal cursor sampler");
        requirePresent(
            discardInput,
            "cursorTracking_",
            "discarding presentation input must update the persistent cursor baseline");
    }};
} // namespace
