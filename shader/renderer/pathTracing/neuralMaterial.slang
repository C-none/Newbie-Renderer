module neuralMaterial;
import common;
import renderer.pathTracing.neuralResources;
import include.neuralAppearance.model;

public static const uint kRtNeuralMaterialInvalidArtifactIndex = 0xffffffffu;

public bool rtNeuralMaterialEnabled(uint materialIndex)
{
    uint referenceCount = 0u;
    uint referenceStride = 0u;
    gRtNeuralMaterialRefs.GetDimensions(referenceCount, referenceStride);
    if (referenceStride == 0u || materialIndex >= referenceCount)
    {
        return false;
    }
    RtNeuralMaterialRef reference = gRtNeuralMaterialRefs[materialIndex];
    return uint(reference.flags & RtNeuralMaterialFlag.enabled) != 0u &&
           reference.artifactIndex != kRtNeuralMaterialInvalidArtifactIndex;
}

public float2 packRtNeuralRepeatUv(float2 uv)
{
    // The artifact contract fixes repeat addressing. Quantizing the transformed
    // UV once lets closest-hit and raygen observe exactly the same address.
    return frac(uv);
}

public uint packRtNeuralRepeatUvUnorm2x16(float2 uv)
{
    return packUnorm2x16(packRtNeuralRepeatUv(uv));
}

public float2 unpackRtNeuralRepeatUvUnorm2x16(uint value)
{
    return unpackUnorm2x16ToFloat(value);
}

public bool rtNeuralProjectedFiniteNonNegative(float3 value)
{
    return all(isfinite(value)) && all(value >= 0.0f);
}

public bool rtNeuralP0Eligible(ResolvedMaterialPayload material, bool neuralEnabled)
{
    return neuralEnabled && material.alphaMode == AlphaMode.opaque &&
           material.layerFlags == RtMaterialLayerFlag.baseSurface && material.anisotropyStrength == 0.0f;
}

public bool rtNeuralP0Eligible(ResolvedMaterialPayload material)
{
    return material.alphaMode == AlphaMode.opaque && material.layerFlags == RtMaterialLayerFlag.baseSurface &&
           material.anisotropyStrength == 0.0f;
}

// The ray payload intentionally discards the proposal PDF, so this correction
// is valid only while closest-hit still owns the full analytic scatter record.
public bool replaceRtAnalyticSpecularWithNeuralProjected(inout ResolvedMaterialScatter scatter,
                                                         float3 neuralProjected)
{
    if (!scatter.valid || scatter.delta || scatter.pdf <= 1.0e-6f || !rtNeuralProjectedFiniteNonNegative(neuralProjected))
    {
        return false;
    }

    scatter.specularWeight = neuralProjected / scatter.pdf;
    scatter.weight = scatter.diffuseWeight + scatter.specularWeight;
    scatter.valid = any(scatter.weight > float3(0.0f));
    return scatter.valid;
}

// NEE uses the same projected numerator as continuation. Keep its replacement
// and invalid-output fallback here so the two estimators cannot drift.
public float3 resolveRtNeuralDirectSpecularRadiance(float3 analyticSpecularRadiance, float3 neuralProjected,
                                                     float3 lightRadiance, bool neuralValid)
{
    return neuralValid && rtNeuralProjectedFiniteNonNegative(neuralProjected)
               ? neuralProjected * lightRadiance
               : analyticSpecularRadiance;
}

public float3 evaluateRtNeuralProjected(float2 repeatUv, float3 tangent, float3 normal, float tangentSign,
                                         float3 viewDirection, float3 lightDirection, out bool valid)
{
    float3 tangentAxis = safeNormalize(tangent - normal * dot(normal, tangent), orthogonalTangent(normal));
    float3 bitangentAxis = safeNormalize(cross(normal, tangentAxis) * (tangentSign < 0.0f ? -1.0f : 1.0f),
                                         cross(normal, tangentAxis));
    float3 artifactViewDirection =
        float3(dot(tangentAxis, viewDirection), dot(normal, viewDirection), dot(bitangentAxis, viewDirection));
    float3 artifactLightDirection =
        float3(dot(tangentAxis, lightDirection), dot(normal, lightDirection), dot(bitangentAxis, lightDirection));
    float3 projected = evaluateNeuralAppearanceRuntime(gNeuralModelParameters, gNeuralLatentTexture0,
                                                        gNeuralLatentTexture1, repeatUv, artifactViewDirection,
                                                        artifactLightDirection);
    valid = rtNeuralProjectedFiniteNonNegative(projected);
    return valid ? projected : float3(0.0f);
}
