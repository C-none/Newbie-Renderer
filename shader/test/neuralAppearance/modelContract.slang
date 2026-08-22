module modelContract;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.trainingSampling;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(0, 2)]]
RWStructuredBuffer<uint> gContractResults;

bool neuralAppearanceValuesMatch(float3 cached, float3 recomputed)
{
    float3 tolerance = 3.0e-5f * (float3(1.0f) + max(abs(cached), abs(recomputed)));
    return all(isfinite(cached)) && all(isfinite(recomputed)) &&
           all(abs(cached - recomputed) <= tolerance);
}

bool neuralAppearanceLobesMatch(NeuralAppearanceLobes cached, NeuralAppearanceLobes recomputed)
{
    return neuralAppearanceValuesMatch(cached.diffuse, recomputed.diffuse) &&
           neuralAppearanceValuesMatch(cached.specular, recomputed.specular) &&
           all(cached.diffuse >= float3(0.0f)) && all(cached.specular >= float3(0.0f));
}

bool neuralAppearanceLobesBounded(NeuralAppearanceLobes lobes)
{
    // The decode saturates smoothly, so both heads must stay inside their cap
    // and therefore inside the finite rgba16f range.
    return all(lobes.diffuse <= float3(exp(kNeuralAppearanceDiffuseLogMaximum))) &&
           all(lobes.specular <= float3(exp(kNeuralAppearanceSpecularLogMaximum)));
}

bool neuralAppearanceFrameUnitFinite(NeuralAppearanceFrame frame)
{
    float3 lengths = float3(length(frame.normal), length(frame.tangent), length(frame.bitangent));
    return all(isfinite(frame.normal)) && all(isfinite(frame.tangent)) &&
           all(isfinite(frame.bitangent)) &&
           all(abs(lengths - float3(1.0f)) <= float3(2.0e-4f));
}

bool neuralAppearanceFrameCrossMatches(NeuralAppearanceFrame frame)
{
    float3 crossValue = cross(frame.normal, frame.tangent);
    float crossLengthSquared = dot(crossValue, crossValue);
    float3 expected = crossLengthSquared >= 1.0e-12f
                          ? crossValue * rsqrt(max(crossLengthSquared, 1.0e-12f))
                          : float3(0.0f, 0.0f, -1.0f);
    return all(abs(expected - frame.bitangent) <= float3(2.0e-4f)) &&
           abs(dot(frame.bitangent, frame.normal)) <= 2.0e-4f &&
           abs(dot(frame.bitangent, frame.tangent)) <= 2.0e-4f;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void modelContractMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    static const float2 materialUv = float2(24.5f / 64.0f, 40.5f / 64.0f);
    NeuralAppearanceFixture fixture = neuralAppearanceFixture(materialUv);
    NeuralAppearanceMaterialInput material = neuralAppearanceMaterialInput(fixture);

    bool materialFinite = true;
    for (uint value = 0u; value < kNeuralAppearanceMaterialInputCount; ++value)
    {
        materialFinite = materialFinite && isfinite(material.values[value]);
    }
    gContractResults[0] = uint(materialFinite);

    float latent[kNeuralAppearanceLatentCount] =
        encodeNeuralAppearanceLatentRuntime(gModelParameters, material);
    bool latentFinite = true;
    for (uint value = 0u; value < kNeuralAppearanceLatentCount; ++value)
    {
        latentFinite = latentFinite && isfinite(latent[value]);
    }
    gContractResults[1] = uint(latentFinite);

    NeuralMaterialContext context = buildNeuralMaterialContextRuntime(gModelParameters, latent);
    bool prefixFinite = true;
    for (uint value = 0u; value < kNeuralAppearanceHiddenCount; ++value)
    {
        prefixFinite = prefixFinite && isfinite(context.spatialPrefix[value]);
    }
    gContractResults[2] = uint(prefixFinite);
    gContractResults[3] = uint(neuralAppearanceFrameUnitFinite(context.frame));
    gContractResults[4] = uint(neuralAppearanceFrameCrossMatches(context.frame));

    float3 viewDirections[4] = {
        safeNormalize(float3(0.15f, 0.98f, 0.09f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(0.71f, 0.62f, -0.33f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.54f, 0.44f, 0.72f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.82f, 0.25f, -0.51f), float3(0.0f, 1.0f, 0.0f)),
    };
    float3 lightDirections[4] = {
        safeNormalize(float3(0.42f, 0.87f, 0.25f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.65f, 0.66f, 0.37f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(0.76f, 0.36f, -0.54f), float3(0.0f, 1.0f, 0.0f)),
        safeNormalize(float3(-0.25f, 0.22f, -0.94f), float3(0.0f, 1.0f, 0.0f)),
    };

    for (uint pair = 0u; pair < 4u; ++pair)
    {
        NeuralAppearanceLobes cached = evaluateNeuralAppearanceRuntime(
            gModelParameters, context, viewDirections[pair], lightDirections[pair]);
        NeuralAppearanceLobes recomputed = evaluateNeuralAppearanceRecomputedRuntime(
            gModelParameters, material, viewDirections[pair], lightDirections[pair]);
        gContractResults[5u + pair] = uint(
            neuralAppearanceLobesMatch(cached, recomputed) && neuralAppearanceLobesBounded(cached));
    }

    gContractResults[9] = uint(
        all(isfinite(fixture.shadingNormal)) && abs(fixture.shadingNormal.y - 1.0f) > 1.0e-4f);
    gContractResults[10] = uint(
        fixture.anisotropyStrength > 0.0f && all(isfinite(fixture.anisotropyTangent)) &&
        abs(length(fixture.anisotropyTangent) - 1.0f) <= 2.0e-4f);

    // A seeded fixture must stay inside the base surface parameter domain the
    // reference evaluator accepts.
    static const uint seededFixtureSeed = 0x51ed270bu;
    NeuralAppearanceFixture seeded = neuralAppearanceFixtureSample(seededFixtureSeed);
    float normalDiskRadiusSquared = neuralAppearanceTrainingRandom(seededFixtureSeed, 5u);
    float normalDiskRadius = sqrt(normalDiskRadiusSquared);
    float normalDiskAngle = kNeuralAppearanceTrainingTwoPi *
                            neuralAppearanceTrainingRandom(seededFixtureSeed, 6u);
    float2 normalDisk = normalDiskRadius * float2(cos(normalDiskAngle), sin(normalDiskAngle));
    float3 expectedShadingNormal = float3(
        normalDisk.x, sqrt(saturate(1.0f - normalDiskRadiusSquared)), normalDisk.y);
    gContractResults[11] = uint(
        all(seeded.baseColor >= float3(0.0f)) && all(seeded.baseColor <= float3(1.0f)) &&
        seeded.metallic >= 0.0f && seeded.metallic <= 1.0f &&
        seeded.roughness >= kMaterialMinRoughness && seeded.roughness <= 1.0f &&
        seeded.anisotropyStrength >= 0.0f && seeded.anisotropyStrength <= 1.0f &&
        abs(length(seeded.shadingNormal) - 1.0f) <= 2.0e-4f &&
        seeded.shadingNormal.y >= 0.0f &&
        all(abs(seeded.shadingNormal - expectedShadingNormal) <= float3(2.0e-4f)) &&
        abs(length(seeded.anisotropyTangent) - 1.0f) <= 2.0e-4f);

    bool allPassed = true;
    for (uint flag = 0u; flag < 12u; ++flag)
    {
        allPassed = allPassed && gContractResults[flag] != 0u;
    }
    gContractResults[12] = uint(allPassed);
}
