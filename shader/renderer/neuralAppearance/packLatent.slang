module packLatent;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.quality;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

[[vk::binding(0, 4)]]
ByteAddressBuffer gInferenceLatent;

[[vk::binding(1, 4)]]
StructuredBuffer<float4> gModelParameters;

[[vk::binding(2, 4)]]
StructuredBuffer<float4> gTrainingLatent;

[[vk::binding(3, 4)]]
ByteAddressBuffer gInferenceParameters;

[[vk::binding(4, 4)]]
RWStructuredBuffer<float4> gHeldOutQualitySamples;

[[vk::binding(0, 2)]]
[format("rgba16f")]
RWTexture2D<float4> gLatentTexture0;

[[vk::binding(1, 2)]]
[format("rgba16f")]
RWTexture2D<float4> gLatentTexture1;

[shader("compute")]
[require(spvCooperativeVectorNV)]
[numthreads(8, 8, 1)]
void packLatentMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= kNeuralAppearanceLatentWidth ||
        dispatchThreadID.y >= kNeuralAppearanceLatentHeight)
    {
        return;
    }

    uint texelIndex = dispatchThreadID.y * kNeuralAppearanceLatentWidth + dispatchThreadID.x;
    int2 coordinate = int2(dispatchThreadID.xy);
    uint chunk0 = texelIndex * 2u + 0u;
    uint chunk1 = texelIndex * 2u + 1u;
    uint byteOffset0 = chunk0 * 8u;
    uint byteOffset1 = chunk1 * 8u;
    gLatentTexture0[coordinate] = float4(
        float(gInferenceLatent.Load<half>(byteOffset0 + 0u)),
        float(gInferenceLatent.Load<half>(byteOffset0 + 2u)),
        float(gInferenceLatent.Load<half>(byteOffset0 + 4u)),
        float(gInferenceLatent.Load<half>(byteOffset0 + 6u)));
    gLatentTexture1[coordinate] = float4(
        float(gInferenceLatent.Load<half>(byteOffset1 + 0u)),
        float(gInferenceLatent.Load<half>(byteOffset1 + 2u)),
        float(gInferenceLatent.Load<half>(byteOffset1 + 4u)),
        float(gInferenceLatent.Load<half>(byteOffset1 + 6u)));

    uint qualitySampleIndex = texelIndex;
    if (qualitySampleIndex >= kNeuralAppearanceHeldOutQualitySampleCount)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceHeldOutQualitySample(qualitySampleIndex);
    float3 target = neuralAppearanceReferenceSpecularProjected(
        sample.uv, sample.directions.viewDirection, sample.directions.lightDirection);
    float3 masterPrediction = evaluateNeuralAppearanceRecomputedRuntime(
        gModelParameters, neuralAppearanceQualitySampleMasterLatent(gTrainingLatent, sample.uv),
        sample.directions.viewDirection, sample.directions.lightDirection);
    float3 fp16Prediction = evaluateNeuralAppearanceRuntime(
        gInferenceParameters, neuralAppearanceQualitySampleInferenceLatent(gInferenceLatent, sample.uv),
        sample.directions.viewDirection, sample.directions.lightDirection);

    uint flags = 0u;
    float masterLoss = neuralAppearanceQualitySafeLogLoss(masterPrediction, target);
    float fp16Loss = neuralAppearanceQualitySafeLogLoss(fp16Prediction, target);
    float zeroLoss = neuralAppearanceQualitySafeLogLoss(float3(0.0f), target);
    if (all(isfinite(masterPrediction)) && isfinite(masterLoss))
    {
        flags |= kNeuralAppearanceQualityFp32Finite;
    }
    if (all(masterPrediction >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityFp32Nonnegative;
    }
    if (all(isfinite(fp16Prediction)) && isfinite(fp16Loss))
    {
        flags |= kNeuralAppearanceQualityFp16Finite;
    }
    if (all(fp16Prediction >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityFp16Nonnegative;
    }
    if (all(isfinite(target)) && isfinite(zeroLoss))
    {
        flags |= kNeuralAppearanceQualityTargetFinite;
    }
    if (all(target >= float3(0.0f)))
    {
        flags |= kNeuralAppearanceQualityTargetNonnegative;
    }
    gHeldOutQualitySamples[qualitySampleIndex] = float4(masterLoss, fp16Loss, zeroLoss, float(flags));
}
