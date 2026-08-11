module guides;
import common;
import renderer.pathTracing.resources;

public static const float kPathTracingGuideHalfMax = 65504.0f;

public float pathTracingGuideSafeDivideW(float w)
{
    if (abs(w) > 1.0e-6f)
    {
        return w;
    }
    return w < 0.0f ? -1.0e-6f : 1.0e-6f;
}

public float2 pathTracingGuideClipToPixel(float4 clip, uint2 dimensions)
{
    float2 ndc = clip.xy / pathTracingGuideSafeDivideW(clip.w);
    float2 uv = float2(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    return uv * float2(dimensions);
}

public float2 pathTracingGuideSurfaceMotionVector(float3 position, uint2 dimensions)
{
    float4 currentClip = mul(float4(position, 1.0f), gFrame.unjitteredViewProjection);
    float4 previousClip = mul(float4(position, 1.0f), gFrame.previousViewProjection);
    float2 motion =
        pathTracingGuideClipToPixel(previousClip, dimensions) - pathTracingGuideClipToPixel(currentClip, dimensions);
    return clamp(motion, float2(-kPathTracingGuideHalfMax), float2(kPathTracingGuideHalfMax));
}

public float2 pathTracingGuideSkyMotionVector(float3 worldDirection, uint2 dimensions)
{
    float4 direction = float4(worldDirection, 0.0f);
    float4 currentClip = mul(direction, gFrame.unjitteredViewProjection);
    float4 previousClip = mul(direction, gFrame.previousViewProjection);
    float2 motion =
        pathTracingGuideClipToPixel(previousClip, dimensions) - pathTracingGuideClipToPixel(currentClip, dimensions);
    return clamp(motion, float2(-kPathTracingGuideHalfMax), float2(kPathTracingGuideHalfMax));
}

public struct PathTracingGuideState
{
    public float depth = 1.0f;
    public float3 diffuseAlbedo = float3(0.0f);
    public float3 specularAlbedo = float3(0.5f);
    public float4 normalRoughness = float4(0.0f, 0.0f, 1.0f, 1.0f);
    public float2 motionVectors = float2(0.0f);
    public float specularHitDistance = 0.0f;
    public float3 primaryPosition = float3(0.0f);
    public uint awaitingSpecularHit = 0u;

    public __init()
    {
    }

    public __init(float3 rayDirection, uint2 dimensions)
    {
        normalRoughness = float4(safeNormalize(-rayDirection, float3(0.0f, 0.0f, 1.0f)), 1.0f);
        motionVectors = pathTracingGuideSkyMotionVector(rayDirection, dimensions);
    }
};

public float3 pathTracingGuideEnvBrdfApprox2(float3 specularColor, float alpha, float noV)
{
    noV = abs(noV);
    float4 x = float4(1.0f, noV, noV * noV, noV * noV * noV);
    float4 y = float4(1.0f, alpha, alpha * alpha, alpha * alpha * alpha);

    // Coefficients are the transpose of the published column-vector matrices so vector-first mul preserves BRDF.
    float2x2 m1 = float2x2(0.99044f, 1.29678f, -1.28514f, -0.755907f);
    float3x3 m2 = float3x3(1.0f, 20.3225f, 121.563f, 2.92338f, -27.0302f, 626.13f, 59.4188f, 222.592f, 316.627f);
    float2x2 m3 = float2x2(0.0365463f, 9.0632f, 3.32707f, -9.04756f);
    float3x3 m4 = float3x3(1.0f, 9.04401f, 5.56589f, 3.59685f, -16.3174f, 19.7886f, -1.36772f, 9.22949f, -20.2123f);

    float biasDenominator = dot(mul(x.xyw, m2), y.xyw);
    float scaleDenominator = dot(mul(x.xzw, m4), y.xyw);
    float bias = dot(mul(x.xy, m1), y.xy) / pathTracingGuideSafeDivideW(biasDenominator);
    float scale = dot(mul(x.xy, m3), y.xy) / pathTracingGuideSafeDivideW(scaleDenominator);
    bias *= saturate(specularColor.g * 50.0f);
    return specularColor * max(0.0f, scale) + max(0.0f, bias);
}

public float3 pathTracingGuidePrimarySpecularF0(ResolvedMaterialPayload material, MaterialSample core)
{
    if (uint(material.layerFlags & RtMaterialLayerFlag.transmission) == 0u)
    {
        return metallicRoughnessF0(core);
    }

    float ior = material.layers.transmissionIor;
    float dielectricF0 = 1.0f;
    if (ior > 0.0f)
    {
        float ratio = (ior - 1.0f) / max(ior + 1.0f, 1.0e-7f);
        dielectricF0 = saturate(ratio * ratio);
    }
    return lerp(float3(dielectricF0), core.baseColor.rgb, core.metallic);
}

public void capturePathTracingPrimarySurfaceGuides(inout PathTracingGuideState guides, ResolvedMaterialPayload material,
                                                   float3 viewDirection, uint2 dimensions)
{
    float3 normal = safeNormalize(material.shadingNormal, material.geometryNormal);
    float4 clip = mul(float4(material.position, 1.0f), gFrame.viewProjection);

    guides.depth = saturate(clip.z / pathTracingGuideSafeDivideW(clip.w));
    guides.normalRoughness = float4(normal, saturate(material.roughness));
    guides.motionVectors = pathTracingGuideSurfaceMotionVector(material.position, dimensions);
    guides.primaryPosition = material.position;

    if (material.layerFlags == RtMaterialLayerFlag.none)
    {
        guides.diffuseAlbedo = max(material.baseColor.rgb, float3(0.0f));
        guides.specularAlbedo = float3(0.0f);
        return;
    }

    guides.diffuseAlbedo = max(material.baseColor.rgb * (1.0f - saturate(material.metallic)), float3(0.0f));

    MaterialSample core = materialSampleFromResolvedPayload(material);
    float3 specularColor = pathTracingGuidePrimarySpecularF0(material, core);
    float alpha = material.roughness * material.roughness;
    float3 specularNormal =
        adjustMaterialPayloadSpecularNormal(material.shadingNormal, material.geometryNormal, viewDirection);
    float noV = saturate(dot(specularNormal, viewDirection));
    guides.specularAlbedo = max(pathTracingGuideEnvBrdfApprox2(specularColor, alpha, noV), float3(0.0f));
}

public bool pathTracingGuideIsSpecularReflection(ResolvedMaterialScatterKind kind)
{
    return kind == ResolvedMaterialScatterKind.specular || kind == ResolvedMaterialScatterKind.clearcoat;
}

public void beginPathTracingSpecularHitTracking(inout PathTracingGuideState guides, ResolvedMaterialScatterKind kind)
{
    guides.awaitingSpecularHit = pathTracingGuideIsSpecularReflection(kind) ? 1u : 0u;
}

public void resolvePathTracingSpecularHit(inout PathTracingGuideState guides, float3 hitPosition)
{
    if (guides.awaitingSpecularHit == 0u)
    {
        return;
    }

    guides.specularHitDistance = min(length(hitPosition - guides.primaryPosition), kPathTracingGuideHalfMax);
    guides.awaitingSpecularHit = 0u;
}

public void writePathTracingGuides(uint2 pixel, PathTracingGuideState guides)
{
    int2 location = int2(pixel);
    depthImage[location] = guides.depth;
    diffuseAlbedoImage[location] = float4(guides.diffuseAlbedo, 1.0f);
    specularAlbedoImage[location] = float4(guides.specularAlbedo, 1.0f);
    normalRoughnessImage[location] = guides.normalRoughness;
    motionVectorsImage[location] = guides.motionVectors;
    specularHitDistanceImage[location] = guides.specularHitDistance;
}
