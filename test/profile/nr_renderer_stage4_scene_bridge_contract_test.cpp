import std;
import nr.renderer;
import nr.scene;
import nr.resource;

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

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
}

static_assert(std::same_as<
              decltype(nr::renderer::NodeFrameParameters{}.sceneBridgeFrame),
              std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>>>);

static_assert(requires(const nr::scene::ScenePacketSet &packetSet,
                       const std::optional<std::reference_wrapper<const nr::scene::SceneResolvedCamera>> &camera) {
    nr::scene::SceneRenderBridgeBuildInput{
        .packetSet = std::cref(packetSet),
        .primaryCamera = camera,
    };

    {
        nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .primaryCamera = camera,
        })
    } -> std::same_as<nr::scene::SceneBridgeFrame>;
});

[[nodiscard]] bool checkSceneRenderBridgeBuildFrame()
{
    auto packetSet = nr::scene::ScenePacketSet{};
    packetSet.domain = nr::scene::ScenePacketDomain::rasterDraw;
    packetSet.rasterDraws = {
        nr::scene::RasterDrawPacket{
            .renderable = {},
            .mesh = nr::resource::MeshHandle{5u, 1u},
            .material = nr::resource::MaterialHandle{11u, 3u},
            .submeshIndex = 0u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 10u,
        },
        nr::scene::RasterDrawPacket{
            .renderable = {},
            .mesh = nr::resource::MeshHandle{6u, 1u},
            .material = nr::resource::MaterialHandle{13u, 2u},
            .submeshIndex = 1u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 20u,
        },
        nr::scene::RasterDrawPacket{
            .renderable = {},
            .mesh = nr::resource::MeshHandle{7u, 1u},
            .material = nr::resource::MaterialHandle{11u, 3u},
            .submeshIndex = 2u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 30u,
        },
    };

    auto camera = nr::scene::SceneResolvedCamera{};
    camera.world = glm::mat4{1.0f};
    camera.world[3] = glm::vec4{2.0f, 3.0f, 4.0f, 1.0f};
    camera.view = glm::mat4{1.0f};
    camera.view[0][0] = 0.5f;
    camera.projection = glm::mat4{1.0f};
    camera.projection[1][1] = 2.0f;

    auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
        .packetSet = std::cref(packetSet),
        .primaryCamera = std::cref(camera),
        .resolveMeshBindless = [](nr::resource::MeshHandle meshHandle) -> std::optional<std::uint32_t> {
            return meshHandle.valid() ? std::optional<std::uint32_t>{meshHandle.slot + 100u} : std::nullopt;
        },
        .resolveMaterialBindless = [](nr::resource::MaterialHandle materialHandle) -> std::optional<std::uint32_t> {
            return materialHandle.valid() ? std::optional<std::uint32_t>{materialHandle.slot + 200u} : std::nullopt;
        },
    });

    if (!require(frame.hasPrimaryCamera, "Bridge frame should mark primary camera as available."))
    {
        return false;
    }

    if (!require(frame.rasterDraws.size() == packetSet.rasterDraws.size(), "Bridge frame should preserve raster draw count."))
    {
        return false;
    }

    if (!require(almostEqual(frame.frameConstants.drawCount, 3.0f), "Bridge frame drawCount constant should match draw list size."))
    {
        return false;
    }

    if (!require(almostEqual(frame.frameConstants.cameraWorld.x, 2.0f) &&
                     almostEqual(frame.frameConstants.cameraWorld.y, 3.0f) &&
                     almostEqual(frame.frameConstants.cameraWorld.z, 4.0f),
                 "Bridge frame camera world constant should come from resolved primary camera."))
    {
        return false;
    }

    auto expectedViewProjection = camera.projection * camera.view;
    if (!require(almostEqual(frame.frameConstants.viewProjection[0][0], expectedViewProjection[0][0]) &&
                     almostEqual(frame.frameConstants.viewProjection[1][1], expectedViewProjection[1][1]),
                 "Bridge frame viewProjection constant should be projection * view."))
    {
        return false;
    }

    if (!require(frame.rasterDraws[0].meshBindless == 105u &&
                     frame.rasterDraws[1].meshBindless == 106u &&
                     frame.rasterDraws[2].meshBindless == 107u,
                 "Bridge frame should apply mesh bindless resolver for each draw."))
    {
        return false;
    }

    if (!require(frame.rasterDraws[0].materialBindless == 211u &&
                     frame.rasterDraws[1].materialBindless == 213u &&
                     frame.rasterDraws[2].materialBindless == 211u,
                 "Bridge frame should apply material bindless resolver for each draw."))
    {
        return false;
    }

    if (!require(frame.materialGroups.size() == 2u, "Bridge frame should group draws by material handle."))
    {
        return false;
    }

    if (!require(frame.materialGroups[0].drawIndices.size() == 2u &&
                     frame.materialGroups[0].drawIndices[0] == 0u &&
                     frame.materialGroups[0].drawIndices[1] == 2u,
                 "Material group for first material should include draw indices [0, 2]."))
    {
        return false;
    }

    if (!require(frame.materialGroups[1].drawIndices.size() == 1u &&
                     frame.materialGroups[1].drawIndices[0] == 1u,
                 "Material group for second material should include draw index [1]."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkSceneRenderBridgeFallbackBindless()
{
    auto packetSet = nr::scene::ScenePacketSet{};
    packetSet.domain = nr::scene::ScenePacketDomain::rasterDraw;
    packetSet.rasterDraws = {
        nr::scene::RasterDrawPacket{
            .renderable = {},
            .mesh = nr::resource::MeshHandle{42u, 9u},
            .material = nr::resource::MaterialHandle{77u, 5u},
            .submeshIndex = 0u,
            .world = glm::mat4{1.0f},
            .worldBounds = {},
            .sortKey = 1u,
        },
    };

    auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
        .packetSet = std::cref(packetSet),
        .primaryCamera = std::nullopt,
    });

    if (!require(!frame.hasPrimaryCamera, "Bridge frame should mark camera unavailable when not provided."))
    {
        return false;
    }

    if (!require(frame.rasterDraws.size() == 1u, "Bridge frame should preserve raster draw count in fallback case."))
    {
        return false;
    }

    if (!require(frame.rasterDraws[0].meshBindless == 42u && frame.rasterDraws[0].materialBindless == 77u,
                 "Bridge frame should fall back to resource slot as bindless index when resolver is absent."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!checkSceneRenderBridgeBuildFrame())
    {
        std::println("[FAIL] stage4 scene bridge build-frame contract failed");
        return 1;
    }

    if (!checkSceneRenderBridgeFallbackBindless())
    {
        std::println("[FAIL] stage4 scene bridge fallback-bindless contract failed");
        return 2;
    }

    std::println("[OK] renderer stage4 scene bridge contract tests passed");
    return 0;
}
