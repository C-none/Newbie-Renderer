implementing common;

public float4 sampleMaterialTexture(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot,
    float2 texCoord0,
    float2 texCoord1,
    float4 fallback)
{
    RtMaterialTextureRef textureRef = rtMaterialTextureRef(textureRefs, material, slot);
    if (textureRef.textureId == 0u)
    {
        return fallback;
    }

    return gSceneTextures[textureRef.textureId].SampleLevel(materialUv(textureRef.uvSet, texCoord0, texCoord1), 0.0f);
}

public float3 applyNormalMapSlot(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot normalSlot,
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

    float3 tangentFallback = orthogonalTangent(normal);
    float3 tangent = safeNormalize(worldTangent - normal * dot(normal, worldTangent), tangentFallback);
    float3 bitangent = safeNormalize(cross(normal, tangent) * tangentSign, cross(normal, tangentFallback));
    float2 normalUv = materialUv(normalRef.uvSet, texCoord0, texCoord1);
    float3 tangentNormal = gSceneTextures[normalRef.textureId].SampleLevel(normalUv, 0.0f).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalScale;
    return safeNormalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        normal * tangentNormal.z,
        normal);
}

public bool tryFindMaterialLayer(
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtMaterialLayerKind kind,
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

    result = {};
    result.kind = kind;
    return false;
}

public MaterialSample sampleCoreMaterial(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface)
{
    float4 baseColorTexture = sampleMaterialTexture(
        textureRefs,
        material,
        MaterialTextureSlot.baseColor,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f));
    float4 metallicRoughnessTexture = sampleMaterialTexture(
        textureRefs,
        material,
        MaterialTextureSlot.metallicRoughness,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f));
    float4 emissiveTexture = sampleMaterialTexture(
        textureRefs,
        material,
        MaterialTextureSlot.emissive,
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

public MaterialLayerStack sampleMaterialLayers(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 orientedNormal)
{
    MaterialLayerStack sample = {};
    sample.clearcoatNormal = orientedNormal;

    RtMaterialLayerRecord clearcoatLayer;
    if (tryFindMaterialLayer(layerRecords, material, RtMaterialLayerKind.clearcoat, clearcoatLayer))
    {
        float clearcoatTexture = sampleMaterialTexture(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoat,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).r;
        float clearcoatRoughnessTexture = sampleMaterialTexture(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoatRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).g;
        sample.clearcoat = saturate(clearcoatLayer.p0.x * clearcoatTexture);
        sample.clearcoatRoughness = clamp(clearcoatLayer.p0.y * clearcoatRoughnessTexture, kMaterialMinRoughness, 1.0f);
        sample.clearcoatNormal = applyNormalMapSlot(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoatNormal,
            1.0f,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1);
    }

    RtMaterialLayerRecord sheenLayer;
    if (tryFindMaterialLayer(layerRecords, material, RtMaterialLayerKind.sheen, sheenLayer))
    {
        float4 sheenColorTexture = sampleMaterialTexture(
            textureRefs,
            material,
            MaterialTextureSlot.sheenColor,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f));
        float sheenRoughnessTexture = sampleMaterialTexture(
            textureRefs,
            material,
            MaterialTextureSlot.sheenRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).a;
        sample.sheenColor = saturate(sheenLayer.p0.rgb * sheenColorTexture.rgb);
        sample.sheenRoughness = clamp(sheenLayer.p0.w * sheenRoughnessTexture, kMaterialMinRoughness, 1.0f);
    }

    return sample;
}
