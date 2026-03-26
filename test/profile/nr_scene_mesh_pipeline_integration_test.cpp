import std;
import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;

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

[[nodiscard]] std::array<float, 16> translatedTransform(float tx, float ty, float tz)
{
    return {
        1.0f, 0.0f, 0.0f, tx,
        0.0f, 1.0f, 0.0f, ty,
        0.0f, 0.0f, 1.0f, tz,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildMeshIntegrationSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"profile_scene_mesh_pipeline.gltf"};

    auto material = nr::load::MaterialAsset{};
    material.name = "profile_mesh_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "profile_mesh";
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
        .name = "ProfileGraphicsCamera",
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

struct GraphicsConsumerDrawItem
{
    std::uint64_t sortKey = 0;
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    glm::mat4 world{1.0f};
};

struct GraphicsConsumerInput
{
    glm::mat4 viewProjection{1.0f};
    std::vector<GraphicsConsumerDrawItem> draws{};
};

[[nodiscard]] std::optional<GraphicsConsumerInput> buildGraphicsConsumerInput(
    const nr::scene::SceneResolvedCamera &camera,
    std::span<const nr::scene::RasterDrawPacket> rasterPackets)
{
    if (rasterPackets.empty())
    {
        return std::nullopt;
    }

    auto consumerInput = GraphicsConsumerInput{};
    consumerInput.viewProjection = camera.projection * camera.view;
    consumerInput.draws.reserve(rasterPackets.size());

    std::ranges::for_each(rasterPackets, [&](const nr::scene::RasterDrawPacket &packet) {
        consumerInput.draws.push_back(GraphicsConsumerDrawItem{
            .sortKey = packet.sortKey,
            .mesh = packet.mesh,
            .material = packet.material,
            .world = packet.world,
        });
    });

    return consumerInput;
}

[[nodiscard]] float projectionAspectRatio(const glm::mat4 &projection)
{
    if (std::abs(projection[0][0]) <= 1e-6f)
    {
        return 0.0f;
    }

    return projection[1][1] / projection[0][0];
}

[[nodiscard]] bool checkSceneDrivenGraphicsConsumer()
{
    std::println("\n=== Case: checkSceneDrivenGraphicsConsumer ===");

    auto sceneAsset = buildMeshIntegrationSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed for graphics integration scene."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance should be valid for graphics integration scene."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto profileHandle = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "profile_scene_graphics",
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    if (!require(profileHandle.valid(), "Graphics extraction profile should be valid."))
    {
        return false;
    }

    auto const viewportExtent = glm::uvec2{1920u, 1080u};
    auto primaryCamera = scene.tryGetPrimaryCamera(std::optional<glm::uvec2>{viewportExtent});
    if (!require(primaryCamera.has_value(), "Graphics consumer should resolve a primary camera."))
    {
        return false;
    }
    if (!require(!primaryCamera->fallback, "Graphics integration scene should use imported camera, not fallback."))
    {
        return false;
    }

    if (!require(std::abs(projectionAspectRatio(primaryCamera->projection) - (16.0f / 9.0f)) <= 1e-3f,
                 "Graphics consumer path should use explicit viewport extent for camera projection."))
    {
        return false;
    }

    auto packetSet = scene.extractPackets(profileHandle, nr::scene::SceneExtractInput{
                                                             .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                             .viewportExtent = viewportExtent,
                                                         });

    if (!require(packetSet.domain == nr::scene::ScenePacketDomain::rasterDraw,
                 "Graphics extraction profile should return rasterDraw packet domain."))
    {
        return false;
    }
    if (!require(!packetSet.rasterDraws.empty(), "Scene-driven graphics extraction should produce raster draw packets."))
    {
        return false;
    }

    auto consumerInput = buildGraphicsConsumerInput(*primaryCamera, std::span{packetSet.rasterDraws});
    if (!require(consumerInput.has_value(), "Graphics consumer input should be buildable from ScenePacketSet."))
    {
        return false;
    }

    if (!require(consumerInput->draws.size() == packetSet.rasterDraws.size(),
                 "Graphics consumer draw count should match extracted raster packet count."))
    {
        return false;
    }

    if (!require(std::ranges::all_of(consumerInput->draws, [](const GraphicsConsumerDrawItem &item) {
                      return item.mesh.valid() && item.material.valid();
                  }),
                 "All graphics consumer items should carry valid mesh/material handles."))
    {
        return false;
    }

    if (!require(std::ranges::is_sorted(consumerInput->draws, {}, &GraphicsConsumerDrawItem::sortKey),
                 "Graphics consumer should observe stable raster sort ordering from Scene."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkSceneDrivenGraphicsConsumer", &checkSceneDrivenGraphicsConsumer},
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
