implementing common;

public struct ClosestHitInput
{
    public bool geometryValid = false;
    public RtMaterialHeader material = {};
    public RtSurfaceData surface = RtSurfaceData();
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public bool frontFace = true;
    public float4 scatterRandomValues = float4(0.0f);
    public RandomSequence materialFilterSequence = RandomSequence();
    public float incidentMediumIor = 1.0f;
    public float exteriorMediumIor = 1.0f;
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint instanceMetadataIndex = 0xffffffffu;
    public uint geometryIndex = 0xffffffffu;

    public __init()
    {
    }

    public __init(bool geometryIsValid, RtMaterialHeader hitMaterial, RtSurfaceData hitSurface,
                  float3 rayViewDirection, float3 hitGeometryNormal, bool hitFrontFace, uint hitMaterialIndex,
                  uint hitInstanceMetadataIndex, uint hitGeometryIndex, float4 hitScatterRandomValues,
                  RandomSequence hitMaterialFilterSequence, float hitIncidentMediumIor, float hitExteriorMediumIor)
    {
        geometryValid = geometryIsValid;
        material = hitMaterial;
        surface = hitSurface;
        viewDirection = rayViewDirection;
        geometryNormal = hitGeometryNormal;
        frontFace = hitFrontFace;
        scatterRandomValues = hitScatterRandomValues;
        materialFilterSequence = hitMaterialFilterSequence;
        incidentMediumIor = hitIncidentMediumIor;
        exteriorMediumIor = hitExteriorMediumIor;
        materialIndex = hitMaterialIndex;
        instanceMetadataIndex = hitInstanceMetadataIndex;
        geometryIndex = hitGeometryIndex;
    }
};

public struct ClosestHitResult
{
    public uint hit = 0u;
    public ResolvedMaterialPayload material = ResolvedMaterialPayload();
    public ResolvedMaterialScatter scatter = ResolvedMaterialScatter();

    public __init()
    {
    }

    public __init(ResolvedMaterialPayload resolvedMaterial, ClosestHitInput input)
    {
        hit = 1u;
        material = resolvedMaterial;
        material.geometryNormal = input.geometryNormal;
        material.frontFace = input.frontFace;
        material.incidentMediumIor = input.incidentMediumIor;
        material.exteriorMediumIor = input.exteriorMediumIor;
        material.instanceMetadataIndex = input.instanceMetadataIndex;
        material.geometryIndex = input.geometryIndex;
    }

};

public interface ICHS
{
    ClosestHitResult handleClosestHit(ClosestHitInput input);
}

public struct MaterialCHS<let LayerFlags : RtMaterialLayerFlag> : ICHS
{
    public ClosestHitResult handleClosestHit(ClosestHitInput input)
    {
        if (!input.geometryValid)
        {
            return ClosestHitResult();
        }

        if (LayerFlags == RtMaterialLayerFlag.none)
        {
            // Unlit (KHR_materials_unlit): base color coverage only, no PBR resolve and no scatter.
            // Raygen treats layerFlags == none as terminal surface radiance.
            ResolvedMaterialPayload material =
                resolveUnlitMaterialPayload(rtMaterialTextureRefs, input.material, input.surface, input.viewDirection,
                                            input.materialIndex, input.materialFilterSequence);
            return ClosestHitResult(material, input);
        }

        ResolvedMaterialPayload material = resolveLitMaterialPayloadVariant<LayerFlags>(
            rtMaterialTextureRefs, rtMaterialLayers, input.material, input.surface, input.viewDirection,
            input.materialIndex, input.materialFilterSequence);
        ClosestHitResult result = ClosestHitResult(material, input);
        // Scatter sampling consumes the completed hit frame and medium state.
        result.scatter =
            sampleResolvedMaterialScatterVariant<LayerFlags>(result.material, input.viewDirection, input.scatterRandomValues);
        return result;
    }
}

public extern struct CHS : ICHS;
