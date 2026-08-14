module optimizeTraining;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceOptimizePushConstants
{
    uint trainingStep;
    uint batchSize;
    uint parameterChunkCount;
    uint latentTexelCount;
    uint totalTrainingSteps;
    uint latentWarmupSteps;
    float initialLearningRate;
    float finalLearningRate;
    float latentLearningRateScale;
    float beta1;
    float beta2;
    float epsilon;
};

[[vk::binding(0, 1)]]
StructuredBuffer<float4> gSampleGradients;

[[vk::binding(1, 1)]]
StructuredBuffer<uint4> gSampleTexelIndices;

[[vk::binding(6, 2)]]
StructuredBuffer<float4> gSampleMetrics;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gModelParameters;

[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> gModelMoments;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> gTrainingLatent;

[[vk::binding(3, 2)]]
RWStructuredBuffer<float4> gLatentMoments;

[[vk::binding(4, 2)]]
RWStructuredBuffer<float4> gTrainingStatus;

[[vk::binding(5, 2)]]
RWStructuredBuffer<uint4> gTrainingControl;

[[vk::binding(7, 2)]]
ByteAddressBuffer gRowMajorWeightGradients;

[[vk::binding(8, 2)]]
ByteAddressBuffer gBiasGradients;

[[vk::binding(9, 2)]]
RWByteAddressBuffer gInferenceParameters;

[[vk::binding(10, 2)]]
RWByteAddressBuffer gInferenceLatent;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceOptimizePushConstants> gTraining;

static const uint kSharedModelLearningRate = 0u;
static const uint kSharedLatentLearningRate = 1u;
static const uint kSharedBeta1 = 2u;
static const uint kSharedBeta2 = 3u;
static const uint kSharedEpsilon = 4u;
static const uint kSharedModelFirstCorrection = 5u;
static const uint kSharedModelSecondCorrection = 6u;
static const uint kSharedLatentFirstCorrection = 7u;
static const uint kSharedLatentSecondCorrection = 8u;
groupshared float gAdamShared[9];

uint neuralAppearanceInferenceByteOffset(uint scalarIndex)
{
    if (scalarIndex < kNeuralAppearanceFrameBiasOffset)
        return kNeuralAppearanceFrameWeightByteOffset + scalarIndex * 2u;
    if (scalarIndex < kNeuralAppearanceSpatialWeightOffset)
        return kNeuralAppearanceFrameBiasByteOffset + (scalarIndex - kNeuralAppearanceFrameBiasOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceDirectionWeightOffset)
        return kNeuralAppearanceSpatialWeightByteOffset + (scalarIndex - kNeuralAppearanceSpatialWeightOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceFirstBiasOffset)
        return kNeuralAppearanceDirectionWeightByteOffset + (scalarIndex - kNeuralAppearanceDirectionWeightOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceSecondWeightOffset)
        return kNeuralAppearanceSpatialBiasByteOffset + (scalarIndex - kNeuralAppearanceFirstBiasOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceSecondBiasOffset)
        return kNeuralAppearanceHiddenWeightByteOffset + (scalarIndex - kNeuralAppearanceSecondWeightOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceOutputWeightOffset)
        return kNeuralAppearanceHiddenBiasByteOffset + (scalarIndex - kNeuralAppearanceSecondBiasOffset) * 2u;
    if (scalarIndex < kNeuralAppearanceOutputBiasOffset)
        return kNeuralAppearanceOutputWeightByteOffset + (scalarIndex - kNeuralAppearanceOutputWeightOffset) * 2u;
    return kNeuralAppearanceOutputBiasByteOffset + (scalarIndex - kNeuralAppearanceOutputBiasOffset) * 2u;
}

bool neuralAppearanceParameterIsBias(uint scalarIndex)
{
    return (scalarIndex >= kNeuralAppearanceFrameBiasOffset && scalarIndex < kNeuralAppearanceSpatialWeightOffset) ||
           (scalarIndex >= kNeuralAppearanceFirstBiasOffset && scalarIndex < kNeuralAppearanceSecondWeightOffset) ||
           (scalarIndex >= kNeuralAppearanceSecondBiasOffset && scalarIndex < kNeuralAppearanceOutputWeightOffset) ||
           scalarIndex >= kNeuralAppearanceOutputBiasOffset;
}

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
        if (scalarIndex < kNeuralAppearancePaddingOffset)
        {
            uint byteOffset = neuralAppearanceInferenceByteOffset(scalarIndex);
            gradient[lane] = neuralAppearanceParameterIsBias(scalarIndex)
                                 ? float(gBiasGradients.Load<half>(byteOffset)) / float(batchSize)
                                 : float(gRowMajorWeightGradients.Load<half>(byteOffset)) / float(batchSize);
        }
    }
    return gradient;
}

void neuralAppearanceReducedLatentGradient(
    uint texelIndex, uint batchSize, out float4 gradient0, out float4 gradient1)
{
    gradient0 = float4(0.0f);
    gradient1 = float4(0.0f);
    for (uint sample = 0u; sample < batchSize; ++sample)
    {
        uint4 indices = gSampleTexelIndices[sample];
        uint gradientBase = sample * kNeuralAppearanceGradientChunkCount +
                            kNeuralAppearanceParameterChunkCount;
        for (uint tap = 0u; tap < 4u; ++tap)
        {
            if (indices[tap] == texelIndex)
            {
                gradient0 += gSampleGradients[gradientBase + tap * 2u + 0u];
                gradient1 += gSampleGradients[gradientBase + tap * 2u + 1u];
            }
        }
    }
    gradient0 /= float(batchSize);
    gradient1 /= float(batchSize);
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

    bool latentActive = gTraining.trainingStep > gTraining.latentWarmupSteps;
    if (groupThreadID.x == 0u)
    {
        float beta1 = min(saturate(gTraining.beta1), 0.9999999f);
        float beta2 = min(saturate(gTraining.beta2), 0.9999999f);
        float modelLearningRate = neuralAppearanceTrainingCosineSchedule(
            max(gTraining.initialLearningRate, 0.0f),
            max(gTraining.finalLearningRate, 0.0f),
            gTraining.trainingStep, gTraining.totalTrainingSteps);
        float modelStep = float(max(gTraining.trainingStep, 1u));
        uint latentOptimizerStep = latentActive
            ? gTraining.trainingStep - gTraining.latentWarmupSteps
            : 1u;
        float latentStep = float(max(latentOptimizerStep, 1u));

        gAdamShared[kSharedModelLearningRate] = modelLearningRate;
        gAdamShared[kSharedLatentLearningRate] = latentActive
            ? modelLearningRate * max(gTraining.latentLearningRateScale, 0.0f)
            : 0.0f;
        gAdamShared[kSharedBeta1] = beta1;
        gAdamShared[kSharedBeta2] = beta2;
        gAdamShared[kSharedEpsilon] = max(gTraining.epsilon, 1.0e-12f);
        gAdamShared[kSharedModelFirstCorrection] =
            max(1.0f - pow(beta1, modelStep), 1.0e-12f);
        gAdamShared[kSharedModelSecondCorrection] =
            max(1.0f - pow(beta2, modelStep), 1.0e-12f);
        gAdamShared[kSharedLatentFirstCorrection] =
            max(1.0f - pow(beta1, latentStep), 1.0e-12f);
        gAdamShared[kSharedLatentSecondCorrection] =
            max(1.0f - pow(beta2, latentStep), 1.0e-12f);
    }
    GroupMemoryBarrierWithGroupSync();

    uint invocation = dispatchThreadID.x;
    uint parameterChunkCount = min(
        gTraining.parameterChunkCount, kNeuralAppearanceParameterChunkCount);
    uint latentTexelCount = min(
        gTraining.latentTexelCount, kNeuralAppearanceLatentTexelCount);
    float modelLearningRate = gAdamShared[kSharedModelLearningRate];
    float latentLearningRate = gAdamShared[kSharedLatentLearningRate];
    float beta1 = gAdamShared[kSharedBeta1];
    float beta2 = gAdamShared[kSharedBeta2];
    float epsilon = gAdamShared[kSharedEpsilon];

    if (invocation < parameterChunkCount)
    {
        float4 value = gModelParameters[invocation];
        float4 firstMoment = gModelMoments[invocation];
        float4 secondMoment =
            gModelMoments[kNeuralAppearanceParameterChunkCount + invocation];
        float4 gradient = neuralAppearanceReducedParameterGradient(invocation, batchSize);
        neuralAppearanceAdamUpdate(
            value, firstMoment, secondMoment, gradient,
            modelLearningRate, beta1, beta2, epsilon,
            gAdamShared[kSharedModelFirstCorrection],
            gAdamShared[kSharedModelSecondCorrection]);
        if (invocation == kNeuralAppearanceParameterChunkCount - 1u)
        {
            value.w = 0.0f;
            firstMoment.w = 0.0f;
            secondMoment.w = 0.0f;
        }
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
                gInferenceParameters.Store<half>(neuralAppearanceInferenceByteOffset(scalarIndex), half(value[lane]));
            }
        }
    }

    if (invocation < latentTexelCount && latentActive)
    {
        float4 gradient0;
        float4 gradient1;
        neuralAppearanceReducedLatentGradient(invocation, batchSize, gradient0, gradient1);

        uint chunk0 = invocation * 2u;
        uint chunk1 = chunk0 + 1u;
        float4 value0 = gTrainingLatent[chunk0];
        float4 value1 = gTrainingLatent[chunk1];
        float4 firstMoment0 = gLatentMoments[chunk0];
        float4 firstMoment1 = gLatentMoments[chunk1];
        float4 secondMoment0 =
            gLatentMoments[kNeuralAppearanceLatentChunkCount + chunk0];
        float4 secondMoment1 =
            gLatentMoments[kNeuralAppearanceLatentChunkCount + chunk1];
        neuralAppearanceAdamUpdate(
            value0, firstMoment0, secondMoment0, gradient0,
            latentLearningRate, beta1, beta2, epsilon,
            gAdamShared[kSharedLatentFirstCorrection],
            gAdamShared[kSharedLatentSecondCorrection]);
        neuralAppearanceAdamUpdate(
            value1, firstMoment1, secondMoment1, gradient1,
            latentLearningRate, beta1, beta2, epsilon,
            gAdamShared[kSharedLatentFirstCorrection],
            gAdamShared[kSharedLatentSecondCorrection]);
        gTrainingLatent[chunk0] = value0;
        gTrainingLatent[chunk1] = value1;
        gLatentMoments[chunk0] = firstMoment0;
        gLatentMoments[chunk1] = firstMoment1;
        gLatentMoments[kNeuralAppearanceLatentChunkCount + chunk0] = secondMoment0;
        gLatentMoments[kNeuralAppearanceLatentChunkCount + chunk1] = secondMoment1;
        gInferenceLatent.Store<half>(chunk0 * 8u + 0u, half(value0.x));
        gInferenceLatent.Store<half>(chunk0 * 8u + 2u, half(value0.y));
        gInferenceLatent.Store<half>(chunk0 * 8u + 4u, half(value0.z));
        gInferenceLatent.Store<half>(chunk0 * 8u + 6u, half(value0.w));
        gInferenceLatent.Store<half>(chunk1 * 8u + 0u, half(value1.x));
        gInferenceLatent.Store<half>(chunk1 * 8u + 2u, half(value1.y));
        gInferenceLatent.Store<half>(chunk1 * 8u + 4u, half(value1.z));
        gInferenceLatent.Store<half>(chunk1 * 8u + 6u, half(value1.w));
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
        float finite = 1.0f;
        for (uint sample = 0u; sample < batchSize; ++sample)
        {
            float4 metrics = gSampleMetrics[sample];
            meanLoss += metrics.x;
            finite *= metrics.w;
        }
        meanLoss /= float(batchSize);
        finite = isfinite(meanLoss) && finite > 0.5f ? 1.0f : 0.0f;

        float4 previousStatus = gTrainingStatus[0];
        bool firstStep = control.x == 0u;
        float currentLoss = firstStep ? meanLoss : lerp(previousStatus.x, meanLoss, 0.05f);
        float initialLoss = firstStep ? meanLoss : previousStatus.y;
        gTrainingStatus[0] = float4(
            currentLoss, initialLoss, float(gTraining.trainingStep), finite);
        gTrainingStatus[1] = float4(
            modelLearningRate, latentLearningRate,
            gSampleMetrics[0].z, float(batchSize));

        control.x = gTraining.trainingStep;
        control.y = 0u;
        control.w = 1u;
        gTrainingControl[0] = control;
    }
}
