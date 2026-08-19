module optimizeTraining;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceOptimizePushConstants
{
    uint trainingStep;
    uint batchSize;
    uint parameterChunkCount;
    uint totalTrainingSteps;
    // The converted row-major gradient region starts at the first 64-byte aligned
    // device address inside its allocation, so weight reads are shifted by the
    // host-measured base padding while bias reads stay at their canonical offset.
    uint rowMajorGradientBaseOffset;
    // Per-element clamp on the batch-normalized gradient. Zero disables clipping.
    float gradientClip;
    float initialLearningRate;
    float finalLearningRate;
    float beta1;
    float beta2;
    float epsilon;
};

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gModelParameters;

[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> gModelMoments;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> gTrainingStatus;

[[vk::binding(3, 2)]]
RWStructuredBuffer<uint4> gTrainingControl;

[[vk::binding(4, 2)]]
StructuredBuffer<float4> gSampleMetrics;

[[vk::binding(5, 2)]]
ByteAddressBuffer gRowMajorWeightGradients;

[[vk::binding(6, 2)]]
ByteAddressBuffer gBiasGradients;

[[vk::binding(7, 2)]]
RWByteAddressBuffer gInferenceParameters;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceOptimizePushConstants> gTraining;

static const uint kSharedLearningRate = 0u;
static const uint kSharedBeta1 = 1u;
static const uint kSharedBeta2 = 2u;
static const uint kSharedEpsilon = 3u;
static const uint kSharedFirstCorrection = 4u;
static const uint kSharedSecondCorrection = 5u;
groupshared float gAdamShared[6];

float4 neuralAppearanceSanitizeGradient(float4 gradient)
{
    return all(isfinite(gradient)) ? gradient : float4(0.0f);
}

void neuralAppearanceAdamUpdate(
    inout float4 value, inout float4 firstMoment, inout float4 secondMoment,
    float4 gradient, float learningRate,
    float beta1, float beta2, float epsilon,
    float firstCorrection, float secondCorrection)
{
    gradient = neuralAppearanceSanitizeGradient(gradient);
    firstMoment = beta1 * firstMoment + (1.0f - beta1) * gradient;
    secondMoment = beta2 * secondMoment + (1.0f - beta2) * gradient * gradient;

    float4 correctedFirstMoment = firstMoment / firstCorrection;
    float4 correctedSecondMoment = secondMoment / secondCorrection;
    value -= max(learningRate, 0.0f) * correctedFirstMoment /
             (sqrt(correctedSecondMoment) + epsilon);
}

float4 neuralAppearanceReducedParameterGradient(uint chunkIndex, uint batchSize)
{
    uint scalar = chunkIndex * 4u;
    float4 gradient = float4(0.0f);
    [ForceUnroll]
    for (uint lane = 0u; lane < 4u; ++lane)
    {
        uint scalarIndex = scalar + lane;
        if (scalarIndex < kNeuralAppearanceParameterScalarCount)
        {
            uint byteOffset = neuralAppearanceInferenceByteOffset(scalarIndex);
            gradient[lane] =
                neuralAppearanceParameterIsBias(scalarIndex)
                    ? float(gBiasGradients.Load<half>(byteOffset)) / float(batchSize)
                    : float(gRowMajorWeightGradients.Load<half>(
                          gTraining.rowMajorGradientBaseOffset + byteOffset)) /
                          float(batchSize);
        }
    }
    // Clipping is applied after batch normalization so the bound is independent of
    // the batch size, suppressing the rare extreme sample that dominates a step.
    return gTraining.gradientClip > 0.0f
               ? clamp(gradient, float4(-gTraining.gradientClip), float4(gTraining.gradientClip))
               : gradient;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void optimizeTrainingMain(
    uint3 dispatchThreadID: SV_DispatchThreadID,
    uint3 groupThreadID: SV_GroupThreadID)
{
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (batchSize == 0u)
    {
        return;
    }

    if (groupThreadID.x == 0u)
    {
        float beta1 = min(saturate(gTraining.beta1), 0.9999999f);
        float beta2 = min(saturate(gTraining.beta2), 0.9999999f);
        float step = float(max(gTraining.trainingStep, 1u));

        gAdamShared[kSharedLearningRate] = neuralAppearanceTrainingCosineSchedule(
            max(gTraining.initialLearningRate, 0.0f), max(gTraining.finalLearningRate, 0.0f),
            gTraining.trainingStep, gTraining.totalTrainingSteps);
        gAdamShared[kSharedBeta1] = beta1;
        gAdamShared[kSharedBeta2] = beta2;
        gAdamShared[kSharedEpsilon] = max(gTraining.epsilon, 1.0e-12f);
        gAdamShared[kSharedFirstCorrection] = max(1.0f - pow(beta1, step), 1.0e-12f);
        gAdamShared[kSharedSecondCorrection] = max(1.0f - pow(beta2, step), 1.0e-12f);
    }
    GroupMemoryBarrierWithGroupSync();

    uint invocation = dispatchThreadID.x;
    uint parameterChunkCount = min(
        gTraining.parameterChunkCount, kNeuralAppearanceParameterChunkCount);
    float learningRate = gAdamShared[kSharedLearningRate];

    if (invocation < parameterChunkCount)
    {
        float4 value = gModelParameters[invocation];
        float4 firstMoment = gModelMoments[invocation];
        float4 secondMoment =
            gModelMoments[kNeuralAppearanceParameterChunkCount + invocation];
        neuralAppearanceAdamUpdate(
            value, firstMoment, secondMoment,
            neuralAppearanceReducedParameterGradient(invocation, batchSize),
            learningRate, gAdamShared[kSharedBeta1], gAdamShared[kSharedBeta2],
            gAdamShared[kSharedEpsilon], gAdamShared[kSharedFirstCorrection],
            gAdamShared[kSharedSecondCorrection]);
        gModelParameters[invocation] = value;
        gModelMoments[invocation] = firstMoment;
        gModelMoments[kNeuralAppearanceParameterChunkCount + invocation] = secondMoment;

        uint scalar = invocation * 4u;
        [ForceUnroll]
        for (uint lane = 0u; lane < 4u; ++lane)
        {
            uint scalarIndex = scalar + lane;
            if (scalarIndex < kNeuralAppearanceParameterScalarCount)
            {
                gInferenceParameters.Store<half>(
                    neuralAppearanceInferenceByteOffset(scalarIndex), half(value[lane]));
            }
        }
    }

    if (invocation == 0u)
    {
        // Only invocation zero accesses the control record. An invalid host schema
        // suppresses publication without introducing a cross-invocation buffer race.
        uint4 control = gTrainingControl[0];
        if (control.z != kNeuralAppearanceTrainingSchemaMagic)
        {
            return;
        }

        float meanLoss = 0.0f;
        float meanOptimizationLoss = 0.0f;
        float finite = 1.0f;
        for (uint sample = 0u; sample < batchSize; ++sample)
        {
            float4 metrics = gSampleMetrics[sample];
            meanLoss += metrics.x;
            meanOptimizationLoss += metrics.y;
            finite *= metrics.w;
        }
        meanLoss /= float(batchSize);
        meanOptimizationLoss /= float(batchSize);
        finite = isfinite(meanLoss) && finite > 0.5f ? 1.0f : 0.0f;

        float4 previousStatus = gTrainingStatus[0];
        bool firstStep = control.x == 0u;
        float currentLoss = firstStep ? meanLoss : lerp(previousStatus.x, meanLoss, 0.05f);
        float initialLoss = firstStep ? meanLoss : previousStatus.y;
        gTrainingStatus[0] = float4(
            currentLoss, initialLoss, float(gTraining.trainingStep), finite);
        gTrainingStatus[1] = float4(
            learningRate, meanOptimizationLoss, gSampleMetrics[0].z, float(batchSize));

        control.x = gTraining.trainingStep;
        control.y = 0u;
        control.w = 1u;
        gTrainingControl[0] = control;
    }
}
