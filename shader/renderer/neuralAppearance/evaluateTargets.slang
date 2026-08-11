module evaluateTargets;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.reference;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceTargetPushConstants
{
    uint trainingStep;
    uint batchSize;
    uint latentWidth;
    uint latentHeight;
    uint mollificationSampleCount;
    uint mollificationStepCount;
    float initialAngleRadians;
    float padding;
};

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gSampleTargets;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceTargetPushConstants> gTraining;

groupshared float4 gTargetReduction[kNeuralAppearanceMollificationSampleCountMax];
groupshared float2 gTargetUv;
groupshared float3 gTargetViewDirection;
groupshared float3 gTargetLightDirection;
groupshared uint gTargetSampleSeed;
groupshared uint gTargetSampleCount;
groupshared float gTargetMollificationAngle;

[shader("compute")]
[numthreads(256, 1, 1)]
void evaluateTargetsMain(
    uint3 groupID: SV_GroupID,
    uint3 groupThreadID: SV_GroupThreadID)
{
    uint sampleIndex = groupID.x;
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (sampleIndex >= batchSize || batchSize == 0u ||
        gTraining.latentWidth != kNeuralAppearanceLatentWidth ||
        gTraining.latentHeight != kNeuralAppearanceLatentHeight)
    {
        return;
    }

    uint lane = groupThreadID.x;
    if (lane == 0u)
    {
        NeuralAppearanceTrainingSample sample = neuralAppearanceTrainingSample(
            gTraining.trainingStep, sampleIndex,
            gTraining.mollificationStepCount, gTraining.initialAngleRadians);
        gTargetUv = sample.uv;
        gTargetViewDirection = sample.directions.viewDirection;
        gTargetLightDirection = sample.directions.lightDirection;
        gTargetSampleSeed = sample.sampleSeed;
        gTargetMollificationAngle = sample.mollificationAngle;
        gTargetSampleCount = sample.mollificationAngle > 0.0f
            ? min(max(gTraining.mollificationSampleCount, 1u),
                  kNeuralAppearanceMollificationSampleCountMax)
            : 1u;
    }
    GroupMemoryBarrierWithGroupSync();

    // Once mollification reaches its sharp endpoint there is only one target.
    // Avoid keeping all 256 lanes in the reduction/barrier path for the remaining
    // majority of the training budget.
    if (gTargetSampleCount == 1u)
    {
        if (lane == 0u)
        {
            float3 sampledLightDirection = gTargetLightDirection;
            if (gTargetMollificationAngle > 0.0f)
            {
                uint coneSeed = neuralAppearanceTrainingHash(
                    gTargetSampleSeed ^ 0xb7e15162u);
                sampledLightDirection = neuralAppearanceTrainingUniformConeDirection(
                    gTargetLightDirection, gTargetMollificationAngle,
                    coneSeed, 0u);
            }
            float3 target = neuralAppearanceReferenceSpecularProjected(
                gTargetUv, gTargetViewDirection, sampledLightDirection);
            gSampleTargets[sampleIndex] = float4(
                target, gTargetMollificationAngle);
        }
        return;
    }

    float3 target = float3(0.0f);
    if (lane < gTargetSampleCount)
    {
        float3 sampledLightDirection = gTargetLightDirection;
        if (gTargetMollificationAngle > 0.0f)
        {
            uint coneSeed = neuralAppearanceTrainingHash(
                gTargetSampleSeed ^ 0xb7e15162u);
            sampledLightDirection = neuralAppearanceTrainingUniformConeDirection(
                gTargetLightDirection, gTargetMollificationAngle,
                coneSeed, lane * 2u);
        }
        target = neuralAppearanceReferenceSpecularProjected(
            gTargetUv, gTargetViewDirection, sampledLightDirection);
    }
    gTargetReduction[lane] = float4(target, 0.0f);
    GroupMemoryBarrierWithGroupSync();

    for (uint stride = 128u; stride > 0u; stride >>= 1u)
    {
        if (lane < stride)
        {
            gTargetReduction[lane] += gTargetReduction[lane + stride];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    if (lane == 0u)
    {
        gSampleTargets[sampleIndex] = float4(
            gTargetReduction[0].xyz / float(gTargetSampleCount),
            gTargetMollificationAngle);
    }
}
