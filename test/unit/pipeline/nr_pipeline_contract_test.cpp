import dependency.vulkan;
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

        auto accumulateContext = graphContext();
        accumulateContext.rtPostProcessingMode = nr::pipeline::RtPostProcessingMode::accumulate;
        auto accumulateGraph = rtObject->get().buildGraph(accumulateContext);
        nr::test::requireEqual(accumulateGraph.nodes[4].config.instanceName, std::string{"Accumulate"});
        nr::test::require(!accumulateGraph.frameResolutionResolver.has_value());
    }};

const nr::test::CaseRegistrar historyCase{
    "model history keeps most recent entries and roundtrips under build",
    [] {
        auto historyPath = testHistoryPath();
        auto ec = std::error_code{};
        std::filesystem::remove(historyPath, ec);

        auto history = nr::pipeline::ModelHistory{historyPath, 3u};
        history.noteLoaded("D:/assets/First.gltf");
        history.noteLoaded("D:/assets/Second.gltf");
        history.noteLoaded("D:/assets/First.gltf");
        history.noteLoaded("D:/assets/Third.gltf");
        history.save();

        auto reloaded = nr::pipeline::ModelHistory{historyPath, 3u};
        reloaded.load();

        nr::test::requireEqual(reloaded.entries().size(), std::size_t{3u});
        nr::test::requireEqual(reloaded.entries()[0].filename().string(), std::string{"Third.gltf"});
        nr::test::requireEqual(reloaded.entries()[1].filename().string(), std::string{"First.gltf"});
        nr::test::requireEqual(reloaded.entries()[2].filename().string(), std::string{"Second.gltf"});
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

        auto badValues = std::vector<std::string>{"--unknown"};
        auto badArgv = makeArgSpanStorage(badValues);
        auto badOptions = nr::pipeline::parseViewerCommandLine(
            std::span<char*>{badArgv.data(), badArgv.size()});
        nr::test::require(!badOptions.errorMessage.empty());
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
} // namespace
