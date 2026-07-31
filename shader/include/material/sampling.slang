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

public float4 sampleMaterialTextureVariant(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot slot,
    float2 texCoord0,
    float2 texCoord1,
    float4 fallback,
    float filterRandomValue)
{
    RtMaterialTextureRef textureRef = rtMaterialTextureRef(textureRefs, material, slot);
    float2 uv = materialTextureUv(textureRef, texCoord0, texCoord1);
    if (kEnableFilterAfterShading)
    {
        uint width;
        uint height;
        gSceneTextures[textureRef.textureId].GetDimensions(width, height);
        uv = stochasticBilinearTexelCenterUv(
            uv,
            uint2(width, height),
            filterRandomValue);
    }

    return gSceneTextures[textureRef.textureId].SampleLevel(uv, 0.0f);
}

public float3 applyNormalMapSlotVariant(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    MaterialTextureSlot normalSlot,
    float normalScale,
    float3 normal,
    float3 worldTangent,
    float tangentSign,
    float2 texCoord0,
    float2 texCoord1,
    float filterRandomValue)
{
    // Always sample the normal slot. Unauthored normals hold texture id 0 (neutral white) and their
    // effective normalScale is 0, so the decoded tangent normal collapses to (0,0,1) and the shading
    // normal equals the incoming geometric/interpolated normal without a texture-presence branch.
    float3 tangentFallback = orthogonalTangent(normal);
    float3 tangent = safeNormalize(worldTangent - normal * dot(normal, worldTangent), tangentFallback);
    float3 bitangent = safeNormalize(cross(normal, tangent) * tangentSign, cross(normal, tangentFallback));
    float3 tangentNormal =
        sampleMaterialTextureVariant(
            textureRefs,
            material,
            normalSlot,
            texCoord0,
            texCoord1,
            float4(1.0f),
            filterRandomValue).xyz * 2.0f - 1.0f;
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

public void sampleMaterialAnisotropyVariant(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 shadingNormal,
    float filterRandomValue,
    out float3 anisotropyTangent,
    out float anisotropyStrength)
{
    float3 anisotropySample = float3(1.0f, 0.5f, 1.0f);
    if (material.anisotropy.z > 0.5f)
    {
        anisotropySample = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.anisotropy,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f, 0.5f, 1.0f, 1.0f),
            filterRandomValue).rgb;
    }

    decodeMaterialAnisotropy(
        material,
        surface,
        shadingNormal,
        anisotropySample,
        anisotropyTangent,
        anisotropyStrength);
}

public MaterialSample sampleCoreMaterialVariant(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 filterRandomValues)
{
    float4 baseColorTexture = sampleMaterialTextureVariant(
        textureRefs,
        material,
        MaterialTextureSlot.baseColor,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f),
        filterRandomValues.x);
    float4 metallicRoughnessTexture = sampleMaterialTextureVariant(
        textureRefs,
        material,
        MaterialTextureSlot.metallicRoughness,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f),
        filterRandomValues.y);
    float4 emissiveTexture = sampleMaterialTextureVariant(
        textureRefs,
        material,
        MaterialTextureSlot.emissive,
        surface.texCoord0,
        surface.texCoord1,
        float4(1.0f),
        filterRandomValues.z);

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
public MaterialLayerStack sampleMaterialLayersVariant<
    let LayerFlags : RtMaterialLayerFlag>(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 orientedNormal,
    float3 clearcoatFilterRandomValues,
    inout RandomSequence filterSequence)
{
    MaterialLayerStack sample = {};
    sample.clearcoatNormal = orientedNormal;

    uint layerRecordIndex = material.layerOffset + 1u;

    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        RtMaterialLayerRecord clearcoatLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
        float clearcoatTexture = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoat,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f),
            clearcoatFilterRandomValues.x).r;
        float clearcoatRoughnessTexture = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoatRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f),
            clearcoatFilterRandomValues.y).g;
        sample.clearcoat = saturate(clearcoatLayer.p0.x * clearcoatTexture);
        sample.clearcoatRoughness = clamp(clearcoatLayer.p0.y * clearcoatRoughnessTexture, kMaterialMinRoughness, 1.0f);
        sample.clearcoatNormal = applyNormalMapSlotVariant(
            textureRefs,
            material,
            MaterialTextureSlot.clearcoatNormal,
            clearcoatLayer.p0.z,
            orientedNormal,
            surface.tangent,
            surface.tangentSign,
            surface.texCoord0,
            surface.texCoord1,
            clearcoatFilterRandomValues.z);
    }

    // packet 1 is dead after the clearcoat block. Generate packet 2 only now
    // so no more than one float4 texture-filter packet is live at a time.
    float4 layerFilterRandomValues = filterSequence.rand4();
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        RtMaterialLayerRecord sheenLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
        float4 sheenColorTexture = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.sheenColor,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f),
            layerFilterRandomValues.x);
        float sheenRoughnessTexture = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.sheenRoughness,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f),
            layerFilterRandomValues.y).a;
        sample.sheenColor = saturate(sheenLayer.p0.rgb * sheenColorTexture.rgb);
        sample.sheenRoughness = clamp(sheenLayer.p0.w * sheenRoughnessTexture, kMaterialMinRoughness, 1.0f);
    }

    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        RtMaterialLayerRecord transmissionLayer = layerRecords[layerRecordIndex];
        layerRecordIndex += 1u;
        float transmissionTexture = sampleMaterialTextureVariant(
            textureRefs,
            material,
            MaterialTextureSlot.transmission,
            surface.texCoord0,
            surface.texCoord1,
            float4(1.0f),
            layerFilterRandomValues.z).r;
        sample.transmission = saturate(transmissionLayer.p0.x * transmissionTexture);
        sample.transmissionMode = transmissionLayer.aux0 == uint(RtTransmissionMode.volume)
            ? RtTransmissionMode.volume
            : RtTransmissionMode.thin;
        sample.transmissionIor = transmissionLayer.p0.y;
    }

    return sample;
}
