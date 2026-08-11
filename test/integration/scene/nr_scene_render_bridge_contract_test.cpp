import std;
import dependency.math;
import dependency.vulkan;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
inline constexpr nr::scene::SceneTextureId normalTextureId = 17u;
inline constexpr nr::scene::SceneTextureId occlusionTextureId = 23u;

[[nodiscard]] bool near(float left, float right, float epsilon = 1e-4f) noexcept
{
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] bool vec2Near(const DirectX::XMFLOAT2 &left, const DirectX::XMFLOAT2 &right,
                            float epsilon = 1e-4f) noexcept
{
    return near(left.x, right.x, epsilon) && near(left.y, right.y, epsilon);
}

[[nodiscard]] bool vec3Near(const DirectX::XMFLOAT3 &left, const DirectX::XMFLOAT3 &right,
                            float epsilon = 1e-4f) noexcept
{
    return near(left.x, right.x, epsilon) && near(left.y, right.y, epsilon) && near(left.z, right.z, epsilon);
}

[[nodiscard]] bool vec4Near(const DirectX::XMFLOAT4 &left, const DirectX::XMFLOAT4 &right,
                            float epsilon = 1e-4f) noexcept
{
    return near(left.x, right.x, epsilon) && near(left.y, right.y, epsilon) && near(left.z, right.z, epsilon) &&
           near(left.w, right.w, epsilon);
}

[[nodiscard]] bool mat4Near(const DirectX::XMFLOAT4X4 &left, const DirectX::XMFLOAT4X4 &right,
                            float epsilon = 1e-4f) noexcept
{
    auto rows = std::views::iota(0, 4);
    auto columns = std::views::iota(0, 4);
    return std::ranges::all_of(rows, [&](int row) {
        return std::ranges::all_of(columns,
                                   [&](int column) { return near(left.m[row][column], right.m[row][column], epsilon); });
    });
}

[[nodiscard]] DirectX::XMFLOAT4X4 diagonalMatrix(float value) noexcept
{
    return DirectX::XMFLOAT4X4{
        value, 0.0f, 0.0f, 0.0f,
        0.0f, value, 0.0f, 0.0f,
        0.0f, 0.0f, value, 0.0f,
        0.0f, 0.0f, 0.0f, value,
    };
}

[[nodiscard]] nr::scene::ScenePacketSet makeRasterPacketSet(const nr::rhi::Buffer &vertexAtlas,
                                                            const nr::rhi::Buffer &indexAtlas)
{
    auto materialTextures = nr::scene::SceneMaterialTextureBindings{};
    materialTextures.ids[nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal)] =
        normalTextureId;
    materialTextures.ids[nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::occlusion)] =
        occlusionTextureId;
    materialTextures.normal = nr::scene::SceneMaterialNormalTextureBinding{
        .textureId = normalTextureId,
        .uvSet = 1u,
        .uvLinear = DirectX::XMFLOAT4{2.0f, 0.25f, -0.5f, 3.0f},
        .uvOffset = DirectX::XMFLOAT2{0.125f, -0.25f},
        .normalScale = 0.75f,
    };

    auto translated = nr::scene::kIdentityMatrix;
    translated._41 = 1.0f;
    translated._42 = 2.0f;
    translated._43 = 3.0f;

    return nr::scene::ScenePacketSet{
        .rasterGeometryBuffers =
            nr::scene::SceneBridgeGeometryBuffers{
                .vertexBuffer =
                    nr::scene::SceneBridgeBufferBinding{
                        .buffer = std::cref(vertexAtlas),
                        .offset = 64u,
                    },
                .indexBuffer =
                    nr::scene::SceneBridgeBufferBinding{
                        .buffer = std::cref(indexAtlas),
                        .offset = 128u,
                    },
                .indexType = vk::IndexType::eUint16,
            },
        .rasterTextureHandlesById =
            {
                {normalTextureId, nr::resource::TextureHandle{normalTextureId - 1u, 4u}},
                {occlusionTextureId, nr::resource::TextureHandle{occlusionTextureId - 1u, 5u}},
            },
        .rasterDraws =
            {
                nr::scene::RasterDrawPacket{
                    .mesh = nr::resource::MeshHandle{3u, 1u},
                    .material = nr::resource::MaterialHandle{9u, 2u},
                    .world = nr::scene::kIdentityMatrix,
                    .worldBounds = nr::resource::Aabb{DirectX::XMFLOAT3{-1.0f, -1.0f, -1.0f},
                                                       DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}},
                    .sortKey = 10u,
                    .materialTextures = materialTextures,
                    .geometry =
                        nr::scene::SceneBridgeDrawGeometry{
                            .firstIndex = 6u,
                            .indexCount = 3u,
                            .vertexOffset = 4,
                            .frontFace = vk::FrontFace::eClockwise,
                        },
                },
                nr::scene::RasterDrawPacket{
                    .mesh = nr::resource::MeshHandle{4u, 1u},
                    .material = nr::resource::MaterialHandle{10u, 2u},
                    .geometryIndex = 1u,
                    .world = translated,
                    .worldBounds = nr::resource::Aabb{DirectX::XMFLOAT3{}, DirectX::XMFLOAT3{2.0f, 2.0f, 2.0f}},
                    .sortKey = 20u,
                    .materialTextures = materialTextures,
                    .materialRaster =
                        nr::scene::SceneBridgeMaterialRasterState{
                            .cullMode = vk::CullModeFlags{vk::CullModeFlagBits::eNone},
                        },
                    .geometry =
                        nr::scene::SceneBridgeDrawGeometry{
                            .firstVertex = 8u,
                            .vertexCount = 4u,
                        },
                },
            },
    };
}

static_assert(
    std::same_as<decltype(nr::scene::SceneBridgeFrame::rasterDraws), std::vector<nr::scene::RasterDrawPacket>>);

const nr::test::CaseRegistrar rasterFrameCase{
    "scene render bridge copies resolved raster packets and frame-owned bindings", [] {
        auto vertexAtlas = nr::rhi::Buffer{};
        auto indexAtlas = nr::rhi::Buffer{};
        auto packetSet = makeRasterPacketSet(vertexAtlas, indexAtlas);
        auto frameConstants = nr::scene::SceneBridgeFrameConstants{
            .view = diagonalMatrix(2.0f),
            .projection = diagonalMatrix(3.0f),
            .viewProjection = diagonalMatrix(4.0f),
            .cameraWorld = DirectX::XMFLOAT3{5.0f, 6.0f, 7.0f},
        };

        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .frameConstantsOverride = frameConstants,
        });

        nr::test::requireEqual(frame.domain, nr::scene::ScenePacketDomain::rasterDraw);
        nr::test::require(frame.hasPrimaryCamera, "frame constants override should mark camera data present");
        nr::test::require(mat4Near(frame.frameConstants.view, frameConstants.view),
                          "view matrix should come from the frame constants override");
        nr::test::requireEqual(frame.frameConstants.drawCount, 2.0f);
        nr::test::requireEqual(frame.rasterDraws.size(), std::size_t{2u});

        nr::test::require(frame.geometryBuffers.hasVertexBuffer(),
                          "raster bridge frame should carry the shared vertex atlas binding");
        nr::test::require(frame.geometryBuffers.hasIndexBuffer(),
                          "raster bridge frame should carry the shared index atlas binding");
        nr::test::require(std::addressof(frame.geometryBuffers.vertexBuffer.buffer->get()) ==
                              std::addressof(vertexAtlas),
                          "frame vertex binding should reference the packet-set atlas");
        nr::test::require(std::addressof(frame.geometryBuffers.indexBuffer.buffer->get()) == std::addressof(indexAtlas),
                          "frame index binding should reference the packet-set atlas");
        nr::test::requireEqual(frame.geometryBuffers.vertexBuffer.offset, vk::DeviceSize{64u});
        nr::test::requireEqual(frame.geometryBuffers.indexBuffer.offset, vk::DeviceSize{128u});
        nr::test::requireEqual(frame.geometryBuffers.indexType, vk::IndexType::eUint16);
        nr::test::requireEqual(frame.rasterTextureHandlesById, packetSet.rasterTextureHandlesById,
                               "frame should carry the packet-set texture handle table");
        nr::test::require(std::ranges::all_of(frame.rasterTextureHandlesById,
                                              [](const auto &entry) {
                                                  return entry.second.valid() &&
                                                         nr::scene::sceneTextureId(entry.second) == entry.first;
                                              }),
                          "texture table entries should be valid and keyed by their scene texture ids");

        auto const &indexed = frame.rasterDraws[0];
        nr::test::requireEqual(indexed.mesh, packetSet.rasterDraws[0].mesh);
        nr::test::requireEqual(indexed.material, packetSet.rasterDraws[0].material);
        nr::test::requireEqual(indexed.geometryIndex, 0u);
        nr::test::require(mat4Near(indexed.world, packetSet.rasterDraws[0].world),
                          "indexed draw world transform should be copied");
        nr::test::require(vec3Near(indexed.worldBounds.min, DirectX::XMFLOAT3{-1.0f, -1.0f, -1.0f}) &&
                              vec3Near(indexed.worldBounds.max, DirectX::XMFLOAT3{1.0f, 1.0f, 1.0f}),
                          "indexed draw bounds should be copied");
        nr::test::requireEqual(indexed.sortKey, std::uint64_t{10u});
        nr::test::require(indexed.geometry.indexed(), "first draw should remain indexed");
        nr::test::requireEqual(indexed.geometry.firstIndex, 6u);
        nr::test::requireEqual(indexed.geometry.indexCount, 3u);
        nr::test::requireEqual(indexed.geometry.vertexOffset, 4);
        nr::test::requireEqual(indexed.geometry.frontFace, vk::FrontFace::eClockwise);
        nr::test::requireEqual(indexed.materialRaster.cullMode, vk::CullModeFlags{vk::CullModeFlagBits::eBack});

        auto const &nonIndexed = frame.rasterDraws[1];
        nr::test::requireEqual(nonIndexed.mesh, packetSet.rasterDraws[1].mesh);
        nr::test::requireEqual(nonIndexed.material, packetSet.rasterDraws[1].material);
        nr::test::requireEqual(nonIndexed.geometryIndex, 1u);
        nr::test::require(mat4Near(nonIndexed.world, packetSet.rasterDraws[1].world),
                          "non-indexed draw world transform should be copied");
        nr::test::requireEqual(nonIndexed.sortKey, std::uint64_t{20u});
        nr::test::require(!nonIndexed.geometry.indexed(), "second draw should remain non-indexed");
        nr::test::requireEqual(nonIndexed.geometry.firstVertex, 8u);
        nr::test::requireEqual(nonIndexed.geometry.vertexCount, 4u);
        nr::test::requireEqual(nonIndexed.materialRaster.cullMode, vk::CullModeFlags{vk::CullModeFlagBits::eNone},
                               "double-sided material state should be represented by disabled culling");

        auto const &normal = indexed.materialTextures.normal;
        nr::test::requireEqual(
            indexed.materialTextures
                .ids[nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal)],
            normalTextureId);
        nr::test::requireEqual(normal.textureId, normalTextureId);
        nr::test::requireEqual(normal.uvSet, 1u);
        nr::test::require(vec4Near(normal.uvLinear, DirectX::XMFLOAT4{2.0f, 0.25f, -0.5f, 3.0f}),
                          "normal texture linear UV transform should cross the bridge");
        nr::test::require(vec2Near(normal.uvOffset, DirectX::XMFLOAT2{0.125f, -0.25f}),
                          "normal texture UV offset should cross the bridge");
        nr::test::require(near(normal.normalScale, 0.75f), "normal texture scale should cross the bridge");
    }};

const nr::test::CaseRegistrar cameraPrecedenceCase{
    "scene render bridge applies frame override before primary camera", [] {
        auto packetSet = nr::scene::ScenePacketSet{};
        auto overrideConstants = nr::scene::SceneBridgeFrameConstants{
            .view = diagonalMatrix(2.0f),
            .projection = diagonalMatrix(3.0f),
            .viewProjection = diagonalMatrix(4.0f),
            .cameraWorld = DirectX::XMFLOAT3{1.0f, 2.0f, 3.0f},
        };

        auto overrideFrame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .frameConstantsOverride = overrideConstants,
        });
        nr::test::require(overrideFrame.hasPrimaryCamera, "frame override should provide camera constants");
        nr::test::require(mat4Near(overrideFrame.frameConstants.view, overrideConstants.view),
                          "frame override view should be used without a primary camera");
        nr::test::require(mat4Near(overrideFrame.frameConstants.projection, overrideConstants.projection),
                          "frame override projection should be used without a primary camera");
        nr::test::require(vec3Near(overrideFrame.frameConstants.cameraWorld, overrideConstants.cameraWorld),
                          "frame override camera position should be used without a primary camera");

        auto cameraWorld = nr::scene::kIdentityMatrix;
        cameraWorld._41 = 7.0f;
        cameraWorld._42 = 8.0f;
        cameraWorld._43 = 9.0f;
        auto camera = nr::scene::SceneResolvedCamera{
            .world = cameraWorld,
            .view = diagonalMatrix(6.0f),
            .projection = diagonalMatrix(7.0f),
        };
        auto cameraFrame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
            .primaryCamera = std::cref(camera),
            .frameConstantsOverride = overrideConstants,
        });

        nr::test::require(cameraFrame.hasPrimaryCamera, "primary camera should mark camera data present");
        nr::test::require(mat4Near(cameraFrame.frameConstants.view, camera.view),
                          "primary camera view should take precedence over the frame override");
        nr::test::require(mat4Near(cameraFrame.frameConstants.projection, camera.projection),
                          "primary camera projection should take precedence over the frame override");
        auto expectedViewProjection = DirectX::XMFLOAT4X4{};
        DirectX::XMStoreFloat4x4(&expectedViewProjection,
                                 DirectX::XMMatrixMultiply(DirectX::XMLoadFloat4x4(&camera.view),
                                                            DirectX::XMLoadFloat4x4(&camera.projection)));
        nr::test::require(mat4Near(cameraFrame.frameConstants.viewProjection, expectedViewProjection),
                          "primary camera should derive the final view-projection matrix");
        nr::test::require(vec3Near(cameraFrame.frameConstants.cameraWorld, DirectX::XMFLOAT3{7.0f, 8.0f, 9.0f}),
                          "primary camera world position should take precedence over the frame override");
    }};

const nr::test::CaseRegistrar nonRasterCase{
    "scene render bridge bypasses raster payloads for non-raster packet domains", [] {
        auto packetSet = nr::scene::ScenePacketSet{
            .domain = nr::scene::ScenePacketDomain::rayTracingInstance,
            .rasterDraws =
                {
                    nr::scene::RasterDrawPacket{},
                },
            .rtInstances =
                {
                    nr::scene::RayTracingInstancePacket{.mesh = nr::resource::MeshHandle{7u, 1u}},
                },
        };

        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packetSet),
        });

        nr::test::requireEqual(frame.domain, nr::scene::ScenePacketDomain::rayTracingInstance);
        nr::test::require(frame.rasterDraws.empty(), "non-raster packets should not create raster bridge draws");
        nr::test::require(!frame.geometryBuffers.hasVertexBuffer(),
                          "non-raster packets should not expose raster geometry bindings");
        nr::test::require(frame.rasterTextureHandlesById.empty(),
                          "non-raster packets should not expose raster texture bindings");
        nr::test::require(!frame.hasPrimaryCamera, "camera data should remain absent without camera input");
    }};

const nr::test::CaseRegistrar rasterPacketResolutionCase{
    "scene raster packet resolution validates frame-owned bindings and texture invariants", [] {
        auto vertexAtlas = nr::rhi::Buffer{};
        auto indexAtlas = nr::rhi::Buffer{};
        auto packetSet = makeRasterPacketSet(vertexAtlas, indexAtlas);
        auto const &indexed = packetSet.rasterDraws[0];

        nr::test::require(nr::scene::rasterDrawPacketResolved(indexed, packetSet.rasterGeometryBuffers,
                                                              packetSet.rasterTextureHandlesById),
                          "fully resolved indexed packet should satisfy the bridge invariant");

        auto invalidMesh = indexed;
        invalidMesh.mesh = {};
        nr::test::require(!nr::scene::rasterDrawPacketResolved(invalidMesh, packetSet.rasterGeometryBuffers,
                                                               packetSet.rasterTextureHandlesById),
                          "packet with an unresolved mesh should fail validation");

        auto invalidMaterial = indexed;
        invalidMaterial.material = {};
        nr::test::require(!nr::scene::rasterDrawPacketResolved(invalidMaterial, packetSet.rasterGeometryBuffers,
                                                               packetSet.rasterTextureHandlesById),
                          "packet with an unresolved material should fail validation");

        auto invalidGeometry = indexed;
        invalidGeometry.geometry = {};
        nr::test::require(!nr::scene::rasterDrawPacketResolved(invalidGeometry, packetSet.rasterGeometryBuffers,
                                                               packetSet.rasterTextureHandlesById),
                          "packet with unresolved geometry should fail validation");

        auto noVertexBuffers = nr::scene::SceneBridgeGeometryBuffers{};
        nr::test::require(
            !nr::scene::rasterDrawPacketResolved(indexed, noVertexBuffers, packetSet.rasterTextureHandlesById),
            "packet without a frame vertex atlas should fail validation");

        auto missingNormalTexture = packetSet.rasterTextureHandlesById;
        missingNormalTexture.erase(normalTextureId);
        nr::test::require(
            !nr::scene::rasterDrawPacketResolved(indexed, packetSet.rasterGeometryBuffers, missingNormalTexture),
            "packet whose texture id is absent from the frame table should fail validation");

        auto mismatchedNormalTexture = packetSet.rasterTextureHandlesById;
        mismatchedNormalTexture[normalTextureId] = nr::resource::TextureHandle{99u, 1u};
        nr::test::require(
            !nr::scene::rasterDrawPacketResolved(indexed, packetSet.rasterGeometryBuffers, mismatchedNormalTexture),
            "texture table key must match the handle-derived scene texture id");

        auto invalidNormalTexture = packetSet.rasterTextureHandlesById;
        invalidNormalTexture[normalTextureId] = {};
        nr::test::require(
            !nr::scene::rasterDrawPacketResolved(indexed, packetSet.rasterGeometryBuffers, invalidNormalTexture),
            "texture table entries must contain valid handles");

        auto mismatchedNormalBinding = indexed;
        mismatchedNormalBinding.materialTextures.normal.textureId = occlusionTextureId;
        nr::test::require(!nr::scene::rasterDrawPacketResolved(mismatchedNormalBinding, packetSet.rasterGeometryBuffers,
                                                               packetSet.rasterTextureHandlesById),
                          "normal binding id must match the normal material slot id");

        auto invalidUvSet = indexed;
        invalidUvSet.materialTextures.normal.uvSet = 2u;
        nr::test::require(!nr::scene::rasterDrawPacketResolved(invalidUvSet, packetSet.rasterGeometryBuffers,
                                                               packetSet.rasterTextureHandlesById),
                          "normal binding should reject unsupported UV sets");

        auto noIndexBuffers = packetSet.rasterGeometryBuffers;
        noIndexBuffers.indexBuffer = {};
        nr::test::require(
            !nr::scene::rasterDrawPacketResolved(indexed, noIndexBuffers, packetSet.rasterTextureHandlesById),
            "indexed packet without a frame index atlas should fail validation");
        nr::test::require(nr::scene::rasterDrawPacketResolved(packetSet.rasterDraws[1], noIndexBuffers,
                                                              packetSet.rasterTextureHandlesById),
                          "non-indexed packet should not require a frame index atlas");
    }};
} // namespace
