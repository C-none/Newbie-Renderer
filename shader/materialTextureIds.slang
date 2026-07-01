implementing common;

public static const uint kMaterialTextureSlotBaseColor = 0u;
public static const uint kMaterialTextureSlotNormal = 1u;
public static const uint kMaterialTextureSlotMetallicRoughness = 2u;
public static const uint kMaterialTextureSlotOcclusion = 3u;
public static const uint kMaterialTextureSlotEmissive = 4u;
public static const uint kMaterialTextureSlotClearcoat = 5u;
public static const uint kMaterialTextureSlotClearcoatRoughness = 6u;
public static const uint kMaterialTextureSlotClearcoatNormal = 7u;
public static const uint kMaterialTextureSlotSheenColor = 8u;
public static const uint kMaterialTextureSlotSheenRoughness = 9u;
public static const uint kMaterialTextureSlotTransmission = 10u;
public static const uint kMaterialTextureSlotAnisotropy = 11u;
public static const uint kMaterialTextureSlotCount = 12u;

public uint unpackMaterialTextureId(uint packedPair, uint lane)
{
    return (packedPair >> ((lane & 1u) * 16u)) & 0xFFFFu;
}

public uint materialTextureIdPair(uint4 pairs0, uint2 pairs1, uint pairIndex)
{
    if (pairIndex == 0u) { return pairs0.x; }
    if (pairIndex == 1u) { return pairs0.y; }
    if (pairIndex == 2u) { return pairs0.z; }
    if (pairIndex == 3u) { return pairs0.w; }
    if (pairIndex == 4u) { return pairs1.x; }
    return pairs1.y;
}

public uint materialTextureId(uint4 pairs0, uint2 pairs1, uint slot)
{
    return unpackMaterialTextureId(materialTextureIdPair(pairs0, pairs1, slot >> 1u), slot);
}
