import std;
import dependency;
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

[[nodiscard]] std::array<float, 16> identityTransform()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"renderer_camera_override_contract.gltf"};

    auto material = nr::load::MaterialAsset{};
    material.name = "camera_override_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "camera_override_triangle";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(3);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1, 2};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
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
        .orthographicWidth = 0.0f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;
    return scene;
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildGraphSpec(vk::Format swapchainFormat)
{
    auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
    normalBuffer->input.colorFormat = swapchainFormat;
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = normalBuffer,
            .config = nr::renderer::NodeConfig{
                .instanceName = "NormalBuffer",
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
            .from = nr::renderer::NodePortRef{.nodeName = "NormalBuffer", .portName = "normalBuffer"},
            .to = nr::renderer::NodePortRef{.nodeName = "Present", .portName = "sourceColor"},
        },
    };

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "CameraOverride.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 0,
        },
    };

    return graphSpec;
}

[[nodiscard]] bool checkRendererCameraOverrideContract()
{
    auto renderer = nr::renderer::Renderer{};

    try
    {
        renderer.initialize(nr::renderer::RendererCreateInfo{
            .appName = "rendererCameraOverrideContract",
            .engineName = "NewbieRenderer",
        });

        auto testPassed = false;
        {
            auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = renderer.device()});
            auto sceneAsset = buildSceneAsset();

            do
            {
                auto templateHandle = scene.registerTemplate(sceneAsset);
                if (!require(templateHandle.valid(), "registerTemplate should succeed for camera override contract scene."))
                {
                    break;
                }

                auto instanceHandle = scene.instantiate(templateHandle);
                if (!require(instanceHandle.valid(), "instantiate should succeed for camera override contract scene."))
                {
                    break;
                }

                scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

                auto graphSpec = buildGraphSpec(renderer.device().presentationContext.swapchainFormat());
                renderer.installGraph(graphSpec);

                auto baseline = renderer.renderFrame(nr::renderer::RendererFrameInput{
                    .scene = std::ref(scene),
                    .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
                    .sceneExtractInput = nr::scene::SceneExtractInput{},
                    .cameraOverride = std::nullopt,
                });

                if (!require(baseline.rendered, "Baseline renderer frame should render successfully."))
                {
                    break;
                }

                if (!require(baseline.usedScenePath && !baseline.usedCameraOverride,
                             "Baseline renderer frame should use scene camera path without override."))
                {
                    break;
                }

                if (!require(baseline.sceneBridgeDrawCount > 0u,
                             "Baseline scene camera path should produce at least one bridge draw."))
                {
                    break;
                }

                auto viewerCamera = nr::renderer::ViewerPerspectiveCamera{};
                auto extent = renderer.device().presentationContext.swapchainExtent();
                viewerCamera.setViewportExtent(glm::uvec2{extent.width, extent.height});
                viewerCamera.setPoseFromLookAt(glm::vec3{0.0f, 0.0f, 3.0f}, glm::vec3{0.0f, 0.0f, 0.0f});

                auto override = viewerCamera.buildRendererCameraOverride();
                override.frustum.planes = {
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
                    .cameraOverride = override,
                });

                if (!require(overridden.rendered, "Override renderer frame should render successfully."))
                {
                    break;
                }

                if (!require(overridden.usedScenePath && overridden.usedCameraOverride,
                             "Override renderer frame should report camera override usage."))
                {
                    break;
                }

                if (!require(overridden.sceneBridgeDrawCount == 0u,
                             "Override custom frustum should be able to cull all bridge draws."))
                {
                    break;
                }

                testPassed = true;
            } while (false);

            renderer.device().waitIdle();
        }

        renderer.shutdown();
        return testPassed;
    }
    catch (const std::exception& error)
    {
        std::println("[fail] renderer camera override contract exception: {}", error.what());
        if (renderer.initialized())
        {
            renderer.shutdown();
        }
        return false;
    }
}
} // namespace

int main()
{
    if (!checkRendererCameraOverrideContract())
    {
        std::println("[FAIL] renderer camera override contract test failed");
        return 1;
    }

    std::println("[OK] renderer camera override contract test passed");
    return 0;
}
