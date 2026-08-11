module fixture;
import common;
import include.neuralAppearance.layout;

public struct NeuralAppearanceFixture
{
    public float3 f0;
    public float roughness;
    public float3 geometryNormal;
    public float anisotropyStrength;
    public float3 shadingNormal;
    public float anisotropyAngle;
    public float3 anisotropyTangent;
};

public NeuralAppearanceFixture neuralAppearanceFixture(float2 uv)
{
    float2 phase = 6.28318530718f * frac(uv);
    float3 waves = 0.5f + 0.5f * float3(sin(phase.x), sin(phase.y),
                                        sin(phase.x + phase.y));

    NeuralAppearanceFixture fixture;
    fixture.f0 = 0.035f + 0.72f * waves;
    fixture.roughness = 0.18f + 0.52f * (0.5f + 0.5f * cos(phase.x - phase.y));
    fixture.geometryNormal = float3(0.0f, 1.0f, 0.0f);
    fixture.anisotropyStrength = 0.55f + 0.35f * (0.5f + 0.5f * sin(phase.x + 0.7f * phase.y));

    float2 normalSlope = 0.24f * float2(sin(phase.x), cos(phase.y));
    fixture.shadingNormal = safeNormalize(float3(normalSlope.x, 1.0f, normalSlope.y),
                                          fixture.geometryNormal);

    fixture.anisotropyAngle = phase.x + 0.65f * sin(phase.y);
    float3 tangentHint = float3(cos(fixture.anisotropyAngle), 0.0f,
                                sin(fixture.anisotropyAngle));
    fixture.anisotropyTangent = safeNormalize(
        tangentHint - fixture.shadingNormal * dot(fixture.shadingNormal, tangentHint),
        orthogonalTangent(fixture.shadingNormal));
    return fixture;
}

public NeuralAppearanceLatent neuralAppearanceFixtureLatent(float2 uv)
{
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(uv);
    NeuralAppearanceLatent latent;
    latent.chunks[0] = float4(fixture.f0, fixture.roughness);
    latent.chunks[1] = float4(fixture.shadingNormal.x, fixture.shadingNormal.z,
                              cos(fixture.anisotropyAngle), sin(fixture.anisotropyAngle));
    return latent;
}

public NeuralAppearanceLatent neuralAppearanceFixtureLatentTexel(uint2 texel)
{
    float2 size = float2(kNeuralAppearanceLatentWidth, kNeuralAppearanceLatentHeight);
    return neuralAppearanceFixtureLatent((float2(texel) + 0.5f) / size);
}
