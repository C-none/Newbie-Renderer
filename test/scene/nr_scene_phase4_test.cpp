import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(nr::scene::Scene &scene, std::uint32_t frameSlot) {
    scene.beginFrame(frameSlot);
    scene.uploadPending();
});

static_assert(requires(const nr::scene::Scene &scene) {
    { scene.currentFrameStamp() } -> std::same_as<nr::scene::SceneFrameStamp>;
});

static_assert(requires {
    nr::scene::GpuResidencyState::none;
    nr::scene::GpuResidencyState::uploadQueued;
    nr::scene::GpuResidencyState::waitingAcquire;
    nr::scene::GpuResidencyState::resident;
    nr::scene::GpuResidencyState::evictQueued;
});

static_assert(requires(const nr::scene::MeshAssetRecord &meshRecord,
                       const nr::scene::MaterialAssetRecord &materialRecord,
                       const nr::scene::TextureAssetRecord &textureRecord,
                       const nr::scene::CameraAssetRecord &cameraRecord,
                       const nr::scene::LightAssetRecord &lightRecord) {
    meshRecord.gpuVersion;
    meshRecord.gpuState;
    meshRecord.lastUploadFrameSerial;

    materialRecord.gpuVersion;
    materialRecord.gpuState;

    textureRecord.gpuVersion;
    textureRecord.gpuState;

    cameraRecord.gpuVersion;
    cameraRecord.gpuState;

    lightRecord.gpuVersion;
    lightRecord.gpuState;
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

[[nodiscard]] std::array<float, 16> identityTransform()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildPhase4SceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_phase4_scene.gltf"};

    auto image = nr::load::Image{};
    image.width = 1;
    image.height = 1;
    image.channels = 4;
    image.pixels = {255, 255, 255, 255};

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/phase4/baseColor";
    texture.resolvedPath = std::filesystem::path{"manual_textures/phase4_baseColor.png"};
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "phase4_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "phase4_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {0.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {1.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(4);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1, 2, 3};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = identityTransform();

    scene.nodes[2].name = "CameraNode";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].localTransform = identityTransform();

    scene.nodes[3].name = "LightNode";
    scene.nodes[3].parentIndex = 0;
    scene.nodes[3].localTransform = identityTransform();

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "Phase4Camera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .horizontalFov = glm::radians(80.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 400.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "Phase4Light",
        .sourceNodeName = "LightNode",
        .nodeIndex = 3,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 0.8f, 0.7f},
        .attenuationLinear = 0.1f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());

    return scene;
}

[[nodiscard]] bool checkStaticCompileContracts()
{
    std::println("\n=== Case: checkStaticCompileContracts ===");
    return true;
}

[[nodiscard]] bool checkFrameSerialAndUploadQueueTransitions()
{
    std::println("\n=== Case: checkFrameSerialAndUploadQueueTransitions ===");

    auto sceneAsset = buildPhase4SceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template handle should be valid for phase4 scene asset."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
    auto materialHandle = scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
    auto textureHandle = scene.findTextureHandleByStableKey(nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures[0]));
    auto cameraHandle = scene.findCameraHandleByStableKey(nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0));
    auto lightHandle = scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0));

    if (!require(meshHandle.has_value(), "Mesh handle should be resolvable by canonical key."))
    {
        return false;
    }
    if (!require(materialHandle.has_value(), "Material handle should be resolvable by canonical key."))
    {
        return false;
    }
    if (!require(textureHandle.has_value(), "Texture handle should be resolvable by canonical key."))
    {
        return false;
    }
    if (!require(cameraHandle.has_value(), "Camera handle should be resolvable by canonical key."))
    {
        return false;
    }
    if (!require(lightHandle.has_value(), "Light handle should be resolvable by canonical key."))
    {
        return false;
    }

    auto meshBefore = scene.tryGetMeshAsset(*meshHandle);
    auto materialBefore = scene.tryGetMaterialAsset(*materialHandle);
    auto textureBefore = scene.tryGetTextureAsset(*textureHandle);
    auto cameraBefore = scene.tryGetCameraAsset(*cameraHandle);
    auto lightBefore = scene.tryGetLightAsset(*lightHandle);

    if (!require(meshBefore.has_value(), "Mesh record should exist before beginFrame."))
    {
        return false;
    }
    if (!require(materialBefore.has_value(), "Material record should exist before beginFrame."))
    {
        return false;
    }
    if (!require(textureBefore.has_value(), "Texture record should exist before beginFrame."))
    {
        return false;
    }
    if (!require(cameraBefore.has_value(), "Camera record should exist before beginFrame."))
    {
        return false;
    }
    if (!require(lightBefore.has_value(), "Light record should exist before beginFrame."))
    {
        return false;
    }

    if (!require(meshBefore->get().gpuState == nr::scene::GpuResidencyState::none, "Mesh should start in GpuResidencyState::none."))
    {
        return false;
    }
    if (!require(!meshBefore->get().uploadQueued, "Mesh should not be uploadQueued before beginFrame."))
    {
        return false;
    }

    scene.beginFrame(5);

    auto firstStamp = scene.currentFrameStamp();
    if (!require(firstStamp.frameSlot == 5u, "Frame slot should track beginFrame input."))
    {
        return false;
    }
    if (!require(firstStamp.frameSerial == 1u, "Frame serial should start from 1 after first beginFrame."))
    {
        return false;
    }

    auto meshQueued = scene.tryGetMeshAsset(*meshHandle);
    auto materialQueued = scene.tryGetMaterialAsset(*materialHandle);
    auto textureQueued = scene.tryGetTextureAsset(*textureHandle);
    auto cameraQueued = scene.tryGetCameraAsset(*cameraHandle);
    auto lightQueued = scene.tryGetLightAsset(*lightHandle);

    if (!require(meshQueued->get().uploadQueued, "Mesh should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(materialQueued->get().uploadQueued, "Material should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(textureQueued->get().uploadQueued, "Texture should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(cameraQueued->get().uploadQueued, "Camera should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(lightQueued->get().uploadQueued, "Light should be uploadQueued after beginFrame."))
    {
        return false;
    }

    if (!require(meshQueued->get().gpuState == nr::scene::GpuResidencyState::uploadQueued, "Mesh should enter uploadQueued state after beginFrame."))
    {
        return false;
    }

    scene.uploadPending();

    auto meshAfterUploadAttempt = scene.tryGetMeshAsset(*meshHandle);
    if (!require(meshAfterUploadAttempt->get().uploadQueued, "Without initialized RHI context, queued upload should remain pending."))
    {
        return false;
    }

    scene.beginFrame(6);

    auto secondStamp = scene.currentFrameStamp();
    if (!require(secondStamp.frameSlot == 6u, "Frame slot should update on next beginFrame call."))
    {
        return false;
    }
    if (!require(secondStamp.frameSerial == 2u, "Frame serial should increment monotonically."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkStaticCompileContracts", &checkStaticCompileContracts},
        std::pair{"checkFrameSerialAndUploadQueueTransitions", &checkFrameSerialAndUploadQueueTransitions},
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
