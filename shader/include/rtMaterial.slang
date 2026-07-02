implementing common;

public static const uint kRtMaterialAbiVersion = 1u;
public static const uint kRtMaterialFallbackIndex = 0u;
public static const uint kRtGeometryFlagIndexed = 1u << 0u;

public static const uint kRtMaterialFeatureClearcoat = 1u << 0u;
public static const uint kRtMaterialFeatureSheen = 1u << 1u;
public static const uint kRtMaterialFeatureTransmission = 1u << 2u;
public static const uint kRtMaterialFeatureAlphaMask = 1u << 3u;
public static const uint kRtMaterialFeatureAlphaBlend = 1u << 4u;
public static const uint kRtMaterialFeatureDoubleSided = 1u << 5u;
public static const uint kRtMaterialFeatureEmissive = 1u << 6u;
public static const uint kRtMaterialFeatureUnsupportedAnisotropy = 1u << 16u;

public static const uint kRtMaterialLayerBaseSurface = 0u;
public static const uint kRtMaterialLayerClearcoat = 1u;
public static const uint kRtMaterialLayerSheen = 2u;
public static const uint kRtMaterialLayerTransmission = 3u;

public struct RtMaterialHeader
{
    public uint abiVersion;
    public uint featureFlags;
    public uint layerOffset;
    public uint layerCount;
    public uint textureRefOffset;
    public uint textureRefCount;
    public uint alphaMode;
    public float alphaCutoff;
    public float4 baseColorFactor;
    public float4 emissiveAndMetallic;
    public float4 roughnessNormalOcclusionAlpha;
    public float4 transmissionClearcoatSheen;
};

public struct RtMaterialLayerRecord
{
    public uint kind;
    public uint textureMask;
    public uint aux0;
    public uint aux1;
    public float4 p0;
    public float4 p1;
};

public struct RtMaterialTextureRef
{
    public uint slot;
    public uint textureId;
    public uint uvSet;
    public uint flags;
};

public struct RtGeometryMetadata
{
    public uint materialIndex;
    public uint geometryIndex;
    public uint primitiveOffset;
    public uint firstVertex;
    public uint primitiveCount;
    public uint flags;
    public uint reserved0;
    public uint reserved1;
};

public struct RtInstanceMetadata
{
    public uint geometryOffset;
    public uint geometryCount;
    public uint stableInstanceId;
    public uint flags;
    public uint vertexBase;
    public uint indexBase;
    public uint vertexStride;
    public uint reserved0;
};

public bool hasRtMaterialFeature(RtMaterialHeader material, uint mask)
{
    return (material.featureFlags & mask) != 0u;
}

public RtMaterialTextureRef rtMaterialTextureRef(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    uint slot)
{
    for (uint index = 0u; index < material.textureRefCount; ++index)
    {
        RtMaterialTextureRef textureRef = textureRefs[material.textureRefOffset + index];
        if (textureRef.slot == slot)
        {
            return textureRef;
        }
    }

    RtMaterialTextureRef fallback;
    fallback.slot = slot;
    fallback.textureId = 0u;
    fallback.uvSet = 0u;
    fallback.flags = 0u;
    return fallback;
}

public uint rtMaterialTextureId(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    uint slot)
{
    return rtMaterialTextureRef(textureRefs, material, slot).textureId;
}

public float3 rtMaterialFeaturePreviewColor(RtMaterialHeader material)
{
    float3 color = max(material.baseColorFactor.rgb, float3(0.02f));

    if (hasRtMaterialFeature(material, kRtMaterialFeatureTransmission))
    {
        color = lerp(color, float3(0.35f, 0.85f, 1.0f), 0.45f);
    }
    if (hasRtMaterialFeature(material, kRtMaterialFeatureSheen))
    {
        color = lerp(color, float3(1.0f, 0.35f, 0.85f), 0.35f);
    }
    if (hasRtMaterialFeature(material, kRtMaterialFeatureClearcoat))
    {
        color = lerp(color, float3(0.78f, 0.95f, 1.0f), 0.25f);
    }
    if (hasRtMaterialFeature(material, kRtMaterialFeatureEmissive))
    {
        color += min(material.emissiveAndMetallic.rgb, float3(1.0f)) * 0.5f;
    }
    if (hasRtMaterialFeature(material, kRtMaterialFeatureUnsupportedAnisotropy))
    {
        color = lerp(color, float3(1.0f, 0.15f, 0.05f), 0.5f);
    }

    return saturate(color);
}
