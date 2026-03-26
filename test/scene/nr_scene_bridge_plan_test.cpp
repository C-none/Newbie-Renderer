import std;
import dependency;
import nr.load;
import nr.scene;

namespace
{
static_assert(requires(const nr::load::SceneAsset &sceneAsset) {
    { nr::scene::SceneBridge::buildPlan(sceneAsset) } -> std::same_as<nr::scene::SceneBridgePlan>;
    { nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0) } -> std::same_as<std::string>;
    { nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0) } -> std::same_as<std::string>;
    { nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0) } -> std::same_as<std::string>;
    { nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0) } -> std::same_as<std::string>;
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

[[nodiscard]] nr::load::SceneAsset buildBridgePlanSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_scene.gltf"};

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/bridge_plan/baseColor";
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "bridge_plan_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "bridge_plan_mesh";
    mesh.materialIndex = 0;
    scene.meshes.push_back(std::move(mesh));

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "BridgePlanCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 0,
        .horizontalFov = glm::radians(60.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "BridgePlanLight",
        .sourceNodeName = "LightNode",
        .nodeIndex = 0,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 1.0f, 1.0f},
        .attenuationLinear = 0.1f,
    });

    return scene;
}

[[nodiscard]] nr::load::SceneAsset buildBridgePlanMultiAssetSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_multi_scene.gltf"};

    auto textureIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{3});
    std::ranges::for_each(textureIndices, [&](std::uint32_t textureIndex) {
        auto texture = nr::load::TextureAsset{};
        texture.key = std::format("manual://textures/bridge_plan/multi_{}", textureIndex);
        scene.textures.push_back(std::move(texture));
    });

    auto materialIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(materialIndices, [&](std::uint32_t materialIndex) {
        auto material = nr::load::MaterialAsset{};
        material.name = std::format("bridge_plan_material_{}", materialIndex);
        scene.materials.push_back(std::move(material));
    });

    auto meshIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(meshIndices, [&](std::uint32_t meshIndex) {
        auto mesh = nr::load::MeshAsset{};
        mesh.name = std::format("bridge_plan_mesh_{}", meshIndex);
        mesh.materialIndex = meshIndex % static_cast<std::uint32_t>(scene.materials.size());
        scene.meshes.push_back(std::move(mesh));
    });

    auto cameraIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(cameraIndices, [&](std::uint32_t cameraIndex) {
        scene.cameras.push_back(nr::load::CameraAsset{
            .name = std::format("BridgePlanCamera{}", cameraIndex),
            .sourceNodeName = "CameraNode",
            .nodeIndex = cameraIndex,
            .horizontalFov = glm::radians(60.0f),
            .aspect = 16.0f / 9.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
            .orthographicWidth = 0.0f,
        });
    });

    auto lightIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(lightIndices, [&](std::uint32_t lightIndex) {
        scene.lights.push_back(nr::load::LightAsset{
            .name = std::format("BridgePlanLight{}", lightIndex),
            .sourceNodeName = "LightNode",
            .nodeIndex = lightIndex,
            .typeRaw = 2,
            .type = "point",
            .colorDiffuse = {1.0f, 1.0f, 1.0f},
            .attenuationLinear = 0.1f,
        });
    });

    return scene;
}

template <typename InputT, typename KeyFn>
[[nodiscard]] bool requireSequentialBridgeInputs(const std::vector<InputT> &inputs,
                                                 std::uint32_t expectedCount,
                                                 KeyFn &&expectedKey,
                                                 std::string_view label)
{
    if (!require(inputs.size() == expectedCount,
                 std::format("{} bridge entry count mismatch.", label)))
    {
        return false;
    }

    auto const expectedIndices = std::views::iota(std::uint32_t{0}, expectedCount);
    return std::ranges::all_of(expectedIndices, [&](std::uint32_t sourceIndex) {
        auto const &input = inputs[sourceIndex];
        if (!require(input.sourceIndex == sourceIndex,
                     std::format("{} bridge source index mismatch at {}.", label, sourceIndex)))
        {
            return false;
        }

        return require(input.canonicalKey == expectedKey(sourceIndex),
                       std::format("{} bridge canonical key mismatch at {}.", label, sourceIndex));
    });
}

[[nodiscard]] bool checkBridgePlanIncludesAllAssetKinds()
{
    std::println("\n=== Case: checkBridgePlanIncludesAllAssetKinds ===");

    auto sceneAsset = buildBridgePlanSceneAsset();
    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

    if (!require(plan.valid(), "Bridge plan should be valid when sourcePath and canonical texture key are present."))
    {
        return false;
    }

    if (!require(plan.textures.size() == 1, "Bridge plan should include one texture entry."))
    {
        return false;
    }
    if (!require(plan.materials.size() == 1, "Bridge plan should include one material entry."))
    {
        return false;
    }
    if (!require(plan.meshes.size() == 1, "Bridge plan should include one mesh entry."))
    {
        return false;
    }
    if (!require(plan.cameras.size() == 1, "Bridge plan should include one camera entry."))
    {
        return false;
    }
    if (!require(plan.lights.size() == 1, "Bridge plan should include one light entry."))
    {
        return false;
    }

    if (!require(plan.textures.front().sourceIndex == 0u, "Texture source index should be 0."))
    {
        return false;
    }
    if (!require(plan.materials.front().sourceIndex == 0u, "Material source index should be 0."))
    {
        return false;
    }
    if (!require(plan.meshes.front().sourceIndex == 0u, "Mesh source index should be 0."))
    {
        return false;
    }
    if (!require(plan.cameras.front().sourceIndex == 0u, "Camera source index should be 0."))
    {
        return false;
    }
    if (!require(plan.lights.front().sourceIndex == 0u, "Light source index should be 0."))
    {
        return false;
    }

    auto const expectedMaterialKey = nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0);
    auto const expectedMeshKey = nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0);
    auto const expectedCameraKey = nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0);
    auto const expectedLightKey = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0);

    if (!require(plan.textures.front().canonicalKey == sceneAsset.textures.front().key,
                 "Texture canonical key should match texture asset key."))
    {
        return false;
    }
    if (!require(plan.materials.front().canonicalKey == expectedMaterialKey,
                 "Material canonical key should match indexed canonical key helper."))
    {
        return false;
    }
    if (!require(plan.meshes.front().canonicalKey == expectedMeshKey,
                 "Mesh canonical key should match indexed canonical key helper."))
    {
        return false;
    }
    if (!require(plan.cameras.front().canonicalKey == expectedCameraKey,
                 "Camera canonical key should match indexed canonical key helper."))
    {
        return false;
    }
    if (!require(plan.lights.front().canonicalKey == expectedLightKey,
                 "Light canonical key should match indexed canonical key helper."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkBridgePlanRejectsEmptySourcePathForIndexedAssets()
{
    std::println("\n=== Case: checkBridgePlanRejectsEmptySourcePathForIndexedAssets ===");

    auto sceneAsset = buildBridgePlanSceneAsset();
    sceneAsset.sourcePath.clear();

    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);
    if (!require(!plan.valid(), "Bridge plan should be invalid when indexed canonical keys cannot be derived."))
    {
        return false;
    }

    if (!require(plan.textures.size() == 1u,
                 "Texture bridge entries should still be generated from texture asset keys when sourcePath is empty."))
    {
        return false;
    }
    if (!require(plan.materials.empty(), "Material bridge entries should be skipped when sourcePath is empty."))
    {
        return false;
    }
    if (!require(plan.meshes.empty(), "Mesh bridge entries should be skipped when sourcePath is empty."))
    {
        return false;
    }
    if (!require(plan.cameras.empty(), "Camera bridge entries should be skipped when sourcePath is empty."))
    {
        return false;
    }
    if (!require(plan.lights.empty(), "Light bridge entries should be skipped when sourcePath is empty."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkBridgePlanRejectsMissingTextureKey()
{
    std::println("\n=== Case: checkBridgePlanRejectsMissingTextureKey ===");

    auto sceneAsset = buildBridgePlanSceneAsset();
    sceneAsset.textures.front().key.clear();

    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);
    if (!require(!plan.valid(), "Bridge plan should be invalid when texture canonical key is empty."))
    {
        return false;
    }

    if (!require(plan.textures.empty(), "Texture bridge entry should be skipped when texture canonical key is empty."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkBridgePlanPreservesSourceOrdering()
{
    std::println("\n=== Case: checkBridgePlanPreservesSourceOrdering ===");

    auto sceneAsset = buildBridgePlanMultiAssetSceneAsset();
    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

    if (!require(plan.valid(), "Bridge plan should be valid for multi-asset source ordering case."))
    {
        return false;
    }

    if (!requireSequentialBridgeInputs(plan.textures,
                                       static_cast<std::uint32_t>(sceneAsset.textures.size()),
                                       [&](std::uint32_t sourceIndex) {
                                           return sceneAsset.textures[sourceIndex].key;
                                       },
                                       "Texture"))
    {
        return false;
    }

    if (!requireSequentialBridgeInputs(plan.materials,
                                       static_cast<std::uint32_t>(sceneAsset.materials.size()),
                                       [&](std::uint32_t sourceIndex) {
                                           return nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, sourceIndex);
                                       },
                                       "Material"))
    {
        return false;
    }

    if (!requireSequentialBridgeInputs(plan.meshes,
                                       static_cast<std::uint32_t>(sceneAsset.meshes.size()),
                                       [&](std::uint32_t sourceIndex) {
                                           return nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, sourceIndex);
                                       },
                                       "Mesh"))
    {
        return false;
    }

    if (!requireSequentialBridgeInputs(plan.cameras,
                                       static_cast<std::uint32_t>(sceneAsset.cameras.size()),
                                       [&](std::uint32_t sourceIndex) {
                                           return nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, sourceIndex);
                                       },
                                       "Camera"))
    {
        return false;
    }

    if (!requireSequentialBridgeInputs(plan.lights,
                                       static_cast<std::uint32_t>(sceneAsset.lights.size()),
                                       [&](std::uint32_t sourceIndex) {
                                           return nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, sourceIndex);
                                       },
                                       "Light"))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkBridgePlanIncludesAllAssetKinds", &checkBridgePlanIncludesAllAssetKinds},
        std::pair{"checkBridgePlanRejectsEmptySourcePathForIndexedAssets", &checkBridgePlanRejectsEmptySourcePathForIndexedAssets},
        std::pair{"checkBridgePlanRejectsMissingTextureKey", &checkBridgePlanRejectsMissingTextureKey},
        std::pair{"checkBridgePlanPreservesSourceOrdering", &checkBridgePlanPreservesSourceOrdering},
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
