implementing common;

public float schlickWeight(float cosine)
{
    float value = saturate(1.0f - abs(cosine));
    float value2 = value * value;
    return value2 * value2 * value;
}

public float3 fresnelSchlick(float3 f0, float cosine)
{
    return f0 + (float3(1.0f) - f0) * schlickWeight(cosine);
}

public float fresnelSchlick(float f0, float cosine)
{
    return f0 + (1.0f - f0) * schlickWeight(cosine);
}

public float ggxDistribution(float alpha, float noH)
{
    float a2 = alpha * alpha;
    float d = ((noH * a2 - noH) * noH + 1.0f);
    return a2 / max(kMaterialPi * d * d, 1.0e-7f);
}

public float ggxSmithG1(float alpha, float noX)
{
    if (noX <= 0.0f)
    {
        return 0.0f;
    }

    float a2 = alpha * alpha;
    float noX2 = noX * noX;
    float tan2 = max(1.0f - noX2, 0.0f) / max(noX2, 1.0e-7f);
    return 2.0f / (1.0f + sqrt(1.0f + a2 * tan2));
}

// Joint height correlation keeps the masking-shadowing term coupled between the incident and
// outgoing directions. This is G2, not the visibility term G2 / (4 NoV NoL).
public float smithGGX(float alpha, float noV, float noL)
{
    if (noV <= 0.0f || noL <= 0.0f)
    {
        return 0.0f;
    }

    float a2 = alpha * alpha;
    float lenV = sqrt(max(a2 + (1.0f - a2) * noV * noV, 0.0f));
    float lenL = sqrt(max(a2 + (1.0f - a2) * noL * noL, 0.0f));
    return
        2.0f * noV * noL /
        max(noL * lenV + noV * lenL, 1.0e-7f);
}

public struct GgxSpecularEnergyTerms
{
    // W compensates the single-scattering GGX lobe; E is its compensated directional albedo.
    public float3 W = float3(1.0f);
    public float3 E = float3(1.0f);
};

// Analytic form of UE's split-sum GGX directional-albedo lookup. It avoids introducing a shader
// resource solely for the energy LUT while preserving the same Spec.W / Spec.E construction.
public float2 ggxDirectionalAlbedoAnalytic(float roughness, float noV)
{
    float r = max(roughness, kMaterialMinRoughness);
    float c = saturate(noV);
    float directionalAlbedo =
        1.0f -
        saturate(
            pow(r, c / r) *
            ((r * c + 0.0266916f) / (0.466495f + c)));
    float fresnelDirectionalAlbedo =
        schlickWeight(c) *
        pow(
            2.36651f * pow(c, 4.7703f * r) + 0.0387332f,
            r);
    return float2(directionalAlbedo, fresnelDirectionalAlbedo);
}

public GgxSpecularEnergyTerms computeGgxSpecularEnergyTerms(
    float roughness,
    float noV,
    float3 f0,
    float3 f90)
{
    float2 splitSum = ggxDirectionalAlbedoAnalytic(roughness, noV);
    float safeDirectionalAlbedo = max(splitSum.x, 1.0e-4f);

    GgxSpecularEnergyTerms result;
    result.W =
        float3(1.0f) +
        f0 * ((1.0f - safeDirectionalAlbedo) / safeDirectionalAlbedo);
    result.E =
        result.W *
        (splitSum.x * f0 + splitSum.y * (f90 - f0));
    return result;
}

public float3 metallicRoughnessF0(MaterialSample material)
{
    return lerp(float3(0.04f), material.baseColor.rgb, material.metallic);
}

public float3 evaluateMetallicRoughnessDiffuse(
    MaterialSample material,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection)
{
    float noV = saturate(dot(normal, viewDirection));
    float noL = saturate(dot(normal, lightDirection));
    if (noV <= kMaterialMinCosTheta || noL <= kMaterialMinCosTheta)
    {
        return float3(0.0f);
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float voH = saturate(dot(viewDirection, halfVector));

    float3 cDiff = material.baseColor.rgb * (1.0f - material.metallic);
    float3 f0 = metallicRoughnessF0(material);
    float3 fresnel = fresnelSchlick(f0, voH);
    return (float3(1.0f) - fresnel) * cDiff * kMaterialInvPi;
}

public float3 evaluateMetallicRoughnessSpecular(
    MaterialSample material,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection)
{
    float noV = saturate(dot(normal, viewDirection));
    float noL = saturate(dot(normal, lightDirection));
    if (noV <= kMaterialMinCosTheta || noL <= kMaterialMinCosTheta)
    {
        return float3(0.0f);
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float noH = saturate(dot(normal, halfVector));
    float voH = saturate(dot(viewDirection, halfVector));
    float alpha = material.roughness * material.roughness;

    float3 f0 = metallicRoughnessF0(material);
    float3 fresnel = fresnelSchlick(f0, voH);
    float distribution = ggxDistribution(alpha, noH);
    float geometry = smithGGX(alpha, noV, noL);
    return fresnel * distribution * geometry / max(4.0f * noV * noL, 1.0e-6f);
}

public float3 evaluateMetallicRoughnessBsdf(
    MaterialSample material,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection)
{
    return evaluateMetallicRoughnessDiffuse(material, normal, viewDirection, lightDirection) +
        evaluateMetallicRoughnessSpecular(material, normal, viewDirection, lightDirection);
}

public float sheenDistribution(float roughness, float noH)
{
    float safeRoughness = max(roughness, kMaterialMinRoughness);
    float invAlpha = 1.0f / safeRoughness;
    float cos2h = noH * noH;
    float sin2h = max(1.0f - cos2h, 0.0078125f);
    return (2.0f + invAlpha) * pow(sin2h, invAlpha * 0.5f) / (2.0f * kMaterialPi);
}

public float sheenL(float roughness, float x)
{
    float r = (1.0f - roughness) * (1.0f - roughness);
    float oneMinusR = 1.0f - r;

    float a = 25.3245f * r + 21.5473f * oneMinusR;
    float b = 3.32435f * r + 3.82987f * oneMinusR;
    float c = 0.16801f * r + 0.19823f * oneMinusR;
    float d = -1.27393f * r - 1.97760f * oneMinusR;
    float e = -4.85967f * r - 4.32054f * oneMinusR;

    return a / (1.0f + b * pow(x, c)) + d * x + e;
}

public float sheenLambda(float roughness, float cosine)
{
    float clampedCosine = clamp(cosine, 1.0e-4f, 1.0f);
    if (clampedCosine < 0.5f)
    {
        return exp(sheenL(roughness, clampedCosine));
    }

    return exp(2.0f * sheenL(roughness, 0.5f) - sheenL(roughness, 1.0f - clampedCosine));
}

public float sheenVisibility(float roughness, float noV, float noL)
{
    float lambdaV = sheenLambda(roughness, noV);
    float lambdaL = sheenLambda(roughness, noL);
    return 1.0f / (1.0f + pow(lambdaV, 1.0f + 2.0f * pow(1.0f - noV, 8.0f)) + lambdaL);
}

public float3 evaluateSheenBsdf(
    MaterialLayerStack layers,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection)
{
    if (max(max(layers.sheenColor.r, layers.sheenColor.g), layers.sheenColor.b) <= 0.0f)
    {
        return float3(0.0f);
    }

    float noV = saturate(dot(normal, viewDirection));
    float noL = saturate(dot(normal, lightDirection));
    if (noV <= kMaterialMinCosTheta || noL <= kMaterialMinCosTheta)
    {
        return float3(0.0f);
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float noH = saturate(dot(normal, halfVector));
    float distribution = sheenDistribution(layers.sheenRoughness, noH);
    float visibility = sheenVisibility(layers.sheenRoughness, noV, noL);
    return layers.sheenColor * distribution * visibility / max(4.0f * noV * noL, 1.0e-6f);
}

public float evaluateClearcoatSpecular(
    float clearcoatRoughness,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float energyWeight)
{
    float noV = saturate(dot(normal, viewDirection));
    float noL = saturate(dot(normal, lightDirection));
    if (noV <= kMaterialMinCosTheta || noL <= kMaterialMinCosTheta)
    {
        return 0.0f;
    }

    float3 halfVector = safeNormalize(viewDirection + lightDirection, normal);
    float noH = saturate(dot(normal, halfVector));
    float voH = saturate(dot(viewDirection, halfVector));
    float alpha = clearcoatRoughness * clearcoatRoughness;
    float fresnel = fresnelSchlick(0.04f, voH);
    float distribution = ggxDistribution(alpha, noH);
    float geometry = smithGGX(alpha, noV, noL);
    return
        energyWeight * fresnel * distribution * geometry /
        max(4.0f * noV * noL, 1.0e-6f);
}

public GgxSpecularEnergyTerms clearcoatGgxEnergyTerms(
    MaterialLayerStack layers,
    float3 clearcoatSpecularNormal,
    float3 viewDirection)
{
    float noV = saturate(dot(clearcoatSpecularNormal, viewDirection));
    return computeGgxSpecularEnergyTerms(
        layers.clearcoatRoughness,
        noV,
        float3(0.04f),
        float3(1.0f));
}

public float clearcoatBaseAttenuation(
    MaterialLayerStack layers,
    float3 clearcoatSpecularNormal,
    float3 viewDirection)
{
    if (layers.clearcoat <= 0.0f)
    {
        return 1.0f;
    }

    GgxSpecularEnergyTerms energy = clearcoatGgxEnergyTerms(
        layers,
        clearcoatSpecularNormal,
        viewDirection);
    return saturate(1.0f - layers.clearcoat * energy.E.x);
}

public float3 evaluateClearcoatDirect(
    MaterialLayerStack layers,
    float3 clearcoatSpecularNormal,
    float3 viewDirection,
    float3 lightDirection,
    float3 lightRadiance)
{
    if (layers.clearcoat <= 0.0f)
    {
        return float3(0.0f);
    }

    float noL = saturate(dot(clearcoatSpecularNormal, lightDirection));
    GgxSpecularEnergyTerms energy = clearcoatGgxEnergyTerms(
        layers,
        clearcoatSpecularNormal,
        viewDirection);
    float coatSpecular = evaluateClearcoatSpecular(
        layers.clearcoatRoughness,
        clearcoatSpecularNormal,
        viewDirection,
        lightDirection,
        energy.W.x) * layers.clearcoat;
    return float3(coatSpecular) * lightRadiance * noL;
}
