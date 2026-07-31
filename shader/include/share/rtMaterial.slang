implementing common;

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
public enum RtMaterialLayerFlag : uint
{
    none         = 0u,
    baseSurface  = 1u << 0u,
    clearcoat    = 1u << 1u,
    sheen        = 1u << 2u,
    transmission = 1u << 3u,
}

public enum RtTransmissionMode : uint
{
    thin = 0u,
    volume = 1u,
}

public enum RtBaseLobeVariant : uint
{
    isotropic = 0u,
    anisotropic = 1u,
}

[Flags]
public enum RtMaterialFeatureFlag : uint
{
    none = 0u,
    alphaMask = 1u << 0u,
    alphaBlend = 1u << 1u,
    doubleSided = 1u << 2u,
    emissive = 1u << 3u,
    volumeBoundary = 1u << 4u,
}

public struct RtMaterialHeader
{
    public RtMaterialLayerFlag layerFlags = RtMaterialLayerFlag.none;
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
    // x is reserved. Transmission factor, IOR, and mode have one source of truth in the layer record.
    public float4 transmissionClearcoatSheen = float4(0.0f);
    // strength, counter-clockwise rotation in radians, usable-texture presence, reserved.
    public float4 anisotropy = float4(0.0f);
};

public struct RtMaterialLayerRecord
{
    public RtMaterialLayerFlag layer = RtMaterialLayerFlag.baseSurface;
    public uint aux0 = 0u;
    public uint aux1 = 0u;
    public float4 p0 = float4(0.0f);
    public float4 p1 = float4(0.0f);
};

public struct RtMaterialTextureRef
{
    // Row-major 2x2 affine linear term: (m00, m01, m10, m11).
    public float4 uvLinear = float4(1.0f, 0.0f, 0.0f, 1.0f);
    public float2 uvOffset = float2(0.0f);
    public uint textureId = 0u;
    public uint uvSet = 0u;
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

