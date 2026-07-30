module nr.scene;
import :scene;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;
import nr.load;
import nr.resource;
import nr.rhi;
import nr.utils;
import std;
import :bridge;
import :utils;
import :type;

namespace nr::scene::detail
{
struct MetallicRoughnessFactorSet
{
    glm::vec4 baseColorFactor{1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
};

[[nodiscard]] float clamp01(float value) noexcept
{
    return glm::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] glm::vec3 clamp01(glm::vec3 value) noexcept
{
    return glm::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] float perceivedBrightness(glm::vec3 color) noexcept
{
    color = clamp01(color);
    return std::sqrt(0.299f * color.r * color.r + 0.587f * color.g * color.g + 0.114f * color.b * color.b);
}

[[nodiscard]] float solveMetallic(float diffuseBrightness, float specularBrightness, float oneMinusSpecularStrength) noexcept
{
    constexpr auto dielectricSpecular = 0.04f;
    auto a = dielectricSpecular;
    auto b = diffuseBrightness * oneMinusSpecularStrength / (1.0f - dielectricSpecular) + specularBrightness - 2.0f * dielectricSpecular;
    auto c = dielectricSpecular - specularBrightness;
    auto discriminant = std::max((b * b) - (4.0f * a * c), 0.0f);
    return clamp01((-b + std::sqrt(discriminant)) / (2.0f * a));
}

[[nodiscard]] MetallicRoughnessFactorSet convertSpecularGlossinessToMetallicRoughness(const nr::load::MaterialAsset &material) noexcept
{
    constexpr auto dielectricSpecular = 0.04f;
    constexpr auto epsilon = 1.0e-6f;

    auto diffuse = clamp01(glm::vec3{
        material.baseColorFactor[0],
        material.baseColorFactor[1],
        material.baseColorFactor[2],
    });
    auto alpha = clamp01(material.baseColorFactor[3] * material.opacity);

    auto specular = glm::vec3{1.0f};
    if (material.specularFactor.has_value())
    {
        specular = clamp01(glm::vec3{
            (*material.specularFactor)[0],
            (*material.specularFactor)[1],
            (*material.specularFactor)[2],
        });
    }

    auto const glossiness = clamp01(material.glossinessFactor.value_or(1.0f));
    auto const specularStrength = std::max({specular.r, specular.g, specular.b});
    auto const oneMinusSpecularStrength = 1.0f - specularStrength;
    auto const metallic = solveMetallic(perceivedBrightness(diffuse), perceivedBrightness(specular), oneMinusSpecularStrength);

    auto const oneMinusMetallic = 1.0f - metallic;
    auto const baseColorFromDiffuse = diffuse * (oneMinusSpecularStrength / ((1.0f - dielectricSpecular) * std::max(oneMinusMetallic, epsilon)));

    auto const baseColorFromSpecular = (specular - glm::vec3{dielectricSpecular * oneMinusMetallic * oneMinusMetallic}) / std::max(1.0f - oneMinusMetallic * oneMinusMetallic, epsilon);

    auto const baseColor = clamp01(glm::mix(baseColorFromDiffuse, baseColorFromSpecular, metallic * metallic));
    return MetallicRoughnessFactorSet{
        .baseColorFactor = glm::vec4{baseColor, alpha},
        .metallicFactor = metallic,
        .roughnessFactor = clamp01(1.0f - glossiness),
    };
}

[[nodiscard]] bool hasSpecularGlossinessFactors(const nr::load::MaterialAsset &material) noexcept
{
    return material.specularFactor.has_value() || material.glossinessFactor.has_value();
}

} // namespace nr::scene::detail

namespace nr::scene
{
void Scene::bridgeTextures(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan, std::vector<nr::resource::TextureHandle> &textureHandlesBySource)
{
    auto const colorHints = detail::buildTextureColorSpaceHints(sceneAsset);

    std::ranges::for_each(plan.textures, [&](const TextureBridgeInput &entry) {
        if (entry.sourceIndex >= sceneAsset.textures.size())
        {
            reportImport<nr::LogLevel::error>(ImportStage::texture, std::format("Texture bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto [handle, created] = textures_.getOrCreate(entry.canonicalKey, [](nr::resource::TextureHandle newHandle, const std::string &key) {
            return TextureAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            textureHandles_.push_back(handle);
        }

        textureHandlesBySource[entry.sourceIndex] = handle;

        auto *record = textures_.tryGet(handle);
        if (record == nullptr)
        {
            reportImport<nr::LogLevel::error>(ImportStage::texture, std::format("Texture storage lookup failed for key '{}'.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        if (!created && record->cpuReady)
        {
            return;
        }

        auto const &sourceTexture = sceneAsset.textures[entry.sourceIndex];
        auto const hint = entry.sourceIndex < colorHints.size() ? colorHints[entry.sourceIndex] : detail::TextureColorSpaceHint{};
        if (hint.hasColor && hint.hasLinear)
        {
            reportImport<nr::LogLevel::warning>(ImportStage::texture, std::format("Texture '{}' is referenced by both color and linear slots; forcing linear sampling.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
        }

        auto texture = nr::resource::Texture{};
        texture.name = sourceTexture.key;
        texture.sourcePath = sourceTexture.resolvedPath;
        texture.srgb = hint.preferSrgb;

        if (sourceTexture.decodedImage.has_value())
        {
            auto prepared = detail::prepareDecodedImageLevel(*sourceTexture.decodedImage);
            if (!prepared.has_value())
            {
                reportImport<nr::LogLevel::error>(ImportStage::texture, std::format("Decoded texture '{}' failed canonical image-level preparation.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
                return;
            }

            texture.width = sourceTexture.decodedImage->width;
            texture.height = sourceTexture.decodedImage->height;
            texture.format = detail::pickTextureFormat(prepared->channelCount, texture.srgb);
            texture.levels.push_back(std::move(prepared->level));
        }
        else if (sourceTexture.rawRgba8.has_value())
        {
            auto const &raw = *sourceTexture.rawRgba8;
            if (raw.width == 0u || raw.height == 0u)
            {
                reportImport<nr::LogLevel::error>(ImportStage::texture, std::format("Embedded raw texture '{}' has invalid dimensions {}x{}.", entry.canonicalKey, raw.width, raw.height), entry.canonicalKey, entry.sourceIndex);
                return;
            }

            texture.width = raw.width;
            texture.height = raw.height;
            texture.format = detail::pickTextureFormat(4u, texture.srgb);
            texture.levels.push_back(detail::prepareRawImageLevel(raw));
        }
        else
        {
            texture.width = 1;
            texture.height = 1;
            texture.format = detail::pickTextureFormat(4u, texture.srgb);
        }

        if (!texture.valid())
        {
            reportImport<nr::LogLevel::error>(ImportStage::texture, std::format("Canonical texture '{}' failed resource::Texture::valid() validation.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        record->cpu = std::move(texture);
        record->cpuReady = true;
        record->uploadQueued = false;
        if (record->cpuVersion == 0u)
        {
            record->cpuVersion = 1u;
        }
    });
}

void Scene::bridgeMaterials(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan, const std::vector<nr::resource::TextureHandle> &textureHandlesBySource, std::vector<nr::resource::MaterialHandle> &materialHandlesBySource)
{
    auto ensureExtensionForSlot = [](nr::resource::Material &material, nr::resource::MaterialTextureSlotSemantic semantic) {
        switch (semantic)
        {
        case nr::resource::MaterialTextureSlotSemantic::clearcoat:
        case nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness:
        case nr::resource::MaterialTextureSlotSemantic::clearcoatNormal:
            if (!material.clearcoat.has_value())
            {
                material.clearcoat.emplace();
            }
            break;
        case nr::resource::MaterialTextureSlotSemantic::sheenColor:
        case nr::resource::MaterialTextureSlotSemantic::sheenRoughness:
            if (!material.sheen.has_value())
            {
                material.sheen.emplace();
            }
            break;
        case nr::resource::MaterialTextureSlotSemantic::transmission:
            if (!material.transmission.has_value())
            {
                material.transmission.emplace();
            }
            break;
        case nr::resource::MaterialTextureSlotSemantic::anisotropy:
            if (!material.anisotropy.has_value())
            {
                material.anisotropy.emplace();
            }
            break;
        default:
            break;
        }
    };

    std::ranges::for_each(plan.materials, [&](const MaterialBridgeInput &entry) {
        if (entry.sourceIndex >= sceneAsset.materials.size())
        {
            reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto [handle, created] = materials_.getOrCreate(entry.canonicalKey, [](nr::resource::MaterialHandle newHandle, const std::string &key) {
            return MaterialAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            materialHandles_.push_back(handle);
        }

        materialHandlesBySource[entry.sourceIndex] = handle;

        auto *record = materials_.tryGet(handle);
        if (record == nullptr)
        {
            reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material storage lookup failed for key '{}'.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        if (!created && record->cpuReady)
        {
            return;
        }

        auto const &sourceMaterial = sceneAsset.materials[entry.sourceIndex];
        auto material = nr::resource::Material{};
        material.name = sourceMaterial.name;

        auto convertedFactors = detail::MetallicRoughnessFactorSet{
            .baseColorFactor =
                glm::vec4{
                    sourceMaterial.baseColorFactor[0],
                    sourceMaterial.baseColorFactor[1],
                    sourceMaterial.baseColorFactor[2],
                    sourceMaterial.baseColorFactor[3] * sourceMaterial.opacity,
                },
            .metallicFactor = sourceMaterial.metallicFactor,
            .roughnessFactor = sourceMaterial.roughnessFactor,
        };

        if (detail::hasSpecularGlossinessFactors(sourceMaterial))
        {
            convertedFactors = detail::convertSpecularGlossinessToMetallicRoughness(sourceMaterial);
            reportImport<nr::LogLevel::warning>(ImportStage::material, std::format("Material '{}' uses specular-glossiness factors; converted approximately to metallic-roughness.", material.name), entry.canonicalKey, entry.sourceIndex);
        }

        material.core.baseColorFactor = convertedFactors.baseColorFactor;
        material.core.emissiveFactor = glm::vec3{
            sourceMaterial.emissiveFactor[0],
            sourceMaterial.emissiveFactor[1],
            sourceMaterial.emissiveFactor[2],
        };
        material.core.metallicFactor = convertedFactors.metallicFactor;
        material.core.roughnessFactor = convertedFactors.roughnessFactor;
        material.core.doubleSided = sourceMaterial.doubleSided;
        material.unlit = sourceMaterial.unlit;
        assignIfPresent(sourceMaterial.normalScale, [&](float normalScale) { material.core.normalScale = normalScale; });
        assignIfPresent(sourceMaterial.occlusionStrength, [&](float occlusionStrength) { material.core.occlusionStrength = occlusionStrength; });
        assignIfPresent(sourceMaterial.alphaCutoff, [&](float alphaCutoff) { material.core.alphaCutoff = alphaCutoff; });

        material.core.alphaMode = resolveMaterialAlphaMode(sourceMaterial);

        assignIfPresent(sourceMaterial.clearcoatFactor, [&](float clearcoatFactor) {
            if (!material.clearcoat.has_value())
            {
                material.clearcoat.emplace();
            }
            material.clearcoat->factor = clearcoatFactor;
        });
        assignIfPresent(sourceMaterial.clearcoatRoughnessFactor, [&](float clearcoatRoughnessFactor) {
            if (!material.clearcoat.has_value())
            {
                material.clearcoat.emplace();
            }
            material.clearcoat->roughnessFactor = clearcoatRoughnessFactor;
        });
        assignIfPresent(sourceMaterial.sheenColorFactor, [&](const std::array<float, 3> &sheenColorFactor) {
            if (!material.sheen.has_value())
            {
                material.sheen.emplace();
            }
            material.sheen->colorFactor = glm::vec3{sheenColorFactor[0], sheenColorFactor[1], sheenColorFactor[2]};
        });
        assignIfPresent(sourceMaterial.sheenRoughnessFactor, [&](float sheenRoughnessFactor) {
            if (!material.sheen.has_value())
            {
                material.sheen.emplace();
            }
            material.sheen->roughnessFactor = sheenRoughnessFactor;
        });
        assignIfPresent(sourceMaterial.transmissionFactor, [&](float transmissionFactor) {
            if (!material.transmission.has_value())
            {
                material.transmission.emplace();
            }
            material.transmission->factor = transmissionFactor;
        });
        assignIfPresent(sourceMaterial.ior, [&](float ior) {
            if (!material.ior.has_value())
            {
                material.ior.emplace();
            }
            material.ior->ior = ior;
        });
        assignIfPresent(sourceMaterial.thicknessFactor, [&](float thicknessFactor) {
            if (!material.volumeBoundary.has_value())
            {
                material.volumeBoundary.emplace();
            }
            material.volumeBoundary->thicknessFactor = thicknessFactor;
        });
        assignIfPresent(sourceMaterial.anisotropyFactor, [&](float anisotropyFactor) {
            if (!material.anisotropy.has_value())
            {
                material.anisotropy.emplace();
            }
            material.anisotropy->factor = anisotropyFactor;
        });
        assignIfPresent(sourceMaterial.anisotropyRotation, [&](float anisotropyRotation) {
            if (!material.anisotropy.has_value())
            {
                material.anisotropy.emplace();
            }
            material.anisotropy->rotation = anisotropyRotation;
        });

        auto materialHasError = false;
        std::ranges::for_each(sourceMaterial.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            auto const sourceSemantic = binding.sourceSemanticName.empty() ? nr::resource::materialTextureSlotSemanticName(binding.semantic) : std::string_view{binding.sourceSemanticName};
            if (binding.textureIndex >= textureHandlesBySource.size())
            {
                reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material '{}' references out-of-range texture index {}.", material.name, binding.textureIndex), entry.canonicalKey, entry.sourceIndex);
                materialHasError = true;
                return;
            }

            auto textureHandle = textureHandlesBySource[binding.textureIndex];
            if (!textureHandle.valid())
            {
                reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material '{}' references unresolved texture index {}.", material.name, binding.textureIndex), entry.canonicalKey, entry.sourceIndex);
                materialHasError = true;
                return;
            }

            if (textures_.tryGet(textureHandle) == nullptr)
            {
                reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material '{}' references unknown texture handle (slot={}, generation={}).", material.name, textureHandle.slot, textureHandle.generation), entry.canonicalKey, entry.sourceIndex);
                materialHasError = true;
                return;
            }

            if (!nr::resource::materialTextureSlotSemanticValid(binding.semantic))
            {
                auto const specGlossTexture = sourceSemantic == "specular" || sourceSemantic == "shininess" || sourceSemantic == "maya_specular" || sourceSemantic == "maya_specular_color" || sourceSemantic == "maya_specular_roughness";
                auto const volumeThicknessTexture = sourceSemantic == "volume_thickness";
                reportImport<nr::LogLevel::warning>(ImportStage::material,
                                                    specGlossTexture ? std::format("Material '{}' ignored specular-glossiness texture semantic '{}' because texture baking to metallic-roughness is not implemented.", material.name, sourceSemantic)
                                                    : volumeThicknessTexture ? std::format("Material '{}' ignored volume thickness texture semantic '{}'; only scalar thicknessFactor boundary classification is supported.", material.name, sourceSemantic)
                                                                     : std::format("Material '{}' ignored unsupported texture semantic '{}'.", material.name, sourceSemantic),
                                                    entry.canonicalKey, entry.sourceIndex);
                return;
            }

            if (binding.uvChannel > 1u)
            {
                reportImport<nr::LogLevel::error>(
                    ImportStage::material,
                    std::format(
                        "Material '{}' texture semantic '{}' selects unsupported UV set {}; only UV sets 0 and 1 are supported.",
                        material.name,
                        sourceSemantic,
                        binding.uvChannel),
                    entry.canonicalKey,
                    entry.sourceIndex);
                materialHasError = true;
                return;
            }

            ensureExtensionForSlot(material, binding.semantic);
            auto *slot = std::addressof(material.slot(binding.semantic));

            if (slot->texture.valid())
            {
                if (slot->texture == textureHandle)
                {
                    return;
                }

                reportImport<nr::LogLevel::warning>(ImportStage::material, std::format("Material '{}' has duplicate semantic '{}' with a different texture; keeping first slot assignment for {}.", material.name, sourceSemantic, nr::resource::materialTextureSlotSemanticName(binding.semantic)),
                                                    entry.canonicalKey, entry.sourceIndex);
                return;
            }

            slot->texture = textureHandle;
            slot->uvSet = binding.uvChannel;
            slot->transform = binding.transform;
        });

        std::ranges::for_each(material.textureSlots, [&](const nr::resource::MaterialTextureSlot &slot) {
            auto textureHandle = slot.texture;
            if (!textureHandle.valid())
            {
                return;
            }

            if (textures_.tryGet(textureHandle) == nullptr)
            {
                materialHasError = true;
                reportImport<nr::LogLevel::error>(ImportStage::material, std::format("Material '{}' resolved to texture handle (slot={}, generation={}) missing from registry.", material.name, textureHandle.slot, textureHandle.generation), entry.canonicalKey, entry.sourceIndex);
            }
        });

        if (materialHasError)
        {
            return;
        }

        record->cpu = std::move(material);
        record->cpuReady = true;
        record->uploadQueued = false;
        if (record->cpuVersion == 0u)
        {
            record->cpuVersion = 1u;
        }
    });
}

void Scene::bridgeMeshes(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan, const std::vector<nr::resource::MaterialHandle> &materialHandlesBySource, std::vector<nr::resource::MeshHandle> &meshHandlesBySource)
{
    auto defaultMaterialHandle = std::optional<nr::resource::MaterialHandle>{};
    auto ensureDefaultMaterial = [&]() -> nr::resource::MaterialHandle {
        if (defaultMaterialHandle.has_value())
        {
            return *defaultMaterialHandle;
        }

        auto defaultKey = sceneAsset.sourcePath.empty() ? std::string{"<scene>::material[default]"} : std::format("{}::material[default]", sceneAsset.sourcePath.generic_string());
        auto [handle, created] = materials_.getOrCreate(defaultKey, [](nr::resource::MaterialHandle newHandle, const std::string &key) {
            return MaterialAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            materialHandles_.push_back(handle);
        }

        auto *record = materials_.tryGet(handle);
        if (record != nullptr && !record->cpuReady)
        {
            auto material = nr::resource::Material{};
            material.name = "default_material";
            record->cpu = std::move(material);
            record->cpuReady = true;
            record->uploadQueued = false;
            if (record->cpuVersion == 0u)
            {
                record->cpuVersion = 1u;
            }
        }

        defaultMaterialHandle = handle;
        return handle;
    };

    std::ranges::for_each(plan.meshes, [&](const MeshBridgeInput &entry) {
        if (entry.sourceIndex >= sceneAsset.meshes.size())
        {
            reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto [handle, created] = meshes_.getOrCreate(entry.canonicalKey, [](nr::resource::MeshHandle newHandle, const std::string &key) {
            return MeshAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            meshHandles_.push_back(handle);
        }

        meshHandlesBySource[entry.sourceIndex] = handle;

        auto *record = meshes_.tryGet(handle);
        if (record == nullptr)
        {
            reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh storage lookup failed for key '{}'.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        if (!created && record->cpuReady)
        {
            return;
        }

        auto const &sourceMesh = sceneAsset.meshes[entry.sourceIndex];
        auto mesh = nr::resource::Mesh{};
        mesh.name = sourceMesh.name;
        mesh.clockwiseFrontFace = sourceMesh.clockwiseFrontFace;

        mesh.vertices.reserve(sourceMesh.vertices.size());
        std::ranges::for_each(sourceMesh.vertices, [&](const nr::load::VertexAsset &sourceVertex) {
            auto vertex = nr::resource::Vertex{};
            vertex.position = glm::vec3{sourceVertex.position[0], sourceVertex.position[1], sourceVertex.position[2]};
            vertex.normal = glm::vec3{sourceVertex.normal[0], sourceVertex.normal[1], sourceVertex.normal[2]};
            vertex.tangent = glm::vec4{sourceVertex.tangent[0], sourceVertex.tangent[1], sourceVertex.tangent[2], sourceVertex.tangent[3]};
            vertex.texCoord0 = glm::vec2{sourceVertex.texCoord0[0], sourceVertex.texCoord0[1]};
            vertex.texCoord1 = glm::vec2{sourceVertex.texCoord1[0], sourceVertex.texCoord1[1]};
            vertex.color0 = glm::vec4{sourceVertex.color0[0], sourceVertex.color0[1], sourceVertex.color0[2], sourceVertex.color0[3]};
            mesh.vertices.push_back(vertex);
        });

        mesh.indices = sourceMesh.indices;

        if (sourceMesh.geometries.empty())
        {
            reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh '{}' has no geometry records.", sourceMesh.name), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto meshHasError = false;
        auto resolveGeometryMaterial = [&](const nr::load::MeshGeometryAsset &sourceGeometry) {
            if (sourceGeometry.materialIndex == nr::load::invalidIndex)
            {
                auto handle = ensureDefaultMaterial();
                reportImport<nr::LogLevel::warning>(ImportStage::mesh, std::format("Mesh '{}' geometry '{}' has no source material; using default material.", sourceMesh.name, sourceGeometry.name), entry.canonicalKey, entry.sourceIndex);
                return handle;
            }

            if (sourceGeometry.materialIndex >= materialHandlesBySource.size())
            {
                reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh '{}' geometry '{}' references out-of-range material index {}.", sourceMesh.name, sourceGeometry.name, sourceGeometry.materialIndex), entry.canonicalKey, entry.sourceIndex);
                meshHasError = true;
                return nr::resource::MaterialHandle{};
            }

            auto materialHandle = materialHandlesBySource[sourceGeometry.materialIndex];
            if (!materialHandle.valid())
            {
                reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh '{}' geometry '{}' references unresolved material index {}.", sourceMesh.name, sourceGeometry.name, sourceGeometry.materialIndex), entry.canonicalKey, entry.sourceIndex);
                meshHasError = true;
                return nr::resource::MaterialHandle{};
            }

            if (materials_.tryGet(materialHandle) == nullptr)
            {
                reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Mesh '{}' geometry '{}' resolved material handle (slot={}, generation={}) missing in registry.", sourceMesh.name, sourceGeometry.name, materialHandle.slot, materialHandle.generation), entry.canonicalKey,
                                                  entry.sourceIndex);
                meshHasError = true;
                return nr::resource::MaterialHandle{};
            }

            return materialHandle;
        };

        auto buildGeometryBounds = [&](const nr::resource::MeshGeometry &geometry) {
            auto bounds = nr::resource::Aabb{};
            if (!mesh.indexed())
            {
                auto begin = static_cast<std::size_t>(geometry.firstIndex);
                auto end = begin + static_cast<std::size_t>(geometry.indexCount);
                if (end <= mesh.vertices.size())
                {
                    auto vertexRange = std::ranges::subrange(mesh.vertices.begin() + static_cast<std::ptrdiff_t>(begin), mesh.vertices.begin() + static_cast<std::ptrdiff_t>(end));
                    std::ranges::for_each(vertexRange, [&](const nr::resource::Vertex &vertex) { bounds.expand(vertex.position); });
                }
                return bounds;
            }

            auto begin = static_cast<std::size_t>(geometry.firstIndex);
            auto end = begin + static_cast<std::size_t>(geometry.indexCount);
            if (end <= mesh.indices.size())
            {
                auto indexRange = std::ranges::subrange(mesh.indices.begin() + static_cast<std::ptrdiff_t>(begin), mesh.indices.begin() + static_cast<std::ptrdiff_t>(end));
                std::ranges::for_each(indexRange, [&](std::uint32_t localIndex) {
                    auto vertexIndex = static_cast<std::uint64_t>(localIndex) + static_cast<std::uint64_t>(geometry.vertexOffset);
                    if (vertexIndex < mesh.vertices.size())
                    {
                        bounds.expand(mesh.vertices[static_cast<std::size_t>(vertexIndex)].position);
                    }
                });
            }
            return bounds;
        };

        mesh.geometries.reserve(sourceMesh.geometries.size());
        auto geometryIndices = std::views::iota(std::size_t{0}, sourceMesh.geometries.size());
        std::ranges::for_each(geometryIndices, [&](std::size_t geometryIndex) {
            auto const &sourceGeometry = sourceMesh.geometries[geometryIndex];
            auto materialHandle = resolveGeometryMaterial(sourceGeometry);
            if (!materialHandle.valid())
            {
                return;
            }

            auto geometry = nr::resource::MeshGeometry{};
            geometry.name = sourceGeometry.name.empty() ? std::format("{}_geometry_{}", sourceMesh.name.empty() ? "mesh" : sourceMesh.name, geometryIndex) : sourceGeometry.name;
            geometry.firstIndex = sourceGeometry.firstIndex;
            geometry.indexCount = sourceGeometry.indexCount;
            geometry.vertexOffset = sourceGeometry.vertexOffset;
            geometry.material = materialHandle;
            mesh.geometries.push_back(std::move(geometry));
        });

        if (meshHasError)
        {
            return;
        }

        mesh.rebuildLocalBounds();
        mesh.rebuildLocalSphere();
        std::ranges::for_each(mesh.geometries, [&](nr::resource::MeshGeometry &geometry) { geometry.localBounds = buildGeometryBounds(geometry); });

        if (!mesh.validate())
        {
            reportImport<nr::LogLevel::error>(ImportStage::mesh, std::format("Canonical mesh '{}' failed validate() after normalization.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        record->cpu = std::move(mesh);
        record->cpuReady = true;
        record->uploadQueued = false;
        if (record->cpuVersion == 0u)
        {
            record->cpuVersion = 1u;
        }
    });
}

void Scene::bridgeCameras(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan, std::vector<nr::resource::CameraAssetHandle> &cameraHandlesBySource)
{
    constexpr auto kEpsilon = 1e-4f;
    constexpr auto kFallbackFov = glm::radians(60.0f);

    std::ranges::for_each(plan.cameras, [&](const CameraBridgeInput &entry) {
        if (entry.sourceIndex >= sceneAsset.cameras.size())
        {
            reportImport<nr::LogLevel::error>(ImportStage::camera, std::format("Camera bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto [handle, created] = cameras_.getOrCreate(entry.canonicalKey, [](nr::resource::CameraAssetHandle newHandle, const std::string &key) {
            return CameraAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            cameraHandles_.push_back(handle);
        }

        cameraHandlesBySource[entry.sourceIndex] = handle;

        auto *record = cameras_.tryGet(handle);
        if (record == nullptr)
        {
            reportImport<nr::LogLevel::error>(ImportStage::camera, std::format("Camera storage lookup failed for key '{}'.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        if (!created && record->cpuReady)
        {
            return;
        }

        auto const &sourceCamera = sceneAsset.cameras[entry.sourceIndex];

        auto camera = nr::resource::CameraAsset{};
        camera.name = sourceCamera.name.empty() ? std::format("camera_{}", entry.sourceIndex) : sourceCamera.name;

        if (sourceCamera.orthographicWidth > kEpsilon)
        {
            camera.projection = nr::resource::CameraProjection::orthographic;

            auto aspect = sourceCamera.aspect;
            if (std::isfinite(aspect) && aspect > kEpsilon)
            {
                camera.authoredAspectRatio = aspect;
            }
            else if (aspect == 0.0f)
            {
                aspect = 1.0f;
                reportImport<nr::LogLevel::info>(ImportStage::camera, std::format("Camera '{}' does not contain an authored aspect; using viewport aspect.", camera.name), entry.canonicalKey, entry.sourceIndex);
            }
            else
            {
                aspect = 1.0f;
                reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' has invalid aspect {} on orthographic path; falling back to aspect=1.", camera.name, sourceCamera.aspect), entry.canonicalKey, entry.sourceIndex);
            }

            camera.orthoHeight = sourceCamera.orthographicWidth / aspect;
        }
        else
        {
            camera.projection = nr::resource::CameraProjection::perspective;

            auto horizontalFov = sourceCamera.horizontalFov;
            if (!(std::isfinite(horizontalFov) && horizontalFov > kEpsilon))
            {
                horizontalFov = kFallbackFov;
                reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' has invalid horizontalFov {} on perspective path; falling back to 60 degrees.", camera.name, sourceCamera.horizontalFov), entry.canonicalKey, entry.sourceIndex);
            }

            auto aspect = sourceCamera.aspect;
            if (std::isfinite(aspect) && aspect > kEpsilon)
            {
                camera.authoredAspectRatio = aspect;
                camera.verticalFovRadians = 2.0f * std::atan(std::tan(horizontalFov * 0.5f) / aspect);
            }
            else if (aspect == 0.0f)
            {
                camera.verticalFovRadians = horizontalFov;
                reportImport<nr::LogLevel::info>(ImportStage::camera, std::format("Camera '{}' does not contain an authored aspect; using viewport aspect.", camera.name), entry.canonicalKey, entry.sourceIndex);
            }
            else
            {
                camera.verticalFovRadians = horizontalFov;
                reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' has invalid aspect {} on perspective path; using horizontalFov as verticalFov.", camera.name, sourceCamera.aspect), entry.canonicalKey, entry.sourceIndex);
            }
        }

        auto nearPlane = sourceCamera.nearPlane;
        if (!(std::isfinite(nearPlane) && nearPlane > kEpsilon))
        {
            nearPlane = 0.1f;
            reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' has invalid near plane {}; falling back to 0.1.", camera.name, sourceCamera.nearPlane), entry.canonicalKey, entry.sourceIndex);
        }

        auto farPlane = sourceCamera.farPlane;
        if (!(std::isfinite(farPlane) && farPlane > nearPlane + kEpsilon))
        {
            farPlane = nearPlane + 1000.0f;
            reportImport<nr::LogLevel::warning>(ImportStage::camera, std::format("Camera '{}' has invalid far plane {}; falling back to near+1000.", camera.name, sourceCamera.farPlane), entry.canonicalKey, entry.sourceIndex);
        }

        camera.nearPlane = nearPlane;
        camera.farPlane = farPlane;

        record->cpu = std::move(camera);
        record->cpuReady = true;
        record->uploadQueued = false;
        if (record->cpuVersion == 0u)
        {
            record->cpuVersion = 1u;
        }
    });
}

void Scene::bridgeLights(const nr::load::SceneAsset &sceneAsset, const SceneBridgePlan &plan, std::vector<nr::resource::LightAssetHandle> &lightHandlesBySource)
{
    constexpr auto kEpsilon = 1e-4f;

    std::ranges::for_each(plan.lights, [&](const LightBridgeInput &entry) {
        if (entry.sourceIndex >= sceneAsset.lights.size())
        {
            reportImport<nr::LogLevel::error>(ImportStage::light, std::format("Light bridge entry '{}' references out-of-range source index {}.", entry.canonicalKey, entry.sourceIndex), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto const &sourceLight = sceneAsset.lights[entry.sourceIndex];
        auto mappedType = detail::mapLightType(sourceLight.type);
        if (!mappedType.has_value())
        {
            reportImport<nr::LogLevel::warning>(ImportStage::light, std::format("Light '{}' uses unsupported type '{}' (raw={}) and will be skipped.", sourceLight.name, sourceLight.type, sourceLight.typeRaw), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        auto [handle, created] = lights_.getOrCreate(entry.canonicalKey, [](nr::resource::LightAssetHandle newHandle, const std::string &key) {
            return LightAssetRecord{
                .handle = newHandle,
                .stableKey = key,
            };
        });

        if (created)
        {
            lightHandles_.push_back(handle);
        }

        lightHandlesBySource[entry.sourceIndex] = handle;

        auto *record = lights_.tryGet(handle);
        if (record == nullptr)
        {
            reportImport<nr::LogLevel::error>(ImportStage::light, std::format("Light storage lookup failed for key '{}'.", entry.canonicalKey), entry.canonicalKey, entry.sourceIndex);
            return;
        }

        if (!created && record->cpuReady)
        {
            return;
        }

        auto light = nr::resource::LightAsset{};
        light.name = sourceLight.name.empty() ? std::format("light_{}", entry.sourceIndex) : sourceLight.name;
        light.type = *mappedType;

        auto diffuseColor = detail::toVec3(sourceLight.colorDiffuse);
        auto specularColor = detail::toVec3(sourceLight.colorSpecular);
        auto ambientColor = detail::toVec3(sourceLight.colorAmbient);

        auto color = diffuseColor;
        if (glm::dot(color, color) <= kEpsilon)
        {
            if (glm::dot(specularColor, specularColor) > kEpsilon)
            {
                color = specularColor;
            }
            else if (glm::dot(ambientColor, ambientColor) > kEpsilon)
            {
                color = ambientColor;
            }
            else
            {
                color = glm::vec3{1.0f};
            }
        }

        auto intensity = std::max({color.r, color.g, color.b});
        if (!nr::resource::math::finiteVec(color) || !std::isfinite(intensity) || intensity <= kEpsilon)
        {
            light.color = glm::vec3{1.0f};
            light.intensity = 1.0f;
        }
        else
        {
            light.color = color / intensity;
            light.intensity = intensity;
        }

        if (*mappedType != nr::resource::LightType::directional && std::isfinite(sourceLight.range) && sourceLight.range > 0.0f)
        {
            light.range = sourceLight.range;
        }

        auto innerCone = sourceLight.innerCone;
        if (!(std::isfinite(innerCone) && innerCone >= 0.0f))
        {
            innerCone = 0.0f;
        }

        auto outerCone = sourceLight.outerCone;
        if (!(std::isfinite(outerCone) && outerCone > 0.0f))
        {
            outerCone = glm::radians(45.0f);
        }

        if (outerCone < innerCone)
        {
            outerCone = innerCone;
            reportImport<nr::LogLevel::warning>(ImportStage::light, std::format("Light '{}' has outer cone smaller than inner cone; clamping outer to inner.", light.name), entry.canonicalKey, entry.sourceIndex);
        }

        light.innerConeRadians = innerCone;
        light.outerConeRadians = outerCone;

        record->cpu = std::move(light);
        record->cpuReady = true;
        record->uploadQueued = false;
        if (record->cpuVersion == 0u)
        {
            record->cpuVersion = 1u;
        }
    });
}

[[nodiscard]] TemplateResourcePinSet Scene::buildTemplatePinSet(std::span<const nr::resource::MeshHandle> meshHandles, std::span<const nr::resource::MaterialHandle> materialHandles, std::span<const nr::resource::TextureHandle> textureHandles,
                                                                std::span<const nr::resource::CameraAssetHandle> cameraHandles, std::span<const nr::resource::LightAssetHandle> lightHandles) const
{
    auto pinSet = TemplateResourcePinSet{};
    auto appendHandles = [&](const auto &handles, auto &collection) { appendValidUniqueHandles(handles, collection); };

    appendHandles(meshHandles, pinSet.meshes);
    appendHandles(materialHandles, pinSet.materials);

    auto geometryMaterialSeen = std::set<std::uint64_t>{};
    std::ranges::for_each(pinSet.materials, [&](nr::resource::MaterialHandle materialHandle) {
        if (materialHandle.valid())
        {
            geometryMaterialSeen.emplace(materialHandle.packed());
        }
    });
    std::ranges::for_each(pinSet.meshes, [&](nr::resource::MeshHandle meshHandle) {
        auto const *meshRecord = meshes_.tryGet(meshHandle);
        if (meshRecord == nullptr || !meshRecord->cpuReady)
        {
            return;
        }

        std::ranges::for_each(meshRecord->cpu.geometries, [&](const nr::resource::MeshGeometry &geometry) {
            if (geometry.material.valid())
            {
                detail::appendUniqueHandle(pinSet.materials, geometryMaterialSeen, geometry.material);
            }
        });
    });

    appendHandles(textureHandles, pinSet.textures);
    appendHandles(cameraHandles, pinSet.cameras);
    appendHandles(lightHandles, pinSet.lights);

    return pinSet;
}

void Scene::retainTemplatePins(const TemplateResourcePinSet &pinSet)
{
    auto incrementPins = [&](const auto &collection, auto &storage) { incrementTemplatePins(std::span{collection}, storage); };

    incrementPins(pinSet.meshes, meshes_);
    incrementPins(pinSet.materials, materials_);
    incrementPins(pinSet.textures, textures_);
    incrementPins(pinSet.cameras, cameras_);
    incrementPins(pinSet.lights, lights_);
}

void Scene::releaseTemplatePins(const TemplateResourcePinSet &pinSet)
{
    auto decrementPins = [&](const auto &collection, auto &storage) { decrementTemplatePins(std::span{collection}, storage); };

    decrementPins(pinSet.meshes, meshes_);
    decrementPins(pinSet.materials, materials_);
    decrementPins(pinSet.textures, textures_);
    decrementPins(pinSet.cameras, cameras_);
    decrementPins(pinSet.lights, lights_);
}


} // namespace nr::scene
