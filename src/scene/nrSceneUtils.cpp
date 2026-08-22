module nr.scene;
import :utils;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;
import nr.load;
import nr.resource;
import std;
import :type;

namespace nr::scene::detail
{
[[nodiscard]] std::string sanitizeEntityName(std::string_view label)
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

[[nodiscard]] bool semanticIsColor(nr::resource::MaterialTextureSlotSemantic slot) noexcept
{
    switch (slot)
    {
    case nr::resource::MaterialTextureSlotSemantic::baseColor:
    case nr::resource::MaterialTextureSlotSemantic::emissive:
    case nr::resource::MaterialTextureSlotSemantic::sheenColor:
    case nr::resource::MaterialTextureSlotSemantic::specularColor:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool semanticRequiresLinearColorChannels(nr::resource::MaterialTextureSlotSemantic slot) noexcept
{
    // Vulkan sRGB formats decode RGB only. KHR_materials_specular reads its scalar texture from A,
    // so sharing one RGBA image with specularColor remains compatible with an sRGB image view.
    return nr::resource::materialTextureSlotSemanticValid(slot) && !semanticIsColor(slot) &&
           slot != nr::resource::MaterialTextureSlotSemantic::specular;
}

[[nodiscard]] std::vector<TextureColorSpaceHint> buildTextureColorSpaceHints(const nr::load::SceneAsset &sceneAsset)
{
    auto hints = std::vector<TextureColorSpaceHint>(sceneAsset.textures.size());

    std::ranges::for_each(sceneAsset.materials, [&](const nr::load::MaterialAsset &materialAsset) {
        std::ranges::for_each(materialAsset.textures, [&](const nr::load::MaterialTextureBinding &binding) {
            if (binding.textureIndex >= hints.size())
            {
                return;
            }

            auto &hint = hints[binding.textureIndex];

            if (semanticIsColor(binding.semantic))
            {
                hint.hasColor = true;
            }

            if (semanticRequiresLinearColorChannels(binding.semantic))
            {
                hint.hasLinear = true;
            }
        });
    });

    std::ranges::for_each(hints,
                          [](TextureColorSpaceHint &hint) { hint.preferSrgb = hint.hasColor && !hint.hasLinear; });

    return hints;
}

[[nodiscard]] nr::resource::PixelFormat pickTextureFormat(std::uint32_t channels, bool srgb) noexcept
{
    switch (channels)
    {
    case 1u:
        return vk::Format::eR8Unorm;
    case 2u:
        return vk::Format::eR8G8Unorm;
    default:
        return srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
    }
}

[[nodiscard]] std::optional<PreparedImageLevel> prepareDecodedImageLevel(const nr::load::Image &image)
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
    std::ranges::transform(image.pixels, prepared.level.bytes.begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
    return prepared;
}

[[nodiscard]] nr::resource::ImageLevel prepareRawImageLevel(const nr::load::EmbeddedRawTexture &raw)
{
    auto level = nr::resource::ImageLevel{};
    level.width = raw.width;
    level.height = raw.height;
    level.bytes = raw.rgba8;
    return level;
}

[[nodiscard]] std::string makeDeterministicChildName(SiblingNameTable &namesByParent, flecs::entity_t parent,
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

[[nodiscard]] std::string makeTemplateNodeEntityName(SceneTemplateHandle handle, std::uint32_t sourceNodeIndex,
                                                     std::string_view resolvedName)
{
    return std::format("scene_template_{}_{}_node_{}_{}", handle.slot, handle.generation, sourceNodeIndex,
                       sanitizeEntityName(resolvedName));
}

[[nodiscard]] std::string makeTemplateMeshEntityName(SceneTemplateHandle handle, std::uint32_t sourceNodeIndex,
                                                     std::uint32_t meshSlot)
{
    return std::format("scene_template_{}_{}_node_{}_mesh_{}", handle.slot, handle.generation, sourceNodeIndex,
                       meshSlot);
}

[[nodiscard]] std::string makeTemplateCameraEntityName(SceneTemplateHandle handle, std::uint32_t sourceNodeIndex,
                                                       std::uint32_t cameraSlot)
{
    return std::format("scene_template_{}_{}_node_{}_camera_{}", handle.slot, handle.generation, sourceNodeIndex,
                       cameraSlot);
}

[[nodiscard]] std::string makeTemplateLightEntityName(SceneTemplateHandle handle, std::uint32_t sourceNodeIndex,
                                                      std::uint32_t lightSlot)
{
    return std::format("scene_template_{}_{}_node_{}_light_{}", handle.slot, handle.generation, sourceNodeIndex,
                       lightSlot);
}

[[nodiscard]] DirectX::XMFLOAT3 toFloat3(std::array<float, 3> const &value)
{
    return DirectX::XMFLOAT3{value[0], value[1], value[2]};
}

[[nodiscard]] DirectX::XMFLOAT4X4 toRowMajorFloat4x4(const std::array<float, 16> &value)
{
    // Imported transforms use column vectors and row-linear storage. Transpose once
    // at this boundary so every scene matrix thereafter uses DirectX row-vector math.
    return DirectX::XMFLOAT4X4{
        value[0], value[4], value[8], value[12],
        value[1], value[5], value[9], value[13],
        value[2], value[6], value[10], value[14],
        value[3], value[7], value[11], value[15],
    };
}

[[nodiscard]] bool finiteMat4(const DirectX::XMFLOAT4X4 &value) noexcept
{
    return std::isfinite(value._11) && std::isfinite(value._12) && std::isfinite(value._13) && std::isfinite(value._14) &&
           std::isfinite(value._21) && std::isfinite(value._22) && std::isfinite(value._23) && std::isfinite(value._24) &&
           std::isfinite(value._31) && std::isfinite(value._32) && std::isfinite(value._33) && std::isfinite(value._34) &&
           std::isfinite(value._41) && std::isfinite(value._42) && std::isfinite(value._43) && std::isfinite(value._44);
}

[[nodiscard]] DirectX::XMFLOAT3 transformPoint(const DirectX::XMFLOAT4X4 &matrix,
                                                const DirectX::XMFLOAT3 &point)
{
    auto transformed = DirectX::XMFLOAT3{};
    DirectX::XMStoreFloat3(
        &transformed,
        DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&point), DirectX::XMLoadFloat4x4(&matrix)));
    return transformed;
}

[[nodiscard]] nr::resource::Aabb transformAabb(const nr::resource::Aabb &bounds,
                                                const DirectX::XMFLOAT4X4 &matrix)
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
                transformed.expand(transformPoint(matrix, DirectX::XMFLOAT3{x, y, z}));
            });
        });
    });

    return transformed;
}

[[nodiscard]] std::optional<nr::resource::LightType> mapLightType(std::string_view typeName)
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
} // namespace nr::scene::detail
