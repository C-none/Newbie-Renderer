module evaluateQuality;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.quality;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

[[vk::binding(0, 4)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(1, 4)]]
ByteAddressBuffer gInferenceParameters;

[[vk::binding(2, 4)]]
RWStructuredBuffer<float4> gHeldOutQualitySamples;

uint neuralAppearanceQualityFlags(
    NeuralAppearanceLobes master, float masterLoss, NeuralAppearanceLobes fp16, float fp16Loss,
    NeuralAppearanceLobes target, float zeroLoss)
{
    uint flags = 0u;
    if (all(isfinite(master.diffuse)) && all(isfinite(master.specular)) && isfinite(masterLoss))
    {
        flags |= kNeuralAppearanceQualityFp32Finite;
    }
    if (all(master.diffuse >= float3(0.0f)) && all(master.specular >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityFp32Nonnegative;
    }
    if (all(isfinite(fp16.diffuse)) && all(isfinite(fp16.specular)) && isfinite(fp16Loss))
    {
        flags |= kNeuralAppearanceQualityFp16Finite;
    }
    if (all(fp16.diffuse >= float3(0.0f)) && all(fp16.specular >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityFp16Nonnegative;
    }
    if (all(isfinite(target.diffuse)) && all(isfinite(target.specular)) && isfinite(zeroLoss))
    {
        flags |= kNeuralAppearanceQualityTargetFinite;
    }
    if (all(target.diffuse >= float3(0.0f)) && all(target.specular >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityTargetNonnegative;
    }
    return flags;
}

[shader("compute")]
[require(spvCooperativeVectorNV)]
[numthreads(64, 1, 1)]
void evaluateQualityMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    if (sampleIndex >= kNeuralAppearanceHeldOutQualitySampleCount)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceHeldOutQualitySample(sampleIndex);
    NeuralAppearanceFixture fixture = neuralAppearanceFixtureSample(sample.materialSeed);
    NeuralAppearanceMaterialInput material = neuralAppearanceMaterialInput(fixture);
    float3 viewDirection = sample.directions.viewDirection;
    float3 lightDirection = sample.directions.lightDirection;

    NeuralAppearanceLobes target = neuralAppearanceReferenceLobes(fixture, viewDirection, lightDirection);
    NeuralAppearanceLobes masterPrediction = evaluateNeuralAppearanceRecomputedRuntime(
        gModelParameters, material, viewDirection, lightDirection);
    NeuralAppearanceLobes fp16Prediction = evaluateNeuralAppearanceRuntime(
        gInferenceParameters, material, viewDirection, lightDirection);

    float masterLoss = neuralAppearanceLobeSafeLogLoss(masterPrediction, target);
    float fp16Loss = neuralAppearanceLobeSafeLogLoss(fp16Prediction, target);
    float zeroLoss = neuralAppearanceLobeSafeLogLoss(NeuralAppearanceLobes(), target);
    gHeldOutQualitySamples[sampleIndex] = float4(
        masterLoss, fp16Loss, zeroLoss,
        float(neuralAppearanceQualityFlags(masterPrediction, masterLoss, fp16Prediction, fp16Loss,
                                           target, zeroLoss)));
}
