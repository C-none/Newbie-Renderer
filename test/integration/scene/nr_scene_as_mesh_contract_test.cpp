import std;
import dependency.math;
import dependency.vulkan;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::array<float, 16> identityTransform() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
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
            .vertices =
                {
                    nr::load::VertexAsset{.position = {-1.0f, -1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {0.0f, -1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {-1.0f, 1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {1.0f, -1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {1.0f, 1.0f, 0.0f}},
                },
            .indices = {0u, 1u, 2u, 1u, 3u, 4u, 0u, 2u, 4u},
            .geometries =
                {
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
    mesh.vertices.resize(32768u);
    mesh.vertices[0].position = {-1.0f, -1.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, -1.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    mesh.indices.resize(262146u);
    mesh.indices[0] = 0u;
    mesh.indices[1] = 1u;
    mesh.indices[2] = 2u;
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

[[nodiscard]] nr::load::SceneAsset makeAtlasAppendSceneAsset()
{
    auto scene = makeAsMeshSceneAsset();
    scene.sourcePath = std::filesystem::path{"scene_atlas_append_contract.gltf"};
    scene.meshes.resize(1u);
    scene.meshes.front().name = "atlas_append_mesh";
    scene.nodes[1].meshIndices = {0u};
    scene.stats.meshCount = 1u;
    scene.stats.vertexCount = static_cast<std::uint32_t>(scene.meshes.front().vertices.size());
    scene.stats.indexCount = static_cast<std::uint32_t>(scene.meshes.front().indices.size());
    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeAtlasReuseSceneAsset(std::string_view name, std::size_t vertexCount,
                                                            std::size_t indexCount)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::format("scene_atlas_reuse_{}.gltf", name)};
    scene.materials.push_back(nr::load::MaterialAsset{.name = std::format("atlas_reuse_{}_material", name)});

    auto mesh = nr::load::MeshAsset{
        .name = std::format("atlas_reuse_{}_mesh", name),
        .vertices = std::vector<nr::load::VertexAsset>(vertexCount),
        .indices = std::vector<std::uint32_t>(indexCount),
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = std::format("atlas_reuse_{}_geometry", name),
                    .indexCount = static_cast<std::uint32_t>(indexCount),
                    .materialIndex = 0u,
                },
            },
    };
    mesh.vertices[0].position = {-1.0f, -1.0f, 0.0f};
    mesh.vertices[1].position = {1.0f, -1.0f, 0.0f};
    mesh.vertices[2].position = {0.0f, 1.0f, 0.0f};
    auto indexPositions = std::views::iota(std::size_t{0}, mesh.indices.size());
    std::ranges::for_each(indexPositions, [&](std::size_t indexPosition) {
        mesh.indices[indexPosition] = static_cast<std::uint32_t>(indexPosition % 3u);
    });
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(2u);
    scene.rootNodeIndex = 0u;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = std::format("AtlasReuse{}", name);
    scene.nodes[1].parentIndex = 0u;
    scene.nodes[1].meshIndices = {0u};
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = 1u;
    scene.stats.materialCount = 1u;
    scene.stats.vertexCount = static_cast<std::uint32_t>(vertexCount);
    scene.stats.indexCount = static_cast<std::uint32_t>(indexCount);
    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeUploadBudgetSceneAsset()
{
    constexpr auto oversizedVertexCount = std::size_t{2048u};
    constexpr auto textureExtent = std::uint32_t{256u};

    auto makeMesh = [](std::string name, std::size_t vertexCount) {
        auto mesh = nr::load::MeshAsset{
            .name = std::move(name),
            .vertices =
                {
                    nr::load::VertexAsset{.position = {-1.0f, -1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {1.0f, -1.0f, 0.0f}},
                    nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
                },
            .indices = {0u, 1u, 2u},
            .geometries =
                {
                    nr::load::MeshGeometryAsset{
                        .name = "upload_budget_geometry",
                        .indexCount = 3u,
                        .materialIndex = 0u,
                    },
                },
        };
        mesh.vertices.resize(vertexCount);
        return mesh;
    };

    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"scene_upload_budget_contract.gltf"};
    scene.materials.push_back(nr::load::MaterialAsset{.name = "upload_budget_material"});
    scene.meshes.push_back(makeMesh("upload_budget_small_mesh", 3u));
    scene.meshes.push_back(makeMesh("upload_budget_oversized_mesh_0", oversizedVertexCount));
    scene.meshes.push_back(makeMesh("upload_budget_oversized_mesh_1", oversizedVertexCount));

    auto textureIndices = std::views::iota(std::uint32_t{0u}, std::uint32_t{2u});
    std::ranges::for_each(textureIndices, [&](std::uint32_t textureIndex) {
        scene.textures.push_back(nr::load::TextureAsset{
            .key = std::format("manual://textures/upload_budget/{}", textureIndex),
            .decodedImage =
                nr::load::Image{
                    .width = textureExtent,
                    .height = textureExtent,
                    .channels = 4u,
                    .pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(textureExtent) * textureExtent * 4u,
                                                        static_cast<std::uint8_t>(64u + textureIndex)),
                },
        });
    });

    scene.nodes.resize(2u);
    scene.rootNodeIndex = 0u;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = "UploadBudgetMeshes";
    scene.nodes[1].parentIndex = 0u;
    scene.nodes[1].meshIndices = {0u, 1u, 2u};
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.vertexCount = static_cast<std::uint32_t>(3u + oversizedVertexCount * 2u);
    scene.stats.indexCount = 9u;
    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeResidencyReadinessSceneAsset(bool anisotropy)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = anisotropy ? std::filesystem::path{"scene_anisotropy_readiness_contract.gltf"}
                                  : std::filesystem::path{"scene_residency_readiness_contract.gltf"};
    scene.textures.push_back(nr::load::TextureAsset{
        .key = anisotropy ? "manual://textures/readiness/anisotropy" : "manual://textures/readiness/base_color",
        .decodedImage =
            nr::load::Image{
                .width = 1u,
                .height = 1u,
                .channels = 4u,
                .pixels = {255u, 255u, 255u, 255u},
            },
    });

    auto material = nr::load::MaterialAsset{
        .name = anisotropy ? "readiness_anisotropy_material" : "readiness_base_color_material",
        .textures =
            {
                nr::load::MaterialTextureBinding{
                    .textureIndex = 0u,
                    .semantic = anisotropy ? nr::resource::MaterialTextureSlotSemantic::anisotropy
                                           : nr::resource::MaterialTextureSlotSemantic::baseColor,
                    .sourceSemanticName = anisotropy ? "anisotropy" : "base_color",
                },
            },
    };
    if (anisotropy)
    {
        material.anisotropyFactor = 0.75f;
    }
    scene.materials.push_back(std::move(material));

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "readiness_triangle",
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
                    .name = "readiness_triangle_geometry",
                    .indexCount = 3u,
                    .materialIndex = 0u,
                },
            },
    });

    scene.nodes.resize(3u);
    scene.rootNodeIndex = 0u;
    scene.nodes[0].name = "Root";
    scene.nodes[0].childIndices = {1u, 2u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = "ReadinessMesh";
    scene.nodes[1].parentIndex = 0u;
    scene.nodes[1].meshIndices = {0u};
    scene.nodes[1].localTransform = identityTransform();
    scene.nodes[2].name = "ReadinessCamera";
    scene.nodes[2].parentIndex = 0u;
    scene.nodes[2].localTransform = identityTransform();
    scene.nodes[2].localTransform[11] = 3.0f;

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "ReadinessCamera",
        .sourceNodeName = "ReadinessCamera",
        .nodeIndex = 2u,
        .lookAt = {0.0f, 0.0f, -1.0f},
        .up = {0.0f, 1.0f, 0.0f},
        .horizontalFov = nr::math::radians(60.0f),
        .aspect = 1.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
    });
    scene.lights.push_back(nr::load::LightAsset{
        .name = "ReadinessLight",
        .sourceNodeName = "ReadinessMesh",
        .nodeIndex = 1u,
        .type = "point",
        .colorDiffuse = {1.0f, 1.0f, 1.0f},
        .range = 10.0f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = 1u;
    scene.stats.materialCount = 1u;
    scene.stats.textureCount = 1u;
    scene.stats.cameraCount = 1u;
    scene.stats.lightCount = 1u;
    scene.stats.vertexCount = 3u;
    scene.stats.indexCount = 3u;
    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeRasterBridgeResolutionSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"scene_raster_bridge_resolution_contract.gltf"};
    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/raster_bridge/normal",
        .decodedImage =
            nr::load::Image{
                .width = 1u,
                .height = 1u,
                .channels = 4u,
                .pixels = {128u, 128u, 255u, 255u},
            },
    });

    auto normalBinding = nr::load::MaterialTextureBinding{
        .textureIndex = 0u,
        .uvChannel = 1u,
        .transform =
            nr::resource::MaterialTextureTransform{
                .linear = {2.0f, 0.25f, -0.5f, 3.0f},
                .offset = {0.125f, -0.25f},
            },
        .semantic = nr::resource::MaterialTextureSlotSemantic::normal,
        .sourceSemanticName = "normal",
    };
    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "raster_bridge_single_sided",
        .normalScale = 0.75f,
        .textures = {normalBinding},
    });
    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "raster_bridge_double_sided",
        .doubleSided = true,
        .normalScale = 0.5f,
        .textures = {normalBinding},
    });

    auto vertices = std::vector<nr::load::VertexAsset>{
        nr::load::VertexAsset{.position = {-1.0f, -1.0f, 0.0f}}, nr::load::VertexAsset{.position = {0.0f, -1.0f, 0.0f}},
        nr::load::VertexAsset{.position = {-1.0f, 0.0f, 0.0f}},  nr::load::VertexAsset{.position = {0.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {1.0f, 0.0f, 0.0f}},   nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
    };
    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "raster_bridge_indexed",
        .vertices = vertices,
        .indices = {0u, 1u, 2u, 0u, 1u, 2u},
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = "indexed_offset_geometry",
                    .firstIndex = 3u,
                    .indexCount = 3u,
                    .vertexOffset = 1u,
                    .materialIndex = 0u,
                },
            },
    });
    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "raster_bridge_nonindexed",
        .vertices = std::move(vertices),
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = "nonindexed_offset_geometry",
                    .firstIndex = 3u,
                    .indexCount = 3u,
                    .materialIndex = 1u,
                },
            },
        .clockwiseFrontFace = true,
    });

    scene.nodes.resize(2u);
    scene.rootNodeIndex = 0u;
    scene.nodes[0].name = "Root";
    scene.nodes[0].childIndices = {1u};
    scene.nodes[0].localTransform = identityTransform();
    scene.nodes[1].name = "RasterBridgeMeshes";
    scene.nodes[1].parentIndex = 0u;
    scene.nodes[1].meshIndices = {0u, 1u};
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.vertexCount = 12u;
    scene.stats.indexCount = 6u;
    return scene;
}

[[nodiscard]] bool hasInstanceFlag(vk::GeometryInstanceFlagsKHR flags, vk::GeometryInstanceFlagBitsKHR flag) noexcept
{
    return static_cast<bool>(flags & flag);
}

[[nodiscard]] bool hasGeometryFlag(vk::GeometryFlagsKHR flags, vk::GeometryFlagBitsKHR flag) noexcept
{
    return static_cast<bool>(flags & flag);
}

[[nodiscard]] nr::resource::MeshHandle resolveMeshHandle(const nr::scene::Scene &scene,
                                                         const nr::load::SceneAsset &sceneAsset,
                                                         std::uint32_t meshIndex = 0u)
{
    auto handle = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, meshIndex));
    nr::test::require(handle.has_value(), "AS contract mesh handle should resolve by stable key");
    return *handle;
}

[[nodiscard]] nr::resource::MaterialHandle resolveMaterialHandle(const nr::scene::Scene &scene,
                                                                 const nr::load::SceneAsset &sceneAsset,
                                                                 std::uint32_t materialIndex)
{
    auto handle = scene.findMaterialHandleByStableKey(
        nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, materialIndex));
    nr::test::require(handle.has_value(), "AS contract material handle should resolve by stable key");
    return *handle;
}

[[nodiscard]] nr::resource::TextureHandle resolveTextureHandle(const nr::scene::Scene &scene,
                                                               const nr::load::SceneAsset &sceneAsset,
                                                               std::uint32_t textureIndex)
{
    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);
    nr::test::require(plan.valid() && textureIndex < plan.textures.size(),
                      "upload budget texture bridge input should resolve");
    auto handle = scene.findTextureHandleByStableKey(plan.textures[textureIndex].canonicalKey);
    nr::test::require(handle.has_value(), "upload budget texture handle should resolve by stable key");
    return *handle;
}

[[nodiscard]] nr::scene::SceneExtractProfileHandle registerReadinessProfile(nr::scene::Scene &scene,
                                                                            nr::scene::ScenePacketDomain domain)
{
    auto requiredSelection = domain == nr::scene::ScenePacketDomain::rasterDraw
                                 ? nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque)
                                 : nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain);
    return scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = std::format("gpu_readiness_profile_{}", static_cast<std::uint32_t>(domain)),
        .domain = domain,
        .selection = nr::scene::SceneSelectionMask{.requireAll = requiredSelection},
        .requireReadyForDomain = true,
    });
}

void completeNextResidencyFrame(nr::scene::Scene &scene, nr::rhi::Device &device, std::uint32_t frameSlot)
{
    scene.beginFrame(frameSlot);
    scene.uploadPending();
    device.waitIdle();
    scene.uploadPending();
}

void uploadSceneGeometry(nr::scene::Scene &scene, nr::rhi::Device &device, nr::resource::MeshHandle meshHandle)
{
    scene.beginFrame(0u);
    scene.uploadPending();

    auto submittedMesh = scene.tryGetMeshAsset(meshHandle);
    nr::test::require(submittedMesh.has_value(), "submitted scene mesh should remain tracked");
    nr::test::requireEqual(submittedMesh->get().gpuState, nr::scene::GpuResidencyState::waitingGraphicsSync,
                           "staged scene mesh must not become resident before the graphics acquire fence completes");
    nr::test::require(!scene.tryGetAccelerationStructureMesh(meshHandle).has_value(),
                      "staged scene mesh must not be exposed as AS input before graphics acquire completion");

    device.waitIdle();
    scene.uploadPending();

    auto residentMesh = scene.tryGetMeshAsset(meshHandle);
    nr::test::require(residentMesh.has_value(), "completed scene mesh should remain tracked");
    nr::test::requireEqual(residentMesh->get().gpuState, nr::scene::GpuResidencyState::resident,
                           "graphics acquire completion should promote the staged scene mesh to resident");
}

// These readiness contracts live in the GPU scene target so residency changes come from the real
// beginFrame/uploadPending/fence-reap lifecycle instead of mutating Scene asset records from a CPU-only test.
const nr::test::CaseRegistrar readinessCase{
    "scene extraction applies domain-specific residency readiness", [] {
        auto device = nr::rhi::Device::create("nr_scene_residency_readiness_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{
            .device = device,
            .uploadBudgetBytesPerFrame = 1u,
        });
        auto sceneAsset = makeResidencyReadinessSceneAsset(false);
        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "readiness template registration should succeed");
        nr::test::require(instanceHandle.valid(), "readiness instance registration should succeed");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto mesh = resolveMeshHandle(scene, sceneAsset);
        auto material = resolveMaterialHandle(scene, sceneAsset, 0u);
        auto texture = resolveTextureHandle(scene, sceneAsset, 0u);
        auto materialRecord = scene.tryGetMaterialAsset(material);
        nr::test::require(materialRecord.has_value() && materialRecord->get().cpuReady,
                          "readiness material should be CPU-ready before any GPU upload frame");
        auto camera = scene.findCameraHandleByStableKey(
            nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0u));
        auto light =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0u));
        nr::test::require(camera.has_value() && light.has_value(),
                          "readiness camera and light should register as CPU-only scene assets");
        auto cameraRecord = scene.tryGetCameraAsset(*camera);
        auto lightRecord = scene.tryGetLightAsset(*light);
        nr::test::require(cameraRecord.has_value() && cameraRecord->get().cpuReady,
                          "readiness camera CPU data should be ready before any GPU upload frame");
        nr::test::require(lightRecord.has_value() && lightRecord->get().cpuReady,
                          "readiness light CPU data should be ready before any GPU upload frame");
        auto rasterProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::rasterDraw);
        auto rtProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::rayTracingInstance);
        auto tlasProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::tlasBuildInput);
        using RevisionDomain = nr::scene::SceneRtRevisionDomain;
        using RevisionMask = nr::revision::RevisionMask<RevisionDomain>;

        auto requireGpuState = [&]<typename HandleT>(HandleT handle, nr::scene::GpuResidencyState expected,
                                                     std::string_view message) {
            auto actual = nr::scene::GpuResidencyState::none;
            if constexpr (std::same_as<HandleT, nr::resource::MeshHandle>)
            {
                auto record = scene.tryGetMeshAsset(handle);
                nr::test::require(record.has_value(), "readiness mesh record should remain registered");
                actual = record->get().gpuState;
            }
            else if constexpr (std::same_as<HandleT, nr::resource::TextureHandle>)
            {
                auto record = scene.tryGetTextureAsset(handle);
                nr::test::require(record.has_value(), "readiness texture record should remain registered");
                actual = record->get().gpuState;
            }
            else
            {
                static_assert(std::same_as<HandleT, void>, "Readiness GPU state is defined only for meshes/textures.");
            }
            nr::test::requireEqual(actual, expected, message);
        };
        auto requireNoReadyPackets = [&](std::string_view message) {
            nr::test::require(scene.extractPackets(rasterProfile).rasterDraws.empty(), message);
            nr::test::require(scene.extractPackets(rtProfile).rtInstances.empty(), message);
            nr::test::require(scene.extractPackets(tlasProfile).tlasBuildInputs.empty(), message);
        };

        auto const beforeUploads = scene.revisionsSnapshot();
        requireNoReadyPackets("no domain should extract before real GPU residency promotion");

        completeNextResidencyFrame(scene, device, 0u);
        auto const afterMesh = scene.revisionsSnapshot();
        requireGpuState(mesh, nr::scene::GpuResidencyState::resident,
                        "the first one-byte budget frame should promote the mesh");
        requireGpuState(texture, nr::scene::GpuResidencyState::uploadQueued,
                        "the texture should remain queued after mesh consumes the frame budget");
        requireNoReadyPackets("base-color packet domains must still wait for their texture");
        nr::test::requireEqual(nr::revision::diff(beforeUploads.rt, afterMesh.rt),
                               RevisionMask::of<RevisionDomain::topology, RevisionDomain::meshContent>(),
                               "real mesh promotion must advance only topology and mesh content revisions");

        completeNextResidencyFrame(scene, device, 1u);
        auto const afterTexture = scene.revisionsSnapshot();
        requireGpuState(texture, nr::scene::GpuResidencyState::resident,
                        "the second one-byte budget frame should promote the texture");
        nr::test::requireEqual(scene.extractPackets(rasterProfile).rasterDraws.size(), std::size_t{1u});
        nr::test::requireEqual(scene.extractPackets(rtProfile).rtInstances.size(), std::size_t{1u});
        nr::test::requireEqual(scene.extractPackets(tlasProfile).tlasBuildInputs.size(), std::size_t{1u});
        nr::test::requireEqual(nr::revision::diff(afterMesh.rt, afterTexture.rt),
                               RevisionMask::of<RevisionDomain::topology, RevisionDomain::textureResidency>(),
                               "real texture promotion must advance only topology and texture residency revisions");

        auto &mutableMeshRecord =
            const_cast<nr::scene::MeshAssetRecord &>(scene.tryGetMeshAsset(mesh)->get());
        auto &mutableTextureRecord =
            const_cast<nr::scene::TextureAssetRecord &>(scene.tryGetTextureAsset(texture)->get());
        ++mutableMeshRecord.cpuVersion;
        ++mutableTextureRecord.cpuVersion;

        completeNextResidencyFrame(scene, device, 2u);
        requireGpuState(mesh, nr::scene::GpuResidencyState::resident,
                        "a resident mesh with a newer CPU version should complete a replacement upload");
        requireGpuState(texture, nr::scene::GpuResidencyState::uploadQueued,
                        "mesh-first budget ordering should leave the versioned texture queued");
        nr::test::requireEqual(scene.tryGetMeshAsset(mesh)->get().gpuVersion,
                               scene.tryGetMeshAsset(mesh)->get().cpuVersion,
                               "mesh replacement completion should catch GPU version up to CPU version");

        completeNextResidencyFrame(scene, device, 0u);
        requireGpuState(texture, nr::scene::GpuResidencyState::resident,
                        "a resident texture with a newer CPU version should complete a replacement upload");
        nr::test::requireEqual(scene.tryGetTextureAsset(texture)->get().gpuVersion,
                               scene.tryGetTextureAsset(texture)->get().cpuVersion,
                               "texture replacement completion should catch GPU version up to CPU version");
    }};

const nr::test::CaseRegistrar anisotropyReadinessCase{
    "scene extraction keeps raster strict while RT accepts unavailable anisotropy", [] {
        auto device = nr::rhi::Device::create("nr_scene_anisotropy_readiness_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{
            .device = device,
            .uploadBudgetBytesPerFrame = 1u,
        });
        auto sceneAsset = makeResidencyReadinessSceneAsset(true);
        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "anisotropy readiness template should register");
        nr::test::require(instanceHandle.valid(), "anisotropy readiness instance should register");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto mesh = resolveMeshHandle(scene, sceneAsset);
        auto material = resolveMaterialHandle(scene, sceneAsset, 0u);
        auto texture = resolveTextureHandle(scene, sceneAsset, 0u);
        auto rasterProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::rasterDraw);
        auto rtProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::rayTracingInstance);
        auto tlasProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::tlasBuildInput);

        completeNextResidencyFrame(scene, device, 0u);

        auto meshRecord = scene.tryGetMeshAsset(mesh);
        auto materialRecord = scene.tryGetMaterialAsset(material);
        auto textureRecord = scene.tryGetTextureAsset(texture);
        nr::test::require(meshRecord.has_value() && materialRecord.has_value() && textureRecord.has_value(),
                          "anisotropy readiness records should remain registered");
        nr::test::requireEqual(meshRecord->get().gpuState, nr::scene::GpuResidencyState::resident);
        nr::test::require(materialRecord->get().cpuReady && materialRecord->get().cpuVersion > 0u,
                          "anisotropy material should remain CPU-ready with a semantic version");
        nr::test::requireEqual(textureRecord->get().gpuState, nr::scene::GpuResidencyState::uploadQueued);
        nr::test::require(scene.extractPackets(rasterProfile).rasterDraws.empty(),
                          "raster must retain strict residency for an authored anisotropy texture");
        nr::test::requireEqual(scene.extractPackets(rtProfile).rtInstances.size(), std::size_t{1u},
                               "RT must use the anisotropy semantic fallback while its texture is unavailable");
        nr::test::requireEqual(scene.extractPackets(tlasProfile).tlasBuildInputs.size(), std::size_t{1u},
                               "TLAS must not suppress a packet for unavailable anisotropy alone");
        nr::test::require(!scene.tryGetSampledTextureBinding(texture).has_value(),
                          "queued anisotropy must not expose a sampled binding");

        auto const unavailableRevisions = scene.revisionsSnapshot();
        completeNextResidencyFrame(scene, device, 1u);
        auto const residentRevisions = scene.revisionsSnapshot();

        nr::test::requireEqual(scene.extractPackets(rasterProfile).rasterDraws.size(), std::size_t{1u},
                               "raster should become ready when anisotropy becomes resident");
        nr::test::requireEqual(scene.extractPackets(rtProfile).rtInstances.size(), std::size_t{1u});
        nr::test::requireEqual(scene.extractPackets(tlasProfile).tlasBuildInputs.size(), std::size_t{1u});
        nr::test::require(scene.tryGetSampledTextureBinding(texture).has_value(),
                          "resident anisotropy should expose its real sampled binding");
        nr::test::require(residentRevisions.rt.get<nr::scene::SceneRtRevisionDomain::textureResidency>() !=
                              unavailableRevisions.rt.get<nr::scene::SceneRtRevisionDomain::textureResidency>(),
                          "anisotropy residency promotion must invalidate RT material/texture collection");
    }};

const nr::test::CaseRegistrar rasterBridgeResolutionCase{
    "ready raster extraction resolves atlas ranges, material state, and texture handles", [] {
        auto device = nr::rhi::Device::create("nr_scene_raster_bridge_resolution_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRasterBridgeResolutionSceneAsset();
        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid() && instanceHandle.valid(),
                          "raster bridge resolution scene should register and instantiate");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto indexedMesh = resolveMeshHandle(scene, sceneAsset, 0u);
        auto nonindexedMesh = resolveMeshHandle(scene, sceneAsset, 1u);
        auto normalTexture = resolveTextureHandle(scene, sceneAsset, 0u);
        uploadSceneGeometry(scene, device, indexedMesh);

        auto rasterProfile = registerReadinessProfile(scene, nr::scene::ScenePacketDomain::rasterDraw);
        auto packets = scene.extractPackets(rasterProfile);
        nr::test::requireEqual(packets.rasterDraws.size(), std::size_t{2u});
        nr::test::require(packets.rasterGeometryBuffers.valid(),
                          "ready raster extraction should publish the shared vertex atlas");
        nr::test::require(packets.rasterGeometryBuffers.hasIndexBuffer(),
                          "an indexed raster packet should publish the shared index atlas");

        auto const textureId = nr::scene::sceneTextureId(normalTexture);
        auto textureEntry = packets.rasterTextureHandlesById.find(textureId);
        nr::test::require(textureEntry != packets.rasterTextureHandlesById.end() &&
                              textureEntry->second == normalTexture,
                          "raster extraction should publish the normal texture id-to-handle mapping once");
        nr::test::requireEqual(packets.rasterTextureHandlesById.size(), std::size_t{1u},
                               "materials sharing a texture should share one raster texture table entry");

        auto findDraw = [&](nr::resource::MeshHandle mesh) -> const nr::scene::RasterDrawPacket & {
            auto draw = std::ranges::find(packets.rasterDraws, mesh, &nr::scene::RasterDrawPacket::mesh);
            nr::test::require(draw != packets.rasterDraws.end(), "expected raster draw should resolve by mesh handle");
            return *draw;
        };
        auto const &indexedDraw = findDraw(indexedMesh);
        auto const &nonindexedDraw = findDraw(nonindexedMesh);
        auto indexedRecord = scene.tryGetMeshAsset(indexedMesh);
        auto nonindexedRecord = scene.tryGetMeshAsset(nonindexedMesh);
        nr::test::require(indexedRecord.has_value() && indexedRecord->get().gpu.has_value() &&
                              nonindexedRecord.has_value() && nonindexedRecord->get().gpu.has_value(),
                          "resolved raster meshes should retain their atlas allocations");

        auto indexedAsMesh = scene.tryGetAccelerationStructureMesh(indexedMesh);
        auto nonindexedAsMesh = scene.tryGetAccelerationStructureMesh(nonindexedMesh);
        nr::test::require(indexedAsMesh.has_value() && indexedAsMesh->geometries.size() == 1u &&
                              nonindexedAsMesh.has_value() && nonindexedAsMesh->geometries.size() == 1u,
                          "resident offset meshes should expose one AS geometry each");
        nr::test::require(indexedAsMesh->hasIndexBuffer() && !nonindexedAsMesh->hasIndexBuffer(),
                          "Scene AS geometry should preserve each mesh's indexed mode");
        nr::test::require(indexedAsMesh->geometries.front().indexed &&
                              !nonindexedAsMesh->geometries.front().indexed,
                          "Scene AS geometry records should preserve indexed and non-indexed modes");
        nr::test::requireEqual(indexedAsMesh->geometries.front().primitiveOffset,
                               vk::DeviceSize{3u * sizeof(std::uint32_t)});
        nr::test::requireEqual(indexedAsMesh->geometries.front().firstVertex, 1u);
        nr::test::requireEqual(indexedAsMesh->geometries.front().primitiveCount, 1u);
        nr::test::requireEqual(nonindexedAsMesh->geometries.front().primitiveOffset,
                               vk::DeviceSize{3u * sizeof(nr::resource::Vertex)});
        nr::test::requireEqual(nonindexedAsMesh->geometries.front().firstVertex, 0u);
        nr::test::requireEqual(nonindexedAsMesh->geometries.front().primitiveCount, 1u);

        nr::test::require(indexedDraw.geometry.indexed(), "indexed mesh should resolve an indexed draw range");
        nr::test::requireEqual(indexedDraw.geometry.firstIndex, indexedRecord->get().gpu->atlas.indexBase + 3u);
        nr::test::requireEqual(indexedDraw.geometry.indexCount, 3u);
        nr::test::requireEqual(indexedDraw.geometry.vertexOffset,
                               static_cast<std::int32_t>(indexedRecord->get().gpu->atlas.vertexBase + 1u));
        nr::test::requireEqual(indexedDraw.geometry.frontFace, vk::FrontFace::eCounterClockwise);
        nr::test::require(!nonindexedDraw.geometry.indexed(),
                          "non-indexed mesh should resolve a direct vertex draw range");
        nr::test::requireEqual(nonindexedDraw.geometry.firstVertex, nonindexedRecord->get().gpu->atlas.vertexBase + 3u);
        nr::test::requireEqual(nonindexedDraw.geometry.vertexCount, 3u);
        nr::test::requireEqual(nonindexedDraw.geometry.frontFace, vk::FrontFace::eClockwise);
        nr::test::requireEqual(indexedDraw.materialRaster.cullMode, vk::CullModeFlags{vk::CullModeFlagBits::eBack});
        nr::test::requireEqual(nonindexedDraw.materialRaster.cullMode, vk::CullModeFlags{vk::CullModeFlagBits::eNone});

        auto const normalSlot =
            nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::normal);
        nr::test::requireEqual(indexedDraw.materialTextures.ids[normalSlot],
                               static_cast<nr::scene::SceneTextureId>(textureId));
        nr::test::requireEqual(indexedDraw.materialTextures.normal.textureId,
                               static_cast<nr::scene::SceneTextureId>(textureId));
        nr::test::requireEqual(indexedDraw.materialTextures.normal.uvSet, 1u);
        nr::test::require(indexedDraw.materialTextures.normal.uvLinear.x == 2.0f &&
                              indexedDraw.materialTextures.normal.uvLinear.y == 0.25f &&
                              indexedDraw.materialTextures.normal.uvLinear.z == -0.5f &&
                              indexedDraw.materialTextures.normal.uvLinear.w == 3.0f,
                          "normal texture UV linear transform should be preserved");
        nr::test::require(indexedDraw.materialTextures.normal.uvOffset.x == 0.125f &&
                              indexedDraw.materialTextures.normal.uvOffset.y == -0.25f,
                          "normal texture UV offset should be preserved");
        nr::test::requireEqual(indexedDraw.materialTextures.normal.normalScale, 0.75f);
        nr::test::requireEqual(nonindexedDraw.materialTextures.normal.normalScale, 0.5f);

        nr::test::require(nr::scene::rasterDrawPacketResolved(indexedDraw, packets.rasterGeometryBuffers,
                                                              packets.rasterTextureHandlesById) &&
                              nr::scene::rasterDrawPacketResolved(nonindexedDraw, packets.rasterGeometryBuffers,
                                                                  packets.rasterTextureHandlesById),
                          "ready Scene raster packets should satisfy the pure bridge resolution invariant");
        auto frame = nr::scene::SceneRenderBridge::buildFrame(nr::scene::SceneRenderBridgeBuildInput{
            .packetSet = std::cref(packets),
        });
        nr::test::requireEqual(frame.rasterDraws.size(), std::size_t{2u});
        nr::test::require(std::addressof(frame.geometryBuffers.vertexBuffer.buffer->get()) ==
                                  std::addressof(packets.rasterGeometryBuffers.vertexBuffer.buffer->get()) &&
                              std::addressof(frame.geometryBuffers.indexBuffer.buffer->get()) ==
                                  std::addressof(packets.rasterGeometryBuffers.indexBuffer.buffer->get()),
                          "bridge frame should preserve the one shared Scene geometry atlas binding");
    }};

const nr::test::CaseRegistrar asMeshFlagsCase{
    "scene AS mesh query derives instance and geometry flags from mesh and material state", [] {
        auto device = nr::rhi::Device::create("nr_scene_as_mesh_contract_test", "NewbieRenderer");

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
        nr::test::require(importedClockwiseMesh.has_value(),
                          "clockwise source mesh should import into scene mesh storage");
        nr::test::require(importedClockwiseMesh->get().cpu.clockwiseFrontFace,
                          "mesh bridge should preserve source clockwise front-face winding");

        auto asMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(asMesh.has_value(), "resident scene mesh should expose AS build input");
        nr::test::requireEqual(asMesh->geometries.size(), std::size_t{4u});
        nr::test::require(!hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFlipFacing),
                          "default CCW mesh should keep Vulkan RT default facing");
        nr::test::require(
            hasInstanceFlag(asMesh->instanceFlags, vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "any double-sided geometry should disable facing cull for the whole TLAS instance");
        nr::test::require(
            !hasGeometryFlag(asMesh->geometries[0].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "mixed-instance single-sided geometry should stay non-opaque for any-hit back-face filtering");
        nr::test::require(hasGeometryFlag(asMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
                          "alpha-blended material should still mark AS geometry opaque for RT traversal");
        nr::test::require(!hasGeometryFlag(asMesh->geometries[2].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
                          "alpha-mask material should keep AS geometry non-opaque for any-hit alpha testing");
        nr::test::require(hasGeometryFlag(asMesh->geometries[3].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
                          "volume-boundary transmission should remain opaque-like for RT traversal");
        nr::test::requireEqual(asMesh->semanticKey.geometries[0].geometryFlags, asMesh->geometries[0].geometryFlags,
                               "AS semantic and concrete geometry flags must share one policy source");

        {
            auto record = scene.tryGetMaterialAsset(doubleSidedMaterial);
            nr::test::require(record.has_value(), "AS contract double-sided material record should exist");
            auto &mutableRecord = const_cast<nr::scene::MaterialAssetRecord &>(record->get());
            mutableRecord.cpu.core.doubleSided = false;
        }

        {
            auto record = scene.tryGetMeshAsset(meshHandle);
            nr::test::require(record.has_value(), "AS contract mesh record should exist");
            auto &mutableRecord = const_cast<nr::scene::MeshAssetRecord &>(record->get());
            mutableRecord.cpu.geometries[3].indexCount = 0u;

            auto zeroPrimitiveVolumeAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
            nr::test::require(zeroPrimitiveVolumeAsMesh.has_value(),
                              "zero-primitive volume geometry should not hide live AS geometry");
            nr::test::requireEqual(zeroPrimitiveVolumeAsMesh->geometries.size(), std::size_t{3u});
            nr::test::require(!hasInstanceFlag(zeroPrimitiveVolumeAsMesh->instanceFlags,
                                               vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
                              "zero-primitive volume geometry must not alter live instance culling");

            mutableRecord.cpu.geometries[3].indexCount = 3u;
        }

        auto volumeCullAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(volumeCullAsMesh.has_value(), "volume-boundary mesh should remain AS-visible");
        nr::test::require(
            hasInstanceFlag(volumeCullAsMesh->instanceFlags,
                            vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
            "a single-sided volume boundary should retain exit-face intersections for the whole TLAS instance");
        nr::test::require(
            !hasGeometryFlag(volumeCullAsMesh->geometries[1].geometryFlags, vk::GeometryFlagBitsKHR::eOpaque),
            "a now-single-sided sibling should route retained back faces through any-hit");

        {
            auto record = scene.tryGetMaterialAsset(volumeBoundaryMaterial);
            nr::test::require(record.has_value(), "AS contract volume-boundary material record should exist");
            auto &mutableRecord = const_cast<nr::scene::MaterialAssetRecord &>(record->get());
            nr::test::require(mutableRecord.cpu.transmission.has_value(),
                              "AS contract volume-boundary material should retain its transmission extension");
            mutableRecord.cpu.transmission->factor = 0.0f;
        }

        auto baseOnlyCullAsMesh = scene.tryGetAccelerationStructureMesh(meshHandle);
        nr::test::require(baseOnlyCullAsMesh.has_value(), "base-only mesh should remain AS-visible");
        nr::test::require(
            !hasInstanceFlag(baseOnlyCullAsMesh->instanceFlags,
                             vk::GeometryInstanceFlagBitsKHR::eTriangleFacingCullDisable),
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
            auto &mutableRecord = const_cast<nr::scene::MaterialAssetRecord &>(record->get());
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
    "scene defers submitted asset collection and drains submitted graphics work on destruction", [] {
        auto device = nr::rhi::Device::create("nr_scene_graphics_sync_lifetime_contract_test", "NewbieRenderer");

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
                submittedMesh->get().gpuState, nr::scene::GpuResidencyState::waitingGraphicsSync,
                "submitted mesh should remain non-resident while its graphics acquire fence is pending");

            nr::test::requireEqual(scene.destroyTemplate(templateHandle), nr::scene::DestroyTemplateResult::destroyed,
                                   "uninstantiated template destruction should succeed while its upload is pending");
            nr::test::require(
                scene.tryGetMeshAsset(meshHandle).has_value(),
                "template destruction must defer collection of a mesh referenced by submitted graphics work");

            device.waitIdle();
            scene.uploadPending();
            nr::test::require(!scene.tryGetMeshAsset(meshHandle).has_value(),
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
            nr::test::requireEqual(submittedMesh->get().gpuState, nr::scene::GpuResidencyState::waitingGraphicsSync,
                                   "scene destruction case requires submitted graphics work");
            nr::test::require(templateHandle.valid(), "scene destruction case should retain its template");
        }
    }};

const nr::test::CaseRegistrar uploadBudgetForwardProgressCase{
    "scene upload budget preserves normal ordering and advances one oversized asset per frame", [] {
        constexpr auto uploadBudget = std::size_t{64u * 1024u};

        auto device = nr::rhi::Device::create("nr_scene_upload_budget_forward_progress_contract_test",
                                              "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{
            .device = device,
            .uploadBudgetBytesPerFrame = uploadBudget,
        });
        auto sceneAsset = makeUploadBudgetSceneAsset();

        auto const smallMeshBytes = sceneAsset.meshes[0].vertices.size() * sizeof(nr::resource::Vertex) +
                                    sceneAsset.meshes[0].indices.size() * sizeof(std::uint32_t);
        auto const oversizedMeshBytes = sceneAsset.meshes[1].vertices.size() * sizeof(nr::resource::Vertex) +
                                        sceneAsset.meshes[1].indices.size() * sizeof(std::uint32_t);
        nr::test::require(smallMeshBytes <= uploadBudget,
                          "fixture small mesh should fit in the normal frame budget");
        nr::test::require(oversizedMeshBytes > uploadBudget,
                          "fixture oversized meshes should exceed the configured frame budget");
        nr::test::require(sceneAsset.textures[0].decodedImage->pixels.size() > uploadBudget,
                          "fixture textures should exceed the configured frame budget");

        auto templateHandle = scene.registerTemplate(sceneAsset);
        nr::test::require(templateHandle.valid(), "upload budget template should register");

        auto smallMesh = resolveMeshHandle(scene, sceneAsset, 0u);
        auto oversizedMesh0 = resolveMeshHandle(scene, sceneAsset, 1u);
        auto oversizedMesh1 = resolveMeshHandle(scene, sceneAsset, 2u);
        auto oversizedTexture0 = resolveTextureHandle(scene, sceneAsset, 0u);
        auto oversizedTexture1 = resolveTextureHandle(scene, sceneAsset, 1u);

        auto meshState = [&](nr::resource::MeshHandle handle) {
            auto record = scene.tryGetMeshAsset(handle);
            nr::test::require(record.has_value(), "upload budget mesh should remain tracked");
            return record->get().gpuState;
        };
        auto textureState = [&](nr::resource::TextureHandle handle) {
            auto record = scene.tryGetTextureAsset(handle);
            nr::test::require(record.has_value(), "upload budget texture should remain tracked");
            return record->get().gpuState;
        };
        auto reapSameFrame = [&] {
            device.waitIdle();
            scene.uploadPending();
        };

        scene.beginFrame(0u);
        scene.uploadPending();
        nr::test::requireEqual(meshState(smallMesh), nr::scene::GpuResidencyState::waitingGraphicsSync,
                               "small mesh should consume normal budget first");
        nr::test::requireEqual(meshState(oversizedMesh0), nr::scene::GpuResidencyState::uploadQueued,
                               "normal uploads should defer the first oversized mesh to the next frame");
        nr::test::requireEqual(textureState(oversizedTexture0), nr::scene::GpuResidencyState::uploadQueued,
                               "normal uploads should leave oversized textures queued");

        reapSameFrame();
        nr::test::requireEqual(meshState(smallMesh), nr::scene::GpuResidencyState::resident);
        nr::test::requireEqual(meshState(oversizedMesh0), nr::scene::GpuResidencyState::uploadQueued,
                               "same-frame fence reaping must not reset the upload budget");

        scene.beginFrame(1u);
        scene.uploadPending();
        nr::test::requireEqual(meshState(oversizedMesh0), nr::scene::GpuResidencyState::waitingGraphicsSync,
                               "first queued oversized mesh should make forward progress in an empty budget frame");
        nr::test::requireEqual(meshState(oversizedMesh1), nr::scene::GpuResidencyState::uploadQueued,
                               "only one oversized mesh may consume a frame");
        nr::test::requireEqual(textureState(oversizedTexture0), nr::scene::GpuResidencyState::uploadQueued,
                               "a mesh oversize should defer later texture oversizes");

        reapSameFrame();
        nr::test::requireEqual(meshState(oversizedMesh0), nr::scene::GpuResidencyState::resident);
        nr::test::requireEqual(meshState(oversizedMesh1), nr::scene::GpuResidencyState::uploadQueued,
                               "same-frame completion must not admit the second oversized mesh");

        scene.beginFrame(2u);
        scene.uploadPending();
        nr::test::requireEqual(meshState(oversizedMesh1), nr::scene::GpuResidencyState::waitingGraphicsSync,
                               "second oversized mesh should advance on the next frame");
        nr::test::requireEqual(textureState(oversizedTexture0), nr::scene::GpuResidencyState::uploadQueued,
                               "mesh category order should keep textures queued for this frame");
        reapSameFrame();
        nr::test::requireEqual(meshState(oversizedMesh1), nr::scene::GpuResidencyState::resident);

        scene.beginFrame(0u);
        scene.uploadPending();
        nr::test::requireEqual(textureState(oversizedTexture0), nr::scene::GpuResidencyState::waitingGraphicsSync,
                               "first oversized texture should make forward progress once it leads the frame");
        nr::test::requireEqual(textureState(oversizedTexture1), nr::scene::GpuResidencyState::uploadQueued,
                               "only one oversized texture may consume a frame");
        reapSameFrame();
        nr::test::requireEqual(textureState(oversizedTexture0), nr::scene::GpuResidencyState::resident);
        nr::test::requireEqual(textureState(oversizedTexture1), nr::scene::GpuResidencyState::uploadQueued,
                               "same-frame completion must not admit the second oversized texture");

        scene.beginFrame(1u);
        scene.uploadPending();
        nr::test::requireEqual(textureState(oversizedTexture1), nr::scene::GpuResidencyState::waitingGraphicsSync,
                               "second oversized texture should advance on the next frame");
        reapSameFrame();
        nr::test::requireEqual(textureState(oversizedTexture1), nr::scene::GpuResidencyState::resident);
    }};

const nr::test::CaseRegistrar geometryAtlasRetiredSliceReuseCase{
    "scene geometry atlas reuses retired indexed slices only after their safe serial", [] {
        constexpr auto aVertexCount = std::size_t{8u};
        constexpr auto aIndexCount = std::size_t{12u};
        constexpr auto cVertexCount = std::size_t{3u};
        constexpr auto cIndexCount = std::size_t{3u};
        constexpr auto combinedVertexCount = aVertexCount * 2u;
        constexpr auto combinedIndexCount = aIndexCount * 2u;
        constexpr auto vertexStride = vk::DeviceSize{sizeof(nr::resource::Vertex)};
        constexpr auto indexStride = vk::DeviceSize{sizeof(std::uint32_t)};

        auto device = nr::rhi::Device::create("nr_scene_geometry_atlas_retired_slice_reuse_contract_test",
                                              "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto completeCurrentFrameUploads = [&] {
            scene.uploadPending();
            device.waitIdle();
            scene.uploadPending();
        };
        auto residentAllocation = [&](nr::resource::MeshHandle handle) {
            auto record = scene.tryGetMeshAsset(handle);
            nr::test::require(record.has_value() && record->get().gpu.has_value(),
                              "atlas reuse mesh should retain a GPU allocation");
            nr::test::requireEqual(record->get().gpuState, nr::scene::GpuResidencyState::resident,
                                   "atlas reuse mesh should be resident before inspecting its allocation");
            return record->get().gpu->atlas;
        };

        auto assetA = makeAtlasReuseSceneAsset("a", aVertexCount, aIndexCount);
        auto templateA = scene.registerTemplate(assetA);
        auto meshA = resolveMeshHandle(scene, assetA);
        nr::test::require(templateA.valid(), "atlas reuse template A should register");
        scene.beginFrame(0u);
        completeCurrentFrameUploads();

        auto const allocationA = residentAllocation(meshA);
        auto const statsAfterA = scene.geometryAtlasStats();
        auto const generationAfterA = scene.geometryAtlasBackingGeneration();
        auto const aVertexBytes = static_cast<vk::DeviceSize>(aVertexCount) * vertexStride;
        auto const aIndexBytes = static_cast<vk::DeviceSize>(aIndexCount) * indexStride;
        nr::test::requireEqual(allocationA.vertexByteOffset, vk::DeviceSize{0u});
        nr::test::requireEqual(allocationA.indexByteOffset, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterA.vertex.highWaterBytes, aVertexBytes);
        nr::test::requireEqual(statsAfterA.index.highWaterBytes, aIndexBytes);
        nr::test::requireEqual(statsAfterA.vertex.reusableBytes, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterA.index.reusableBytes, vk::DeviceSize{0u});

        nr::test::requireEqual(scene.destroyTemplate(templateA), nr::scene::DestroyTemplateResult::destroyed,
                               "destroying template A should move its mesh allocation to the graveyard");
        nr::test::requireEqual(scene.geometryAtlasStats(), statsAfterA,
                               "destroying A must not expose its slice before the retirement serial");

        auto assetB = makeAtlasReuseSceneAsset("b", aVertexCount, aIndexCount);
        auto templateB = scene.registerTemplate(assetB);
        auto meshB = resolveMeshHandle(scene, assetB);
        nr::test::require(templateB.valid(), "atlas reuse template B should register");
        scene.beginFrame(1u);
        completeCurrentFrameUploads();

        auto const allocationB = residentAllocation(meshB);
        auto const statsAfterB = scene.geometryAtlasStats();
        nr::test::requireEqual(allocationB.vertexByteOffset, aVertexBytes,
                               "B must append instead of reusing A's unsafe vertex slice");
        nr::test::requireEqual(allocationB.indexByteOffset, aIndexBytes,
                               "B must append instead of reusing A's unsafe index slice");
        nr::test::requireEqual(statsAfterB.vertex.highWaterBytes, aVertexBytes * 2u);
        nr::test::requireEqual(statsAfterB.index.highWaterBytes, aIndexBytes * 2u);
        nr::test::requireEqual(statsAfterB.vertex.reusableBytes, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterB.index.reusableBytes, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterB.vertex.capacityBytes, statsAfterA.vertex.capacityBytes);
        nr::test::requireEqual(statsAfterB.index.capacityBytes, statsAfterA.index.capacityBytes);
        nr::test::requireEqual(scene.geometryAtlasBackingGeneration(), generationAfterA,
                               "unsafe-retirement append should fit without replacing atlas backings");

        scene.beginFrame(2u);
        nr::test::requireEqual(scene.geometryAtlasStats(), statsAfterB,
                               "A must remain unavailable one serial before its safe retirement point");

        auto assetC = makeAtlasReuseSceneAsset("c", cVertexCount, cIndexCount);
        auto templateC = scene.registerTemplate(assetC);
        auto meshC = resolveMeshHandle(scene, assetC);
        nr::test::require(templateC.valid(), "atlas reuse template C should register");
        scene.beginFrame(0u);

        auto const statsBeforeC = scene.geometryAtlasStats();
        nr::test::requireEqual(statsBeforeC.vertex.reusableBytes, aVertexBytes,
                               "A's vertex slice should become reusable exactly at its safe serial");
        nr::test::requireEqual(statsBeforeC.index.reusableBytes, aIndexBytes,
                               "A's index slice should become reusable exactly at its safe serial");
        completeCurrentFrameUploads();

        auto const allocationC = residentAllocation(meshC);
        auto const statsAfterC = scene.geometryAtlasStats();
        nr::test::requireEqual(allocationC.vertexByteOffset, allocationA.vertexByteOffset,
                               "C should first-fit A's retired vertex slice");
        nr::test::requireEqual(allocationC.indexByteOffset, allocationA.indexByteOffset,
                               "C should first-fit A's retired index slice");
        nr::test::requireEqual(allocationC.vertexByteOffset % vertexStride, vk::DeviceSize{0u});
        nr::test::requireEqual(allocationC.indexByteOffset % indexStride, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterC.vertex.reusableBytes,
                               aVertexBytes - static_cast<vk::DeviceSize>(cVertexCount) * vertexStride,
                               "smaller C vertex allocation should leave the aligned suffix reusable");
        nr::test::requireEqual(statsAfterC.index.reusableBytes,
                               aIndexBytes - static_cast<vk::DeviceSize>(cIndexCount) * indexStride,
                               "smaller C index allocation should leave the aligned suffix reusable");
        nr::test::requireEqual(statsAfterC.vertex.highWaterBytes, statsAfterB.vertex.highWaterBytes);
        nr::test::requireEqual(statsAfterC.index.highWaterBytes, statsAfterB.index.highWaterBytes);
        nr::test::requireEqual(statsAfterC.vertex.capacityBytes, statsAfterB.vertex.capacityBytes);
        nr::test::requireEqual(statsAfterC.index.capacityBytes, statsAfterB.index.capacityBytes);
        nr::test::requireEqual(scene.geometryAtlasBackingGeneration(), generationAfterA,
                               "safe slice reuse must not replace either atlas backing");

        nr::test::requireEqual(scene.destroyTemplate(templateC), nr::scene::DestroyTemplateResult::destroyed,
                               "template C destruction should retire its reused slice");
        nr::test::requireEqual(scene.destroyTemplate(templateB), nr::scene::DestroyTemplateResult::destroyed,
                               "template B should remain live until after C proves first-fit reuse");
        scene.beginFrame(1u);
        scene.beginFrame(2u);

        auto assetD = makeAtlasReuseSceneAsset("d", combinedVertexCount, combinedIndexCount);
        auto templateD = scene.registerTemplate(assetD);
        auto meshD = resolveMeshHandle(scene, assetD);
        nr::test::require(templateD.valid(), "atlas coalescing proof template D should register");
        scene.beginFrame(0u);

        auto const statsBeforeD = scene.geometryAtlasStats();
        nr::test::requireEqual(statsBeforeD.vertex.reusableBytes, statsAfterB.vertex.highWaterBytes,
                               "adjacent retired vertex slices should coalesce across the full high-water prefix");
        nr::test::requireEqual(statsBeforeD.index.reusableBytes, statsAfterB.index.highWaterBytes,
                               "adjacent retired index slices should coalesce across the full high-water prefix");
        completeCurrentFrameUploads();

        auto const allocationD = residentAllocation(meshD);
        auto const statsAfterD = scene.geometryAtlasStats();
        nr::test::requireEqual(allocationD.vertexByteOffset, vk::DeviceSize{0u},
                               "D should require the coalesced vertex range at offset zero");
        nr::test::requireEqual(allocationD.indexByteOffset, vk::DeviceSize{0u},
                               "D should require the coalesced index range at offset zero");
        nr::test::requireEqual(statsAfterD.vertex.reusableBytes, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterD.index.reusableBytes, vk::DeviceSize{0u});
        nr::test::requireEqual(statsAfterD.vertex.highWaterBytes, statsAfterB.vertex.highWaterBytes);
        nr::test::requireEqual(statsAfterD.index.highWaterBytes, statsAfterB.index.highWaterBytes);
        nr::test::requireEqual(statsAfterD.vertex.capacityBytes, statsAfterB.vertex.capacityBytes);
        nr::test::requireEqual(statsAfterD.index.capacityBytes, statsAfterB.index.capacityBytes);
        nr::test::requireEqual(scene.geometryAtlasBackingGeneration(), generationAfterA,
                               "coalesced atlas reuse must preserve backing generations");
    }};

const nr::test::CaseRegistrar geometryAtlasGrowVisibilityCase{
    "scene atlas replacement hides pending geometry and invalidates cached AS addresses by generation", [] {
        auto device = nr::rhi::Device::create("nr_scene_atlas_grow_visibility_contract_test", "NewbieRenderer");

        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto initialAsset = makeAsMeshSceneAsset();
        auto initialTemplate = scene.registerTemplate(initialAsset);
        auto initialMesh = resolveMeshHandle(scene, initialAsset);
        nr::test::require(initialTemplate.valid(), "initial atlas template should register");
        uploadSceneGeometry(scene, device, initialMesh);
        nr::test::require(scene.tryGetRasterGeometryBuffers().has_value(),
                          "completed initial atlas should be raster-visible");

        auto initialAsMesh = scene.tryGetAccelerationStructureMesh(initialMesh);
        nr::test::require(initialAsMesh.has_value(), "completed initial atlas should be AS-visible");
        nr::test::require(initialAsMesh->hasVertexBuffer() && initialAsMesh->hasIndexBuffer(),
                          "indexed AS input should expose both geometry atlas backings");

        auto const initialGeneration = scene.geometryAtlasBackingGeneration();
        nr::test::require(initialGeneration.vertex > 0u && initialGeneration.index > 0u,
                          "initial vertex and index atlas creation should establish nonzero generations");
        nr::test::requireEqual(initialAsMesh->semanticKey.atlasBackingGeneration, initialGeneration,
                               "indexed AS keys should capture both current atlas backing generations");

        auto const initialVertexAddress =
            initialAsMesh->vertexBuffer.buffer->get().deviceAddress() + initialAsMesh->vertexByteOffset;
        auto const initialIndexAddress =
            initialAsMesh->indexBuffer.buffer->get().deviceAddress() + initialAsMesh->indexByteOffset;
        auto const cachedSemanticKey = initialAsMesh->semanticKey;

        auto initialRecord = scene.tryGetMeshAsset(initialMesh);
        nr::test::require(initialRecord.has_value() && initialRecord->get().gpu.has_value(),
                          "initial AS mesh should retain its atlas allocation");
        nr::test::requireEqual(initialRecord->get().gpu->atlas.backingGenerationAtAllocation, initialGeneration,
                               "atlas allocation results should capture the backing generation used for upload");

        auto appendAsset = makeAtlasAppendSceneAsset();
        auto appendTemplate = scene.registerTemplate(appendAsset);
        auto appendMesh = resolveMeshHandle(scene, appendAsset);
        nr::test::require(appendTemplate.valid(), "atlas append template should register");

        scene.beginFrame(1u);
        scene.uploadPending();
        nr::test::requireEqual(scene.geometryAtlasBackingGeneration(), initialGeneration,
                               "ordinary atlas append must not advance backing generations");

        device.waitIdle();
        scene.uploadPending();
        nr::test::require(scene.tryGetAccelerationStructureMesh(appendMesh).has_value(),
                          "completed capacity-preserving append should become AS-visible");
        nr::test::requireEqual(scene.geometryAtlasBackingGeneration(), initialGeneration,
                               "append completion must retain the existing atlas generations");

        auto growAsset = makeAtlasGrowSceneAsset();
        auto growTemplate = scene.registerTemplate(growAsset);
        auto growMesh = resolveMeshHandle(scene, growAsset);
        nr::test::require(growTemplate.valid(), "atlas grow template should register");

        scene.beginFrame(2u);
        scene.uploadPending();
        auto const replacementGeneration = scene.geometryAtlasBackingGeneration();
        nr::test::require(replacementGeneration.vertex > initialGeneration.vertex,
                          "vertex atlas replacement should advance only after creating the new backing");
        nr::test::require(replacementGeneration.index > initialGeneration.index,
                          "index atlas replacement should advance only after creating the new backing");
        nr::test::require(!scene.tryGetRasterGeometryBuffers().has_value(),
                          "new atlas buffers must stay hidden while their prefix copy is pending");
        nr::test::require(!scene.tryGetAccelerationStructureMeshSemanticKey(initialMesh).has_value(),
                          "resident meshes must stay hidden from AS consumers while the atlas prefix copy is pending");

        device.waitIdle();
        scene.uploadPending();
        auto rasterBuffers = scene.tryGetRasterGeometryBuffers();
        nr::test::require(rasterBuffers.has_value(), "completed atlas grow should restore raster visibility");

        auto replacedAsMesh = scene.tryGetAccelerationStructureMesh(initialMesh);
        nr::test::require(replacedAsMesh.has_value(), "completed atlas grow should restore AS visibility");
        nr::test::requireEqual(replacedAsMesh->gpuVersion, initialAsMesh->gpuVersion,
                               "atlas replacement should not masquerade as a mesh content upload");
        nr::test::requireEqual(replacedAsMesh->semanticKey.geometries, cachedSemanticKey.geometries,
                               "atlas replacement should preserve unchanged geometry semantics");
        nr::test::requireEqual(replacedAsMesh->semanticKey.atlasBackingGeneration, replacementGeneration,
                               "refreshed AS keys should identify the replacement vertex and index backings");
        nr::test::require(replacedAsMesh->semanticKey != cachedSemanticKey,
                          "cached address-derived BLAS records must be invalidated by backing generation");

        auto const replacementVertexAddress =
            replacedAsMesh->vertexBuffer.buffer->get().deviceAddress() + replacedAsMesh->vertexByteOffset;
        auto const replacementIndexAddress =
            replacedAsMesh->indexBuffer.buffer->get().deviceAddress() + replacedAsMesh->indexByteOffset;
        nr::test::require(replacementVertexAddress != initialVertexAddress,
                          "refreshed AS records must derive the vertex address from the replacement backing");
        nr::test::require(replacementIndexAddress != initialIndexAddress,
                          "refreshed AS records must derive the index address from the replacement backing");
        nr::test::requireEqual(replacedAsMesh->vertexBuffer.buffer->get().deviceAddress(),
                               rasterBuffers->vertexBuffer.buffer->get().deviceAddress(),
                               "raster and AS queries should resolve the same replacement vertex backing");
        nr::test::requireEqual(replacedAsMesh->indexBuffer.buffer->get().deviceAddress(),
                               rasterBuffers->indexBuffer.buffer->get().deviceAddress(),
                               "raster and AS queries should resolve the same replacement index backing");

        auto replacedInitialRecord = scene.tryGetMeshAsset(initialMesh);
        nr::test::require(replacedInitialRecord.has_value() && replacedInitialRecord->get().gpu.has_value(),
                          "original mesh allocation should survive the prefix-preserving replacement");
        nr::test::requireEqual(
            replacedInitialRecord->get().gpu->atlas.backingGenerationAtAllocation, initialGeneration,
            "existing allocation snapshots should remain historical while AS keys use current backing");

        auto growRecord = scene.tryGetMeshAsset(growMesh);
        nr::test::require(growRecord.has_value() && growRecord->get().gpu.has_value(),
                          "growth mesh should retain its replacement-atlas allocation");
        nr::test::requireEqual(growRecord->get().gpu->atlas.backingGenerationAtAllocation, replacementGeneration,
                               "new allocation should record the replacement backing generations");
    }};
} // namespace
