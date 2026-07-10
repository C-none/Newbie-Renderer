implementing common;

public enum ResolvedMaterialScatterKind : uint
{
    diffuse = 0u,
    specular = 1u,
    sheen = 2u,
    clearcoat = 3u,
    transmission = 4u,
}

public struct ResolvedMaterialPayload
{
    public uint materialIndex = kRtMaterialFallbackIndex;
    public RtMaterialLayerFlag layerFlags = RtMaterialLayerFlag.none;
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
    public uint delta = 0u;
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

// glTF base color coverage: base color factor x base color texture x vertex color. Shared by unlit
// terminal shading and alpha coverage; never touches normal/metallic-roughness/emissive/layer data.
public float4 resolveBaseColorAndAlpha(
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
    return material.baseColorFactor * baseColorTexture * surface.color0;
}

// Minimal alpha coverage for the alpha-mask any-hit. lit and unlit share the same glTF coverage rule.
public float resolveAlphaCoverage(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface)
{
    return saturate(resolveBaseColorAndAlpha(textureRefs, material, surface).a);
}

// Unlit resolver (KHR_materials_unlit): only base color, vertex color and alpha state. Metallic,
// roughness, normal, occlusion, emissive and every PBR extension layer are intentionally ignored.
public ResolvedMaterialPayload resolveUnlitMaterialPayload(
    StructuredBuffer<RtMaterialTextureRef> textureRefs,
    RtMaterialHeader material,
    RtSurfaceData surface,
    float3 viewDirection,
    float hitT,
    uint materialIndex)
{
    float4 baseColor = resolveBaseColorAndAlpha(textureRefs, material, surface);
    float3 orientedNormal = safeNormalize(surface.normal, viewDirection);

    ResolvedMaterialPayload payload;
    payload.materialIndex = materialIndex;
    payload.layerFlags = RtMaterialLayerFlag.none;
    payload.featureFlags = material.featureFlags;
    payload.alphaMode = material.alphaMode;
    payload.alphaCutoff = material.alphaCutoff;
    payload.hitT = hitT;
    payload.position = surface.position;
    payload.geometryNormal = orientedNormal;
    payload.shadingNormal = orientedNormal;
    payload.baseColor = baseColor;
    payload.metallic = 0.0f;
    payload.roughness = 1.0f;
    payload.emissive = float3(0.0f);
    payload.alpha = saturate(baseColor.a);
    return payload;
}

// Lit resolver. baseSurface is always resolved from the header plus base textures; optional layer data
// is parsed canonically inside sampleMaterialLayersVariant static blocks so inactive layers contribute
// no record reads or texture samples.
public ResolvedMaterialPayload resolveLitMaterialPayloadVariant<let LayerFlags : RtMaterialLayerFlag>(
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
    payload.layerFlags = LayerFlags;
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
    payload.layers = sampleMaterialLayersVariant<LayerFlags>(textureRefs, layerRecords, material, surface, orientedNormal);
    return payload;
}

// Direct lighting specialized by concrete LayerFlags. Sheen and clearcoat contributions live in
// `if (LayerFlags & flag)` static blocks so raygen's per-case dispatch executes only active-layer math.
public MaterialShadingResult evaluateResolvedMaterialDirectVariant<let LayerFlags : RtMaterialLayerFlag>(
    ResolvedMaterialPayload payload,
    MaterialDirectLightingInput lighting)
{
    MaterialSample core = materialSampleFromResolvedPayload(payload);
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
    float noL = saturate(dot(normal, lighting.lightDirection));

    float3 bsdf = evaluateMetallicRoughnessBsdf(core, normal, lighting.viewDirection, lighting.lightDirection);
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        bsdf += evaluateSheenBsdf(payload.layers, normal, lighting.viewDirection, lighting.lightDirection);
    }

    float3 radiance;
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        bsdf = attenuateBaseByClearcoat(bsdf, payload.layers, lighting.viewDirection);
        radiance = bsdf * lighting.lightRadiance * noL +
            evaluateClearcoatDirect(payload.layers, lighting.viewDirection, lighting.lightDirection, lighting.lightRadiance);
    }
    else
    {
        radiance = bsdf * lighting.lightRadiance * noL;
    }

    MaterialShadingResult result;
    result.radiance = radiance;
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

public float materialPayloadClearcoatGgxPdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
{
    float3 normal = safeNormalize(payload.layers.clearcoatNormal, payload.geometryNormal);
    float noL = saturate(dot(normal, lightDirection));
    float noV = saturate(dot(normal, viewDirection));
    if (noL <= kMaterialMinCosTheta || noV <= kMaterialMinCosTheta)
    {
        return 0.0f;
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float noH = saturate(dot(normal, halfVector));
    float voH = saturate(dot(viewDirection, halfVector));
    float alpha = payload.layers.clearcoatRoughness * payload.layers.clearcoatRoughness;
    float distribution = ggxDistribution(alpha, noH);
    return distribution * noH / max(4.0f * voH, 1.0e-6f);
}

public static const RtMaterialLayerFlag kResolvedMaterialLayerVariantMask =
    RtMaterialLayerFlag.baseSurface |
    RtMaterialLayerFlag.clearcoat |
    RtMaterialLayerFlag.sheen |
    RtMaterialLayerFlag.transmission;

public interface IBsdfLobe
{
    float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection);
    float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection);
    float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection);
    ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues);
    bool isDelta();
}

public struct DiffuseBsdfLobe : IBsdfLobe
{
    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        return max(materialPayloadLuminance(payload.baseColor.rgb * (1.0f - payload.metallic)), 0.0f);
    }

    public float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        MaterialSample core = materialSampleFromResolvedPayload(payload);
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        return evaluateMetallicRoughnessDiffuse(core, normal, viewDirection, lightDirection);
    }

    public float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        return materialPayloadDiffusePdf(normal, lightDirection);
    }

    public ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        scatter.direction = sampleMaterialPayloadCosineHemisphere(randomValues, normal);
        scatter.kind = ResolvedMaterialScatterKind.diffuse;
        scatter.pdf = pdf(payload, viewDirection, scatter.direction);
        scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
        return scatter;
    }

    public bool isDelta()
    {
        return false;
    }
};

public struct SpecularGgxBsdfLobe : IBsdfLobe
{
    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        MaterialSample core = materialSampleFromResolvedPayload(payload);
        return max(materialPayloadLuminance(metallicRoughnessF0(core)), 0.0f);
    }

    public float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        MaterialSample core = materialSampleFromResolvedPayload(payload);
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        return evaluateMetallicRoughnessSpecular(core, normal, viewDirection, lightDirection);
    }

    public float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        return materialPayloadGgxPdf(payload, viewDirection, lightDirection);
    }

    public ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        float3 halfVector = sampleMaterialPayloadGgxHalfVector(randomValues, payload.roughness, normal);
        scatter.direction = safeNormalize(reflect(-viewDirection, halfVector), normal);
        scatter.kind = ResolvedMaterialScatterKind.specular;
        scatter.pdf = pdf(payload, viewDirection, scatter.direction);
        scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
        return scatter;
    }

    public bool isDelta()
    {
        return false;
    }
};

public struct SheenBsdfLobe : IBsdfLobe
{
    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        return max(materialPayloadLuminance(payload.layers.sheenColor), 0.0f);
    }

    public float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        return evaluateSheenBsdf(payload.layers, normal, viewDirection, lightDirection);
    }

    public float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        return materialPayloadDiffusePdf(normal, lightDirection);
    }

    public ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        scatter.direction = sampleMaterialPayloadCosineHemisphere(randomValues, normal);
        scatter.kind = ResolvedMaterialScatterKind.sheen;
        scatter.pdf = pdf(payload, viewDirection, scatter.direction);
        scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
        return scatter;
    }

    public bool isDelta()
    {
        return false;
    }
};

public struct ClearcoatBsdfLobe : IBsdfLobe
{
    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        return max(payload.layers.clearcoat, 0.0f);
    }

    public float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        float coatSpecular = evaluateClearcoatSpecular(
            payload.layers.clearcoatRoughness,
            payload.layers.clearcoatNormal,
            viewDirection,
            lightDirection) * payload.layers.clearcoat;
        return float3(coatSpecular);
    }

    public float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        return materialPayloadClearcoatGgxPdf(payload, viewDirection, lightDirection);
    }

    public ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        float3 normal = safeNormalize(payload.layers.clearcoatNormal, payload.geometryNormal);
        float3 halfVector = sampleMaterialPayloadGgxHalfVector(randomValues, payload.layers.clearcoatRoughness, normal);
        scatter.direction = safeNormalize(reflect(-viewDirection, halfVector), normal);
        scatter.kind = ResolvedMaterialScatterKind.clearcoat;
        scatter.pdf = pdf(payload, viewDirection, scatter.direction);
        scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
        return scatter;
    }

    public bool isDelta()
    {
        return false;
    }
};

public struct TransmissionBsdfLobe : IBsdfLobe
{
    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        return 0.0f;
    }

    public float3 evaluate(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        return float3(0.0f);
    }

    public float pdf(ResolvedMaterialPayload payload, float3 viewDirection, float3 lightDirection)
    {
        return 0.0f;
    }

    public ResolvedMaterialScatter sample(ResolvedMaterialPayload payload, float3 viewDirection, float2 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        scatter.kind = ResolvedMaterialScatterKind.transmission;
        scatter.delta = 1u;
        return scatter;
    }

    public bool isDelta()
    {
        return true;
    }
};

public float3 evaluateResolvedMaterialBsdfVariant<let LayerFlags : RtMaterialLayerFlag>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    float3 baseBsdf = float3(0.0f);
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        DiffuseBsdfLobe diffuse;
        SpecularGgxBsdfLobe specular;
        baseBsdf =
            diffuse.evaluate(payload, viewDirection, lightDirection) +
            specular.evaluate(payload, viewDirection, lightDirection);
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        baseBsdf += sheen.evaluate(payload, viewDirection, lightDirection);
    }

    float3 result = baseBsdf;
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        result = attenuateBaseByClearcoat(baseBsdf, payload.layers, viewDirection) +
            clearcoat.evaluate(payload, viewDirection, lightDirection);
    }

    return result;
}

public float resolvedMaterialCombinedPdfVariant<let LayerFlags : RtMaterialLayerFlag>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    float totalWeight = 0.0f;
    float weightedPdf = 0.0f;

    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        DiffuseBsdfLobe diffuse;
        SpecularGgxBsdfLobe specular;
        float diffuseWeight = diffuse.selectionWeight(payload, viewDirection);
        float specularWeight = specular.selectionWeight(payload, viewDirection);
        totalWeight += diffuseWeight + specularWeight;
        weightedPdf += diffuseWeight * diffuse.pdf(payload, viewDirection, lightDirection);
        weightedPdf += specularWeight * specular.pdf(payload, viewDirection, lightDirection);
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        float sheenWeight = sheen.selectionWeight(payload, viewDirection);
        totalWeight += sheenWeight;
        weightedPdf += sheenWeight * sheen.pdf(payload, viewDirection, lightDirection);
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        float clearcoatWeight = clearcoat.selectionWeight(payload, viewDirection);
        totalWeight += clearcoatWeight;
        weightedPdf += clearcoatWeight * clearcoat.pdf(payload, viewDirection, lightDirection);
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        TransmissionBsdfLobe transmission;
        float transmissionWeight = transmission.selectionWeight(payload, viewDirection);
        totalWeight += transmissionWeight;
        weightedPdf += transmissionWeight * transmission.pdf(payload, viewDirection, lightDirection);
    }

    if (totalWeight <= 0.0f)
    {
        return 0.0f;
    }

    return weightedPdf / totalWeight;
}

public ResolvedMaterialScatter sampleResolvedMaterialScatterVariant<let LayerFlags : RtMaterialLayerFlag>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 randomValues)
{
    ResolvedMaterialScatter scatter = {};
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);

    // Pass 1: accumulate per-lobe selection weights. Each optional lobe's weight computation lives in its
    // own static block, so inactive-layer selection math is removed by link specialization.
    float diffuseWeight = 0.0f;
    float specularWeight = 0.0f;
    float sheenWeight = 0.0f;
    float clearcoatWeight = 0.0f;
    float transmissionWeight = 0.0f;
    float totalWeight = 0.0f;
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        DiffuseBsdfLobe diffuse;
        SpecularGgxBsdfLobe specular;
        diffuseWeight = diffuse.selectionWeight(payload, viewDirection);
        specularWeight = specular.selectionWeight(payload, viewDirection);
        totalWeight += diffuseWeight + specularWeight;
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        sheenWeight = sheen.selectionWeight(payload, viewDirection);
        totalWeight += sheenWeight;
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        clearcoatWeight = clearcoat.selectionWeight(payload, viewDirection);
        totalWeight += clearcoatWeight;
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        TransmissionBsdfLobe transmission;
        transmissionWeight = transmission.selectionWeight(payload, viewDirection);
        totalWeight += transmissionWeight;
    }

    if (totalWeight <= 1.0e-6f)
    {
        return scatter;
    }

    // Pass 2: pick one lobe by cumulative threshold and sample only that lobe. Each lobe's sample() call
    // is inside its static block; `sampled` keeps the earliest match and stops later lobes from writing.
    float2 sampleValues = float2(randomValues.y, randomValues.z);
    float selectedWeight = saturate(randomValues.x) * totalWeight;
    float threshold = 0.0f;
    bool sampled = false;
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        DiffuseBsdfLobe diffuse;
        SpecularGgxBsdfLobe specular;
        threshold += diffuseWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = diffuse.sample(payload, viewDirection, sampleValues);
            sampled = true;
        }
        threshold += specularWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = specular.sample(payload, viewDirection, sampleValues);
            sampled = true;
        }
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        threshold += sheenWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = sheen.sample(payload, viewDirection, sampleValues);
            sampled = true;
        }
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        threshold += clearcoatWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = clearcoat.sample(payload, viewDirection, sampleValues);
            sampled = true;
        }
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
    {
        TransmissionBsdfLobe transmission;
        threshold += transmissionWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = transmission.sample(payload, viewDirection, sampleValues);
            sampled = true;
        }
    }

    if (!sampled)
    {
        return scatter;
    }

    float noL = saturate(dot(normal, scatter.direction));
    if (noL <= kMaterialMinCosTheta)
    {
        return scatter;
    }
    if (scatter.delta != 0u)
    {
        return scatter;
    }

    float pdf = resolvedMaterialCombinedPdfVariant<LayerFlags>(
        payload,
        viewDirection,
        scatter.direction);
    if (pdf <= 1.0e-6f)
    {
        return scatter;
    }

    float3 bsdf = evaluateResolvedMaterialBsdfVariant<LayerFlags>(
        payload,
        viewDirection,
        scatter.direction);
    scatter.valid = any(bsdf > float3(0.0f)) ? 1u : 0u;
    scatter.pdf = pdf;
    scatter.weight = bsdf * noL / pdf;
    return scatter;
}

public float3 evaluateResolvedMaterialBsdf(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    return evaluateResolvedMaterialBsdfVariant<kResolvedMaterialLayerVariantMask>(
        payload,
        viewDirection,
        lightDirection);
}

public ResolvedMaterialScatter sampleResolvedMaterialScatter(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 randomValues)
{
    return sampleResolvedMaterialScatterVariant<kResolvedMaterialLayerVariantMask>(
        payload,
        viewDirection,
        randomValues);
}
