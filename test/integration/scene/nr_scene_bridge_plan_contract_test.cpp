import std;
import dependency;
import nr.load;
import nr.scene;
import nr.test;

namespace
{
[[nodiscard]] nr::load::SceneAsset makeBridgePlanAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_scene.gltf"};

    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/bridge_plan/base_color",
    });

    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "bridge_plan_material",
    });

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "bridge_plan_mesh",
        .materialIndex = 0,
    });

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "BridgePlanCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 0,
        .horizontalFov = glm::radians(60.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
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

[[nodiscard]] nr::load::SceneAsset makeMultiAssetBridgePlanAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_multi_scene.gltf"};

    auto textureIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{3});
    std::ranges::for_each(textureIndices, [&](std::uint32_t textureIndex) {
        scene.textures.push_back(nr::load::TextureAsset{
            .key = std::format("manual://textures/bridge_plan/multi_{}", textureIndex),
        });
    });

    auto materialIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(materialIndices, [&](std::uint32_t materialIndex) {
        scene.materials.push_back(nr::load::MaterialAsset{
            .name = std::format("bridge_plan_material_{}", materialIndex),
        });
    });

    auto meshIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(meshIndices, [&](std::uint32_t meshIndex) {
        scene.meshes.push_back(nr::load::MeshAsset{
            .name = std::format("bridge_plan_mesh_{}", meshIndex),
            .materialIndex = meshIndex % static_cast<std::uint32_t>(scene.materials.size()),
        });
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
void requireSequentialBridgeInputs(const std::vector<InputT> &inputs,
                                   std::uint32_t expectedCount,
                                   KeyFn &&expectedKey,
                                   std::string_view label)
{
    nr::test::requireEqual(inputs.size(), static_cast<std::size_t>(expectedCount), "bridge entry count mismatch");

    auto const sourceIndices = std::views::iota(std::uint32_t{0}, expectedCount);
    std::ranges::for_each(sourceIndices, [&](std::uint32_t sourceIndex) {
        auto const &input = inputs[sourceIndex];
        nr::test::requireEqual(input.sourceIndex, sourceIndex, "bridge source index mismatch");
        nr::test::require(input.canonicalKey == expectedKey(sourceIndex),
                          std::format("{} bridge canonical key mismatch at {}", label, sourceIndex));
    });
}

const nr::test::CaseRegistrar allAssetKindsCase{
    "scene bridge plan includes all asset kinds",
    [] {
        auto sceneAsset = makeBridgePlanAsset();
        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(plan.valid(), "bridge plan should be valid");
        nr::test::requireEqual(plan.sourcePath, sceneAsset.sourcePath);
        nr::test::requireEqual(plan.textures.size(), std::size_t{1});
        nr::test::requireEqual(plan.materials.size(), std::size_t{1});
        nr::test::requireEqual(plan.meshes.size(), std::size_t{1});
        nr::test::requireEqual(plan.cameras.size(), std::size_t{1});
        nr::test::requireEqual(plan.lights.size(), std::size_t{1});

        nr::test::requireEqual(plan.textures.front().canonicalKey, sceneAsset.textures.front().key);
        nr::test::requireEqual(plan.materials.front().canonicalKey,
                               nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.meshes.front().canonicalKey,
                               nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.cameras.front().canonicalKey,
                               nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.lights.front().canonicalKey,
                               nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0));
    }};

const nr::test::CaseRegistrar emptySourcePathCase{
    "scene bridge plan reports indexed assets without source path",
    [] {
        auto sceneAsset = makeBridgePlanAsset();
        sceneAsset.sourcePath.clear();

        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(!plan.valid(), "indexed canonical keys require a source path");
        nr::test::requireEqual(plan.textures.size(), std::size_t{1});
        nr::test::require(plan.materials.empty(), "materials should be skipped without source path");
        nr::test::require(plan.meshes.empty(), "meshes should be skipped without source path");
        nr::test::require(plan.cameras.empty(), "cameras should be skipped without source path");
        nr::test::require(plan.lights.empty(), "lights should be skipped without source path");
    }};

const nr::test::CaseRegistrar missingTextureKeyCase{
    "scene bridge plan reports missing texture keys",
    [] {
        auto sceneAsset = makeBridgePlanAsset();
        sceneAsset.textures.front().key.clear();

        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(!plan.valid(), "texture canonical keys must not be empty");
        nr::test::require(plan.textures.empty(), "invalid texture entry should be skipped");
        nr::test::requireEqual(plan.materials.size(), std::size_t{1});
        nr::test::requireEqual(plan.meshes.size(), std::size_t{1});
    }};

const nr::test::CaseRegistrar sourceOrderingCase{
    "scene bridge plan preserves source ordering",
    [] {
        auto sceneAsset = makeMultiAssetBridgePlanAsset();
        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(plan.valid(), "multi-asset bridge plan should be valid");
        requireSequentialBridgeInputs(
            plan.textures,
            static_cast<std::uint32_t>(sceneAsset.textures.size()),
            [&](std::uint32_t sourceIndex) { return sceneAsset.textures[sourceIndex].key; },
            "Texture");
        requireSequentialBridgeInputs(
            plan.materials,
            static_cast<std::uint32_t>(sceneAsset.materials.size()),
            [&](std::uint32_t sourceIndex) { return nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, sourceIndex); },
            "Material");
        requireSequentialBridgeInputs(
            plan.meshes,
            static_cast<std::uint32_t>(sceneAsset.meshes.size()),
            [&](std::uint32_t sourceIndex) { return nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, sourceIndex); },
            "Mesh");
        requireSequentialBridgeInputs(
            plan.cameras,
            static_cast<std::uint32_t>(sceneAsset.cameras.size()),
            [&](std::uint32_t sourceIndex) { return nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, sourceIndex); },
            "Camera");
        requireSequentialBridgeInputs(
            plan.lights,
            static_cast<std::uint32_t>(sceneAsset.lights.size()),
            [&](std::uint32_t sourceIndex) { return nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, sourceIndex); },
            "Light");
    }};
} // namespace
