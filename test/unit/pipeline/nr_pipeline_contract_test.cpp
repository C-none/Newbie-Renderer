import dependency.vulkan;
import nr.pipeline;
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
        nr::test::requireEqual(normalGraph.nodes[1].config.instanceName, std::string{"Ui"});
        nr::test::requireEqual(normalGraph.nodes[2].config.instanceName, std::string{"Present"});
        nr::test::requireEqual(normalGraph.submitNodes.size(), std::size_t{1u});
        nr::test::requireEqual(normalGraph.submitNodes[0].afterNodeIndex, std::size_t{1u});

        auto rtObject = registry.find(nr::pipeline::rtObjectPipelineId);
        nr::test::require(rtObject.has_value());
        auto rtGraph = rtObject->get().buildGraph(graphContext());
        nr::test::requireEqual(rtGraph.nodes.size(), std::size_t{4u});
        nr::test::requireEqual(rtGraph.nodes[0].config.instanceName, std::string{"ASBuild"});
        nr::test::requireEqual(rtGraph.nodes[1].config.instanceName, std::string{"RTInstanceHash"});
        nr::test::requireEqual(rtGraph.nodes[2].config.instanceName, std::string{"Ui"});
        nr::test::requireEqual(rtGraph.nodes[3].config.instanceName, std::string{"Present"});
        nr::test::requireEqual(rtGraph.submitNodes.size(), std::size_t{1u});
        nr::test::requireEqual(rtGraph.submitNodes[0].afterNodeIndex, std::size_t{2u});
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
} // namespace
