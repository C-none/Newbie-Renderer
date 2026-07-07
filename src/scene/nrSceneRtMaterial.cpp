module nr.scene;
import :rtMaterial;

import nr.resource;
import std;
import :type;

namespace nr::scene
{
namespace
{
[[nodiscard]] bool slotHasTexture(
    const nr::resource::Material& material,
    nr::resource::MaterialTextureSlotSemantic semantic) noexcept
{
    if (!nr::resource::materialTextureSlotSemanticValid(semantic))
    {
        return false;
    }

    return material.slot(semantic).texture.valid();
}

[[nodiscard]] MaterialTextureSlotFlag layerTextureMask(
    const nr::resource::Material& material,
    std::initializer_list<nr::resource::MaterialTextureSlotSemantic> slots) noexcept
{
    auto mask = MaterialTextureSlotFlag::none;
    std::ranges::for_each(slots, [&](nr::resource::MaterialTextureSlotSemantic semantic) {
        if (slotHasTexture(material, semantic))
        {
            mask |= rtMaterialTextureMask(semantic);
        }
    });
    return mask;
}

[[nodiscard]] bool anyNonZero(glm::vec3 value) noexcept
{
    return value.x != 0.0f || value.y != 0.0f || value.z != 0.0f;
}

[[nodiscard]] RtMaterialFeatureFlag rtFeatureFlags(const nr::resource::Material& material) noexcept
{
    auto flags = RtMaterialFeatureFlag::none;

    if (material.clearcoat.has_value() ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoat) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal))
    {
        flags |= RtMaterialFeatureFlag::clearcoat;
    }

    if (material.sheen.has_value() ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::sheenColor) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::sheenRoughness))
    {
        flags |= RtMaterialFeatureFlag::sheen;
    }

    if (material.transmission.has_value() ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::transmission))
    {
        flags |= RtMaterialFeatureFlag::transmission;
    }

    if (material.core.alphaMode == nr::resource::AlphaMode::mask)
    {
        flags |= RtMaterialFeatureFlag::alphaMask;
    }
    else if (material.core.alphaMode == nr::resource::AlphaMode::blend)
    {
        flags |= RtMaterialFeatureFlag::alphaBlend;
    }

    if (material.core.doubleSided)
    {
        flags |= RtMaterialFeatureFlag::doubleSided;
    }

    if (anyNonZero(material.core.emissiveFactor) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::emissive))
    {
        flags |= RtMaterialFeatureFlag::emissive;
    }

    if (material.anisotropy.has_value() ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::anisotropy))
    {
        flags |= RtMaterialFeatureFlag::unsupportedAnisotropy;
    }

    return flags;
}

[[nodiscard]] constexpr AlphaMode toRtAlphaMode(nr::resource::AlphaMode mode) noexcept
{
    switch (mode)
    {
    case nr::resource::AlphaMode::opaque: return AlphaMode::opaque;
    case nr::resource::AlphaMode::mask: return AlphaMode::mask;
    case nr::resource::AlphaMode::blend: return AlphaMode::blend;
    }

    return AlphaMode::opaque;
}

[[nodiscard]] RtMaterialLayerRecord makeBaseLayer(const nr::resource::Material& material) noexcept
{
    return RtMaterialLayerRecord{
        .kind = RtMaterialLayerKind::baseSurface,
        .textureMask = layerTextureMask(
            material,
            {
                nr::resource::MaterialTextureSlotSemantic::baseColor,
                nr::resource::MaterialTextureSlotSemantic::normal,
                nr::resource::MaterialTextureSlotSemantic::metallicRoughness,
                nr::resource::MaterialTextureSlotSemantic::occlusion,
                nr::resource::MaterialTextureSlotSemantic::emissive,
            }),
        .p0 = material.core.baseColorFactor,
        .p1 = glm::vec4{
            material.core.emissiveFactor,
            material.core.metallicFactor,
        },
    };
}

[[nodiscard]] RtMaterialLayerRecord makeClearcoatLayer(const nr::resource::Material& material) noexcept
{
    auto factor = 0.0f;
    auto roughness = 0.0f;
    if (material.clearcoat.has_value())
    {
        factor = material.clearcoat->factor;
        roughness = material.clearcoat->roughnessFactor;
    }

    return RtMaterialLayerRecord{
        .kind = RtMaterialLayerKind::clearcoat,
        .textureMask = layerTextureMask(
            material,
            {
                nr::resource::MaterialTextureSlotSemantic::clearcoat,
                nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness,
                nr::resource::MaterialTextureSlotSemantic::clearcoatNormal,
            }),
        .p0 = glm::vec4{factor, roughness, 0.0f, 0.0f},
    };
}

[[nodiscard]] RtMaterialLayerRecord makeSheenLayer(const nr::resource::Material& material) noexcept
{
    auto color = glm::vec3{0.0f};
    auto roughness = 0.0f;
    if (material.sheen.has_value())
    {
        color = material.sheen->colorFactor;
        roughness = material.sheen->roughnessFactor;
    }

    return RtMaterialLayerRecord{
        .kind = RtMaterialLayerKind::sheen,
        .textureMask = layerTextureMask(
            material,
            {
                nr::resource::MaterialTextureSlotSemantic::sheenColor,
                nr::resource::MaterialTextureSlotSemantic::sheenRoughness,
            }),
        .p0 = glm::vec4{color, roughness},
    };
}

[[nodiscard]] RtMaterialLayerRecord makeTransmissionLayer(const nr::resource::Material& material) noexcept
{
    auto factor = material.transmission.has_value() ? material.transmission->factor : 0.0f;
    return RtMaterialLayerRecord{
        .kind = RtMaterialLayerKind::transmission,
        .textureMask = layerTextureMask(
            material,
            {
                nr::resource::MaterialTextureSlotSemantic::transmission,
            }),
        .p0 = glm::vec4{factor, 0.0f, 0.0f, 0.0f},
    };
}
} // namespace

[[nodiscard]] MaterialTextureSlotFlag rtMaterialTextureMask(nr::resource::MaterialTextureSlotSemantic semantic) noexcept
{
    if (!nr::resource::materialTextureSlotSemanticValid(semantic))
    {
        return MaterialTextureSlotFlag::none;
    }

    return static_cast<MaterialTextureSlotFlag>(1u << static_cast<std::uint32_t>(semantic));
}

[[nodiscard]] RtCompiledMaterial compileRtMaterial(
    const nr::resource::Material& material,
    const SceneMaterialTextureIds& textureIds)
{
    auto compiled = RtCompiledMaterial{};
    auto const flags = rtFeatureFlags(material);

    auto const clearcoatFactor = material.clearcoat.has_value() ? material.clearcoat->factor : 0.0f;
    auto const clearcoatRoughness = material.clearcoat.has_value() ? material.clearcoat->roughnessFactor : 0.0f;
    auto const sheenMax = material.sheen.has_value()
                              ? std::max({material.sheen->colorFactor.x, material.sheen->colorFactor.y, material.sheen->colorFactor.z})
                              : 0.0f;

    compiled.header = RtMaterialHeader{
        .abiVersion = kRtMaterialAbiVersion,
        .featureFlags = flags,
        .alphaMode = toRtAlphaMode(material.core.alphaMode),
        .alphaCutoff = material.core.alphaCutoff,
        .baseColorFactor = material.core.baseColorFactor,
        .emissiveAndMetallic = glm::vec4{material.core.emissiveFactor, material.core.metallicFactor},
        .roughnessNormalOcclusionAlpha = glm::vec4{
            material.core.roughnessFactor,
            material.core.normalScale,
            material.core.occlusionStrength,
            material.core.alphaCutoff,
        },
        .transmissionClearcoatSheen = glm::vec4{
            material.transmission.has_value() ? material.transmission->factor : 0.0f,
            clearcoatFactor,
            clearcoatRoughness,
            sheenMax,
        },
    };

    if (hasAnyRtMaterialFeature(flags, RtMaterialFeatureFlag::clearcoat))
    {
        compiled.layers.push_back(makeClearcoatLayer(material));
    }
    if (hasAnyRtMaterialFeature(flags, RtMaterialFeatureFlag::sheen))
    {
        compiled.layers.push_back(makeSheenLayer(material));
    }

    compiled.layers.push_back(makeBaseLayer(material));

    if (hasAnyRtMaterialFeature(flags, RtMaterialFeatureFlag::transmission))
    {
        compiled.layers.push_back(makeTransmissionLayer(material));
    }

    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto const& slot = material.textureSlots[slotIndex];
        if (!slot.texture.valid())
        {
            return;
        }

        compiled.textureRefs.push_back(RtMaterialTextureRef{
            .slot = static_cast<MaterialTextureSlot>(static_cast<std::uint32_t>(slotIndex)),
            .textureId = textureIds[slotIndex],
            .uvSet = slot.uvSet,
        });
    });

    compiled.header.layerCount = static_cast<std::uint32_t>(compiled.layers.size());
    compiled.header.textureRefCount = static_cast<std::uint32_t>(compiled.textureRefs.size());
    return compiled;
}

[[nodiscard]] RtMaterialTable makeRtMaterialTable(std::span<const std::reference_wrapper<const RtCompiledMaterial>> materials)
{
    auto table = RtMaterialTable{};
    table.headers.reserve(materials.size());

    std::ranges::for_each(materials, [&](std::reference_wrapper<const RtCompiledMaterial> materialRef) {
        auto const& material = materialRef.get();
        auto header = material.header;
        header.layerOffset = static_cast<std::uint32_t>(table.layers.size());
        header.layerCount = static_cast<std::uint32_t>(material.layers.size());
        header.textureRefOffset = static_cast<std::uint32_t>(table.textureRefs.size());
        header.textureRefCount = static_cast<std::uint32_t>(material.textureRefs.size());

        table.headers.push_back(header);
        table.layers.insert(table.layers.end(), material.layers.begin(), material.layers.end());
        table.textureRefs.insert(table.textureRefs.end(), material.textureRefs.begin(), material.textureRefs.end());
    });

    return table;
}

[[nodiscard]] RtCompiledMaterial makeFallbackRtMaterial()
{
    auto material = nr::resource::Material{};
    material.name = "rt_fallback";
    material.core.baseColorFactor = glm::vec4{1.0f, 0.0f, 1.0f, 1.0f};
    material.core.roughnessFactor = 1.0f;
    material.core.metallicFactor = 0.0f;
    return compileRtMaterial(material);
}
} // namespace nr::scene
