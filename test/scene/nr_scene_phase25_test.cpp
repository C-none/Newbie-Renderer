import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(const nr::scene::SceneBridgePlan &plan) {
    plan.cameras.size();
    plan.lights.size();
});

static_assert(requires(const nr::scene::SceneStatistics &stats) {
    stats.cameraAssetCount;
    stats.lightAssetCount;
    stats.templateCameraBindingCount;
    stats.templateLightBindingCount;
});

static_assert(requires(const nr::scene::SceneTemplateRecord &record) {
    record.pins.cameras.size();
    record.pins.lights.size();
    record.templateCameraBindingCount;
    record.templateLightBindingCount;
});

static_assert(requires(nr::scene::Scene &scene,
                       nr::resource::CameraAssetHandle cameraHandle,
                       nr::resource::LightAssetHandle lightHandle,
                       std::string_view stableKey) {
    scene.tryGetCameraAsset(cameraHandle);
    scene.tryGetLightAsset(lightHandle);
    scene.findCameraHandleByStableKey(stableKey);
    scene.findLightHandleByStableKey(stableKey);
});

static_assert(std::same_as<
              decltype(nr::scene::SceneBridge::makeCameraCanonicalKey(std::declval<const nr::load::SceneAsset &>(), std::uint32_t{0})),
              std::string>);

static_assert(std::same_as<
              decltype(nr::scene::SceneBridge::makeLightCanonicalKey(std::declval<const nr::load::SceneAsset &>(), std::uint32_t{0})),
              std::string>);

[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }
    return true;
}

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-5f)
{
    return std::abs(lhs - rhs) <= epsilon;
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

[[nodiscard]] nr::load::SceneAsset buildPhase25SceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_phase25_scene.gltf"};
    scene.rootNodeIndex = 0;

    scene.nodes.resize(3);

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1, 2};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "CameraNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].localTransform = identityTransform();

    scene.nodes[2].name = "LightNode";
    scene.nodes[2].parentIndex = 0;
    scene.nodes[2].localTransform = identityTransform();

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "CameraPerspective",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 1,
        .horizontalFov = glm::radians(90.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.5f,
        .farPlane = 250.0f,
        .orthographicWidth = 0.0f,
    });

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "CameraOrtho",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 1,
        .horizontalFov = glm::radians(50.0f),
        .aspect = 2.0f,
        .nearPlane = 0.2f,
        .farPlane = 50.0f,
        .orthographicWidth = 20.0f,
    });

    scene.cameras.push_back(nr::load::CameraAsset{
        .name = "CameraInvalidNode",
        .sourceNodeName = "MissingNode",
        .nodeIndex = 999,
        .horizontalFov = glm::radians(75.0f),
        .aspect = 1.0f,
        .nearPlane = 0.1f,
        .farPlane = 100.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "LightDirectional",
        .sourceNodeName = "LightNode",
        .nodeIndex = 2,
        .typeRaw = 1,
        .type = "directional",
        .colorDiffuse = {1.0f, 0.9f, 0.8f},
        .attenuationConstant = 1.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "LightSpot",
        .sourceNodeName = "LightNode",
        .nodeIndex = 2,
        .typeRaw = 3,
        .type = "spot",
        .colorDiffuse = {0.6f, 0.7f, 1.0f},
        .attenuationQuadratic = 0.04f,
        .innerCone = 0.2f,
        .outerCone = 0.8f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "LightAmbientUnsupported",
        .sourceNodeName = "LightNode",
        .nodeIndex = 2,
        .typeRaw = 4,
        .type = "ambient",
        .colorAmbient = {0.2f, 0.2f, 0.2f},
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "LightPointInvalidNode",
        .sourceNodeName = "MissingNode",
        .nodeIndex = 777,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {0.7f, 0.5f, 0.2f},
        .attenuationLinear = 0.2f,
    });

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());

    return scene;
}

[[nodiscard]] std::size_t countDescendants(const flecs::world &world, flecs::entity_t parentId)
{
    auto recurse = [&](auto &&self, flecs::entity_t currentParent) -> std::size_t {
        auto total = std::size_t{0};
        auto iterator = ecs_children(world.c_ptr(), currentParent);
        while (ecs_children_next(&iterator))
        {
            auto indices = std::views::iota(0, iterator.count);
            std::ranges::for_each(indices, [&](int index) {
                auto childId = iterator.entities[index];
                ++total;
                total += self(self, childId);
            });
        }

        return total;
    };

    return recurse(recurse, parentId);
}

[[nodiscard]] std::pair<std::size_t, std::size_t> countTemplateCameraAndLightBindings(const flecs::world &world, flecs::entity_t rootId)
{
    auto cameraBindingCount = std::size_t{0};
    auto lightBindingCount = std::size_t{0};

    auto recurse = [&](auto &&self, flecs::entity_t currentParent) -> void {
        auto iterator = ecs_children(world.c_ptr(), currentParent);
        while (ecs_children_next(&iterator))
        {
            auto indices = std::views::iota(0, iterator.count);
            std::ranges::for_each(indices, [&](int index) {
                auto child = flecs::entity{world.c_ptr(), iterator.entities[index]};
                if (child.try_get<nr::scene::SceneTemplateCameraBindingRef>() != nullptr)
                {
                    ++cameraBindingCount;
                }
                if (child.try_get<nr::scene::SceneTemplateLightBindingRef>() != nullptr)
                {
                    ++lightBindingCount;
                }

                self(self, child.id());
            });
        }
    };

    recurse(recurse, rootId);
    return {cameraBindingCount, lightBindingCount};
}

[[nodiscard]] bool checkBridgePlanCameraLightCoverage()
{
    std::println("\n=== Case: checkBridgePlanCameraLightCoverage ===");
    auto sceneAsset = buildPhase25SceneAsset();

    auto plan = nr::scene::SceneBridge::buildPlan(sceneAsset);

    if (!require(plan.valid(), "Bridge plan should be valid when sourcePath is provided."))
    {
        return false;
    }

    if (!require(plan.cameras.size() == sceneAsset.cameras.size(), "Bridge plan camera count mismatch."))
    {
        return false;
    }
    if (!require(plan.lights.size() == sceneAsset.lights.size(), "Bridge plan light count mismatch."))
    {
        return false;
    }

    auto cameraKeysMatch = std::ranges::all_of(std::views::iota(std::size_t{0}, plan.cameras.size()), [&](std::size_t index) {
        auto expected = nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, static_cast<std::uint32_t>(index));
        return plan.cameras[index].canonicalKey == expected;
    });

    if (!require(cameraKeysMatch, "Camera canonical keys should match SceneBridge convention."))
    {
        return false;
    }

    auto lightKeysMatch = std::ranges::all_of(std::views::iota(std::size_t{0}, plan.lights.size()), [&](std::size_t index) {
        auto expected = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, static_cast<std::uint32_t>(index));
        return plan.lights[index].canonicalKey == expected;
    });

    if (!require(lightKeysMatch, "Light canonical keys should match SceneBridge convention."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkCameraLightRegistrationAndTemplateBindings()
{
    std::println("\n=== Case: checkCameraLightRegistrationAndTemplateBindings ===");
    auto sceneAsset = buildPhase25SceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);

    if (!require(templateHandle.valid(), "Template handle must be valid for phase2.5 scene asset."))
    {
        return false;
    }

    auto statistics = scene.statistics();
    std::println("[stats] templates={} instances={} cameraAssets={} lightAssets={} templateCameraBindings={} templateLightBindings={}",
                 statistics.templateCount,
                 statistics.instanceCount,
                 statistics.cameraAssetCount,
                 statistics.lightAssetCount,
                 statistics.templateCameraBindingCount,
                 statistics.templateLightBindingCount);

    if (!require(statistics.templateCount == 1, "Template count should be 1 after registration."))
    {
        return false;
    }
    if (!require(statistics.cameraAssetCount == 3, "Camera asset count should include perspective/ortho/invalid-node camera records."))
    {
        return false;
    }
    if (!require(statistics.lightAssetCount == 3, "Light asset count should skip unsupported light type and keep supported records."))
    {
        return false;
    }
    if (!require(statistics.templateCameraBindingCount == 2, "Only node-resolved cameras should produce template bindings."))
    {
        return false;
    }
    if (!require(statistics.templateLightBindingCount == 2, "Only node-resolved supported lights should produce template bindings."))
    {
        return false;
    }

    auto perspectiveCameraKey = nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0);
    auto perspectiveCameraHandle = scene.findCameraHandleByStableKey(perspectiveCameraKey);
    if (!require(perspectiveCameraHandle.has_value(), "Perspective camera key should resolve in Scene registry."))
    {
        return false;
    }

    auto perspectiveCameraRecord = scene.tryGetCameraAsset(*perspectiveCameraHandle);
    if (!require(perspectiveCameraRecord.has_value(), "Perspective camera record should be retrievable."))
    {
        return false;
    }

    auto expectedVerticalFov = 2.0f * std::atan(std::tan(sceneAsset.cameras[0].horizontalFov * 0.5f) / sceneAsset.cameras[0].aspect);
    if (!require(perspectiveCameraRecord->get().cpu.projection == nr::resource::CameraProjection::perspective, "CameraPerspective should map to perspective projection."))
    {
        return false;
    }
    if (!require(almostEqual(perspectiveCameraRecord->get().cpu.verticalFovRadians, expectedVerticalFov), "Horizontal FOV should be converted to vertical FOV for perspective camera."))
    {
        return false;
    }
    if (!require(perspectiveCameraRecord->get().cpu.authoredAspectRatio.has_value(),
                 "Perspective camera should preserve authored aspect ratio metadata."))
    {
        return false;
    }
    if (!require(almostEqual(*perspectiveCameraRecord->get().cpu.authoredAspectRatio, sceneAsset.cameras[0].aspect),
                 "Perspective camera authored aspect ratio metadata mismatch."))
    {
        return false;
    }

    auto orthoCameraKey = nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 1);
    auto orthoCameraHandle = scene.findCameraHandleByStableKey(orthoCameraKey);
    if (!require(orthoCameraHandle.has_value(), "Orthographic camera key should resolve in Scene registry."))
    {
        return false;
    }

    auto orthoCameraRecord = scene.tryGetCameraAsset(*orthoCameraHandle);
    if (!require(orthoCameraRecord.has_value(), "Orthographic camera record should be retrievable."))
    {
        return false;
    }

    if (!require(orthoCameraRecord->get().cpu.projection == nr::resource::CameraProjection::orthographic, "CameraOrtho should map to orthographic projection."))
    {
        return false;
    }
    if (!require(almostEqual(orthoCameraRecord->get().cpu.orthoHeight, 10.0f), "Orthographic width/aspect should map to orthoHeight."))
    {
        return false;
    }
    if (!require(orthoCameraRecord->get().cpu.authoredAspectRatio.has_value(),
                 "Orthographic camera should preserve authored aspect ratio metadata."))
    {
        return false;
    }
    if (!require(almostEqual(*orthoCameraRecord->get().cpu.authoredAspectRatio, sceneAsset.cameras[1].aspect),
                 "Orthographic camera authored aspect ratio metadata mismatch."))
    {
        return false;
    }

    auto unsupportedLightKey = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 2);
    if (!require(!scene.findLightHandleByStableKey(unsupportedLightKey).has_value(), "Unsupported light type should not enter light registry."))
    {
        return false;
    }

    auto directionalLightKey = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0);
    auto directionalLightHandle = scene.findLightHandleByStableKey(directionalLightKey);
    if (!require(directionalLightHandle.has_value(), "Directional light key should resolve in Scene registry."))
    {
        return false;
    }

    auto directionalLightRecord = scene.tryGetLightAsset(*directionalLightHandle);
    if (!require(directionalLightRecord.has_value(), "Directional light record should be retrievable."))
    {
        return false;
    }
    if (!require(directionalLightRecord->get().cpu.type == nr::resource::LightType::directional, "Directional light type mapping mismatch."))
    {
        return false;
    }

    auto spotLightKey = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 1);
    auto spotLightHandle = scene.findLightHandleByStableKey(spotLightKey);
    if (!require(spotLightHandle.has_value(), "Spot light key should resolve in Scene registry."))
    {
        return false;
    }

    auto spotLightRecord = scene.tryGetLightAsset(*spotLightHandle);
    if (!require(spotLightRecord.has_value(), "Spot light record should be retrievable."))
    {
        return false;
    }
    if (!require(spotLightRecord->get().cpu.type == nr::resource::LightType::spot, "Spot light type mapping mismatch."))
    {
        return false;
    }
    if (!require(almostEqual(spotLightRecord->get().cpu.innerConeRadians, sceneAsset.lights[1].innerCone), "Spot inner cone mapping mismatch."))
    {
        return false;
    }
    if (!require(almostEqual(spotLightRecord->get().cpu.outerConeRadians, sceneAsset.lights[1].outerCone), "Spot outer cone mapping mismatch."))
    {
        return false;
    }

    auto invalidNodePointLightKey = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 3);
    if (!require(scene.findLightHandleByStableKey(invalidNodePointLightKey).has_value(), "Supported light with invalid node should still be registered."))
    {
        return false;
    }

    auto templateRecord = scene.tryGetTemplate(templateHandle);
    if (!require(templateRecord.has_value(), "Template record should exist after phase2.5 registration."))
    {
        return false;
    }

    if (!require(templateRecord->get().pins.cameras.size() == 3, "Template camera pins should include all registered camera assets."))
    {
        return false;
    }
    if (!require(templateRecord->get().pins.lights.size() == 3, "Template light pins should include all registered light assets."))
    {
        return false;
    }

    auto [cameraBindingsInTree, lightBindingsInTree] = countTemplateCameraAndLightBindings(scene.ecs(), templateRecord->get().prefabRoot.id());
    if (!require(cameraBindingsInTree == 2, "Template hierarchy should contain 2 camera binding entities."))
    {
        return false;
    }
    if (!require(lightBindingsInTree == 2, "Template hierarchy should contain 2 light binding entities."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance handle should be valid for phase2.5 template."))
    {
        return false;
    }

    auto instanceRecord = scene.tryGetInstance(instanceHandle);
    if (!require(instanceRecord.has_value(), "Instance record should exist after instantiate."))
    {
        return false;
    }

    auto expectedDescendants = templateRecord->get().templateNodeCount +
                               templateRecord->get().templateMeshBindingCount +
                               templateRecord->get().templateCameraBindingCount +
                               templateRecord->get().templateLightBindingCount;
    auto actualDescendants = countDescendants(scene.ecs(), instanceRecord->get().root.id());

    std::println("[instance] expectedDescendants={} actualDescendants={} expectedEntityCount={} actualExpectedEntityCount={}",
                 expectedDescendants,
                 actualDescendants,
                 expectedDescendants + 1,
                 instanceRecord->get().expectedEntityCount);

    if (!require(actualDescendants == expectedDescendants, "Instance descendants should include camera/light bindings in addition to template nodes."))
    {
        return false;
    }
    if (!require(instanceRecord->get().expectedEntityCount == expectedDescendants + 1, "Instance expectedEntityCount should include root + descendants."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkStaticCompileContracts()
{
    std::println("\n=== Case: checkStaticCompileContracts ===");
    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkStaticCompileContracts", &checkStaticCompileContracts},
        std::pair{"checkBridgePlanCameraLightCoverage", &checkBridgePlanCameraLightCoverage},
        std::pair{"checkCameraLightRegistrationAndTemplateBindings", &checkCameraLightRegistrationAndTemplateBindings},
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
