module;
export module nr.scene:utils;

import dependency;
import nr.load;
import nr.resource;
import std;
import :type;

export namespace nr::scene::detail
{
[[nodiscard]] inline std::string sanitizeEntityName(std::string_view label)
{
    auto sanitized = std::string{label};
    std::ranges::for_each(sanitized, [](char &character) {
        auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) == 0 && character != '_' && character != '-')
        {
            character = '_';
        }
    });

    if (sanitized.empty())
    {
        return "scene";
    }

    return sanitized;
}

[[nodiscard]] inline std::string normalizeSemantic(std::string_view semantic)
{
    auto normalized = std::string{};
    normalized.reserve(semantic.size());

    std::ranges::for_each(semantic, [&](char character) {
        auto value = static_cast<unsigned char>(character);
        if (std::isalnum(value) == 0)
        {
            return;
        }

        normalized.push_back(static_cast<char>(std::tolower(value)));
    });

    return normalized;
}

[[nodiscard]] inline MaterialSemanticSlot classifyMaterialSemantic(std::string_view semantic)
{
    auto normalized = normalizeSemantic(semantic);

    if (normalized.empty())
    {
        return MaterialSemanticSlot::unsupported;
    }

    auto contains = [&](std::string_view token) {
        return normalized.find(token) != std::string::npos;
    };

    if (contains("diffuse") || contains("basecolor") || contains("albedo") || contains("color"))
    {
        return MaterialSemanticSlot::baseColor;
    }

    if (contains("emissive"))
    {
        return MaterialSemanticSlot::emissive;
    }

    if (contains("normal") || contains("height") || contains("displacement") || contains("bump"))
    {
        return MaterialSemanticSlot::normal;
    }

    if (contains("lightmap") || contains("ambient") || contains("ao") || contains("occlusion"))
    {
        return MaterialSemanticSlot::occlusion;
    }

    if (contains("metal") || contains("rough") || contains("specular") || contains("shininess"))
    {
        return MaterialSemanticSlot::metallicRoughness;
    }

    return MaterialSemanticSlot::unsupported;
}

[[nodiscard]] inline bool semanticIsColor(MaterialSemanticSlot slot) noexcept
{
    return slot == MaterialSemanticSlot::baseColor || slot == MaterialSemanticSlot::emissive;
}

[[nodiscard]] inline bool semanticIsLinear(MaterialSemanticSlot slot) noexcept
{
    return slot == MaterialSemanticSlot::normal ||
           slot == MaterialSemanticSlot::metallicRoughness ||
           slot == MaterialSemanticSlot::occlusion;
}

[[nodiscard]] inline std::string_view slotName(MaterialSemanticSlot slot) noexcept
{
    switch (slot)
    {
    case MaterialSemanticSlot::baseColor: return "baseColor";
    case MaterialSemanticSlot::normal: return "normal";
    case MaterialSemanticSlot::metallicRoughness: return "metallicRoughness";
    case MaterialSemanticSlot::occlusion: return "occlusion";
    case MaterialSemanticSlot::emissive: return "emissive";
    default: return "unsupported";
    }
}

[[nodiscard]] inline std::vector<TextureColorSpaceHint> buildTextureColorSpaceHints(const nr::load::SceneAsset &sceneAsset)
{
    auto hints = std::vector<TextureColorSpaceHint>(sceneAsset.textures.size());

    std::ranges::for_each(sceneAsset.materials, [&](const nr::load::MaterialAsset &materialAsset) {
        std::ranges::for_each(materialAsset.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex >= hints.size())
            {
                return;
            }

            auto semanticSlot = classifyMaterialSemantic(binding.semantic);
            auto &hint = hints[binding.textureIndex];

            if (semanticIsColor(semanticSlot))
            {
                hint.hasColor = true;
            }

            if (semanticIsLinear(semanticSlot))
            {
                hint.hasLinear = true;
            }
        });
    });

    std::ranges::for_each(hints, [](TextureColorSpaceHint &hint) {
        hint.preferSrgb = !hint.hasLinear;
    });

    return hints;
}

[[nodiscard]] inline nr::resource::PixelFormat pickTextureFormat(std::uint32_t channels, bool srgb) noexcept
{
    switch (channels)
    {
    case 1u: return vk::Format::eR8Unorm;
    case 2u: return vk::Format::eR8G8Unorm;
    default: return srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    }
}

[[nodiscard]] inline std::optional<PreparedImageLevel> prepareDecodedImageLevel(const nr::load::Image &image)
{
    if (image.width == 0u || image.height == 0u || image.channels == 0u || image.pixels.empty())
    {
        return std::nullopt;
    }

    auto const texelCount = static_cast<std::uint64_t>(image.width) * static_cast<std::uint64_t>(image.height);
    auto const expectedBytes = texelCount * static_cast<std::uint64_t>(image.channels);
    if (expectedBytes != image.pixels.size())
    {
        return std::nullopt;
    }

    auto prepared = PreparedImageLevel{};
    prepared.level.width = image.width;
    prepared.level.height = image.height;
    prepared.level.depth = 1;

    if (image.channels == 3u)
    {
        prepared.channelCount = 4u;
        prepared.level.bytes.resize(static_cast<std::size_t>(texelCount) * 4u);
        auto const texelIndices = std::views::iota(std::size_t{0}, static_cast<std::size_t>(texelCount));
        std::ranges::for_each(texelIndices, [&](std::size_t texelIndex) {
            auto const source = texelIndex * 3u;
            auto const destination = texelIndex * 4u;
            prepared.level.bytes[destination + 0u] = std::byte{image.pixels[source + 0u]};
            prepared.level.bytes[destination + 1u] = std::byte{image.pixels[source + 1u]};
            prepared.level.bytes[destination + 2u] = std::byte{image.pixels[source + 2u]};
            prepared.level.bytes[destination + 3u] = std::byte{0xFFu};
        });
        return prepared;
    }

    prepared.channelCount = image.channels;
    prepared.level.bytes.resize(image.pixels.size());
    std::ranges::transform(image.pixels, prepared.level.bytes.begin(), [](std::uint8_t value) {
        return std::byte{value};
    });
    return prepared;
}

[[nodiscard]] inline nr::resource::ImageLevel prepareRawImageLevel(const nr::load::EmbeddedRawTexture &raw)
{
    auto level = nr::resource::ImageLevel{};
    level.width = raw.width;
    level.height = raw.height;
    level.depth = 1;
    level.bytes = raw.rgba8;
    return level;
}

[[nodiscard]] inline std::string makeDeterministicChildName(SiblingNameTable &namesByParent,
                                                             flecs::entity_t parent,
                                                             std::string_view sourceName)
{
    auto const baseName = sanitizeEntityName(sourceName.empty() ? std::string_view{"node"} : sourceName);
    auto &nameCounters = namesByParent[parent];
    auto &count = nameCounters[baseName];
    auto const suffix = count;
    ++count;

    if (suffix == 0u)
    {
        return baseName;
    }

    return std::format("{}_{}", baseName, suffix);
}

[[nodiscard]] inline std::string makeTemplateNodeEntityName(SceneTemplateHandle handle,
                                                             std::uint32_t sourceNodeIndex,
                                                             std::string_view resolvedName)
{
    return std::format("scene_template_{}_{}_node_{}_{}",
                       handle.slot,
                       handle.generation,
                       sourceNodeIndex,
                       sanitizeEntityName(resolvedName));
}

[[nodiscard]] inline std::string makeTemplateMeshEntityName(SceneTemplateHandle handle,
                                                             std::uint32_t sourceNodeIndex,
                                                             std::uint32_t meshSlot)
{
    return std::format("scene_template_{}_{}_node_{}_mesh_{}",
                       handle.slot,
                       handle.generation,
                       sourceNodeIndex,
                       meshSlot);
}

[[nodiscard]] inline std::string makeTemplateCameraEntityName(SceneTemplateHandle handle,
                                                               std::uint32_t sourceNodeIndex,
                                                               std::uint32_t cameraSlot)
{
    return std::format("scene_template_{}_{}_node_{}_camera_{}",
                       handle.slot,
                       handle.generation,
                       sourceNodeIndex,
                       cameraSlot);
}

[[nodiscard]] inline std::string makeTemplateLightEntityName(SceneTemplateHandle handle,
                                                              std::uint32_t sourceNodeIndex,
                                                              std::uint32_t lightSlot)
{
    return std::format("scene_template_{}_{}_node_{}_light_{}",
                       handle.slot,
                       handle.generation,
                       sourceNodeIndex,
                       lightSlot);
}

[[nodiscard]] inline glm::vec3 toVec3(std::array<float, 3> const &value)
{
    return glm::vec3{value[0], value[1], value[2]};
}

[[nodiscard]] inline glm::mat4 toGlmMat4(const std::array<float, 16> &value)
{
    auto matrix = glm::mat4{1.0f};
    auto const rowIndices = std::views::iota(0, 4);
    auto const columnIndices = std::views::iota(0, 4);

    std::ranges::for_each(rowIndices, [&](int row) {
        std::ranges::for_each(columnIndices, [&](int column) {
            auto const linearIndex = static_cast<std::size_t>(row * 4 + column);
            matrix[column][row] = value[linearIndex];
        });
    });

    return matrix;
}

[[nodiscard]] inline bool finiteMat4(const glm::mat4 &value) noexcept
{
    auto const columnIndices = std::views::iota(0, 4);
    return std::ranges::all_of(columnIndices, [&](int column) {
        return nr::resource::math::finiteVec(value[column]);
    });
}

[[nodiscard]] inline glm::vec3 transformPoint(const glm::mat4 &matrix, const glm::vec3 &point)
{
    auto const transformed = matrix * glm::vec4{point, 1.0f};
    return glm::vec3{transformed.x, transformed.y, transformed.z};
}

[[nodiscard]] inline nr::resource::Aabb transformAabb(const nr::resource::Aabb &bounds,
                                                       const glm::mat4 &matrix)
{
    if (!bounds.valid() || !finiteMat4(matrix))
    {
        return nr::resource::Aabb{};
    }

    auto transformed = nr::resource::Aabb{};
    auto const xMask = std::array{0.0f, 1.0f};
    auto const yMask = std::array{0.0f, 1.0f};
    auto const zMask = std::array{0.0f, 1.0f};

    std::ranges::for_each(xMask, [&](float xBit) {
        std::ranges::for_each(yMask, [&](float yBit) {
            std::ranges::for_each(zMask, [&](float zBit) {
                auto const x = xBit > 0.5f ? bounds.max.x : bounds.min.x;
                auto const y = yBit > 0.5f ? bounds.max.y : bounds.min.y;
                auto const z = zBit > 0.5f ? bounds.max.z : bounds.min.z;
                transformed.expand(transformPoint(matrix, glm::vec3{x, y, z}));
            });
        });
    });

    return transformed;
}

[[nodiscard]] inline std::optional<nr::resource::LightType> mapLightType(std::string_view typeName)
{
    auto lowered = std::string{};
    lowered.reserve(typeName.size());

    std::ranges::for_each(typeName, [&](char character) {
        auto value = static_cast<unsigned char>(character);
        lowered.push_back(static_cast<char>(std::tolower(value)));
    });

    if (lowered == "directional")
    {
        return nr::resource::LightType::directional;
    }

    if (lowered == "point")
    {
        return nr::resource::LightType::point;
    }

    if (lowered == "spot")
    {
        return nr::resource::LightType::spot;
    }

    return std::nullopt;
}

template <typename HandleT>
inline void appendUniqueHandle(std::vector<HandleT> &handles,
                               std::set<std::uint64_t> &seen,
                               HandleT handle)
{
    if (seen.emplace(handle.packed()).second)
    {
        handles.push_back(handle);
    }
}
} // namespace nr::scene::detail
