import std;
import dependency;
import nr.load;
import nr.rhi;
import nr.scene;

namespace
{
static_assert(requires(nr::scene::Scene &scene, nr::scene::SceneTemplateHandle handle) {
    { scene.destroyTemplate(handle) } -> std::same_as<nr::scene::DestroyTemplateResult>;
});

static_assert(requires {
    nr::scene::DestroyTemplateResult::destroyed;
    nr::scene::DestroyTemplateResult::notFound;
    nr::scene::DestroyTemplateResult::instancesAlive;
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

struct CanonicalKeys
{
    std::string mesh{};
    std::string material{};
    std::string texture{};
    std::string camera{};
    std::string light{};
};

[[nodiscard]] CanonicalKeys makeCanonicalKeys(const nr::load::SceneAsset &sceneAsset)
{
    return CanonicalKeys{
        .mesh = nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0),
        .material = nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0),
        .texture = nr::scene::SceneBridge::makeTextureCanonicalKey(sceneAsset.textures[0]),
        .camera = nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0),
        .light = nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0),
    };
}

[[nodiscard]] nr::load::SceneAsset buildTemplateReleaseSceneAsset(std::string_view sourcePath)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::string{sourcePath}};

    auto image = nr::load::Image{};
    image.width = 1;
    image.height = 1;
    image.channels = 4;
    image.pixels = {255, 255, 255, 255};

    auto texture = nr::load::TextureAsset{};
    texture.key = "manual://textures/template_release/baseColor";
    texture.resolvedPath = std::filesystem::path{"manual_textures/template_release_baseColor.png"};
    texture.decodedImage = image;
    scene.textures.push_back(std::move(texture));

    auto material = nr::load::MaterialAsset{};
    material.name = "template_release_material";
    material.textures.push_back(nr::load::MaterialTextureBinding{
        .textureIndex = 0,
        .uvChannel = 0,
        .textureTypeRaw = 0,
        .semantic = "diffuse",
    });
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "template_release_mesh";
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
        .name = "TemplateReleaseCamera",
        .sourceNodeName = "CameraNode",
        .nodeIndex = 2,
        .horizontalFov = glm::radians(75.0f),
        .aspect = 16.0f / 9.0f,
        .nearPlane = 0.1f,
        .farPlane = 200.0f,
        .orthographicWidth = 0.0f,
    });

    scene.lights.push_back(nr::load::LightAsset{
        .name = "TemplateReleaseLight",
        .sourceNodeName = "LightNode",
        .nodeIndex = 3,
        .typeRaw = 2,
        .type = "point",
        .colorDiffuse = {1.0f, 0.9f, 0.8f},
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

[[nodiscard]] nr::load::SceneAsset buildTemplateReleaseNoCameraSceneAsset(std::string_view sourcePath)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{std::string{sourcePath}};

    auto material = nr::load::MaterialAsset{};
    material.name = "template_release_no_camera_material";
    scene.materials.push_back(std::move(material));

    auto mesh = nr::load::MeshAsset{};
    mesh.name = "template_release_no_camera_mesh";
    mesh.materialIndex = 0;
    mesh.vertices = {
        nr::load::VertexAsset{.position = {-0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.5f, -0.5f, 0.0f}},
        nr::load::VertexAsset{.position = {0.0f, 0.5f, 0.0f}},
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
    scene.nodes[1].localTransform = identityTransform();

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.vertexCount = 3;
    scene.stats.indexCount = 3;

    return scene;
}

[[nodiscard]] std::size_t countTemplatePrefabEntities(const nr::scene::Scene &scene,
                                                      nr::scene::SceneTemplateHandle templateHandle)
{
    auto query = scene.ecs().query_builder<const nr::scene::SceneTemplateRef>()
                     .with(EcsPrefab)
                     .cached()
                     .build();

    auto count = std::size_t{0};
    query.each([&](flecs::entity, const nr::scene::SceneTemplateRef &templateRef) {
        if (templateRef.handle == templateHandle)
        {
            ++count;
        }
    });

    return count;
}

[[nodiscard]] std::size_t countSyntheticRuntimeCameras(const nr::scene::Scene &scene)
{
    auto query = scene.ecs().query_builder<const nr::scene::SceneCameraBinding>()
                     .cached()
                     .build();

    auto count = std::size_t{0};
    query.each([&](flecs::entity entity, const nr::scene::SceneCameraBinding &binding) {
        if (!binding.synthetic)
        {
            return;
        }

        if (ecs_has_id(scene.ecs().c_ptr(), entity.id(), EcsPrefab))
        {
            return;
        }

        ++count;
    });

    return count;
}

[[nodiscard]] bool checkDestroyTemplateRejectsLiveInstances()
{
    std::println("\n=== Case: checkDestroyTemplateRejectsLiveInstances ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_reject.gltf");
    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/reject",
    });
    if (!require(templateHandle.valid(), "Template handle should be valid."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Instance handle should be valid."))
    {
        return false;
    }

    auto beforeDestroyTemplate = scene.tryGetTemplate(templateHandle);
    if (!require(beforeDestroyTemplate.has_value(), "Template record should exist before destroy attempt."))
    {
        return false;
    }
    if (!require(beforeDestroyTemplate->get().liveInstanceCount == 1u,
                 "Template should report one live instance before first destroyTemplate attempt."))
    {
        return false;
    }

    auto denied = scene.destroyTemplate(templateHandle);
    if (!require(denied == nr::scene::DestroyTemplateResult::instancesAlive,
                 "destroyTemplate should reject templates that still have live instances."))
    {
        return false;
    }

    if (!require(scene.tryGetTemplate(templateHandle).has_value(), "Template should still exist after instancesAlive result."))
    {
        return false;
    }

    scene.destroyInstance(instanceHandle);

    auto afterDestroyInstance = scene.tryGetTemplate(templateHandle);
    if (!require(afterDestroyInstance.has_value(), "Template should still exist after destroyInstance."))
    {
        return false;
    }
    if (!require(afterDestroyInstance->get().liveInstanceCount == 0u,
                 "Template should report zero live instances after destroyInstance."))
    {
        return false;
    }

    auto destroyed = scene.destroyTemplate(templateHandle);
    if (!require(destroyed == nr::scene::DestroyTemplateResult::destroyed,
                 "destroyTemplate should succeed once all instances are destroyed."))
    {
        return false;
    }

    auto repeated = scene.destroyTemplate(templateHandle);
    if (!require(repeated == nr::scene::DestroyTemplateResult::notFound,
                 "Repeated destroyTemplate on same handle should report notFound."))
    {
        return false;
    }

    auto stats = scene.statistics();
    if (!require(stats.templateCount == 0, "Template count should be zero after successful destroyTemplate."))
    {
        return false;
    }
    if (!require(stats.instanceCount == 0, "Instance count should be zero after destroying instance."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkDestroyTemplateReleasesPinnedAssets()
{
    std::println("\n=== Case: checkDestroyTemplateReleasesPinnedAssets ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_release.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/release",
    });
    if (!require(templateHandle.valid(), "Template handle should be valid."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    auto materialHandle = scene.findMaterialHandleByStableKey(keys.material);
    auto textureHandle = scene.findTextureHandleByStableKey(keys.texture);
    auto cameraHandle = scene.findCameraHandleByStableKey(keys.camera);
    auto lightHandle = scene.findLightHandleByStableKey(keys.light);

    if (!require(meshHandle.has_value(), "Mesh handle should be discoverable by stable key before destroy."))
    {
        return false;
    }
    if (!require(materialHandle.has_value(), "Material handle should be discoverable by stable key before destroy."))
    {
        return false;
    }
    if (!require(textureHandle.has_value(), "Texture handle should be discoverable by stable key before destroy."))
    {
        return false;
    }
    if (!require(cameraHandle.has_value(), "Camera handle should be discoverable by stable key before destroy."))
    {
        return false;
    }
    if (!require(lightHandle.has_value(), "Light handle should be discoverable by stable key before destroy."))
    {
        return false;
    }

    if (!require(scene.tryGetMeshAsset(*meshHandle)->get().liveTemplatePins == 1u,
                 "Mesh liveTemplatePins should be 1 before destroyTemplate."))
    {
        return false;
    }
    if (!require(scene.tryGetMaterialAsset(*materialHandle)->get().liveTemplatePins == 1u,
                 "Material liveTemplatePins should be 1 before destroyTemplate."))
    {
        return false;
    }
    if (!require(scene.tryGetTextureAsset(*textureHandle)->get().liveTemplatePins == 1u,
                 "Texture liveTemplatePins should be 1 before destroyTemplate."))
    {
        return false;
    }
    if (!require(scene.tryGetCameraAsset(*cameraHandle)->get().liveTemplatePins == 1u,
                 "Camera liveTemplatePins should be 1 before destroyTemplate."))
    {
        return false;
    }
    if (!require(scene.tryGetLightAsset(*lightHandle)->get().liveTemplatePins == 1u,
                 "Light liveTemplatePins should be 1 before destroyTemplate."))
    {
        return false;
    }

    auto result = scene.destroyTemplate(templateHandle);
    if (!require(result == nr::scene::DestroyTemplateResult::destroyed,
                 "destroyTemplate should succeed when there are no live instances."))
    {
        return false;
    }

    if (!require(!scene.tryGetTemplate(templateHandle).has_value(), "Template lookup should fail after destroyTemplate."))
    {
        return false;
    }
    if (!require(!scene.findMeshHandleByStableKey(keys.mesh).has_value(), "Mesh stable key lookup should fail after asset collection."))
    {
        return false;
    }
    if (!require(!scene.findMaterialHandleByStableKey(keys.material).has_value(), "Material stable key lookup should fail after asset collection."))
    {
        return false;
    }
    if (!require(!scene.findTextureHandleByStableKey(keys.texture).has_value(), "Texture stable key lookup should fail after asset collection."))
    {
        return false;
    }
    if (!require(!scene.findCameraHandleByStableKey(keys.camera).has_value(), "Camera stable key lookup should fail after asset collection."))
    {
        return false;
    }
    if (!require(!scene.findLightHandleByStableKey(keys.light).has_value(), "Light stable key lookup should fail after asset collection."))
    {
        return false;
    }

    auto stats = scene.statistics();
    if (!require(stats.templateCount == 0, "Template count should be zero after release."))
    {
        return false;
    }
    if (!require(stats.meshAssetCount == 0, "Mesh asset count should be zero after release."))
    {
        return false;
    }
    if (!require(stats.materialAssetCount == 0, "Material asset count should be zero after release."))
    {
        return false;
    }
    if (!require(stats.textureAssetCount == 0, "Texture asset count should be zero after release."))
    {
        return false;
    }
    if (!require(stats.cameraAssetCount == 0, "Camera asset count should be zero after release."))
    {
        return false;
    }
    if (!require(stats.lightAssetCount == 0, "Light asset count should be zero after release."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkSharedAssetPinningAcrossTemplates()
{
    std::println("\n=== Case: checkSharedAssetPinningAcrossTemplates ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_shared.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto firstTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/shared/A",
    });
    auto secondTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/shared/B",
    });

    if (!require(firstTemplate.valid(), "First shared template should be valid."))
    {
        return false;
    }
    if (!require(secondTemplate.valid(), "Second shared template should be valid."))
    {
        return false;
    }
    if (!require(firstTemplate != secondTemplate, "Shared templates should have different template handles."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    auto materialHandle = scene.findMaterialHandleByStableKey(keys.material);
    auto textureHandle = scene.findTextureHandleByStableKey(keys.texture);
    auto cameraHandle = scene.findCameraHandleByStableKey(keys.camera);
    auto lightHandle = scene.findLightHandleByStableKey(keys.light);

    if (!require(meshHandle.has_value(), "Mesh handle should exist for shared templates."))
    {
        return false;
    }

    if (!require(scene.tryGetMeshAsset(*meshHandle)->get().liveTemplatePins == 2u,
                 "Mesh liveTemplatePins should be 2 while two templates are alive."))
    {
        return false;
    }
    if (!require(scene.tryGetMaterialAsset(*materialHandle)->get().liveTemplatePins == 2u,
                 "Material liveTemplatePins should be 2 while two templates are alive."))
    {
        return false;
    }
    if (!require(scene.tryGetTextureAsset(*textureHandle)->get().liveTemplatePins == 2u,
                 "Texture liveTemplatePins should be 2 while two templates are alive."))
    {
        return false;
    }
    if (!require(scene.tryGetCameraAsset(*cameraHandle)->get().liveTemplatePins == 2u,
                 "Camera liveTemplatePins should be 2 while two templates are alive."))
    {
        return false;
    }
    if (!require(scene.tryGetLightAsset(*lightHandle)->get().liveTemplatePins == 2u,
                 "Light liveTemplatePins should be 2 while two templates are alive."))
    {
        return false;
    }

    auto firstDestroy = scene.destroyTemplate(firstTemplate);
    if (!require(firstDestroy == nr::scene::DestroyTemplateResult::destroyed,
                 "Destroying first shared template should succeed."))
    {
        return false;
    }

    if (!require(scene.findMeshHandleByStableKey(keys.mesh).has_value(),
                 "Shared mesh should still exist after first template destroy."))
    {
        return false;
    }

    auto meshAfterFirstDestroy = scene.tryGetMeshAsset(*meshHandle);
    if (!require(meshAfterFirstDestroy.has_value(), "Mesh record should still exist after first template destroy."))
    {
        return false;
    }
    if (!require(meshAfterFirstDestroy->get().liveTemplatePins == 1u,
                 "Shared mesh liveTemplatePins should decrement to 1 after first destroy."))
    {
        return false;
    }

    auto secondDestroy = scene.destroyTemplate(secondTemplate);
    if (!require(secondDestroy == nr::scene::DestroyTemplateResult::destroyed,
                 "Destroying second shared template should succeed."))
    {
        return false;
    }

    if (!require(!scene.findMeshHandleByStableKey(keys.mesh).has_value(),
                 "Shared mesh should be collected after last template destroy."))
    {
        return false;
    }

    auto stats = scene.statistics();
    if (!require(stats.templateCount == 0, "Template count should be zero after destroying both shared templates."))
    {
        return false;
    }
    if (!require(stats.meshAssetCount == 0, "Mesh asset count should be zero after destroying both shared templates."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkUnloadReloadGenerationProgression()
{
    std::println("\n=== Case: checkUnloadReloadGenerationProgression ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_reload.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto firstTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/reload",
    });
    if (!require(firstTemplate.valid(), "First template registration should succeed."))
    {
        return false;
    }

    auto firstMeshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    if (!require(firstMeshHandle.has_value(), "First mesh handle should exist before unload."))
    {
        return false;
    }

    auto firstDestroy = scene.destroyTemplate(firstTemplate);
    if (!require(firstDestroy == nr::scene::DestroyTemplateResult::destroyed,
                 "First destroyTemplate should succeed."))
    {
        return false;
    }

    auto secondTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/reload",
    });
    if (!require(secondTemplate.valid(), "Second template registration should succeed."))
    {
        return false;
    }
    if (!require(secondTemplate != firstTemplate, "Reloaded template should have a different handle value."))
    {
        return false;
    }

    if (secondTemplate.slot == firstTemplate.slot)
    {
        if (!require(secondTemplate.generation > firstTemplate.generation,
                     "Template handle generation should advance when slot is reused after unload."))
        {
            return false;
        }
    }

    auto secondMeshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    if (!require(secondMeshHandle.has_value(), "Second mesh handle should exist after reload."))
    {
        return false;
    }
    if (!require(*secondMeshHandle != *firstMeshHandle, "Reloaded mesh should not reuse the exact stale handle value."))
    {
        return false;
    }

    if (secondMeshHandle->slot == firstMeshHandle->slot)
    {
        if (!require(secondMeshHandle->generation > firstMeshHandle->generation,
                     "Mesh handle generation should advance when slot is reused after unload."))
        {
            return false;
        }
    }

    auto meshRecord = scene.tryGetMeshAsset(*secondMeshHandle);
    if (!require(meshRecord.has_value(), "Reloaded mesh record should be queryable."))
    {
        return false;
    }
    if (!require(meshRecord->get().liveTemplatePins == 1u,
                 "Reloaded mesh should have exactly one template pin."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkQueuedUploadTemplateDestroyPath()
{
    std::println("\n=== Case: checkQueuedUploadTemplateDestroyPath ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_queued_upload.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/queued-upload",
    });
    if (!require(templateHandle.valid(), "Template handle should be valid."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    auto materialHandle = scene.findMaterialHandleByStableKey(keys.material);
    auto textureHandle = scene.findTextureHandleByStableKey(keys.texture);
    auto cameraHandle = scene.findCameraHandleByStableKey(keys.camera);
    auto lightHandle = scene.findLightHandleByStableKey(keys.light);

    scene.beginFrame(7);

    if (!require(scene.tryGetMeshAsset(*meshHandle)->get().uploadQueued,
                 "Mesh should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(scene.tryGetMaterialAsset(*materialHandle)->get().uploadQueued,
                 "Material should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(scene.tryGetTextureAsset(*textureHandle)->get().uploadQueued,
                 "Texture should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(scene.tryGetCameraAsset(*cameraHandle)->get().uploadQueued,
                 "Camera should be uploadQueued after beginFrame."))
    {
        return false;
    }
    if (!require(scene.tryGetLightAsset(*lightHandle)->get().uploadQueued,
                 "Light should be uploadQueued after beginFrame."))
    {
        return false;
    }

    auto destroyResult = scene.destroyTemplate(templateHandle);
    if (!require(destroyResult == nr::scene::DestroyTemplateResult::destroyed,
                 "destroyTemplate should collect queued assets without GPU payloads."))
    {
        return false;
    }

    if (!require(!scene.findMeshHandleByStableKey(keys.mesh).has_value(), "Queued mesh should be collected after destroyTemplate."))
    {
        return false;
    }
    if (!require(!scene.findMaterialHandleByStableKey(keys.material).has_value(), "Queued material should be collected after destroyTemplate."))
    {
        return false;
    }
    if (!require(!scene.findTextureHandleByStableKey(keys.texture).has_value(), "Queued texture should be collected after destroyTemplate."))
    {
        return false;
    }
    if (!require(!scene.findCameraHandleByStableKey(keys.camera).has_value(), "Queued camera should be collected after destroyTemplate."))
    {
        return false;
    }
    if (!require(!scene.findLightHandleByStableKey(keys.light).has_value(), "Queued light should be collected after destroyTemplate."))
    {
        return false;
    }

    scene.beginFrame(8);
    scene.uploadPending();

    auto stats = scene.statistics();
    if (!require(stats.meshAssetCount == 0, "Mesh assets should remain empty after queued-upload destroy path."))
    {
        return false;
    }
    if (!require(stats.materialAssetCount == 0, "Material assets should remain empty after queued-upload destroy path."))
    {
        return false;
    }
    if (!require(stats.textureAssetCount == 0, "Texture assets should remain empty after queued-upload destroy path."))
    {
        return false;
    }
    if (!require(stats.cameraAssetCount == 0, "Camera assets should remain empty after queued-upload destroy path."))
    {
        return false;
    }
    if (!require(stats.lightAssetCount == 0, "Light assets should remain empty after queued-upload destroy path."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkPrefabWorldCleanupAcrossReloads()
{
    std::println("\n=== Case: checkPrefabWorldCleanupAcrossReloads ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_world_cleanup.gltf");
    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto countAllTemplatePrefabs = [&]() {
        auto query = scene.ecs().query_builder<const nr::scene::SceneTemplateRef>()
                         .with(EcsPrefab)
                         .cached()
                         .build();

        auto count = std::size_t{0};
        query.each([&](flecs::entity, const nr::scene::SceneTemplateRef &) {
            ++count;
        });
        return count;
    };

    auto const cycleIndices = std::views::iota(std::size_t{0}, std::size_t{3});
    for (auto cycleIndex : cycleIndices)
    {
        auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
            .stableKey = "manual://template/world-cleanup",
        });
        if (!require(templateHandle.valid(), std::format("Cycle {}: template registration should succeed.", cycleIndex)))
        {
            return false;
        }

        auto templateRecord = scene.tryGetTemplate(templateHandle);
        if (!require(templateRecord.has_value(), std::format("Cycle {}: template record should exist.", cycleIndex)))
        {
            return false;
        }

        auto expectedPrefabEntityCount = templateRecord->get().prefabEntities.size();
        if (!require(expectedPrefabEntityCount > 0u,
                     std::format("Cycle {}: prefab entity tracking list should not be empty.", cycleIndex)))
        {
            return false;
        }

        auto countedPrefabEntities = countTemplatePrefabEntities(scene, templateHandle);
        if (!require(countedPrefabEntities == expectedPrefabEntityCount,
                     std::format("Cycle {}: world prefab count should match tracked prefabEntities.", cycleIndex)))
        {
            return false;
        }

        auto destroyResult = scene.destroyTemplate(templateHandle);
        if (!require(destroyResult == nr::scene::DestroyTemplateResult::destroyed,
                     std::format("Cycle {}: destroyTemplate should succeed.", cycleIndex)))
        {
            return false;
        }

        if (!require(countTemplatePrefabEntities(scene, templateHandle) == 0u,
                     std::format("Cycle {}: template prefab entities should be fully removed from ECS world.", cycleIndex)))
        {
            return false;
        }

        if (!require(countAllTemplatePrefabs() == 0u,
                     std::format("Cycle {}: ECS world should not retain any template prefab entities.", cycleIndex)))
        {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool checkFallbackCameraInfrastructureCleanup()
{
    std::println("\n=== Case: checkFallbackCameraInfrastructureCleanup ===");

    auto sceneAsset = buildTemplateReleaseNoCameraSceneAsset("manual_phase32_fallback_cleanup.gltf");

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/fallback-cleanup",
    });
    if (!require(templateHandle.valid(), "Fallback cleanup template should register successfully."))
    {
        return false;
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!require(instanceHandle.valid(), "Fallback cleanup instance should be valid."))
    {
        return false;
    }

    scene.updateSimulation({.deltaSeconds = 1.0f / 60.0f});

    auto primaryCamera = scene.tryGetPrimaryCamera();
    if (!require(primaryCamera.has_value(), "Fallback camera should resolve in no-imported-camera scene."))
    {
        return false;
    }
    if (!require(primaryCamera->fallback, "Resolved primary camera should be marked as fallback."))
    {
        return false;
    }

    if (!require(countSyntheticRuntimeCameras(scene) == 1u,
                 "Fallback camera entity should exist while template/instance are alive."))
    {
        return false;
    }

    auto statsBeforeDestroy = scene.statistics();
    if (!require(statsBeforeDestroy.cameraAssetCount == 1u,
                 "Fallback camera should appear in cameraAssetCount before teardown."))
    {
        return false;
    }

    scene.destroyInstance(instanceHandle);

    auto destroyResult = scene.destroyTemplate(templateHandle);
    if (!require(destroyResult == nr::scene::DestroyTemplateResult::destroyed,
                 "Template destroy should succeed after fallback path instance removal."))
    {
        return false;
    }

    auto statsAfterDestroy = scene.statistics();
    if (!require(statsAfterDestroy.templateCount == 0u,
                 "Template count should be zero after fallback cleanup destroyTemplate."))
    {
        return false;
    }
    if (!require(statsAfterDestroy.cameraAssetCount == 0u,
                 "Fallback camera asset should be cleaned once last template is destroyed."))
    {
        return false;
    }

    if (!require(countSyntheticRuntimeCameras(scene) == 0u,
                 "Fallback camera entity should be removed from ECS world after cleanup."))
    {
        return false;
    }

    if (!require(!scene.tryGetPrimaryCamera().has_value(),
                 "With no templates left, repeated tryGetPrimaryCamera should return nullopt."))
    {
        return false;
    }

    scene.beginFrame(11);
    scene.uploadPending();

    auto statsAfterFrame = scene.statistics();
    if (!require(statsAfterFrame.cameraAssetCount == 0u,
                 "Fallback camera should not be recreated by frame lifecycle calls."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkTemplateBindingCountersResetAfterDestroy()
{
    std::println("\n=== Case: checkTemplateBindingCountersResetAfterDestroy ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_binding_counter_reset.gltf");

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto firstTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/counter-reset/A",
    });
    auto secondTemplate = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/counter-reset/B",
    });

    if (!require(firstTemplate.valid() && secondTemplate.valid(),
                 "Both templates should register successfully for counter reset case."))
    {
        return false;
    }

    auto statsWithTemplates = scene.statistics();
    if (!require(statsWithTemplates.templateCount == 2u,
                 "Template count should be two before counter reset destroy path."))
    {
        return false;
    }
    if (!require(statsWithTemplates.templateNodeCount > 0u,
                 "Template node count should be non-zero while templates are alive."))
    {
        return false;
    }
    if (!require(statsWithTemplates.templateMeshBindingCount > 0u,
                 "Template mesh binding count should be non-zero while templates are alive."))
    {
        return false;
    }
    if (!require(statsWithTemplates.templateCameraBindingCount > 0u,
                 "Template camera binding count should be non-zero while templates are alive."))
    {
        return false;
    }
    if (!require(statsWithTemplates.templateLightBindingCount > 0u,
                 "Template light binding count should be non-zero while templates are alive."))
    {
        return false;
    }

    if (!require(scene.destroyTemplate(firstTemplate) == nr::scene::DestroyTemplateResult::destroyed,
                 "First template destroy should succeed for counter reset case."))
    {
        return false;
    }
    if (!require(scene.destroyTemplate(secondTemplate) == nr::scene::DestroyTemplateResult::destroyed,
                 "Second template destroy should succeed for counter reset case."))
    {
        return false;
    }

    auto statsAfterDestroy = scene.statistics();
    if (!require(statsAfterDestroy.templateCount == 0u,
                 "Template count should be zero after destroying both templates."))
    {
        return false;
    }
    if (!require(statsAfterDestroy.templateNodeCount == 0u,
                 "Template node count should reset to zero after destroying both templates."))
    {
        return false;
    }
    if (!require(statsAfterDestroy.templateMeshBindingCount == 0u,
                 "Template mesh binding count should reset to zero after destroying both templates."))
    {
        return false;
    }
    if (!require(statsAfterDestroy.templateCameraBindingCount == 0u,
                 "Template camera binding count should reset to zero after destroying both templates."))
    {
        return false;
    }
    if (!require(statsAfterDestroy.templateLightBindingCount == 0u,
                 "Template light binding count should reset to zero after destroying both templates."))
    {
        return false;
    }

    if (!require(scene.destroyTemplate(secondTemplate) == nr::scene::DestroyTemplateResult::notFound,
                 "Repeated destroyTemplate should report notFound after counter reset."))
    {
        return false;
    }

    auto statsAfterRepeat = scene.statistics();
    if (!require(statsAfterRepeat.templateNodeCount == 0u &&
                     statsAfterRepeat.templateMeshBindingCount == 0u &&
                     statsAfterRepeat.templateCameraBindingCount == 0u &&
                     statsAfterRepeat.templateLightBindingCount == 0u,
                 "Template counters should remain zero after repeated notFound destroyTemplate call."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkTemplatePinnedAssetsCollectionPreservesOrder()
{
    std::println("\n=== Case: checkTemplatePinnedAssetsCollectionPreservesOrder ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_collection.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/collection",
    });
    if (!require(templateHandle.valid(), "Template handle should be valid."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    auto materialHandle = scene.findMaterialHandleByStableKey(keys.material);
    auto textureHandle = scene.findTextureHandleByStableKey(keys.texture);
    auto cameraHandle = scene.findCameraHandleByStableKey(keys.camera);
    auto lightHandle = scene.findLightHandleByStableKey(keys.light);

    if (!require(meshHandle.has_value() && materialHandle.has_value() &&
                     textureHandle.has_value() && cameraHandle.has_value() &&
                     lightHandle.has_value(),
                 "All asset types (mesh, material, texture, camera, light) should be retrievable."))
    {
        return false;
    }

    auto stats = scene.statistics();
    if (!require(stats.meshAssetCount == 1u && stats.materialAssetCount == 1u &&
                     stats.textureAssetCount == 1u && stats.cameraAssetCount == 1u &&
                     stats.lightAssetCount == 1u,
                 "All asset counts should be 1 before destroying template."))
    {
        return false;
    }

    auto result = scene.destroyTemplate(templateHandle);
    if (!require(result == nr::scene::DestroyTemplateResult::destroyed,
                 "destroyTemplate should succeed."))
    {
        return false;
    }

    auto statsAfter = scene.statistics();
    if (!require(statsAfter.meshAssetCount == 0u && statsAfter.materialAssetCount == 0u &&
                     statsAfter.textureAssetCount == 0u && statsAfter.cameraAssetCount == 0u &&
                     statsAfter.lightAssetCount == 0u,
                 "All asset counts should be zero after destroying template (collectTemplatePinnedAssets lambda "
                 "collection should have processed all 5 asset types correctly)."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkTemplatePinSetOperationsUnified()
{
    std::println("\n=== Case: checkTemplatePinSetOperationsUnified ===");

    auto sceneAsset = buildTemplateReleaseSceneAsset("manual_phase32_pinset.gltf");
    auto keys = makeCanonicalKeys(sceneAsset);

    nr::rhi::Device device{};
    nr::scene::Scene scene(nr::scene::SceneCreateInfo{.device = device});

    auto templateHandle = scene.registerTemplate(sceneAsset, nr::scene::SceneTemplateCreateInfo{
        .stableKey = "manual://template/pinset",
    });
    if (!require(templateHandle.valid(), "Template should be valid."))
    {
        return false;
    }

    auto meshHandle = scene.findMeshHandleByStableKey(keys.mesh);
    auto materialHandle = scene.findMaterialHandleByStableKey(keys.material);
    auto textureHandle = scene.findTextureHandleByStableKey(keys.texture);
    auto cameraHandle = scene.findCameraHandleByStableKey(keys.camera);
    auto lightHandle = scene.findLightHandleByStableKey(keys.light);

    if (!require(meshHandle.has_value() && materialHandle.has_value() &&
                     textureHandle.has_value() && cameraHandle.has_value() &&
                     lightHandle.has_value(),
                 "All 5 asset types should be pinned (unified pinset build/retain/release operations)."))
    {
        return false;
    }

    auto statsBefore = scene.statistics();
    if (!require(statsBefore.meshAssetCount == 1u && statsBefore.materialAssetCount == 1u &&
                     statsBefore.textureAssetCount == 1u && statsBefore.cameraAssetCount == 1u &&
                     statsBefore.lightAssetCount == 1u,
                 "All 5 asset types should exist with pin count 1 before template destruction."))
    {
        return false;
    }

    auto destroyed = scene.destroyTemplate(templateHandle);
    if (!require(destroyed == nr::scene::DestroyTemplateResult::destroyed,
                 "Template destruction should succeed."))
    {
        return false;
    }

    auto statsAfter = scene.statistics();
    if (!require(statsAfter.meshAssetCount == 0u && statsAfter.materialAssetCount == 0u &&
                     statsAfter.textureAssetCount == 0u && statsAfter.cameraAssetCount == 0u &&
                     statsAfter.lightAssetCount == 0u,
                 "All 5 asset types should be released after template destruction "
                 "(unified releaseTemplatePins should have processed all collections)."))
    {
        return false;
    }

    return true;
}

} // namespace

int main()
{
    auto const cases = std::array{
        std::pair{"checkDestroyTemplateRejectsLiveInstances", &checkDestroyTemplateRejectsLiveInstances},
        std::pair{"checkDestroyTemplateReleasesPinnedAssets", &checkDestroyTemplateReleasesPinnedAssets},
        std::pair{"checkSharedAssetPinningAcrossTemplates", &checkSharedAssetPinningAcrossTemplates},
        std::pair{"checkUnloadReloadGenerationProgression", &checkUnloadReloadGenerationProgression},
        std::pair{"checkQueuedUploadTemplateDestroyPath", &checkQueuedUploadTemplateDestroyPath},
        std::pair{"checkPrefabWorldCleanupAcrossReloads", &checkPrefabWorldCleanupAcrossReloads},
        std::pair{"checkFallbackCameraInfrastructureCleanup", &checkFallbackCameraInfrastructureCleanup},
        std::pair{"checkTemplateBindingCountersResetAfterDestroy", &checkTemplateBindingCountersResetAfterDestroy},
        std::pair{"checkTemplatePinnedAssetsCollectionPreservesOrder", &checkTemplatePinnedAssetsCollectionPreservesOrder},
        std::pair{"checkTemplatePinSetOperationsUnified", &checkTemplatePinSetOperationsUnified},
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
