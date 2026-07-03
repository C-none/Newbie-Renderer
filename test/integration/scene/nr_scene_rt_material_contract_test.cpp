import std;
import dependency.math;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.scene;
import nr.test;
import nr.utils;

namespace
{
[[nodiscard]] std::filesystem::path projectRoot()
{
    return std::filesystem::path{std::string{nr::projectRoot}};
}

[[nodiscard]] bool hasFeature(const nr::scene::RtCompiledMaterial &material, nr::scene::RtMaterialFeatureFlag flag) noexcept
{
    return (material.header.featureFlags & static_cast<std::uint32_t>(flag)) != 0u;
}

[[nodiscard]] nr::resource::AlphaMode alphaModeFromHint(nr::load::MaterialAlphaModeHint hint) noexcept
{
    switch (hint)
    {
    case nr::load::MaterialAlphaModeHint::mask: return nr::resource::AlphaMode::mask;
    case nr::load::MaterialAlphaModeHint::blend: return nr::resource::AlphaMode::blend;
    default: return nr::resource::AlphaMode::opaque;
    }
}

[[nodiscard]] nr::resource::Material convertImportedMaterial(const nr::load::MaterialAsset &source)
{
    auto material = nr::resource::Material{};
    material.name = source.name;
    material.core.baseColorFactor = glm::vec4{
        source.baseColorFactor[0],
        source.baseColorFactor[1],
        source.baseColorFactor[2],
        source.baseColorFactor[3],
    };
    material.core.emissiveFactor = glm::vec3{
        source.emissiveFactor[0],
        source.emissiveFactor[1],
        source.emissiveFactor[2],
    };
    material.core.metallicFactor = source.metallicFactor;
    material.core.roughnessFactor = source.roughnessFactor;
    material.core.alphaMode = alphaModeFromHint(source.alphaModeHint);
    material.core.doubleSided = source.doubleSided;

    if (source.alphaCutoff.has_value())
    {
        material.core.alphaCutoff = *source.alphaCutoff;
    }
    if (source.normalScale.has_value())
    {
        material.core.normalScale = *source.normalScale;
    }
    if (source.occlusionStrength.has_value())
    {
        material.core.occlusionStrength = *source.occlusionStrength;
    }
    if (source.clearcoatFactor.has_value() || source.clearcoatRoughnessFactor.has_value())
    {
        material.clearcoat.emplace();
        if (source.clearcoatFactor.has_value())
        {
            material.clearcoat->factor = *source.clearcoatFactor;
        }
        if (source.clearcoatRoughnessFactor.has_value())
        {
            material.clearcoat->roughnessFactor = *source.clearcoatRoughnessFactor;
        }
    }
    if (source.sheenColorFactor.has_value() || source.sheenRoughnessFactor.has_value())
    {
        material.sheen.emplace();
        if (source.sheenColorFactor.has_value())
        {
            auto const &color = *source.sheenColorFactor;
            material.sheen->colorFactor = glm::vec3{color[0], color[1], color[2]};
        }
        if (source.sheenRoughnessFactor.has_value())
        {
            material.sheen->roughnessFactor = *source.sheenRoughnessFactor;
        }
    }
    if (source.transmissionFactor.has_value())
    {
        material.transmission.emplace();
        material.transmission->factor = *source.transmissionFactor;
    }
    if (source.anisotropyFactor.has_value() || source.anisotropyRotation.has_value())
    {
        material.anisotropy.emplace();
        if (source.anisotropyFactor.has_value())
        {
            material.anisotropy->factor = *source.anisotropyFactor;
        }
        if (source.anisotropyRotation.has_value())
        {
            material.anisotropy->rotation = *source.anisotropyRotation;
        }
    }

    std::ranges::for_each(source.textures, [&](const nr::load::MaterialTextureBinding &binding) {
        if (!nr::resource::materialTextureSlotSemanticValid(binding.semantic))
        {
            return;
        }

        auto &slot = material.slot(binding.semantic);
        if (slot.texture.valid())
        {
            return;
        }

        slot.texture = nr::resource::TextureHandle{binding.textureIndex + 1u, 1u};
        slot.uvSet = binding.uvChannel;
    });

    return material;
}

[[nodiscard]] nr::scene::SceneMaterialTextureIds makeTextureIds(const nr::resource::Material &material) noexcept
{
    auto textureIds = nr::scene::SceneMaterialTextureIds{};
    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto texture = material.textureSlots[slotIndex].texture;
        if (texture.valid())
        {
            textureIds[slotIndex] = static_cast<nr::scene::SceneTextureId>(texture.slot);
        }
    });
    return textureIds;
}

[[nodiscard]] std::vector<nr::scene::RtCompiledMaterial> compileAssetMaterials(const std::filesystem::path &relativePath)
{
    auto request = nr::load::SceneLoadRequest{};
    request.sourcePath = projectRoot() / relativePath;
    auto imported = nr::load::loadScene(request);
    nr::test::require(imported.has_value(), std::format("asset '{}' should import", relativePath.generic_string()));

    auto compiled = std::vector<nr::scene::RtCompiledMaterial>{};
    compiled.reserve(imported->materials.size());
    std::ranges::for_each(imported->materials, [&](const nr::load::MaterialAsset &source) {
        auto material = convertImportedMaterial(source);
        compiled.push_back(nr::scene::compileRtMaterial(material, makeTextureIds(material)));
    });
    return compiled;
}

void requireSourceMentionsExtension(const std::filesystem::path &relativePath, std::string_view extension)
{
    auto text = std::string{};
    {
        auto file = std::ifstream{projectRoot() / relativePath};
        nr::test::require(file.good(), std::format("asset '{}' should be readable", relativePath.generic_string()));
        text.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    }
    nr::test::require(text.find(extension) != std::string::npos, std::format("asset should mention {}", extension));
}

[[nodiscard]] std::size_t materialTextureSemanticCount(
    const nr::load::SceneAsset& scene,
    nr::resource::MaterialTextureSlotSemantic semantic)
{
    auto count = std::size_t{0};
    std::ranges::for_each(scene.materials, [&](const nr::load::MaterialAsset& material) {
        if (std::ranges::any_of(material.textures, [&](const nr::load::MaterialTextureBinding& binding) {
                return binding.semantic == semantic;
            }))
        {
            ++count;
        }
    });
    return count;
}

[[nodiscard]] const nr::load::MaterialTextureBinding& requireTextureBinding(
    const nr::load::MaterialAsset& material,
    nr::resource::MaterialTextureSlotSemantic semantic)
{
    auto bindingIt = std::ranges::find_if(material.textures, [&](const nr::load::MaterialTextureBinding& binding) {
        return binding.semantic == semantic;
    });
    nr::test::require(bindingIt != material.textures.end(),
                      std::format("material '{}' should bind {} texture",
                                  material.name,
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
    return *bindingIt;
}

[[nodiscard]] std::string textureFilename(
    const nr::load::SceneAsset& scene,
    const nr::load::MaterialTextureBinding& binding)
{
    nr::test::require(binding.textureIndex < scene.textures.size(),
                      std::format("texture binding index {} should be in range", binding.textureIndex));
    auto filename = scene.textures[binding.textureIndex].resolvedPath.filename().string();
    nr::test::require(!filename.empty(),
                      std::format("texture {} should have a resolved file path", binding.textureIndex));
    return filename;
}

void requireTextureFilename(
    const nr::load::SceneAsset& scene,
    const nr::load::MaterialAsset& material,
    nr::resource::MaterialTextureSlotSemantic semantic,
    std::string_view expectedFilename)
{
    auto const& binding = requireTextureBinding(material, semantic);
    auto const filename = textureFilename(scene, binding);
    nr::test::require(filename == expectedFilename,
                      std::format("material '{}' {} texture expected '{}', got '{}'",
                                  material.name,
                                  nr::resource::materialTextureSlotSemanticName(semantic),
                                  expectedFilename,
                                  filename));
}

struct ExpectedTextureSlot
{
    nr::resource::MaterialTextureSlotSemantic semantic{};
    std::string_view filename{};
};

inline constexpr auto kSponzaArchMaterialTextureSlots = std::array{
    ExpectedTextureSlot{
        .semantic = nr::resource::MaterialTextureSlotSemantic::baseColor,
        .filename = "14650633544276105767.jpg",
    },
    ExpectedTextureSlot{
        .semantic = nr::resource::MaterialTextureSlotSemantic::metallicRoughness,
        .filename = "4871783166746854860.jpg",
    },
    ExpectedTextureSlot{
        .semantic = nr::resource::MaterialTextureSlotSemantic::normal,
        .filename = "2051777328469649772.jpg",
    },
};

inline constexpr auto kSponzaGltfMaterial6PrimitiveMeshes = std::array{
    std::string_view{"meshes[0]-5"},
    std::string_view{"meshes[0]-8"},
    std::string_view{"meshes[0]-11"},
    std::string_view{"meshes[0]-16"},
    std::string_view{"meshes[0]-18"},
    std::string_view{"meshes[0]-20"},
    std::string_view{"meshes[0]-22"},
    std::string_view{"meshes[0]-24"},
    std::string_view{"meshes[0]-26"},
    std::string_view{"meshes[0]-28"},
    std::string_view{"meshes[0]-30"},
    std::string_view{"meshes[0]-32"},
    std::string_view{"meshes[0]-34"},
    std::string_view{"meshes[0]-36"},
    std::string_view{"meshes[0]-48"},
};

[[nodiscard]] bool materialHasTextureFilename(
    const nr::load::SceneAsset& scene,
    const nr::load::MaterialAsset& material,
    ExpectedTextureSlot expected)
{
    auto bindingIt = std::ranges::find_if(material.textures, [&](const nr::load::MaterialTextureBinding& binding) {
        return binding.semantic == expected.semantic;
    });
    if (bindingIt == material.textures.end())
    {
        return false;
    }

    nr::test::require(bindingIt->textureIndex < scene.textures.size(),
                      std::format("material '{}' {} texture index {} should be in range",
                                  material.name,
                                  nr::resource::materialTextureSlotSemanticName(expected.semantic),
                                  bindingIt->textureIndex));
    return textureFilename(scene, *bindingIt) == expected.filename;
}

[[nodiscard]] std::string materialTextureSummary(
    const nr::load::SceneAsset& scene,
    std::size_t materialIndex,
    const nr::load::MaterialAsset& material)
{
    auto summary = std::format("\n  [{}] '{}'", materialIndex, material.name);
    std::ranges::for_each(material.textures, [&](const nr::load::MaterialTextureBinding& binding) {
        auto const filename = binding.textureIndex < scene.textures.size()
                                  ? scene.textures[binding.textureIndex].resolvedPath.filename().string()
                                  : std::format("<out-of-range:{}>", binding.textureIndex);
        summary += std::format(" {}={}",
                               nr::resource::materialTextureSlotSemanticName(binding.semantic),
                               filename);
    });
    return summary;
}

[[nodiscard]] std::string materialTextureSummaries(const nr::load::SceneAsset& scene)
{
    auto summary = std::string{};
    auto materialIndices = std::views::iota(std::size_t{0}, scene.materials.size());
    std::ranges::for_each(materialIndices, [&](std::size_t materialIndex) {
        summary += materialTextureSummary(scene, materialIndex, scene.materials[materialIndex]);
    });
    return summary;
}

[[nodiscard]] std::size_t requireUniqueMaterialIndexByTextureFilenames(
    const nr::load::SceneAsset& scene,
    std::span<const ExpectedTextureSlot> expectedSlots)
{
    auto materialIndices = std::views::iota(std::size_t{0}, scene.materials.size());
    auto matches = materialIndices |
                   std::views::filter([&](std::size_t materialIndex) {
                       auto const& material = scene.materials[materialIndex];
                       return std::ranges::all_of(expectedSlots, [&](ExpectedTextureSlot expected) {
                           return materialHasTextureFilename(scene, material, expected);
                       });
                   }) |
                   std::ranges::to<std::vector>();

    nr::test::require(!matches.empty(),
                      std::format("expected exactly one imported material with Sponza arch textures; found none. Imported material textures:{}",
                                  materialTextureSummaries(scene)));
    nr::test::require(matches.size() == std::size_t{1},
                      std::format("expected exactly one imported material with Sponza arch textures; found {}. Imported material textures:{}",
                                  matches.size(),
                                  materialTextureSummaries(scene)));
    return matches.front();
}

[[nodiscard]] const nr::load::MeshAsset& requireMeshByName(
    const nr::load::SceneAsset& scene,
    std::string_view meshName)
{
    auto meshIt = std::ranges::find_if(scene.meshes, [&](const nr::load::MeshAsset& mesh) {
        return mesh.name == meshName;
    });
    nr::test::require(meshIt != scene.meshes.end(),
                      std::format("Sponza import should contain raw glTF primitive mesh '{}'", meshName));
    return *meshIt;
}

void requireGltfMaterial6UvOrientation(const nr::load::SceneAsset& scene)
{
    auto hasHighImageSpaceV = false;
    std::ranges::for_each(kSponzaGltfMaterial6PrimitiveMeshes, [&](std::string_view meshName) {
        auto const& mesh = requireMeshByName(scene, meshName);
        nr::test::require(!mesh.vertices.empty(), std::format("Sponza primitive mesh '{}' should contain vertices", meshName));
        auto const uvVValues = mesh.vertices |
                               std::views::transform([](const nr::load::VertexAsset& vertex) {
                                   return vertex.texCoord0[1];
                               });
        hasHighImageSpaceV = hasHighImageSpaceV || std::ranges::max(uvVValues) > 0.9f;
    });

    nr::test::require(
        hasHighImageSpaceV,
        "Sponza material 6 primitive meshes should keep glTF image-space uv0 V orientation after Assimp import");
}

void requireRtTextureRef(
    const nr::scene::RtCompiledMaterial& material,
    nr::resource::MaterialTextureSlotSemantic semantic)
{
    auto const slot = static_cast<std::uint32_t>(nr::resource::materialTextureSlotIndex(semantic));
    auto refIt = std::ranges::find_if(material.textureRefs, [&](const nr::scene::RtMaterialTextureRef& textureRef) {
        return textureRef.slot == slot;
    });
    nr::test::require(refIt != material.textureRefs.end(),
                      std::format("compiled RT material should contain {} texture ref",
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
    nr::test::require(refIt->textureId != nr::scene::kDefaultSceneTextureId,
                      std::format("compiled RT material {} texture id should be non-zero",
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
}

struct TangentSignSummary
{
    std::size_t positive = 0;
    std::size_t negative = 0;
};

[[nodiscard]] TangentSignSummary tangentSignSummary(const nr::load::SceneAsset& scene) noexcept
{
    auto summary = TangentSignSummary{};
    std::ranges::for_each(scene.meshes, [&](const nr::load::MeshAsset& mesh) {
        std::ranges::for_each(mesh.vertices, [&](const nr::load::VertexAsset& vertex) {
            auto const tangentSign = vertex.tangent[3];
            if (tangentSign < 0.0f)
            {
                ++summary.negative;
            }
            else if (tangentSign > 0.0f)
            {
                ++summary.positive;
            }
        });
    });
    return summary;
}

const nr::test::CaseRegistrar rtMaterialCompilerCase{
    "scene RT material compiler builds canonical layer order and marks unsupported anisotropy",
    [] {
        auto material = nr::resource::Material{};
        material.core.baseColorFactor = glm::vec4{0.8f, 0.2f, 0.1f, 1.0f};
        material.clearcoat.emplace();
        material.clearcoat->factor = 0.7f;
        material.clearcoat->roughnessFactor = 0.2f;
        material.sheen.emplace();
        material.sheen->colorFactor = glm::vec3{0.1f, 0.2f, 0.9f};
        material.sheen->roughnessFactor = 0.6f;
        material.transmission.emplace();
        material.transmission->factor = 0.5f;
        material.anisotropy.emplace();
        material.anisotropy->factor = 1.0f;
        material.anisotropy->rotation = 0.25f;
        material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).texture = nr::resource::TextureHandle{2u, 1u};
        material.slot(nr::resource::MaterialTextureSlotSemantic::baseColor).uvSet = 0u;
        material.slot(nr::resource::MaterialTextureSlotSemantic::anisotropy).texture = nr::resource::TextureHandle{3u, 1u};

        auto textureIds = nr::scene::SceneMaterialTextureIds{};
        textureIds[nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::baseColor)] = 7u;
        textureIds[nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::anisotropy)] = 8u;

        auto compiled = nr::scene::compileRtMaterial(material, textureIds);

        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::clearcoat));
        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::sheen));
        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::transmission));
        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::unsupportedAnisotropy));
        nr::test::requireEqual(compiled.layers.size(), std::size_t{4});
        nr::test::requireEqual(compiled.layers[0].kind, static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::clearcoat));
        nr::test::requireEqual(compiled.layers[1].kind, static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::sheen));
        nr::test::requireEqual(compiled.layers[2].kind, static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::baseSurface));
        nr::test::requireEqual(compiled.layers[3].kind, static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::transmission));
        nr::test::require(
            std::ranges::none_of(compiled.layers, [](const nr::scene::RtMaterialLayerRecord &layer) {
                return layer.kind > static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::transmission);
            }),
            "unsupported features must not create RT layers");
        nr::test::requireEqual(compiled.textureRefs.size(), std::size_t{2});
    }};

const nr::test::CaseRegistrar rtSampleAssetsCase{
    "scene RT material compiler covers Khronos sample asset material features",
    [] {
        auto toyCar = compileAssetMaterials("assets/glTF-Sample-Assets/Models/ToyCar/glTF/ToyCar.gltf");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasFeature(material, nr::scene::RtMaterialFeatureFlag::clearcoat);
                          }),
                          "ToyCar should produce at least one RT clearcoat material");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasFeature(material, nr::scene::RtMaterialFeatureFlag::sheen);
                          }),
                          "ToyCar should produce at least one RT sheen material");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasFeature(material, nr::scene::RtMaterialFeatureFlag::transmission);
                          }),
                          "ToyCar should produce at least one RT transmission material");

        auto anisotropy = compileAssetMaterials("assets/glTF-Sample-Assets/Models/AnisotropyRotationTest/glTF/AnisotropyRotationTest.gltf");
        nr::test::require(std::ranges::any_of(anisotropy, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasFeature(material, nr::scene::RtMaterialFeatureFlag::unsupportedAnisotropy);
                          }),
                          "AnisotropyRotationTest should be imported but marked unsupported for RT");
    }};

const nr::test::CaseRegistrar directionalLightImportCase{
    "DirectionalLight glTF import bridges punctual light packets",
    [] {
        auto request = nr::load::SceneLoadRequest{};
        request.sourcePath = projectRoot() / "assets/glTF-Sample-Assets/Models/DirectionalLight/glTF/DirectionalLight.gltf";
        auto imported = nr::load::loadScene(request);
        nr::test::require(imported.has_value(), "DirectionalLight should import");

        nr::test::requireEqual(imported->lights.size(), std::size_t{1});
        auto const& sourceLight = imported->lights.front();
        nr::test::require(sourceLight.type == "directional", "DirectionalLight source light should import as directional");
        nr::test::require(sourceLight.nodeIndex < imported->nodes.size(), "DirectionalLight source light should reference a valid node");
        nr::test::require(sourceLight.colorDiffuse[0] > 0.0f, "DirectionalLight source light should carry positive red energy");

        nr::rhi::Device device{};
        auto runtimeScene = nr::scene::Scene(nr::scene::SceneCreateInfo{.device = device});
        auto templateHandle = runtimeScene.registerTemplate(*imported);
        auto instanceHandle = runtimeScene.instantiate(templateHandle);
        nr::test::require(templateHandle.valid(), "DirectionalLight scene template should register");
        nr::test::require(instanceHandle.valid(), "DirectionalLight scene instance should instantiate");
        runtimeScene.updateSimulation(nr::scene::SceneUpdateInput{.deltaSeconds = 1.0f / 60.0f});

        auto lightHandle = runtimeScene.findLightHandleByStableKey(nr::scene::SceneBridge::makeLightCanonicalKey(*imported, 0u));
        nr::test::require(lightHandle.has_value(), "DirectionalLight source light should bridge to runtime scene");
        auto lightRecord = runtimeScene.tryGetLightAsset(*lightHandle);
        nr::test::require(lightRecord.has_value(), "DirectionalLight runtime light record should exist");
        nr::test::require(lightRecord->get().cpu.type == nr::resource::LightType::directional,
                          "DirectionalLight runtime light should remain directional");
        nr::test::require(nr::scene::sceneLightAliasEnergy(lightRecord->get().cpu.color, lightRecord->get().cpu.intensity) > 0.0f,
                          "DirectionalLight runtime light should produce positive alias energy");

        auto lightProfile = runtimeScene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
            .debugName = "directional_light_runtime",
            .requireReadyForDomain = false,
        });
        auto lightPackets = runtimeScene.extractPackets(lightProfile);
        nr::test::requireEqual(lightPackets.lights.size(), std::size_t{1});
        auto const& lightPacket = lightPackets.lights.front();
        nr::test::require(lightPacket.light == *lightHandle, "DirectionalLight packet should reference the runtime light");
        nr::test::require(glm::dot(lightPacket.direction, lightPacket.direction) > 0.0f,
                          "DirectionalLight packet should carry a non-zero direction");
    }};

const nr::test::CaseRegistrar sponzaMaterialTextureImportCase{
    "Sponza glTF import preserves textured arch material slots",
    [] {
        auto request = nr::load::SceneLoadRequest{};
        request.sourcePath = projectRoot() / "assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf";
        auto imported = nr::load::loadScene(request);
        nr::test::require(imported.has_value(), "Sponza should import");

        nr::test::require(imported->materials.size() >= std::size_t{25},
                          std::format("Sponza should keep at least 25 source materials, got {}",
                                      imported->materials.size()));
        nr::test::requireEqual(
            materialTextureSemanticCount(*imported, nr::resource::MaterialTextureSlotSemantic::baseColor),
            std::size_t{25});
        nr::test::requireEqual(
            materialTextureSemanticCount(*imported, nr::resource::MaterialTextureSlotSemantic::normal),
            std::size_t{24});
        nr::test::requireEqual(
            materialTextureSemanticCount(*imported, nr::resource::MaterialTextureSlotSemantic::metallicRoughness),
            std::size_t{24});

        auto const archMaterialIndex = requireUniqueMaterialIndexByTextureFilenames(
            *imported,
            kSponzaArchMaterialTextureSlots);
        requireGltfMaterial6UvOrientation(*imported);

        auto const& archMaterial = imported->materials[archMaterialIndex];
        std::ranges::for_each(kSponzaArchMaterialTextureSlots, [&](ExpectedTextureSlot expected) {
            requireTextureFilename(*imported, archMaterial, expected.semantic, expected.filename);
        });

        auto archRuntimeMaterial = convertImportedMaterial(archMaterial);
        auto compiledArchMaterial = nr::scene::compileRtMaterial(
            archRuntimeMaterial,
            makeTextureIds(archRuntimeMaterial));
        requireRtTextureRef(compiledArchMaterial, nr::resource::MaterialTextureSlotSemantic::baseColor);
        requireRtTextureRef(compiledArchMaterial, nr::resource::MaterialTextureSlotSemantic::metallicRoughness);
        requireRtTextureRef(compiledArchMaterial, nr::resource::MaterialTextureSlotSemantic::normal);

        auto const tangentSigns = tangentSignSummary(*imported);
        nr::test::require(tangentSigns.negative > 0u,
                          "Sponza should preserve negative tangent handedness for normal-mapped mirrored UVs");
        nr::test::require(tangentSigns.positive > 0u,
                          "Sponza should preserve positive tangent handedness for normal-mapped surfaces");
    }};

const nr::test::CaseRegistrar unsupportedExtensionAssetCase{
    "scene RT material compiler keeps unsupported glTF extensions out of canonical layers",
    [] {
        auto unsupportedAssets = std::array{
            std::pair{
                std::filesystem::path{"assets/glTF-Sample-Assets/Models/CompareIridescence/glTF/CompareIridescence.gltf"},
                std::string_view{"KHR_materials_iridescence"},
            },
            std::pair{
                std::filesystem::path{"assets/glTF-Sample-Assets/Models/DiffuseTransmissionTest/glTF/DiffuseTransmissionTest.gltf"},
                std::string_view{"KHR_materials_diffuse_transmission"},
            },
            std::pair{
                std::filesystem::path{"assets/glTF-Sample-Assets/Models/CompareDispersion/glTF/CompareDispersion.gltf"},
                std::string_view{"KHR_materials_dispersion"},
            },
        };

        std::ranges::for_each(unsupportedAssets, [](const auto &asset) {
            requireSourceMentionsExtension(asset.first, asset.second);
            auto compiled = compileAssetMaterials(asset.first);
            nr::test::require(
                std::ranges::all_of(compiled, [](const nr::scene::RtCompiledMaterial &material) {
                    return std::ranges::all_of(material.layers, [](const nr::scene::RtMaterialLayerRecord &layer) {
                        return layer.kind <= static_cast<std::uint32_t>(nr::scene::RtMaterialLayerKind::transmission);
                    });
                }),
                std::format("{} should not create unsupported RT layer kinds", asset.second));
        });
    }};
} // namespace
