module coopVecV2Numerics;
import common;

static const uint kCoopVecV2LayerCount = 5u;

struct CoopVecV2NumericsPushConstants
{
    uint optimalWeightOffsets[kCoopVecV2LayerCount];
    uint contributionCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::binding(0, 0)]]
ByteAddressBuffer gParameters;

[[vk::binding(1, 0)]]
RWByteAddressBuffer gOptimalWeightGradients;

[[vk::binding(2, 0)]]
RWByteAddressBuffer gBiasGradients;

[[vk::binding(3, 0)]]
RWStructuredBuffer<float> gForwardResults;

[[vk::binding(4, 0)]]
RWStructuredBuffer<float> gInputGradientResults;

[[vk::binding(5, 0)]]
RWStructuredBuffer<float> gAdamMaster;

[[vk::binding(6, 0)]]
RWStructuredBuffer<float> gAdamFirstMoment;

[[vk::binding(7, 0)]]
RWStructuredBuffer<float> gAdamSecondMoment;

[[vk::binding(8, 0)]]
RWByteAddressBuffer gAdamFp16Mirror;

[[vk::push_constant]]
ConstantBuffer<CoopVecV2NumericsPushConstants> gNumerics;

CoopVec<half, N> numericsInput<let N : int>(uint layer)
{
    CoopVec<half, N> result;
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = half(float(layer + uint(index) + 1u) * (1.0f / 32.0f));
    }
    return result;
}

CoopVec<half, N> numericsOutputGradient<let N : int>(uint layer)
{
    CoopVec<half, N> result;
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        result[index] = half(float(layer + uint(index) + 1u) * (1.0f / 128.0f));
    }
    return result;
}

void numericsStoreForward<let N : int>(uint layer, CoopVec<half, N> values)
{
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        gForwardResults[layer * 32u + uint(index)] = float(values[index]);
    }
}

void numericsStoreInputGradient<let N : int>(uint layer, CoopVec<half, N> values)
{
    [ForceUnroll]
    for (int index = 0; index < N; ++index)
    {
        gInputGradientResults[layer * 32u + uint(index)] = float(values[index]);
    }
}

void numericsRunBiasLinear<let Inputs : int, let Outputs : int>(
    uint layer, uint contribution, uint weightOffsetBytes, uint biasOffsetBytes,
    uint optimalWeightOffsetBytes, uint rowStrideBytes)
{
    CoopVec<half, Inputs> input = numericsInput<Inputs>(layer);
    CoopVec<half, Outputs> forward = coopVecMatMulAdd<half, Outputs>(
        input, CoopVecComponentType.Float16, gParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, gParameters, int(biasOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(rowStrideBytes));
    CoopVec<half, Outputs> outputGradient = numericsOutputGradient<Outputs>(layer);
    CoopVec<half, Inputs> inputGradient = coopVecMatMul<half, Inputs>(
        outputGradient, CoopVecComponentType.Float16, gParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(rowStrideBytes));

    coopVecOuterProductAccumulate(
        outputGradient, input, gOptimalWeightGradients, int(optimalWeightOffsetBytes), 0,
        CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);
    coopVecReduceSumAccumulate(outputGradient, gBiasGradients, int(biasOffsetBytes));

    if (contribution == 0u)
    {
        numericsStoreForward(layer, forward);
        numericsStoreInputGradient(layer, inputGradient);
    }
}

void numericsRunNoBiasLinear<let Inputs : int, let Outputs : int>(
    uint layer, uint contribution, uint weightOffsetBytes,
    uint optimalWeightOffsetBytes, uint rowStrideBytes)
{
    CoopVec<half, Inputs> input = numericsInput<Inputs>(layer);
    CoopVec<half, Outputs> forward = coopVecMatMul<half, Outputs>(
        input, CoopVecComponentType.Float16, gParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.RowMajor, false, int(rowStrideBytes));
    CoopVec<half, Outputs> outputGradient = numericsOutputGradient<Outputs>(layer);
    CoopVec<half, Inputs> inputGradient = coopVecMatMul<half, Inputs>(
        outputGradient, CoopVecComponentType.Float16, gParameters, int(weightOffsetBytes),
        CoopVecComponentType.Float16, CoopVecMatrixLayout.ColumnMajor, false, int(rowStrideBytes));

    coopVecOuterProductAccumulate(
        outputGradient, input, gOptimalWeightGradients, int(optimalWeightOffsetBytes), 0,
        CoopVecMatrixLayout.TrainingOptimal, CoopVecComponentType.Float16);

    if (contribution == 0u)
    {
        numericsStoreForward(layer, forward);
        numericsStoreInputGradient(layer, inputGradient);
    }
}

void numericsRunAdam()
{
    static const float beta1 = 0.9f;
    static const float beta2 = 0.999f;
    static const float epsilon = 1.0e-7f;
    static const float learningRate = 0.00390625f;

    for (uint scalar = 0u; scalar < 16u; ++scalar)
    {
        float gradient = float(scalar + 1u) * (1.0f / 256.0f);
        float firstMoment = beta1 * gAdamFirstMoment[scalar] + (1.0f - beta1) * gradient;
        float secondMoment = beta2 * gAdamSecondMoment[scalar] +
                             (1.0f - beta2) * gradient * gradient;
        float master = gAdamMaster[scalar] - learningRate *
            (firstMoment / (1.0f - beta1)) /
            (sqrt(secondMoment / (1.0f - beta2)) + epsilon);
        gAdamMaster[scalar] = master;
        gAdamFirstMoment[scalar] = firstMoment;
        gAdamSecondMoment[scalar] = secondMoment;
        gAdamFp16Mirror.Store<half>(scalar * 2u, half(master));
    }
}

[shader("compute")]
[require(spvGroupNonUniformBallot, spvGroupNonUniformArithmetic, spvCooperativeVectorNV, spvCooperativeVectorTrainingNV)]
[numthreads(1, 1, 1)]
void coopVecV2NumericsMain(uint3 groupID: SV_GroupID)
{
    uint contributionCount = max(gNumerics.contributionCount, 1u);
    uint layer = groupID.x / contributionCount;
    uint contribution = groupID.x % contributionCount;
    if (layer >= kCoopVecV2LayerCount)
    {
        return;
    }

    if (layer == 0u)
    {
        numericsRunBiasLinear<8, 12>(layer, contribution, 0u, 192u,
                                     gNumerics.optimalWeightOffsets[0u], 16u);
    }
    else if (layer == 1u)
    {
        numericsRunBiasLinear<8, 32>(layer, contribution, 256u, 768u,
                                     gNumerics.optimalWeightOffsets[1u], 16u);
    }
    else if (layer == 2u)
    {
        numericsRunNoBiasLinear<12, 32>(layer, contribution, 832u,
                                        gNumerics.optimalWeightOffsets[2u], 24u);
    }
    else if (layer == 3u)
    {
        numericsRunBiasLinear<32, 32>(layer, contribution, 1600u, 3648u,
                                      gNumerics.optimalWeightOffsets[3u], 64u);
    }
    else
    {
        numericsRunBiasLinear<32, 3>(layer, contribution, 3712u, 3904u,
                                     gNumerics.optimalWeightOffsets[4u], 64u);
    }

    if (groupID.x == 0u)
    {
        numericsRunAdam();
    }
}
