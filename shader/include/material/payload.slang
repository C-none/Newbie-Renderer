implementing common;

public enum ResolvedMaterialScatterKind : uint
{
    diffuse = 0u,
    specular = 1u,
}

public struct ResolvedMaterialPayload
{
    public uint materialIndex = kRtMaterialFallbackIndex;
    public RtMaterialFeatureFlag featureFlags = RtMaterialFeatureFlag.none;
    public AlphaMode alphaMode = AlphaMode.opaque;
    public float alphaCutoff = 0.5f;
    public float hitT = 0.0f;
    public float3 position = float3(0.0f);
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 shadingNormal = float3(0.0f, 1.0f, 0.0f);
    public float4 baseColor = float4(1.0f);
    public float metallic = 0.0f;
    public float roughness = 1.0f;
    public float3 emissive = float3(0.0f);
    public float alpha = 1.0f;
    public MaterialLayerStack layers = {};
};

public struct ResolvedMaterialScatter
{
    public uint valid = 0u;
    public ResolvedMaterialScatterKind kind = ResolvedMaterialScatterKind.diffuse;
    public float3 direction = float3(0.0f, 1.0f, 0.0f);
    public float3 weight = float3(0.0f);
    public float pdf = 0.0f;
};

public MaterialSample materialSampleFromResolvedPayload(ResolvedMaterialPayload payload)
{
    MaterialSample sample;
    sample.baseColor = payload.baseColor;
    sample.metallic = payload.metallic;
    sample.roughness = payload.roughness;
    sample.emissive = payload.emissive;
    sample.alpha = payload.alpha;
    return sample;
}

public ResolvedMaterialPayload resolveRtMaterialPayload(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    StructuredBuffer<RtMaterialLayerRecord> layerRecords,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 viewDirection,
    float hitT,
    uint materialIndex)
{
    MaterialSample core = sampleCoreMaterial(textureRefs, material, surface);
    float3 orientedNormal = safeNormalize(surface.normal, viewDirection);
    float normalScale = material.roughnessNormalOcclusionAlpha.y;
    float3 shadingNormal = applyNormalMapSlot(
        textureRefs,
        material,
        MaterialTextureSlot.normal,
        normalScale,
        orientedNormal,
        surface.tangent,
        surface.tangentSign,
        surface.texCoord0,
        surface.texCoord1);

    ResolvedMaterialPayload payload;
    payload.materialIndex = materialIndex;
    payload.featureFlags = material.featureFlags;
    payload.alphaMode = material.alphaMode;
    payload.alphaCutoff = material.alphaCutoff;
    payload.hitT = hitT;
    payload.position = surface.position;
    payload.geometryNormal = orientedNormal;
    payload.shadingNormal = shadingNormal;
    payload.baseColor = core.baseColor;
    payload.metallic = core.metallic;
    payload.roughness = core.roughness;
    payload.emissive = core.emissive;
    payload.alpha = core.alpha;
    payload.layers = sampleMaterialLayers(textureRefs, layerRecords, material, surface, orientedNormal);
    return payload;
}

public bool resolvedMaterialHasFeature(ResolvedMaterialPayload payload, RtMaterialFeatureFlag mask)
{
    return uint(payload.featureFlags & mask) != 0u;
}

public bool resolvedMaterialFailsAlphaMask(ResolvedMaterialPayload payload)
{
    return resolvedMaterialHasFeature(payload, RtMaterialFeatureFlag.alphaMask) && payload.alpha < payload.alphaCutoff;
}

public MaterialShadingResult evaluateResolvedMaterialDirect(
    ResolvedMaterialPayload payload,
    MaterialDirectLightingInput lighting)
{
    MaterialSample core = materialSampleFromResolvedPayload(payload);
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
    float noL = saturate(dot(normal, lighting.lightDirection));

    float3 bsdf = evaluateMetallicRoughnessBsdf(core, normal, lighting.viewDirection, lighting.lightDirection);
    bsdf += evaluateSheenBsdf(payload.layers, normal, lighting.viewDirection, lighting.lightDirection);
    bsdf = attenuateBaseByClearcoat(bsdf, payload.layers, lighting.viewDirection);

    MaterialShadingResult result;
    result.radiance =
        bsdf * lighting.lightRadiance * noL +
        evaluateClearcoatDirect(payload.layers, lighting.viewDirection, lighting.lightDirection, lighting.lightRadiance);
    result.alpha = payload.alpha;
    return result;
}

public float materialPayloadLuminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

public void materialPayloadBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    tangent = orthogonalTangent(normal);
    bitangent = safeNormalize(cross(normal, tangent), float3(0.0f, 0.0f, 1.0f));
}

public float3 materialPayloadToWorld(float3 localDirection, float3 normal)
{
    float3 tangent;
    float3 bitangent;
    materialPayloadBasis(normal, tangent, bitangent);
    return safeNormalize(
        tangent * localDirection.x +
        bitangent * localDirection.y +
        normal * localDirection.z,
        normal);
}

public float3 sampleMaterialPayloadCosineHemisphere(float2 randomValues, float3 normal)
{
    float phi = 2.0f * kMaterialPi * randomValues.x;
    float radius = sqrt(saturate(randomValues.y));
    float z = sqrt(saturate(1.0f - randomValues.y));
    return materialPayloadToWorld(float3(cos(phi) * radius, sin(phi) * radius, z), normal);
}

public float3 sampleMaterialPayloadGgxHalfVector(float2 randomValues, float roughness, float3 normal)
{
    float alpha = max(roughness * roughness, kMaterialMinRoughness * kMaterialMinRoughness);
    float alpha2 = alpha * alpha;
    float phi = 2.0f * kMaterialPi * randomValues.x;
    float cosTheta = sqrt((1.0f - randomValues.y) / max(1.0f + (alpha2 - 1.0f) * randomValues.y, 1.0e-6f));
    float sinTheta = sqrt(saturate(1.0f - cosTheta * cosTheta));
    return materialPayloadToWorld(float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta), normal);
}

public float materialPayloadDiffusePdf(float3 normal, float3 lightDirection)
{
    return saturate(dot(normal, lightDirection)) * kMaterialInvPi;
}

public float materialPayloadGgxPdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
{
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
    float noL = saturate(dot(normal, lightDirection));
    float noV = saturate(dot(normal, viewDirection));
    if (noL <= kMaterialMinCosTheta || noV <= kMaterialMinCosTheta)
    {
        return 0.0f;
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float noH = saturate(dot(normal, halfVector));
    float voH = saturate(dot(viewDirection, halfVector));
    float alpha = payload.roughness * payload.roughness;
    float distribution = ggxDistribution(alpha, noH);
    return distribution * noH / max(4.0f * voH, 1.0e-6f);
}

public float3 evaluateResolvedMaterialBsdf(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    MaterialSample core = materialSampleFromResolvedPayload(payload);
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
    float3 bsdf = evaluateMetallicRoughnessBsdf(core, normal, viewDirection, lightDirection);
    bsdf += evaluateSheenBsdf(payload.layers, normal, viewDirection, lightDirection);
    return attenuateBaseByClearcoat(bsdf, payload.layers, viewDirection);
}

public ResolvedMaterialScatter sampleResolvedMaterialScatter(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 randomValues)
{
    ResolvedMaterialScatter scatter = {};
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);

    float3 diffuseReflectance = payload.baseColor.rgb * (1.0f - payload.metallic);
    float3 specularReflectance = lerp(float3(0.04f), payload.baseColor.rgb, payload.metallic);
    float diffuseWeight = max(materialPayloadLuminance(diffuseReflectance), 0.0f);
    float specularWeight = max(materialPayloadLuminance(specularReflectance), 0.0f);
    float specularProbability = clamp(
        specularWeight / max(diffuseWeight + specularWeight, 1.0e-6f),
        0.05f,
        0.95f);

    if (randomValues.x < specularProbability)
    {
        float2 sampleValues = float2(randomValues.y, randomValues.z);
        float3 halfVector = sampleMaterialPayloadGgxHalfVector(sampleValues, payload.roughness, normal);
        scatter.direction = safeNormalize(reflect(-viewDirection, halfVector), normal);
        scatter.kind = ResolvedMaterialScatterKind.specular;
    }
    else
    {
        float2 sampleValues = float2(randomValues.y, randomValues.z);
        scatter.direction = sampleMaterialPayloadCosineHemisphere(sampleValues, normal);
        scatter.kind = ResolvedMaterialScatterKind.diffuse;
    }

    float noL = saturate(dot(normal, scatter.direction));
    if (noL <= kMaterialMinCosTheta)
    {
        return scatter;
    }

    float diffusePdf = materialPayloadDiffusePdf(normal, scatter.direction);
    float specularPdf = materialPayloadGgxPdf(payload, viewDirection, scatter.direction);
    float pdf = diffusePdf * (1.0f - specularProbability) + specularPdf * specularProbability;
    if (pdf <= 1.0e-6f)
    {
        return scatter;
    }

    float3 bsdf = evaluateResolvedMaterialBsdf(payload, viewDirection, scatter.direction);
    scatter.valid = any(bsdf > float3(0.0f)) ? 1u : 0u;
    scatter.pdf = pdf;
    scatter.weight = bsdf * noL / pdf;
    return scatter;
}
