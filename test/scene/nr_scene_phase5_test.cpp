import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(nr::scene::Scene &scene,
                       const nr::scene::SceneExtractProfileCreateInfo &profileCreateInfo,
                       nr::scene::SceneExtractProfileHandle profileHandle,
                       const nr::scene::SceneExtractInput &extractInput) {
    { scene.registerExtractProfile(profileCreateInfo) } -> std::same_as<nr::scene::SceneExtractProfileHandle>;
    scene.destroyExtractProfile(profileHandle);
    { std::as_const(scene).extractPackets(profileHandle, extractInput) } -> std::same_as<nr::scene::ScenePacketSet>;
    { std::as_const(scene).tryGetPrimaryCamera() } -> std::same_as<std::optional<nr::scene::SceneResolvedCamera>>;
});

static_assert(requires {
    nr::scene::ScenePacketDomain::rasterDraw;
    nr::scene::ScenePacketDomain::rayTracingInstance;
    nr::scene::ScenePacketDomain::tlasBuildInput;
    nr::scene::SceneVisibilityMode::none;
    nr::scene::SceneVisibilityMode::primaryCameraFrustum;
    nr::scene::SceneVisibilityMode::customFrustum;
    nr::scene::SceneSelectionMask{};
    nr::scene::SceneFrustum{};
    nr::scene::SceneExtractProfileCreateInfo{};
    nr::scene::SceneExtractInput{};
    nr::scene::RasterDrawPacket{};
    nr::scene::RayTracingInstancePacket{};
    nr::scene::TlasBuildInputPacket{};
    nr::scene::SceneResolvedCamera{};
    nr::scene::ScenePacketSet{};
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

[[nodiscard]] std::array<float, 16> translatedTransform(float tx, float ty, float tz)
{
    return {
        1.0f, 0.0f, 0.0f, tx,
        0.0f, 1.0f, 0.0f, ty,
        0.0f, 0.0f, 1.0f, tz,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset buildPhase5MinimalSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"manual_phase5_scene.gltf"};

    auto material = nr::load::MaterialAsset{};
    material.name = "phase5_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "phase5_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {0.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {1.0f, 0.0f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 1.0f, 0.0f}},
    };
    mesh.indices = {0, 1, 2};
    scene.meshes.push_back(std::move(mesh));

    scene.nodes.resize(2);
    scene.rootNodeIndex = 0;

    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1};
    scene.nodes[0].localTransform = identityTransform();

    scene.nodes[1].name = "MeshNode";
    scene.nodes[1].parentIndex = 0;
    scene.nodes[1].meshIndices = {0};
    scene.nodes[1].localTransform = translatedTransform(0.0f, 0.0f, -2.0f);

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

[[nodiscard]] nr::scene::SceneFrustum buildRejectAllFrustum()
{
    auto frustum = nr::scene::SceneFrustum{};
    frustum.planes[0] = glm::vec4{1.0f, 0.0f, 0.0f, -1000.0f};
    return frustum;
}

[[nodiscard]] bool checkStaticCompileContracts()
{
    std::println("\n=== Case: checkStaticCompileContracts ===");
    return true;
}

[[nodiscard]] bool checkMinimalDataFlowAndPacketExtraction()
{
    std::println("\n=== Case: checkMinimalDataFlowAndPacketExtraction ===");

    auto sceneAsset = buildPhase5MinimalSceneAsset();

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!require(templateHandle.valid(), "Template handle should be valid for phase5 minimal scene."))
    {
        return false;
    }

    auto activeInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                .activate = true,
                                                            });
    auto inactiveInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{
                                                                  .activate = false,
                                                              });

    if (!require(activeInstance.valid(), "Active instance should be valid."))
    {
        return false;
    }
    if (!require(inactiveInstance.valid(), "Inactive instance should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto activeOnlyProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "phase5_active_raster",
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    auto allInstancesProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "phase5_all_raster",
        .domain = nr::scene::ScenePacketDomain::rasterDraw,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rasterOpaque),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = false,
    });

    auto rtMainProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "phase5_rt_main",
        .domain = nr::scene::ScenePacketDomain::rayTracingInstance,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    auto tlasBuildProfile = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
        .debugName = "phase5_tlas_build",
        .domain = nr::scene::ScenePacketDomain::tlasBuildInput,
        .selection = nr::scene::SceneSelectionMask{
            .requireAll = nr::scene::sceneSelectionMask(nr::scene::SceneSelectionBit::rtMain),
        },
        .requireReadyForDomain = false,
        .requireActiveInstances = true,
    });

    if (!require(activeOnlyProfile.valid(), "Active-only extraction profile should be valid."))
    {
        return false;
    }
    if (!require(allInstancesProfile.valid(), "All-instances extraction profile should be valid."))
    {
        return false;
    }
    if (!require(rtMainProfile.valid(), "RT profile should be valid."))
    {
        return false;
    }
    if (!require(tlasBuildProfile.valid(), "TLAS-build profile should be valid."))
    {
        return false;
    }

    auto primaryCamera = scene.tryGetPrimaryCamera();
    if (!require(primaryCamera.has_value(), "Primary camera should resolve with fallback when scene has no imported camera."))
    {
        return false;
    }
    if (!require(primaryCamera->fallback, "Primary camera should be marked as fallback for camera-less scene."))
    {
        return false;
    }

    auto activePackets = scene.extractPackets(activeOnlyProfile);
    auto allPackets = scene.extractPackets(allInstancesProfile);
    auto rtPackets = scene.extractPackets(rtMainProfile);
    auto tlasPackets = scene.extractPackets(tlasBuildProfile);
    auto primaryFrustumPackets = scene.extractPackets(activeOnlyProfile, nr::scene::SceneExtractInput{
                                                                           .visibility = nr::scene::SceneVisibilityMode::primaryCameraFrustum,
                                                                       });

    if (!require(activePackets.domain == nr::scene::ScenePacketDomain::rasterDraw,
                 "Active-only profile should return raster packet domain."))
    {
        return false;
    }
    if (!require(rtPackets.domain == nr::scene::ScenePacketDomain::rayTracingInstance,
                 "RT profile should return rayTracingInstance packet domain."))
    {
        return false;
    }
    if (!require(tlasPackets.domain == nr::scene::ScenePacketDomain::tlasBuildInput,
                 "TLAS profile should return tlasBuildInput packet domain."))
    {
        return false;
    }

    if (!require(!activePackets.rasterDraws.empty(), "Active-only extraction should produce raster packets."))
    {
        return false;
    }
    if (!require(allPackets.rasterDraws.size() > activePackets.rasterDraws.size(),
                 "All-instances profile should include packets from inactive instance."))
    {
        return false;
    }
    if (!require(!rtPackets.rtInstances.empty(), "RT profile should produce ray-tracing packets."))
    {
        return false;
    }
    if (!require(rtPackets.tlasBuildInputs.empty(), "rayTracingInstance domain should not emit tlasBuildInputs."))
    {
        return false;
    }
    if (!require(!tlasPackets.tlasBuildInputs.empty(), "TLAS profile should produce tlasBuildInputs packets."))
    {
        return false;
    }
    if (!require(tlasPackets.rtInstances.empty(), "tlasBuildInput domain should not emit rtInstances."))
    {
        return false;
    }
    if (!require(!primaryFrustumPackets.rasterDraws.empty(),
                 "primaryCameraFrustum extraction should use resolved primary camera frustum."))
    {
        return false;
    }

    if (!require(std::ranges::is_sorted(activePackets.rasterDraws, {}, &nr::scene::RasterDrawPacket::sortKey),
                 "Raster packets should be sorted by sortKey."))
    {
        return false;
    }

    auto firstRaster = activePackets.rasterDraws.front();
    if (!require(firstRaster.mesh.valid(), "Raster packet mesh handle should be valid."))
    {
        return false;
    }
    if (!require(firstRaster.material.valid(), "Raster packet material handle should be valid."))
    {
        return false;
    }
    if (!require(firstRaster.renderable.is_alive(), "Raster packet renderable should be alive."))
    {
        return false;
    }

    auto firstRt = rtPackets.rtInstances.front();
    if (!require(firstRt.mesh.valid(), "RT packet mesh handle should be valid."))
    {
        return false;
    }
    if (!require(firstRt.instanceMask != 0u, "RT packet instanceMask should not be zero."))
    {
        return false;
    }

    auto rejectFrustum = buildRejectAllFrustum();
    auto frustumFilteredPackets = scene.extractPackets(activeOnlyProfile, nr::scene::SceneExtractInput{
                                                                            .visibility = nr::scene::SceneVisibilityMode::customFrustum,
                                                                            .customFrustum = rejectFrustum,
                                                                        });
    if (!require(frustumFilteredPackets.rasterDraws.empty(),
                 "Reject-all frustum should filter out all raster packets."))
    {
        return false;
    }

    auto missingPartitionPackets = scene.extractPackets(activeOnlyProfile, nr::scene::SceneExtractInput{
                                                                             .partitionOverride = 1024u,
                                                                         });
    if (!require(missingPartitionPackets.rasterDraws.empty(),
                 "Unknown partition override should filter out all raster packets."))
    {
        return false;
    }

    scene.destroyExtractProfile(activeOnlyProfile);
    scene.destroyExtractProfile(allInstancesProfile);
    scene.destroyExtractProfile(rtMainProfile);
    scene.destroyExtractProfile(tlasBuildProfile);

    auto afterDestroyPackets = scene.extractPackets(activeOnlyProfile);
    if (!require(afterDestroyPackets.rasterDraws.empty() &&
                     afterDestroyPackets.rtInstances.empty() &&
                     afterDestroyPackets.tlasBuildInputs.empty(),
                 "Destroyed extraction profile should produce empty packets."))
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
        std::pair{"checkMinimalDataFlowAndPacketExtraction", &checkMinimalDataFlowAndPacketExtraction},
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
