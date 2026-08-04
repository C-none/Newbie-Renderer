module nr.scene;
import :rtMaterial;

import nr.resource;
import nr.utils;
import std;
import :type;

namespace nr::scene
{
namespace
{
[[nodiscard]] bool slotHasTexture(const nr::resource::Material &material,
                                  nr::resource::MaterialTextureSlotSemantic semantic) noexcept
{
    if (!nr::resource::materialTextureSlotSemanticValid(semantic))
    {
        return false;
    }

    return material.slot(semantic).texture.valid();
}

[[nodiscard]] bool anyNonZero(glm::vec3 value) noexcept
{
    return value.x != 0.0f || value.y != 0.0f || value.z != 0.0f;
}

[[nodiscard]] float effectiveTransmissionFactor(const nr::resource::Material &material) noexcept
{
    if (!material.transmission.has_value() || !std::isfinite(material.transmission->factor))
    {
        return 0.0f;
    }

    return std::clamp(material.transmission->factor, 0.0f, 1.0f);
}

[[nodiscard]] float effectiveAnisotropyFactor(const nr::resource::Material &material) noexcept
{
    if (!material.anisotropy.has_value() || !std::isfinite(material.anisotropy->factor))
    {
        return 0.0f;
    }

    return std::clamp(material.anisotropy->factor, 0.0f, 1.0f);
}

[[nodiscard]] float anisotropyRotation(const nr::resource::Material &material) noexcept
{
    if (!material.anisotropy.has_value() || !std::isfinite(material.anisotropy->rotation))
    {
        return 0.0f;
    }

    return material.anisotropy->rotation;
}

[[nodiscard]] float transmissionIor(const nr::resource::Material &material) noexcept
{
    auto const authoredIor = material.ior.has_value() ? material.ior->ior : 1.5f;
    if (!std::isfinite(authoredIor))
    {
        return 1.5f;
    }
    if (authoredIor == 0.0f)
    {
        return 0.0f;
    }
    return std::max(authoredIor, 1.0f);
}

[[nodiscard]] RtTransmissionMode transmissionMode(const nr::resource::Material &material) noexcept
{
    return material.hasVolumeTransmissionBoundary() ? RtTransmissionMode::volume : RtTransmissionMode::thin;
}

// Non-layer feature flags only. Layer classification (clearcoat/sheen/transmission) lives in
// rtLayerFlags/normalizeRtLayerFlags; RtMaterialFeatureFlag no longer carries layer bits.
[[nodiscard]] RtMaterialFeatureFlag rtFeatureFlags(const nr::resource::Material &material) noexcept
{
    auto flags = RtMaterialFeatureFlag::none;

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

    if (!material.unlit && material.hasVolumeTransmissionBoundary())
    {
        flags |= RtMaterialFeatureFlag::volumeBoundary;
    }

    if (anyNonZero(material.core.emissiveFactor) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::emissive))
    {
        flags |= RtMaterialFeatureFlag::emissive;
    }

    return flags;
}

// RT layer classification is independent from non-layer feature flags. layerFlags == none is the sole
// unlit encoding; any non-zero mask must contain baseSurface (the 9 valid glTF combinations).
[[nodiscard]] RtMaterialLayerFlag rtLayerFlags(const nr::resource::Material &material) noexcept
{
    if (material.unlit)
    {
        return RtMaterialLayerFlag::none;
    }

    auto flags = RtMaterialLayerFlag::baseSurface;
    if (material.clearcoat.has_value() ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoat) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoatRoughness) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal))
    {
        flags |= RtMaterialLayerFlag::clearcoat;
    }
    if (material.sheen.has_value() || slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::sheenColor) ||
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::sheenRoughness))
    {
        flags |= RtMaterialLayerFlag::sheen;
    }
    // The texture is a multiplier and cannot revive a zero scalar factor. Keeping zero-factor
    // materials on the non-transmission specialization preserves the fixed nine-variant SBT matrix.
    if (effectiveTransmissionFactor(material) > 0.0f)
    {
        flags |= RtMaterialLayerFlag::transmission;
    }
    return flags;
}

[[nodiscard]] RtMaterialLayerFlag normalizeRtLayerFlags(const nr::resource::Material &material)
{
    auto const layerFlags = rtLayerFlags(material);
    auto const layerBits = static_cast<std::uint32_t>(layerFlags);
    nr::nrAssert((layerBits & ~static_cast<std::uint32_t>(kRtMaterialPhysicalLayerMask)) == 0u,
                 "RT material layer flags contain bits outside the known layer mask.");
    nr::nrAssert(layerFlags == RtMaterialLayerFlag::none ||
                     (layerBits & static_cast<std::uint32_t>(RtMaterialLayerFlag::baseSurface)) != 0u,
                 "Non-empty RT material layer flags must contain baseSurface.");

    if (material.unlit &&
        (material.clearcoat.has_value() || material.sheen.has_value() || material.transmission.has_value() ||
         material.ior.has_value() || material.volumeBoundary.has_value() || material.anisotropy.has_value() ||
         slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::anisotropy) ||
         anyNonZero(material.core.emissiveFactor)))
    {
        nr::nrInfo<nr::LogLevel::warning>(std::format(
            "RT material '{}' is unlit; ignoring authored PBR extension and emissive data.", material.name));
    }

    return layerFlags;
}

[[nodiscard]] constexpr AlphaMode toRtAlphaMode(nr::resource::AlphaMode mode) noexcept
{
    switch (mode)
    {
    case nr::resource::AlphaMode::opaque:
        return AlphaMode::opaque;
    case nr::resource::AlphaMode::mask:
        return AlphaMode::mask;
    case nr::resource::AlphaMode::blend:
        return AlphaMode::blend;
    }

    return AlphaMode::opaque;
}

[[nodiscard]] RtMaterialLayerRecord makeBaseLayer(const nr::resource::Material &material) noexcept
{
    return RtMaterialLayerRecord{
        .layer = RtMaterialLayerFlag::baseSurface,
        .p0 = material.core.baseColorFactor,
        .p1 =
            glm::vec4{
                material.core.emissiveFactor,
                material.core.metallicFactor,
            },
    };
}

[[nodiscard]] RtMaterialLayerRecord makeClearcoatLayer(const nr::resource::Material &material) noexcept
{
    auto factor = 0.0f;
    auto roughness = 0.0f;
    if (material.clearcoat.has_value())
    {
        factor = material.clearcoat->factor;
        roughness = material.clearcoat->roughnessFactor;
    }

    // Effective clearcoat normal scale is 0 when the clearcoat normal slot is unauthored, so the shader
    // can always sample texture id 0 (neutral) and have the decoded tangent normal collapse to (0,0,1).
    auto const clearcoatNormalScale =
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::clearcoatNormal)
            ? material.slot(nr::resource::MaterialTextureSlotSemantic::clearcoatNormal).scale
            : 0.0f;

    return RtMaterialLayerRecord{
        .layer = RtMaterialLayerFlag::clearcoat,
        .p0 = glm::vec4{factor, roughness, clearcoatNormalScale, 0.0f},
    };
}

[[nodiscard]] RtMaterialLayerRecord makeSheenLayer(const nr::resource::Material &material) noexcept
{
    auto color = glm::vec3{0.0f};
    auto roughness = 0.0f;
    if (material.sheen.has_value())
    {
        color = material.sheen->colorFactor;
        roughness = material.sheen->roughnessFactor;
    }

    return RtMaterialLayerRecord{
        .layer = RtMaterialLayerFlag::sheen,
        .p0 = glm::vec4{color, roughness},
    };
}

[[nodiscard]] RtMaterialLayerRecord makeTransmissionLayer(const nr::resource::Material &material) noexcept
{
    return RtMaterialLayerRecord{
        .layer = RtMaterialLayerFlag::transmission,
        .aux0 = static_cast<std::uint32_t>(transmissionMode(material)),
        .p0 = glm::vec4{effectiveTransmissionFactor(material), transmissionIor(material), 0.0f, 0.0f},
    };
}
} // namespace

[[nodiscard]] RtCompiledMaterial compileRtMaterial(const nr::resource::Material &material,
                                                   const SceneMaterialTextureIds &textureIds)
{
    auto compiled = RtCompiledMaterial{};
    auto const flags = rtFeatureFlags(material);
    auto const physicalLayerFlags = normalizeRtLayerFlags(material);

    auto const clearcoatFactor = material.clearcoat.has_value() ? material.clearcoat->factor : 0.0f;
    auto const clearcoatRoughness = material.clearcoat.has_value() ? material.clearcoat->roughnessFactor : 0.0f;
    auto const sheenMax =
        material.sheen.has_value()
            ? std::max({material.sheen->colorFactor.x, material.sheen->colorFactor.y, material.sheen->colorFactor.z})
            : 0.0f;

    // Effective base normal scale is 0 when the base normal slot is unauthored, so always-sampling the
    // neutral texture id 0 collapses the decoded tangent normal back to the geometric/interpolated normal.
    auto const baseNormalScale =
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::normal) ? material.core.normalScale : 0.0f;
    auto const anisotropySlotIndex =
        nr::resource::materialTextureSlotIndex(nr::resource::MaterialTextureSlotSemantic::anisotropy);
    auto const anisotropyTexturePresent =
        physicalLayerFlags != RtMaterialLayerFlag::none &&
        slotHasTexture(material, nr::resource::MaterialTextureSlotSemantic::anisotropy) &&
        textureIds[anisotropySlotIndex] != 0u;
    auto const anisotropyStrength =
        physicalLayerFlags != RtMaterialLayerFlag::none ? effectiveAnisotropyFactor(material) : 0.0f;
    auto layerFlags = physicalLayerFlags;
    if (anisotropyStrength > 0.0f)
    {
        layerFlags |= RtMaterialLayerFlag::anisotropicBaseLobe;
    }

    compiled.header = RtMaterialHeader{
        .layerFlags = layerFlags,
        .featureFlags = flags,
        .alphaMode = toRtAlphaMode(material.core.alphaMode),
        .alphaCutoff = material.core.alphaCutoff,
        .baseColorFactor = material.core.baseColorFactor,
        .emissiveAndMetallic = glm::vec4{material.core.emissiveFactor, material.core.metallicFactor},
        .roughnessNormalOcclusionAlpha =
            glm::vec4{
                material.core.roughnessFactor,
                baseNormalScale,
                material.core.occlusionStrength,
                material.core.alphaCutoff,
            },
        .transmissionClearcoatSheen =
            glm::vec4{
                0.0f,
                clearcoatFactor,
                clearcoatRoughness,
                sheenMax,
            },
        .anisotropy =
            glm::vec4{
                anisotropyStrength,
                physicalLayerFlags != RtMaterialLayerFlag::none ? anisotropyRotation(material) : 0.0f,
                anisotropyTexturePresent ? 1.0f : 0.0f,
                0.0f,
            },
    };

    // Lit materials write compact layer records in canonical bit order: base -> clearcoat -> sheen ->
    // transmission. Unlit (layerFlags == none) writes no records; layerCount stays 0.
    if (physicalLayerFlags != RtMaterialLayerFlag::none)
    {
        compiled.layers.push_back(makeBaseLayer(material));
        if (hasRtMaterialLayer(physicalLayerFlags, RtMaterialLayerFlag::clearcoat))
        {
            compiled.layers.push_back(makeClearcoatLayer(material));
        }
        if (hasRtMaterialLayer(physicalLayerFlags, RtMaterialLayerFlag::sheen))
        {
            compiled.layers.push_back(makeSheenLayer(material));
        }
        if (hasRtMaterialLayer(physicalLayerFlags, RtMaterialLayerFlag::transmission))
        {
            compiled.layers.push_back(makeTransmissionLayer(material));
        }
    }

    // Dense texture refs: one RtMaterialTextureRef per MaterialTextureSlot in slot order. Unauthored or
    // non-resident slots write texture id 0 (neutral default). Shaders index textureRefs[slot] directly
    // and never scan for a slot, so texture presence never influences CHS variant selection.
    compiled.textureRefs.reserve(material.textureSlots.size());
    auto slotIndices = std::views::iota(std::size_t{0}, material.textureSlots.size());
    std::ranges::for_each(slotIndices, [&](std::size_t slotIndex) {
        auto const &textureSlot = material.textureSlots[slotIndex];
        nr::nrAssert(textureSlot.uvSet <= 1u, [&] {
            return std::format("RT material '{}' texture slot {} uses unsupported UV set {}.", material.name, slotIndex,
                               textureSlot.uvSet);
        });
        compiled.textureRefs.push_back(RtMaterialTextureRef{
            .uvLinear = textureSlot.transform.linear,
            .uvOffset = textureSlot.transform.offset,
            .textureId = textureIds[slotIndex],
            .uvSet = textureSlot.uvSet,
        });
    });

    compiled.header.layerCount = static_cast<std::uint32_t>(compiled.layers.size());
    compiled.header.textureRefCount = static_cast<std::uint32_t>(compiled.textureRefs.size());
    return compiled;
}

[[nodiscard]] RtMaterialTable makeRtMaterialTable(
    std::span<const std::reference_wrapper<const RtCompiledMaterial>> materials)
{
    auto table = RtMaterialTable{};
    table.headers.reserve(materials.size());

    std::ranges::for_each(materials, [&](std::reference_wrapper<const RtCompiledMaterial> materialRef) {
        auto const &material = materialRef.get();
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
