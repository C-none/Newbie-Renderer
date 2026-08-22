module mlp;
import common;

// Cooperative-vector inference benchmark core. Library module: no stage entry
// point. Every benchmark entry point is one width/component-type pair that
// evaluates the same square eight-layer MLP, so GPU time differences come from
// the cooperative-vector multiply itself rather than from resource layout.

public static const uint kCoopVecBenchLayerCount = 8u;
public static const float kCoopVecBenchLeakySlope = 0.01f;

public struct CoopVecBenchPushConstants
{
    public uint weightOffsets[kCoopVecBenchLayerCount];
    public uint biasOffsets[kCoopVecBenchLayerCount];
    public uint elementCount;
    public uint iterationCount;
};

CoopVec<half, Width> coopVecBenchInput<let Width : int>(uint threadIndex)
{
    CoopVec<half, Width> result;
    [ForceUnroll]
    for (int index = 0; index < Width; ++index)
    {
        result[index] = half(frac(float(threadIndex + uint(index) + 1u) * 0.017f) * 2.0f - 1.0f);
    }
    return result;
}

CoopVec<half, Width> coopVecBenchLeakyRelu<let Width : int>(CoopVec<half, Width> value)
{
    CoopVec<half, Width> result = value;
    [ForceUnroll]
    for (int index = 0; index < Width; ++index)
    {
        result[index] = max(value[index], value[index] * half(kCoopVecBenchLeakySlope));
    }
    return result;
}

CoopVec<half, Width> coopVecBenchLayer<let Width : int>(
    CoopVec<half, Width> input, ByteAddressBuffer parameters, uint weightOffset, uint biasOffset,
    constexpr CoopVecComponentType interpretation)
{
    return coopVecMatMulAdd<half, Width>(
        input,
        interpretation,
        parameters,
        int(weightOffset),
        interpretation,
        parameters,
        int(biasOffset),
        CoopVecComponentType.Float16,
        CoopVecMatrixLayout.InferencingOptimal,
        false,
        0);
}

public void coopVecBenchRun<let Width : int>(
    ByteAddressBuffer parameters, RWStructuredBuffer<float> results, CoopVecBenchPushConstants bench,
    uint threadIndex, constexpr CoopVecComponentType interpretation)
{
    if (threadIndex >= bench.elementCount)
    {
        return;
    }

    CoopVec<half, Width> value = coopVecBenchInput<Width>(threadIndex);
    for (uint iteration = 0u; iteration < bench.iterationCount; ++iteration)
    {
        [ForceUnroll]
        for (uint layer = 0u; layer < kCoopVecBenchLayerCount; ++layer)
        {
            value = coopVecBenchLeakyRelu<Width>(coopVecBenchLayer<Width>(
                value, parameters, bench.weightOffsets[layer], bench.biasOffsets[layer], interpretation));
        }
    }

    // The single retained lane keeps the whole chain live without adding a
    // width-proportional store to the measured loop.
    results[threadIndex] = float(value[0]);
}
