module evaluateGradients;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.model;
import include.neuralAppearance.quality;
import include.neuralAppearance.trainingSampling;

struct NeuralAppearanceGradientPushConstants
{
    uint trainingStep;
    uint batchSize;
    uint trainingSeed;
    uint optimalWeightOffsets[kNeuralAppearanceAffineLayerCount];
};

// The material input is the only differentiable entry point into the graph. Its
// gradient is discarded: it exists so the backward pass reaches every affine
// layer through the cooperative-vector accumulation hooks. Targets are kept as
// plain float3 values because a reverse-mode chain that carries an
// IDifferentiable aggregate across a module boundary loses its gradient.
struct NeuralAppearanceLossInputs : IDifferentiable
{
    NeuralAppearanceMaterialInput material = NeuralAppearanceMaterialInput();
    no_diff float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 lightDirection = float3(0.0f, 1.0f, 0.0f);
    no_diff float3 targetDiffuse = float3(0.0f);
    no_diff float3 targetSpecular = float3(0.0f);

    __init()
    {
    }

    __init(
        NeuralAppearanceMaterialInput inputMaterial, float3 inputViewDirection,
        float3 inputLightDirection, float3 inputTargetDiffuse, float3 inputTargetSpecular)
    {
        material = inputMaterial;
        viewDirection = inputViewDirection;
        lightDirection = inputLightDirection;
        targetDiffuse = inputTargetDiffuse;
        targetSpecular = inputTargetSpecular;
    }
};

[[vk::binding(0, 1)]]
ByteAddressBuffer gInferenceParameters;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gSampleMetrics;

[[vk::binding(1, 2)]]
StructuredBuffer<float4> gSampleTargets;

[[vk::binding(2, 2)]]
RWByteAddressBuffer gOptimalWeightGradients;

[[vk::binding(3, 2)]]
RWByteAddressBuffer gBiasGradients;

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
    CoopVec<half, Inputs> input, uint layerIndex, uint weightGradientOffsetBytes)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    return coopVecMatMulAdd<half, Outputs>(
        input, CoopVecComponentType.Float16, gInferenceParameters, int(layer.weightByteOffset),
        CoopVecComponentType.Float16, gInferenceParameters, int(layer.biasByteOffset),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(layer.rowStrideBytes));
}

[BackwardDerivativeOf(neuralAppearanceTrainingLinear)]
void neuralAppearanceTrainingLinearBwd<let Inputs : int, let Outputs : int>(
    inout DifferentialPair<CoopVec<half, Inputs>> input, uint layerIndex,
    uint weightGradientOffsetBytes, CoopVec<half, Outputs> resultGrad)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    coopVecOuterProductAccumulate(resultGrad, input.p, gOptimalWeightGradients,
                                  int(weightGradientOffsetBytes), 0,
                                  CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);
    coopVecReduceSumAccumulate(resultGrad, gBiasGradients, int(layer.biasByteOffset));
    CoopVec<half, Inputs> inputGrad = coopVecMatMul<half, Inputs>(
        resultGrad, CoopVecComponentType.Float16, gInferenceParameters, int(layer.weightByteOffset),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(layer.rowStrideBytes));
    input = diffPair(input.p, inputGrad);
}

CoopVec<half, Outputs> neuralAppearanceTrainingLinearNoBias<let Inputs : int, let Outputs : int>(
    CoopVec<half, Inputs> input, uint layerIndex, uint weightGradientOffsetBytes)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    return coopVecMatMul<half, Outputs>(
        input, CoopVecComponentType.Float16, gInferenceParameters, int(layer.weightByteOffset),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(layer.rowStrideBytes));
}

[BackwardDerivativeOf(neuralAppearanceTrainingLinearNoBias)]
void neuralAppearanceTrainingLinearNoBiasBwd<let Inputs : int, let Outputs : int>(
    inout DifferentialPair<CoopVec<half, Inputs>> input, uint layerIndex,
    uint weightGradientOffsetBytes, CoopVec<half, Outputs> resultGrad)
{
    NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
    coopVecOuterProductAccumulate(resultGrad, input.p, gOptimalWeightGradients,
                                  int(weightGradientOffsetBytes), 0,
                                  CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);
    CoopVec<half, Inputs> inputGrad = coopVecMatMul<half, Inputs>(
        resultGrad, CoopVecComponentType.Float16, gInferenceParameters, int(layer.weightByteOffset),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(layer.rowStrideBytes));
    input = diffPair(input.p, inputGrad);
}

[Differentiable]
float[N] neuralAppearanceTrainingLeakyRelu<let N : int>(float values[N])
{
    float result[N];
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = neuralAppearanceLeakyRelu(values[index]);
    }
    return result;
}

[Differentiable]
float[kNeuralAppearanceOutputCount] neuralAppearanceTrainingEvaluateLogits(
    NeuralAppearanceMaterialInput material, no_diff float3 viewDirection,
    no_diff float3 lightDirection)
{
    CoopVec<half, 12> materialVector =
        neuralAppearanceTrainingCoopVecFromFloatArray<12>(material.values);
    float encodeFirst[32] = neuralAppearanceTrainingLeakyRelu(
        neuralAppearanceTrainingFloatArrayFromCoopVec(neuralAppearanceTrainingLinear<12, 32>(
            materialVector, kNeuralAppearanceEncodeFirstLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceEncodeFirstLayer])));
    float encodeSecond[32] = neuralAppearanceTrainingLeakyRelu(
        neuralAppearanceTrainingFloatArrayFromCoopVec(neuralAppearanceTrainingLinear<32, 32>(
            neuralAppearanceTrainingCoopVecFromFloatArray(encodeFirst), kNeuralAppearanceEncodeSecondLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceEncodeSecondLayer])));
    CoopVec<half, 8> latentVector = neuralAppearanceTrainingLinear<32, 8>(
        neuralAppearanceTrainingCoopVecFromFloatArray(encodeSecond), kNeuralAppearanceEncodeThirdLayer,
        gTraining.optimalWeightOffsets[kNeuralAppearanceEncodeThirdLayer]);

    NeuralMaterialContext context = NeuralMaterialContext(neuralAppearanceBuildFrame(
        neuralAppearanceTrainingFloatArrayFromCoopVec(neuralAppearanceTrainingLinear<8, 6>(
            latentVector, kNeuralAppearanceFrameLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceFrameLayer]))));
    context.spatialPrefix = neuralAppearanceTrainingFloatArrayFromCoopVec(
        neuralAppearanceTrainingLinear<8, 32>(
            latentVector, kNeuralAppearanceSpatialLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceSpatialLayer]));

    NeuralAppearanceDirectionFeatures directionFeatures =
        neuralAppearanceDirectionFeatures(context, viewDirection, lightDirection);
    float directional[32] = neuralAppearanceTrainingFloatArrayFromCoopVec(
        neuralAppearanceTrainingLinearNoBias<8, 32>(
            neuralAppearanceTrainingCoopVecFromFloatArray<8>(directionFeatures.values),
            kNeuralAppearanceDirectionLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceDirectionLayer]));

    float firstHidden[32];
    [ForceUnroll]
    for (int index = 0; index < 32; ++index)
    {
        firstHidden[index] = neuralAppearanceLeakyRelu(context.spatialPrefix[index] + directional[index]);
    }
    float secondHidden[32] = neuralAppearanceTrainingLeakyRelu(
        neuralAppearanceTrainingFloatArrayFromCoopVec(neuralAppearanceTrainingLinear<32, 32>(
            neuralAppearanceTrainingCoopVecFromFloatArray(firstHidden), kNeuralAppearanceHiddenLayer,
            gTraining.optimalWeightOffsets[kNeuralAppearanceHiddenLayer])));
    return neuralAppearanceTrainingFloatArrayFromCoopVec(neuralAppearanceTrainingLinear<32, 6>(
        neuralAppearanceTrainingCoopVecFromFloatArray(secondHidden), kNeuralAppearanceOutputLayer,
        gTraining.optimalWeightOffsets[kNeuralAppearanceOutputLayer]));
}

[Differentiable]
float neuralAppearanceTrainingLoss(NeuralAppearanceLossInputs inputs)
{
    float logits[kNeuralAppearanceOutputCount] = neuralAppearanceTrainingEvaluateLogits(
        inputs.material, inputs.viewDirection, inputs.lightDirection);
    float3 diffuse = neuralAppearanceDecodeLobe(float3(logits[0], logits[1], logits[2]),
                                                kNeuralAppearanceDiffuseLogMaximum);
    float3 specular = neuralAppearanceDecodeLobe(float3(logits[3], logits[4], logits[5]),
                                                 kNeuralAppearanceSpecularLogMaximum);
    return kNeuralAppearanceDiffuseLossWeight *
               neuralAppearanceChannelTrainingLoss(diffuse, inputs.targetDiffuse) +
           kNeuralAppearanceSpecularLossWeight *
               neuralAppearanceChannelTrainingLoss(specular, inputs.targetSpecular);
}

[shader("compute")]
[require(spvGroupNonUniformBallot, spvGroupNonUniformArithmetic, spvCooperativeVectorNV, spvCooperativeVectorTrainingNV)]
[numthreads(32, 1, 1)]
void evaluateGradientsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint sampleIndex = dispatchThreadID.x;
    uint batchSize = min(gTraining.batchSize, kNeuralAppearanceBatchSize);
    if (sampleIndex >= batchSize || batchSize == 0u)
    {
        return;
    }

    NeuralAppearanceTrainingSample sample = neuralAppearanceTrainingSample(
        gTraining.trainingStep, sampleIndex, gTraining.trainingSeed);
    NeuralAppearanceLobes target = NeuralAppearanceLobes(
        gSampleTargets[sampleIndex * 2u + 0u].xyz, gSampleTargets[sampleIndex * 2u + 1u].xyz);
    NeuralAppearanceLossInputs inputs = NeuralAppearanceLossInputs(
        neuralAppearanceMaterialInput(neuralAppearanceFixtureSample(sample.materialSeed)),
        sample.directions.viewDirection, sample.directions.lightDirection, target.diffuse, target.specular);

    DifferentialPair<NeuralAppearanceLossInputs> pair = diffPair(inputs);
    bwd_diff(neuralAppearanceTrainingLoss)(pair, 1.0f);

    NeuralAppearanceLobes prediction = neuralAppearanceDecodeLobes(neuralAppearanceTrainingEvaluateLogits(
        inputs.material, inputs.viewDirection, inputs.lightDirection));
    float acceptanceLoss = neuralAppearanceLobeSafeLogLoss(prediction, target);
    float optimizationLoss = neuralAppearanceLobeTrainingLoss(prediction, target);
    bool finite = isfinite(acceptanceLoss) && isfinite(optimizationLoss) &&
                  all(isfinite(prediction.diffuse)) && all(isfinite(prediction.specular)) &&
                  all(isfinite(target.diffuse)) && all(isfinite(target.specular));
    gSampleMetrics[sampleIndex] = float4(
        acceptanceLoss, optimizationLoss,
        dot(prediction.diffuse + prediction.specular, float3(1.0f / 3.0f)),
        finite ? 1.0f : 0.0f);
}
