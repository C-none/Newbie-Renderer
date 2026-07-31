implementing common;

// The material variant mask combines the four physical layer bits with the anisotropic base-lobe
// specialization bit. The CPU validates the "non-zero implies baseSurface" invariant.
public static const RtMaterialLayerFlag kKnownRtMaterialVariantMask =
    RtMaterialLayerFlag.baseSurface |
    RtMaterialLayerFlag.clearcoat |
    RtMaterialLayerFlag.sheen |
    RtMaterialLayerFlag.transmission |
    RtMaterialLayerFlag.anisotropicBaseLobe;

public static const RtMaterialFeatureFlag kKnownRtMaterialFeatureMask =
    RtMaterialFeatureFlag.alphaMask |
    RtMaterialFeatureFlag.alphaBlend |
    RtMaterialFeatureFlag.doubleSided |
    RtMaterialFeatureFlag.emissive |
    RtMaterialFeatureFlag.volumeBoundary;

public struct ClosestHitInput
{
    public bool geometryValid = false;
    public RtMaterialHeader material = {};
    public RtSurfaceData surface = {};
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public bool frontFace = true;
    public float4 scatterRandomValues = float4(0.0f);
    public RandomSequence materialFilterSequence = {};
    public float incidentMediumIor = 1.0f;
    public float exteriorMediumIor = 1.0f;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint instanceMetadataIndex = 0xffffffffu;
    public uint geometryIndex = 0xffffffffu;
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
                input.materialIndex,
                input.materialFilterSequence);
            result.material.geometryNormal = input.geometryNormal;
            result.material.frontFace = input.frontFace;
            result.material.incidentMediumIor = input.incidentMediumIor;
            result.material.exteriorMediumIor = input.exteriorMediumIor;
            result.material.instanceMetadataIndex = input.instanceMetadataIndex;
            result.material.geometryIndex = input.geometryIndex;
            return result;
        }

        result.material = resolveLitMaterialPayloadVariant<LayerFlags>(
            rtMaterialTextureRefs,
            rtMaterialLayers,
            input.material,
            input.surface,
            input.viewDirection,
            input.materialIndex,
            input.materialFilterSequence);
        result.material.geometryNormal = input.geometryNormal;
        result.material.frontFace = input.frontFace;
        result.material.incidentMediumIor = input.incidentMediumIor;
        result.material.exteriorMediumIor = input.exteriorMediumIor;
        result.material.instanceMetadataIndex = input.instanceMetadataIndex;
        result.material.geometryIndex = input.geometryIndex;
        result.scatter = sampleResolvedMaterialScatterVariant<LayerFlags>(
            result.material,
            input.viewDirection,
            input.scatterRandomValues);
        return result;
    }
}

public extern struct CHS : ICHS;
