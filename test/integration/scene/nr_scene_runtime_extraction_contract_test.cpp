import std;
import dependency;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-4f) noexcept
{
    return std::abs(lhs - rhs) <= epsilon;
}

[[nodiscard]] float projectionAspectRatio(const glm::mat4 &projection) noexcept
{
    if (std::abs(projection[0][0]) <= 1e-6f)
    {
        return 0.0f;
    }
    return projection[1][1] / projection[0][0];
}

[[nodiscard]] std::array<float, 16> identityTransform() noexcept
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset makeRuntimeSceneAsset(bool includeCamera)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = includeCamera ? std::filesystem::path{"runtime_camera_scene.gltf"}
                                     : std::filesystem::path{"runtime_readiness_scene.gltf"};

    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/runtime/base_color",
        .decodedImage = nr::load::Image{
            .width = 1,
            .height = 1,
            .channels = 4,
            .pixels = {255u, 255u, 255u, 255u},
        },
    });

    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "runtime_material",
        .textures = {
            nr::load::MaterialTextureBinding{
                .textureIndex = 0,
                .semantic = "diffuse",
            },
        },
    });

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "runtime_triangle",
        .vertices = {
            nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
            nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
            nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
        },
        .indices = {0u, 1u, 2u},
        .materialIndex = 0,
    });

    scene.nodes.resize(includeCamera ? 3u : 2u);
    scene.rootNodeIndex = 0;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = includeCamera ? std::vector<std::uint32_t>{1u, 2u}
                                                : std::vector<std::uint32_t>{1u};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0u};
    scene.nodes[1].localTransform = identityTransform();

    if (includeCamera)
    {
        scene.nodes[2].name = "CameraNode";
        scene.nodes[2].parentIndex = 0;
        scene.nodes[2].localTransform = identityTransform();

        scene.cameras.push_back(nr::load::CameraAsset{
            .name = "RuntimeCamera",
            .sourceNodeName = "CameraNode",
            .nodeIndex = 2,
            .lookAt = {0.0f, 0.0f, -1.0f},
            .up = {0.0f, 1.0f, 0.0f},
            .horizontalFov = glm::radians(90.0f),
            .aspect = 1.0f,
            .nearPlane = 0.1f,
            .farPlane = 500.0f,
        });
    }

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;
    return scene;
}

[[nodiscard]] nr::scene::SceneExtractProfileHandle registerProfile(nr::scene::Scene &scene,
                                                                   nr::scene::ScenePacketDomain domain,
                                                                   bool requireReadyForDomain)
{
    auto requiredSelection = domain == nr::scene::ScenePacketDomain::rasterDraw
                                 ? nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque)
                                 : nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain);

    return scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = std::format("runtime_profile_{}", static_cast<std::uint32_t>(domain)),
        .domain = domain,
        .selection = nr::scene::SceneSelectionMask{.requireAll = requiredSelection},
        .requireReadyForDomain = requireReadyForDomain,
    });
}

struct RuntimeHandles
{
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    nr::resource::TextureHandle texture{};
};

[[nodiscard]] RuntimeHandles resolveRuntimeHandles(const nr::scene::Scene &scene,
                                                   const nr::load::SceneAsset &sceneAsset)
{
    auto mesh = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
    auto material = scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
    auto texture = scene.findTextureHandleByStableKey(nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures[0]));

    nr::test::require(mesh.has_value(), "mesh handle should resolve by stable key");
    nr::test::require(material.has_value(), "material handle should resolve by stable key");
    nr::test::require(texture.has_value(), "texture handle should resolve by stable key");

    return RuntimeHandles{.mesh = *mesh, .material = *material, .texture = *texture};
}

void setMeshResidentForTest(nr::scene::Scene &scene,
                            nr::resource::MeshHandle handle,
                            bool resident)
{
    auto record = scene.tryGetMeshAsset(handle);
    nr::test::require(record.has_value(), "mesh record should exist");
    auto &mutableRecord = const_cast<nr::scene::MeshAssetRecord &>(record->get());
    mutableRecord.uploadQueued = false;
    mutableRecord.gpuState = resident ? nr::scene::GpuResidencyState::resident : nr::scene::GpuResidencyState::none;
    mutableRecord.gpuVersion = resident ? mutableRecord.cpuVersion : 0u;
}

void setMaterialResidentForTest(nr::scene::Scene &scene,
                                nr::resource::MaterialHandle handle,
                                bool resident)
{
    auto record = scene.tryGetMaterialAsset(handle);
    nr::test::require(record.has_value(), "material record should exist");
    auto &mutableRecord = const_cast<nr::scene::MaterialAssetRecord &>(record->get());
    mutableRecord.uploadQueued = false;
    mutableRecord.gpuState = resident ? nr::scene::GpuResidencyState::resident : nr::scene::GpuResidencyState::none;
    mutableRecord.gpuVersion = resident ? mutableRecord.cpuVersion : 0u;
}

void setTextureResidentForTest(nr::scene::Scene &scene,
                               nr::resource::TextureHandle handle,
                               bool resident)
{
    auto record = scene.tryGetTextureAsset(handle);
    nr::test::require(record.has_value(), "texture record should exist");
    auto &mutableRecord = const_cast<nr::scene::TextureAssetRecord &>(record->get());
    mutableRecord.uploadQueued = false;
    mutableRecord.gpuState = resident ? nr::scene::GpuResidencyState::resident : nr::scene::GpuResidencyState::none;
    mutableRecord.gpuVersion = resident ? mutableRecord.cpuVersion : 0u;
}

const nr::test::CaseRegistrar cameraAspectCase{
    "scene primary camera uses authored aspect and viewport overrides",
    [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRuntimeSceneAsset(true);

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(instanceHandle.valid(), "instance registration should succeed");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto authoredCamera = scene.tryGetPrimaryCamera();
        auto squareCamera = scene.tryGetPrimaryCamera(glm::uvec2{1024u, 1024u});
        auto wideCamera = scene.tryGetPrimaryCamera(glm::uvec2{1920u, 1080u});

        nr::test::require(authoredCamera.has_value(), "authored camera should resolve");
        nr::test::require(squareCamera.has_value(), "square viewport camera should resolve");
        nr::test::require(wideCamera.has_value(), "wide viewport camera should resolve");
        nr::test::require(almostEqual(projectionAspectRatio(authoredCamera->projection), 1.0f, 1e-3f),
                          "authored aspect should be used without viewport");
        nr::test::require(almostEqual(projectionAspectRatio(squareCamera->projection), 1.0f, 1e-3f),
                          "square viewport should produce 1:1 projection aspect");
        nr::test::require(almostEqual(projectionAspectRatio(wideCamera->projection), 16.0f / 9.0f, 1e-3f),
                          "wide viewport should produce 16:9 projection aspect");
    }};

const nr::test::CaseRegistrar activeInstanceCase{
    "scene extraction filters inactive instances",
    [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRuntimeSceneAsset(false);

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto activeInstance = scene.instantiate(templateHandle);
        auto inactiveInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{.activate = false});
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(activeInstance.valid(), "active instance should be valid");
        nr::test::require(inactiveInstance.valid(), "inactive instance should be valid");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, false);
        auto packets = scene.extractPackets(rasterProfile);

        nr::test::requireEqual(packets.domain, nr::scene::ScenePacketDomain::rasterDraw);
        nr::test::requireEqual(packets.rasterDraws.size(), std::size_t{1});

        scene.destroyInstance(activeInstance);
        auto afterDestroy = scene.extractPackets(rasterProfile);
        nr::test::require(afterDestroy.rasterDraws.empty(), "destroying active instance should remove raster packets");
    }};

const nr::test::CaseRegistrar readinessCase{
    "scene extraction applies domain-specific residency readiness",
    [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRuntimeSceneAsset(false);

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(instanceHandle.valid(), "instance registration should succeed");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto handles = resolveRuntimeHandles(scene, sceneAsset);
        auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, true);
        auto rtProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rayTracingInstance, true);
        auto tlasProfile = registerProfile(scene, nr::scene::ScenePacketDomain::tlasBuildInput, true);

        setMeshResidentForTest(scene, handles.mesh, false);
        setMaterialResidentForTest(scene, handles.material, false);
        setTextureResidentForTest(scene, handles.texture, false);
        nr::test::require(scene.extractPackets(rasterProfile).rasterDraws.empty(), "raster should wait for all dependencies");
        nr::test::require(scene.extractPackets(rtProfile).rtInstances.empty(), "RT should wait for mesh");
        nr::test::require(scene.extractPackets(tlasProfile).tlasBuildInputs.empty(), "TLAS should wait for mesh");

        setMeshResidentForTest(scene, handles.mesh, true);
        nr::test::require(scene.extractPackets(rasterProfile).rasterDraws.empty(), "raster should still wait for material and texture");
        nr::test::requireEqual(scene.extractPackets(rtProfile).rtInstances.size(), std::size_t{1});
        nr::test::requireEqual(scene.extractPackets(tlasProfile).tlasBuildInputs.size(), std::size_t{1});

        setMaterialResidentForTest(scene, handles.material, true);
        nr::test::require(scene.extractPackets(rasterProfile).rasterDraws.empty(), "raster should still wait for texture");

        setTextureResidentForTest(scene, handles.texture, true);
        nr::test::requireEqual(scene.extractPackets(rasterProfile).rasterDraws.size(), std::size_t{1});
    }};
} // namespace
