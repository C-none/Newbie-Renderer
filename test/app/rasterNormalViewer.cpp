import std;
import nr.load;
import nr.scene;
import nr.renderer;
import nr.renderPasses;

namespace
{
[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }

    return true;
}

[[nodiscard]] std::filesystem::path projectRootPath()
{
#ifdef NR_PROJECT_ROOT_DIR
    return std::filesystem::path{NR_PROJECT_ROOT_DIR};
#else
    return std::filesystem::current_path();
#endif
}

[[nodiscard]] std::filesystem::path triangleScenePath()
{
    return projectRootPath() / std::filesystem::path{"assets/glTF-Sample-Assets/Models/Triangle/glTF/Triangle.gltf"};
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildRasterNormalViewerGraphSpec()
{
    auto normalView = std::make_shared<nr::renderPasses::NormalViewNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = normalView,
            .config = nr::renderer::NodeConfig{
                .instanceName = "NormalView",
                .queue = nr::renderer::QueueDomain::Graphics,
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

    graphSpec.connections = {
        nr::renderer::NodeConnection{
            .from = nr::renderer::NodePortRef{
                .nodeName = "NormalView",
                .portName = "color",
            },
            .to = nr::renderer::NodePortRef{
                .nodeName = "Present",
                .portName = "sourceColor",
            },
        },
    };

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "App.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 0,
        },
    };

    return graphSpec;
}

[[nodiscard]] bool runRasterNormalViewerSmoke()
{
    auto renderer = nr::renderer::Renderer{};

    try
    {
        renderer.initialize(nr::renderer::RendererCreateInfo{
            .appName = "rasterNormalViewer",
            .engineName = "NewbieRenderer",
        });

        auto const scenePath = triangleScenePath();
        if (!require(std::filesystem::exists(scenePath),
                     "Triangle.gltf must exist under assets/glTF-Sample-Assets/Models/Triangle/glTF/."))
        {
            renderer.shutdown();
            return false;
        }

        auto importResult = nr::load::loadScene(nr::load::SceneLoadRequest{
            .sourcePath = scenePath,
            .searchRoot = scenePath.parent_path(),
            .strict = true,
        });

        if (!importResult.has_value())
        {
            auto const &error = importResult.error();
            std::println("[fail] failed to import Triangle.gltf: {}", error.message);
            renderer.shutdown();
            return false;
        }

        auto sceneAsset = std::move(importResult.value());

        if (!require(!sceneAsset.meshes.empty(), "Imported scene should contain at least one mesh."))
        {
            renderer.shutdown();
            return false;
        }

        auto runInstalledGraphPath = [&]() -> bool {
            auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = renderer.device()});

            auto templateHandle = scene.registerTemplate(
                sceneAsset,
                nr::scene::SceneTemplateCreateInfo{
                    .debugName = "rasterNormalViewer.Triangle",
                    .stableKey = scenePath.generic_string(),
                });

            if (!require(templateHandle.valid(), "registerTemplate should succeed for Triangle.gltf."))
            {
                return false;
            }

            auto instanceHandle = scene.instantiate(templateHandle);
            if (!require(instanceHandle.valid(), "instantiate should succeed for Triangle template."))
            {
                return false;
            }

            scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

            auto graphSpec = buildRasterNormalViewerGraphSpec();
            if (!require(graphSpec.nodes.size() == 2u, "Graph should contain NormalView and Present nodes."))
            {
                return false;
            }

            auto normalRuntime = std::dynamic_pointer_cast<nr::renderPasses::NormalViewNode>(graphSpec.nodes.front().runtime);
            if (!require(static_cast<bool>(normalRuntime), "Graph first node should be NormalView runtime."))
            {
                return false;
            }
            normalRuntime->input.colorFormat = renderer.device().presentationContext.swapchainFormat();

            if (!require(graphSpec.submitNodes.size() == 1u && graphSpec.submitNodes.front().afterNodeIndex == 0u,
                         "Graph should include one explicit submit boundary after NormalView."))
            {
                return false;
            }

            renderer.installGraph(graphSpec);
            if (!require(renderer.graphInstalled(), "Renderer should report graphInstalled=true after installGraph."))
            {
                return false;
            }

            auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
                .scene = std::ref(scene),
                .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
                .sceneExtractInput = nr::scene::SceneExtractInput{},
            });

            if (!require(frameResult.rendered, "Renderer should render one frame in rasterNormalViewer smoke path."))
            {
                return false;
            }

            if (!require(frameResult.usedScenePath, "Frame should use Scene path when scene input is provided."))
            {
                return false;
            }

            if (!require(frameResult.compiledSubmitBatchCount == 2u,
                         "Installed graph should compile into two submit batches (NormalView + Present)."))
            {
                return false;
            }

            if (!require(frameResult.sceneBridgeDrawCount > 0u,
                         "Scene bridge should produce raster draws for Triangle.gltf path."))
            {
                return false;
            }

            return true;
        };

        auto const runOk = runInstalledGraphPath();
        renderer.shutdown();
        return runOk;
    }
    catch (const std::exception &error)
    {
        std::println("[fail] rasterNormalViewer smoke exception: {}", error.what());
        renderer.shutdown();
        return false;
    }
}
} // namespace

int main()
{
    auto const ok = runRasterNormalViewerSmoke();
    std::println("[summary] rasterNormalViewer smoke => {}", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
