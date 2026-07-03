implementing common;

public static const uint kSceneLightAbiVersion = 2u;
public static const uint kSceneLightTypeDirectional = 0u;
public static const uint kSceneLightTypePoint = 1u;
public static const uint kSceneLightTypeSpot = 2u;
public static const uint kSceneLightFlagCastShadow = 1u << 0u;

public struct SceneLightGpuHeader
{
    public uint abiVersion;
    public uint lightCount;
    public uint aliasCount;
    public float totalEnergy;
};

public struct SceneLightGpuRecord
{
    // meta.x: light type, meta.y: flags, meta.z: stable/debug instance id.
    public uint4 meta;
    public float4 colorIntensity;
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

[[vk::binding(0, 6)]]
public ConstantBuffer<SceneLightGpuHeader> gSceneLightHeader;

[[vk::binding(1, 6)]]
public StructuredBuffer<SceneLightGpuRecord> gSceneLights;

[[vk::binding(2, 6)]]
public StructuredBuffer<SceneLightAliasGpuRecord> gSceneLightAliasTable;

public static const float kSceneLightMinDistanceSquared = 1.0e-6f;
public static const float3 kSceneLightDefaultDirection = float3(0.0f, 0.0f, -1.0f);

public struct SceneLightContribution
{
    public float3 direction;
    public float3 radiance;
};

public struct SceneLightSample
{
    public uint lightIndex;
    public float pdf;
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

public SceneLightContribution sceneLightContributionZero()
{
    SceneLightContribution contribution;
    contribution.direction = float3(0.0f, 1.0f, 0.0f);
    contribution.radiance = float3(0.0f);
    return contribution;
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

public SceneLightSample sceneLightSampleZero()
{
    SceneLightSample sample;
    sample.lightIndex = 0u;
    sample.pdf = 0.0f;
    return sample;
}

public bool sampleSceneLightAlias(float2 randomValues, out SceneLightSample sample)
{
    sample = sceneLightSampleZero();

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

public float sceneLightDistanceAttenuation(float distanceSquared)
{
    return 1.0f / max(distanceSquared, kSceneLightMinDistanceSquared);
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
    contribution = sceneLightContributionZero();

    float3 color = max(light.colorIntensity.rgb, float3(0.0f));
    float intensity = max(light.colorIntensity.w, 0.0f);
    if (intensity <= 0.0f)
    {
        return false;
    }

    uint lightType = light.meta.x;
    if (lightType == kSceneLightTypeDirectional)
    {
        contribution.direction = sceneLightSafeNormalize(-light.direction.xyz, -kSceneLightDefaultDirection);
        contribution.radiance = color * intensity;
        return dot(contribution.radiance, contribution.radiance) > 0.0f;
    }

    if (lightType != kSceneLightTypePoint && lightType != kSceneLightTypeSpot)
    {
        return false;
    }

    float3 toLight = light.positionRange.xyz - hitPosition;
    float distanceSquared = max(dot(toLight, toLight), kSceneLightMinDistanceSquared);
    float attenuation = sceneLightDistanceAttenuation(distanceSquared);

    float3 surfaceToLightDirection = toLight * rsqrt(distanceSquared);
    if (lightType == kSceneLightTypeSpot)
    {
        attenuation *= sceneLightSpotConeAttenuation(light, -surfaceToLightDirection);
        if (attenuation <= 0.0f)
        {
            return false;
        }
    }

    contribution.direction = surfaceToLightDirection;
    contribution.radiance = color * intensity * attenuation;
    return dot(contribution.radiance, contribution.radiance) > 0.0f;
}

public bool evaluateSceneLightAt(uint lightIndex, float3 hitPosition, out SceneLightContribution contribution)
{
    contribution = sceneLightContributionZero();
    if (lightIndex >= sceneLightCount())
    {
        return false;
    }

    return evaluateSceneLight(gSceneLights[lightIndex], hitPosition, contribution);
}
