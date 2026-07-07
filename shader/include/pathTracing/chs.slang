implementing common;

public enum PathTracingRtHitShadingModel : uint
{
    gltfPbr = 0u,
}

public enum PathTracingRtHitAlphaPolicy : uint
{
    opaqueLike = 0u,
    alphaMask = 1u,
}

public static const RtMaterialFeatureFlag kPathTracingKnownRtMaterialFeatureMask =
    RtMaterialFeatureFlag.clearcoat |
    RtMaterialFeatureFlag.sheen |
    RtMaterialFeatureFlag.transmission |
    RtMaterialFeatureFlag.alphaMask |
    RtMaterialFeatureFlag.alphaBlend |
    RtMaterialFeatureFlag.doubleSided |
    RtMaterialFeatureFlag.emissive |
    RtMaterialFeatureFlag.unsupportedAnisotropy;

public struct PathTracingClosestHitInput
{
    public bool geometryValid = false;
    public RtMaterialHeader material = {};
    public RtSurfaceData surface = {};
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float hitT = 0.0f;
    public uint materialIndex = kRtMaterialFallbackIndex;
};

public struct PathTracingClosestHitResult
{
    public uint hit = 0u;
    public ResolvedMaterialPayload material = {};
};

public interface ICHS
{
    PathTracingClosestHitResult handleClosestHit(PathTracingClosestHitInput input);
}

public bool pathTracingHitPolicySupported(
    RtMaterialFeatureFlag policyFeatureFlags,
    PathTracingRtHitAlphaPolicy policyAlphaPolicy,
    PathTracingRtHitShadingModel policyShadingModel)
{
    return policyShadingModel == PathTracingRtHitShadingModel.gltfPbr &&
        (policyAlphaPolicy == PathTracingRtHitAlphaPolicy.opaqueLike ||
            policyAlphaPolicy == PathTracingRtHitAlphaPolicy.alphaMask) &&
        (uint(policyFeatureFlags) & ~uint(kPathTracingKnownRtMaterialFeatureMask)) == 0u;
}

public struct DefaultLitCHS<let FeatureMask : RtMaterialFeatureFlag, let AlphaPolicy : PathTracingRtHitAlphaPolicy> : ICHS
{
    public PathTracingClosestHitResult handleClosestHit(PathTracingClosestHitInput input)
    {
        PathTracingClosestHitResult result = {};
        if (!pathTracingHitPolicySupported(FeatureMask, AlphaPolicy, PathTracingRtHitShadingModel.gltfPbr) ||
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
