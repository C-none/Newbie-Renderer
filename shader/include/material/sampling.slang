implementing common;

public float4 sampleMaterialTexture(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot,
    float2 texCoord0,
    float2 texCoord1,
    float4 fallback)
{
    // Always sample. Unauthored slots hold texture id 0 (neutral white RGBA(1,1,1,1)), so a missing
    // texture yields a neutral multiplier without a texture-presence branch. `fallback` is retained for
    // source compatibility and stays neutral now that id 0 provides the default.
    RtMaterialTextureRef textureRef = rtMaterialTextureRef(textureRefs, material, slot);
    return gSceneTextures[textureRef.textureId].SampleLevel(
        materialTextureUv(textureRef, texCoord0, texCoord1),
        0.0f);
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
    // Always sample the normal slot. Unauthored normals hold texture id 0 (neutral white) and their
    // effective normalScale is 0, so the decoded tangent normal collapses to (0,0,1) and the shading
    // normal equals the incoming geometric/interpolated normal without a texture-presence branch.
    RtMaterialTextureRef normalRef = rtMaterialTextureRef(textureRefs, material, normalSlot);
    float3 tangentFallback = orthogonalTangent(normal);
    float3 tangent = safeNormalize(worldTangent - normal * dot(normal, worldTangent), tangentFallback);
    float3 bitangent = safeNormalize(cross(normal, tangent) * tangentSign, cross(normal, tangentFallback));
    float2 normalUv = materialTextureUv(normalRef, texCoord0, texCoord1);
    float3 tangentNormal =
        gSceneTextures[normalRef.textureId].SampleLevel(normalUv, 0.0f).xyz * 2.0f - 1.0f;
    tangentNormal.xy *= normalScale;
    return safeNormalize(
        tangent * tangentNormal.x +
        bitangent * tangentNormal.y +
        normal * tangentNormal.z,
        normal);
}

public void decodeMaterialAnisotropy(
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 shadingNormal,
    float3 anisotropySample,
    out float3 anisotropyTangent,
    out float anisotropyStrength)
{
    float2 direction = anisotropySample.rg * 2.0f - 1.0f;
    float directionLengthSquared = dot(direction, direction);
    direction = directionLengthSquared > 1.0e-8f
        ? direction * rsqrt(directionLengthSquared)
        : float2(1.0f, 0.0f);

    float rotationSin;
    float rotationCos;
    sincos(material.anisotropy.y, rotationSin, rotationCos);
    direction = float2(
        rotationCos * direction.x - rotationSin * direction.y,
        rotationSin * direction.x + rotationCos * direction.y);

    float3 normal = safeNormalize(shadingNormal, surface.normal);
    float3 tangentFallback = orthogonalTangent(normal);
    float3 tangent0 = safeNormalize(
        surface.tangent - normal * dot(normal, surface.tangent),
        tangentFallback);
    float3 bitangent0 = cross(normal, tangent0) * surface.tangentSign;
    anisotropyTangent = safeNormalize(
        tangent0 * direction.x + bitangent0 * direction.y,
        tangent0);
    anisotropyStrength =
        saturate(material.anisotropy.x) *
        saturate(anisotropySample.b);
}

public void sampleMaterialAnisotropy(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 shadingNormal,
    out float3 anisotropyTangent,
    out float anisotropyStrength)
{
    float3 anisotropySample = float3(1.0f, 0.5f, 1.0f);
    if (material.anisotropy.z > 0.5f)
    {
        RtMaterialTextureRef anisotropyRef =
            rtMaterialTextureRef(textureRefs, material, MaterialTextureSlot.anisotropy);
        anisotropySample = gSceneTextures[anisotropyRef.textureId].SampleLevel(
            materialTextureUv(anisotropyRef, surface.texCoord0, surface.texCoord1),
            0.0f).rgb;
    }

    decodeMaterialAnisotropy(
        material,
        surface,
        shadingNormal,
        anisotropySample,
        anisotropyTangent,
        anisotropyStrength);
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

// Canonical layer-record parser. Records are compact in bit order (base -> clearcoat -> sheen ->
// transmission); base surface data lives in the header, so optional records start at layerOffset + 1.
// Each optional layer reads its record and textures inside an `if (LayerFlags & flag)` static block, so
// link specialization removes the record read and texture samples for inactive layers.
public MaterialLayerStack sampleMaterialLayersVariant<let LayerFlags : RtMaterialLayerFlag>(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 orientedNormal)
{
    MaterialLayerStack sample = {};
    sample.clearcoatNormal = orientedNormal;

    uint layerRecordIndex = material.layerOffset + 1u;

    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        RtMaterialLayerRecord clearcoatLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
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
            clearcoatLayer.p0.z,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1);
    }

    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        RtMaterialLayerRecord sheenLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
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

    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        RtMaterialLayerRecord transmissionLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
        float transmissionTexture = sampleMaterialTexture(
            textureRefs,
            material,
            MaterialTextureSlot.transmission,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f)).r;
        sample.transmission = saturate(transmissionLayer.p0.x * transmissionTexture);
        sample.transmissionMode = transmissionLayer.aux0 == uint(RtTransmissionMode.volume)
            ? RtTransmissionMode.volume
            : RtTransmissionMode.thin;
        sample.transmissionIor = transmissionLayer.p0.y;
    }

    return sample;
}
