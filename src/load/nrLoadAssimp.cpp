module nr.load;
import dependency.assets;
import dependency.math;

import :assimp;
import :type;
import :backend;
import :decode;
import nr.resource;
import nr.utils;
import std;

namespace nr::load::detail
{
constexpr unsigned int assimpSceneFlagIncomplete = 0x1u;

struct MaterialPropertyKey
{
    const char *name{};
    unsigned int type{};
    unsigned int index{};
};

constexpr MaterialPropertyKey kMatKeyBaseColor{"$clr.base", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyColorEmissive{"$clr.emissive", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyMetallicFactor{"$mat.metallicFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyRoughnessFactor{"$mat.roughnessFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyAnisotropyFactor{"$mat.anisotropyFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeySpecularFactor{"$mat.specularFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyGlossinessFactor{"$mat.glossinessFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyOpacity{"$mat.opacity", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyGltfAlphaMode{"$mat.gltf.alphaMode", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyGltfAlphaCutoff{"$mat.gltf.alphaCutoff", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyTwoSided{"$mat.twosided", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyBumpScaling{"$mat.bumpscaling", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyEmissiveIntensity{"$mat.emissiveIntensity", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyBlendFunc{"$mat.blend", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyClearcoatFactor{"$mat.clearcoat.factor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyClearcoatRoughnessFactor{"$mat.clearcoat.roughnessFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeySheenColorFactor{"$clr.sheen.factor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeySheenRoughnessFactor{"$mat.sheen.roughnessFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyTransmissionFactor{"$mat.transmission.factor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyIor{"$mat.refracti", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyVolumeThicknessFactor{"$mat.volume.thicknessFactor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyVolumeAttenuationDistance{"$mat.volume.attenuationDistance", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyVolumeAttenuationColor{"$mat.volume.attenuationColor", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyAnisotropyRotation{"$mat.anisotropyRotation", 0u, 0u};
constexpr MaterialPropertyKey kMatKeyShadingModel{"$mat.shadingm", 0u, 0u};
// Stable property key used by AI_MATKEY_UVTRANSFORM(textureType, textureSlot).
inline constexpr const char *kMatKeyUvTransform = "$tex.uvtrafo";
inline constexpr const char *kAssimpGltfLightRangeMetadataKey = "PBR_LightRange";

struct AssimpTextureTransform
{
    float translationU = 0.0f;
    float translationV = 0.0f;
    float scaleU = 1.0f;
    float scaleV = 1.0f;
    float rotation = 0.0f;
};

[[nodiscard]] bool readMaterialColor4(const aiMaterial &material, const MaterialPropertyKey &key, aiColor4D &value)
{
    return aiGetMaterialColor(&material, key.name, key.type, key.index, &value) == aiReturn_SUCCESS;
}

[[nodiscard]] bool readMaterialColor3(const aiMaterial &material, const MaterialPropertyKey &key, aiColor3D &value)
{
    auto color = aiColor4D{};
    if (aiGetMaterialColor(&material, key.name, key.type, key.index, &color) != aiReturn_SUCCESS)
    {
        return false;
    }

    value = aiColor3D{color.r, color.g, color.b};
    return true;
}

[[nodiscard]] bool readMaterialFloat(const aiMaterial &material, const MaterialPropertyKey &key, float &value)
{
    auto raw = ai_real{};
    auto count = 1u;
    if (aiGetMaterialFloatArray(&material, key.name, key.type, key.index, &raw, &count) != aiReturn_SUCCESS ||
        count < 1u)
    {
        return false;
    }

    value = static_cast<float>(raw);
    return true;
}

[[nodiscard]] bool readMaterialInteger(const aiMaterial &material, const MaterialPropertyKey &key, int &value)
{
    auto count = 1u;
    if (aiGetMaterialIntegerArray(&material, key.name, key.type, key.index, &value, &count) != aiReturn_SUCCESS ||
        count < 1u)
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool readMaterialString(const aiMaterial &material, const MaterialPropertyKey &key, std::string &value)
{
    auto raw = aiString{};
    if (aiGetMaterialString(&material, key.name, key.type, key.index, &raw) != aiReturn_SUCCESS)
    {
        return false;
    }

    auto const *text = raw.C_Str();
    value = text == nullptr ? std::string{} : std::string{text};
    return !value.empty();
}

[[nodiscard]] bool readMaterialTextureTransform(const aiMaterial &material, aiTextureType textureType,
                                                unsigned int textureSlot, AssimpTextureTransform &value)
{
    auto raw = std::array<ai_real, 5>{};
    auto count = static_cast<unsigned int>(raw.size());
    if (aiGetMaterialFloatArray(&material, kMatKeyUvTransform, static_cast<unsigned int>(textureType), textureSlot,
                                raw.data(), &count) != aiReturn_SUCCESS ||
        count != static_cast<unsigned int>(raw.size()))
    {
        return false;
    }

    value = AssimpTextureTransform{
        .translationU = static_cast<float>(raw[0]),
        .translationV = static_cast<float>(raw[1]),
        .scaleU = static_cast<float>(raw[2]),
        .scaleV = static_cast<float>(raw[3]),
        .rotation = static_cast<float>(raw[4]),
    };
    return true;
}

[[nodiscard]] nr::resource::MaterialTextureTransform makeTextureTransform(const AssimpTextureTransform &source,
                                                                          bool restoreGltfTextureCoordinateV) noexcept
{
    if (restoreGltfTextureCoordinateV)
    {
        // Assimp stores glTF transforms in its V-flipped, center-rotation convention. The mesh path
        // restores the original glTF image-space UVs, so invert that conversion and retain the
        // canonical KHR_texture_transform affine: offset + rotation * scale * uv.
        auto const rotation = -source.rotation;
        auto const rotationCos = std::cos(rotation);
        auto const rotationSin = std::sin(rotation);
        auto const offsetU = source.translationU - 0.5f * source.scaleU * (-rotationCos + rotationSin + 1.0f);
        auto const offsetV =
            0.5f * source.scaleV * (rotationSin + rotationCos - 1.0f) + 1.0f - source.scaleV - source.translationV;

        return nr::resource::MaterialTextureTransform{
            .linear =
                glm::vec4{
                    rotationCos * source.scaleU,
                    -rotationSin * source.scaleV,
                    rotationSin * source.scaleU,
                    rotationCos * source.scaleV,
                },
            .offset = glm::vec2{offsetU, offsetV},
        };
    }

    auto const rotationCos = std::cos(source.rotation);
    auto const rotationSin = std::sin(source.rotation);
    auto const m00 = source.scaleU * rotationCos;
    auto const m01 = -source.scaleU * rotationSin;
    auto const m10 = source.scaleV * rotationSin;
    auto const m11 = source.scaleV * rotationCos;
    return nr::resource::MaterialTextureTransform{
        .linear = glm::vec4{m00, m01, m10, m11},
        .offset =
            glm::vec2{
                0.5f + m00 * (source.translationU - 0.5f) + m01 * (source.translationV - 0.5f),
                0.5f + m10 * (source.translationU - 0.5f) + m11 * (source.translationV - 0.5f),
            },
    };
}

[[nodiscard]] bool textureTransformFinite(const nr::resource::MaterialTextureTransform &transform) noexcept
{
    return std::isfinite(transform.linear.x) && std::isfinite(transform.linear.y) &&
           std::isfinite(transform.linear.z) && std::isfinite(transform.linear.w) &&
           std::isfinite(transform.offset.x) && std::isfinite(transform.offset.y);
}

[[nodiscard]] std::string toStdString(const aiString &value)
{
    auto const *raw = value.C_Str();
    return raw == nullptr ? std::string{} : std::string{raw};
}

[[nodiscard]] MaterialAlphaModeHint gltfAlphaModeHint(std::string_view value) noexcept
{
    if (value == "MASK" || value == "mask")
    {
        return MaterialAlphaModeHint::mask;
    }

    if (value == "BLEND" || value == "blend")
    {
        return MaterialAlphaModeHint::blend;
    }

    return MaterialAlphaModeHint::opaque;
}

[[nodiscard]] std::optional<float> readMetadataFinitePositiveFloat(const aiMetadata *metadata, const char *key)
{
    if (metadata == nullptr || key == nullptr)
    {
        return std::nullopt;
    }

    auto doubleValue = 0.0;
    if (metadata->Get(key, doubleValue) && std::isfinite(doubleValue) && doubleValue > 0.0)
    {
        return static_cast<float>(doubleValue);
    }

    auto floatValue = 0.0f;
    if (metadata->Get(key, floatValue) && std::isfinite(floatValue) && floatValue > 0.0f)
    {
        return floatValue;
    }

    return std::nullopt;
}

void collectGltfLightRangesByNodeName(const aiNode &node, std::map<std::string, float> &rangesByNodeName)
{
    auto const nodeName = toStdString(node.mName);
    if (!nodeName.empty())
    {
        if (auto range = readMetadataFinitePositiveFloat(node.mMetaData, kAssimpGltfLightRangeMetadataKey);
            range.has_value())
        {
            rangesByNodeName.insert_or_assign(nodeName, *range);
        }
    }

    auto const childSlots = std::views::iota(0u, node.mNumChildren);
    std::ranges::for_each(childSlots, [&](unsigned int childSlot) {
        auto const *child = node.mChildren[childSlot];
        if (child != nullptr)
        {
            collectGltfLightRangesByNodeName(*child, rangesByNodeName);
        }
    });
}

[[nodiscard]] std::map<std::string, float> buildGltfLightRangesByNodeName(const aiScene &assimpScene)
{
    auto rangesByNodeName = std::map<std::string, float>{};
    if (assimpScene.mRootNode != nullptr)
    {
        collectGltfLightRangesByNodeName(*assimpScene.mRootNode, rangesByNodeName);
    }
    return rangesByNodeName;
}

[[nodiscard]] float dot(const aiVector3D &lhs, const aiVector3D &rhs) noexcept
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] aiVector3D cross(const aiVector3D &lhs, const aiVector3D &rhs) noexcept
{
    return aiVector3D{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] float squaredLength(const aiVector3D &value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] float tangentHandedness(float signSource) noexcept
{
    return signSource < 0.0f ? -1.0f : 1.0f;
}

struct TangentUvSelection
{
    std::uint32_t uvChannel = 0u;
    nr::resource::MaterialTextureTransform transform{};
    nr::resource::MaterialTextureSlotSemantic semantic = nr::resource::MaterialTextureSlotSemantic::unsupported;
    bool hasNormalTexture = false;
};

[[nodiscard]] const MaterialTextureBinding *findTextureBinding(
    const MaterialAsset &material, nr::resource::MaterialTextureSlotSemantic semantic) noexcept
{
    auto binding = std::ranges::find(material.textures, semantic, &MaterialTextureBinding::semantic);
    return binding == material.textures.end() ? nullptr : std::addressof(*binding);
}

[[nodiscard]] bool tangentMappingsEquivalent(const MaterialTextureBinding &lhs, const MaterialTextureBinding &rhs,
                                             float tolerance = 1e-6f) noexcept
{
    auto const near = [tolerance](float a, float b) noexcept { return std::abs(a - b) <= tolerance; };
    return lhs.uvChannel == rhs.uvChannel && near(lhs.transform.linear.x, rhs.transform.linear.x) &&
           near(lhs.transform.linear.y, rhs.transform.linear.y) &&
           near(lhs.transform.linear.z, rhs.transform.linear.z) && near(lhs.transform.linear.w, rhs.transform.linear.w);
}

[[nodiscard]] TangentUvSelection selectTangentUv(const SceneAsset &scene, std::uint32_t materialIndex,
                                                 std::string_view meshName)
{
    if (materialIndex >= scene.materials.size())
    {
        return {};
    }

    auto const &material = scene.materials[materialIndex];
    auto const *baseNormal = findTextureBinding(material, nr::resource::MaterialTextureSlotSemantic::normal);
    auto const *clearcoatNormal =
        findTextureBinding(material, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal);

    if (baseNormal != nullptr && clearcoatNormal != nullptr &&
        !tangentMappingsEquivalent(*baseNormal, *clearcoatNormal))
    {
        nr::nrLog(
            nr::LogLevel::warning, "LOAD",
            std::format(
                "Mesh '{}' material '{}' uses different effective UV mappings for base and clearcoat normal textures. "
                "A vertex stores one tangent frame; generated tangents use the base normal mapping, so the clearcoat "
                "normal tangent orientation cannot also be exact.",
                meshName, material.name));
    }

    auto const *selected = baseNormal != nullptr ? baseNormal : clearcoatNormal;
    if (selected == nullptr)
    {
        return {};
    }

    return TangentUvSelection{
        .uvChannel = selected->uvChannel,
        .transform = selected->transform,
        .semantic = selected->semantic,
        .hasNormalTexture = true,
    };
}

[[nodiscard]] std::array<float, 2> tangentTexCoord(const VertexAsset &vertex,
                                                   const TangentUvSelection &selection) noexcept
{
    auto const &source = selection.uvChannel == 1u ? vertex.texCoord1 : vertex.texCoord0;
    return {
        selection.transform.linear.x * source[0] + selection.transform.linear.y * source[1] +
            selection.transform.offset.x,
        selection.transform.linear.z * source[0] + selection.transform.linear.w * source[1] +
            selection.transform.offset.y,
    };
}

[[nodiscard]] bool tangentDirectionFinite(const nr::dependency::mikktspace::Tangent &tangent) noexcept
{
    return std::ranges::all_of(tangent.direction, [](float component) { return std::isfinite(component); }) &&
           std::isfinite(tangent.sign);
}

[[nodiscard]] std::optional<std::array<float, 4>> normalizedTangent(
    const nr::dependency::mikktspace::Tangent &tangent) noexcept
{
    if (!tangentDirectionFinite(tangent))
    {
        return std::nullopt;
    }

    auto const lengthSquared = tangent.direction[0] * tangent.direction[0] +
                               tangent.direction[1] * tangent.direction[1] +
                               tangent.direction[2] * tangent.direction[2];
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1e-12f)
    {
        return std::nullopt;
    }

    auto const inverseLength = 1.0f / std::sqrt(lengthSquared);
    return std::array<float, 4>{
        tangent.direction[0] * inverseLength,
        tangent.direction[1] * inverseLength,
        tangent.direction[2] * inverseLength,
        tangentHandedness(tangent.sign),
    };
}

[[nodiscard]] bool tangentsCompatible(const std::array<float, 4> &lhs, const std::array<float, 4> &rhs) noexcept
{
    constexpr float directionTolerance = 1e-5f;
    auto const directionDot = lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
    return lhs[3] == rhs[3] && directionDot >= 1.0f - directionTolerance;
}

struct VertexTangentVariant
{
    std::array<float, 4> tangent{};
    std::uint32_t vertexIndex = 0u;
};

[[nodiscard]] std::optional<LoadError> generateMikkTangents(MeshAsset &mesh,
                                                            std::span<const std::uint32_t> faceVertexCounts,
                                                            const TangentUvSelection &selection,
                                                            const std::filesystem::path &sourcePath)
{
    auto corners = std::vector<nr::dependency::mikktspace::Corner>{};
    corners.reserve(mesh.indices.size());
    for (auto vertexIndex : mesh.indices)
    {
        if (vertexIndex >= mesh.vertices.size())
        {
            return makeLoadError(
                LoadErrorCode::invalidScene, "assimp", sourcePath,
                std::format(
                    "Mesh '{}' references vertex {} while generating tangent space, but only {} vertices exist.",
                    mesh.name, vertexIndex, mesh.vertices.size()));
        }

        auto const &vertex = mesh.vertices[vertexIndex];
        auto const texCoord = tangentTexCoord(vertex, selection);
        auto const finiteCorner =
            std::ranges::all_of(vertex.position, [](float component) { return std::isfinite(component); }) &&
            std::ranges::all_of(vertex.normal, [](float component) { return std::isfinite(component); }) &&
            std::ranges::all_of(texCoord, [](float component) { return std::isfinite(component); });
        if (!finiteCorner)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", sourcePath,
                                 std::format("Mesh '{}' contains non-finite position, normal, or effective normal-map "
                                             "UV data required by MikkTSpace.",
                                             mesh.name));
        }

        corners.push_back(nr::dependency::mikktspace::Corner{
            .position = vertex.position,
            .normal = vertex.normal,
            .texCoord = texCoord,
        });
    }

    auto cornerTangents = std::vector<nr::dependency::mikktspace::Tangent>(corners.size());
    if (!nr::dependency::mikktspace::generateTangents(faceVertexCounts, corners, cornerTangents))
    {
        return makeLoadError(
            LoadErrorCode::invalidScene, "assimp", sourcePath,
            std::format("MikkTSpace failed to generate tangent space for mesh '{}' from its effective normal-map UVs.",
                        mesh.name));
    }

    auto const originalVertexCount = mesh.vertices.size();
    auto tangentVariants = std::vector<std::vector<VertexTangentVariant>>(originalVertexCount);
    auto cornerIndices = std::views::iota(std::size_t{0}, mesh.indices.size());
    for (auto cornerIndex : cornerIndices)
    {
        auto const originalVertexIndex = mesh.indices[cornerIndex];
        auto tangent = normalizedTangent(cornerTangents[cornerIndex]);
        if (!tangent.has_value())
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", sourcePath,
                                 std::format("MikkTSpace generated an invalid tangent frame for mesh '{}' corner {}.",
                                             mesh.name, cornerIndex));
        }

        auto &variants = tangentVariants[originalVertexIndex];
        auto compatible = std::ranges::find_if(variants, [&](const VertexTangentVariant &variant) {
            return tangentsCompatible(variant.tangent, *tangent);
        });
        if (compatible != variants.end())
        {
            mesh.indices[cornerIndex] = compatible->vertexIndex;
            continue;
        }

        auto targetVertexIndex = originalVertexIndex;
        if (!variants.empty())
        {
            if (mesh.vertices.size() >= std::numeric_limits<std::uint32_t>::max())
            {
                return makeLoadError(LoadErrorCode::invalidScene, "assimp", sourcePath,
                                     std::format("Mesh '{}' exceeds the 32-bit vertex index range while splitting "
                                                 "incompatible MikkTSpace corners.",
                                                 mesh.name));
            }

            targetVertexIndex = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(mesh.vertices[originalVertexIndex]);
        }

        mesh.vertices[targetVertexIndex].tangent = *tangent;
        variants.push_back(VertexTangentVariant{
            .tangent = *tangent,
            .vertexIndex = targetVertexIndex,
        });
        mesh.indices[cornerIndex] = targetVertexIndex;
    }

    return std::nullopt;
}

[[nodiscard]] std::string textureTypeName(aiTextureType textureType, unsigned int textureSlot)
{
    switch (textureType)
    {
    case aiTextureType_NONE:
        return "none";
    case aiTextureType_DIFFUSE:
        return "diffuse";
    case aiTextureType_SPECULAR:
        return "specular";
    case aiTextureType_AMBIENT:
        return "ambient";
    case aiTextureType_EMISSIVE:
        return "emissive";
    case aiTextureType_HEIGHT:
        return "height";
    case aiTextureType_NORMALS:
        return "normals";
    case aiTextureType_SHININESS:
        return "shininess";
    case aiTextureType_OPACITY:
        return "opacity";
    case aiTextureType_DISPLACEMENT:
        return "displacement";
    case aiTextureType_LIGHTMAP:
        return "lightmap";
    case aiTextureType_REFLECTION:
        return "reflection";
    case aiTextureType_BASE_COLOR:
        return "base_color";
    case aiTextureType_NORMAL_CAMERA:
        return "normal_camera";
    case aiTextureType_EMISSION_COLOR:
        return "emission_color";
    case aiTextureType_METALNESS:
        return "metalness";
    case aiTextureType_DIFFUSE_ROUGHNESS:
        return "diffuse_roughness";
    case aiTextureType_AMBIENT_OCCLUSION:
        return "ambient_occlusion";
    case aiTextureType_UNKNOWN:
        return "unknown";
    case aiTextureType_SHEEN:
        switch (textureSlot)
        {
        case 0u:
            return "sheen_color";
        case 1u:
            return "sheen_roughness";
        default:
            return std::format("sheen_{}", textureSlot);
        }
    case aiTextureType_CLEARCOAT:
        switch (textureSlot)
        {
        case 0u:
            return "clearcoat";
        case 1u:
            return "clearcoat_roughness";
        case 2u:
            return "clearcoat_normal";
        default:
            return std::format("clearcoat_{}", textureSlot);
        }
    case aiTextureType_TRANSMISSION:
        switch (textureSlot)
        {
        case 0u:
            return "transmission";
        case 1u:
            return "volume_thickness";
        default:
            return std::format("transmission_{}", textureSlot);
        }
    case aiTextureType_MAYA_BASE:
        return "maya_base";
    case aiTextureType_MAYA_SPECULAR:
        return "maya_specular";
    case aiTextureType_MAYA_SPECULAR_COLOR:
        return "maya_specular_color";
    case aiTextureType_MAYA_SPECULAR_ROUGHNESS:
        return "maya_specular_roughness";
    case aiTextureType_ANISOTROPY:
        return "anisotropy";
    case aiTextureType_GLTF_METALLIC_ROUGHNESS:
        return "gltf_metallic_roughness";
    default:
        return std::format("type_{}", static_cast<unsigned>(textureType));
    }
}

[[nodiscard]] nr::resource::MaterialTextureSlotSemantic textureSlotSemantic(aiTextureType textureType,
                                                                            unsigned int textureSlot) noexcept
{
    using enum nr::resource::MaterialTextureSlotSemantic;

    switch (textureType)
    {
    case aiTextureType_DIFFUSE:
    case aiTextureType_BASE_COLOR:
    case aiTextureType_MAYA_BASE:
        return baseColor;
    case aiTextureType_HEIGHT:
    case aiTextureType_NORMALS:
    case aiTextureType_DISPLACEMENT:
    case aiTextureType_NORMAL_CAMERA:
        return normal;
    case aiTextureType_AMBIENT:
    case aiTextureType_LIGHTMAP:
    case aiTextureType_AMBIENT_OCCLUSION:
        return occlusion;
    case aiTextureType_EMISSIVE:
    case aiTextureType_EMISSION_COLOR:
        return emissive;
    case aiTextureType_METALNESS:
    case aiTextureType_DIFFUSE_ROUGHNESS:
    case aiTextureType_GLTF_METALLIC_ROUGHNESS:
        return metallicRoughness;
    case aiTextureType_CLEARCOAT:
        switch (textureSlot)
        {
        case 0u:
            return clearcoat;
        case 1u:
            return clearcoatRoughness;
        case 2u:
            return clearcoatNormal;
        default:
            return unsupported;
        }
    case aiTextureType_SHEEN:
        switch (textureSlot)
        {
        case 0u:
            return sheenColor;
        case 1u:
            return sheenRoughness;
        default:
            return unsupported;
        }
    case aiTextureType_TRANSMISSION:
        return textureSlot == 0u ? transmission : unsupported;
    case aiTextureType_ANISOTROPY:
        return textureSlot == 0u ? anisotropy : unsupported;
    default:
        return unsupported;
    }
}

[[nodiscard]] std::string lightSourceTypeName(aiLightSourceType lightType)
{
    switch (lightType)
    {
    case aiLightSource_UNDEFINED:
        return "undefined";
    case aiLightSource_DIRECTIONAL:
        return "directional";
    case aiLightSource_POINT:
        return "point";
    case aiLightSource_SPOT:
        return "spot";
    case aiLightSource_AMBIENT:
        return "ambient";
    case aiLightSource_AREA:
        return "area";
    default:
        return std::format("type_{}", static_cast<unsigned>(lightType));
    }
}

[[nodiscard]] std::string normalizeTextureKey(std::string_view key)
{
    std::string normalized{key};
    std::ranges::transform(normalized, normalized.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return normalized;
}

[[nodiscard]] std::string formatHintToString(const aiTexture &texture)
{
    auto rawView = std::string_view{texture.achFormatHint, std::size(texture.achFormatHint)};
    auto terminator = rawView.find('\0');
    if (terminator != std::string_view::npos)
    {
        rawView = rawView.substr(0, terminator);
    }
    return std::string{rawView};
}

[[nodiscard]] std::optional<std::uint32_t> parseEmbeddedTextureIndex(std::string_view reference)
{
    if (reference.size() < 2 || reference.front() != '*')
    {
        return std::nullopt;
    }

    std::uint64_t value = 0;
    auto digits = reference.substr(1);
    auto *first = digits.data();
    auto *last = digits.data() + digits.size();
    auto [parsedEnd, parseError] = std::from_chars(first, last, value);
    if (parseError != std::errc{} || parsedEnd != last || value > std::numeric_limits<std::uint32_t>::max())
    {
        return std::nullopt;
    }

    return static_cast<std::uint32_t>(value);
}

[[nodiscard]] std::array<float, 16> toMatrix(const aiMatrix4x4 &matrix)
{
    return {
        matrix.a1, matrix.a2, matrix.a3, matrix.a4, matrix.b1, matrix.b2, matrix.b3, matrix.b4,
        matrix.c1, matrix.c2, matrix.c3, matrix.c4, matrix.d1, matrix.d2, matrix.d3, matrix.d4,
    };
}

[[nodiscard]] std::array<float, 3> toVector3(const aiVector3D &value)
{
    return {value.x, value.y, value.z};
}

[[nodiscard]] std::array<float, 3> toColor3(const aiColor3D &value)
{
    return {value.r, value.g, value.b};
}

[[nodiscard]] std::filesystem::path resolveTexturePath(const std::filesystem::path &baseDirectory,
                                                       std::string_view sourcePath)
{
    auto path = std::filesystem::path{sourcePath};
    if (path.is_absolute())
    {
        return path;
    }
    return (baseDirectory / path).lexically_normal();
}

[[nodiscard]] std::optional<LoadError> appendEmbeddedTexture(const aiTexture &texture, std::uint32_t textureIndex,
                                                             SceneAsset &scene)
{
    TextureAsset textureAsset{};
    textureAsset.key = std::format("*{}", textureIndex);

    if (texture.mHeight == 0)
    {
        auto byteCount = static_cast<std::size_t>(texture.mWidth);
        auto const *byteSource = reinterpret_cast<const std::byte *>(texture.pcData);
        if (byteCount > 0 && byteSource == nullptr)
        {
            return makeLoadError(
                LoadErrorCode::textureDataUnsupported, "assimp", scene.sourcePath,
                std::format("Embedded compressed texture {} has null data pointer.", textureAsset.key));
        }

        EmbeddedCompressedTexture compressed{};
        compressed.formatHint = formatHintToString(texture);
        compressed.bytes.resize(byteCount);
        if (byteCount > 0)
        {
            std::ranges::copy(byteSource, byteSource + byteCount, compressed.bytes.begin());
        }

        textureAsset.payloadKind = TexturePayloadKind::embeddedCompressedBlob;
        textureAsset.compressed = std::move(compressed);
        scene.textures.push_back(std::move(textureAsset));
        return std::nullopt;
    }

    auto width = static_cast<std::size_t>(texture.mWidth);
    auto height = static_cast<std::size_t>(texture.mHeight);
    if (width == 0 || height == 0 || texture.pcData == nullptr)
    {
        return makeLoadError(
            LoadErrorCode::textureDataUnsupported, "assimp", scene.sourcePath,
            std::format("Embedded raw texture {} has invalid dimensions or null data.", textureAsset.key));
    }

    auto texelCount = width * height;
    if (texelCount > std::numeric_limits<std::size_t>::max() / 4)
    {
        return makeLoadError(
            LoadErrorCode::textureDataUnsupported, "assimp", scene.sourcePath,
            std::format("Embedded raw texture {} size overflows host memory limits.", textureAsset.key));
    }

    EmbeddedRawTexture raw{};
    raw.width = static_cast<std::uint32_t>(width);
    raw.height = static_cast<std::uint32_t>(height);
    raw.rgba8.resize(texelCount * 4);

    for (auto texelIndex : std::views::iota(std::size_t{0}, texelCount))
    {
        auto const &texel = texture.pcData[texelIndex];
        auto const byteBase = texelIndex * 4;

        raw.rgba8[byteBase + 0] = static_cast<std::byte>(texel.r);
        raw.rgba8[byteBase + 1] = static_cast<std::byte>(texel.g);
        raw.rgba8[byteBase + 2] = static_cast<std::byte>(texel.b);
        raw.rgba8[byteBase + 3] = static_cast<std::byte>(texel.a);
    }

    textureAsset.payloadKind = TexturePayloadKind::embeddedRawRgba8;
    textureAsset.rawRgba8 = std::move(raw);
    scene.textures.push_back(std::move(textureAsset));

    return std::nullopt;
}

[[nodiscard]] std::optional<LoadError> appendEmbeddedTextures(const aiScene &assimpScene, SceneAsset &scene)
{
    auto textureIndices = std::views::iota(0u, assimpScene.mNumTextures);
    for (auto textureIndex : textureIndices)
    {
        auto const *texture = assimpScene.mTextures[textureIndex];
        if (texture == nullptr)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Scene texture {} is null.", textureIndex));
        }

        auto error = appendEmbeddedTexture(*texture, textureIndex, scene);
        if (error.has_value())
        {
            return error;
        }
    }

    return std::nullopt;
}

[[nodiscard]] std::uint32_t textureIndexFromKey(const std::map<std::string, std::uint32_t> &indexByKey,
                                                std::string_view key)
{
    auto found = indexByKey.find(std::string{key});
    if (found == indexByKey.end())
    {
        return invalidIndex;
    }
    return found->second;
}

[[nodiscard]] bool shouldFlipAssimpTextureCoordinateV(const std::filesystem::path &sourcePath);

[[nodiscard]] std::optional<LoadError> appendMaterials(const aiScene &assimpScene,
                                                       const std::filesystem::path &baseDirectory, SceneAsset &scene)
{
    auto textureIndexByKey = std::map<std::string, std::uint32_t>{};
    for (auto textureIndex : std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(scene.textures.size())))
    {
        textureIndexByKey.emplace(scene.textures[textureIndex].key, textureIndex);
    }
    auto const restoreGltfTextureCoordinateV = shouldFlipAssimpTextureCoordinateV(scene.sourcePath);

    auto materialIndices = std::views::iota(0u, assimpScene.mNumMaterials);
    for (auto materialIndex : materialIndices)
    {
        auto const *material = assimpScene.mMaterials[materialIndex];
        if (material == nullptr)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Scene material {} is null.", materialIndex));
        }

        MaterialAsset materialAsset{};
        materialAsset.name = toStdString(material->GetName());
        if (materialAsset.name.empty())
        {
            materialAsset.name = std::format("material_{}", materialIndex);
        }

        // Read base color factor
        if (aiColor4D baseColor; readMaterialColor4(*material, kMatKeyBaseColor, baseColor))
        {
            materialAsset.baseColorFactor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
        }

        // Read emissive factor
        if (aiColor3D emissive; readMaterialColor3(*material, kMatKeyColorEmissive, emissive))
        {
            materialAsset.emissiveFactor = {emissive.r, emissive.g, emissive.b};
        }

        // Read metallic factor
        if (float metallic; readMaterialFloat(*material, kMatKeyMetallicFactor, metallic))
        {
            materialAsset.metallicFactor = metallic;
        }

        // Read roughness factor
        if (float roughness; readMaterialFloat(*material, kMatKeyRoughnessFactor, roughness))
        {
            materialAsset.roughnessFactor = roughness;
        }

        // Read anisotropy factor
        if (float anisotropy; readMaterialFloat(*material, kMatKeyAnisotropyFactor, anisotropy))
        {
            materialAsset.anisotropyFactor = anisotropy;
        }

        if (float anisotropyRotation; readMaterialFloat(*material, kMatKeyAnisotropyRotation, anisotropyRotation))
        {
            materialAsset.anisotropyRotation = anisotropyRotation;
        }

        if (float clearcoat; readMaterialFloat(*material, kMatKeyClearcoatFactor, clearcoat))
        {
            materialAsset.clearcoatFactor = clearcoat;
        }

        if (float clearcoatRoughness; readMaterialFloat(*material, kMatKeyClearcoatRoughnessFactor, clearcoatRoughness))
        {
            materialAsset.clearcoatRoughnessFactor = clearcoatRoughness;
        }

        if (aiColor3D sheenColor; readMaterialColor3(*material, kMatKeySheenColorFactor, sheenColor))
        {
            materialAsset.sheenColorFactor = std::array<float, 3>{sheenColor.r, sheenColor.g, sheenColor.b};
        }

        if (float sheenRoughness; readMaterialFloat(*material, kMatKeySheenRoughnessFactor, sheenRoughness))
        {
            materialAsset.sheenRoughnessFactor = sheenRoughness;
        }

        if (float transmission; readMaterialFloat(*material, kMatKeyTransmissionFactor, transmission))
        {
            materialAsset.transmissionFactor = transmission;
        }

        if (float ior; readMaterialFloat(*material, kMatKeyIor, ior))
        {
            materialAsset.ior = ior;
        }

        if (float thicknessFactor; readMaterialFloat(*material, kMatKeyVolumeThicknessFactor, thicknessFactor))
        {
            materialAsset.thicknessFactor = thicknessFactor;
        }

        auto attenuationDistance = 0.0f;
        auto const hasUnsupportedAttenuationDistance =
            readMaterialFloat(*material, kMatKeyVolumeAttenuationDistance, attenuationDistance) &&
            std::isfinite(attenuationDistance);
        auto attenuationColor = aiColor3D{};
        auto const hasUnsupportedAttenuationColor =
            readMaterialColor3(*material, kMatKeyVolumeAttenuationColor, attenuationColor) &&
            (attenuationColor.r != 1.0f || attenuationColor.g != 1.0f || attenuationColor.b != 1.0f);
        if (hasUnsupportedAttenuationDistance || hasUnsupportedAttenuationColor)
        {
            nr::nrLog(nr::LogLevel::warning, "LOAD",
                      std::format("Material '{}' uses volume attenuation/absorption properties; Beer-Lambert "
                                  "absorption is unsupported and will be ignored.",
                                  materialAsset.name));
        }

        // Read specular factor (for specular-glossiness workflow)
        if (aiColor3D specular; readMaterialColor3(*material, kMatKeySpecularFactor, specular))
        {
            materialAsset.specularFactor = std::array<float, 3>{specular.r, specular.g, specular.b};
        }

        // Read glossiness factor
        if (float glossiness; readMaterialFloat(*material, kMatKeyGlossinessFactor, glossiness))
        {
            materialAsset.glossinessFactor = glossiness;
        }

        // Read opacity
        if (float opacity; readMaterialFloat(*material, kMatKeyOpacity, opacity))
        {
            materialAsset.opacity = opacity;
        }

        auto hasExplicitGltfAlphaMode = false;
        if (std::string alphaMode; readMaterialString(*material, kMatKeyGltfAlphaMode, alphaMode))
        {
            materialAsset.alphaModeHint = gltfAlphaModeHint(alphaMode);
            hasExplicitGltfAlphaMode = true;
        }
        if (float alphaCutoff; readMaterialFloat(*material, kMatKeyGltfAlphaCutoff, alphaCutoff))
        {
            materialAsset.alphaCutoff = alphaCutoff;
        }

        // Read two-sided flag
        if (int twoSided; readMaterialInteger(*material, kMatKeyTwoSided, twoSided))
        {
            materialAsset.doubleSided = twoSided != 0;
        }

        // Read normal scale / bump scaling
        if (float normalScale; readMaterialFloat(*material, kMatKeyBumpScaling, normalScale))
        {
            materialAsset.normalScale = normalScale;
        }

        // Read occlusion strength
        if (float aoIntensity; readMaterialFloat(*material, kMatKeyEmissiveIntensity, aoIntensity))
        {
            materialAsset.occlusionStrength = aoIntensity;
        }

        // Classify KHR_materials_unlit: Assimp maps the extension to no-shading mode.
        if (int shadingMode; readMaterialInteger(*material, kMatKeyShadingModel, shadingMode))
        {
            materialAsset.unlit = shadingMode == assimpShadingModeUnlit;
        }

        // Classify legacy alpha mode from blend function when glTF did not provide an explicit mode.
        if (!hasExplicitGltfAlphaMode)
        {
            if (int blendFunc; readMaterialInteger(*material, kMatKeyBlendFunc, blendFunc))
            {
                (void)blendFunc;
                // Check if material has transparency-related properties
                if (materialAsset.opacity < 1.0f)
                {
                    materialAsset.alphaModeHint = MaterialAlphaModeHint::blend;
                }
            }
        }

        // Classify workflow flags based on properties
        auto classifyWorkflow = [&]() {
            MaterialWorkflowFlags flags = MaterialWorkflowFlags::metallicRoughness;

            if (materialAsset.specularFactor.has_value() && materialAsset.glossinessFactor.has_value())
            {
                flags = static_cast<MaterialWorkflowFlags>(
                    static_cast<std::uint8_t>(flags) |
                    static_cast<std::uint8_t>(MaterialWorkflowFlags::specularGlossiness));
            }

            if (materialAsset.anisotropyFactor.has_value() && *materialAsset.anisotropyFactor > 0.0f)
            {
                flags = static_cast<MaterialWorkflowFlags>(
                    static_cast<std::uint8_t>(flags) | static_cast<std::uint8_t>(MaterialWorkflowFlags::anisotropy));
            }

            return flags;
        };
        materialAsset.workflowFlags = classifyWorkflow();

        // Read texture bindings
        auto textureTypeRange = std::views::iota(0u, assimpTextureTypeMax + 1u);
        for (auto textureTypeRaw : textureTypeRange)
        {
            auto textureType = static_cast<aiTextureType>(textureTypeRaw);
            auto textureCount = material->GetTextureCount(textureType);
            auto textureSlots = std::views::iota(0u, textureCount);
            for (auto slotIndex : textureSlots)
            {
                aiString texturePath{};
                unsigned int uvChannel = 0;
                auto textureQuery = material->GetTexture(textureType, slotIndex, &texturePath, nullptr, &uvChannel,
                                                         nullptr, nullptr, nullptr);
                if (textureQuery != aiReturn_SUCCESS)
                {
                    continue;
                }

                if (uvChannel > 1u)
                {
                    return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                         std::format("Material '{}' texture semantic '{}' selects unsupported UV set "
                                                     "{}; only UV sets 0 and 1 are supported.",
                                                     materialAsset.name, textureTypeName(textureType, slotIndex),
                                                     uvChannel));
                }

                auto textureTransform = nr::resource::MaterialTextureTransform{};
                auto assimpTextureTransform = AssimpTextureTransform{};
                if (readMaterialTextureTransform(*material, textureType, slotIndex, assimpTextureTransform))
                {
                    textureTransform = makeTextureTransform(assimpTextureTransform, restoreGltfTextureCoordinateV);
                    if (!textureTransformFinite(textureTransform))
                    {
                        return makeLoadError(
                            LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                            std::format("Material '{}' texture semantic '{}' has a non-finite UV transform.",
                                        materialAsset.name, textureTypeName(textureType, slotIndex)));
                    }
                }

                auto rawTexturePath = toStdString(texturePath);
                if (rawTexturePath.empty())
                {
                    continue;
                }

                std::uint32_t resolvedTextureIndex = invalidIndex;
                auto embeddedIndex = parseEmbeddedTextureIndex(rawTexturePath);
                if (embeddedIndex.has_value())
                {
                    auto embeddedKey = std::format("*{}", *embeddedIndex);
                    resolvedTextureIndex = textureIndexFromKey(textureIndexByKey, embeddedKey);
                    if (resolvedTextureIndex == invalidIndex)
                    {
                        return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                             std::format("Material '{}' references missing embedded texture '{}'.",
                                                         materialAsset.name, embeddedKey));
                    }
                }
                else
                {
                    auto resolvedPath = resolveTexturePath(baseDirectory, rawTexturePath);
                    auto normalizedKey = normalizeTextureKey(resolvedPath.generic_string());
                    resolvedTextureIndex = textureIndexFromKey(textureIndexByKey, normalizedKey);
                    if (resolvedTextureIndex == invalidIndex)
                    {
                        TextureAsset externalTexture{};
                        externalTexture.key = normalizedKey;
                        externalTexture.resolvedPath = std::move(resolvedPath);

                        scene.textures.push_back(std::move(externalTexture));
                        resolvedTextureIndex = static_cast<std::uint32_t>(scene.textures.size() - 1);
                        textureIndexByKey.emplace(normalizedKey, resolvedTextureIndex);
                    }
                }

                materialAsset.textures.push_back(MaterialTextureBinding{
                    .textureIndex = resolvedTextureIndex,
                    .uvChannel = uvChannel,
                    .transform = textureTransform,
                    .textureTypeRaw = textureTypeRaw,
                    .semantic = textureSlotSemantic(textureType, slotIndex),
                    .sourceSemanticName = textureTypeName(textureType, slotIndex),
                });
            }
        }

        scene.materials.push_back(std::move(materialAsset));
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<LoadError> appendMeshes(const aiScene &assimpScene, SceneAsset &scene, bool strict,
                                                    bool generateMissingGltfTangents)
{
    auto meshIndices = std::views::iota(0u, assimpScene.mNumMeshes);
    for (auto meshIndex : meshIndices)
    {
        auto const *mesh = assimpScene.mMeshes[meshIndex];
        if (mesh == nullptr)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Scene mesh {} is null.", meshIndex));
        }

        if ((mesh->mNumVertices == 0 || mesh->mVertices == nullptr) && strict)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Mesh {} has no vertex data.", meshIndex));
        }

        MeshAsset meshAsset{};
        meshAsset.name = toStdString(mesh->mName);
        if (meshAsset.name.empty())
        {
            meshAsset.name = std::format("mesh_{}", meshIndex);
        }
        meshAsset.clockwiseFrontFace = false;
        auto const materialIndex =
            mesh->mMaterialIndex < assimpScene.mNumMaterials ? mesh->mMaterialIndex : invalidIndex;

        meshAsset.vertices.reserve(mesh->mNumVertices);
        auto const flipTextureCoordinateV = shouldFlipAssimpTextureCoordinateV(scene.sourcePath);
        auto const hasSourceTangents = mesh->HasTangentsAndBitangents();
        if (generateMissingGltfTangents && (mesh->mTangents != nullptr || mesh->mBitangents != nullptr) &&
            !hasSourceTangents)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Mesh '{}' contains incomplete authored tangent data; both tangent and "
                                             "bitangent arrays are required.",
                                             meshAsset.name));
        }

        auto invalidTangentFrameCount = 0u;
        auto firstInvalidTangentFrame = std::optional<std::string>{};
        auto vertexIndices = std::views::iota(0u, mesh->mNumVertices);
        for (auto vertexIndex : vertexIndices)
        {
            VertexAsset vertex{};

            auto const &position = mesh->mVertices[vertexIndex];
            vertex.position = {position.x, position.y, position.z};

            if (mesh->HasNormals() && mesh->mNormals != nullptr)
            {
                auto const &normal = mesh->mNormals[vertexIndex];
                vertex.normal = {normal.x, normal.y, normal.z};
            }

            if (hasSourceTangents)
            {
                if (mesh->mTangents == nullptr || mesh->mBitangents == nullptr || !mesh->HasNormals() ||
                    mesh->mNormals == nullptr)
                {
                    return makeLoadError(
                        LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                        std::format("Mesh '{}' reports tangent space but is missing tangent, bitangent, or normal "
                                    "arrays required to preserve tangent handedness.",
                                    meshAsset.name));
                }

                auto const &tangent = mesh->mTangents[vertexIndex];
                auto const &bitangent = mesh->mBitangents[vertexIndex];
                auto const &normal = mesh->mNormals[vertexIndex];
                auto const tangentFrameBitangent = cross(normal, tangent);
                auto const tangentFrameBitangentLength2 = squaredLength(tangentFrameBitangent);
                auto const bitangentLength2 = squaredLength(bitangent);
                auto const signSource = dot(tangentFrameBitangent, bitangent);
                if (tangentFrameBitangentLength2 <= 1e-12f || bitangentLength2 <= 1e-12f || !std::isfinite(signSource))
                {
                    ++invalidTangentFrameCount;
                    if (!firstInvalidTangentFrame.has_value())
                    {
                        firstInvalidTangentFrame =
                            std::format("vertex {} cross(normal,tangent)^2={} bitangent^2={} signSource={}",
                                        vertexIndex, tangentFrameBitangentLength2, bitangentLength2, signSource);
                    }

                    vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
                }
                else
                {
                    vertex.tangent = {tangent.x, tangent.y, tangent.z, tangentHandedness(signSource)};
                }
            }

            if (mesh->HasTextureCoords(0) && mesh->mTextureCoords[0] != nullptr)
            {
                auto const &uv = mesh->mTextureCoords[0][vertexIndex];
                vertex.texCoord0 = {uv.x, flipTextureCoordinateV ? 1.0f - uv.y : uv.y};
            }

            if (mesh->HasTextureCoords(1) && mesh->mTextureCoords[1] != nullptr)
            {
                auto const &uv = mesh->mTextureCoords[1][vertexIndex];
                vertex.texCoord1 = {uv.x, flipTextureCoordinateV ? 1.0f - uv.y : uv.y};
            }

            if (mesh->HasVertexColors(0) && mesh->mColors[0] != nullptr)
            {
                auto const &color = mesh->mColors[0][vertexIndex];
                vertex.color0 = {color.r, color.g, color.b, color.a};
            }

            meshAsset.vertices.push_back(vertex);
        }

        if (invalidTangentFrameCount > 0u)
        {
            nr::nrLog(nr::LogLevel::warning, "LOAD",
                      std::format("Mesh '{}' contains {} vertices with undefined tangent handedness; using +1 tangent "
                                  "sign for those vertices. First invalid frame: {}.",
                                  meshAsset.name, invalidTangentFrameCount,
                                  firstInvalidTangentFrame.value_or("unavailable")));
        }

        meshAsset.indices.reserve(static_cast<std::size_t>(mesh->mNumFaces) * 3u);
        auto faceVertexCounts = std::vector<std::uint32_t>{};
        faceVertexCounts.reserve(mesh->mNumFaces);
        auto faceIndices = std::views::iota(0u, mesh->mNumFaces);
        for (auto faceIndex : faceIndices)
        {
            auto const &face = mesh->mFaces[faceIndex];
            if (face.mNumIndices == 0)
            {
                continue;
            }

            if (strict && face.mNumIndices < 3)
            {
                return makeLoadError(
                    LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                    std::format("Mesh '{}' contains a face with fewer than 3 indices.", meshAsset.name));
            }

            faceVertexCounts.push_back(face.mNumIndices);
            auto localIndexRange = std::views::iota(0u, face.mNumIndices);
            for (auto localIndex : localIndexRange)
            {
                meshAsset.indices.push_back(face.mIndices[localIndex]);
            }
        }

        if (generateMissingGltfTangents && !hasSourceTangents && !meshAsset.indices.empty())
        {
            auto const selection = selectTangentUv(scene, materialIndex, meshAsset.name);
            auto const hasNormals = mesh->HasNormals() && mesh->mNormals != nullptr;
            auto const hasSelectedTexCoords =
                mesh->HasTextureCoords(selection.uvChannel) && mesh->mTextureCoords[selection.uvChannel] != nullptr;
            auto const hasSupportedFaces =
                std::ranges::all_of(faceVertexCounts, [](std::uint32_t count) { return count >= 3u; });

            if (selection.hasNormalTexture && (!hasNormals || !hasSelectedTexCoords || !hasSupportedFaces))
            {
                return makeLoadError(
                    LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                    std::format("Mesh '{}' requires generated tangents for its {} texture, but normals, TEXCOORD_{}, "
                                "or triangle/polygon face data is missing.",
                                meshAsset.name, nr::resource::materialTextureSlotSemanticName(selection.semantic),
                                selection.uvChannel));
            }

            if (hasNormals && hasSelectedTexCoords && hasSupportedFaces)
            {
                if (auto error = generateMikkTangents(meshAsset, faceVertexCounts, selection, scene.sourcePath);
                    error.has_value())
                {
                    return error;
                }
            }
        }

        meshAsset.geometries.push_back(MeshGeometryAsset{
            .name = meshAsset.name.empty() ? std::format("mesh_{}_geometry_0", meshIndex)
                                           : std::format("{}_geometry_0", meshAsset.name),
            .firstIndex = 0,
            .indexCount = static_cast<std::uint32_t>(meshAsset.indices.empty() ? meshAsset.vertices.size()
                                                                               : meshAsset.indices.size()),
            .materialIndex = materialIndex,
        });

        scene.meshes.push_back(std::move(meshAsset));
    }

    return std::nullopt;
}

[[nodiscard]] std::uint32_t appendNodeRecursive(const aiNode &node, std::uint32_t parentIndex, SceneAsset &scene)
{
    auto nodeIndex = static_cast<std::uint32_t>(scene.nodes.size());

    NodeAsset nodeAsset{};
    nodeAsset.name = toStdString(node.mName);
    if (nodeAsset.name.empty())
    {
        nodeAsset.name = std::format("node_{}", nodeIndex);
    }
    nodeAsset.parentIndex = parentIndex;
    nodeAsset.localTransform = toMatrix(node.mTransformation);
    nodeAsset.meshIndices.reserve(node.mNumMeshes);

    auto meshSlots = std::views::iota(0u, node.mNumMeshes);
    for (auto meshSlot : meshSlots)
    {
        nodeAsset.meshIndices.push_back(node.mMeshes[meshSlot]);
    }

    scene.nodes.push_back(std::move(nodeAsset));

    auto childSlots = std::views::iota(0u, node.mNumChildren);
    for (auto childSlot : childSlots)
    {
        auto const *child = node.mChildren[childSlot];
        if (child == nullptr)
        {
            continue;
        }

        auto childIndex = appendNodeRecursive(*child, nodeIndex, scene);
        scene.nodes[nodeIndex].childIndices.push_back(childIndex);
    }

    return nodeIndex;
}

[[nodiscard]] std::optional<LoadError> appendNodes(const aiScene &assimpScene, SceneAsset &scene)
{
    if (assimpScene.mRootNode == nullptr)
    {
        return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                             "Assimp scene root node is null.");
    }

    scene.rootNodeIndex = appendNodeRecursive(*assimpScene.mRootNode, invalidIndex, scene);
    return std::nullopt;
}

[[nodiscard]] std::map<std::string, std::vector<std::uint32_t>> buildNodeIndicesByName(const SceneAsset &scene)
{
    auto nodeIndicesByName = std::map<std::string, std::vector<std::uint32_t>>{};

    auto nodeIndices = std::views::iota(std::uint32_t{0}, static_cast<std::uint32_t>(scene.nodes.size()));
    for (auto nodeIndex : nodeIndices)
    {
        auto const &node = scene.nodes[nodeIndex];
        if (node.name.empty())
        {
            continue;
        }

        nodeIndicesByName[node.name].push_back(nodeIndex);
    }

    return nodeIndicesByName;
}

[[nodiscard]] std::optional<LoadError> resolveNodeIndexByName(
    const std::map<std::string, std::vector<std::uint32_t>> &nodeIndicesByName, std::string_view nodeName,
    std::string_view assetKind, std::uint32_t assetIndex, bool strict, const SceneAsset &scene,
    std::uint32_t &resolvedNodeIndex)
{
    resolvedNodeIndex = invalidIndex;
    if (nodeName.empty())
    {
        return std::nullopt;
    }

    auto found = nodeIndicesByName.find(std::string{nodeName});
    if (found == nodeIndicesByName.end())
    {
        if (!strict)
        {
            return std::nullopt;
        }

        return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                             std::format("{} {} references missing node '{}'.", assetKind, assetIndex, nodeName));
    }

    auto const &matches = found->second;
    if (matches.empty())
    {
        return std::nullopt;
    }

    resolvedNodeIndex = matches.front();
    if (matches.size() > 1 && strict)
    {
        return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                             std::format("{} {} references non-unique node name '{}' ({} matches).", assetKind,
                                         assetIndex, nodeName, matches.size()));
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<LoadError> appendCameras(
    const aiScene &assimpScene, const std::map<std::string, std::vector<std::uint32_t>> &nodeIndicesByName,
    SceneAsset &scene, bool strict)
{
    auto cameraIndices = std::views::iota(0u, assimpScene.mNumCameras);
    for (auto cameraIndex : cameraIndices)
    {
        auto const *camera = assimpScene.mCameras[cameraIndex];
        if (camera == nullptr)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Scene camera {} is null.", cameraIndex));
        }

        auto sourceNodeName = toStdString(camera->mName);

        CameraAsset cameraAsset{};
        cameraAsset.name = sourceNodeName.empty() ? std::format("camera_{}", cameraIndex) : sourceNodeName;
        cameraAsset.sourceNodeName = sourceNodeName;
        cameraAsset.position = toVector3(camera->mPosition);
        cameraAsset.lookAt = toVector3(camera->mLookAt);
        cameraAsset.up = toVector3(camera->mUp);
        cameraAsset.horizontalFov = camera->mHorizontalFOV;
        cameraAsset.aspect = camera->mAspect;
        cameraAsset.nearPlane = camera->mClipPlaneNear;
        cameraAsset.farPlane = camera->mClipPlaneFar;
        cameraAsset.orthographicWidth = camera->mOrthographicWidth;

        if (auto error = resolveNodeIndexByName(nodeIndicesByName, sourceNodeName, "camera", cameraIndex, strict, scene,
                                                cameraAsset.nodeIndex);
            error.has_value())
        {
            return error;
        }

        scene.cameras.push_back(std::move(cameraAsset));
    }

    return std::nullopt;
}

[[nodiscard]] std::optional<LoadError> appendLights(
    const aiScene &assimpScene, const std::map<std::string, std::vector<std::uint32_t>> &nodeIndicesByName,
    const std::map<std::string, float> &gltfLightRangesByNodeName, SceneAsset &scene, bool strict)
{
    auto lightIndices = std::views::iota(0u, assimpScene.mNumLights);
    for (auto lightIndex : lightIndices)
    {
        auto const *light = assimpScene.mLights[lightIndex];
        if (light == nullptr)
        {
            return makeLoadError(LoadErrorCode::invalidScene, "assimp", scene.sourcePath,
                                 std::format("Scene light {} is null.", lightIndex));
        }

        auto sourceNodeName = toStdString(light->mName);

        LightAsset lightAsset{};
        lightAsset.name = sourceNodeName.empty() ? std::format("light_{}", lightIndex) : sourceNodeName;
        lightAsset.sourceNodeName = sourceNodeName;
        lightAsset.typeRaw = static_cast<std::uint32_t>(light->mType);
        lightAsset.type = lightSourceTypeName(light->mType);
        lightAsset.position = toVector3(light->mPosition);
        lightAsset.direction = toVector3(light->mDirection);
        lightAsset.up = toVector3(light->mUp);
        lightAsset.colorDiffuse = toColor3(light->mColorDiffuse);
        lightAsset.colorSpecular = toColor3(light->mColorSpecular);
        lightAsset.colorAmbient = toColor3(light->mColorAmbient);
        lightAsset.attenuationConstant = light->mAttenuationConstant;
        lightAsset.attenuationLinear = light->mAttenuationLinear;
        lightAsset.attenuationQuadratic = light->mAttenuationQuadratic;
        if (auto range = gltfLightRangesByNodeName.find(sourceNodeName); range != gltfLightRangesByNodeName.end())
        {
            lightAsset.range = range->second;
        }
        lightAsset.innerCone = light->mAngleInnerCone;
        lightAsset.outerCone = light->mAngleOuterCone;
        lightAsset.areaSize = {light->mSize.x, light->mSize.y};

        if (auto error = resolveNodeIndexByName(nodeIndicesByName, sourceNodeName, "light", lightIndex, strict, scene,
                                                lightAsset.nodeIndex);
            error.has_value())
        {
            return error;
        }

        scene.lights.push_back(std::move(lightAsset));
    }

    return std::nullopt;
}

[[nodiscard]] std::uint32_t totalVertexCount(const SceneAsset &scene)
{
    auto vertexSizes = scene.meshes | std::views::transform([](const MeshAsset &mesh) {
                           return static_cast<std::uint64_t>(mesh.vertices.size());
                       });
    auto total = std::accumulate(vertexSizes.begin(), vertexSizes.end(), std::uint64_t{0});
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(total, std::numeric_limits<std::uint32_t>::max()));
}

[[nodiscard]] std::uint32_t totalIndexCount(const SceneAsset &scene)
{
    auto indexSizes = scene.meshes | std::views::transform([](const MeshAsset &mesh) {
                          return static_cast<std::uint64_t>(mesh.indices.size());
                      });
    auto total = std::accumulate(indexSizes.begin(), indexSizes.end(), std::uint64_t{0});
    return static_cast<std::uint32_t>(std::min<std::uint64_t>(total, std::numeric_limits<std::uint32_t>::max()));
}

[[nodiscard]] bool shouldFlipAssimpTextureCoordinateV(const std::filesystem::path &sourcePath)
{
    auto const extension = normalizedExtension(sourcePath);
    return extension == ".gltf" || extension == ".glb";
}

[[nodiscard]] aiPostProcessSteps buildPostProcessFlags(const SceneLoadRequest &request, bool gltfSource)
{
    std::uint32_t flags = aiProcess_SortByPType;

    if (request.triangulate)
    {
        flags |= aiProcess_Triangulate;
    }
    if (request.joinIdenticalVertices)
    {
        flags |= aiProcess_JoinIdenticalVertices;
    }
    if (request.generateNormals)
    {
        flags |= aiProcess_GenSmoothNormals;
    }
    if (request.generateTangents && !gltfSource)
    {
        flags |= aiProcess_CalcTangentSpace;
    }
    if (request.validateDataStructure)
    {
        flags |= aiProcess_ValidateDataStructure;
    }
    if (request.preTransformVertices)
    {
        flags |= aiProcess_PreTransformVertices;
    }
    if (request.optimizeMeshes)
    {
        flags |= aiProcess_OptimizeMeshes;
    }
    if (request.optimizeGraph)
    {
        flags |= aiProcess_OptimizeGraph;
    }

    return static_cast<aiPostProcessSteps>(flags);
}

} // namespace nr::load::detail

namespace nr::load
{
[[nodiscard]] nr::resource::MaterialTextureSlotSemantic assimpTextureSlotSemantic(std::uint32_t textureTypeRaw,
                                                                                  std::uint32_t textureSlot) noexcept
{
    return detail::textureSlotSemantic(static_cast<aiTextureType>(textureTypeRaw), textureSlot);
}

bool AssimpSceneImporter::supportsExtension(std::string_view extension)
{
    return std::ranges::find(kSupportedExtensions, extension) != kSupportedExtensions.end();
}

SceneImportResult AssimpSceneImporter::importScene(const SceneLoadRequest &request)
{
    if (request.sourcePath.empty())
    {
        return SceneImportResult{std::unexpected(
            makeError(LoadErrorCode::invalidArgument, request.sourcePath, "Scene source path is empty."))};
    }

    auto extension = normalizedExtension(request.sourcePath);
    if (!supportsExtension(extension))
    {
        return SceneImportResult{
            std::unexpected(makeError(LoadErrorCode::unsupportedFormat, request.sourcePath,
                                      std::format("Unsupported file extension '{}' for assimp backend.", extension)))};
    }

    if (!std::filesystem::exists(request.sourcePath))
    {
        return SceneImportResult{std::unexpected(
            makeError(LoadErrorCode::fileNotFound, request.sourcePath, "Source asset file does not exist."))};
    }

    Assimp::Importer importer{};
    auto const gltfSource = detail::shouldFlipAssimpTextureCoordinateV(request.sourcePath);
    auto flags = detail::buildPostProcessFlags(request, gltfSource);
    auto const *assimpScene = importer.ReadFile(request.sourcePath.string(), flags);
    if (assimpScene == nullptr)
    {
        return SceneImportResult{
            std::unexpected(makeError(LoadErrorCode::importFailed, request.sourcePath,
                                      std::format("Assimp import failed: {}", importer.GetErrorString())))};
    }

    if ((assimpScene->mFlags & detail::assimpSceneFlagIncomplete) != 0 || assimpScene->mRootNode == nullptr)
    {
        return SceneImportResult{std::unexpected(
            makeError(LoadErrorCode::invalidScene, request.sourcePath, "Assimp returned an incomplete scene graph."))};
    }

    SceneAsset scene{};
    scene.sourcePath = request.sourcePath;

    if (auto error = detail::appendEmbeddedTextures(*assimpScene, scene); error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    auto baseDirectory = request.searchRoot.empty() ? request.sourcePath.parent_path() : request.searchRoot;
    if (auto error = detail::appendMaterials(*assimpScene, baseDirectory, scene); error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    if (auto decodeResult = decodeSceneTextureImages(scene); !decodeResult.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(decodeResult.error()))};
    }

    if (auto error = detail::appendMeshes(*assimpScene, scene, request.strict, request.generateTangents && gltfSource);
        error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    if (auto error = detail::appendNodes(*assimpScene, scene); error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    auto nodeIndicesByName = detail::buildNodeIndicesByName(scene);
    auto gltfLightRangesByNodeName = detail::buildGltfLightRangesByNodeName(*assimpScene);
    if (auto error = detail::appendCameras(*assimpScene, nodeIndicesByName, scene, request.strict); error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    if (auto error =
            detail::appendLights(*assimpScene, nodeIndicesByName, gltfLightRangesByNodeName, scene, request.strict);
        error.has_value())
    {
        return SceneImportResult{std::unexpected(std::move(*error))};
    }

    scene.stats.nodeCount = static_cast<std::uint32_t>(scene.nodes.size());
    scene.stats.meshCount = static_cast<std::uint32_t>(scene.meshes.size());
    scene.stats.materialCount = static_cast<std::uint32_t>(scene.materials.size());
    scene.stats.textureCount = static_cast<std::uint32_t>(scene.textures.size());
    scene.stats.cameraCount = static_cast<std::uint32_t>(scene.cameras.size());
    scene.stats.lightCount = static_cast<std::uint32_t>(scene.lights.size());
    scene.stats.vertexCount = detail::totalVertexCount(scene);
    scene.stats.indexCount = detail::totalIndexCount(scene);

    return scene;
}
} // namespace nr::load
