module evaluateGradients;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceGradientPushConstants
{
    uint trainingStep;
    uint batchSize;
    uint latentWidth;
    uint latentHeight;
    uint mollificationSampleCount;
    uint mollificationStepCount;
    float initialAngleRadians;
    float padding;
    uint optimalWeightOffsets[kNeuralAppearanceAffineLayerCount];
};

struct NeuralAppearanceLossInputs : IDifferentiable
{
    NeuralAppearanceLatentNeighborhood neighborhood = NeuralAppearanceLatentNeighborhood();
    no_diff float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 target = float3(0.0f);

    __init()
    {
    }

    __init(
        NeuralAppearanceLatentNeighborhood inputNeighborhood,
        float3 inputViewDirection, float3 inputLightDirection, float3 inputTarget)
    {
        neighborhood = inputNeighborhood;
        viewDirection = inputViewDirection;
        lightDirection = inputLightDirection;
        target = inputTarget;
    }
};

[[vk::binding(0, 1)]]
ByteAddressBuffer gInferenceParameters;

[[vk::binding(1, 1)]]
ByteAddressBuffer gInferenceLatent;

[[vk::binding(4, 2)]]
RWByteAddressBuffer gOptimalWeightGradients;

[[vk::binding(5, 2)]]
RWByteAddressBuffer gBiasGradients;

[[vk::binding(3, 2)]]
StructuredBuffer<float4> gSampleTargets;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gSampleGradients;

[[vk::binding(1, 2)]]
RWStructuredBuffer<uint4> gSampleTexelIndices;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> gSampleMetrics;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceGradientPushConstants> gTraining;

CoopVec<half, N> neuralAppearanceTrainingCoopVecFromFloatArray<let N : int>(float values[N])
{
    CoopVec<half, N> result;
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = half(values[index]);
    }
    return result;
}

[BackwardDerivativeOf(neuralAppearanceTrainingCoopVecFromFloatArray)]
void neuralAppearanceTrainingCoopVecFromFloatArrayBwd<let N : int>(
    inout DifferentialPair<float[N]> values, CoopVec<half, N> resultGrad)
{
    float valuesGrad[N];
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        valuesGrad[index] = float(resultGrad[index]);
    }
    values = diffPair(values.p, valuesGrad);
}

float[N] neuralAppearanceTrainingFloatArrayFromCoopVec<let N : int>(CoopVec<half, N> value)
{
    float result[N];
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = float(value[index]);
    }
    return result;
}

[BackwardDerivativeOf(neuralAppearanceTrainingFloatArrayFromCoopVec)]
void neuralAppearanceTrainingFloatArrayFromCoopVecBwd<let N : int>(
    inout DifferentialPair<CoopVec<half, N>> value, float[N] resultGrad)
{
    value = diffPair(value.p, neuralAppearanceTrainingCoopVecFromFloatArray(resultGrad));
}

CoopVec<half, Outputs> neuralAppearanceTrainingLinear<let Inputs : int, let Outputs : int>(
    CoopVec<half, Inputs> input, uint weightOffsetBytes, uint biasOffsetBytes,
    uint weightGradientOffsetBytes, uint rowStrideBytes)
{
    return coopVecMatMulAdd<half, Outputs>(
        input, CoopVecComponentType.Float16, gInferenceParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, gInferenceParameters, int(biasOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(rowStrideBytes));
}

[BackwardDerivativeOf(neuralAppearanceTrainingLinear)]
void neuralAppearanceTrainingLinearBwd<let Inputs : int, let Outputs : int>(
    inout DifferentialPair<CoopVec<half, Inputs>> input, uint weightOffsetBytes,
    uint biasOffsetBytes, uint weightGradientOffsetBytes, uint rowStrideBytes,
    CoopVec<half, Outputs> resultGrad)
{
    coopVecOuterProductAccumulate(resultGrad, input.p, gOptimalWeightGradients,
                                  int(weightGradientOffsetBytes), 0,
                                  CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);
    coopVecReduceSumAccumulate(resultGrad, gBiasGradients, int(biasOffsetBytes));
    CoopVec<half, Inputs> inputGrad = coopVecMatMul<half, Inputs>(
        resultGrad, CoopVecComponentType.Float16, gInferenceParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(rowStrideBytes));
    input = diffPair(input.p, inputGrad);
}

CoopVec<half, Outputs> neuralAppearanceTrainingLinearNoBias<let Inputs : int, let Outputs : int>(
    CoopVec<half, Inputs> input, uint weightOffsetBytes, uint weightGradientOffsetBytes,
    uint rowStrideBytes)
{
    return coopVecMatMul<half, Outputs>(
        input, CoopVecComponentType.Float16, gInferenceParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(rowStrideBytes));
}

[BackwardDerivativeOf(neuralAppearanceTrainingLinearNoBias)]
void neuralAppearanceTrainingLinearNoBiasBwd<let Inputs : int, let Outputs : int>(
    inout DifferentialPair<CoopVec<half, Inputs>> input, uint weightOffsetBytes,
    uint weightGradientOffsetBytes, uint rowStrideBytes, CoopVec<half, Outputs> resultGrad)
{
    coopVecOuterProductAccumulate(resultGrad, input.p, gOptimalWeightGradients,
                                  int(weightGradientOffsetBytes), 0,
                                  CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);
    CoopVec<half, Inputs> inputGrad = coopVecMatMul<half, Inputs>(
        resultGrad, CoopVecComponentType.Float16, gInferenceParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(rowStrideBytes));
    input = diffPair(input.p, inputGrad);
}

int neuralAppearanceRepeatCoordinate(int coordinate, int extent)
{
    int wrapped = coordinate % extent;
    return wrapped < 0 ? wrapped + extent : wrapped;
}

uint neuralAppearanceTexelIndex(int2 coordinate, uint width, uint height)
{
    int x = neuralAppearanceRepeatCoordinate(coordinate.x, int(width));
    int y = neuralAppearanceRepeatCoordinate(coordinate.y, int(height));
    return uint(y) * width + uint(x);
}

NeuralAppearanceLatentNeighborhood loadNeuralAppearanceLatentNeighborhood(
    float2 uv, uint width, uint height, out uint4 texelIndices)
{
    float2 texelPosition = frac(uv) * float2(width, height) - 0.5f;
    int2 baseTexel = int2(floor(texelPosition));

    texelIndices = uint4(
        neuralAppearanceTexelIndex(baseTexel, width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(1, 0), width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(0, 1), width, height),
        neuralAppearanceTexelIndex(baseTexel + int2(1, 1), width, height));

    float4 values[8] = {};
    for (uint tap = 0u; tap < 4u; ++tap)
    {
        for (uint textureIndex = 0u; textureIndex < 2u; ++textureIndex)
        {
            uint byteOffset =
                (texelIndices[tap] * kNeuralAppearanceLatentTextureCount + textureIndex) * 8u;
            values[tap * 2u + textureIndex] = float4(
                float(gInferenceLatent.Load<half>(byteOffset + 0u)),
                float(gInferenceLatent.Load<half>(byteOffset + 2u)),
                float(gInferenceLatent.Load<half>(byteOffset + 4u)),
                float(gInferenceLatent.Load<half>(byteOffset + 6u)));
        }
    }
    return NeuralAppearanceLatentNeighborhood(values, frac(texelPosition));
}

[Differentiable]
float3 neuralAppearanceTrainingEvaluate(NeuralAppearanceLatent latent,
                                        float3 viewDirection, float3 lightDirection)
{
    float latentValues[8] = {
        latent.chunks[0].x, latent.chunks[0].y, latent.chunks[0].z, latent.chunks[0].w,
        latent.chunks[1].x, latent.chunks[1].y, latent.chunks[1].z, latent.chunks[1].w,
    };
    CoopVec<half, 8> latentVector = neuralAppearanceTrainingCoopVecFromFloatArray(latentValues);
    float frameValues[12] = neuralAppearanceTrainingFloatArrayFromCoopVec(
        neuralAppearanceTrainingLinear<8, 12>(
            latentVector, kNeuralAppearanceFrameWeightByteOffset, kNeuralAppearanceFrameBiasByteOffset,
            gTraining.optimalWeightOffsets[0], kNeuralAppearanceFrameWeightRowStrideBytes));
    float frame0Raw[6];
    float frame1Raw[6];
    [ForceUnroll]
    for (uint value = 0u; value < 6u; ++value)
    {
        frame0Raw[value] = frameValues[value];
        frame1Raw[value] = frameValues[6u + value];
    }
    NeuralMaterialContext context = NeuralMaterialContext(
        neuralAppearanceBuildFrame(frame0Raw), neuralAppearanceBuildFrame(frame1Raw));

    CoopVec<half, 32> spatial = neuralAppearanceTrainingLinear<8, 32>(
        latentVector, kNeuralAppearanceSpatialWeightByteOffset, kNeuralAppearanceSpatialBiasByteOffset,
        gTraining.optimalWeightOffsets[1], kNeuralAppearanceSpatialWeightRowStrideBytes);
    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    CoopVec<half, 12> directionVector =
        neuralAppearanceTrainingCoopVecFromFloatArray<12>(directionFeatures.values);
    CoopVec<half, 32> directional = neuralAppearanceTrainingLinearNoBias<12, 32>(
        directionVector, kNeuralAppearanceDirectionWeightByteOffset, gTraining.optimalWeightOffsets[2],
        kNeuralAppearanceDirectionWeightRowStrideBytes);
    float spatialValues[32] = neuralAppearanceTrainingFloatArrayFromCoopVec(spatial);
    float directionalValues[32] = neuralAppearanceTrainingFloatArrayFromCoopVec(directional);
    float firstHiddenValues[32];
    [ForceUnroll]
    for (int value = 0; value < 32; ++value)
    {
        firstHiddenValues[value] = max(spatialValues[value] + directionalValues[value], 0.0f);
    }
    CoopVec<half, 32> firstHidden = neuralAppearanceTrainingCoopVecFromFloatArray(firstHiddenValues);
    CoopVec<half, 32> secondHidden = neuralAppearanceTrainingLinear<32, 32>(
        firstHidden, kNeuralAppearanceHiddenWeightByteOffset, kNeuralAppearanceHiddenBiasByteOffset,
        gTraining.optimalWeightOffsets[3], kNeuralAppearanceHiddenWeightRowStrideBytes);
    float secondHiddenValues[32] = neuralAppearanceTrainingFloatArrayFromCoopVec(secondHidden);
    [ForceUnroll]
    for (int value = 0; value < 32; ++value)
    {
        secondHiddenValues[value] = max(secondHiddenValues[value], 0.0f);
    }
    CoopVec<half, 3> logits = neuralAppearanceTrainingLinear<32, 3>(
        neuralAppearanceTrainingCoopVecFromFloatArray(secondHiddenValues),
        kNeuralAppearanceOutputWeightByteOffset, kNeuralAppearanceOutputBiasByteOffset,
        gTraining.optimalWeightOffsets[4], kNeuralAppearanceOutputWeightRowStrideBytes);
    float outputValues[3] = neuralAppearanceTrainingFloatArrayFromCoopVec(logits);
    return neuralAppearanceDecodeOutput(float3(outputValues[0], outputValues[1], outputValues[2]));
}

[Differentiable]
float neuralAppearanceTrainingLoss(NeuralAppearanceLossInputs inputs)
{
    NeuralAppearanceLatent latent = interpolateNeuralAppearanceLatent(inputs.neighborhood);
    float3 prediction = neuralAppearanceTrainingEvaluate(latent, inputs.viewDirection, inputs.lightDirection);
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(inputs.target, float3(0.0f)));
    return dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
}

[shader("compute")]
[require(spvGroupNonUniformBallot, spvGroupNonUniformArithmetic, spvCooperativeVectorNV, spvCooperativeVectorTrainingNV)]
[numthreads(32, 1, 1)]
void evaluateGradientsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (sampleIndex >= batchSize || batchSize == 0u ||
        gTraining.latentWidth != kNeuralAppearanceLatentWidth ||
        gTraining.latentHeight != kNeuralAppearanceLatentHeight)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceTrainingSample(
        gTraining.trainingStep, sampleIndex,
        gTraining.mollificationStepCount, gTraining.initialAngleRadians);
    float4 targetRecord = gSampleTargets[sampleIndex];

    uint4 texelIndices;
    NeuralAppearanceLossInputs inputs = NeuralAppearanceLossInputs(
        loadNeuralAppearanceLatentNeighborhood(
            sample.uv, gTraining.latentWidth, gTraining.latentHeight, texelIndices),
        sample.directions.viewDirection, sample.directions.lightDirection, targetRecord.xyz);

    DifferentialPair<NeuralAppearanceLossInputs> pair = diffPair(inputs);
    bwd_diff(neuralAppearanceTrainingLoss)(pair, 1.0f);

    uint gradientBase = sampleIndex * kNeuralAppearanceGradientChunkCount;
    for (uint tapChunk = 0u; tapChunk < 8u; ++tapChunk)
    {
        gSampleGradients[gradientBase + kNeuralAppearanceParameterChunkCount + tapChunk] =
            pair.d.neighborhood.values[tapChunk];
    }

    NeuralAppearanceLatent latent = interpolateNeuralAppearanceLatent(inputs.neighborhood);
    float3 prediction = neuralAppearanceTrainingEvaluate(
        latent, inputs.viewDirection, inputs.lightDirection);
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(inputs.target, float3(0.0f)));
    float loss = dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
    bool finite = isfinite(loss) && all(isfinite(prediction)) && all(isfinite(inputs.target));
    gSampleTexelIndices[sampleIndex] = texelIndices;
    gSampleMetrics[sampleIndex] = float4(
        loss, dot(prediction, float3(1.0f / 3.0f)),
        targetRecord.w, finite ? 1.0f : 0.0f);
}
