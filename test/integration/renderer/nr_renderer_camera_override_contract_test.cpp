import std;
import dependency;
import nr.load;
import nr.renderPasses;
import nr.renderer;
import nr.scene;
import nr.test;

namespace
{
struct RendererShutdownGuard
{
    nr::renderer::Renderer &renderer;

    ~RendererShutdownGuard()
    {
        if (renderer.initialized())
        {
            renderer.shutdown();
        }
    }
};

[[nodiscard]] std::array<float, 16> identityTransform() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset makeSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"renderer_camera_override_contract.gltf"};

    scene.materials.push_back(nr::load::MaterialAsset{.name = "camera_override_material"});
    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "camera_override_triangle",
        .vertices = {
            nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
            nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
            nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
        },
        .indices = {0u, 1u, 2u},
        .geometries = {
            nr::load::MeshGeometryAsset{
                .name = "camera_override_triangle_geometry_0",
                .indexCount = 3,
                .materialIndex = 0,
            },
        },
    });

    scene.nodes.resize(3);
    scene.rootNodeIndex = 0;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1u, 2u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0u};
    scene.nodes[1].localTransform = identityTransform();
    scene.nodes[2].name = "CameraNode";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].localTransform = identityTransform();

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "ImportedCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .lookAt = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .horizontalFov = glm::radians(90.0f),
        .aspect = 1.0f,
        .nearPlane = 0.1f,
        .farPlane = 500.0f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;
    return scene;
}

[[nodiscard]] nr::renderer::RendererGraphSpec makeGraphSpec(vk::Format swapchainFormat)
{
    auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
    normalBuffer->input.colorFormat = swapchainFormat;
    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    return nr::renderer::RendererGraphSpec{
        .nodes = {
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
        },
        .submitNodes = {
            nr::renderer::SubmitNodeSpec{
                .debugName = "CameraOverride.GraphicsToCompute",
                .afterNodeIndex = 1,
            },
        },
    };
}

const nr::test::CaseRegistrar cameraOverrideCase{
    "renderer camera override switches scene extraction to override frustum",
    [] {
        auto renderer = nr::renderer::Renderer{};
        auto shutdownGuard = RendererShutdownGuard{renderer};

        renderer.initialize(nr::renderer::RendererCreateInfo{
            .appName = "rendererCameraOverrideContract",
            .engineName = "NewbieRenderer",
        });

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = renderer.device()});
        auto sceneAsset = makeSceneAsset();
        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(instanceHandle.valid(), "instance registration should succeed");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        renderer.installGraph(makeGraphSpec(renderer.device().presentationContext.swapchainFormat()));

        auto baseline = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .scene = std::ref(scene),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .sceneExtractInput = nr::scene::SceneExtractInput{},
        });

        nr::test::require(baseline.rendered, "baseline renderer frame should render");
        nr::test::require(baseline.usedScenePath, "baseline should use scene path");
        nr::test::require(!baseline.usedCameraOverride, "baseline should not use camera override");
        nr::test::require(baseline.sceneBridgeDrawCount > 0u, "baseline should produce scene bridge draws");
        nr::test::require(baseline.sceneTlasPacketCount > 0u, "baseline should produce TLAS packets");

        auto viewerCamera = nr::renderer::ViewerPerspectiveCamera{};
        auto extent = renderer.device().presentationContext.swapchainExtent();
        viewerCamera.setViewportExtent(glm::uvec2{extent.width, extent.height});
        viewerCamera.setPoseFromLookAt(glm::vec3{0.0f, 0.0f, 3.0f}, glm::vec3{0.0f});

        auto overrideCamera = viewerCamera.buildRendererCameraOverride();
        overrideCamera.frustum.planes = {
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
            glm::vec4{0.0f, 0.0f, 1.0f, -10000.0f},
        };

        auto overridden = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .scene = std::ref(scene),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .sceneExtractInput = nr::scene::SceneExtractInput{},
            .cameraOverride = overrideCamera,
        });

        nr::test::require(overridden.rendered, "override renderer frame should render");
        nr::test::require(overridden.usedScenePath, "override should still use scene path");
        nr::test::require(overridden.usedCameraOverride, "override frame should report camera override usage");
        nr::test::requireEqual(overridden.sceneBridgeDrawCount, std::uint32_t{0},
                               "override custom frustum should be able to cull all bridge draws");
        nr::test::requireEqual(
            overridden.sceneTlasPacketCount,
            baseline.sceneTlasPacketCount,
            "RT/TLAS extraction must ignore camera override frustum culling");

        renderer.device().waitIdle();
    }};
} // namespace
