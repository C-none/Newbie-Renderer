module qualityContract;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.quality;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

static const uint kNeuralAppearanceQualitySampleCount = 256u;
static const uint kNeuralAppearanceQualityUniformCount = 128u;
static const uint kNeuralAppearanceQualityHighlightCount = 64u;
static const uint kNeuralAppearanceQualitySeedBase = 0x80000000u;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gQualityMetrics;

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
    uint stratum = sampleIndex < kNeuralAppearanceQualityUniformCount ? 0u
                   : sampleIndex < kNeuralAppearanceQualityUniformCount + kNeuralAppearanceQualityHighlightCount
                       ? 1u
                       : 2u;
    NeuralAppearanceDirectionPair pair = neuralAppearanceStratumDirectionPair(stratum, sampleSeed);

    NeuralAppearanceFixture fixture = neuralAppearanceFixtureSample(
        neuralAppearanceTrainingHash(sampleSeed ^ 0x5bf03635u));
    NeuralAppearanceLobes prediction = evaluateNeuralAppearanceRecomputedRuntime(
        gModelParameters, neuralAppearanceMaterialInput(fixture), pair.viewDirection, pair.lightDirection);
    NeuralAppearanceLobes target = neuralAppearanceReferenceLobes(
        fixture, pair.viewDirection, pair.lightDirection);

    // Both lobes are folded into one bounded metric so the host contract keeps a
    // single distribution per stratum.
    float3 predictionSum = prediction.diffuse + prediction.specular;
    float3 targetSum = target.diffuse + target.specular;
    float mappedMae = neuralAppearanceQualityRgbMean(abs(
        neuralAppearanceQualityMap(predictionSum) - neuralAppearanceQualityMap(targetSum)));
    float safeLogL1 = neuralAppearanceLobeSafeLogLoss(prediction, target);
    float rawMae = neuralAppearanceQualityRgbMean(abs(predictionSum - targetSum));
    float zeroMappedBaseline = neuralAppearanceQualityRgbMean(
        neuralAppearanceQualityMap(targetSum));
    float zeroLogBaseline = neuralAppearanceLobeSafeLogLoss(NeuralAppearanceLobes(), target);
    float viewPhysicalCosine = saturate(pair.viewDirection.y);
    float lightPhysicalCosine = saturate(pair.lightDirection.y);
    float minimumPhysicalCosine = min(viewPhysicalCosine, lightPhysicalCosine);
    float maximumPhysicalCosine = max(viewPhysicalCosine, lightPhysicalCosine);
    float encodedStratum = float(stratum) + 0.25f * minimumPhysicalCosine;

    bool finite = all(isfinite(prediction.diffuse)) && all(isfinite(prediction.specular)) &&
                  all(isfinite(target.diffuse)) && all(isfinite(target.specular)) &&
                  all(predictionSum >= float3(0.0f)) && all(targetSum >= float3(0.0f)) &&
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
