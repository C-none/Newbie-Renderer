implementing common;

public static const uint kSceneLightAbiVersion = 3u;

public enum SceneLightType : uint
{
    directional = 0u,
    point = 1u,
    spot = 2u,
}

[Flags]
public enum SceneLightFlag : uint
{
    none = 0u,
    castShadow = 1u << 0u,
}

public struct SceneLightGpuHeader
{
    public uint abiVersion = kSceneLightAbiVersion;
    public uint lightCount = 0u;
    public uint aliasCount = 0u;
    // Sampling weight sum only; not a physical energy unit.
    public float totalEnergy = 0.0f;
};

public struct SceneLightGpuRecord
{
    // meta.x: light type, meta.y: flags, meta.z: stable/debug instance id.
    public uint4 meta;
    // glTF KHR_lights_punctual:
    // rgb is the unitless linear RGB color multiplier.
    // w is intensity: lux for directional lights, candela for point/spot lights.
    public float4 colorIntensity;
    // xyz is point/spot world position. w is glTF range in meters; <=0 is infinite.
    public float4 positionRange;
    public float4 direction;
    public float4 spotCone;
};

public struct SceneLightAliasGpuRecord
{
    // meta.x: primary light index, meta.y: alias light index.
    public uint4 meta;
    // probabilities.x: alias accept threshold, probabilities.y: primary pdf, probabilities.z: alias pdf.
    public float4 probabilities;
};

[[vk::binding(0, 5)]]
public ConstantBuffer<SceneLightGpuHeader> gSceneLightHeader;

[[vk::binding(1, 5)]]
public StructuredBuffer<SceneLightGpuRecord> gSceneLights;

[[vk::binding(2, 5)]]
public StructuredBuffer<SceneLightAliasGpuRecord> gSceneLightAliasTable;

public static const float kSceneLightMinDistanceSquared = 1.0e-6f;
public static const float3 kSceneLightDefaultDirection = float3(0.0f, 0.0f, -1.0f);

public struct SceneLightContribution
{
    public float3 direction = float3(0.0f, 1.0f, 0.0f);
    // Scene-linear light value used by material BSDF evaluation. It is derived
    // from glTF photometric units and remains HDR until the final present
    // shader clamps and optionally applies the sRGB transfer curve for pixels.
    public float3 radiance = float3(0.0f);
};

public struct SceneLightSample
{
    public uint lightIndex = 0u;
    public float pdf = 0.0f;
};

public float3 sceneLightSafeNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    if (lengthSquared > kSceneLightMinDistanceSquared)
    {
        return value * rsqrt(lengthSquared);
    }

    return fallback;
}

public bool sceneLightHeaderValid()
{
    return gSceneLightHeader.abiVersion == kSceneLightAbiVersion;
}

public uint sceneLightCount()
{
    return sceneLightHeaderValid() ? gSceneLightHeader.lightCount : 0u;
}

public uint sceneLightAliasCount()
{
    return sceneLightHeaderValid() ? gSceneLightHeader.aliasCount : 0u;
}

public bool sampleSceneLightAlias(float2 randomValues, out SceneLightSample sample)
{
    sample = {};

    uint aliasCount = sceneLightAliasCount();
    uint lightCount = sceneLightCount();
    if (aliasCount == 0u || lightCount == 0u)
    {
        return false;
    }

    uint tableIndex = min(uint(saturate(randomValues.x) * float(aliasCount)), aliasCount - 1u);
    SceneLightAliasGpuRecord aliasRecord = gSceneLightAliasTable[tableIndex];
    bool acceptedPrimary = saturate(randomValues.y) < aliasRecord.probabilities.x;
    sample.lightIndex = acceptedPrimary ? aliasRecord.meta.x : aliasRecord.meta.y;
    sample.pdf = acceptedPrimary ? aliasRecord.probabilities.y : aliasRecord.probabilities.z;

    return sample.lightIndex < lightCount && sample.pdf > 0.0f;
}

public float sceneLightDistanceAttenuation(float distanceSquared, float rangeMeters)
{
    float inverseSquare = 1.0f / max(distanceSquared, kSceneLightMinDistanceSquared);
    if (rangeMeters <= 0.0f)
    {
        return inverseSquare;
    }

    float distance = sqrt(max(distanceSquared, kSceneLightMinDistanceSquared));
    float rangeRatio = distance / rangeMeters;
    float rangeAttenuation = saturate(1.0f - rangeRatio * rangeRatio * rangeRatio * rangeRatio);
    return inverseSquare * rangeAttenuation;
}

public float sceneLightSpotConeAttenuation(SceneLightGpuRecord light, float3 lightToSurfaceDirection)
{
    float cosInnerCone = light.spotCone.z;
    float cosOuterCone = light.spotCone.w;
    float angleScale = 1.0f / max(0.001f, cosInnerCone - cosOuterCone);
    float angleOffset = -cosOuterCone * angleScale;
    float3 spotDirection = sceneLightSafeNormalize(light.direction.xyz, kSceneLightDefaultDirection);
    float cosine = dot(spotDirection, lightToSurfaceDirection);
    float angularAttenuation = saturate(cosine * angleScale + angleOffset);
    return angularAttenuation * angularAttenuation;
}

public bool evaluateSceneLight(SceneLightGpuRecord light, float3 hitPosition, out SceneLightContribution contribution)
{
    contribution = {};

    float3 color = max(light.colorIntensity.rgb, float3(0.0f));
    float intensity = max(light.colorIntensity.w, 0.0f);
    if (intensity <= 0.0f)
    {
        return false;
    }

    SceneLightType lightType = SceneLightType(light.meta.x);
    if (lightType == SceneLightType.directional)
    {
        contribution.direction = sceneLightSafeNormalize(-light.direction.xyz, -kSceneLightDefaultDirection);
        // Directional glTF intensity is illuminance in lux. The path tracer keeps
        // color * lux as the directional incident scene-linear light value.
        contribution.radiance = color * intensity;
        return dot(contribution.radiance, contribution.radiance) > 0.0f;
    }

    if (lightType != SceneLightType.point && lightType != SceneLightType.spot)
    {
        return false;
    }

    float3 toLight = light.positionRange.xyz - hitPosition;
    float distanceSquared = max(dot(toLight, toLight), kSceneLightMinDistanceSquared);
    float attenuation = sceneLightDistanceAttenuation(distanceSquared, max(light.positionRange.w, 0.0f));
    if (attenuation <= 0.0f)
    {
        return false;
    }

    float3 surfaceToLightDirection = toLight * rsqrt(distanceSquared);
    if (lightType == SceneLightType.spot)
    {
        attenuation *= sceneLightSpotConeAttenuation(light, -surfaceToLightDirection);
        if (attenuation <= 0.0f)
        {
            return false;
        }
    }

    contribution.direction = surfaceToLightDirection;
    // Point and spot glTF intensity is luminous intensity in candela. Shading
    // converts it to incident scene-linear light by applying inverse-square
    // distance attenuation and the optional glTF range fade; spot lights also
    // multiply by the cone attenuation.
    contribution.radiance = color * intensity * attenuation;
    return dot(contribution.radiance, contribution.radiance) > 0.0f;
}

public bool evaluateSceneLightAt(uint lightIndex, float3 hitPosition, out SceneLightContribution contribution)
{
    contribution = {};
    if (lightIndex >= sceneLightCount())
    {
        return false;
    }

    return evaluateSceneLight(gSceneLights[lightIndex], hitPosition, contribution);
}
