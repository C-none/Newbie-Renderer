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

[[vk::binding(5, 2)]]
RWByteAddressBuffer gInferenceParameters;

[[vk::binding(6, 2)]]
RWByteAddressBuffer gInferenceLatent;

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
    if (control.y == 0u || control.y > 2u || control.z != kNeuralAppearanceTrainingSchemaMagic)
    {
        return;
    }

    uint chunkIndex = dispatchThreadID.x;
    // The fixed V2 model blob contains alignment padding. It is part of the
    // portable ABI and must remain zero for both fresh and restored mirrors.
    if (chunkIndex < (kNeuralAppearanceInferenceParameterBytes / 4u))
    {
        uint byteOffset = chunkIndex * 4u;
        bool padding0 = (byteOffset >= 216u && byteOffset < 256u) ||
                        (byteOffset >= 3910u && byteOffset < kNeuralAppearanceInferenceParameterBytes);
        bool padding1 = (byteOffset + 2u >= 216u && byteOffset + 2u < 256u) ||
                        (byteOffset + 2u >= 3910u && byteOffset + 2u < kNeuralAppearanceInferenceParameterBytes);
        if (padding0)
        {
            gInferenceParameters.Store<half>(byteOffset, half(0.0h));
        }
        if (padding1)
        {
            gInferenceParameters.Store<half>(byteOffset + 2u, half(0.0h));
        }
    }
    if (chunkIndex < kNeuralAppearanceParameterChunkCount)
    {
        uint scalar = chunkIndex * 4u;
        float4 values = control.y == 1u
                            ? float4(
                                  neuralAppearanceInitialParameter(scalar + 0u),
                                  neuralAppearanceInitialParameter(scalar + 1u),
                                  neuralAppearanceInitialParameter(scalar + 2u),
                                  neuralAppearanceInitialParameter(scalar + 3u))
                            : gModelParameters[chunkIndex];
        gModelParameters[chunkIndex] = values;
        if (control.y == 1u)
        {
            gModelMoments[chunkIndex] = float4(0.0f);
            gModelMoments[kNeuralAppearanceParameterChunkCount + chunkIndex] = float4(0.0f);
        }

        [ForceUnroll]
        for (uint lane = 0u; lane < 4u; ++lane)
        {
            uint scalarIndex = scalar + lane;
            if (scalarIndex < kNeuralAppearanceParameterScalarCount)
            {
                gInferenceParameters.Store<half>(neuralAppearanceInferenceByteOffset(scalarIndex), half(values[lane]));
            }
        }
    }

    if (chunkIndex < kNeuralAppearanceLatentChunkCount)
    {
        uint texelIndex = chunkIndex / kNeuralAppearanceLatentTextureCount;
        uint textureIndex = chunkIndex % kNeuralAppearanceLatentTextureCount;
        uint2 texel = uint2(texelIndex % kNeuralAppearanceLatentWidth,
                            texelIndex / kNeuralAppearanceLatentWidth);
        NeuralAppearanceLatent latent = neuralAppearanceFixtureLatentTexel(texel);
        float4 values = control.y == 1u ? latent.chunks[textureIndex] : gTrainingLatent[chunkIndex];
        gTrainingLatent[chunkIndex] = values;
        if (control.y == 1u)
        {
            gLatentMoments[chunkIndex] = float4(0.0f);
            gLatentMoments[kNeuralAppearanceLatentChunkCount + chunkIndex] = float4(0.0f);
        }
        uint byteOffset = chunkIndex * 8u;
        gInferenceLatent.Store<half>(byteOffset + 0u, half(values.x));
        gInferenceLatent.Store<half>(byteOffset + 2u, half(values.y));
        gInferenceLatent.Store<half>(byteOffset + 4u, half(values.z));
        gInferenceLatent.Store<half>(byteOffset + 6u, half(values.w));
    }

    if (chunkIndex == 0u && control.y == 1u)
    {
        gTrainingStatus[0] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gTrainingStatus[1] = float4(0.0f);
    }
}
