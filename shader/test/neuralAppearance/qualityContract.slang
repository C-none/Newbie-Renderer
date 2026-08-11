module qualityContract;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

static const uint kNeuralAppearanceQualitySampleCount = 256u;
static const uint kNeuralAppearanceQualityUniformCount = 128u;
static const uint kNeuralAppearanceQualityHighlightCount = 64u;
static const uint kNeuralAppearanceQualitySeedBase = 0x80000000u;

static const uint kNeuralAppearanceQualityUniformStratum = 0u;
static const uint kNeuralAppearanceQualityHighlightStratum = 1u;
static const uint kNeuralAppearanceQualityGrazingStratum = 2u;

[[vk::binding(0, 1)]]
Sampler2D<float4> gLatentTexture0;

[[vk::binding(1, 1)]]
Sampler2D<float4> gLatentTexture1;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gQualityMetrics;

float3 neuralAppearanceQualityNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared >= 1.0e-12f
        ? value * rsqrt(lengthSquared)
        : fallback;
}

NeuralAppearanceDirectionPair neuralAppearanceQualityHighlightPair(
    ResolvedMaterialPayload material, uint sampleSeed)
{
    float3 viewDirection = neuralAppearanceTrainingUniformConeDirection(
        material.geometryNormal, 0.6108652382f, sampleSeed, 2u);
    float3 reflectionAxis = neuralAppearanceQualityNormalize(
        reflect(-viewDirection, material.shadingNormal), material.shadingNormal);
    NeuralAppearanceDirectionPair pair = NeuralAppearanceDirectionPair(
        viewDirection, neuralAppearanceTrainingUniformConeDirection(
            reflectionAxis, 0.0436332313f, sampleSeed, 4u));

    if (dot(material.geometryNormal, pair.lightDirection) < 1.0e-3f)
    {
        pair.lightDirection = reflectionAxis;
    }
    return pair;
}

float3 neuralAppearanceQualityGrazingDirection(
    float3 normal, uint sampleSeed, uint dimensionOffset)
{
    float height = lerp(
        0.005f, 0.08f,
        neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 0u));
    float phase = kNeuralAppearanceTrainingTwoPi *
                  neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 1u);
    float3 tangent = neuralAppearanceTrainingOrthogonal(normal);
    float3 bitangent = cross(normal, tangent);
    float3 horizonDirection = tangent * cos(phase) + bitangent * sin(phase);
    return neuralAppearanceQualityNormalize(
        horizonDirection * sqrt(max(1.0f - height * height, 0.0f)) +
            normal * height,
        normal);
}

float3 neuralAppearanceQualityMap(float3 value)
{
    float3 nonNegative = max(value, float3(0.0f));
    return nonNegative / (float3(1.0f) + nonNegative);
}

float3 neuralAppearanceQualitySafeLog(float3 value)
{
    return log(float3(1.0f) + max(value, float3(0.0f)));
}

float neuralAppearanceQualityRgbMean(float3 value)
{
    return dot(value, float3(1.0f / 3.0f));
}

[shader("compute")]
[numthreads(64, 1, 1)]
void qualityContractMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    if (sampleIndex >= kNeuralAppearanceQualitySampleCount)
    {
        return;
    }

    // Training consumes the small positive preimage range for this bounded
    // contract. The high-bit namespace makes these 256 hash inputs held out.
    uint sampleSeed = neuralAppearanceTrainingHash(
        kNeuralAppearanceQualitySeedBase + sampleIndex);
    float2 materialUv = float2(
        neuralAppearanceTrainingRandom(sampleSeed, 0u),
        neuralAppearanceTrainingRandom(sampleSeed, 1u));
    ResolvedMaterialPayload material = neuralAppearanceFixtureMaterial(materialUv);

    uint stratum = kNeuralAppearanceQualityUniformStratum;
    NeuralAppearanceDirectionPair pair = neuralAppearanceTrainingDirectionPair(sampleSeed, 2u);
    if (sampleIndex >= kNeuralAppearanceQualityUniformCount &&
        sampleIndex < kNeuralAppearanceQualityUniformCount +
                          kNeuralAppearanceQualityHighlightCount)
    {
        stratum = kNeuralAppearanceQualityHighlightStratum;
        pair = neuralAppearanceQualityHighlightPair(material, sampleSeed);
    }
    else if (sampleIndex >= kNeuralAppearanceQualityUniformCount +
                               kNeuralAppearanceQualityHighlightCount)
    {
        stratum = kNeuralAppearanceQualityGrazingStratum;
        pair = NeuralAppearanceDirectionPair(
            neuralAppearanceQualityGrazingDirection(material.geometryNormal, sampleSeed, 2u),
            neuralAppearanceQualityGrazingDirection(material.geometryNormal, sampleSeed, 4u));
    }

    NeuralAppearanceLatent latent = sampleNeuralAppearanceLatent(
        gLatentTexture0, gLatentTexture1, materialUv);
    NeuralMaterialContext context = buildNeuralMaterialContextRuntime(
        gModelParameters, latent);
    float3 prediction = evaluateNeuralAppearanceRuntime(
        gModelParameters, context, pair.viewDirection, pair.lightDirection);
    float3 target = neuralAppearanceReferenceSpecularProjected(
        materialUv, pair.viewDirection, pair.lightDirection);

    float mappedMae = neuralAppearanceQualityRgbMean(abs(
        neuralAppearanceQualityMap(prediction) - neuralAppearanceQualityMap(target)));
    float safeLogL1 = neuralAppearanceQualityRgbMean(abs(
        neuralAppearanceQualitySafeLog(prediction) -
        neuralAppearanceQualitySafeLog(target)));
    float rawMae = neuralAppearanceQualityRgbMean(abs(prediction - target));
    float zeroMappedBaseline = neuralAppearanceQualityRgbMean(
        neuralAppearanceQualityMap(target));
    float zeroLogBaseline = neuralAppearanceQualityRgbMean(
        neuralAppearanceQualitySafeLog(target));
    float viewPhysicalCosine = saturate(
        dot(material.geometryNormal, pair.viewDirection));
    float lightPhysicalCosine = saturate(
        dot(material.geometryNormal, pair.lightDirection));
    float minimumPhysicalCosine = min(
        viewPhysicalCosine, lightPhysicalCosine);
    float maximumPhysicalCosine = max(
        viewPhysicalCosine, lightPhysicalCosine);
    float encodedStratum = float(stratum) + 0.25f * minimumPhysicalCosine;

    bool finite = all(isfinite(prediction)) && all(isfinite(target)) &&
                  all(prediction >= float3(0.0f)) &&
                  all(target >= float3(0.0f)) &&
                  isfinite(mappedMae) && isfinite(safeLogL1) &&
                  isfinite(rawMae) && isfinite(zeroMappedBaseline) &&
                  isfinite(zeroLogBaseline) &&
                  isfinite(minimumPhysicalCosine) &&
                  isfinite(maximumPhysicalCosine);
    uint record = sampleIndex * 2u;
    gQualityMetrics[record + 0u] = float4(
        mappedMae, safeLogL1, rawMae, finite ? 1.0f : 0.0f);
    gQualityMetrics[record + 1u] = float4(
        zeroMappedBaseline, zeroLogBaseline,
        maximumPhysicalCosine, encodedStratum);
}
