implementing common;

public struct GltfMultiLayerMaterial : IMultiLayerMaterial
{
    public MaterialShadingResult evaluateDirect(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        StructuredBuffer<RtMaterialLayerRecord> layerRecords,
        RtMaterialHeader material,
        RtSurfaceData surface,
        MaterialDirectLightingInput lighting)
    {
        MaterialSample core = nrSampleCoreMaterial(textureRefs, material, surface);
        float3 orientedNormal = nrSafeNormalize(surface.normal, lighting.viewDirection);
        float normalScale = material.roughnessNormalOcclusionAlpha.y;
        float3 normal = nrApplyNormalMapSlot(
            textureRefs,
            material,
            kMaterialTextureSlotNormal,
            normalScale,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1);
        MaterialLayerStack layers = nrSampleMaterialLayers(textureRefs, layerRecords, material, surface, orientedNormal);

        float noL = saturate(dot(normal, lighting.lightDirection));
        float3 bsdf = nrEvaluateMetallicRoughnessBsdf(core, normal, lighting.viewDirection, lighting.lightDirection);
        bsdf += nrEvaluateSheenBsdf(layers, normal, lighting.viewDirection, lighting.lightDirection);
        bsdf = nrAttenuateBaseByClearcoat(bsdf, layers, lighting.viewDirection);

        MaterialShadingResult result;
        result.radiance =
            bsdf * lighting.lightRadiance * noL +
            nrEvaluateClearcoatDirect(layers, lighting.viewDirection, lighting.lightDirection, lighting.lightRadiance) +
            core.emissive;
        result.alpha = core.alpha;
        return result;
    }

    public float alpha(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        RtMaterialHeader material,
        RtSurfaceData surface)
    {
        return nrSampleCoreMaterial(textureRefs, material, surface).alpha;
    }
}
