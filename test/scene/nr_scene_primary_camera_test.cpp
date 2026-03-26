import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(const nr::scene::Scene &scene,
                       nr::scene::SceneExtractProfileHandle profile,
                       const nr::scene::SceneExtractInput &input) {
    { scene.tryGetPrimaryCamera() } -> std::same_as<std::optional<nr::scene::SceneResolvedCamera>>;
    { scene.extractPackets(profile, input) } -> std::same_as<nr::scene::ScenePacketSet>;
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

[[nodiscard]] bool almostEqual(float lhs, float rhs, float epsilon = 1e-4f)
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

[[nodiscard]] std::array<float, 16> translatedTransform(float tx, float ty, float tz)
{
    return {
        1.0f, 0.0f, 0.0f, tx,
        0.0f, 1.0f, 0.0f, ty,
        0.0f, 0.0f, 1.0f, tz,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildSceneAssetWithCameraCount(std::size_t cameraCount)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::format("manual_primary_camera_{}.gltf", cameraCount)};

    auto material = nr::load::MaterialAsset{};
    material.name = "primary_camera_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "primary_camera_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    auto nodeCount = cameraCount > 0 ? 3u : 2u;
    scene.nodes.resize(nodeCount);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = cameraCount > 0 ? std::vector<std::uint32_t>{1, 2} : std::vector<std::uint32_t>{1};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = translatedTransform(0.0f, 0.0f, -3.0f);

    if (cameraCount > 0)
    {
        scene.nodes[2].name = "CameraNode";
        scene.nodes[2].parentIndex = 0;
        scene.nodes[2].localTransform = identityTransform();

        auto cameraIndices = std::views::iota(std::size_t{0}, cameraCount);
        std::ranges::for_each(cameraIndices, [&](std::size_t cameraIndex) {
            scene.cameras.push_back(nr::load::CameraAsset{
                .name = std::format("PrimaryCamera{}", cameraIndex),
                .sourceNodeName = "CameraNode",
                .nodeIndex = 2,
                .lookAt = {0.0f, 0.0f, -1.0f},
                .up = {0.0f, 1.0f, 0.0f},
                .horizontalFov = glm::radians(90.0f),
                .aspect = 1.0f,
                .nearPlane = 0.1f,
                .farPlane = 500.0f,
                .orthographicWidth = 0.0f,
            });
        });
    }

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

[[nodiscard]] nr::scene::SceneExtractProfileHandle registerRasterProfile(nr::scene::Scene &scene)
{
    return scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "primary_camera_raster_profile",
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });
}

[[nodiscard]] bool checkFallbackCameraWhenNoImportedCamera()
{
    std::println("\n=== Case: checkFallbackCameraWhenNoImportedCamera ===");

    auto sceneAsset = buildSceneAssetWithCameraCount(0);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed for no-camera scene."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance should be valid for no-camera scene."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto const statsBeforeQuery = scene.statistics();
    auto primaryCamera = std::as_const(scene).tryGetPrimaryCamera();
    if (!require(primaryCamera.has_value(), "Fallback primary camera should resolve for scene without imported cameras."))
    {
        return false;
    }
    if (!require(primaryCamera->fallback, "Resolved primary camera should be marked as fallback."))
    {
        return false;
    }

    auto const statsAfterQuery = scene.statistics();
    if (!require(statsBeforeQuery.cameraAssetCount == statsAfterQuery.cameraAssetCount,
                 "Const primary camera query should not mutate fallback camera asset state."))
    {
        return false;
    }

    auto rasterProfile = registerRasterProfile(scene);
    if (!require(rasterProfile.valid(), "Raster extraction profile should be valid."))
    {
        return false;
    }

    auto const statsBeforeExtract = scene.statistics();
    auto packets = scene.extractPackets(rasterProfile, nr::scene::SceneExtractInput{
                                                           .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                       });

    if (!require(!packets.rasterDraws.empty(), "Fallback primary camera frustum should keep visible mesh packets."))
    {
        return false;
    }

    auto const statsAfterExtract = scene.statistics();
    if (!require(statsBeforeExtract.cameraAssetCount == statsAfterExtract.cameraAssetCount,
                 "Const primaryCameraFrustum extraction should not mutate fallback camera asset state."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkImportedCameraPreferredAndDeterministic()
{
    std::println("\n=== Case: checkImportedCameraPreferredAndDeterministic ===");

    auto sceneAsset = buildSceneAssetWithCameraCount(2);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed for imported-camera scene."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance should be valid for imported-camera scene."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto primaryCameraA = scene.tryGetPrimaryCamera();
    auto primaryCameraB = scene.tryGetPrimaryCamera();

    if (!require(primaryCameraA.has_value(), "Imported primary camera should resolve."))
    {
        return false;
    }
    if (!require(primaryCameraB.has_value(), "Repeated primary camera resolution should resolve."))
    {
        return false;
    }
    if (!require(!primaryCameraA->fallback, "Imported camera should be preferred over fallback."))
    {
        return false;
    }
    if (!require(primaryCameraA->entity.id() == primaryCameraB->entity.id(), "Primary camera resolution should be deterministic."))
    {
        return false;
    }

    auto sourceBinding = primaryCameraA->entity.try_get<nr::scene::SceneTemplateCameraBindingRef>();
    if (!require(sourceBinding != nullptr, "Imported primary camera entity should preserve template binding metadata."))
    {
        return false;
    }
    if (!require(sourceBinding->sourceCameraIndex == 0u,
                 "Tie-break should prefer imported camera with the lowest sourceCameraIndex."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkInactiveInstanceCameraFallsBack()
{
    std::println("\n=== Case: checkInactiveInstanceCameraFallsBack ===");

    auto sceneAsset = buildSceneAssetWithCameraCount(1);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed."))
    {
        return false;
    }

    auto inactiveInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                  .activate = false,
                                                              });
    if (!require(inactiveInstance.valid(), "Inactive instance should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto primaryCamera = scene.tryGetPrimaryCamera();
    if (!require(primaryCamera.has_value(), "Primary camera should still resolve through fallback."))
    {
        return false;
    }
    if (!require(primaryCamera->fallback, "Inactive imported cameras should not be selected as primary."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkPrimaryCameraSwitchesWithInstanceLifetime()
{
    std::println("\n=== Case: checkPrimaryCameraSwitchesWithInstanceLifetime ===");

    auto sceneAsset = buildSceneAssetWithCameraCount(1);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed."))
    {
        return false;
    }

    auto firstInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                               .activate = true,
                                                           });
    if (!require(firstInstance.valid(), "First active instance should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto importedCamera = scene.tryGetPrimaryCamera();
    if (!require(importedCamera.has_value(), "Imported camera should resolve while an active instance exists."))
    {
        return false;
    }
    if (!require(!importedCamera->fallback, "Imported camera should be selected over fallback when active."))
    {
        return false;
    }

    scene.destroyInstance(firstInstance);
    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto fallbackCamera = scene.tryGetPrimaryCamera();
    if (!require(fallbackCamera.has_value(), "Fallback camera should resolve after destroying the only active instance."))
    {
        return false;
    }
    if (!require(fallbackCamera->fallback, "Fallback camera should be selected with no active imported camera."))
    {
        return false;
    }

    auto secondInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                .activate = true,
                                                            });
    if (!require(secondInstance.valid(), "Second active instance should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto importedCameraAgain = scene.tryGetPrimaryCamera();
    if (!require(importedCameraAgain.has_value(), "Imported camera should resolve again after re-instantiation."))
    {
        return false;
    }
    if (!require(!importedCameraAgain->fallback,
                 "Imported camera should regain priority after re-instantiation."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkPrimaryFrustumCullsFarInstance()
{
    std::println("\n=== Case: checkPrimaryFrustumCullsFarInstance ===");

    auto sceneAsset = buildSceneAssetWithCameraCount(1);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template registration should succeed."))
    {
        return false;
    }

    auto nearInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                             .rootTransform = glm::mat4{1.0f},
                                                             .activate = true,
                                                         });

    auto farTransform = glm::mat4{1.0f};
    farTransform[3] = glm::vec4{500.0f, 0.0f, 0.0f, 1.0f};
    auto farInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                            .rootTransform = farTransform,
                                                            .activate = true,
                                                        });

    if (!require(nearInstance.valid() && farInstance.valid(), "Both near/far instances should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto rasterProfile = registerRasterProfile(scene);
    if (!require(rasterProfile.valid(), "Raster profile should be valid for frustum culling case."))
    {
        return false;
    }

    auto noVisibilityPackets = scene.extractPackets(rasterProfile, nr::scene::SceneExtractInput{
                                                                       .visibility = nr::scene::SceneVisibilityMode::none,
                                                                   });
    auto primaryFrustumPackets = scene.extractPackets(rasterProfile, nr::scene::SceneExtractInput{
                                                                         .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                                     });

    if (!require(noVisibilityPackets.rasterDraws.size() == 2,
                 "Without frustum visibility override, both instances should be included."))
    {
        return false;
    }

    if (!require(primaryFrustumPackets.rasterDraws.size() == 1,
                 "primaryCameraFrustum should cull the far translated instance."))
    {
        return false;
    }

    auto keptX = primaryFrustumPackets.rasterDraws.front().world[3].x;
    if (!require(almostEqual(keptX, 0.0f),
                 "primaryCameraFrustum should keep the near-origin instance packet."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkFallbackCameraWhenNoImportedCamera", &checkFallbackCameraWhenNoImportedCamera},
        std::pair{"checkImportedCameraPreferredAndDeterministic", &checkImportedCameraPreferredAndDeterministic},
        std::pair{"checkInactiveInstanceCameraFallsBack", &checkInactiveInstanceCameraFallsBack},
        std::pair{"checkPrimaryCameraSwitchesWithInstanceLifetime", &checkPrimaryCameraSwitchesWithInstanceLifetime},
        std::pair{"checkPrimaryFrustumCullsFarInstance", &checkPrimaryFrustumCullsFarInstance},
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
