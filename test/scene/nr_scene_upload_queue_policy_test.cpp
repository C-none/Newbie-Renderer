import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(nr::scene::Scene &scene, std::uint32_t frameSlot) {
    scene.beginFrame(frameSlot);
    { scene.tryGetMeshAsset(nr::resource::MeshHandle{}) } -> std::same_as<std::optional<std::reference_wrapper<const nr::scene::MeshAssetRecord>>>;
    { scene.tryGetMaterialAsset(nr::resource::MaterialHandle{}) } -> std::same_as<std::optional<std::reference_wrapper<const nr::scene::MaterialAssetRecord>>>;
    { scene.tryGetTextureAsset(nr::resource::TextureHandle{}) } -> std::same_as<std::optional<std::reference_wrapper<const nr::scene::TextureAssetRecord>>>;
    { scene.tryGetCameraAsset(nr::resource::CameraAssetHandle{}) } -> std::same_as<std::optional<std::reference_wrapper<const nr::scene::CameraAssetRecord>>>;
    { scene.tryGetLightAsset(nr::resource::LightAssetHandle{}) } -> std::same_as<std::optional<std::reference_wrapper<const nr::scene::LightAssetRecord>>>;
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

[[nodiscard]] nr::load::SceneAsset buildUploadQueuePolicySceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_upload_queue_policy_scene.gltf"};

    auto image = nr::load::Image{};
    image.width = 1;
    image.height = 1;
    image.channels = 4;
    image.pixels = {255, 255, 255, 255};

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/upload_queue_policy/baseColor";
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "upload_queue_policy_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "upload_queue_policy_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
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
        .name = "UploadQueuePolicyCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .horizontalFov = glm::radians(75.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 500.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "UploadQueuePolicyLight",
        .sourceNodeName = "LightNode",
        .nodeIndex = 3,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 1.0f, 1.0f},
        .attenuationLinear = 0.1f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

struct UploadQueuePolicyHandles
{
    nr::resource::MeshHandle mesh{};
    nr::resource::MaterialHandle material{};
    nr::resource::TextureHandle texture{};
    nr::resource::CameraAssetHandle camera{};
    nr::resource::LightAssetHandle light{};
};

[[nodiscard]] std::optional<UploadQueuePolicyHandles> resolveHandles(
    const nr::scene::Scene &scene,
    const nr::load::SceneAsset &sceneAsset)
{
    auto mesh = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
    auto material = scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
    auto texture = scene.findTextureHandleByStableKey(nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures[0]));
    auto camera = scene.findCameraHandleByStableKey(nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0));
    auto light = scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0));

    if (!mesh.has_value() ||
        !material.has_value() ||
        !texture.has_value() ||
        !camera.has_value() ||
        !light.has_value())
    {
        return std::nullopt;
    }

    return UploadQueuePolicyHandles{
        .mesh = *mesh,
        .material = *material,
        .texture = *texture,
        .camera = *camera,
        .light = *light,
    };
}

template <typename RecordT>
[[nodiscard]] bool requireRecord(std::optional<std::reference_wrapper<const RecordT>> record,
                                 std::string_view label)
{
    return require(record.has_value(), std::format("Missing record for {}.", label));
}

[[nodiscard]] bool checkUnifiedUploadQueuePolicy()
{
    std::println("\n=== Case: checkUnifiedUploadQueuePolicy ===");

    auto sceneAsset = buildUploadQueuePolicySceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(templateHandle.valid() && instanceHandle.valid(),
                 "Template and instance should be valid for upload queue policy test."))
    {
        return false;
    }

    auto handles = resolveHandles(scene, sceneAsset);
    if (!require(handles.has_value(), "Should resolve all resource handles for upload queue policy test."))
    {
        return false;
    }

    auto meshRecord = scene.tryGetMeshAsset(handles->mesh);
    auto materialRecord = scene.tryGetMaterialAsset(handles->material);
    auto textureRecord = scene.tryGetTextureAsset(handles->texture);
    auto cameraRecord = scene.tryGetCameraAsset(handles->camera);
    auto lightRecord = scene.tryGetLightAsset(handles->light);

    if (!requireRecord(meshRecord, "mesh") ||
        !requireRecord(materialRecord, "material") ||
        !requireRecord(textureRecord, "texture") ||
        !requireRecord(cameraRecord, "camera") ||
        !requireRecord(lightRecord, "light"))
    {
        return false;
    }

    auto &mutableMesh = const_cast<nr::scene::MeshAssetRecord &>(meshRecord->get());
    auto &mutableMaterial = const_cast<nr::scene::MaterialAssetRecord &>(materialRecord->get());
    auto &mutableTexture = const_cast<nr::scene::TextureAssetRecord &>(textureRecord->get());
    auto &mutableCamera = const_cast<nr::scene::CameraAssetRecord &>(cameraRecord->get());
    auto &mutableLight = const_cast<nr::scene::LightAssetRecord &>(lightRecord->get());

    mutableMesh.uploadQueued = false;
    mutableMesh.gpuState = nr::scene::GpuResidencyState::none;
    mutableMesh.cpuReady = true;
    mutableMesh.cpuVersion = 2;
    mutableMesh.gpuVersion = 1;

    mutableMaterial.uploadQueued = false;
    mutableMaterial.gpuState = nr::scene::GpuResidencyState::none;
    mutableMaterial.cpuReady = true;
    mutableMaterial.cpuVersion = 2;
    mutableMaterial.gpuVersion = 1;

    mutableTexture.uploadQueued = false;
    mutableTexture.gpuState = nr::scene::GpuResidencyState::none;
    mutableTexture.cpuReady = true;
    mutableTexture.cpuVersion = 2;
    mutableTexture.gpuVersion = 1;

    mutableCamera.uploadQueued = false;
    mutableCamera.gpuState = nr::scene::GpuResidencyState::none;
    mutableCamera.cpuReady = true;
    mutableCamera.cpuVersion = 1;
    mutableCamera.gpuVersion = 1;
    mutableCamera.lastUploadFrameSerial = 0;

    mutableLight.uploadQueued = false;
    mutableLight.gpuState = nr::scene::GpuResidencyState::none;
    mutableLight.cpuReady = true;
    mutableLight.cpuVersion = 1;
    mutableLight.gpuVersion = 1;
    mutableLight.lastUploadFrameSerial = 0;

    scene.beginFrame(0);

    if (!require(mutableMesh.uploadQueued, "Mesh should be uploadQueued when cpuVersion > gpuVersion."))
    {
        return false;
    }
    if (!require(mutableMaterial.uploadQueued, "Material should be uploadQueued when cpuVersion > gpuVersion."))
    {
        return false;
    }
    if (!require(mutableTexture.uploadQueued, "Texture should be uploadQueued when cpuVersion > gpuVersion."))
    {
        return false;
    }
    if (!require(mutableCamera.uploadQueued,
                 "Camera should be uploadQueued when lastUploadFrameSerial != current frame serial."))
    {
        return false;
    }
    if (!require(mutableLight.uploadQueued,
                 "Light should be uploadQueued when lastUploadFrameSerial != current frame serial."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkWaitingAcquireBlocksQueueing()
{
    std::println("\n=== Case: checkWaitingAcquireBlocksQueueing ===");

    auto sceneAsset = buildUploadQueuePolicySceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(templateHandle.valid() && instanceHandle.valid(),
                 "Template and instance should be valid for waitingAcquire guard test."))
    {
        return false;
    }

    auto handles = resolveHandles(scene, sceneAsset);
    if (!require(handles.has_value(), "Should resolve all resource handles for waitingAcquire guard test."))
    {
        return false;
    }

    auto meshRecord = scene.tryGetMeshAsset(handles->mesh);
    auto materialRecord = scene.tryGetMaterialAsset(handles->material);
    auto textureRecord = scene.tryGetTextureAsset(handles->texture);
    auto cameraRecord = scene.tryGetCameraAsset(handles->camera);
    auto lightRecord = scene.tryGetLightAsset(handles->light);

    if (!requireRecord(meshRecord, "mesh") ||
        !requireRecord(materialRecord, "material") ||
        !requireRecord(textureRecord, "texture") ||
        !requireRecord(cameraRecord, "camera") ||
        !requireRecord(lightRecord, "light"))
    {
        return false;
    }

    auto &mutableMesh = const_cast<nr::scene::MeshAssetRecord &>(meshRecord->get());
    auto &mutableMaterial = const_cast<nr::scene::MaterialAssetRecord &>(materialRecord->get());
    auto &mutableTexture = const_cast<nr::scene::TextureAssetRecord &>(textureRecord->get());
    auto &mutableCamera = const_cast<nr::scene::CameraAssetRecord &>(cameraRecord->get());
    auto &mutableLight = const_cast<nr::scene::LightAssetRecord &>(lightRecord->get());

    auto setWaitingAcquire = [](auto &record) {
        record.uploadQueued = false;
        record.cpuReady = true;
        record.gpuState = nr::scene::GpuResidencyState::waitingAcquire;
        record.cpuVersion = 3;
        record.gpuVersion = 1;
    };

    setWaitingAcquire(mutableMesh);
    setWaitingAcquire(mutableMaterial);
    setWaitingAcquire(mutableTexture);
    setWaitingAcquire(mutableCamera);
    setWaitingAcquire(mutableLight);
    mutableCamera.lastUploadFrameSerial = 0;
    mutableLight.lastUploadFrameSerial = 0;

    scene.beginFrame(0);

    if (!require(!mutableMesh.uploadQueued, "Mesh should not be queued when gpuState is waitingAcquire."))
    {
        return false;
    }
    if (!require(!mutableMaterial.uploadQueued, "Material should not be queued when gpuState is waitingAcquire."))
    {
        return false;
    }
    if (!require(!mutableTexture.uploadQueued, "Texture should not be queued when gpuState is waitingAcquire."))
    {
        return false;
    }
    if (!require(!mutableCamera.uploadQueued, "Camera should not be queued when gpuState is waitingAcquire."))
    {
        return false;
    }
    if (!require(!mutableLight.uploadQueued, "Light should not be queued when gpuState is waitingAcquire."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkUnifiedUploadQueuePolicy", &checkUnifiedUploadQueuePolicy},
        std::pair{"checkWaitingAcquireBlocksQueueing", &checkWaitingAcquireBlocksQueueing},
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
