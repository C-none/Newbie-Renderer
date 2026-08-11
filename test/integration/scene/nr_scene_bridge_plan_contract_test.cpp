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
[[nodiscard]] std::string readProjectFile(const std::filesystem::path &relativePath)
{
    auto file = std::ifstream{std::filesystem::path{std::string{nr::projectRoot}} / relativePath};
    nr::test::require(file.good(), std::format("project file '{}' should be readable", relativePath.generic_string()));
    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
}

[[nodiscard]] nr::load::SceneAsset makeBridgePlanAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_scene.gltf"};

    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/bridge_plan/base_color",
    });

    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "bridge_plan_material",
        .textures =
            {
                nr::load::MaterialTextureBinding{
                    .textureIndex = 0u,
                    .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                    .sourceSemanticName = "base_color",
                },
            },
    });

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "bridge_plan_mesh",
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = "bridge_plan_geometry_0",
                    .materialIndex = 0,
                },
            },
    });

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "BridgePlanCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 0,
        .horizontalFov = nr::math::radians(60.0f),
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

    scene.rootNodeIndex = 0u;
    scene.nodes.push_back(nr::load::NodeAsset{
        .name = "BridgePlanRoot",
        .meshIndices = {0u},
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
            .geometries =
                {
                    nr::load::MeshGeometryAsset{
                        .name = std::format("bridge_plan_geometry_{}", meshIndex),
                        .materialIndex = meshIndex % static_cast<std::uint32_t>(scene.materials.size()),
                    },
                },
        });
    });

    auto cameraIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{2});
    std::ranges::for_each(cameraIndices, [&](std::uint32_t cameraIndex) {
        scene.cameras.push_back(nr::load::CameraAsset{
            .name = std::format("BridgePlanCamera{}", cameraIndex),
            .sourceNodeName = "CameraNode",
            .nodeIndex = cameraIndex,
            .horizontalFov = nr::math::radians(60.0f),
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

    scene.rootNodeIndex = 0u;
    scene.nodes.push_back(nr::load::NodeAsset{
        .name = "BridgePlanMultiRoot",
        .meshIndices = {0u, 1u},
    });

    return scene;
}

[[nodiscard]] nr::load::SceneAsset makeGraphOnlyBridgePlanAsset(std::size_t nodeCount)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_graph_scene.gltf"};
    scene.nodes.resize(nodeCount);
    if (!scene.nodes.empty())
    {
        scene.rootNodeIndex = 0u;
        scene.nodes.front().name = "Root";
    }
    return scene;
}

void connectLinearNodeChain(nr::load::SceneAsset &scene)
{
    if (scene.nodes.size() < 2u)
    {
        return;
    }

    auto const parentIndices = std::views::iota(std::size_t{0}, scene.nodes.size() - 1u);
    std::ranges::for_each(parentIndices, [&](std::size_t parentIndex) {
        scene.nodes[parentIndex].childIndices = {static_cast<std::uint32_t>(parentIndex + 1u)};
        scene.nodes[parentIndex + 1u].parentIndex = static_cast<std::uint32_t>(parentIndex);
    });
}

[[nodiscard]] nr::load::SceneAsset makeTextureIntentRegistrationAsset(
    std::string_view sourceName, std::span<const nr::resource::MaterialTextureSlotSemantic> semantics)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::format("texture_intent_{}.gltf", sourceName)};
    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/bridge_plan/shared_registration_identity",
        .decodedImage =
            nr::load::Image{
                .width = 1u,
                .height = 1u,
                .channels = 4u,
                .pixels = {64u, 128u, 192u, 255u},
            },
    });

    auto material = nr::load::MaterialAsset{.name = std::format("texture_intent_{}_material", sourceName)};
    std::ranges::for_each(semantics, [&](nr::resource::MaterialTextureSlotSemantic semantic) {
        material.textures.push_back(nr::load::MaterialTextureBinding{
            .textureIndex = 0u,
            .semantic = semantic,
        });
    });
    scene.materials.push_back(std::move(material));
    return scene;
}

[[nodiscard]] nr::load::TextureAsset makeSinglePixelTexture(std::string key)
{
    return nr::load::TextureAsset{
        .key = std::move(key),
        .decodedImage =
            nr::load::Image{
                .width = 1u,
                .height = 1u,
                .channels = 4u,
                .pixels = {32u, 96u, 160u, 255u},
            },
    };
}

[[nodiscard]] nr::load::SceneAsset makeRegistrationRollbackAsset()
{
    constexpr auto sharedTextureKey = std::string_view{"manual://textures/bridge_plan/shared_registration_identity"};
    constexpr auto repeatedTextureKey = std::string_view{"manual://textures/bridge_plan/rollback_repeated"};

    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_bridge_plan_registration_rollback.gltf"};
    scene.textures = {
        makeSinglePixelTexture(std::string{sharedTextureKey}),
        makeSinglePixelTexture(std::string{repeatedTextureKey}),
        makeSinglePixelTexture(std::string{repeatedTextureKey}),
    };

    scene.materials = {
        nr::load::MaterialAsset{
            .name = "rollback_shared_material",
            .textures =
                {
                    nr::load::MaterialTextureBinding{
                        .textureIndex = 0u,
                        .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                    },
                },
        },
        nr::load::MaterialAsset{
            .name = "rollback_repeated_material",
            .textures =
                {
                    nr::load::MaterialTextureBinding{
                        .textureIndex = 1u,
                        .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                    },
                    nr::load::MaterialTextureBinding{
                        .textureIndex = 2u,
                        .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                    },
                },
        },
    };

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "rollback_late_failure_mesh",
        .indices = {0u, 1u, 2u},
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = "rollback_default_material_geometry",
                    .indexCount = 3u,
                },
            },
    });

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "RollbackCamera",
        .sourceNodeName = "RollbackRoot",
        .nodeIndex = 0u,
        .horizontalFov = nr::math::radians(60.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
    });
    scene.lights.push_back(nr::load::LightAsset{
        .name = "RollbackLight",
        .sourceNodeName = "RollbackRoot",
        .nodeIndex = 0u,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 1.0f, 1.0f},
    });

    scene.rootNodeIndex = 0u;
    scene.nodes.push_back(nr::load::NodeAsset{
        .name = "RollbackRoot",
        .meshIndices = {0u},
    });
    return scene;
}

template <typename InputT, typename KeyFn>
void requireSequentialBridgeInputs(const std::vector<InputT> &inputs, std::uint32_t expectedCount, KeyFn &&expectedKey,
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

void requireEqualSceneStatistics(const nr::scene::SceneStatistics &actual, const nr::scene::SceneStatistics &expected)
{
    nr::test::requireEqual(actual.templateCount, expected.templateCount);
    nr::test::requireEqual(actual.instanceCount, expected.instanceCount);
    nr::test::requireEqual(actual.extractProfileCount, expected.extractProfileCount);
    nr::test::requireEqual(actual.meshAssetCount, expected.meshAssetCount);
    nr::test::requireEqual(actual.materialAssetCount, expected.materialAssetCount);
    nr::test::requireEqual(actual.textureAssetCount, expected.textureAssetCount);
    nr::test::requireEqual(actual.cameraAssetCount, expected.cameraAssetCount);
    nr::test::requireEqual(actual.lightAssetCount, expected.lightAssetCount);
    nr::test::requireEqual(actual.templateNodeCount, expected.templateNodeCount);
    nr::test::requireEqual(actual.templateMeshBindingCount, expected.templateMeshBindingCount);
    nr::test::requireEqual(actual.templateCameraBindingCount, expected.templateCameraBindingCount);
    nr::test::requireEqual(actual.templateLightBindingCount, expected.templateLightBindingCount);
}

const nr::test::CaseRegistrar allAssetKindsCase{
    "scene bridge plan includes all asset kinds", [] {
        auto sceneAsset = makeBridgePlanAsset();
        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(plan.valid(), "bridge plan should be valid");
        nr::test::requireEqual(plan.sourcePath, sceneAsset.sourcePath);
        nr::test::requireEqual(plan.textures.size(), std::size_t{1});
        nr::test::requireEqual(plan.materials.size(), std::size_t{1});
        nr::test::requireEqual(plan.meshes.size(), std::size_t{1});
        nr::test::requireEqual(plan.cameras.size(), std::size_t{1});
        nr::test::requireEqual(plan.lights.size(), std::size_t{1});
        nr::test::requireEqual(plan.maximumReachableTemplateDepth, std::size_t{2});

        nr::test::requireEqual(plan.textures.front().samplingColorIntent, nr::scene::TextureSamplingColorIntent::srgb);
        nr::test::requireEqual(plan.textures.front().canonicalKey,
                               nr::scene::SceneBridge::makeTextureCanonicalKey(
                                   sceneAsset, 0u, nr::scene::TextureSamplingColorIntent::srgb));
        nr::test::requireEqual(plan.materials.front().canonicalKey,
                               nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.meshes.front().canonicalKey,
                               nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.cameras.front().canonicalKey,
                               nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0));
        nr::test::requireEqual(plan.lights.front().canonicalKey,
                               nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0));
    }};

const nr::test::CaseRegistrar transparentStableKeyLookupCase{
    "scene stable-key lookup accepts bounded string-view slices without allocation adapters", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = nr::load::SceneAsset{};
        sceneAsset.sourcePath = std::filesystem::path{"transparent_stable_key_lookup.gltf"};
        sceneAsset.textures.push_back(nr::load::TextureAsset{
            .key = "manual://textures/transparent_stable_key_lookup",
        });
        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);
        auto templateHandle = scene.registerTemplate(sceneAsset);
        nr::test::require(plan.valid() && templateHandle.valid(),
                          "stable-key lookup fixture should plan and register");

        auto const &textureKey = plan.textures.front().canonicalKey;
        auto paddedKey = std::format("<{}>", textureKey);
        auto slicedKey = std::string_view{paddedKey}.substr(1u, textureKey.size());
        nr::test::require(slicedKey.data()[slicedKey.size()] == '>',
                          "lookup slice should not end at a null terminator");

        auto fullLookup = scene.findTextureHandleByStableKey(textureKey);
        auto slicedLookup = scene.findTextureHandleByStableKey(slicedKey);
        nr::test::require(fullLookup.has_value() && slicedLookup.has_value(),
                          "full and bounded stable-key views should both resolve");
        nr::test::requireEqual(*slicedLookup, *fullLookup);

        auto sceneInterface = readProjectFile("src/scene/nrScene.ixx");
        auto sceneExtraction = readProjectFile("src/scene/nrSceneExtraction.cpp");
        nr::test::require(sceneInterface.contains("std::map<std::string, HandleT, std::less<>> handlesByStableKey_{};"),
                          "keyed storage should use a transparent comparator");
        nr::test::require(sceneInterface.contains("handlesByStableKey_.find(stableKey)"),
                          "keyed storage should find directly with string_view");
        nr::test::require(!sceneInterface.contains("handlesByStableKey_.find(std::string{stableKey})"),
                          "noexcept stable-key lookup must not allocate a temporary string");
        nr::test::require(!sceneInterface.contains("static auto findHandleByStableKey"),
                          "Scene should not retain a pure stable-key forwarding helper");

        auto directLookups = std::array{
            "return meshes_.findHandleByStableKey(stableKey);",
            "return materials_.findHandleByStableKey(stableKey);",
            "return textures_.findHandleByStableKey(stableKey);",
            "return cameras_.findHandleByStableKey(stableKey);",
            "return lights_.findHandleByStableKey(stableKey);",
        };
        nr::test::require(std::ranges::all_of(directLookups, [&](std::string_view lookup) {
                              return sceneExtraction.contains(lookup);
                          }),
                          "each typed Scene stable-key entry should call its storage directly");
    }};

const nr::test::CaseRegistrar emptySourcePathCase{
    "scene bridge plan reports indexed assets without source path", [] {
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
    "scene bridge plan reports missing texture keys", [] {
        auto sceneAsset = makeBridgePlanAsset();
        sceneAsset.textures.front().key.clear();

        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(!plan.valid(), "texture canonical keys must not be empty");
        nr::test::require(plan.textures.empty(), "invalid texture entry should be skipped");
        nr::test::requireEqual(plan.materials.size(), std::size_t{1});
        nr::test::requireEqual(plan.meshes.size(), std::size_t{1});
    }};

const nr::test::CaseRegistrar sourceOrderingCase{
    "scene bridge plan preserves source ordering", [] {
        auto sceneAsset = makeMultiAssetBridgePlanAsset();
        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

        nr::test::require(plan.valid(), "multi-asset bridge plan should be valid");
        requireSequentialBridgeInputs(
            plan.textures, static_cast<std::uint32_t>(sceneAsset.textures.size()),
            [&](std::uint32_t sourceIndex) {
                return nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset, sourceIndex,
                                                                       nr::scene::TextureSamplingColorIntent::linear);
            },
            "Texture");
        requireSequentialBridgeInputs(
            plan.materials, static_cast<std::uint32_t>(sceneAsset.materials.size()),
            [&](std::uint32_t sourceIndex) {
                return nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, sourceIndex);
            },
            "Material");
        requireSequentialBridgeInputs(
            plan.meshes, static_cast<std::uint32_t>(sceneAsset.meshes.size()),
            [&](std::uint32_t sourceIndex) {
                return nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, sourceIndex);
            },
            "Mesh");
        requireSequentialBridgeInputs(
            plan.cameras, static_cast<std::uint32_t>(sceneAsset.cameras.size()),
            [&](std::uint32_t sourceIndex) {
                return nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, sourceIndex);
            },
            "Camera");
        requireSequentialBridgeInputs(
            plan.lights, static_cast<std::uint32_t>(sceneAsset.lights.size()),
            [&](std::uint32_t sourceIndex) {
                return nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, sourceIndex);
            },
            "Light");
    }};

const nr::test::CaseRegistrar structuralPreflightCase{
    "scene bridge plan rejects malformed reachable node graphs and structural references", [] {
        auto requireInvalid = [](const nr::load::SceneAsset &sceneAsset, std::string_view message) {
            nr::test::require(!nr::scene::SceneBridge::buildPlan(sceneAsset).valid(), message);
        };

        auto invalidRoot = makeGraphOnlyBridgePlanAsset(1u);
        invalidRoot.rootNodeIndex = nr::load::invalidIndex;
        requireInvalid(invalidRoot, "a non-empty scene requires a valid root");

        auto cycle = makeGraphOnlyBridgePlanAsset(3u);
        cycle.nodes[0].childIndices = {1u};
        cycle.nodes[1].childIndices = {2u};
        cycle.nodes[2].childIndices = {1u};
        requireInvalid(cycle, "a reachable multi-node cycle must be rejected");

        auto selfCycle = makeGraphOnlyBridgePlanAsset(1u);
        selfCycle.nodes[0].childIndices = {0u};
        requireInvalid(selfCycle, "a root self-cycle must be rejected");

        auto sharedChild = makeGraphOnlyBridgePlanAsset(4u);
        sharedChild.nodes[0].childIndices = {1u, 2u};
        sharedChild.nodes[1].childIndices = {3u};
        sharedChild.nodes[2].childIndices = {3u};
        requireInvalid(sharedChild, "a reachable diamond must reject its shared child");

        auto invalidChild = makeGraphOnlyBridgePlanAsset(1u);
        invalidChild.nodes[0].childIndices = {9u};
        requireInvalid(invalidChild, "an out-of-range child reference must be rejected");

        auto invalidMesh = makeGraphOnlyBridgePlanAsset(1u);
        invalidMesh.nodes[0].meshIndices = {4u};
        requireInvalid(invalidMesh, "an out-of-range node mesh reference must be rejected");

        auto invalidTexture = makeBridgePlanAsset();
        invalidTexture.materials[0].textures[0].textureIndex = 7u;
        requireInvalid(invalidTexture, "an out-of-range material texture reference must be rejected");

        auto invalidUvSet = makeBridgePlanAsset();
        invalidUvSet.materials[0].textures[0].uvChannel = 2u;
        requireInvalid(invalidUvSet, "a supported texture semantic must reject unsupported UV sets");

        auto ignoredSemanticUvSet = makeBridgePlanAsset();
        ignoredSemanticUvSet.materials[0].textures[0].semantic = nr::resource::MaterialTextureSlotSemantic::unsupported;
        ignoredSemanticUvSet.materials[0].textures[0].uvChannel = 7u;
        nr::test::require(nr::scene::SceneBridge::buildPlan(ignoredSemanticUvSet).valid(),
                          "an ignored unsupported semantic must retain warning-only UV behavior");
    }};

const nr::test::CaseRegistrar disconnectedAndDeepGraphCase{
    "scene bridge plan handles disconnected, deep, and Flecs-bounded hierarchies deterministically", [] {
        auto disconnected = makeGraphOnlyBridgePlanAsset(2u);
        disconnected.nodes[1].name = "Disconnected";
        auto disconnectedPlan = nr::scene::SceneBridge::buildPlan(disconnected);
        nr::test::require(disconnectedPlan.valid(), "a disconnected node is warning-only, not a cycle");
        nr::test::requireEqual(disconnectedPlan.reachableNodeCount, std::size_t{1u});
        nr::test::requireEqual(disconnectedPlan.unreachableNodeCount, std::size_t{1u});
        nr::test::requireEqual(disconnectedPlan.maximumReachableTemplateDepth, std::size_t{1u});

        auto empty = makeGraphOnlyBridgePlanAsset(0u);
        auto emptyPlan = nr::scene::SceneBridge::buildPlan(empty);
        nr::test::require(emptyPlan.valid(), "an empty node graph must remain valid without a root");
        nr::test::requireEqual(emptyPlan.reachableNodeCount, std::size_t{0u});
        nr::test::requireEqual(emptyPlan.unreachableNodeCount, std::size_t{0u});
        nr::test::requireEqual(emptyPlan.maximumReachableTemplateDepth, std::size_t{0u});

        auto unsupportedLight = makeGraphOnlyBridgePlanAsset(1u);
        unsupportedLight.lights.push_back(nr::load::LightAsset{
            .name = "SkippedAmbient",
            .nodeIndex = 0u,
            .typeRaw = 3,
            .type = "ambient",
        });
        auto unsupportedLightPlan = nr::scene::SceneBridge::buildPlan(unsupportedLight);
        nr::test::require(unsupportedLightPlan.valid(),
                          "an unsupported light kind should retain warning-only behavior");
        nr::test::requireEqual(unsupportedLightPlan.maximumReachableTemplateDepth, std::size_t{1u},
                               "a skipped light must not contribute a template binding level");

        constexpr auto deepNodeCount = std::size_t{8192u};
        auto deep = makeGraphOnlyBridgePlanAsset(deepNodeCount);
        connectLinearNodeChain(deep);

        auto deepPlan = nr::scene::SceneBridge::buildPlan(deep);
        nr::test::require(deepPlan.valid(), "a deep acyclic chain should pass iterative preflight");
        nr::test::requireEqual(deepPlan.reachableNodeCount, deepNodeCount);
        nr::test::requireEqual(deepPlan.unreachableNodeCount, std::size_t{0u});
        nr::test::requireEqual(deepPlan.maximumReachableTemplateDepth, deepNodeCount);

        nr::rhi::Device device{};
        auto runtimeScene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});

        auto const beforeDeepRegistration = runtimeScene.statistics();
        auto deepTemplateHandle = runtimeScene.registerTemplate(deep);
        auto const afterDeepRegistration = runtimeScene.statistics();
        nr::test::require(!deepTemplateHandle.valid(),
                          "a hierarchy beyond the Flecs DAG limit must fail before ECS mutation");
        requireEqualSceneStatistics(afterDeepRegistration, beforeDeepRegistration);

        constexpr auto runtimeNodeCount = std::size_t{120u};
        auto runtimeDeep = makeGraphOnlyBridgePlanAsset(runtimeNodeCount);
        runtimeDeep.sourcePath = "manual_bridge_plan_runtime_deep_scene.gltf";
        connectLinearNodeChain(runtimeDeep);
        runtimeDeep.cameras.push_back(nr::load::CameraAsset{
            .name = "RuntimeDeepCamera",
            .sourceNodeName = "RuntimeDeepCameraNode",
            .nodeIndex = static_cast<std::uint32_t>(runtimeNodeCount - 1u),
            .horizontalFov = nr::math::radians(60.0f),
            .aspect = 16.0f / 9.0f,
            .nearPlane = 0.1f,
            .farPlane = 100.0f,
        });

        auto runtimeDeepPlan = nr::scene::SceneBridge::buildPlan(runtimeDeep);
        nr::test::require(runtimeDeepPlan.valid(), "a within-limit runtime chain should pass structural preflight");
        nr::test::requireEqual(runtimeDeepPlan.maximumReachableTemplateDepth, runtimeNodeCount + 1u,
                               "camera bindings must contribute one template hierarchy level");

        auto templateHandle = runtimeScene.registerTemplate(runtimeDeep);
        nr::test::require(templateHandle.valid(), "within-limit ChildOf template construction should succeed");
        auto instanceHandle = runtimeScene.instantiate(templateHandle);
        nr::test::require(instanceHandle.valid(), "deep ChildOf runtime initialization should remain iterative");
        runtimeScene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});
        nr::test::requireEqual(runtimeScene.statistics().templateNodeCount, runtimeNodeCount,
                               "runtime registration should retain every reachable node");
        nr::test::requireEqual(runtimeScene.statistics().templateCameraBindingCount, std::size_t{1u});
        nr::test::require(runtimeScene.tryGetInstance(instanceHandle).has_value(),
                          "deep ChildOf instance should survive iterative hierarchy update");

        auto parentTemplateHandle = runtimeScene.registerTemplate(
            runtimeDeep, nr::scene::SceneTemplateCreateInfo{
                             .stableKey = "manual_bridge_plan_runtime_deep_scene.parent",
                             .hierarchyPolicy = nr::scene::TemplateHierarchyPolicy::preferParent,
                         });
        nr::test::require(parentTemplateHandle.valid(), "within-limit Parent template construction should succeed");
        auto parentInstanceHandle = runtimeScene.instantiate(parentTemplateHandle);
        nr::test::require(parentInstanceHandle.valid(), "deep Parent runtime initialization should remain iterative");
        runtimeScene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});
        nr::test::require(runtimeScene.tryGetInstance(parentInstanceHandle).has_value(),
                          "deep Parent instance should survive iterative hierarchy update");

        constexpr auto overLimitNodeCount = std::size_t{129u};
        auto overLimit = makeGraphOnlyBridgePlanAsset(overLimitNodeCount);
        overLimit.sourcePath = "manual_bridge_plan_over_limit_scene.gltf";
        connectLinearNodeChain(overLimit);
        auto overLimitPlan = nr::scene::SceneBridge::buildPlan(overLimit);
        nr::test::require(overLimitPlan.valid(), "over-limit source structure should remain valid for planning");
        nr::test::requireEqual(overLimitPlan.maximumReachableTemplateDepth, overLimitNodeCount);

        constexpr auto policies = std::array{
            nr::scene::TemplateHierarchyPolicy::autoSelect,
            nr::scene::TemplateHierarchyPolicy::preferParent,
            nr::scene::TemplateHierarchyPolicy::preferChildOf,
        };
        auto const policyIndices = std::views::iota(std::size_t{0}, policies.size());
        std::ranges::for_each(policyIndices, [&](std::size_t policyIndex) {
            auto const beforeRejectedRegistration = runtimeScene.statistics();
            auto rejectedHandle = runtimeScene.registerTemplate(
                overLimit, nr::scene::SceneTemplateCreateInfo{
                               .stableKey = std::format("manual_bridge_plan_over_limit_scene.policy_{}", policyIndex),
                               .hierarchyPolicy = policies[policyIndex],
                           });
            auto const afterRejectedRegistration = runtimeScene.statistics();
            nr::test::require(!rejectedHandle.valid(),
                              "every hierarchy storage policy must reject a depth beyond the Flecs DAG limit");
            requireEqualSceneStatistics(afterRejectedRegistration, beforeRejectedRegistration);
        });
    }};

const nr::test::CaseRegistrar textureIntentPlanCase{
    "scene bridge plan canonicalizes texture identity with final sampling color intent", [] {
        auto sceneAsset = makeGraphOnlyBridgePlanAsset(0u);
        constexpr auto sharedBaseKey = std::string_view{"manual://textures/bridge_plan/shared_intent"};
        sceneAsset.textures = {
            nr::load::TextureAsset{.key = std::string{sharedBaseKey}},
            nr::load::TextureAsset{.key = std::string{sharedBaseKey}},
            nr::load::TextureAsset{.key = std::string{sharedBaseKey}},
        };
        sceneAsset.materials = {
            nr::load::MaterialAsset{
                .name = "color",
                .textures =
                    {
                        nr::load::MaterialTextureBinding{
                            .textureIndex = 0u,
                            .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                        },
                    },
            },
            nr::load::MaterialAsset{
                .name = "linear_and_mixed",
                .textures =
                    {
                        nr::load::MaterialTextureBinding{
                            .textureIndex = 1u,
                            .semantic = nr::resource::MaterialTextureSlotSemantic::normal,
                        },
                        nr::load::MaterialTextureBinding{
                            .textureIndex = 2u,
                            .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                        },
                        nr::load::MaterialTextureBinding{
                            .textureIndex = 2u,
                            .semantic = nr::resource::MaterialTextureSlotSemantic::normal,
                        },
                    },
            },
        };

        auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);
        nr::test::require(plan.valid(), "texture intent fixture should produce a valid plan");
        nr::test::requireEqual(plan.textures[0].samplingColorIntent, nr::scene::TextureSamplingColorIntent::srgb);
        nr::test::requireEqual(plan.textures[1].samplingColorIntent, nr::scene::TextureSamplingColorIntent::linear);
        nr::test::requireEqual(plan.textures[2].samplingColorIntent, nr::scene::TextureSamplingColorIntent::linear);
        nr::test::require(!plan.textures[0].mixedColorAndLinearReferences);
        nr::test::require(!plan.textures[1].mixedColorAndLinearReferences);
        nr::test::require(plan.textures[2].mixedColorAndLinearReferences,
                          "mixed color and linear references should be recorded once in the plan");
        nr::test::require(plan.textures[0].canonicalKey != plan.textures[1].canonicalKey,
                          "the same base key with different final intents must have distinct identities");
        nr::test::requireEqual(plan.textures[1].canonicalKey, plan.textures[2].canonicalKey,
                               "the same base key and final intent should retain stable dedup identity");
    }};

const nr::test::CaseRegistrar invalidRegistrationPreflightCase{
    "scene registration rejects an invalid plan before registry and template mutations", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto invalidAsset = makeBridgePlanAsset();
        invalidAsset.nodes[0].childIndices = {0u};

        auto const before = scene.statistics();
        auto templateHandle = scene.registerTemplate(invalidAsset);
        auto const after = scene.statistics();

        nr::test::require(!templateHandle.valid(), "invalid structural preflight must reject registration");
        nr::test::requireEqual(after.templateCount, before.templateCount);
        nr::test::requireEqual(after.instanceCount, before.instanceCount);
        nr::test::requireEqual(after.meshAssetCount, before.meshAssetCount);
        nr::test::requireEqual(after.materialAssetCount, before.materialAssetCount);
        nr::test::requireEqual(after.textureAssetCount, before.textureAssetCount);
        nr::test::requireEqual(after.cameraAssetCount, before.cameraAssetCount);
        nr::test::requireEqual(after.lightAssetCount, before.lightAssetCount);
        nr::test::requireEqual(after.templateNodeCount, before.templateNodeCount);
        nr::test::requireEqual(after.templateMeshBindingCount, before.templateMeshBindingCount);
        nr::test::requireEqual(after.templateCameraBindingCount, before.templateCameraBindingCount);
        nr::test::requireEqual(after.templateLightBindingCount, before.templateLightBindingCount);
    }};

const nr::test::CaseRegistrar atomicRegistrationRollbackCase{
    "scene registration rolls back every late-created record and permits an exact-key retry", [] {
        constexpr auto baseColor = nr::resource::MaterialTextureSlotSemantic::baseColor;

        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sharedOwner = makeTextureIntentRegistrationAsset("rollback_shared_owner", std::array{baseColor});
        auto sharedOwnerPlan = nr::scene::SceneBridge::buildPlan(sharedOwner);
        nr::test::require(sharedOwnerPlan.valid(), "shared rollback owner must pass preflight");
        nr::test::require(scene.registerTemplate(sharedOwner).valid(), "shared rollback owner must register");

        auto sharedHandle = scene.findTextureHandleByStableKey(sharedOwnerPlan.textures[0].canonicalKey);
        nr::test::require(sharedHandle.has_value(), "shared rollback texture must resolve before the failure");
        auto sharedRecordBefore = scene.tryGetTextureAsset(*sharedHandle);
        nr::test::require(sharedRecordBefore.has_value(), "shared rollback texture record must exist");
        auto const sharedPinsBefore = sharedRecordBefore->get().liveTemplatePins;
        auto const sharedCpuVersionBefore = sharedRecordBefore->get().cpuVersion;

        auto rollbackAsset = makeRegistrationRollbackAsset();
        auto rollbackPlan = nr::scene::SceneBridge::buildPlan(rollbackAsset);
        nr::test::require(rollbackPlan.valid(), "late rollback fixture must pass structural preflight");
        nr::test::requireEqual(rollbackPlan.textures[0].canonicalKey, sharedOwnerPlan.textures[0].canonicalKey,
                               "the failure must reuse the owner's shared texture");
        nr::test::requireEqual(rollbackPlan.textures[1].canonicalKey, rollbackPlan.textures[2].canonicalKey,
                               "the failure must exercise repeated-key creation journaling");

        auto const defaultMaterialKey =
            std::format("{}::material[default]", rollbackAsset.sourcePath.generic_string());
        auto const beforeFailureStatistics = scene.statistics();
        auto const beforeFailureRevisions = scene.revisionsSnapshot();

        auto failedTemplate = scene.registerTemplate(rollbackAsset);
        nr::test::require(!failedTemplate.valid(), "an empty late mesh must fail resource validation");
        requireEqualSceneStatistics(scene.statistics(), beforeFailureStatistics);
        nr::test::requireEqual(scene.revisionsSnapshot(), beforeFailureRevisions,
                               "a failed registration must not advance scene revisions");

        nr::test::require(!scene.findTextureHandleByStableKey(rollbackPlan.textures[1].canonicalKey).has_value(),
                          "the transaction-created repeated texture key must be removed");
        nr::test::require(!scene.findMaterialHandleByStableKey(rollbackPlan.materials[0].canonicalKey).has_value(),
                          "the first transaction-created material key must be removed");
        nr::test::require(!scene.findMaterialHandleByStableKey(rollbackPlan.materials[1].canonicalKey).has_value(),
                          "the second transaction-created material key must be removed");
        nr::test::require(!scene.findMaterialHandleByStableKey(defaultMaterialKey).has_value(),
                          "the transaction-created default material key must be removed");
        nr::test::require(!scene.findMeshHandleByStableKey(rollbackPlan.meshes[0].canonicalKey).has_value(),
                          "the transaction-created invalid mesh key must be removed");
        nr::test::require(!scene.findCameraHandleByStableKey(rollbackPlan.cameras[0].canonicalKey).has_value(),
                          "a camera created after an earlier mesh error must be removed");
        nr::test::require(!scene.findLightHandleByStableKey(rollbackPlan.lights[0].canonicalKey).has_value(),
                          "a light created after an earlier mesh error must be removed");

        auto sharedHandleAfterFailure =
            scene.findTextureHandleByStableKey(sharedOwnerPlan.textures[0].canonicalKey);
        nr::test::require(sharedHandleAfterFailure.has_value(), "the pre-existing shared texture must survive");
        nr::test::requireEqual(*sharedHandleAfterFailure, *sharedHandle,
                               "rollback must preserve the pre-existing shared handle");
        auto sharedRecordAfterFailure = scene.tryGetTextureAsset(*sharedHandleAfterFailure);
        nr::test::require(sharedRecordAfterFailure.has_value(), "the shared record must remain accessible");
        nr::test::requireEqual(sharedRecordAfterFailure->get().liveTemplatePins, sharedPinsBefore,
                               "rollback must preserve the shared template pin count");
        nr::test::requireEqual(sharedRecordAfterFailure->get().cpuVersion, sharedCpuVersionBefore,
                               "rollback must not rewrite the shared CPU payload");

        rollbackAsset.meshes[0].vertices = {
            nr::load::VertexAsset{.position = {-1.0f, -1.0f, 0.0f}},
            nr::load::VertexAsset{.position = {1.0f, -1.0f, 0.0f}},
            nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
        };
        auto retriedTemplate = scene.registerTemplate(rollbackAsset);
        nr::test::require(retriedTemplate.valid(),
                          "the same template and asset stable keys must succeed after rollback clears the error latch");

        auto const afterRetryStatistics = scene.statistics();
        nr::test::requireEqual(afterRetryStatistics.templateCount, beforeFailureStatistics.templateCount + 1u);
        nr::test::requireEqual(afterRetryStatistics.textureAssetCount,
                               beforeFailureStatistics.textureAssetCount + 1u,
                               "the repeated source key must create exactly one texture record");
        nr::test::requireEqual(afterRetryStatistics.materialAssetCount,
                               beforeFailureStatistics.materialAssetCount + 3u,
                               "two source materials and one default material must be retained");
        nr::test::requireEqual(afterRetryStatistics.meshAssetCount, beforeFailureStatistics.meshAssetCount + 1u);
        nr::test::requireEqual(afterRetryStatistics.cameraAssetCount, beforeFailureStatistics.cameraAssetCount + 1u);
        nr::test::requireEqual(afterRetryStatistics.lightAssetCount, beforeFailureStatistics.lightAssetCount + 1u);
        nr::test::require(scene.revisionsSnapshot() != beforeFailureRevisions,
                          "a successful retry must advance the template registration revision");

        nr::test::require(scene.findTextureHandleByStableKey(rollbackPlan.textures[1].canonicalKey).has_value(),
                          "the retried repeated texture key must resolve");
        nr::test::require(scene.findMaterialHandleByStableKey(defaultMaterialKey).has_value(),
                          "the retried default material key must resolve");
        nr::test::require(scene.findMeshHandleByStableKey(rollbackPlan.meshes[0].canonicalKey).has_value(),
                          "the retried mesh key must resolve");
        nr::test::require(scene.findCameraHandleByStableKey(rollbackPlan.cameras[0].canonicalKey).has_value(),
                          "the retried camera key must resolve");
        nr::test::require(scene.findLightHandleByStableKey(rollbackPlan.lights[0].canonicalKey).has_value(),
                          "the retried light key must resolve");
    }};

const nr::test::CaseRegistrar textureIntentRegistrationIdentityCase{
    "scene texture registry distinguishes final sampling intent and stably deduplicates equal intent", [] {
        constexpr auto baseColor = nr::resource::MaterialTextureSlotSemantic::baseColor;
        constexpr auto normal = nr::resource::MaterialTextureSlotSemantic::normal;

        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto srgbAsset = makeTextureIntentRegistrationAsset("srgb", std::array{baseColor});
        auto linearAsset = makeTextureIntentRegistrationAsset("linear", std::array{normal});
        auto srgbDedupAsset = makeTextureIntentRegistrationAsset("srgb_dedup", std::array{baseColor});
        auto mixedAsset = makeTextureIntentRegistrationAsset("mixed", std::array{baseColor, normal});

        auto srgbPlan = nr::scene::SceneBridge::buildPlan(srgbAsset);
        auto linearPlan = nr::scene::SceneBridge::buildPlan(linearAsset);
        auto srgbDedupPlan = nr::scene::SceneBridge::buildPlan(srgbDedupAsset);
        auto mixedPlan = nr::scene::SceneBridge::buildPlan(mixedAsset);
        nr::test::require(srgbPlan.valid() && linearPlan.valid() && srgbDedupPlan.valid() && mixedPlan.valid(),
                          "texture intent registration fixtures should preflight successfully");
        nr::test::require(srgbPlan.textures[0].canonicalKey != linearPlan.textures[0].canonicalKey,
                          "different final intents need different canonical keys");
        nr::test::requireEqual(srgbPlan.textures[0].canonicalKey, srgbDedupPlan.textures[0].canonicalKey);
        nr::test::requireEqual(linearPlan.textures[0].canonicalKey, mixedPlan.textures[0].canonicalKey);
        nr::test::require(mixedPlan.textures[0].mixedColorAndLinearReferences,
                          "mixed intent should be recorded before registration");

        nr::test::require(scene.registerTemplate(srgbAsset).valid(), "sRGB template should register");
        nr::test::require(scene.registerTemplate(linearAsset).valid(), "linear template should register");
        nr::test::require(scene.registerTemplate(srgbDedupAsset).valid(), "same-intent template should register");
        nr::test::require(scene.registerTemplate(mixedAsset).valid(), "mixed-intent template should register");

        auto srgbHandle = scene.findTextureHandleByStableKey(srgbPlan.textures[0].canonicalKey);
        auto linearHandle = scene.findTextureHandleByStableKey(linearPlan.textures[0].canonicalKey);
        auto srgbDedupHandle = scene.findTextureHandleByStableKey(srgbDedupPlan.textures[0].canonicalKey);
        auto mixedHandle = scene.findTextureHandleByStableKey(mixedPlan.textures[0].canonicalKey);
        nr::test::require(srgbHandle.has_value() && linearHandle.has_value() && srgbDedupHandle.has_value() &&
                              mixedHandle.has_value(),
                          "all texture intent identities should resolve from the registry");
        nr::test::require(*srgbHandle != *linearHandle,
                          "the same base key with different intent must allocate distinct handles");
        nr::test::requireEqual(*srgbHandle, *srgbDedupHandle,
                               "the same base key and sRGB intent should deduplicate stably");
        nr::test::requireEqual(*linearHandle, *mixedHandle,
                               "mixed references should force and deduplicate with the linear identity");

        auto srgbRecord = scene.tryGetTextureAsset(*srgbHandle);
        auto linearRecord = scene.tryGetTextureAsset(*linearHandle);
        nr::test::require(srgbRecord.has_value() && linearRecord.has_value(),
                          "both texture intent records should remain registered");
        nr::test::require(srgbRecord->get().cpu.srgb && !linearRecord->get().cpu.srgb,
                          "texture resource sampling flags should match final plan intent");
        nr::test::requireEqual(srgbRecord->get().cpu.format, vk::Format::eR8G8B8A8Srgb);
        nr::test::requireEqual(linearRecord->get().cpu.format, vk::Format::eR8G8B8A8Unorm);
    }};
} // namespace
