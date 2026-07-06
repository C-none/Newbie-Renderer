implementing common;

public static const uint kPathTracingRtHitShadingModelGltfPbr = 0u;
public static const uint kPathTracingRtHitAlphaOpaqueLike = 0u;
public static const uint kPathTracingRtHitAlphaMask = 1u;
public static const uint kPathTracingKnownRtMaterialFeatureMask =
    kRtMaterialFeatureClearcoat |
    kRtMaterialFeatureSheen |
    kRtMaterialFeatureTransmission |
    kRtMaterialFeatureAlphaMask |
    kRtMaterialFeatureAlphaBlend |
    kRtMaterialFeatureDoubleSided |
    kRtMaterialFeatureEmissive |
    kRtMaterialFeatureUnsupportedAnisotropy;

public struct PathTracingClosestHitInput
{
    public bool geometryValid;
    public RtMaterialHeader material;
    public RtSurfaceData surface;
    public float3 viewDirection;
    public float hitT;
    public uint materialIndex;
};

public struct PathTracingClosestHitResult
{
    public uint hit;
    public ResolvedMaterialPayload material;
};

public interface ICHS
{
    PathTracingClosestHitResult handleClosestHit(PathTracingClosestHitInput input);
}

public bool pathTracingHitPolicySupported(uint policyFeatureFlags, uint policyAlphaPolicy, uint policyShadingModel)
{
    return policyShadingModel == kPathTracingRtHitShadingModelGltfPbr &&
        (policyAlphaPolicy == kPathTracingRtHitAlphaOpaqueLike || policyAlphaPolicy == kPathTracingRtHitAlphaMask) &&
        (policyFeatureFlags & ~kPathTracingKnownRtMaterialFeatureMask) == 0u;
}

public PathTracingClosestHitInput defaultPathTracingClosestHitInput()
{
    PathTracingClosestHitInput input;
    input.geometryValid = false;
    input.material.abiVersion = kRtMaterialAbiVersion;
    input.material.featureFlags = 0u;
    input.material.layerOffset = 0u;
    input.material.layerCount = 0u;
    input.material.textureRefOffset = 0u;
    input.material.textureRefCount = 0u;
    input.material.alphaMode = kAlphaModeOpaque;
    input.material.alphaCutoff = 0.5f;
    input.material.baseColorFactor = float4(1.0f);
    input.material.emissiveAndMetallic = float4(0.0f);
    input.material.roughnessNormalOcclusionAlpha = float4(1.0f, 1.0f, 1.0f, 1.0f);
    input.material.transmissionClearcoatSheen = float4(0.0f);
    input.surface.position = float3(0.0f);
    input.surface.normal = float3(0.0f, 1.0f, 0.0f);
    input.surface.tangent = float3(1.0f, 0.0f, 0.0f);
    input.surface.tangentSign = 1.0f;
    input.surface.texCoord0 = float2(0.0f);
    input.surface.texCoord1 = float2(0.0f);
    input.surface.color0 = float4(1.0f);
    input.viewDirection = float3(0.0f, 1.0f, 0.0f);
    input.hitT = 0.0f;
    input.materialIndex = kRtMaterialFallbackIndex;
    return input;
}

public PathTracingClosestHitResult defaultPathTracingClosestHitResult()
{
    PathTracingClosestHitResult result;
    result.hit = 0u;
    result.material = defaultResolvedMaterialPayload();
    return result;
}

public struct DefaultLitCHS<let FeatureMask : uint, let AlphaPolicy : uint> : ICHS
{
    public PathTracingClosestHitResult handleClosestHit(PathTracingClosestHitInput input)
    {
        PathTracingClosestHitResult result = defaultPathTracingClosestHitResult();
        if (!pathTracingHitPolicySupported(FeatureMask, AlphaPolicy, kPathTracingRtHitShadingModelGltfPbr) ||
            !input.geometryValid)
        {
            return result;
        }

        result.hit = 1u;
        result.material = resolveRtMaterialPayload(
            rtMaterialTextureRefs,
            rtMaterialLayers,
            input.material,
            input.surface,
            input.viewDirection,
            input.hitT,
            input.materialIndex);
        return result;
    }
}

public extern struct CHS : ICHS;
