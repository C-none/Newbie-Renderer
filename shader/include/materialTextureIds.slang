implementing common;

public uint unpackMaterialTextureId(uint packedPair, uint lane)
{
    return (packedPair >> ((lane & 1u) * 16u)) & 0xFFFFu;
}

public uint materialTextureIdPair(uint4 pairs0, uint2 pairs1, uint pairIndex)
{
    if (pairIndex == 0u)
    {
        return pairs0.x;
    }
    if (pairIndex == 1u)
    {
        return pairs0.y;
    }
    if (pairIndex == 2u)
    {
        return pairs0.z;
    }
    if (pairIndex == 3u)
    {
        return pairs0.w;
    }
    if (pairIndex == 4u)
    {
        return pairs1.x;
    }
    return pairs1.y;
}

public uint materialTextureId(uint4 pairs0, uint2 pairs1, MaterialTextureSlot slot)
{
    uint slotIndex = uint(slot);
    return unpackMaterialTextureId(materialTextureIdPair(pairs0, pairs1, slotIndex >> 1u), slotIndex);
}
