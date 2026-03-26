import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(const nr::scene::Scene &scene,
                       const nr::scene::SceneExtractInput &input,
                       nr::scene::SceneExtractProfileHandle profile) {
    { scene.tryGetPrimaryCamera() } -> std::same_as<std::optional<nr::scene::SceneResolvedCamera>>;
    { scene.tryGetPrimaryCamera(std::optional<glm::uvec2>{}) } -> std::same_as<std::optional<nr::scene::SceneResolvedCamera>>;
    { scene.extractPackets(profile, input) } -> std::same_as<nr::scene::ScenePacketSet>;
});

[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }

    return true;
}

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-4f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] float projectionAspectRatio(const glm::mat4 &projection)
{
    if (std::abs(projection[0][0]) <= 1e-6f)
    {
        return 0.0f;
    }

    return projection[1][1] / projection[0][0];
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

[[nodiscard]] std::array<float, 16> translatedTransform(float x, float y, float z)
{
    return {
        1.0f, 0.0f, 0.0f, x,
        0.0f, 1.0f, 0.0f, y,
        0.0f, 0.0f, 1.0f, z,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildProjectionContractSceneAsset(float cameraAspect,
                                                                      std::string_view sourcePath)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::string{sourcePath}};

    auto material = nr::load::MaterialAsset{};
    material.name = "projection_contract_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "projection_contract_mesh";
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
    scene.nodes[1].localTransform = translatedTransform(0.0f, 0.0f, -3.0f);

    scene.nodes[2].name = "CameraNode";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].localTransform = identityTransform();

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "ProjectionContractCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .lookAt = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .horizontalFov = glm::radians(90.0f),
        .aspect = cameraAspect,
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

[[nodiscard]] nr::scene::SceneExtractProfileHandle registerRasterProfile(nr::scene::Scene &scene)
{
    return scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "projection_contract_raster",
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });
}

[[nodiscard]] bool checkImportedAuthoredAspectUsedWithoutViewport()
{
    std::println("\n=== Case: checkImportedAuthoredAspectUsedWithoutViewport ===");

    auto squareSceneAsset = buildProjectionContractSceneAsset(1.0f, "manual_projection_square.gltf");
    auto widescreenSceneAsset = buildProjectionContractSceneAsset(16.0f / 9.0f, "manual_projection_wide.gltf");

    nr::rhi::Device squareDevice{};
    nr::scene::Scene squareScene(nr::scene::SceneCreateInfo{.device = squareDevice});
    auto squareTemplate = squareScene.registerTemplate(squareSceneAsset);
    auto squareInstance = squareScene.instantiate(squareTemplate);
    if (!require(squareTemplate.valid() && squareInstance.valid(), "Square camera scene should register and instantiate."))
    {
        return false;
    }
    squareScene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto squareCamera = squareScene.tryGetPrimaryCamera();
    if (!require(squareCamera.has_value(), "Square authored camera should resolve as primary camera."))
    {
        return false;
    }

    auto squareProjectionAspect = projectionAspectRatio(squareCamera->projection);
    if (!require(almostEqual(squareProjectionAspect, 1.0f, 1e-3f),
                 "Without viewport override, projection should follow imported authored aspect 1:1."))
    {
        return false;
    }

    nr::rhi::Device wideDevice{};
    nr::scene::Scene wideScene(nr::scene::SceneCreateInfo{.device = wideDevice});
    auto wideTemplate = wideScene.registerTemplate(widescreenSceneAsset);
    auto wideInstance = wideScene.instantiate(wideTemplate);
    if (!require(wideTemplate.valid() && wideInstance.valid(), "Widescreen camera scene should register and instantiate."))
    {
        return false;
    }
    wideScene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto wideCamera = wideScene.tryGetPrimaryCamera();
    if (!require(wideCamera.has_value(), "Widescreen authored camera should resolve as primary camera."))
    {
        return false;
    }

    auto wideProjectionAspect = projectionAspectRatio(wideCamera->projection);
    if (!require(almostEqual(wideProjectionAspect, 16.0f / 9.0f, 1e-3f),
                 "Without viewport override, projection should follow imported authored aspect 16:9."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkViewportExtentOverridesAuthoredAspect()
{
    std::println("\n=== Case: checkViewportExtentOverridesAuthoredAspect ===");

    auto sceneAsset = buildProjectionContractSceneAsset(1.0f, "manual_projection_viewport_override.gltf");

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(templateHandle.valid() && instanceHandle.valid(), "Scene should register and instantiate for viewport override case."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto squareCamera = scene.tryGetPrimaryCamera(std::optional<glm::uvec2>{glm::uvec2{1024u, 1024u}});
    auto wideCamera = scene.tryGetPrimaryCamera(std::optional<glm::uvec2>{glm::uvec2{1920u, 1080u}});

    if (!require(squareCamera.has_value() && wideCamera.has_value(), "Primary camera should resolve for both viewport variants."))
    {
        return false;
    }

    auto squareAspect = projectionAspectRatio(squareCamera->projection);
    auto wideAspect = projectionAspectRatio(wideCamera->projection);

    if (!require(almostEqual(squareAspect, 1.0f, 1e-3f),
                 "Square viewport should produce 1:1 projection aspect ratio."))
    {
        return false;
    }

    if (!require(almostEqual(wideAspect, 16.0f / 9.0f, 1e-3f),
                 "Widescreen viewport should produce 16:9 projection aspect ratio."))
    {
        return false;
    }

    if (!require(!almostEqual(squareAspect, wideAspect, 1e-3f),
                 "Viewport override should change projection output when viewport aspect changes."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkPrimaryFrustumUsesViewportExtent()
{
    std::println("\n=== Case: checkPrimaryFrustumUsesViewportExtent ===");

    auto sceneAsset = buildProjectionContractSceneAsset(1.0f, "manual_projection_frustum_extent.gltf");

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template should register for viewport-frustum case."))
    {
        return false;
    }

    auto centerInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                .rootTransform = glm::mat4{1.0f},
                                                                .activate = true,
                                                            });

    auto sideTransform = glm::mat4{1.0f};
    sideTransform[3] = glm::vec4{4.2f, 0.0f, 0.0f, 1.0f};
    auto sideInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                              .rootTransform = sideTransform,
                                                              .activate = true,
                                                          });

    if (!require(centerInstance.valid() && sideInstance.valid(), "Center and side instances should both be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto rasterProfile = registerRasterProfile(scene);
    if (!require(rasterProfile.valid(), "Raster profile should be valid for viewport-frustum case."))
    {
        return false;
    }

    auto squarePackets = scene.extractPackets(rasterProfile, nr::scene::SceneExtractInput{
                                                                 .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                                 .viewportExtent = glm::uvec2{1024u, 1024u},
                                                             });

    auto widePackets = scene.extractPackets(rasterProfile, nr::scene::SceneExtractInput{
                                                               .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                               .viewportExtent = glm::uvec2{1920u, 1080u},
                                                           });

    std::println("[debug] squarePackets={} widePackets={}",
                 squarePackets.rasterDraws.size(),
                 widePackets.rasterDraws.size());

    if (!require(!squarePackets.rasterDraws.empty(),
                 "Square viewport should keep at least the center packet."))
    {
        return false;
    }

    if (!require(squarePackets.rasterDraws.size() < widePackets.rasterDraws.size(),
                 "Wide viewport should include more packets than square viewport for side-offset instance."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkImportedAuthoredAspectUsedWithoutViewport", &checkImportedAuthoredAspectUsedWithoutViewport},
        std::pair{"checkViewportExtentOverridesAuthoredAspect", &checkViewportExtentOverridesAuthoredAspect},
        std::pair{"checkPrimaryFrustumUsesViewportExtent", &checkPrimaryFrustumUsesViewportExtent},
    };

    std::size_t passedCount = 0;
    for (auto const &[name, fn] : cases)
    {
        std::println("\n[run] {}", name);
        auto const ok = fn();
        std::println("[result] {} => {}", name, ok ? "PASS" : "FAIL");
        if (ok)
        {
            ++passedCount;
        }
    }

    std::println("\n[summary] passed={} failed={}", passedCount, cases.size() - passedCount);
    if (passedCount != cases.size())
    {
        return 1;
    }

    return 0;
}
