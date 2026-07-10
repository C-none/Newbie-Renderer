implementing common;

// Layer mask validates the 4 layer bits (and the "non-zero implies baseSurface" invariant on the CPU
// side). Feature mask validates only non-layer runtime behavior. The two are intentionally disjoint.
public static const RtMaterialLayerFlag kKnownRtMaterialLayerMask =
    RtMaterialLayerFlag.baseSurface |
    RtMaterialLayerFlag.clearcoat |
    RtMaterialLayerFlag.sheen |
    RtMaterialLayerFlag.transmission;

public static const RtMaterialFeatureFlag kKnownRtMaterialFeatureMask =
    RtMaterialFeatureFlag.alphaMask |
    RtMaterialFeatureFlag.alphaBlend |
    RtMaterialFeatureFlag.doubleSided |
    RtMaterialFeatureFlag.emissive |
    RtMaterialFeatureFlag.unsupportedAnisotropy;

public struct ClosestHitInput
{
    public bool geometryValid = false;
    public RtMaterialHeader material = {};
    public RtSurfaceData surface = {};
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 scatterRandomValues = float3(0.0f);
    public float hitT = 0.0f;
    public uint materialIndex = kRtMaterialFallbackIndex;
};

public struct ClosestHitResult
{
    public uint hit = 0u;
    public ResolvedMaterialPayload material = {};
    public ResolvedMaterialScatter scatter = {};
};

public interface ICHS
{
    ClosestHitResult handleClosestHit(ClosestHitInput input);
}

public struct MaterialCHS<let LayerFlags : RtMaterialLayerFlag> : ICHS
{
    public ClosestHitResult handleClosestHit(ClosestHitInput input)
    {
        ClosestHitResult result = {};
        if (!input.geometryValid)
        {
            return result;
        }

        result.hit = 1u;
        if (LayerFlags == RtMaterialLayerFlag.none)
        {
            // Unlit (KHR_materials_unlit): base color coverage only, no PBR resolve and no scatter.
            // Raygen treats layerFlags == none as terminal surface radiance.
            result.material = resolveUnlitMaterialPayload(
                rtMaterialTextureRefs,
                input.material,
                input.surface,
                input.viewDirection,
                input.hitT,
                input.materialIndex);
            return result;
        }

        result.material = resolveLitMaterialPayloadVariant<LayerFlags>(
            rtMaterialTextureRefs,
            rtMaterialLayers,
            input.material,
            input.surface,
            input.viewDirection,
            input.hitT,
            input.materialIndex);
        result.scatter = sampleResolvedMaterialScatterVariant<LayerFlags>(
            result.material,
            input.viewDirection,
            input.scatterRandomValues);
        return result;
    }
}

public extern struct CHS : ICHS;
