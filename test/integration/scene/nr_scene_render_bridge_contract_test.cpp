import std;
import dependency;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
[[nodiscard]] bool near(float left, float right, float epsilon = 1e-4f) noexcept
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] bool mat4Near(const glm::mat4 &left, const glm::mat4 &right, float epsilon = 1e-4f)
{
    auto rows = std::views::iota(0, 4);
    auto columns = std::views::iota(0, 4);
    return std::ranges::all_of(columns, [&](int column) {
        return std::ranges::all_of(rows, [&](int row) {
            return near(left[column][row], right[column][row], epsilon);
        });
    });
}

[[nodiscard]] nr::scene::ScenePacketSet makeRasterPacketSet()
{
    auto meshA = nr::resource::MeshHandle{3u, 1u};
    auto meshB = nr::resource::MeshHandle{4u, 1u};
    auto material = nr::resource::MaterialHandle{9u, 2u};

    auto translated = glm::mat4{1.0f};
    translated[3] = glm::vec4{1.0f, 2.0f, 3.0f, 1.0f};

    return nr::scene::ScenePacketSet{
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .rasterDraws = {
            nr::scene::RasterDrawPacket{
                .mesh = meshA,
                .material = material,
                .submeshIndex = 0,
                .world = glm::mat4{1.0f},
                .worldBounds = nr::resource::Aabb{glm::vec3{-1.0f, -1.0f, -1.0f}, glm::vec3{1.0f, 1.0f, 1.0f}},
                .sortKey = 10,
            },
            nr::scene::RasterDrawPacket{
                .mesh = meshB,
                .material = material,
                .submeshIndex = 1,
                .world = translated,
                .worldBounds = nr::resource::Aabb{glm::vec3{0.0f, 0.0f, 0.0f}, glm::vec3{2.0f, 2.0f, 2.0f}},
                .sortKey = 20,
            },
        },
    };
}

const nr::test::CaseRegistrar rasterFrameCase{
    "scene render bridge converts raster packets into frame draws and material groups",
    [] {
        auto packetSet = makeRasterPacketSet();
        auto frameConstants = nr::scene::SceneBridgeFrameConstants{
            .view = glm::mat4{2.0f},
            .projection = glm::mat4{3.0f},
            .viewProjection = glm::mat4{4.0f},
            .cameraWorld = glm::vec3{5.0f, 6.0f, 7.0f},
        };

        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .frameConstantsOverride = frameConstants,
            .resolveMeshBindless = [](nr::resource::MeshHandle handle) -> std::optional<std::uint32_t> {
                return handle.slot + 100u;
            },
            .resolveMaterialBindless = [](nr::resource::MaterialHandle handle) -> std::optional<std::uint32_t> {
                return handle.slot + 200u;
            },
            .resolveRasterDrawGeometry = [](nr::resource::MeshHandle handle, std::uint32_t submeshIndex) -> std::optional<nr::scene::SceneBridgeDrawGeometry> {
                return nr::scene::SceneBridgeDrawGeometry{
                    .firstVertex = handle.slot,
                    .vertexCount = 3u + submeshIndex,
                    .firstIndex = submeshIndex * 3u,
                    .indexCount = 3u,
                    .frontFace = vk::FrontFace::eClockwise,
                };
            },
        });

        nr::test::requireEqual(frame.domain, nr::scene::ScenePacketDomain::rasterDraw);
        nr::test::require(frame.hasPrimaryCamera, "frame constants override should mark camera data present");
        nr::test::requireEqual(frame.rasterDraws.size(), std::size_t{2});
        nr::test::requireEqual(frame.materialGroups.size(), std::size_t{1});
        nr::test::requireEqual(frame.materialGroups.front().drawIndices, std::vector<std::uint32_t>{0u, 1u});
        nr::test::requireEqual(frame.frameConstants.drawCount, 2.0f);
        nr::test::require(mat4Near(frame.frameConstants.view, frameConstants.view), "view matrix should come from override");
        nr::test::requireEqual(frame.rasterDraws[0].meshBindless, 103u);
        nr::test::requireEqual(frame.rasterDraws[1].meshBindless, 104u);
        nr::test::requireEqual(frame.rasterDraws[0].materialBindless, 209u);
        nr::test::requireEqual(frame.rasterDraws[1].geometry.firstVertex, 4u);
        nr::test::requireEqual(frame.rasterDraws[1].geometry.vertexCount, 4u);
        nr::test::requireEqual(frame.rasterDraws[1].geometry.firstIndex, 3u);
        nr::test::requireEqual(frame.rasterDraws[1].geometry.frontFace, vk::FrontFace::eClockwise);
    }};

const nr::test::CaseRegistrar cameraPrecedenceCase{
    "scene render bridge imported primary camera overrides frame constant override",
    [] {
        auto packetSet = makeRasterPacketSet();
        auto cameraWorld = glm::mat4{1.0f};
        cameraWorld[3] = glm::vec4{2.0f, 3.0f, 4.0f, 1.0f};

        auto camera = nr::scene::SceneResolvedCamera{
            .world = cameraWorld,
            .view = glm::mat4{6.0f},
            .projection = glm::mat4{7.0f},
        };
        auto overrideConstants = nr::scene::SceneBridgeFrameConstants{
            .view = glm::mat4{2.0f},
            .projection = glm::mat4{3.0f},
            .viewProjection = glm::mat4{4.0f},
            .cameraWorld = glm::vec3{1.0f},
        };

        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .primaryCamera = std::cref(camera),
            .frameConstantsOverride = overrideConstants,
        });

        nr::test::require(frame.hasPrimaryCamera, "primary camera should mark camera data present");
        nr::test::require(mat4Near(frame.frameConstants.view, camera.view), "primary camera view should win");
        nr::test::require(mat4Near(frame.frameConstants.projection, camera.projection), "primary camera projection should win");
        nr::test::require(mat4Near(frame.frameConstants.viewProjection, camera.projection * camera.view),
                          "primary camera should derive viewProjection");
        nr::test::require(near(frame.frameConstants.cameraWorld.x, 2.0f) &&
                              near(frame.frameConstants.cameraWorld.y, 3.0f) &&
                              near(frame.frameConstants.cameraWorld.z, 4.0f),
                          "primary camera world position should be copied");
    }};

const nr::test::CaseRegistrar nonRasterCase{
    "scene render bridge ignores draw conversion for non-raster packet domains",
    [] {
        auto packetSet = nr::scene::ScenePacketSet{
            .domain = nr::scene::ScenePacketDomain::rayTracingInstance,
            .rtInstances = {
                nr::scene::RayTracingInstancePacket{.mesh = nr::resource::MeshHandle{7u, 1u}},
            },
        };

        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
        });

        nr::test::requireEqual(frame.domain, nr::scene::ScenePacketDomain::rayTracingInstance);
        nr::test::require(frame.rasterDraws.empty(), "non-raster packets should not create raster bridge draws");
        nr::test::require(frame.materialGroups.empty(), "non-raster packets should not create material groups");
        nr::test::require(!frame.hasPrimaryCamera, "camera data should remain absent without camera input");
    }};
} // namespace
