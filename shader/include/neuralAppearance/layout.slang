module layout;

public static const uint kNeuralAppearanceLatentWidth = 64u;
public static const uint kNeuralAppearanceLatentHeight = 64u;
public static const uint kNeuralAppearanceLatentTexelCount =
    kNeuralAppearanceLatentWidth * kNeuralAppearanceLatentHeight;
public static const uint kNeuralAppearanceLatentTextureCount = 2u;
public static const uint kNeuralAppearanceLatentChunkCount =
    kNeuralAppearanceLatentTexelCount * kNeuralAppearanceLatentTextureCount;

public static const uint kNeuralAppearanceParameterChunkCount = 484u;
public static const uint kNeuralAppearanceParameterScalarCount = 1936u;
public static const uint kNeuralAppearanceBatchSize = 64u;
public static const uint kNeuralAppearanceGradientChunkCount = 492u;
public static const uint kNeuralAppearanceTrainingSchemaMagic = 0x4e415450u;

public static const uint kNeuralAppearanceFrameWeightOffset = 0u;
public static const uint kNeuralAppearanceFrameBiasOffset = 96u;
public static const uint kNeuralAppearanceSpatialWeightOffset = 108u;
public static const uint kNeuralAppearanceDirectionWeightOffset = 364u;
public static const uint kNeuralAppearanceFirstBiasOffset = 748u;
public static const uint kNeuralAppearanceSecondWeightOffset = 780u;
public static const uint kNeuralAppearanceSecondBiasOffset = 1804u;
public static const uint kNeuralAppearanceOutputWeightOffset = 1836u;
public static const uint kNeuralAppearanceOutputBiasOffset = 1932u;
public static const uint kNeuralAppearancePaddingOffset = 1935u;

public struct NeuralAppearanceParameters : IDifferentiable
{
    public float4 chunks[kNeuralAppearanceParameterChunkCount] = {};

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(float4 inputChunks[kNeuralAppearanceParameterChunkCount])
    {
        for (uint chunk = 0u; chunk < kNeuralAppearanceParameterChunkCount; ++chunk)
        {
            chunks[chunk] = inputChunks[chunk];
        }
    }
};

public struct NeuralAppearanceLatent : IDifferentiable
{
    public float4 chunks[kNeuralAppearanceLatentTextureCount] = {};

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(float4 chunk0, float4 chunk1)
    {
        chunks[0] = chunk0;
        chunks[1] = chunk1;
    }
};

public struct NeuralAppearanceLatentNeighborhood : IDifferentiable
{
    public float4 values[8] = {};
    public float2 fraction = float2(0.0f);

    [Differentiable]
    public __init()
    {
    }

    [Differentiable]
    public __init(float4 inputValues[8], float2 inputFraction)
    {
        for (uint value = 0u; value < 8u; ++value)
        {
            values[value] = inputValues[value];
        }
        fraction = inputFraction;
    }
};

[Differentiable]
public float neuralAppearanceParameterScalar(NeuralAppearanceParameters parameters, uint scalarIndex)
{
    return parameters.chunks[scalarIndex >> 2u][scalarIndex & 3u];
}

public float neuralAppearanceParameterScalarRuntime(
    StructuredBuffer<float4> parameterBuffer, uint scalarIndex)
{
    return parameterBuffer[scalarIndex >> 2u][scalarIndex & 3u];
}

public NeuralAppearanceParameters loadNeuralAppearanceParameters(StructuredBuffer<float4> parameterBuffer)
{
    float4 chunks[kNeuralAppearanceParameterChunkCount] = {};
    for (uint chunk = 0u; chunk < kNeuralAppearanceParameterChunkCount; ++chunk)
    {
        chunks[chunk] = parameterBuffer[chunk];
    }
    return NeuralAppearanceParameters(chunks);
}

[Differentiable]
public NeuralAppearanceLatent interpolateNeuralAppearanceLatent(
    NeuralAppearanceLatentNeighborhood neighborhood)
{
    float2 f = neighborhood.fraction;
    float4 weights = float4((1.0f - f.x) * (1.0f - f.y), f.x * (1.0f - f.y),
                            (1.0f - f.x) * f.y, f.x * f.y);

    return NeuralAppearanceLatent(
        neighborhood.values[0u] * weights.x + neighborhood.values[2u] * weights.y +
            neighborhood.values[4u] * weights.z + neighborhood.values[6u] * weights.w,
        neighborhood.values[1u] * weights.x + neighborhood.values[3u] * weights.y +
            neighborhood.values[5u] * weights.z + neighborhood.values[7u] * weights.w);
}
