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
};

public struct MaterialSample
{
    public float4 baseColor = float4(1.0f);
    public float metallic = 0.0f;
    public float roughness = 1.0f;
    public float3 emissive = float3(0.0f);
    public float alpha = 1.0f;
};

public struct MaterialLayerStack
{
    public float clearcoat = 0.0f;
    public float clearcoatRoughness = 0.0f;
    public float3 clearcoatNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 sheenColor = float3(0.0f);
    public float sheenRoughness = 0.0f;
    public float transmission = 0.0f;
};

public struct MaterialDirectLightingInput
{
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 lightRadiance = float3(0.0f);
};

public struct MaterialShadingResult
{
    public float3 radiance = float3(0.0f);
    public float alpha = 1.0f;
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
