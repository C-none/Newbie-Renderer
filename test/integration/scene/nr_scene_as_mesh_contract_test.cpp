import std;
import dependency.vulkan;
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
    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "mask_single_sided",
        .alphaModeHint = nr::load::MaterialAlphaModeHint::mask,
        .alphaCutoff = 0.5f,
    });
    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "volume_boundary_single_sided",
        .transmissionFactor = 1.0f,
        .ior = 1.5f,
        .thicknessFactor = 1.0f,
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
            .indices = {0u, 1u, 2u, 1u, 3u, 4u, 0u, 2u, 4u},
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
                nr::load::MeshGeometryAsset{
                    .name = "mask_single_sided_geometry",
                    .firstIndex = 6u,
                    .indexCount = 3u,
                    .materialIndex = 2u,
                },
                nr::load::MeshGeometryAsset{
                    .name = "volume_boundary_single_sided_geometry",
                    .firstIndex = 0u,
                    .indexCount = 3u,
                    .materialIndex = 3u,
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
    scene.stats.indexCount = 18u;
    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeAtlasGrowSceneAsset()
{
    auto scene = makeAsMeshSceneAsset();
    scene.sourcePath = std::filesystem::path{"scene_atlas_grow_contract.gltf"};
    scene.materials.resize(1u);
    scene.materials.front().name = "atlas_grow_material";
    scene.meshes.resize(1u);

    auto &mesh = scene.meshes.front();
    mesh.name = "atlas_grow_mesh";
    mesh.vertices.resize(16384u);
    mesh.vertices[0].position = {-1.0f, -1.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, -1.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    mesh.indices = {0u, 1u, 2u};
    mesh.geometries = {
        nr::load::MeshGeometryAsset{
            .name = "atlas_grow_geometry",
            .indexCount = 3u,
            .materialIndex = 0u,
        },
    };
    mesh.clockwiseFrontFace = false;

    scene.nodes[1].meshIndices = {0u};
    scene.stats.meshCount = 1u;
    scene.stats.materialCount = 1u;
    scene.stats.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
    scene.stats.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
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

void uploadSceneGeometry(
    nr::scene::Scene& scene,
    nr::rhi::Device& device,
    nr::resource::MeshHandle meshHandle)
{
    scene.beginFrame(0u);
    scene.uploadPending();

    auto submittedMesh = scene.tryGetMeshAsset(meshHandle);
    nr::test::require(submittedMesh.has_value(), "submitted scene mesh should remain tracked");
    nr::test::requireEqual(
        submittedMesh->get().gpuState,
        nr::scene::GpuResidencyState::waitingGraphicsSync,
        "staged scene mesh must not become resident before the graphics acquire fence completes");
    nr::test::require(
        !scene.tryGetAccelerationStructureMesh(meshHandle).has_value(),
        "staged scene mesh must not be exposed as AS input before graphics acquire completion");

    device.waitIdle();
    scene.uploadPending();

    auto residentMesh = scene.tryGetMeshAsset(meshHandle);
    nr::test::require(residentMesh.has_value(), "completed scene mesh should remain tracked");
    nr::test::requireEqual(
        residentMesh->get().gpuState,
        nr::scene::GpuResidencyState::resident,
        "graphics acquire completion should promote the staged scene mesh to resident");
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
        auto alphaMaskMaterial = resolveMaterialHandle(scene, sceneAsset, 2u);
        auto volumeBoundaryMaterial = resolveMaterialHandle(scene, sceneAsset, 3u);
        uploadSceneGeometry(scene, device, meshHandle);

        auto importedClockwiseMesh = scene.tryGetMeshAsset(clockwiseMeshHandle);
        nr::test::require(importedClockwiseMesh.has_value(), "clockwise source mesh should import into scene mesh storage");
        nr::test::require(
            importedClockwiseMesh->get().cpu.clockwiseFrontFace,
            "mesh bridge should preserve source clockwise front-face winding");

        auto asMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(asMesh.has_value(), "resident scene mesh should expose AS build input");
        nr::test::requireEqual(asMesh->geometries.size(), std::size_t{4u});
        nr::test::require(
            !hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing),
            "default CCW mesh should keep Vulkan RT default facing");
        nr::test::require(
            hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "any double-sided geometry should disable facing cull for the whole TLAS instance");
        nr::test::require(
            !hasGeometryFlag(asMesh->geometries[0].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "mixed-instance single-sided geometry should stay non-opaque for any-hit back-face filtering");
        nr::test::require(
            hasGeometryFlag(asMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "alpha-blended material should still mark AS geometry opaque for RT traversal");
        nr::test::require(
            !hasGeometryFlag(asMesh->geometries[2].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "alpha-mask material should keep AS geometry non-opaque for any-hit alpha testing");
        nr::test::require(
            hasGeometryFlag(asMesh->geometries[3].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "volume-boundary transmission should remain opaque-like for RT traversal");
        nr::test::requireEqual(
            asMesh->semanticKey.geometries[0].geometryFlags,
            asMesh->geometries[0].geometryFlags,
            "AS semantic and concrete geometry flags must share one policy source");

        {
            auto record = scene.tryGetMaterialAsset(doubleSidedMaterial);
            nr::test::require(record.has_value(), "AS contract double-sided material record should exist");
            auto& mutableRecord = const_cast<nr::scene::MaterialAssetRecord&>(record->get());
            mutableRecord.cpu.core.doubleSided = false;
        }

        {
            auto record = scene.tryGetMeshAsset(meshHandle);
            nr::test::require(record.has_value(), "AS contract mesh record should exist");
            auto& mutableRecord = const_cast<nr::scene::MeshAssetRecord&>(record->get());
            mutableRecord.cpu.geometries[3].indexCount = 0u;

            auto zeroPrimitiveVolumeAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
            nr::test::require(
                zeroPrimitiveVolumeAsMesh.has_value(),
                "zero-primitive volume geometry should not hide live AS geometry");
            nr::test::requireEqual(zeroPrimitiveVolumeAsMesh->geometries.size(), std::size_t{3u});
            nr::test::require(
                !hasInstanceFlag(
                    zeroPrimitiveVolumeAsMesh->instanceFlags,
                    vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
                "zero-primitive volume geometry must not alter live instance culling");

            mutableRecord.cpu.geometries[3].indexCount = 3u;
        }

        auto volumeCullAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(volumeCullAsMesh.has_value(), "volume-boundary mesh should remain AS-visible");
        nr::test::require(
            hasInstanceFlag(volumeCullAsMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "a single-sided volume boundary should retain exit-face intersections for the whole TLAS instance");
        nr::test::require(
            !hasGeometryFlag(volumeCullAsMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "a now-single-sided sibling should route retained back faces through any-hit");

        {
            auto record = scene.tryGetMaterialAsset(volumeBoundaryMaterial);
            nr::test::require(record.has_value(), "AS contract volume-boundary material record should exist");
            auto& mutableRecord = const_cast<nr::scene::MaterialAssetRecord&>(record->get());
            nr::test::require(
                mutableRecord.cpu.transmission.has_value(),
                "AS contract volume-boundary material should retain its transmission extension");
            mutableRecord.cpu.transmission->factor = 0.0f;
        }

        auto baseOnlyCullAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(baseOnlyCullAsMesh.has_value(), "base-only mesh should remain AS-visible");
        nr::test::require(
            !hasInstanceFlag(baseOnlyCullAsMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "zero effective transmission should not retain back faces after the double-sided material is disabled");
        nr::test::require(
            hasGeometryFlag(baseOnlyCullAsMesh->geometries[0].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "ordinary single-sided geometry should recover the hardware-opaque fast path");

        auto clockwiseAsMesh = scene.tryGetAccelerationStructureMesh(clockwiseMeshHandle);
        nr::test::require(clockwiseAsMesh.has_value(), "clockwise mesh should still expose AS build input");
        nr::test::require(
            hasInstanceFlag(clockwiseAsMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing),
            "clockwise mesh should invert Vulkan RT default facing");

        {
            auto record = scene.tryGetMaterialAsset(alphaMaskMaterial);
            nr::test::require(record.has_value(), "AS contract material record should exist");
            auto& mutableRecord = const_cast<nr::scene::MaterialAssetRecord&>(record->get());
            mutableRecord.cpu.core.alphaMode = nr::resource::AlphaMode::opaque;
        }

        auto opaqueAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(opaqueAsMesh.has_value(), "material flag mutation should keep AS build input available");
        nr::test::require(
            hasGeometryFlag(opaqueAsMesh->geometries[2].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "material opacity changes should update AS geometry flags and therefore BLAS build signatures");

        device.waitIdle();
    }};

const nr::test::CaseRegistrar graphicsSyncLifetimeCase{
    "scene defers submitted asset collection and drains submitted graphics work on destruction",
    [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_scene_graphics_sync_lifetime_contract_test", "NewbieRenderer");

        {
            auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
            auto sceneAsset = makeAsMeshSceneAsset();
            auto templateHandle = scene.registerTemplate(sceneAsset);
            auto meshHandle = resolveMeshHandle(scene, sceneAsset);

            scene.beginFrame(0u);
            scene.uploadPending();
            auto submittedMesh = scene.tryGetMeshAsset(meshHandle);
            nr::test::require(submittedMesh.has_value(), "submitted mesh should remain tracked");
            nr::test::requireEqual(
                submittedMesh->get().gpuState,
                nr::scene::GpuResidencyState::waitingGraphicsSync,
                "submitted mesh should remain non-resident while its graphics acquire fence is pending");

            nr::test::requireEqual(
                scene.destroyTemplate(templateHandle),
                nr::scene::DestroyTemplateResult::destroyed,
                "uninstantiated template destruction should succeed while its upload is pending");
            nr::test::require(
                scene.tryGetMeshAsset(meshHandle).has_value(),
                "template destruction must defer collection of a mesh referenced by submitted graphics work");

            device.waitIdle();
            scene.uploadPending();
            nr::test::require(
                !scene.tryGetMeshAsset(meshHandle).has_value(),
                "graphics completion reaping should collect an unpinned deferred mesh");
        }

        {
            auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
            auto sceneAsset = makeAsMeshSceneAsset();
            auto templateHandle = scene.registerTemplate(sceneAsset);
            auto meshHandle = resolveMeshHandle(scene, sceneAsset);

            scene.beginFrame(0u);
            scene.uploadPending();
            auto submittedMesh = scene.tryGetMeshAsset(meshHandle);
            nr::test::require(submittedMesh.has_value(), "scene destruction case mesh should remain tracked");
            nr::test::requireEqual(
                submittedMesh->get().gpuState,
                nr::scene::GpuResidencyState::waitingGraphicsSync,
                "scene destruction case requires submitted graphics work");
            nr::test::require(templateHandle.valid(), "scene destruction case should retain its template");
        }
    }};

const nr::test::CaseRegistrar geometryAtlasGrowVisibilityCase{
    "scene hides resident geometry while an atlas prefix grow copy is pending",
    [] {
        auto device = nr::rhi::Device{};
        device.initialize("nr_scene_atlas_grow_visibility_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto initialAsset = makeAsMeshSceneAsset();
        auto initialTemplate = scene.registerTemplate(initialAsset);
        auto initialMesh = resolveMeshHandle(scene, initialAsset);
        nr::test::require(initialTemplate.valid(), "initial atlas template should register");
        uploadSceneGeometry(scene, device, initialMesh);
        nr::test::require(scene.tryGetRasterGeometryBuffers().has_value(), "completed initial atlas should be raster-visible");
        nr::test::require(scene.tryGetAccelerationStructureMeshSemanticKey(initialMesh).has_value(), "completed initial atlas should be AS-visible");

        auto growAsset = makeAtlasGrowSceneAsset();
        auto growTemplate = scene.registerTemplate(growAsset);
        nr::test::require(growTemplate.valid(), "atlas grow template should register");

        scene.beginFrame(1u);
        scene.uploadPending();
        nr::test::require(
            !scene.tryGetRasterGeometryBuffers().has_value(),
            "new atlas buffers must stay hidden while their prefix copy is pending");
        nr::test::require(
            !scene.tryGetAccelerationStructureMeshSemanticKey(initialMesh).has_value(),
            "resident meshes must stay hidden from AS consumers while the atlas prefix copy is pending");

        device.waitIdle();
        scene.uploadPending();
        nr::test::require(scene.tryGetRasterGeometryBuffers().has_value(), "completed atlas grow should restore raster visibility");
        nr::test::require(scene.tryGetAccelerationStructureMeshSemanticKey(initialMesh).has_value(), "completed atlas grow should restore AS visibility");
    }};
} // namespace
