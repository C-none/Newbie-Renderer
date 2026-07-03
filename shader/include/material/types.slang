implementing common;

public static const uint kAlphaModeOpaque = 0u;
public static const uint kAlphaModeMask = 1u;
public static const uint kAlphaModeBlend = 2u;

public static const float kMaterialPi = 3.14159265358979323846f;
public static const float kMaterialInvPi = 0.31830988618379067154f;
public static const float kMaterialMinRoughness = 0.045f;
public static const float kMaterialMinCosTheta = 1.0e-5f;

public struct RtSurfaceData
{
    public float3 position;
    public float3 normal;
    public float3 tangent;
    public float tangentSign;
    public float2 texCoord0;
    public float2 texCoord1;
    public float4 color0;
};

public struct MaterialSample
{
    public float4 baseColor;
    public float metallic;
    public float roughness;
    public float3 emissive;
    public float alpha;
};

public struct MaterialLayerStack
{
    public float clearcoat;
    public float clearcoatRoughness;
    public float3 clearcoatNormal;
    public float3 sheenColor;
    public float sheenRoughness;
};

public struct MaterialDirectLightingInput
{
    public float3 viewDirection;
    public float3 lightDirection;
    public float3 lightRadiance;
};

public struct MaterialShadingResult
{
    public float3 radiance;
    public float alpha;
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

public float2 materialUv(uint uvSet, float2 texCoord0, float2 texCoord1)
{
    return uvSet == 1u ? texCoord1 : texCoord0;
}

public float3 orthogonalTangent(float3 normal)
{
    float3 axis = abs(normal.z) < 0.999f ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
    return safeNormalize(cross(axis, normal), float3(1.0f, 0.0f, 0.0f));
}
