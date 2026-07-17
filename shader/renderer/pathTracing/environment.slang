module environment;
import common;

public struct EnvironmentMapParameters
{
    float radianceDecodeScale;
    float intensity;
    float yawRadians;
    float padding;
};

public Sampler2D<float4> gEnvironmentMap;

[[vk::push_constant]]
public ConstantBuffer<EnvironmentMapParameters> gEnvironment;

public float3 sampleEnvironment(float3 direction)
{
    static const float kPi = 3.14159265358979323846f;
    static const float kInversePi = 1.0f / kPi;
    static const float kInverseTwoPi = 0.5f * kInversePi;

    float3 unitDirection = normalize(direction);
    float longitude = atan2(unitDirection.x, -unitDirection.z);
    float2 uv = float2(
        frac(0.5f + (longitude + gEnvironment.yawRadians) * kInverseTwoPi),
        acos(clamp(unitDirection.y, -1.0f, 1.0f)) * kInversePi);
    float3 encodedRadiance = gEnvironmentMap.SampleLevel(uv, 0.0f).rgb;
    return encodedRadiance * gEnvironment.radianceDecodeScale * gEnvironment.intensity;
}
