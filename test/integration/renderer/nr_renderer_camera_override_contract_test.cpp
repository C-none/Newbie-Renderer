import std;
import dependency.math;
import dependency.vulkan;
import nr.load;
import nr.renderPasses;
import nr.renderer;
import nr.scene;
import nr.test;
import nr.test.options;

namespace
{
[[nodiscard]] bool nearlyEqual(float left, float right, float epsilon = 1e-4f) noexcept
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] bool mat4Near(const DirectX::XMFLOAT4X4 &left, const DirectX::XMFLOAT4X4 &right,
                            float epsilon = 1e-4f) noexcept
{
    auto const leftValues = std::array{left._11, left._12, left._13, left._14, left._21, left._22, left._23, left._24,
                                       left._31, left._32, left._33, left._34, left._41, left._42, left._43, left._44};
    auto const rightValues = std::array{right._11, right._12, right._13, right._14, right._21, right._22, right._23,
                                        right._24, right._31, right._32, right._33, right._34, right._41, right._42,
                                        right._43, right._44};
    return std::ranges::equal(leftValues, rightValues, [epsilon](float leftValue, float rightValue) {
        return nearlyEqual(leftValue, rightValue, epsilon);
    });
}

[[nodiscard]] DirectX::XMFLOAT4X4 matrixProduct(const DirectX::XMFLOAT4X4 &left,
                                                 const DirectX::XMFLOAT4X4 &right) noexcept
{
    auto result = DirectX::XMFLOAT4X4{};
    DirectX::XMStoreFloat4x4(&result,
                              DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&left), DirectX::XMLoadFloat4x4(&right)));
    return result;
}

class CameraFrameCaptureNode final : public nr::renderer::NodeRuntime
{
  public:
    explicit CameraFrameCaptureNode(
        std::shared_ptr<std::vector<nr::scene::SceneBridgeFrameConstants>> capturedFrames,
        std::shared_ptr<std::vector<bool>> capturedHistoryResets)
        : capturedFrames_(std::move(capturedFrames)), capturedHistoryResets_(std::move(capturedHistoryResets))
    {
    }

    void build(nr::renderer::NodeBuildContext &,
               const nr::renderer::NodeFrameParameters &frameParameters) override
    {
        capturedFrames_->push_back(frameParameters.renderCameraConstants);
        capturedHistoryResets_->push_back(frameParameters.resolutionPlan.resetHistory);
    }

  private:
    std::shared_ptr<std::vector<nr::scene::SceneBridgeFrameConstants>> capturedFrames_{};
    std::shared_ptr<std::vector<bool>> capturedHistoryResets_{};
};

class ShutdownCountingNode final : public nr::renderer::NodeRuntime
{
  public:
    explicit ShutdownCountingNode(std::shared_ptr<std::size_t> shutdownCount) : shutdownCount_(std::move(shutdownCount))
    {
    }

    void build(nr::renderer::NodeBuildContext &, const nr::renderer::NodeFrameParameters &) override
    {
    }

    void shutdown(nr::renderer::NodeShutdownContext &) override
    {
        ++*shutdownCount_;
    }

  private:
    std::shared_ptr<std::size_t> shutdownCount_{};
};

[[nodiscard]] std::array<float, 16> identityTransform() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset makeSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"renderer_camera_override_contract.gltf"};

    scene.materials.push_back(nr::load::MaterialAsset{.name = "camera_override_material"});
    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "camera_override_triangle",
        .vertices =
            {
                nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
                nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
                nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
            },
        .indices = {0u, 1u, 2u},
        .geometries =
            {
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
        .horizontalFov = nr::math::radians(90.0f),
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

[[nodiscard]] nr::renderer::RendererGraphSpec makeGraphSpec(
    vk::Format swapchainFormat,
    std::shared_ptr<std::vector<nr::scene::SceneBridgeFrameConstants>> capturedCameraFrames,
    std::shared_ptr<std::vector<bool>> capturedHistoryResets)
{
    auto normalBuffer = std::make_shared<nr::renderPasses::NormalBufferNode>();
    normalBuffer->input.colorFormat = swapchainFormat;
    auto ui = std::make_shared<nr::renderPasses::UiNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    return nr::renderer::RendererGraphSpec{
        .nodes =
            {
                nr::renderer::NodeCreateInfo{
                    .runtime = normalBuffer,
                    .config =
                        nr::renderer::NodeConfig{
                            .instanceName = "NormalBuffer",
                        },
                },
                nr::renderer::NodeCreateInfo{
                    .runtime = ui,
                    .config =
                        nr::renderer::NodeConfig{
                            .instanceName = "Ui",
                        },
                },
                nr::renderer::NodeCreateInfo{
                    .runtime = present,
                    .config =
                        nr::renderer::NodeConfig{
                            .instanceName = "Present",
                            .queue = nr::renderer::QueueDomain::Compute,
                        },
                },
                nr::renderer::NodeCreateInfo{
                    .runtime = std::make_shared<CameraFrameCaptureNode>(std::move(capturedCameraFrames),
                                                                        std::move(capturedHistoryResets)),
                    .config =
                        nr::renderer::NodeConfig{
                            .instanceName = "CameraFrameCapture",
                        },
                },
            },
        .submitNodes =
            {
                nr::renderer::SubmitNodeSpec{
                    .debugName = "CameraOverride.GraphicsToCompute",
                    .afterNodeIndex = 1,
                },
            },
    };
}

const nr::test::CaseRegistrar cameraOverrideCase{
    "renderer camera override switches scene extraction to override frustum", [] {
        auto renderer = nr::renderer::Renderer{};

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
        scene.beginFrame(0u);
        scene.uploadPending();
        renderer.device().waitIdle();
        scene.uploadPending();

        auto capturedCameraFrames = std::make_shared<std::vector<nr::scene::SceneBridgeFrameConstants>>();
        auto capturedHistoryResets = std::make_shared<std::vector<bool>>();
        auto graphSpec = makeGraphSpec(renderer.device().presentationContext.swapchainFormat(), capturedCameraFrames,
                                       capturedHistoryResets);
        auto const preflight = renderer.preflightGraph(graphSpec);
        nr::test::require(static_cast<bool>(preflight), "camera override graph should pass preflight");
        nr::test::require(renderer.installGraph(graphSpec), "camera override graph should install");
        auto const optionSnapshot =
            nr::test::options::makeDefaultSnapshot(preflight.optionCatalog, "camera-override-snapshot");

        auto extent = renderer.device().presentationContext.swapchainExtent();
        auto sceneCamera = scene.tryGetPrimaryCamera(DirectX::XMUINT2{extent.width, extent.height});
        nr::test::require(sceneCamera.has_value(), "baseline scene should expose its authored primary camera");

        auto baseline = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
            .scene = std::ref(scene),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .sceneExtractInput = nr::scene::SceneExtractInput{},
        });

        nr::test::require(baseline.rendered, "baseline renderer frame should render");
        nr::test::require(baseline.usedScenePath, "baseline should use scene path");
        nr::test::require(!baseline.usedCameraOverride, "baseline should not use camera override");
        nr::test::require(baseline.sceneBridgeDrawCount > 0u, "baseline should produce scene bridge draws");
        nr::test::require(baseline.sceneTlasPacketCount > 0u, "baseline should produce TLAS packets");
        nr::test::requireEqual(capturedCameraFrames->size(), std::size_t{1u},
                               "baseline graph build should observe one selected camera frame");
        nr::test::requireEqual(capturedHistoryResets->size(), std::size_t{1u});
        nr::test::require(capturedHistoryResets->front(),
                          "the first frame after graph installation must reset temporal history");
        nr::test::require(mat4Near(capturedCameraFrames->front().view, sceneCamera->view),
                          "baseline node camera view should come from the scene primary camera");
        nr::test::require(mat4Near(capturedCameraFrames->front().projection, sceneCamera->projection),
                          "baseline node camera projection should come from the scene primary camera");
        nr::test::require(mat4Near(capturedCameraFrames->front().viewProjection,
                                   matrixProduct(sceneCamera->view, sceneCamera->projection)),
                          "baseline node camera view-projection should come from the scene primary camera");

        auto viewerCamera = nr::renderer::ViewerPerspectiveCamera{};
        viewerCamera.setViewportExtent(DirectX::XMUINT2{extent.width, extent.height});
        viewerCamera.setPoseFromLookAt(DirectX::XMFLOAT3{0.0f, 0.0f, 3.0f}, DirectX::XMFLOAT3{0.0f, 0.0f, 0.0f});

        auto overrideCamera = viewerCamera.buildRendererCameraOverride();
        overrideCamera.frustum.planes = {
            DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f}, DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f},
            DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f}, DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f},
            DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f}, DirectX::XMFLOAT4{0.0f, 0.0f, 1.0f, -10000.0f},
        };

        auto overridden = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
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
        nr::test::requireEqual(overridden.sceneTlasPacketCount, baseline.sceneTlasPacketCount,
                               "RT/TLAS extraction must ignore camera override frustum culling");
        nr::test::requireEqual(capturedCameraFrames->size(), std::size_t{2u},
                               "override graph build should observe one additional selected camera frame");
        nr::test::require(!capturedHistoryResets->back(),
                          "camera-only movement must preserve history for motion-vector reprojection");
        nr::test::require(mat4Near(capturedCameraFrames->back().view, overrideCamera.frameConstants.view),
                          "override node camera view should come from the app camera override");
        nr::test::require(mat4Near(capturedCameraFrames->back().projection,
                                   overrideCamera.frameConstants.projection),
                          "override node camera projection should come from the app camera override");
        nr::test::require(mat4Near(capturedCameraFrames->back().viewProjection,
                                   overrideCamera.frameConstants.viewProjection),
                          "override node camera view-projection should come from the app camera override");

        auto additionalInstance = scene.instantiate(templateHandle);
        nr::test::require(additionalInstance.valid(), "scene revision mutation instance should register");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});
        auto sceneChanged = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
            .scene = std::ref(scene),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .sceneExtractInput = nr::scene::SceneExtractInput{},
            .cameraOverride = overrideCamera,
        });
        nr::test::require(sceneChanged.rendered, "scene-revision frame should render");
        nr::test::require(capturedHistoryResets->back(),
                          "a complete SceneRevisionSnapshot change must reset temporal history");

        auto sceneAbsent = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .cameraOverride = overrideCamera,
        });
        nr::test::require(sceneAbsent.rendered, "scene-absent frame should render");
        nr::test::require(capturedHistoryResets->back(),
                          "changing scene presence from present to absent must reset temporal history");

        auto sceneStillAbsent = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .cameraOverride = overrideCamera,
        });
        nr::test::require(sceneStillAbsent.rendered, "stable scene-absent frame should render");
        nr::test::require(!capturedHistoryResets->back(),
                          "stable scene absence and camera must preserve temporal history");
    }};

const nr::test::CaseRegistrar rendererDestructorCase{
    "renderer destructor shuts installed nodes down exactly once", [] {
        auto shutdownCount = std::make_shared<std::size_t>(0u);
        {
            auto renderer = nr::renderer::Renderer{};
            renderer.initialize(nr::renderer::RendererCreateInfo{
                .appName = "rendererDestructorContract",
                .engineName = "NewbieRenderer",
            });

            auto runtime = std::make_shared<ShutdownCountingNode>(shutdownCount);
            nr::test::require(renderer.installGraph(nr::renderer::RendererGraphSpec{
                                  .nodes =
                                      {
                                          nr::renderer::NodeCreateInfo{
                                              .runtime = std::move(runtime),
                                              .config =
                                                  nr::renderer::NodeConfig{
                                                      .instanceName = "ShutdownCounting",
                                                  },
                                          },
                                      },
                              }),
                              "renderer lifetime graph should install");
        }

        nr::test::requireEqual(*shutdownCount, std::size_t{1u},
                               "Renderer RAII teardown should invoke each installed node shutdown exactly once");
    }};
} // namespace
