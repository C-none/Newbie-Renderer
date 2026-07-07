implementing common;

public static const uint kRtMaterialAbiVersion = 1u;
public static const uint kRtMaterialFallbackIndex = 0u;

[Flags]
public enum RtGeometryFlag : uint
{
    none = 0u,
    indexed = 1u << 0u,
}

[Flags]
public enum RtInstanceFlag : uint
{
    none = 0u,
}

[Flags]
public enum RtMaterialFeatureFlag : uint
{
    none = 0u,
    clearcoat = 1u << 0u,
    sheen = 1u << 1u,
    transmission = 1u << 2u,
    alphaMask = 1u << 3u,
    alphaBlend = 1u << 4u,
    doubleSided = 1u << 5u,
    emissive = 1u << 6u,
    unsupportedAnisotropy = 1u << 31u,
}

public enum RtMaterialLayerKind : uint
{
    baseSurface = 0u,
    clearcoat = 1u,
    sheen = 2u,
    transmission = 3u,
}

public struct RtMaterialHeader
{
    public uint abiVersion = kRtMaterialAbiVersion;
    public RtMaterialFeatureFlag featureFlags = RtMaterialFeatureFlag.none;
    public uint layerOffset = 0u;
    public uint layerCount = 0u;
    public uint textureRefOffset = 0u;
    public uint textureRefCount = 0u;
    public AlphaMode alphaMode = AlphaMode.opaque;
    public float alphaCutoff = 0.5f;
    public float4 baseColorFactor = float4(1.0f);
    public float4 emissiveAndMetallic = float4(0.0f);
    public float4 roughnessNormalOcclusionAlpha = float4(1.0f, 1.0f, 1.0f, 1.0f);
    public float4 transmissionClearcoatSheen = float4(0.0f);
};

public struct RtMaterialLayerRecord
{
    public RtMaterialLayerKind kind = RtMaterialLayerKind.baseSurface;
    public MaterialTextureSlotFlag textureMask = MaterialTextureSlotFlag.none;
    public uint aux0 = 0u;
    public uint aux1 = 0u;
    public float4 p0 = float4(0.0f);
    public float4 p1 = float4(0.0f);
};

public struct RtMaterialTextureRef
{
    public MaterialTextureSlot slot = MaterialTextureSlot.baseColor;
    public uint textureId = 0u;
    public uint uvSet = 0u;
    public MaterialTextureSlotFlag flags = MaterialTextureSlotFlag.none;
};

public struct RtGeometryMetadata
{
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint geometryIndex = 0u;
    public uint primitiveOffset = 0u;
    public uint firstVertex = 0u;
    public uint primitiveCount = 0u;
    public RtGeometryFlag flags = RtGeometryFlag.none;
    public uint reserved0 = 0u;
    public uint reserved1 = 0u;
};

public struct RtInstanceMetadata
{
    public uint geometryOffset = 0u;
    public uint geometryCount = 0u;
    public uint stableInstanceId = 0u;
    public RtInstanceFlag flags = RtInstanceFlag.none;
    public uint vertexBase = 0u;
    public uint indexBase = 0u;
    public uint vertexStride = 0u;
    public uint reserved0 = 0u;
};

