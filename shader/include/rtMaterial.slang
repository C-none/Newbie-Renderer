implementing common;

public bool hasRtMaterialFeature(RtMaterialHeader material, RtMaterialFeatureFlag mask)
{
    return uint(material.featureFlags & mask) != 0u;
}

public RtMaterialTextureRef rtMaterialTextureRef(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot)
{
    for (uint index = 0u; index < material.textureRefCount; ++index)
    {
        RtMaterialTextureRef textureRef = textureRefs[material.textureRefOffset + index];
        if (textureRef.slot == slot)
        {
            return textureRef;
        }
    }

    RtMaterialTextureRef fallback = {};
    fallback.slot = slot;
    return fallback;
}

public uint rtMaterialTextureId(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot)
{
    return rtMaterialTextureRef(textureRefs, material, slot).textureId;
}

public float3 rtMaterialFeaturePreviewColor(RtMaterialHeader material)
{
    float3 color = max(material.baseColorFactor.rgb, float3(0.02f));

    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.transmission))
    {
        color = lerp(color, float3(0.35f, 0.85f, 1.0f), 0.45f);
    }
    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.sheen))
    {
        color = lerp(color, float3(1.0f, 0.35f, 0.85f), 0.35f);
    }
    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.clearcoat))
    {
        color = lerp(color, float3(0.78f, 0.95f, 1.0f), 0.25f);
    }
    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.emissive))
    {
        color += min(material.emissiveAndMetallic.rgb, float3(1.0f)) * 0.5f;
    }
    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.unsupportedAnisotropy))
    {
        color = lerp(color, float3(1.0f, 0.15f, 0.05f), 0.5f);
    }

    return saturate(color);
}
