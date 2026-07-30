import std;
import dependency.json;
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
    return nr::scene::hasAnyRtMaterialFeature(material.header.featureFlags, flag);
}

[[nodiscard]] bool hasLayer(const nr::scene::RtCompiledMaterial &material, nr::scene::RtMaterialLayerFlag flag) noexcept
{
    return nr::scene::hasRtMaterialLayer(material.header.layerFlags, flag);
}

[[nodiscard]] bool nearlyEqual(float lhs, float rhs, float epsilon = 1.0e-6f) noexcept
{
    return std::abs(lhs - rhs) <= epsilon;
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
    if (source.ior.has_value())
    {
        material.ior.emplace();
        material.ior->ior = *source.ior;
    }
    if (source.thicknessFactor.has_value())
    {
        material.volumeBoundary.emplace();
        material.volumeBoundary->thicknessFactor = *source.thicknessFactor;
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

void requireSourceDeclaresExtension(const std::filesystem::path &relativePath, std::string_view extension)
{
    auto text = std::string{};
    {
        auto file = std::ifstream{projectRoot() / relativePath};
        nr::test::require(file.good(), std::format("asset '{}' should be readable", relativePath.generic_string()));
        text.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
    }
    auto parsed = dependency::json::parseJson(text);
    nr::test::require(parsed.valid(), std::format("asset '{}' should contain valid JSON", relativePath.generic_string()));
    auto const *root = std::get_if<dependency::json::JsonValue::Object>(&parsed.value->storage);
    nr::test::require(root != nullptr, "glTF root should be a JSON object");
    auto const extensions = root->find("extensionsUsed");
    nr::test::require(extensions != root->end(), "glTF should declare extensionsUsed");
    auto const *extensionNames = std::get_if<dependency::json::JsonValue::Array>(&extensions->second.storage);
    nr::test::require(extensionNames != nullptr, "glTF extensionsUsed should be an array");
    auto const declared = std::ranges::any_of(*extensionNames, [&](const dependency::json::JsonValue &entry) {
        auto const *name = std::get_if<std::string>(&entry.storage);
        return name != nullptr && *name == extension;
    });
    nr::test::require(declared, std::format("asset should declare {}", extension));
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
    auto const slotIndex = nr::resource::materialTextureSlotIndex(semantic);
    nr::test::require(slotIndex < material.textureRefs.size(),
                      std::format("compiled RT material should contain {} texture ref",
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
    nr::test::require(material.textureRefs[slotIndex].textureId != nr::scene::kDefaultSceneTextureId,
                      std::format("compiled RT material {} texture id should be non-zero",
                                  nr::resource::materialTextureSlotSemanticName(semantic)));
}

[[nodiscard]] std::string readProjectFile(const std::filesystem::path& relativePath)
{
    auto file = std::ifstream{projectRoot() / relativePath};
    nr::test::require(file.good(), std::format("project file '{}' should be readable", relativePath.generic_string()));
    return std::string{
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{},
    };
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
        material.ior.emplace();
        material.ior->ior = 1.33f;
        material.volumeBoundary.emplace();
        material.volumeBoundary->thicknessFactor = 0.25f;
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

        nr::test::require(hasLayer(compiled, nr::scene::RtMaterialLayerFlag::clearcoat));
        nr::test::require(hasLayer(compiled, nr::scene::RtMaterialLayerFlag::sheen));
        nr::test::require(hasLayer(compiled, nr::scene::RtMaterialLayerFlag::transmission));
        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::volumeBoundary));
        nr::test::require(hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::unsupportedAnisotropy));
        nr::test::requireEqual(compiled.layers.size(), std::size_t{4});
        nr::test::requireEqual(compiled.layers[0].layer, nr::scene::RtMaterialLayerFlag::baseSurface);
        nr::test::requireEqual(compiled.layers[1].layer, nr::scene::RtMaterialLayerFlag::clearcoat);
        nr::test::requireEqual(compiled.layers[2].layer, nr::scene::RtMaterialLayerFlag::sheen);
        nr::test::requireEqual(compiled.layers[3].layer, nr::scene::RtMaterialLayerFlag::transmission);
        nr::test::requireEqual(
            compiled.layers[3].aux0,
            static_cast<std::uint32_t>(nr::scene::RtTransmissionMode::volume));
        nr::test::require(nearlyEqual(compiled.layers[3].p0.x, 0.5f));
        nr::test::require(nearlyEqual(compiled.layers[3].p0.y, 1.33f));
        nr::test::require(
            std::ranges::none_of(compiled.layers, [](const nr::scene::RtMaterialLayerRecord &layer) {
                return layer.layer > nr::scene::RtMaterialLayerFlag::transmission;
            }),
            "unsupported features must not create RT layers");
        nr::test::requireEqual(compiled.textureRefs.size(), std::size_t{12});
    }};

const nr::test::CaseRegistrar rtMaterialLayerFlagMatrixCase{
    "scene RT material layer-flag matrix covers unlit, lit combinations, dense refs and default normals",
    [] {
        using MaterialTextureSlotSemantic = nr::resource::MaterialTextureSlotSemantic;
        using Layer = nr::scene::RtMaterialLayerFlag;

        // unlit -> layerFlags none, no layer records; authored PBR extension data is ignored.
        {
            auto material = nr::resource::Material{};
            material.unlit = true;
            material.clearcoat.emplace();
            material.transmission.emplace();
            material.transmission->factor = 1.0f;
            material.volumeBoundary.emplace();
            material.volumeBoundary->thicknessFactor = 1.0f;
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::requireEqual(compiled.header.layerFlags, Layer::none);
            nr::test::requireEqual(compiled.header.layerCount, 0u);
            nr::test::requireEqual(compiled.layers.size(), std::size_t{0});
            nr::test::requireEqual(compiled.textureRefs.size(), std::size_t{12});
            nr::test::require(
                !hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::volumeBoundary),
                "unlit materials must not retain an ignored PBR volume boundary");
        }

        // plain metallic-roughness -> baseSurface only; layer info must not leak into featureFlags;
        // absent base normal writes effective normal scale 0.
        {
            auto material = nr::resource::Material{};
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::requireEqual(compiled.header.layerFlags, Layer::baseSurface);
            nr::test::requireEqual(compiled.layers.size(), std::size_t{1});
            nr::test::requireEqual(compiled.layers[0].layer, Layer::baseSurface);
            nr::test::requireEqual(static_cast<std::uint32_t>(compiled.header.featureFlags), 0u,
                                   "layer classification must not leak into RtMaterialFeatureFlag");
            nr::test::require(nearlyEqual(compiled.header.roughnessNormalOcclusionAlpha.y, 0.0f),
                              "absent base normal must write effective normal scale 0");
        }

        // base + single clearcoat; absent clearcoat normal -> clearcoat effective normal scale 0 (p0.z).
        {
            auto material = nr::resource::Material{};
            material.clearcoat.emplace();
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::require(hasLayer(compiled, Layer::baseSurface));
            nr::test::require(hasLayer(compiled, Layer::clearcoat));
            nr::test::require(!hasLayer(compiled, Layer::sheen));
            nr::test::require(!hasLayer(compiled, Layer::transmission));
            nr::test::requireEqual(compiled.layers.size(), std::size_t{2});
            nr::test::requireEqual(compiled.layers[0].layer, Layer::baseSurface);
            nr::test::requireEqual(compiled.layers[1].layer, Layer::clearcoat);
            nr::test::require(nearlyEqual(compiled.layers[1].p0.z, 0.0f),
                              "absent clearcoat normal must write effective normal scale 0");
        }

        // base + single sheen.
        {
            auto material = nr::resource::Material{};
            material.sheen.emplace();
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::require(hasLayer(compiled, Layer::sheen));
            nr::test::requireEqual(compiled.layers.size(), std::size_t{2});
            nr::test::requireEqual(compiled.layers[1].layer, Layer::sheen);
        }

        // A zero scalar transmission factor cannot be revived by a transmission texture.
        {
            auto material = nr::resource::Material{};
            material.transmission.emplace();
            material.slot(MaterialTextureSlotSemantic::transmission).texture = nr::resource::TextureHandle{2u, 1u};
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::require(!hasLayer(compiled, Layer::transmission));
            nr::test::requireEqual(compiled.layers.size(), std::size_t{1});
            nr::test::require(
                !nr::resource::hasAnyFeature(
                    material.featureFlags(),
                    nr::resource::MaterialFeatureFlag::transmission),
                "zero transmission factor must not enable the resource transmission feature");
            nr::test::require(nearlyEqual(compiled.header.transmissionClearcoatSheen.x, 0.0f));

            material.transmission->factor = -0.25f;
            auto negativeCompiled = nr::scene::compileRtMaterial(material);
            nr::test::require(!hasLayer(negativeCompiled, Layer::transmission));
            nr::test::require(nearlyEqual(negativeCompiled.header.transmissionClearcoatSheen.x, 0.0f));
        }

        // Positive transmission defaults to a thin 1.5-IOR boundary.
        {
            auto material = nr::resource::Material{};
            material.transmission.emplace();
            material.transmission->factor = 0.75f;
            material.volumeBoundary.emplace();
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::require(hasLayer(compiled, Layer::transmission));
            nr::test::requireEqual(compiled.layers.size(), std::size_t{2});
            nr::test::requireEqual(compiled.layers[1].layer, Layer::transmission);
            nr::test::requireEqual(
                compiled.layers[1].aux0,
                static_cast<std::uint32_t>(nr::scene::RtTransmissionMode::thin));
            nr::test::require(nearlyEqual(compiled.layers[1].p0.x, 0.75f));
            nr::test::require(nearlyEqual(compiled.layers[1].p0.y, 1.5f));
            nr::test::require(
                nearlyEqual(compiled.header.transmissionClearcoatSheen.x, 0.0f),
                "positive transmission must keep factor/IOR/mode single-sourced in its layer record");
        }

        // Positive thickness selects the volume boundary without changing the layer/SBT variant.
        {
            auto material = nr::resource::Material{};
            material.transmission.emplace();
            material.transmission->factor = 1.0f;
            material.volumeBoundary.emplace();
            material.volumeBoundary->thicknessFactor = 0.5f;
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::requireEqual(
                compiled.layers[1].aux0,
                static_cast<std::uint32_t>(nr::scene::RtTransmissionMode::volume));
            nr::test::require(
                hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::volumeBoundary),
                "volume transmission should expose its runtime any-hit back-face policy");
        }

        // KHR_materials_ior zero is the positive-infinity compatibility sentinel, not an invalid IOR.
        {
            auto material = nr::resource::Material{};
            material.transmission.emplace();
            material.transmission->factor = 1.0f;
            material.ior.emplace();
            material.ior->ior = 0.0f;
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::require(nearlyEqual(compiled.layers[1].p0.y, 0.0f));
        }

        // base + all three optional layers -> canonical order base -> clearcoat -> sheen -> transmission.
        {
            auto material = nr::resource::Material{};
            material.clearcoat.emplace();
            material.sheen.emplace();
            material.transmission.emplace();
            material.transmission->factor = 1.0f;
            auto compiled = nr::scene::compileRtMaterial(material);
            nr::test::requireEqual(compiled.layers.size(), std::size_t{4});
            nr::test::requireEqual(compiled.layers[0].layer, Layer::baseSurface);
            nr::test::requireEqual(compiled.layers[1].layer, Layer::clearcoat);
            nr::test::requireEqual(compiled.layers[2].layer, Layer::sheen);
            nr::test::requireEqual(compiled.layers[3].layer, Layer::transmission);
        }

        // fallback RT material must be lit (baseSurface), never unlit.
        {
            auto fallback = nr::scene::makeFallbackRtMaterial();
            nr::test::requireEqual(fallback.header.layerFlags, Layer::baseSurface);
            nr::test::require(fallback.header.layerFlags != Layer::none,
                              "fallback RT material must not be unlit");
        }

        // Dense texture refs: fixed 12 entries in slot order; authored -> id, absent -> id 0. Each
        // entry transports UV selection and its affine transform without a redundant slot field.
        {
            auto material = nr::resource::Material{};
            material.core.normalScale = 0.5f;
            auto& baseColorSlot = material.slot(MaterialTextureSlotSemantic::baseColor);
            baseColorSlot.texture = nr::resource::TextureHandle{2u, 1u};
            baseColorSlot.uvSet = 1u;
            baseColorSlot.transform.linear = glm::vec4{2.0f, 0.25f, -0.5f, 3.0f};
            baseColorSlot.transform.offset = glm::vec2{0.125f, -0.25f};

            auto& normalSlot = material.slot(MaterialTextureSlotSemantic::normal);
            normalSlot.texture = nr::resource::TextureHandle{3u, 1u};
            normalSlot.transform.linear = glm::vec4{0.5f, -0.25f, 0.75f, 1.5f};
            normalSlot.transform.offset = glm::vec2{-0.125f, 0.375f};

            auto& occlusionSlot = material.slot(MaterialTextureSlotSemantic::occlusion);
            occlusionSlot.texture = nr::resource::TextureHandle{4u, 1u};
            occlusionSlot.uvSet = 1u;
            occlusionSlot.transform.linear = glm::vec4{4.0f, 0.0f, 0.0f, 2.0f};
            occlusionSlot.transform.offset = glm::vec2{0.5f, 0.25f};

            auto ids = nr::scene::SceneMaterialTextureIds{};
            ids[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::baseColor)] = 7u;
            ids[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::normal)] = 9u;
            ids[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::occlusion)] = 11u;
            auto compiled = nr::scene::compileRtMaterial(material, ids);
            nr::test::requireEqual(sizeof(nr::scene::RtMaterialTextureRef), std::size_t{32u});
            nr::test::requireEqual(compiled.textureRefs.size(), std::size_t{12});

            auto const& baseColorRef =
                compiled.textureRefs[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::baseColor)];
            nr::test::requireEqual(baseColorRef.textureId, 7u);
            nr::test::requireEqual(baseColorRef.uvSet, 1u);
            nr::test::require(nearlyEqual(baseColorRef.uvLinear.x, 2.0f));
            nr::test::require(nearlyEqual(baseColorRef.uvLinear.y, 0.25f));
            nr::test::require(nearlyEqual(baseColorRef.uvLinear.z, -0.5f));
            nr::test::require(nearlyEqual(baseColorRef.uvLinear.w, 3.0f));
            nr::test::require(nearlyEqual(baseColorRef.uvOffset.x, 0.125f));
            nr::test::require(nearlyEqual(baseColorRef.uvOffset.y, -0.25f));

            auto const& normalRef =
                compiled.textureRefs[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::normal)];
            nr::test::requireEqual(normalRef.textureId, 9u);
            nr::test::require(nearlyEqual(normalRef.uvLinear.x, 0.5f));
            nr::test::require(nearlyEqual(normalRef.uvLinear.y, -0.25f));
            nr::test::require(nearlyEqual(normalRef.uvLinear.z, 0.75f));
            nr::test::require(nearlyEqual(normalRef.uvLinear.w, 1.5f));
            nr::test::require(nearlyEqual(normalRef.uvOffset.x, -0.125f));
            nr::test::require(nearlyEqual(normalRef.uvOffset.y, 0.375f));

            auto const& occlusionRef =
                compiled.textureRefs[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::occlusion)];
            nr::test::requireEqual(occlusionRef.textureId, 11u);
            nr::test::requireEqual(occlusionRef.uvSet, 1u);
            nr::test::require(nearlyEqual(occlusionRef.uvLinear.x, 4.0f));
            nr::test::require(nearlyEqual(occlusionRef.uvLinear.w, 2.0f));
            nr::test::require(nearlyEqual(occlusionRef.uvOffset.x, 0.5f));
            nr::test::require(nearlyEqual(occlusionRef.uvOffset.y, 0.25f));

            nr::test::requireEqual(
                compiled.textureRefs[nr::resource::materialTextureSlotIndex(MaterialTextureSlotSemantic::metallicRoughness)].textureId,
                0u,
                "absent texture slot must resolve to id 0");
            nr::test::require(nearlyEqual(compiled.header.roughnessNormalOcclusionAlpha.y, 0.5f),
                              "authored base normal must preserve the real normal scale");
        }
    }};

const nr::test::CaseRegistrar rtMaterialShaderUvAndAoPolicyCase{
    "RT material shader centralizes UV transforms and keeps ambient occlusion unshaded",
    [] {
        auto const materialTypes = readProjectFile("shader/include/material/types.slang");
        auto const materialSampling = readProjectFile("shader/include/material/sampling.slang");
        auto const hitSurface = readProjectFile("shader/include/rt/hitSurface.slang");

        nr::test::require(
            materialTypes.contains("public float2 selectMaterialUv(") &&
                materialTypes.contains("public float2 transformMaterialUv(") &&
                materialTypes.contains("public float2 materialTextureUv(") &&
                materialTypes.contains("dot(uvLinear.xy, uv)") &&
                materialTypes.contains("dot(uvLinear.zw, uv)) + uvOffset"),
            "material shader should centralize UV-set selection and affine texture transforms");
        nr::test::require(
            hitSurface.contains("kRtVertexOffsetTexCoord0 = 40u") &&
                hitSurface.contains("kRtVertexOffsetTexCoord1 = 48u") &&
                hitSurface.contains("kRtVertexOffsetColor0 = 56u"),
            "RT hit-surface atlas offsets should retain the resource Vertex UV/color layout");

        auto const genericSample = materialSampling.find("public float4 sampleMaterialTexture(");
        auto const normalSample = materialSampling.find("public float3 applyNormalMapSlot(");
        auto const coreSample = materialSampling.find("public MaterialSample sampleCoreMaterial(");
        auto const layerSample = materialSampling.find("// Canonical layer-record parser.");
        nr::test::require(
            genericSample != std::string::npos &&
                normalSample != std::string::npos &&
                coreSample != std::string::npos &&
                layerSample != std::string::npos,
            "material shader sampling functions should remain discoverable");
        nr::test::require(
            materialSampling.substr(genericSample, normalSample - genericSample).contains("materialTextureUv("),
            "generic texture sampling should use the centralized transformed UV helper");
        nr::test::require(
            materialSampling.substr(normalSample, coreSample - normalSample).contains("materialTextureUv("),
            "base and clearcoat normal sampling should use the centralized transformed UV helper");

        auto const coreSamplingBody = materialSampling.substr(coreSample, layerSample - coreSample);
        nr::test::require(
            !coreSamplingBody.contains("MaterialTextureSlot.occlusion"),
            "ambient occlusion must not be sampled by RT core material shading");
        nr::test::require(
            !coreSamplingBody.contains("roughnessNormalOcclusionAlpha.z"),
            "ambient-occlusion strength must not multiply RT core material lighting");

        auto shaderSources = std::filesystem::recursive_directory_iterator{projectRoot() / "shader"} |
                             std::views::filter([](const std::filesystem::directory_entry& entry) {
                                 return entry.is_regular_file() && entry.path().extension() == ".slang";
                             });
        std::ranges::for_each(shaderSources, [](const std::filesystem::directory_entry& entry) {
            auto const relativePath = std::filesystem::relative(entry.path(), projectRoot());
            auto const source = readProjectFile(relativePath);
            nr::test::require(
                !source.contains("MaterialTextureSlot.occlusion"),
                std::format(
                    "ambient-occlusion texture sampling must remain absent from shader source '{}'",
                    relativePath.generic_string()));
            nr::test::require(
                !source.contains("roughnessNormalOcclusionAlpha.z") &&
                    !source.contains("occlusionStrength") &&
                    !source.contains("ambientOcclusion"),
                std::format(
                    "ambient-occlusion strength must remain absent from shader source '{}'",
                    relativePath.generic_string()));
        });
    }};

const nr::test::CaseRegistrar rtSampleAssetsCase{
    "scene RT material compiler covers Khronos sample asset material features",
    [] {
        auto toyCar = compileAssetMaterials("assets/glTF-Sample-Assets/Models/ToyCar/glTF/ToyCar.gltf");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasLayer(material, nr::scene::RtMaterialLayerFlag::clearcoat);
                          }),
                          "ToyCar should produce at least one RT clearcoat material");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasLayer(material, nr::scene::RtMaterialLayerFlag::sheen);
                          }),
                          "ToyCar should produce at least one RT sheen material");
        nr::test::require(std::ranges::any_of(toyCar, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasLayer(material, nr::scene::RtMaterialLayerFlag::transmission);
                          }),
                          "ToyCar should produce at least one RT transmission material");

        auto anisotropy = compileAssetMaterials("assets/glTF-Sample-Assets/Models/AnisotropyRotationTest/glTF/AnisotropyRotationTest.gltf");
        nr::test::require(std::ranges::any_of(anisotropy, [](const nr::scene::RtCompiledMaterial &material) {
                              return hasFeature(material, nr::scene::RtMaterialFeatureFlag::unsupportedAnisotropy);
                          }),
                          "AnisotropyRotationTest should be imported but marked unsupported for RT");

        auto compareIor = compileAssetMaterials("assets/glTF-Sample-Assets/Models/CompareIor/glTF/CompareIor.gltf");
        nr::test::require(
            std::ranges::any_of(compareIor, [](const nr::scene::RtCompiledMaterial& material) {
                return std::ranges::any_of(material.layers, [](const nr::scene::RtMaterialLayerRecord& layer) {
                    return layer.layer == nr::scene::RtMaterialLayerFlag::transmission &&
                           layer.aux0 == static_cast<std::uint32_t>(nr::scene::RtTransmissionMode::volume) &&
                           nearlyEqual(layer.p0.y, 2.42f);
                });
            }),
            "CompareIor should import its authored IOR and positive volume thickness into the RT transmission record");
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

        auto const alphaMaskMaterialCount = std::ranges::count_if(imported->materials, [](const nr::load::MaterialAsset& material) {
            return material.alphaModeHint == nr::load::MaterialAlphaModeHint::mask;
        });
        nr::test::requireEqual(static_cast<std::size_t>(alphaMaskMaterialCount), std::size_t{3});
        std::ranges::for_each(imported->materials, [](const nr::load::MaterialAsset& material) {
            if (material.alphaModeHint != nr::load::MaterialAlphaModeHint::mask)
            {
                return;
            }

            nr::test::require(material.alphaCutoff.has_value(), "Sponza alpha-mask material should preserve glTF alphaCutoff");
            nr::test::require(nearlyEqual(*material.alphaCutoff, 0.5f), "Sponza alpha-mask material should keep alphaCutoff=0.5");

            auto runtimeMaterial = convertImportedMaterial(material);
            auto compiled = nr::scene::compileRtMaterial(
                runtimeMaterial,
                makeTextureIds(runtimeMaterial));
            nr::test::require(
                hasFeature(compiled, nr::scene::RtMaterialFeatureFlag::alphaMask),
                "Sponza alpha-mask material should compile to RT alpha-mask feature");
        });

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
            requireSourceDeclaresExtension(asset.first, asset.second);
            auto compiled = compileAssetMaterials(asset.first);
            nr::test::require(
                std::ranges::all_of(compiled, [](const nr::scene::RtCompiledMaterial &material) {
                    return std::ranges::all_of(material.layers, [](const nr::scene::RtMaterialLayerRecord &layer) {
                        return layer.layer <= nr::scene::RtMaterialLayerFlag::transmission;
                    });
                }),
                std::format("{} should not create unsupported RT layer kinds", asset.second));
        });
    }};
} // namespace
