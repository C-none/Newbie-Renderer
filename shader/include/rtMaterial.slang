implementing common;

public bool hasRtMaterialFeature(RtMaterialHeader material, RtMaterialFeatureFlag mask)
{
    return uint(material.featureFlags & mask) != 0u;
}

public RtMaterialTextureRef rtMaterialTextureRef(StructuredBuffer<RtMaterialTextureRef> textureRefs,
                                                 RtMaterialHeader material, MaterialTextureSlot slot)
{
    // Texture refs are dense: one entry per MaterialTextureSlot in slot order. Unauthored slots hold
    // texture id 0 (neutral default), so a direct index replaces the previous slot scan.
    return textureRefs[material.textureRefOffset + uint(slot)];
}
