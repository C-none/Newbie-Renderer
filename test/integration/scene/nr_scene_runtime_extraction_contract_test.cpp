import std;
import dependency.math;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;

namespace
{
template <typename RecordT>
concept HasSceneGpuLifecycle = requires(RecordT &record) {
    record.gpuVersion;
    record.gpuState;
    record.gpu;
    record.retiredGpu;
};

template <typename RecordT>
concept HasCpuVersion = requires(RecordT &record) { record.cpuVersion; };

static_assert(HasSceneGpuLifecycle<nr::scene::MeshAssetRecord>);
static_assert(HasSceneGpuLifecycle<nr::scene::TextureAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::MaterialAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::CameraAssetRecord>);
static_assert(!HasSceneGpuLifecycle<nr::scene::LightAssetRecord>);
static_assert(HasCpuVersion<nr::scene::MaterialAssetRecord>);
static_assert(!HasCpuVersion<nr::scene::CameraAssetRecord>);
static_assert(!HasCpuVersion<nr::scene::LightAssetRecord>);

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
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] std::array<float, 16> translatedTransform(float x, float y, float z) noexcept
{
    return {
        1.0f, 0.0f, 0.0f, x, 0.0f, 1.0f, 0.0f, y, 0.0f, 0.0f, 1.0f, z, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] nr::load::SceneAsset makeRuntimeSceneAsset(bool includeCamera)
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = includeCamera ? std::filesystem::path{"runtime_camera_scene.gltf"}
                                     : std::filesystem::path{"runtime_readiness_scene.gltf"};

    scene.textures.push_back(nr::load::TextureAsset{
        .key = "manual://textures/runtime/base_color",
        .decodedImage =
            nr::load::Image{
                .width = 1,
                .height = 1,
                .channels = 4,
                .pixels = {255u, 255u, 255u, 255u},
            },
    });

    scene.materials.push_back(nr::load::MaterialAsset{
        .name = "runtime_material",
        .textures =
            {
                nr::load::MaterialTextureBinding{
                    .textureIndex = 0,
                    .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
                    .sourceSemanticName = "diffuse",
                },
            },
    });

    scene.meshes.push_back(nr::load::MeshAsset{
        .name = "runtime_triangle",
        .vertices =
            {
                nr::load::VertexAsset{
                    .position = {-0.5f, -0.5f, 0.0f},
                    .texCoord1 = {0.125f, 0.875f},
                },
                nr::load::VertexAsset{
                    .position = {0.5f, -0.5f, 0.0f},
                    .texCoord1 = {0.875f, 0.875f},
                },
                nr::load::VertexAsset{
                    .position = {0.0f, 0.5f, 0.0f},
                    .texCoord1 = {0.5f, 0.125f},
                },
            },
        .indices = {0u, 1u, 2u},
        .geometries =
            {
                nr::load::MeshGeometryAsset{
                    .name = "runtime_triangle_geometry_0",
                    .indexCount = 3,
                    .materialIndex = 0,
                },
            },
    });

    scene.nodes.resize(includeCamera ? 3u : 2u);
    scene.rootNodeIndex = 0;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = includeCamera ? std::vector<std::uint32_t>{1u, 2u} : std::vector<std::uint32_t>{1u};
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

[[nodiscard]] nr::load::SceneAsset makeLightSceneAsset()
{
    auto scene = nr::load::SceneAsset{};
    scene.sourcePath = std::filesystem::path{"runtime_lights_scene.gltf"};

    scene.nodes.resize(6u);
    scene.rootNodeIndex = 0;
    scene.nodes[0].name = "Root";
    scene.nodes[0].parentIndex = nr::load::invalidIndex;
    scene.nodes[0].childIndices = {1u, 2u, 3u, 4u, 5u};
    scene.nodes[0].localTransform = identityTransform();

    auto const nodeIndices = std::views::iota(std::uint32_t{1u}, std::uint32_t{6u});
    std::ranges::for_each(nodeIndices, [&](std::uint32_t nodeIndex) { scene.nodes[nodeIndex].parentIndex = 0u; });
    scene.nodes[1].name = "DirectionalNode";
    scene.nodes[1].localTransform = translatedTransform(1.0f, 2.0f, 3.0f);
    scene.nodes[2].name = "PointNode";
    scene.nodes[2].localTransform = translatedTransform(4.0f, 5.0f, 6.0f);
    scene.nodes[3].name = "SpotNode";
    scene.nodes[3].localTransform = translatedTransform(7.0f, 8.0f, 9.0f);
    scene.nodes[4].name = "AmbientNode";
    scene.nodes[4].localTransform = translatedTransform(10.0f, 11.0f, 12.0f);
    scene.nodes[5].name = "AreaNode";
    scene.nodes[5].localTransform = translatedTransform(13.0f, 14.0f, 15.0f);

    scene.lights = {
        nr::load::LightAsset{
            .name = "DirectionalLight",
            .sourceNodeName = "DirectionalNode",
            .nodeIndex = 1u,
            .type = "directional",
            .colorDiffuse = {2.0f, 1.0f, 1.0f},
        },
        nr::load::LightAsset{
            .name = "PointLight",
            .sourceNodeName = "PointNode",
            .nodeIndex = 2u,
            .type = "point",
            .colorDiffuse = {1.0f, 4.0f, 1.0f},
            .range = 12.5f,
        },
        nr::load::LightAsset{
            .name = "SpotLight",
            .sourceNodeName = "SpotNode",
            .nodeIndex = 3u,
            .type = "spot",
            .colorDiffuse = {1.0f, 1.0f, 3.0f},
            .range = 8.0f,
            .innerCone = glm::radians(10.0f),
            .outerCone = glm::radians(25.0f),
        },
        nr::load::LightAsset{
            .name = "AmbientLight",
            .sourceNodeName = "AmbientNode",
            .nodeIndex = 4u,
            .type = "ambient",
            .colorDiffuse = {1.0f, 1.0f, 1.0f},
        },
        nr::load::LightAsset{
            .name = "AreaLight",
            .sourceNodeName = "AreaNode",
            .nodeIndex = 5u,
            .type = "area",
            .colorDiffuse = {1.0f, 1.0f, 1.0f},
        },
    };

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());
    return scene;
}

[[nodiscard]] nr::scene::SceneLightGpuRecord makeAliasTableTestLight(glm::vec3 color, float intensity) noexcept
{
    auto record = nr::scene::SceneLightGpuRecord{};
    record.colorIntensity = glm::vec4{color, intensity};
    return record;
}

const nr::test::CaseRegistrar materialSemanticClassificationCase{
    "scene material bridge preserves Assimp PBR texture semantics", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRuntimeSceneAsset(false);

        struct SemanticFixture
        {
            std::string_view sourceName{};
            nr::resource::MaterialTextureSlotSemantic semantic = nr::resource::MaterialTextureSlotSemantic::unsupported;
            std::uint32_t materialIndex = 0;
        };

        auto semantics = std::array{
            SemanticFixture{
                .sourceName = "base_color",
                .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
            },
            SemanticFixture{
                .sourceName = "normal_camera",
                .semantic = nr::resource::MaterialTextureSlotSemantic::normal,
            },
            SemanticFixture{
                .sourceName = "emission_color",
                .semantic = nr::resource::MaterialTextureSlotSemantic::emissive,
            },
            SemanticFixture{
                .sourceName = "ambient_occlusion",
                .semantic = nr::resource::MaterialTextureSlotSemantic::occlusion,
            },
            SemanticFixture{
                .sourceName = "gltf_metallic_roughness",
                .semantic = nr::resource::MaterialTextureSlotSemantic::metallicRoughness,
            },
            SemanticFixture{
                .sourceName = "clearcoat_roughness",
                .semantic = nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness,
                .materialIndex = 1u,
            },
            SemanticFixture{
                .sourceName = "sheen_color",
                .semantic = nr::resource::MaterialTextureSlotSemantic::sheenColor,
                .materialIndex = 1u,
            },
            SemanticFixture{
                .sourceName = "transmission",
                .semantic = nr::resource::MaterialTextureSlotSemantic::transmission,
                .materialIndex = 1u,
            },
            SemanticFixture{
                .sourceName = "anisotropy",
                .semantic = nr::resource::MaterialTextureSlotSemantic::anisotropy,
                .materialIndex = 1u,
            },
            SemanticFixture{
                .sourceName = "volume_thickness",
                .semantic = nr::resource::MaterialTextureSlotSemantic::unsupported,
                .materialIndex = 1u,
            },
        };

        sceneAsset.textures.clear();
        sceneAsset.materials[0].textures.clear();
        sceneAsset.materials.push_back(nr::load::MaterialAsset{
            .name = "extension_material",
            .anisotropyFactor = 0.35f,
            .anisotropyRotation = 0.25f,
            .clearcoatFactor = 0.8f,
            .sheenRoughnessFactor = 0.45f,
            .transmissionFactor = 0.6f,
            .ior = 1.33f,
            .thicknessFactor = 0.2f,
        });

        auto semanticIndices = std::views::iota(std::size_t{0}, semantics.size());
        std::ranges::for_each(semanticIndices, [&](std::size_t semanticIndex) {
            sceneAsset.textures.push_back(nr::load::TextureAsset{
                .key = std::format("manual://textures/runtime/{}", semantics[semanticIndex].sourceName),
                .decodedImage =
                    nr::load::Image{
                        .width = 1,
                        .height = 1,
                        .channels = 4,
                        .pixels = {255u, 255u, 255u, 255u},
                    },
            });

            auto &targetMaterial = sceneAsset.materials[semantics[semanticIndex].materialIndex];
            targetMaterial.textures.push_back(nr::load::MaterialTextureBinding{
                .textureIndex = static_cast<std::uint32_t>(semanticIndex),
                .semantic = semantics[semanticIndex].semantic,
                .sourceSemanticName = std::string{semantics[semanticIndex].sourceName},
            });
        });

        auto &baseColorBinding = sceneAsset.materials[0].textures.front();
        baseColorBinding.uvChannel = 1u;
        baseColorBinding.transform = nr::resource::MaterialTextureTransform{
            .linear = glm::vec4{2.0f, -0.5f, 0.25f, 0.75f},
            .offset = glm::vec2{0.125f, 0.375f},
        };
        auto &occlusionBinding = sceneAsset.materials[0].textures[3];
        occlusionBinding.transform = nr::resource::MaterialTextureTransform{
            .linear = glm::vec4{0.5f, 0.0f, 0.0f, 0.25f},
            .offset = glm::vec2{0.625f, 0.75f},
        };
        auto &anisotropyBinding = sceneAsset.materials[1].textures[3];
        anisotropyBinding.uvChannel = 1u;
        anisotropyBinding.transform = nr::resource::MaterialTextureTransform{
            .linear = glm::vec4{0.75f, -0.25f, 0.5f, 1.25f},
            .offset = glm::vec2{0.2f, -0.1f},
        };

        sceneAsset.materials[0].textures.push_back(nr::load::MaterialTextureBinding{
            .textureIndex = 4u,
            .semantic = nr::resource::MaterialTextureSlotSemantic::metallicRoughness,
            .sourceSemanticName = "metalness",
        });
        sceneAsset.textures.push_back(nr::load::TextureAsset{
            .key = "manual://textures/runtime/diffuse_roughness_conflict",
            .decodedImage =
                nr::load::Image{
                    .width = 1,
                    .height = 1,
                    .channels = 4,
                    .pixels = {127u, 127u, 127u, 255u},
                },
        });
        sceneAsset.materials[0].textures.push_back(nr::load::MaterialTextureBinding{
            .textureIndex = static_cast<std::uint32_t>(sceneAsset.textures.size() - 1u),
            .semantic = nr::resource::MaterialTextureSlotSemantic::metallicRoughness,
            .sourceSemanticName = "diffuse_roughness",
        });

        sceneAsset.stats.textureCount = static_cast<std::uint32_t>(sceneAsset.textures.size());
        sceneAsset.stats.materialCount = static_cast<std::uint32_t>(sceneAsset.materials.size());

        auto templateHandle = scene.registerTemplate(sceneAsset);
        nr::test::require(templateHandle.valid(), "template registration should succeed");

        auto materialHandle =
            scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 0));
        nr::test::require(materialHandle.has_value(), "material handle should resolve by stable key");

        auto materialRecord = scene.tryGetMaterialAsset(*materialHandle);
        nr::test::require(materialRecord.has_value(), "material record should exist");
        auto const &material = materialRecord->get().cpu;

        auto textureHandles = std::vector<nr::resource::TextureHandle>{};
        textureHandles.reserve(sceneAsset.textures.size());
        auto texturePlan = nr::scene::SceneBridge::buildPlan(sceneAsset);
        nr::test::require(texturePlan.valid(), "runtime texture fixture should retain a valid bridge plan");
        auto textureIndices = std::views::iota(std::size_t{0}, sceneAsset.textures.size());
        std::ranges::for_each(textureIndices, [&](std::size_t textureIndex) {
            auto textureHandle = scene.findTextureHandleByStableKey(texturePlan.textures[textureIndex].canonicalKey);
            nr::test::require(textureHandle.has_value(),
                              std::format("texture handle {} should resolve by stable key", textureIndex));
            textureHandles.push_back(*textureHandle);
        });

        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).texture ==
                              textureHandles[0],
                          "base_color should bind baseColor slot");
        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::normal).texture == textureHandles[1],
                          "normal_camera should bind normal slot");
        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::emissive).texture ==
                              textureHandles[2],
                          "emission_color should bind emissive slot");
        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::occlusion).texture ==
                              textureHandles[3],
                          "ambient_occlusion should bind occlusion slot");
        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::metallicRoughness).texture ==
                              textureHandles[4],
                          "gltf_metallic_roughness should bind metallicRoughness slot");
        nr::test::requireEqual(material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).uvSet,
                               std::uint32_t{1}, "material bridge should preserve UV set 1");
        nr::test::require(material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).transform.linear ==
                                  glm::vec4{2.0f, -0.5f, 0.25f, 0.75f} &&
                              material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).transform.offset ==
                                  glm::vec2{0.125f, 0.375f},
                          "material bridge should preserve base-color affine texture metadata");
        nr::test::require(
            material.slot(nr::resource::MaterialTextureSlotSemantic::occlusion).transform.linear ==
                    glm::vec4{0.5f, 0.0f, 0.0f, 0.25f} &&
                material.slot(nr::resource::MaterialTextureSlotSemantic::occlusion).transform.offset ==
                    glm::vec2{0.625f, 0.75f},
            "material bridge should transport occlusion affine metadata without enabling shader behavior");
        nr::test::requireEqual(material.core.metallicFactor, 1.0f);
        nr::test::require(!material.clearcoat.has_value(), "core material should not create clearcoat extension");
        nr::test::require(!material.sheen.has_value(), "core material should not create sheen extension");
        nr::test::require(!material.transmission.has_value(), "core material should not create transmission extension");
        nr::test::require(!material.anisotropy.has_value(), "core material should not create anisotropy extension");

        auto extensionMaterialHandle =
            scene.findMaterialHandleByStableKey(nr::scene::SceneBridge::makeMaterialCanonicalKey(sceneAsset, 1));
        nr::test::require(extensionMaterialHandle.has_value(), "extension material should resolve by stable key");

        auto extensionMaterialRecord = scene.tryGetMaterialAsset(*extensionMaterialHandle);
        nr::test::require(extensionMaterialRecord.has_value(), "extension material record should exist");
        auto const &extensionMaterial = extensionMaterialRecord->get().cpu;

        nr::test::require(extensionMaterial.clearcoat.has_value(),
                          "clearcoat texture or factor should create extension block");
        nr::test::require(extensionMaterial.sheen.has_value(), "sheen texture or factor should create extension block");
        nr::test::require(extensionMaterial.transmission.has_value(),
                          "transmission texture or factor should create extension block");
        nr::test::require(extensionMaterial.ior.has_value(), "authored IOR should create the IOR extension block");
        nr::test::require(extensionMaterial.volumeBoundary.has_value(),
                          "authored thickness should create the volume boundary extension block");
        nr::test::require(extensionMaterial.anisotropy.has_value(),
                          "anisotropy texture or factor should create extension block");
        nr::test::requireEqual(extensionMaterial.ior->ior, 1.33f);
        nr::test::requireEqual(extensionMaterial.volumeBoundary->thicknessFactor, 0.2f);
        nr::test::require(
            extensionMaterial.slot(nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness).texture ==
                textureHandles[5],
            "clearcoat_roughness should bind clearcoat roughness slot");
        nr::test::require(extensionMaterial.slot(nr::resource::MaterialTextureSlotSemantic::sheenColor).texture ==
                              textureHandles[6],
                          "sheen_color should bind sheen color slot");
        nr::test::require(extensionMaterial.slot(nr::resource::MaterialTextureSlotSemantic::transmission).texture ==
                              textureHandles[7],
                          "transmission should bind transmission slot");
        nr::test::require(extensionMaterial.slot(nr::resource::MaterialTextureSlotSemantic::anisotropy).texture ==
                              textureHandles[8],
                          "anisotropy should bind anisotropy slot");
        auto const &runtimeAnisotropySlot =
            extensionMaterial.slot(nr::resource::MaterialTextureSlotSemantic::anisotropy);
        nr::test::requireEqual(runtimeAnisotropySlot.uvSet, std::uint32_t{1u});
        nr::test::require(runtimeAnisotropySlot.transform.linear == glm::vec4{0.75f, -0.25f, 0.5f, 1.25f} &&
                              runtimeAnisotropySlot.transform.offset == glm::vec2{0.2f, -0.1f},
                          "anisotropy should preserve UV reference and full affine transform");
        auto anisotropyTextureRecord = scene.tryGetTextureAsset(textureHandles[8]);
        nr::test::require(anisotropyTextureRecord.has_value() && !anisotropyTextureRecord->get().cpu.srgb,
                          "anisotropy texture data must remain linear rather than sRGB");

        auto const extensionFlags = extensionMaterial.featureFlags();
        nr::test::require(nr::resource::hasAnyFeature(extensionFlags, nr::resource::MaterialFeatureFlag::clearcoat),
                          "extension flags should include clearcoat");
        nr::test::require(nr::resource::hasAnyFeature(extensionFlags, nr::resource::MaterialFeatureFlag::sheen),
                          "extension flags should include sheen");
        nr::test::require(nr::resource::hasAnyFeature(extensionFlags, nr::resource::MaterialFeatureFlag::transmission),
                          "extension flags should include transmission");
        nr::test::require(nr::resource::hasAnyFeature(extensionFlags, nr::resource::MaterialFeatureFlag::anisotropy),
                          "extension flags should include anisotropy");

        auto const volumeThicknessBound =
            std::ranges::any_of(extensionMaterial.textureSlots, [&](const nr::resource::MaterialTextureSlot &slot) {
                return slot.texture == textureHandles[9];
            });
        nr::test::require(!volumeThicknessBound,
                          "unsupported volume_thickness should not bind any material texture slot");

        auto const conflictingRoughnessBound =
            std::ranges::any_of(material.textureSlots, [&](const nr::resource::MaterialTextureSlot &slot) {
                return slot.texture == textureHandles[10];
            });
        nr::test::require(!conflictingRoughnessBound,
                          "different metallicRoughness alias texture should be ignored after first slot assignment");

        auto meshHandle = scene.findMeshHandleByStableKey(nr::scene::SceneBridge::makeMeshCanonicalKey(sceneAsset, 0));
        nr::test::require(meshHandle.has_value(), "mesh handle should resolve by stable key");
        auto meshRecord = scene.tryGetMeshAsset(*meshHandle);
        nr::test::require(meshRecord.has_value(), "mesh record should exist");
        nr::test::require(!meshRecord->get().cpu.vertices.empty() &&
                              meshRecord->get().cpu.vertices.front().texCoord1 == glm::vec2{0.125f, 0.875f},
                          "mesh bridge should preserve the second UV set");
    }};

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

const nr::test::CaseRegistrar cameraAspectCase{
    "scene primary camera uses authored aspect and viewport overrides", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeRuntimeSceneAsset(true);

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto instanceHandle = scene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(instanceHandle.valid(), "instance registration should succeed");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto cameraHandle =
            scene.findCameraHandleByStableKey(nr::scene::SceneBridge::makeCameraCanonicalKey(sceneAsset, 0u));
        nr::test::require(cameraHandle.has_value(), "authored camera should be registered as a CPU scene asset");
        auto cameraRecord = scene.tryGetCameraAsset(*cameraHandle);
        nr::test::require(cameraRecord.has_value() && cameraRecord->get().cpuReady,
                          "authored camera CPU data should be ready without a GPU upload context");

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
    "scene extraction filters inactive instances", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto const initialRevisions = scene.revisionsSnapshot();
        auto const syntaxSnapshot = scene.revisionSnapshot().get<nr::scene::SceneRtRevisionDomain>();
        nr::test::requireEqual(syntaxSnapshot, initialRevisions.rt);
        auto sceneAsset = makeRuntimeSceneAsset(false);

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto activeInstance = scene.instantiate(templateHandle);
        auto inactiveInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{.activate = false});
        nr::test::require(templateHandle.valid(), "template registration should succeed");
        nr::test::require(activeInstance.valid(), "active instance should be valid");
        nr::test::require(inactiveInstance.valid(), "inactive instance should be valid");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto fallbackCamera = scene.tryGetPrimaryCamera();
        nr::test::require(fallbackCamera.has_value() && fallbackCamera->fallback,
                          "a scene without an authored camera should expose its CPU-only fallback camera");

        auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, false);
        auto packets = scene.extractPackets(rasterProfile);

        nr::test::require(initialRevisions.valid(), "scene identity should be valid");
        nr::test::requireEqual(packets.revisions.sceneIdentity, initialRevisions.sceneIdentity);
        nr::test::require(packets.revisions.rt.get<nr::scene::SceneRtRevisionDomain::topology>() !=
                              initialRevisions.rt.get<nr::scene::SceneRtRevisionDomain::topology>(),
                          "template and instance lifecycle should advance topology revision");
        nr::test::requireEqual(packets.domain, nr::scene::ScenePacketDomain::rasterDraw);
        nr::test::requireEqual(packets.rasterDraws.size(), std::size_t{1});
        nr::test::require(!packets.rasterGeometryBuffers.valid() && packets.rasterTextureHandlesById.empty(),
                          "diagnostic raster extraction should not pretend to own resident render bindings");
        nr::test::require(!nr::scene::rasterDrawPacketResolved(packets.rasterDraws.front(),
                                                               packets.rasterGeometryBuffers,
                                                               packets.rasterTextureHandlesById),
                          "requireReadyForDomain=false raster packets should remain explicitly non-renderable");

        scene.destroyInstance(activeInstance);
        auto afterDestroy = scene.extractPackets(rasterProfile);
        nr::test::require(afterDestroy.revisions.rt.get<nr::scene::SceneRtRevisionDomain::topology>() !=
                              packets.revisions.rt.get<nr::scene::SceneRtRevisionDomain::topology>(),
                          "destroying an instance should advance topology revision");
        nr::test::require(afterDestroy.rasterDraws.empty(), "destroying active instance should remove raster packets");
    }};

const nr::test::CaseRegistrar lightRuntimePacketCase{
    "scene extraction emits active directional point and spot light packets", [] {
        nr::rhi::Device device{};
        auto scene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto sceneAsset = makeLightSceneAsset();

        auto templateHandle = scene.registerTemplate(sceneAsset);
        auto activeInstance = scene.instantiate(templateHandle);
        auto inactiveInstance = scene.instantiate(templateHandle, nr::scene::SceneInstantiateInfo{.activate = false});
        nr::test::require(templateHandle.valid(), "light template registration should succeed");
        nr::test::require(activeInstance.valid(), "active light instance should be valid");
        nr::test::require(inactiveInstance.valid(), "inactive light instance should be valid");
        scene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto directionalHandle =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 0u));
        auto pointHandle =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 1u));
        auto spotHandle =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 2u));
        auto ambientHandle =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 3u));
        auto areaHandle =
            scene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(sceneAsset, 4u));
        nr::test::require(directionalHandle.has_value(), "directional light should be bridged");
        nr::test::require(pointHandle.has_value(), "point light should be bridged");
        nr::test::require(spotHandle.has_value(), "spot light should be bridged");
        nr::test::require(!ambientHandle.has_value(), "ambient light should be skipped");
        nr::test::require(!areaHandle.has_value(), "area light should be skipped");

        auto pointRecord = scene.tryGetLightAsset(*pointHandle);
        auto spotRecord = scene.tryGetLightAsset(*spotHandle);
        nr::test::require(pointRecord.has_value(), "point light record should exist");
        nr::test::require(spotRecord.has_value(), "spot light record should exist");
        nr::test::require(pointRecord->get().cpuReady && spotRecord->get().cpuReady,
                          "imported light CPU data should be ready without a GPU upload context");
        nr::test::require(almostEqual(pointRecord->get().cpu.range, 12.5f),
                          "point glTF source range should be preserved");
        nr::test::require(almostEqual(spotRecord->get().cpu.range, 8.0f), "spot glTF source range should be preserved");
        nr::test::require(almostEqual(pointRecord->get().cpu.intensity, 4.0f),
                          "point intensity should come from color magnitude");
        nr::test::require(almostEqual(pointRecord->get().cpu.color.g, 1.0f),
                          "point color should be normalized by intensity");
        nr::test::require(almostEqual(spotRecord->get().cpu.outerConeRadians, glm::radians(25.0f)),
                          "spot outer cone should be preserved");

        auto rasterProfile = registerProfile(scene, nr::scene::ScenePacketDomain::rasterDraw, false);
        auto packets = scene.extractPackets(rasterProfile);
        nr::test::requireEqual(packets.lights.size(), std::size_t{3});

        auto packetFor = [&](nr::resource::LightAssetHandle handle) -> const nr::scene::SceneLightPacket * {
            auto found = std::ranges::find_if(
                packets.lights, [&](const nr::scene::SceneLightPacket &packet) { return packet.light == handle; });
            return found == packets.lights.end() ? nullptr : std::addressof(*found);
        };

        auto const *pointPacket = packetFor(*pointHandle);
        auto const *spotPacket = packetFor(*spotHandle);
        nr::test::require(pointPacket != nullptr, "point light packet should exist");
        nr::test::require(spotPacket != nullptr, "spot light packet should exist");
        nr::test::require(almostEqual(pointPacket->position.x, 4.0f), "point world position x should be extracted");
        nr::test::require(almostEqual(pointPacket->position.y, 5.0f), "point world position y should be extracted");
        nr::test::require(almostEqual(pointPacket->position.z, 6.0f), "point world position z should be extracted");
        nr::test::require(almostEqual(spotPacket->direction.x, 0.0f), "spot direction x should default to node -Z");
        nr::test::require(almostEqual(spotPacket->direction.y, 0.0f), "spot direction y should default to node -Z");
        nr::test::require(almostEqual(spotPacket->direction.z, -1.0f), "spot direction z should default to node -Z");

        scene.destroyInstance(activeInstance);
        auto afterDestroy = scene.extractPackets(rasterProfile);
        nr::test::require(afterDestroy.lights.empty(), "destroying active instance should remove light packets");
    }};

const nr::test::CaseRegistrar sceneLightAliasGpuAbiCase{
    "scene light alias table gpu abi and energy weighting are stable", [] {
        nr::test::requireEqual(nr::scene::kSceneLightGpuAbiVersion, std::uint32_t{3u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightGpuHeader), std::size_t{16u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightGpuRecord), std::size_t{80u});
        nr::test::requireEqual(sizeof(nr::scene::SceneLightAliasGpuRecord), std::size_t{32u});

        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(glm::vec3{1.0f}, 2.0f), 2.0f),
                          "white light alias energy should equal intensity");
        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(glm::vec3{0.0f, 1.0f, 0.0f}, 2.0f), 1.4304f),
                          "green light alias energy should use Rec.709 luminance");
        nr::test::require(almostEqual(nr::scene::sceneLightAliasEnergy(glm::vec3{1.0f}, -2.0f), 0.0f),
                          "negative light intensity should have zero alias energy");

        auto emptyTable = nr::scene::buildSceneLightAliasTable(std::span<const nr::scene::SceneLightGpuRecord>{});
        nr::test::requireEqual(emptyTable.aliasCount, std::uint32_t{0u});
        nr::test::require(almostEqual(emptyTable.totalEnergy, 0.0f), "empty alias table total energy should be zero");
        nr::test::requireEqual(emptyTable.records.size(), std::size_t{1u});

        auto zeroEnergyRecords = std::array{
            makeAliasTableTestLight(glm::vec3{1.0f}, 0.0f),
            makeAliasTableTestLight(glm::vec3{0.0f}, 5.0f),
        };
        auto zeroEnergyTable = nr::scene::buildSceneLightAliasTable(zeroEnergyRecords);
        nr::test::requireEqual(zeroEnergyTable.aliasCount, std::uint32_t{0u});
        nr::test::require(almostEqual(zeroEnergyTable.totalEnergy, 0.0f),
                          "zero-energy alias table total should be zero");
        nr::test::requireEqual(zeroEnergyTable.records.size(), std::size_t{1u});

        auto weightedRecords = std::array{
            makeAliasTableTestLight(glm::vec3{1.0f}, 1.0f),
            makeAliasTableTestLight(glm::vec3{1.0f}, 3.0f),
        };
        auto weightedTable = nr::scene::buildSceneLightAliasTable(weightedRecords);
        nr::test::requireEqual(weightedTable.aliasCount, std::uint32_t{2u});
        nr::test::require(almostEqual(weightedTable.totalEnergy, 4.0f),
                          "weighted alias table total energy should sum weights");
        nr::test::requireEqual(weightedTable.records.size(), std::size_t{2u});

        auto const &firstAlias = weightedTable.records[0];
        nr::test::requireEqual(firstAlias.meta.x, std::uint32_t{0u});
        nr::test::requireEqual(firstAlias.meta.y, std::uint32_t{1u});
        nr::test::require(almostEqual(firstAlias.probabilities.x, 0.5f), "first alias accept threshold should be 0.5");
        nr::test::require(almostEqual(firstAlias.probabilities.y, 0.25f), "first alias primary pdf should be 0.25");
        nr::test::require(almostEqual(firstAlias.probabilities.z, 0.75f), "first alias secondary pdf should be 0.75");

        auto const &secondAlias = weightedTable.records[1];
        nr::test::requireEqual(secondAlias.meta.x, std::uint32_t{1u});
        nr::test::requireEqual(secondAlias.meta.y, std::uint32_t{1u});
        nr::test::require(almostEqual(secondAlias.probabilities.x, 1.0f), "second alias should always accept primary");
        nr::test::require(almostEqual(secondAlias.probabilities.y, 0.75f), "second alias primary pdf should be 0.75");
        nr::test::require(almostEqual(secondAlias.probabilities.z, 0.75f), "second alias secondary pdf should be 0.75");
    }};

} // namespace
