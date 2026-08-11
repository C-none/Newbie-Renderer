module fixture;
import common;
import include.neuralAppearance.layout;

public struct NeuralAppearanceFixture
{
    public float3 f0 = float3(0.04f);
    public float roughness = 0.5f;
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public float anisotropyStrength = 0.0f;
    public float3 shadingNormal = float3(0.0f, 1.0f, 0.0f);
    public float anisotropyAngle = 0.0f;
    public float3 anisotropyTangent = float3(1.0f, 0.0f, 0.0f);

    public __init()
    {
    }

    public __init(
        float3 inputF0, float inputRoughness, float3 inputGeometryNormal,
        float inputAnisotropyStrength, float3 inputShadingNormal,
        float inputAnisotropyAngle, float3 inputAnisotropyTangent)
    {
        f0 = inputF0;
        roughness = inputRoughness;
        geometryNormal = inputGeometryNormal;
        anisotropyStrength = inputAnisotropyStrength;
        shadingNormal = inputShadingNormal;
        anisotropyAngle = inputAnisotropyAngle;
        anisotropyTangent = inputAnisotropyTangent;
    }
};

public NeuralAppearanceFixture neuralAppearanceFixture(float2 uv)
{
    float2 phase = 6.28318530718f * frac(uv);
    float3 waves = 0.5f + 0.5f * float3(sin(phase.x), sin(phase.y),
                                        sin(phase.x + phase.y));

    float3 f0 = 0.035f + 0.72f * waves;
    float roughness = 0.18f + 0.52f * (0.5f + 0.5f * cos(phase.x - phase.y));
    float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    float anisotropyStrength =
        0.55f + 0.35f * (0.5f + 0.5f * sin(phase.x + 0.7f * phase.y));

    float2 normalSlope = 0.24f * float2(sin(phase.x), cos(phase.y));
    float3 shadingNormal = safeNormalize(float3(normalSlope.x, 1.0f, normalSlope.y),
                                         geometryNormal);

    float anisotropyAngle = phase.x + 0.65f * sin(phase.y);
    float3 tangentHint = float3(cos(anisotropyAngle), 0.0f, sin(anisotropyAngle));
    float3 anisotropyTangent = safeNormalize(
        tangentHint - shadingNormal * dot(shadingNormal, tangentHint),
        orthogonalTangent(shadingNormal));
    return NeuralAppearanceFixture(
        f0, roughness, geometryNormal, anisotropyStrength,
        shadingNormal, anisotropyAngle, anisotropyTangent);
}

public NeuralAppearanceLatent neuralAppearanceFixtureLatent(float2 uv)
{
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(uv);
    return NeuralAppearanceLatent(
        float4(fixture.f0, fixture.roughness),
        float4(fixture.shadingNormal.x, fixture.shadingNormal.z,
               cos(fixture.anisotropyAngle), sin(fixture.anisotropyAngle)));
}

public NeuralAppearanceLatent neuralAppearanceFixtureLatentTexel(uint2 texel)
{
    float2 size = float2(kNeuralAppearanceLatentWidth, kNeuralAppearanceLatentHeight);
    return neuralAppearanceFixtureLatent((float2(texel) + 0.5f) / size);
}
