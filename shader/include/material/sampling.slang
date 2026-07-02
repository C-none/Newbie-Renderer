implementing common;

public float4 nrSampleMaterialTexture(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    uint slot,
    float2 texCoord0,
    float2 texCoord1,
    float4 fallback)
{
    RtMaterialTextureRef textureRef = rtMaterialTextureRef(textureRefs, material, slot);
    if (textureRef.textureId == 0u)
    {
        return fallback;
    }

    return gSceneTextures[textureRef.textureId].SampleLevel(nrMaterialUv(textureRef.uvSet, texCoord0, texCoord1), 0.0f);
}

public float3 nrApplyNormalMapSlot(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    uint normalSlot,
    float normalScale,
    float3 normal,
    float3 worldTangent,
    float tangentSign,
    float2 texCoord0,
    float2 texCoord1)
{
    RtMaterialTextureRef normalRef = rtMaterialTextureRef(textureRefs, material, normalSlot);
    if (normalRef.textureId == 0u)
    {
        return normal;
    }

    float3 tangentFallback = nrOrthogonalTangent(normal);
    float3 tangent = nrSafeNormalize(worldTangent - normal * dot(normal, worldTangent), tangentFallback);
    float3 bitangent = nrSafeNormalize(cross(normal, tangent) * tangentSign, cross(normal, tangentFallback));
    float2 normalUv = nrMaterialUv(normalRef.uvSet, texCoord0, texCoord1);
    float3 tangentNormal = gSceneTextures[normalRef.textureId].SampleLevel(normalUv, 0.0f).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalScale;
    return nrSafeNormalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        normal * tangentNormal.z,
        normal);
}

public RtMaterialLayerRecord nrDefaultMaterialLayer(uint kind)
{
    RtMaterialLayerRecord layer;
    layer.kind = kind;
    layer.textureMask = 0u;
    layer.aux0 = 0u;
    layer.aux1 = 0u;
    layer.p0 = float4(0.0f);
    layer.p1 = float4(0.0f);
    return layer;
}

public bool nrTryFindMaterialLayer(
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    uint kind,
    out RtMaterialLayerRecord result)
{
    for (uint layerIndex = 0u; layerIndex < material.layerCount; ++layerIndex)
    {
        RtMaterialLayerRecord layer = layerRecords[material.layerOffset + layerIndex];
        if (layer.kind == kind)
        {
            result = layer;
            return true;
        }
    }

    result = nrDefaultMaterialLayer(kind);
    return false;
}

public MaterialSample nrSampleCoreMaterial(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface)
{
    float4 baseColorTexture = nrSampleMaterialTexture(
        textureRefs,
        material,
        kMaterialTextureSlotBaseColor,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f));
    float4 metallicRoughnessTexture = nrSampleMaterialTexture(
        textureRefs,
        material,
        kMaterialTextureSlotMetallicRoughness,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f));
    float4 emissiveTexture = nrSampleMaterialTexture(
        textureRefs,
        material,
        kMaterialTextureSlotEmissive,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f));

    MaterialSample sample;
    sample.baseColor = material.baseColorFactor * baseColorTexture * surface.color0;
    sample.alpha = saturate(sample.baseColor.a);
    sample.metallic = saturate(material.emissiveAndMetallic.w * metallicRoughnessTexture.b);
    sample.roughness = clamp(material.roughnessNormalOcclusionAlpha.x * metallicRoughnessTexture.g, kMaterialMinRoughness, 1.0f);
    sample.emissive = material.emissiveAndMetallic.rgb * emissiveTexture.rgb;
    return sample;
}

public MaterialLayerStack nrSampleMaterialLayers(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 orientedNormal)
{
    MaterialLayerStack sample;
    sample.clearcoat = 0.0f;
    sample.clearcoatRoughness = 0.0f;
    sample.clearcoatNormal = orientedNormal;
    sample.sheenColor = float3(0.0f);
    sample.sheenRoughness = 0.0f;

    RtMaterialLayerRecord clearcoatLayer;
    if (nrTryFindMaterialLayer(layerRecords, material, kRtMaterialLayerClearcoat, clearcoatLayer))
    {
        float clearcoatTexture = nrSampleMaterialTexture(
            textureRefs,
            material,
            kMaterialTextureSlotClearcoat,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).r;
        float clearcoatRoughnessTexture = nrSampleMaterialTexture(
            textureRefs,
            material,
            kMaterialTextureSlotClearcoatRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).g;
        sample.clearcoat = saturate(clearcoatLayer.p0.x * clearcoatTexture);
        sample.clearcoatRoughness = clamp(clearcoatLayer.p0.y * clearcoatRoughnessTexture, kMaterialMinRoughness, 1.0f);
        sample.clearcoatNormal = nrApplyNormalMapSlot(
            textureRefs,
            material,
            kMaterialTextureSlotClearcoatNormal,
            1.0f,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1);
    }

    RtMaterialLayerRecord sheenLayer;
    if (nrTryFindMaterialLayer(layerRecords, material, kRtMaterialLayerSheen, sheenLayer))
    {
        float4 sheenColorTexture = nrSampleMaterialTexture(
            textureRefs,
            material,
            kMaterialTextureSlotSheenColor,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f));
        float sheenRoughnessTexture = nrSampleMaterialTexture(
            textureRefs,
            material,
            kMaterialTextureSlotSheenRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).a;
        sample.sheenColor = saturate(sheenLayer.p0.rgb * sheenColorTexture.rgb);
        sample.sheenRoughness = clamp(sheenLayer.p0.w * sheenRoughnessTexture, kMaterialMinRoughness, 1.0f);
    }

    return sample;
}
