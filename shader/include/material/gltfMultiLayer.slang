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
        MaterialSample core = sampleCoreMaterial(textureRefs, material, surface);
        float3 orientedNormal = safeNormalize(surface.normal, lighting.viewDirection);
        float normalScale = material.roughnessNormalOcclusionAlpha.y;
        float3 normal = applyNormalMapSlot(
            textureRefs,
            material,
            MaterialTextureSlot.normal,
            normalScale,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1);
        MaterialLayerStack layers = sampleMaterialLayers(textureRefs, layerRecords, material, surface, orientedNormal);

        float noL = saturate(dot(normal, lighting.lightDirection));
        float3 bsdf = evaluateMetallicRoughnessBsdf(core, normal, lighting.viewDirection, lighting.lightDirection);
        bsdf += evaluateSheenBsdf(layers, normal, lighting.viewDirection, lighting.lightDirection);
        bsdf = attenuateBaseByClearcoat(bsdf, layers, lighting.viewDirection);

        MaterialShadingResult result;
        result.radiance =
            bsdf * lighting.lightRadiance * noL +
            evaluateClearcoatDirect(layers, lighting.viewDirection, lighting.lightDirection, lighting.lightRadiance);
        result.alpha = core.alpha;
        return result;
    }

    public float3 emissive(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        RtMaterialHeader material,
        RtSurfaceData surface)
    {
        return sampleCoreMaterial(textureRefs, material, surface).emissive;
    }

    public float alpha(
        StructuredBuffer<RtMaterialTextureRef> textureRefs,
        RtMaterialHeader material,
        RtSurfaceData surface)
    {
        return sampleCoreMaterial(textureRefs, material, surface).alpha;
    }
}
