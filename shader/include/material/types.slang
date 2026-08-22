implementing common;

public static const float kMaterialPi = 3.14159265358979323846f;
public static const float kMaterialInvPi = 0.31830988618379067154f;
public static const float kMaterialMinRoughness = 0.045f;
public static const float kMaterialMinCosTheta = 1.0e-5f;

public struct RtSurfaceData
{
    public float3 position = float3(0.0f);
    public float3 normal = float3(0.0f, 1.0f, 0.0f);
    public float3 tangent = float3(1.0f, 0.0f, 0.0f);
    public float tangentSign = 1.0f;
    public float2 texCoord0 = float2(0.0f);
    public float2 texCoord1 = float2(0.0f);
    public float4 color0 = float4(1.0f);

    // Keeps the semantic fallback frame available to cross-module `{}` initialization.
    public __init()
    {
    }

    public __init(float3 position, float3 normal, float3 tangent, float tangentSign, float2 texCoord0, float2 texCoord1,
           float4 color0)
    {
        this.position = position;
        this.normal = normal;
        this.tangent = tangent;
        this.tangentSign = tangentSign;
        this.texCoord0 = texCoord0;
        this.texCoord1 = texCoord1;
        this.color0 = color0;
    }
};

public struct MaterialSample
{
    public float4 baseColor = float4(1.0f);
    public float metallic = 0.0f;
    public float roughness = 1.0f;
    public float3 emissive = float3(0.0f);
    public float alpha = 1.0f;
    public float3 dielectricF0 = float3(0.04f);
    public float dielectricF90 = 1.0f;
    public float occlusion = 1.0f;

    public __init(float4 baseColor, float metallic, float roughness, float3 emissive, float alpha,
                  float3 dielectricF0, float dielectricF90, float occlusion)
    {
        this.baseColor = baseColor;
        this.metallic = metallic;
        this.roughness = roughness;
        this.emissive = emissive;
        this.alpha = alpha;
        this.dielectricF0 = dielectricF0;
        this.dielectricF90 = dielectricF90;
        this.occlusion = occlusion;
    }
};

public struct MaterialLayerStack
{
    public float clearcoat = 0.0f;
    public float clearcoatRoughness = 0.0f;
    public float3 clearcoatNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 sheenColor = float3(0.0f);
    public float sheenRoughness = 0.0f;
    public float transmission = 0.0f;
    public RtTransmissionMode transmissionMode = RtTransmissionMode.thin;
    public float transmissionIor = 1.5f;

    // Layer parsing begins from these defaults and conditionally overrides authored layers.
    public __init()
    {
    }

    public __init(float3 clearcoatNormal)
    {
        this.clearcoatNormal = clearcoatNormal;
    }

    public __init(float clearcoat, float clearcoatRoughness, float3 clearcoatNormal, float3 sheenColor,
           float sheenRoughness, float transmission, RtTransmissionMode transmissionMode, float transmissionIor)
    {
        this.clearcoat = clearcoat;
        this.clearcoatRoughness = clearcoatRoughness;
        this.clearcoatNormal = clearcoatNormal;
        this.sheenColor = sheenColor;
        this.sheenRoughness = sheenRoughness;
        this.transmission = transmission;
        this.transmissionMode = transmissionMode;
        this.transmissionIor = transmissionIor;
    }
};

public struct MaterialDirectLightingInput
{
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 lightRadiance = float3(0.0f);

    // Compatibility default for callers that fill lighting fields conditionally.
    public __init()
    {
    }

    public __init(float3 viewDirection, float3 lightDirection, float3 lightRadiance)
    {
        this.viewDirection = viewDirection;
        this.lightDirection = lightDirection;
        this.lightRadiance = lightRadiance;
    }
};

public float3 safeNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    if (lengthSquared > 1.0e-8f)
    {
        return value * rsqrt(lengthSquared);
    }

    return fallback;
}

public float2 selectMaterialUv(uint uvSet, float2 texCoord0, float2 texCoord1)
{
    return uvSet == 1u ? texCoord1 : texCoord0;
}

public float2 transformMaterialUv(float2 uv, float4 uvLinear, float2 uvOffset)
{
    return float2(dot(uvLinear.xy, uv), dot(uvLinear.zw, uv)) + uvOffset;
}

public float2 materialTextureUv(RtMaterialTextureRef textureRef, float2 texCoord0, float2 texCoord1)
{
    return transformMaterialUv(selectMaterialUv(textureRef.uvSet, texCoord0, texCoord1), textureRef.uvLinear,
                               textureRef.uvOffset);
}

public float3 orthogonalTangent(float3 normal)
{
    float3 axis = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    return safeNormalize(cross(axis, normal), float3(1.0f, 0.0f, 0.0f));
}
