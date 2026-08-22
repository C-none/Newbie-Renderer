module fixture;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

// The V3 model is trained across the whole reflective base-surface parameter
// space instead of one spatially varying material, so the fixture is a sampler
// over that space. The uv variant is a continuous slice used by the preview
// viewer; the seeded variant is the training and held-out distribution.
public struct NeuralAppearanceFixture
{
    public float3 baseColor = float3(0.5f);
    public float metallic = 0.0f;
    public float roughness = 0.5f;
    public float3 geometryNormal = float3(0.0f, 1.0f, 0.0f);
    public float anisotropyStrength = 0.0f;
    public float3 shadingNormal = float3(0.0f, 1.0f, 0.0f);
    public float3 anisotropyTangent = float3(1.0f, 0.0f, 0.0f);

    public __init()
    {
    }

    public __init(
        float3 inputBaseColor, float inputMetallic, float inputRoughness,
        float inputAnisotropyStrength, float3 inputShadingNormal, float3 inputAnisotropyTangent)
    {
        baseColor = inputBaseColor;
        metallic = inputMetallic;
        roughness = inputRoughness;
        anisotropyStrength = inputAnisotropyStrength;
        shadingNormal = inputShadingNormal;
        anisotropyTangent = inputAnisotropyTangent;
    }
};

NeuralAppearanceFixture neuralAppearanceFixtureFromUnitValues(
    float3 baseColorUnit, float metallicUnit, float roughnessUnit, float2 normalDiskUnit,
    float anisotropyStrengthUnit, float anisotropyAngleUnit)
{
    float3 baseColor = lerp(float3(0.02f), float3(1.0f), saturate(baseColorUnit));
    float metallic = saturate(metallicUnit);
    float roughness = lerp(kMaterialMinRoughness, 1.0f, saturate(roughnessUnit));
    float anisotropyStrength = saturate(anisotropyStrengthUnit);

    // Sampling the projected XZ disk uniformly and lifting its remaining Y
    // component covers the complete geometric upper hemisphere. The sqrt on
    // the radius compensates the polar-area Jacobian.
    float normalDiskRadiusSquared = saturate(normalDiskUnit.x);
    float normalDiskRadius = sqrt(normalDiskRadiusSquared);
    float normalDiskAngle = kNeuralAppearanceTrainingTwoPi * saturate(normalDiskUnit.y);
    float2 normalDisk = normalDiskRadius * float2(cos(normalDiskAngle), sin(normalDiskAngle));
    float3 shadingNormal = safeNormalize(
        float3(normalDisk.x, sqrt(saturate(1.0f - normalDiskRadiusSquared)), normalDisk.y),
        float3(0.0f, 1.0f, 0.0f));

    float anisotropyAngle = kNeuralAppearanceTrainingTwoPi * saturate(anisotropyAngleUnit);
    float3 tangentHint = float3(cos(anisotropyAngle), 0.0f, sin(anisotropyAngle));
    float3 anisotropyTangent = safeNormalize(
        tangentHint - shadingNormal * dot(shadingNormal, tangentHint),
        orthogonalTangent(shadingNormal));

    return NeuralAppearanceFixture(
        baseColor, metallic, roughness, anisotropyStrength, shadingNormal, anisotropyTangent);
}

public NeuralAppearanceFixture neuralAppearanceFixtureSample(uint sampleSeed)
{
    return neuralAppearanceFixtureFromUnitValues(
        float3(neuralAppearanceTrainingRandom(sampleSeed, 0u),
               neuralAppearanceTrainingRandom(sampleSeed, 1u),
               neuralAppearanceTrainingRandom(sampleSeed, 2u)),
        neuralAppearanceTrainingRandom(sampleSeed, 3u),
        neuralAppearanceTrainingRandom(sampleSeed, 4u),
        float2(neuralAppearanceTrainingRandom(sampleSeed, 5u),
               neuralAppearanceTrainingRandom(sampleSeed, 6u)),
        neuralAppearanceTrainingRandom(sampleSeed, 7u),
        neuralAppearanceTrainingRandom(sampleSeed, 8u));
}

public NeuralAppearanceFixture neuralAppearanceFixture(float2 uv)
{
    float2 phase = kNeuralAppearanceTrainingTwoPi * frac(uv);
    float3 waves = 0.5f + 0.5f * float3(sin(phase.x), sin(phase.y), sin(phase.x + phase.y));
    return neuralAppearanceFixtureFromUnitValues(
        waves,
        0.5f + 0.5f * sin(1.5f * phase.x - phase.y),
        0.5f + 0.5f * cos(phase.x - phase.y),
        float2(0.5f + 0.5f * sin(phase.x), 0.5f + 0.5f * cos(phase.y)),
        0.5f + 0.5f * sin(phase.x + 0.7f * phase.y),
        frac((phase.x + 0.65f * sin(phase.y)) / kNeuralAppearanceTrainingTwoPi));
}

public NeuralAppearanceMaterialInput neuralAppearanceMaterialInput(NeuralAppearanceFixture fixture)
{
    return NeuralAppearanceMaterialInput(
        fixture.baseColor, fixture.metallic, fixture.roughness, fixture.shadingNormal,
        fixture.anisotropyTangent, fixture.anisotropyStrength);
}
