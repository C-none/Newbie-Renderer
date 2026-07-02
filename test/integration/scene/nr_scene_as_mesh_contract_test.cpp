import std;
import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
[[nodiscard]] std::array<float, 16> identityTransform() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset makeAsMeshSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"scene_as_mesh_contract.gltf"};

    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "opaque_single_sided",
    });
    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "blend_double_sided",
        .opacity = 0.5f,
        .alphaModeHint = nr::load::MaterialAlphaModeHint::blend,
        .doubleSided = true,
    });

    auto makeMesh = [](std::string name, bool clockwiseFrontFace) {
        return nr::load::MeshAsset{
            .name = std::move(name),
            .vertices = {
                nr::load::VertexAsset{.position = {-1.0f, -1.0f, 0.0f}},
                nr::load::VertexAsset{.position = {0.0f, -1.0f, 0.0f}},
                nr::load::VertexAsset{.position = {-1.0f, 1.0f, 0.0f}},
                nr::load::VertexAsset{.position = {1.0f, -1.0f, 0.0f}},
                nr::load::VertexAsset{.position = {1.0f, 1.0f, 0.0f}},
            },
            .indices = {0u, 1u, 2u, 1u, 3u, 4u},
            .geometries = {
                nr::load::MeshGeometryAsset{
                    .name = "opaque_geometry",
                    .firstIndex = 0u,
                    .indexCount = 3u,
                    .materialIndex = 0u,
                },
                nr::load::MeshGeometryAsset{
                    .name = "blend_double_sided_geometry",
                    .firstIndex = 3u,
                    .indexCount = 3u,
                    .materialIndex = 1u,
                },
            },
            .clockwiseFrontFace = clockwiseFrontFace,
        };
    };

    scene.meshes.push_back(makeMesh("as_contract_quad_ccw", false));
    scene.meshes.push_back(makeMesh("as_contract_quad_clockwise", true));

    scene.nodes.resize(2u);
    scene.rootNodeIndex = 0u;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0u;
    scene.nodes[1].meshIndices = {0u, 1u};
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.vertexCount = 10u;
    scene.stats.indexCount = 12u;
    return scene;
}

[[nodiscard]] bool hasInstanceFlag(
    vk::GeometryInstanceFlagsKHR flags,
    vk::GeometryInstanceFlagBitsKHR flag) noexcept
{
    return static_cast<bool>(flags & flag);
}

[[nodiscard]] bool hasGeometryFlag(
    vk::GeometryFlagsKHR flags,
    vk::GeometryFlagBitsKHR flag) noexcept
{
    return static_cast<bool>(flags & flag);
}

[[nodiscard]] nr::resource::MeshHandle resolveMeshHandle(
    const nr::scene::Scene& scene,
    const nr::load::SceneAsset& sceneAsset,
    std::uint32_t meshIndex = 0u)
{
    auto handle = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, meshIndex));
    nr::test::require(handle.has_value(), "AS contract mesh handle should resolve by stable key");
    return *handle;
}

[[nodiscard]] nr::resource::MaterialHandle resolveMaterialHandle(
    const nr::scene::Scene& scene,
    const nr::load::SceneAsset& sceneAsset,
    std::uint32_t materialIndex)
{
    auto handle = scene.findMaterialHandleByStableKey(
        nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, materialIndex));
    nr::test::require(handle.has_value(), "AS contract material handle should resolve by stable key");
    return *handle;
}

void uploadSceneGeometry(nr::scene::Scene& scene, nr::rhi::Device& device)
{
    scene.beginFrame(0u);
    scene.uploadPending();
    device.waitIdle();
}

const nr::test::CaseRegistrar asMeshFlagsCase{
    "scene AS mesh query derives instance and geometry flags from mesh and material state",
    [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_scene_as_mesh_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeAsMeshSceneAsset();
        auto templateHandle = scene.registerTemplate(sceneAsset);
        nr::test::require(templateHandle.valid(), "AS contract template registration should succeed");

        auto meshHandle = resolveMeshHandle(scene, sceneAsset, 0u);
        auto clockwiseMeshHandle = resolveMeshHandle(scene, sceneAsset, 1u);
        auto doubleSidedMaterial = resolveMaterialHandle(scene, sceneAsset, 1u);
        uploadSceneGeometry(scene, device);

        auto importedClockwiseMesh = scene.tryGetMeshAsset(clockwiseMeshHandle);
        nr::test::require(importedClockwiseMesh.has_value(), "clockwise source mesh should import into scene mesh storage");
        nr::test::require(
            importedClockwiseMesh->get().cpu.clockwiseFrontFace,
            "mesh bridge should preserve source clockwise front-face winding");

        auto asMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(asMesh.has_value(), "resident scene mesh should expose AS build input");
        nr::test::requireEqual(asMesh->geometries.size(), std::size_t{2u});
        nr::test::require(
            !hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing),
            "default CCW mesh should keep Vulkan RT triangle facing unflipped");
        nr::test::require(
            hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "any double-sided geometry should disable facing cull for the whole TLAS instance");
        nr::test::require(
            hasGeometryFlag(asMesh->geometries[0].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "opaque material should mark AS geometry opaque");
        nr::test::require(
            !hasGeometryFlag(asMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "alpha-blended material should keep AS geometry non-opaque");

        auto clockwiseAsMesh = scene.tryGetAccelerationStructureMesh(clockwiseMeshHandle);
        nr::test::require(clockwiseAsMesh.has_value(), "clockwise mesh should still expose AS build input");
        nr::test::require(
            hasInstanceFlag(clockwiseAsMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing),
            "clockwise mesh should flip Vulkan RT triangle facing to preserve clockwise front-face");

        {
            auto record = scene.tryGetMaterialAsset(doubleSidedMaterial);
            nr::test::require(record.has_value(), "AS contract material record should exist");
            auto& mutableRecord = const_cast<nr::scene::MaterialAssetRecord&>(record->get());
            mutableRecord.cpu.core.alphaMode = nr::resource::AlphaMode::opaque;
        }

        auto opaqueAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(opaqueAsMesh.has_value(), "material flag mutation should keep AS build input available");
        nr::test::require(
            hasGeometryFlag(opaqueAsMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "material opacity changes should update AS geometry flags and therefore BLAS build signatures");

        device.waitIdle();
    }};
} // namespace
