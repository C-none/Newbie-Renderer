module initializeTraining;
import common;
import include.neuralAppearance.fixture;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

[[vk::binding(0, 1)]]
StructuredBuffer<uint4> gTrainingControl;

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

float neuralAppearanceInitialRandom(uint scalarIndex, float scale)
{
    float unitRandom = neuralAppearanceTrainingRandom(0x6a09e667u, scalarIndex);
    return (2.0f * unitRandom - 1.0f) * scale;
}

float neuralAppearanceInitialFrameWeight(uint scalarIndex)
{
    uint row = scalarIndex / 8u;
    uint input = scalarIndex % 8u;

    if ((row == 0u || row == 6u) && input == 4u) return 1.0f;
    if ((row == 2u || row == 8u) && input == 5u) return 1.0f;
    if (row == 3u && input == 6u) return 1.0f;
    if (row == 5u && input == 7u) return 1.0f;
    if (row == 9u && input == 7u) return -1.0f;
    if (row == 11u && input == 6u) return 1.0f;
    return 0.0f;
}

float neuralAppearanceInitialParameter(uint scalarIndex)
{
    if (scalarIndex < kNeuralAppearanceFrameBiasOffset)
    {
        return neuralAppearanceInitialFrameWeight(scalarIndex);
    }
    if (scalarIndex < kNeuralAppearanceSpatialWeightOffset)
    {
        uint row = scalarIndex - kNeuralAppearanceFrameBiasOffset;
        return (row == 1u || row == 7u) ? 1.0f : 0.0f;
    }
    if (scalarIndex < kNeuralAppearanceFirstBiasOffset)
    {
        return neuralAppearanceInitialRandom(scalarIndex, 0.12f);
    }
    if (scalarIndex < kNeuralAppearanceSecondWeightOffset)
    {
        return 0.08f;
    }
    if (scalarIndex < kNeuralAppearanceSecondBiasOffset)
    {
        return neuralAppearanceInitialRandom(scalarIndex, 0.08f);
    }
    if (scalarIndex < kNeuralAppearanceOutputWeightOffset)
    {
        return 0.08f;
    }
    if (scalarIndex < kNeuralAppearanceOutputBiasOffset)
    {
        return neuralAppearanceInitialRandom(scalarIndex, 0.08f);
    }
    if (scalarIndex < kNeuralAppearancePaddingOffset)
    {
        return 0.0f;
    }
    return 0.0f;
}

[shader("compute")]
[numthreads(64, 1, 1)]
void initializeTrainingMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint4 control = gTrainingControl[0];
    if (control.y == 0u || control.z != kNeuralAppearanceTrainingSchemaMagic)
    {
        return;
    }

    uint chunkIndex = dispatchThreadID.x;
    if (chunkIndex < kNeuralAppearanceParameterChunkCount)
    {
        uint scalar = chunkIndex * 4u;
        gModelParameters[chunkIndex] = float4(
            neuralAppearanceInitialParameter(scalar + 0u),
            neuralAppearanceInitialParameter(scalar + 1u),
            neuralAppearanceInitialParameter(scalar + 2u),
            neuralAppearanceInitialParameter(scalar + 3u));
        gModelMoments[chunkIndex] = float4(0.0f);
        gModelMoments[kNeuralAppearanceParameterChunkCount + chunkIndex] = float4(0.0f);
    }

    if (chunkIndex < kNeuralAppearanceLatentChunkCount)
    {
        uint texelIndex = chunkIndex / kNeuralAppearanceLatentTextureCount;
        uint textureIndex = chunkIndex % kNeuralAppearanceLatentTextureCount;
        uint2 texel = uint2(texelIndex % kNeuralAppearanceLatentWidth,
                            texelIndex / kNeuralAppearanceLatentWidth);
        NeuralAppearanceLatent latent = neuralAppearanceFixtureLatentTexel(texel);
        gTrainingLatent[chunkIndex] = latent.chunks[textureIndex];
        gLatentMoments[chunkIndex] = float4(0.0f);
        gLatentMoments[kNeuralAppearanceLatentChunkCount + chunkIndex] = float4(0.0f);
    }

    if (chunkIndex == 0u)
    {
        gTrainingStatus[0] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gTrainingStatus[1] = float4(0.0f);
    }
}
