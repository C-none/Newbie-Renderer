implementing common;

public enum ResolvedMaterialScatterKind : uint
{
    diffuse = 0u,
    specular = 1u,
    sheen = 2u,
    clearcoat = 3u,
    transmission = 4u,
}

public enum ResolvedMaterialMediumTransition : uint
{
    none = 0u,
    enter = 1u,
    exit = 2u,
}

public struct ResolvedMaterialPayload
{
    public uint materialIndex = kRtMaterialFallbackIndex;
    public uint instanceMetadataIndex = 0xffffffffu;
    public uint geometryIndex = 0xffffffffu;
    public RtMaterialLayerFlag layerFlags = RtMaterialLayerFlag.none;
    public RtMaterialFeatureFlag featureFlags = RtMaterialFeatureFlag.none;
    public AlphaMode alphaMode = AlphaMode.opaque;
    public float alphaCutoff = 0.5f;
    public float hitT = 0.0f;
    public float3 position = float3(0.0f);
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 shadingNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 anisotropyTangent = float3(1.0f, 0.0f, 0.0f);
    public float anisotropyStrength = 0.0f;
    public float4 baseColor = float4(1.0f);
    public float metallic = 0.0f;
    public float roughness = 1.0f;
    public float3 emissive = float3(0.0f);
    public float alpha = 1.0f;
    public MaterialLayerStack layers = {};
    public bool frontFace = true;
    public float incidentMediumIor = 1.0f;
    public float exteriorMediumIor = 1.0f;
};

public struct ResolvedMaterialScatter
{
    public uint valid = 0u;
    public ResolvedMaterialScatterKind kind = ResolvedMaterialScatterKind.diffuse;
    public float3 direction = float3(0.0f, 1.0f, 0.0f);
    public float3 weight = float3(0.0f);
    public float3 diffuseWeight = float3(0.0f);
    public float3 specularWeight = float3(0.0f);
    public float pdf = 0.0f;
    public float pmf = 0.0f;
    public uint delta = 0u;
    public ResolvedMaterialMediumTransition mediumTransition = ResolvedMaterialMediumTransition.none;
    public float etaRatio = 1.0f;
};

public struct ResolvedMaterialBsdfComponents
{
    public float3 diffuse = float3(0.0f);
    public float3 specular = float3(0.0f);
};

public struct ResolvedMaterialDirectComponents
{
    public float3 diffuseRadiance = float3(0.0f);
    public float3 specularRadiance = float3(0.0f);
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
public ResolvedMaterialPayload resolveLitMaterialPayloadVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
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
    payload.anisotropyTangent = orthogonalTangent(shadingNormal);
    payload.anisotropyStrength = 0.0f;
    if (BaseLobeVariant == RtBaseLobeVariant.anisotropic)
    {
        sampleMaterialAnisotropy(
            textureRefs,
            material,
            surface,
            shadingNormal,
            payload.anisotropyTangent,
            payload.anisotropyStrength);
    }
    payload.baseColor = core.baseColor;
    payload.metallic = core.metallic;
    payload.roughness = core.roughness;
    payload.emissive = core.emissive;
    payload.alpha = core.alpha;
    payload.layers = sampleMaterialLayersVariant<LayerFlags>(
        textureRefs,
        layerRecords,
        material,
        surface,
        orientedNormal);
    return payload;
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

public float3 materialPayloadToLocal(float3 worldDirection, float3 normal)
{
    float3 tangent;
    float3 bitangent;
    materialPayloadBasis(normal, tangent, bitangent);
    return float3(
        dot(worldDirection, tangent),
        dot(worldDirection, bitangent),
        dot(worldDirection, normal));
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

public float3 sampleMaterialPayloadGgxVisibleHalfVector(
    float2 randomValues,
    float roughness,
    float3 normal,
    float3 viewDirection)
{
    float alpha = max(roughness * roughness, kMaterialMinRoughness * kMaterialMinRoughness);
    float3 localView = materialPayloadToLocal(viewDirection, normal);
    float3 stretchedView = safeNormalize(
        float3(alpha * localView.x, alpha * localView.y, max(localView.z, 0.0f)),
        float3(0.0f, 0.0f, 1.0f));

    float lensq = stretchedView.x * stretchedView.x + stretchedView.y * stretchedView.y;
    float3 tangent1 = lensq > 1.0e-8f
        ? float3(-stretchedView.y, stretchedView.x, 0.0f) * rsqrt(lensq)
        : float3(1.0f, 0.0f, 0.0f);
    float3 tangent2 = cross(stretchedView, tangent1);

    float radius = sqrt(saturate(randomValues.x));
    float phi = 2.0f * kMaterialPi * randomValues.y;
    float diskX = radius * cos(phi);
    float diskY = radius * sin(phi);
    float blend = 0.5f * (1.0f + stretchedView.z);
    diskY =
        (1.0f - blend) * sqrt(saturate(1.0f - diskX * diskX)) +
        blend * diskY;
    float diskZ = sqrt(saturate(1.0f - diskX * diskX - diskY * diskY));
    float3 stretchedNormal =
        diskX * tangent1 +
        diskY * tangent2 +
        diskZ * stretchedView;
    float3 localHalfVector = safeNormalize(
        float3(
            alpha * stretchedNormal.x,
            alpha * stretchedNormal.y,
            max(stretchedNormal.z, 0.0f)),
        float3(0.0f, 0.0f, 1.0f));
    return materialPayloadToWorld(localHalfVector, normal);
}

public float materialPayloadGgxVisibleHalfVectorPdf(
    float roughness,
    float3 normal,
    float3 viewDirection,
    float3 halfVector)
{
    float noV = abs(dot(normal, viewDirection));
    float noH = saturate(dot(normal, halfVector));
    float voH = abs(dot(viewDirection, halfVector));
    if (noV <= kMaterialMinCosTheta ||
        noH <= kMaterialMinCosTheta ||
        voH <= kMaterialMinCosTheta)
    {
        return 0.0f;
    }

    float alpha = max(roughness * roughness, kMaterialMinRoughness * kMaterialMinRoughness);
    return
        ggxDistribution(alpha, noH) *
        ggxSmithG1(alpha, noV) *
        voH /
        noV;
}

public struct BaseGgxDistribution<let BaseLobeVariant : RtBaseLobeVariant>
{
    public float alpha(ResolvedMaterialPayload payload)
    {
        return max(
            payload.roughness * payload.roughness,
            kMaterialMinRoughness * kMaterialMinRoughness);
    }

    public void basis(
        ResolvedMaterialPayload payload,
        out float3 normal,
        out float3 tangent,
        out float3 bitangent)
    {
        normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        tangent = safeNormalize(
            payload.anisotropyTangent - normal * dot(normal, payload.anisotropyTangent),
            orthogonalTangent(normal));
        bitangent = safeNormalize(cross(normal, tangent), cross(normal, orthogonalTangent(normal)));
    }

    public void roughness(
        ResolvedMaterialPayload payload,
        out float alphaT,
        out float alphaB)
    {
        float isotropicAlpha = alpha(payload);
        float strength = saturate(payload.anisotropyStrength);
        alphaT = lerp(isotropicAlpha, 1.0f, strength * strength);
        alphaB = isotropicAlpha;
    }

    public float distribution(ResolvedMaterialPayload payload, float3 halfVector)
    {
        float3 normal;
        float3 tangent;
        float3 bitangent;
        basis(payload, normal, tangent, bitangent);
        float noH = saturate(dot(normal, halfVector));
        float isotropicAlpha = alpha(payload);
        if (BaseLobeVariant == RtBaseLobeVariant.isotropic ||
            payload.anisotropyStrength <= 0.0f)
        {
            return ggxDistribution(isotropicAlpha, noH);
        }

        float alphaT;
        float alphaB;
        roughness(payload, alphaT, alphaB);
        float ht = dot(halfVector, tangent);
        float hb = dot(halfVector, bitangent);
        float denominator =
            (ht / alphaT) * (ht / alphaT) +
            (hb / alphaB) * (hb / alphaB) +
            noH * noH;
        return 1.0f / max(
            kMaterialPi * alphaT * alphaB * denominator * denominator,
            1.0e-7f);
    }

    public float smithG1(ResolvedMaterialPayload payload, float3 direction)
    {
        float3 normal;
        float3 tangent;
        float3 bitangent;
        basis(payload, normal, tangent, bitangent);
        float noX = abs(dot(normal, direction));
        float isotropicAlpha = alpha(payload);
        if (BaseLobeVariant == RtBaseLobeVariant.isotropic ||
            payload.anisotropyStrength <= 0.0f)
        {
            return ggxSmithG1(isotropicAlpha, noX);
        }
        if (noX <= 0.0f)
        {
            return 0.0f;
        }

        float alphaT;
        float alphaB;
        roughness(payload, alphaT, alphaB);
        float xt = dot(direction, tangent);
        float xb = dot(direction, bitangent);
        float projected =
            alphaT * alphaT * xt * xt +
            alphaB * alphaB * xb * xb;
        float root = sqrt(1.0f + projected / max(noX * noX, 1.0e-7f));
        return 2.0f / (1.0f + root);
    }

    public float smithG2(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float3 lightDirection)
    {
        return smithG1(payload, viewDirection) * smithG1(payload, lightDirection);
    }

    public float3 sampleVisibleHalfVector(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float2 randomValues)
    {
        float3 normal;
        float3 tangent;
        float3 bitangent;
        basis(payload, normal, tangent, bitangent);
        if (BaseLobeVariant == RtBaseLobeVariant.isotropic ||
            payload.anisotropyStrength <= 0.0f)
        {
            return sampleMaterialPayloadGgxVisibleHalfVector(
                randomValues,
                payload.roughness,
                normal,
                viewDirection);
        }

        float alphaT;
        float alphaB;
        roughness(payload, alphaT, alphaB);
        float3 localView = float3(
            dot(viewDirection, tangent),
            dot(viewDirection, bitangent),
            dot(viewDirection, normal));
        float3 stretchedView = safeNormalize(
            float3(alphaT * localView.x, alphaB * localView.y, max(localView.z, 0.0f)),
            float3(0.0f, 0.0f, 1.0f));

        float lensq = stretchedView.x * stretchedView.x + stretchedView.y * stretchedView.y;
        float3 tangent1 = lensq > 1.0e-8f
            ? float3(-stretchedView.y, stretchedView.x, 0.0f) * rsqrt(lensq)
            : float3(1.0f, 0.0f, 0.0f);
        float3 tangent2 = cross(stretchedView, tangent1);
        float radius = sqrt(saturate(randomValues.x));
        float phi = 2.0f * kMaterialPi * randomValues.y;
        float diskX = radius * cos(phi);
        float diskY = radius * sin(phi);
        float blend = 0.5f * (1.0f + stretchedView.z);
        diskY =
            (1.0f - blend) * sqrt(saturate(1.0f - diskX * diskX)) +
            blend * diskY;
        float diskZ = sqrt(saturate(1.0f - diskX * diskX - diskY * diskY));
        float3 stretchedNormal =
            diskX * tangent1 +
            diskY * tangent2 +
            diskZ * stretchedView;
        float3 localHalfVector = safeNormalize(
            float3(
                alphaT * stretchedNormal.x,
                alphaB * stretchedNormal.y,
                max(stretchedNormal.z, 0.0f)),
            float3(0.0f, 0.0f, 1.0f));
        return safeNormalize(
            tangent * localHalfVector.x +
            bitangent * localHalfVector.y +
            normal * localHalfVector.z,
            normal);
    }

    public float visibleHalfVectorPdf(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float3 halfVector)
    {
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        float noV = abs(dot(normal, viewDirection));
        float noH = saturate(dot(normal, halfVector));
        float voH = abs(dot(viewDirection, halfVector));
        if (noV <= kMaterialMinCosTheta ||
            noH <= kMaterialMinCosTheta ||
            voH <= kMaterialMinCosTheta)
        {
            return 0.0f;
        }

        return
            distribution(payload, halfVector) *
            smithG1(payload, viewDirection) *
            voH /
            noV;
    }
}

public float materialPayloadDiffusePdf(float3 normal, float3 lightDirection)
{
    return saturate(dot(normal, lightDirection)) * kMaterialInvPi;
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

// The base surface is one top-level lobe. Diffuse reflection, interface reflection, and optional
// transmission share one selection distribution so Fresnel/TIR decisions and their measures cannot
// diverge between evaluate(), pdf(), and sample(). Every transmission-only operation remains in a
// LayerFlags static block, allowing non-transmission link variants to discard it completely.
public struct BaseSurfaceBsdfLobe<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>
{
    public float dielectricInterfaceFresnel(
        ResolvedMaterialPayload payload,
        float cosine)
    {
        float dielectric = fresnelSchlick(0.04f, cosine);

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            float interfaceIor = payload.layers.transmissionIor;
            dielectric = 1.0f;
            if (interfaceIor > 0.0f)
            {
                if (payload.layers.transmissionMode == RtTransmissionMode.volume)
                {
                    float etaI = payload.incidentMediumIor;
                    float etaT = payload.frontFace ? interfaceIor : payload.exteriorMediumIor;
                    if (etaI > 0.0f && etaT > 0.0f)
                    {
                        float cosThetaI = saturate(abs(cosine));
                        float eta = etaI / etaT;
                        float sinThetaT2 = eta * eta * max(1.0f - cosThetaI * cosThetaI, 0.0f);
                        if (sinThetaT2 < 1.0f)
                        {
                            float cosThetaT = sqrt(max(1.0f - sinThetaT2, 0.0f));
                            float parallelNumerator = etaT * cosThetaI - etaI * cosThetaT;
                            float parallelDenominator = etaT * cosThetaI + etaI * cosThetaT;
                            float perpendicularNumerator = etaI * cosThetaI - etaT * cosThetaT;
                            float perpendicularDenominator = etaI * cosThetaI + etaT * cosThetaT;
                            float parallel = parallelNumerator / max(abs(parallelDenominator), 1.0e-7f);
                            float perpendicular =
                                perpendicularNumerator / max(abs(perpendicularDenominator), 1.0e-7f);
                            dielectric = saturate(
                                0.5f * (parallel * parallel + perpendicular * perpendicular));
                        }
                    }
                }
                else
                {
                    float iorDenominator = interfaceIor + 1.0f;
                    float f0 = abs(iorDenominator) > 1.0e-7f
                        ? ((interfaceIor - 1.0f) / iorDenominator) *
                            ((interfaceIor - 1.0f) / iorDenominator)
                        : 1.0f;
                    dielectric = fresnelSchlick(saturate(f0), cosine);
                }
            }
        }

        return dielectric;
    }

    public float3 interfaceFresnel(
        ResolvedMaterialPayload payload,
        float cosine)
    {
        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            // KHR_materials_ior reserves zero for its positive-infinity compatibility mode.
            // The final Fresnel term is exactly one even for fractional metallic materials.
            if (payload.layers.transmissionIor == 0.0f)
            {
                return float3(1.0f);
            }
        }

        MaterialSample core = materialSampleFromResolvedPayload(payload);
        float3 dielectricFresnel = float3(dielectricInterfaceFresnel(payload, cosine));
        float3 conductorFresnel = fresnelSchlick(payload.baseColor.rgb, cosine);
        return lerp(dielectricFresnel, conductorFresnel, core.metallic);
    }

    public void selectionWeights(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        out float diffuseWeight,
        out float interfaceWeight)
    {
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        float noV = saturate(dot(normal, viewDirection));
        float3 fresnel = interfaceFresnel(payload, noV);
        float transmissionFactor = 0.0f;
        float3 transmissionImportance = float3(0.0f);

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            if (payload.layers.transmissionIor > 0.0f)
            {
                transmissionFactor = payload.layers.transmission;
                float transmissionFresnel = dielectricInterfaceFresnel(payload, noV);
                transmissionImportance =
                    (1.0f - transmissionFresnel) *
                    payload.baseColor.rgb *
                    (1.0f - payload.metallic) *
                    transmissionFactor;
            }
        }

        float dielectricShare = 1.0f - payload.metallic;
        float3 diffuseImportance =
            (float3(1.0f) - fresnel) *
            payload.baseColor.rgb *
            dielectricShare *
            (1.0f - transmissionFactor);
        diffuseWeight = max(materialPayloadLuminance(diffuseImportance), 0.0f);
        interfaceWeight =
            max(materialPayloadLuminance(fresnel), 0.0f) +
            max(materialPayloadLuminance(transmissionImportance), 0.0f);
    }

    public void interfaceEventProbabilities(
        ResolvedMaterialPayload payload,
        float3 fresnel,
        float cosine,
        out float reflectionProbability,
        out float transmissionProbability)
    {
        float reflectionImportance = max(materialPayloadLuminance(fresnel), 0.0f);
        float transmissionImportance = 0.0f;

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            if (payload.layers.transmissionIor > 0.0f)
            {
                float transmissionFresnel = dielectricInterfaceFresnel(payload, cosine);
                float3 importance =
                    (1.0f - transmissionFresnel) *
                    payload.baseColor.rgb *
                    (1.0f - payload.metallic) *
                    payload.layers.transmission;
                transmissionImportance = max(materialPayloadLuminance(importance), 0.0f);
            }
        }

        float totalImportance = reflectionImportance + transmissionImportance;
        if (totalImportance <= 1.0e-7f)
        {
            reflectionProbability = 1.0f;
            transmissionProbability = 0.0f;
            return;
        }

        reflectionProbability = reflectionImportance / totalImportance;
        transmissionProbability = transmissionImportance / totalImportance;
    }

    public float selectionWeight(ResolvedMaterialPayload payload, float3 viewDirection)
    {
        float diffuseWeight;
        float interfaceWeight;
        selectionWeights(payload, viewDirection, diffuseWeight, interfaceWeight);
        return diffuseWeight + interfaceWeight;
    }

    public ResolvedMaterialBsdfComponents evaluate(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float3 lightDirection)
    {
        ResolvedMaterialBsdfComponents result = {};
        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        float noV = saturate(dot(normal, viewDirection));
        float signedNoL = dot(normal, lightDirection);
        if (noV <= kMaterialMinCosTheta)
        {
            return result;
        }

        MaterialSample core = materialSampleFromResolvedPayload(payload);
        BaseGgxDistribution<BaseLobeVariant> baseGgx;
        if (signedNoL > kMaterialMinCosTheta)
        {
            float noL = saturate(signedNoL);
            float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
            float noH = saturate(dot(normal, halfVector));
            float voH = saturate(dot(viewDirection, halfVector));
            if (noH <= kMaterialMinCosTheta || voH <= kMaterialMinCosTheta)
            {
                return result;
            }

            float3 fresnel = interfaceFresnel(payload, voH);
            float distribution = baseGgx.distribution(payload, halfVector);
            float geometry = baseGgx.smithG2(payload, viewDirection, lightDirection);
            result.specular =
                fresnel * distribution * geometry /
                max(4.0f * noV * noL, 1.0e-7f);

            float transmissionFactor = 0.0f;
            if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
            {
                transmissionFactor = payload.layers.transmission;
            }

            result.diffuse =
                (float3(1.0f) - fresnel) *
                core.baseColor.rgb *
                (1.0f - core.metallic) *
                (1.0f - transmissionFactor) *
                kMaterialInvPi;
            return result;
        }

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            float absNoL = abs(signedNoL);
            float transmissionFactor =
                payload.layers.transmission *
                (1.0f - core.metallic);
            if (signedNoL >= -kMaterialMinCosTheta ||
                absNoL <= kMaterialMinCosTheta ||
                transmissionFactor <= 0.0f ||
                payload.layers.transmissionIor <= 0.0f)
            {
                return result;
            }

            if (payload.layers.transmissionMode == RtTransmissionMode.thin)
            {
                float3 foldedLightDirection =
                    lightDirection -
                    2.0f * dot(normal, lightDirection) * normal;
                float3 halfVector = safeNormalize(viewDirection + foldedLightDirection, normal);
                float noH = saturate(dot(normal, halfVector));
                float voH = saturate(dot(viewDirection, halfVector));
                if (noH <= kMaterialMinCosTheta || voH <= kMaterialMinCosTheta)
                {
                    return result;
                }

                float transmissionFresnel = dielectricInterfaceFresnel(payload, voH);
                float distribution = baseGgx.distribution(payload, halfVector);
                float geometry = baseGgx.smithG2(payload, viewDirection, lightDirection);
                result.specular =
                    (1.0f - transmissionFresnel) *
                    core.baseColor.rgb *
                    transmissionFactor *
                    distribution *
                    geometry /
                    max(4.0f * noV * absNoL, 1.0e-7f);
                return result;
            }

            float etaI = payload.incidentMediumIor;
            float etaT = payload.frontFace
                ? payload.layers.transmissionIor
                : payload.exteriorMediumIor;
            if (etaI <= 0.0f || etaT <= 0.0f || abs(etaT - etaI) <= 1.0e-6f)
            {
                // Equal-IOR volume transmission is represented by sample() as a straight-through delta.
                return result;
            }

            float3 halfVector = safeNormalize(
                -(etaI * viewDirection + etaT * lightDirection),
                normal);
            if (dot(normal, halfVector) < 0.0f)
            {
                halfVector = -halfVector;
            }

            float noH = saturate(dot(normal, halfVector));
            float voH = dot(viewDirection, halfVector);
            float loH = dot(lightDirection, halfVector);
            if (noH <= kMaterialMinCosTheta ||
                voH <= kMaterialMinCosTheta ||
                loH >= -kMaterialMinCosTheta)
            {
                return result;
            }

            float denominator = etaI * voH + etaT * loH;
            float denominator2 = denominator * denominator;
            if (denominator2 <= 1.0e-10f)
            {
                return result;
            }

            float transmissionFresnel = dielectricInterfaceFresnel(payload, voH);
            float distribution = baseGgx.distribution(payload, halfVector);
            float geometry = baseGgx.smithG2(payload, viewDirection, lightDirection);
            float refractionFactor =
                etaT * etaT *
                abs(voH * loH) /
                max(noV * absNoL * denominator2, 1.0e-10f);
            float radianceEtaCorrection = (etaI * etaI) / (etaT * etaT);
            result.specular =
                (1.0f - transmissionFresnel) *
                core.baseColor.rgb *
                transmissionFactor *
                distribution *
                geometry *
                refractionFactor *
                radianceEtaCorrection;
        }

        return result;
    }

    public float pdf(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float3 lightDirection)
    {
        float diffuseWeight;
        float interfaceWeight;
        selectionWeights(payload, viewDirection, diffuseWeight, interfaceWeight);
        float baseWeight = diffuseWeight + interfaceWeight;
        if (baseWeight <= 1.0e-7f)
        {
            return 0.0f;
        }

        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        BaseGgxDistribution<BaseLobeVariant> baseGgx;
        float noV = saturate(dot(normal, viewDirection));
        float signedNoL = dot(normal, lightDirection);
        if (noV <= kMaterialMinCosTheta)
        {
            return 0.0f;
        }

        if (signedNoL > kMaterialMinCosTheta)
        {
            float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
            float noH = saturate(dot(normal, halfVector));
            float voH = saturate(dot(viewDirection, halfVector));
            if (noH <= kMaterialMinCosTheta || voH <= kMaterialMinCosTheta)
            {
                return 0.0f;
            }

            float3 fresnel = interfaceFresnel(payload, voH);
            float reflectionProbability;
            float transmissionProbability;
            interfaceEventProbabilities(
                payload,
                fresnel,
                voH,
                reflectionProbability,
                transmissionProbability);
            if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
            {
                if (payload.layers.transmissionMode == RtTransmissionMode.volume)
                {
                    float etaI = payload.incidentMediumIor;
                    float etaT = payload.frontFace
                        ? payload.layers.transmissionIor
                        : payload.exteriorMediumIor;
                    if (etaI > 0.0f && etaT > 0.0f && abs(etaT - etaI) <= 1.0e-6f)
                    {
                        // All transmitted microfacets collapse to the same straight-through delta when
                        // etaI == etaT. Use a direction-independent event probability so its PMF is the
                        // integrated probability mass, while rough reflection keeps the VNDF density.
                        float3 selectionFresnel = interfaceFresnel(payload, noV);
                        interfaceEventProbabilities(
                            payload,
                            selectionFresnel,
                            noV,
                            reflectionProbability,
                            transmissionProbability);
                    }
                }
            }
            float diffusePdf = materialPayloadDiffusePdf(normal, lightDirection);
            float halfVectorPdf =
                baseGgx.visibleHalfVectorPdf(payload, viewDirection, halfVector);
            float reflectionPdf = halfVectorPdf / max(4.0f * voH, 1.0e-7f);
            return (
                diffuseWeight * diffusePdf +
                interfaceWeight * reflectionProbability * reflectionPdf) /
                baseWeight;
        }

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            if (signedNoL >= -kMaterialMinCosTheta ||
                payload.layers.transmission <= 0.0f ||
                payload.metallic >= 1.0f ||
                payload.layers.transmissionIor <= 0.0f)
            {
                return 0.0f;
            }

            float3 halfVector;
            float refractionJacobian = 0.0f;
            if (payload.layers.transmissionMode == RtTransmissionMode.thin)
            {
                float3 foldedLightDirection =
                    lightDirection -
                    2.0f * dot(normal, lightDirection) * normal;
                halfVector = safeNormalize(viewDirection + foldedLightDirection, normal);
                float voH = saturate(dot(viewDirection, halfVector));
                refractionJacobian = 1.0f / max(4.0f * voH, 1.0e-7f);
            }
            else
            {
                float etaI = payload.incidentMediumIor;
                float etaT = payload.frontFace
                    ? payload.layers.transmissionIor
                    : payload.exteriorMediumIor;
                if (etaI <= 0.0f || etaT <= 0.0f || abs(etaT - etaI) <= 1.0e-6f)
                {
                    return 0.0f;
                }

                halfVector = safeNormalize(
                    -(etaI * viewDirection + etaT * lightDirection),
                    normal);
                if (dot(normal, halfVector) < 0.0f)
                {
                    halfVector = -halfVector;
                }

                float voH = dot(viewDirection, halfVector);
                float loH = dot(lightDirection, halfVector);
                if (voH <= kMaterialMinCosTheta || loH >= -kMaterialMinCosTheta)
                {
                    return 0.0f;
                }

                float denominator = etaI * voH + etaT * loH;
                refractionJacobian =
                    etaT * etaT * abs(loH) /
                    max(denominator * denominator, 1.0e-10f);
            }

            float noH = saturate(dot(normal, halfVector));
            float voH = saturate(dot(viewDirection, halfVector));
            if (noH <= kMaterialMinCosTheta || voH <= kMaterialMinCosTheta)
            {
                return 0.0f;
            }

            float3 fresnel = interfaceFresnel(payload, voH);
            float reflectionProbability;
            float transmissionProbability;
            interfaceEventProbabilities(
                payload,
                fresnel,
                voH,
                reflectionProbability,
                transmissionProbability);
            float halfVectorPdf =
                baseGgx.visibleHalfVectorPdf(payload, viewDirection, halfVector);
            return interfaceWeight * transmissionProbability * halfVectorPdf * refractionJacobian /
                baseWeight;
        }

        return 0.0f;
    }

    public ResolvedMaterialScatter sample(
        ResolvedMaterialPayload payload,
        float3 viewDirection,
        float3 randomValues)
    {
        ResolvedMaterialScatter scatter = {};
        float diffuseWeight;
        float interfaceWeight;
        selectionWeights(payload, viewDirection, diffuseWeight, interfaceWeight);
        float baseWeight = diffuseWeight + interfaceWeight;
        if (baseWeight <= 1.0e-7f)
        {
            return scatter;
        }

        float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
        BaseGgxDistribution<BaseLobeVariant> baseGgx;
        float noV = saturate(dot(normal, viewDirection));
        if (noV <= kMaterialMinCosTheta)
        {
            return scatter;
        }

        float selectedWeight = min(saturate(randomValues.x), 0.99999994f) * baseWeight;
        if (selectedWeight < diffuseWeight)
        {
            scatter.direction = sampleMaterialPayloadCosineHemisphere(randomValues.yz, normal);
            scatter.kind = ResolvedMaterialScatterKind.diffuse;
            scatter.pdf = pdf(payload, viewDirection, scatter.direction);
            scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
            return scatter;
        }

        if (interfaceWeight <= 1.0e-7f)
        {
            return scatter;
        }

        float3 halfVector =
            baseGgx.sampleVisibleHalfVector(payload, viewDirection, randomValues.yz);
        float voH = dot(viewDirection, halfVector);
        if (voH <= kMaterialMinCosTheta)
        {
            return scatter;
        }

        float3 fresnel = interfaceFresnel(payload, voH);
        float reflectionProbability;
        float transmissionProbability;
        interfaceEventProbabilities(
            payload,
            fresnel,
            voH,
            reflectionProbability,
            transmissionProbability);
        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            if (payload.layers.transmissionMode == RtTransmissionMode.volume)
            {
                float etaI = payload.incidentMediumIor;
                float etaT = payload.frontFace
                    ? payload.layers.transmissionIor
                    : payload.exteriorMediumIor;
                if (etaI > 0.0f && etaT > 0.0f && abs(etaT - etaI) <= 1.0e-6f)
                {
                    float3 selectionFresnel = interfaceFresnel(payload, noV);
                    interfaceEventProbabilities(
                        payload,
                        selectionFresnel,
                        noV,
                        reflectionProbability,
                        transmissionProbability);
                }
            }
        }
        float interfaceRandom = saturate((selectedWeight - diffuseWeight) / interfaceWeight);
        if (interfaceRandom < reflectionProbability)
        {
            scatter.direction = safeNormalize(reflect(-viewDirection, halfVector), normal);
            if (dot(normal, scatter.direction) <= kMaterialMinCosTheta)
            {
                return {};
            }

            scatter.kind = ResolvedMaterialScatterKind.specular;
            scatter.pdf = pdf(payload, viewDirection, scatter.direction);
            scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
            return scatter;
        }

        if (uint(LayerFlags & RtMaterialLayerFlag.transmission) != 0u)
        {
            if (transmissionProbability <= 0.0f ||
                payload.layers.transmission <= 0.0f ||
                payload.metallic >= 1.0f ||
                payload.layers.transmissionIor <= 0.0f)
            {
                return scatter;
            }

            scatter.kind = ResolvedMaterialScatterKind.transmission;
            if (payload.layers.transmissionMode == RtTransmissionMode.thin)
            {
                float3 reflectedDirection = safeNormalize(
                    reflect(-viewDirection, halfVector),
                    normal);
                scatter.direction =
                    reflectedDirection -
                    2.0f * dot(normal, reflectedDirection) * normal;
                if (dot(normal, scatter.direction) >= -kMaterialMinCosTheta)
                {
                    return {};
                }

                scatter.pdf = pdf(payload, viewDirection, scatter.direction);
                scatter.valid = scatter.pdf > 0.0f ? 1u : 0u;
                return scatter;
            }

            float etaI = payload.incidentMediumIor;
            float etaT = payload.frontFace
                ? payload.layers.transmissionIor
                : payload.exteriorMediumIor;
            if (etaI <= 0.0f || etaT <= 0.0f)
            {
                return {};
            }

            scatter.etaRatio = etaT / etaI;
            scatter.mediumTransition = payload.frontFace
                ? ResolvedMaterialMediumTransition.enter
                : ResolvedMaterialMediumTransition.exit;
            if (abs(etaT - etaI) <= 1.0e-6f)
            {
                scatter.direction = -viewDirection;
                scatter.delta = 1u;
                scatter.pmf =
                    (interfaceWeight / baseWeight) *
                    transmissionProbability;
                scatter.specularWeight =
                    payload.baseColor.rgb *
                    (1.0f - payload.metallic) *
                    payload.layers.transmission;
                if (scatter.pmf <= 0.0f)
                {
                    return {};
                }

                scatter.valid = 1u;
                return scatter;
            }

            float eta = etaI / etaT;
            float cosThetaI = saturate(voH);
            float sinThetaT2 = eta * eta * max(1.0f - cosThetaI * cosThetaI, 0.0f);
            if (sinThetaT2 >= 1.0f)
            {
                // Exact Fresnel makes transmissionProbability zero under TIR. Reaching this branch can
                // only be a numeric boundary disagreement, so reject the sample instead of changing its
                // measure after selection.
                return {};
            }

            float cosThetaT = sqrt(max(1.0f - sinThetaT2, 0.0f));
            scatter.direction = safeNormalize(
                -eta * viewDirection +
                    (eta * cosThetaI - cosThetaT) * halfVector,
                -normal);
            if (dot(normal, scatter.direction) >= -kMaterialMinCosTheta)
            {
                return {};
            }

            scatter.pdf = pdf(payload, viewDirection, scatter.direction);
            if (scatter.pdf <= 0.0f)
            {
                return {};
            }

            scatter.valid = 1u;
            return scatter;
        }

        return scatter;
    }
};

public struct SheenBsdfLobe
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

public struct ClearcoatBsdfLobe
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

public ResolvedMaterialBsdfComponents evaluateResolvedMaterialBsdfComponentsVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    ResolvedMaterialBsdfComponents result = {};
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        BaseSurfaceBsdfLobe<LayerFlags, BaseLobeVariant> baseSurface;
        result = baseSurface.evaluate(payload, viewDirection, lightDirection);
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        result.diffuse += sheen.evaluate(payload, viewDirection, lightDirection);
    }

    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        result.diffuse = attenuateBaseByClearcoat(result.diffuse, payload.layers, viewDirection);
        result.specular = attenuateBaseByClearcoat(result.specular, payload.layers, viewDirection) +
            clearcoat.evaluate(payload, viewDirection, lightDirection);
    }

    return result;
}

// Direct lighting enters the same shared base-surface evaluate path as continuation sampling. The
// absolute cosine is required for the opposite-hemisphere BTDF while same-hemisphere reflection keeps
// the usual positive cosine. Optional sheen and clearcoat remain separate top-level lobes inside the
// component evaluator above.
public ResolvedMaterialDirectComponents evaluateResolvedMaterialDirectComponentsVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    MaterialDirectLightingInput lighting)
{
    ResolvedMaterialBsdfComponents bsdf =
        evaluateResolvedMaterialBsdfComponentsVariant<LayerFlags, BaseLobeVariant>(
            payload,
            lighting.viewDirection,
            lighting.lightDirection);
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);
    float absNoL = abs(dot(normal, lighting.lightDirection));

    ResolvedMaterialDirectComponents result = {};
    result.diffuseRadiance = bsdf.diffuse * lighting.lightRadiance * absNoL;
    result.specularRadiance = bsdf.specular * lighting.lightRadiance * absNoL;
    return result;
}

public MaterialShadingResult evaluateResolvedMaterialDirectVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    MaterialDirectLightingInput lighting)
{
    ResolvedMaterialDirectComponents components =
        evaluateResolvedMaterialDirectComponentsVariant<LayerFlags, BaseLobeVariant>(payload, lighting);

    MaterialShadingResult result;
    result.radiance = components.diffuseRadiance + components.specularRadiance;
    result.alpha = payload.alpha;
    return result;
}

public float3 evaluateResolvedMaterialBsdfVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    ResolvedMaterialBsdfComponents components =
        evaluateResolvedMaterialBsdfComponentsVariant<LayerFlags, BaseLobeVariant>(
            payload,
            viewDirection,
            lightDirection);
    return components.diffuse + components.specular;
}

public float resolvedMaterialCombinedPdfVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    float totalWeight = 0.0f;
    float weightedPdf = 0.0f;

    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        BaseSurfaceBsdfLobe<LayerFlags, BaseLobeVariant> baseSurface;
        float baseSurfaceWeight = baseSurface.selectionWeight(payload, viewDirection);
        totalWeight += baseSurfaceWeight;
        weightedPdf +=
            baseSurfaceWeight *
            baseSurface.pdf(payload, viewDirection, lightDirection);
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
    if (totalWeight <= 0.0f)
    {
        return 0.0f;
    }

    return weightedPdf / totalWeight;
}

public ResolvedMaterialScatter sampleResolvedMaterialScatterVariant<
    let LayerFlags : RtMaterialLayerFlag,
    let BaseLobeVariant : RtBaseLobeVariant>(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float4 randomValues)
{
    ResolvedMaterialScatter scatter = {};
    float3 normal = safeNormalize(payload.shadingNormal, payload.geometryNormal);

    // Pass 1: accumulate per-lobe selection weights. Each optional lobe's weight computation lives in its
    // own static block, so inactive-layer selection math is removed by link specialization.
    float baseSurfaceWeight = 0.0f;
    float sheenWeight = 0.0f;
    float clearcoatWeight = 0.0f;
    float totalWeight = 0.0f;
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        BaseSurfaceBsdfLobe<LayerFlags, BaseLobeVariant> baseSurface;
        baseSurfaceWeight = baseSurface.selectionWeight(payload, viewDirection);
        totalWeight += baseSurfaceWeight;
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
    if (totalWeight <= 1.0e-6f)
    {
        return scatter;
    }

    // Pass 2: pick one lobe by cumulative threshold and sample only that lobe. Each lobe's sample() call
    // is inside its static block; `sampled` keeps the earliest match and stops later lobes from writing.
    float selectedWeight = min(saturate(randomValues.x), 0.99999994f) * totalWeight;
    float selectedTopLevelWeight = 0.0f;
    float threshold = 0.0f;
    bool sampled = false;
    if (uint(LayerFlags & RtMaterialLayerFlag.baseSurface) != 0u)
    {
        BaseSurfaceBsdfLobe<LayerFlags, BaseLobeVariant> baseSurface;
        threshold += baseSurfaceWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = baseSurface.sample(payload, viewDirection, randomValues.yzw);
            selectedTopLevelWeight = baseSurfaceWeight;
            sampled = true;
        }
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.sheen) != 0u)
    {
        SheenBsdfLobe sheen;
        threshold += sheenWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = sheen.sample(payload, viewDirection, randomValues.zw);
            selectedTopLevelWeight = sheenWeight;
            sampled = true;
        }
    }
    if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
    {
        ClearcoatBsdfLobe clearcoat;
        threshold += clearcoatWeight;
        if (!sampled && selectedWeight < threshold)
        {
            scatter = clearcoat.sample(payload, viewDirection, randomValues.zw);
            selectedTopLevelWeight = clearcoatWeight;
            sampled = true;
        }
    }

    if (!sampled || scatter.valid == 0u)
    {
        return scatter;
    }

    if (scatter.delta != 0u)
    {
        scatter.pdf = 0.0f;
        scatter.pmf *= selectedTopLevelWeight / totalWeight;
        if (scatter.pmf <= 1.0e-7f)
        {
            scatter.valid = 0u;
            return scatter;
        }

        if (uint(LayerFlags & RtMaterialLayerFlag.clearcoat) != 0u)
        {
            scatter.diffuseWeight =
                attenuateBaseByClearcoat(scatter.diffuseWeight, payload.layers, viewDirection);
            scatter.specularWeight =
                attenuateBaseByClearcoat(scatter.specularWeight, payload.layers, viewDirection);
        }

        scatter.diffuseWeight /= scatter.pmf;
        scatter.specularWeight /= scatter.pmf;
        scatter.weight = scatter.diffuseWeight + scatter.specularWeight;
        scatter.valid = any(scatter.weight > float3(0.0f)) ? 1u : 0u;
        return scatter;
    }

    float absNoL = abs(dot(normal, scatter.direction));
    if (absNoL <= kMaterialMinCosTheta)
    {
        scatter.valid = 0u;
        return scatter;
    }

    float pdf = resolvedMaterialCombinedPdfVariant<LayerFlags, BaseLobeVariant>(
        payload,
        viewDirection,
        scatter.direction);
    if (pdf <= 1.0e-6f)
    {
        scatter.valid = 0u;
        return scatter;
    }

    ResolvedMaterialBsdfComponents bsdf =
        evaluateResolvedMaterialBsdfComponentsVariant<LayerFlags, BaseLobeVariant>(
        payload,
        viewDirection,
        scatter.direction);
    scatter.diffuseWeight = bsdf.diffuse * absNoL / pdf;
    scatter.specularWeight = bsdf.specular * absNoL / pdf;
    scatter.weight = scatter.diffuseWeight + scatter.specularWeight;
    scatter.valid = any(scatter.weight > float3(0.0f)) ? 1u : 0u;
    scatter.pdf = pdf;
    scatter.pmf = 0.0f;
    return scatter;
}

public float3 evaluateResolvedMaterialBsdf(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float3 lightDirection)
{
    return evaluateResolvedMaterialBsdfVariant<
        kResolvedMaterialLayerVariantMask,
        RtBaseLobeVariant.anisotropic>(
        payload,
        viewDirection,
        lightDirection);
}

public ResolvedMaterialScatter sampleResolvedMaterialScatter(
    ResolvedMaterialPayload payload,
    float3 viewDirection,
    float4 randomValues)
{
    return sampleResolvedMaterialScatterVariant<
        kResolvedMaterialLayerVariantMask,
        RtBaseLobeVariant.anisotropic>(
        payload,
        viewDirection,
        randomValues);
}
