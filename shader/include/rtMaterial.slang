implementing common;

public bool hasRtMaterialFeature(RtMaterialHeader material, RtMaterialFeatureFlag mask)
{
    return uint(material.featureFlags & mask) != 0u;
}

public bool hasRtMaterialLayer(RtMaterialHeader material, RtMaterialLayerFlag mask)
{
    return uint(material.layerFlags & mask) != 0u;
}

public RtMaterialTextureRef rtMaterialTextureRef(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot)
{
    // Texture refs are dense: one entry per MaterialTextureSlot in slot order. Unauthored slots hold
    // texture id 0 (neutral default), so a direct index replaces the previous slot scan.
    return textureRefs[material.textureRefOffset + uint(slot)];
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

    if (hasRtMaterialLayer(material, RtMaterialLayerFlag.transmission))
    {
        color = lerp(color, float3(0.35f, 0.85f, 1.0f), 0.45f);
    }
    if (hasRtMaterialLayer(material, RtMaterialLayerFlag.sheen))
    {
        color = lerp(color, float3(1.0f, 0.35f, 0.85f), 0.35f);
    }
    if (hasRtMaterialLayer(material, RtMaterialLayerFlag.clearcoat))
    {
        color = lerp(color, float3(0.78f, 0.95f, 1.0f), 0.25f);
    }
    if (hasRtMaterialFeature(material, RtMaterialFeatureFlag.emissive))
    {
        color += min(material.emissiveAndMetallic.rgb, float3(1.0f)) * 0.5f;
    }
    return saturate(color);
}
