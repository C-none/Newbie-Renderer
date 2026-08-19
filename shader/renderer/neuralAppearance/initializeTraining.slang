module initializeTraining;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

// The residual frame formulation makes a small random frame layer start at the
// geometry frame, so no hand-tuned identity weights are required any more.
static const float kNeuralAppearanceFrameInitialScale = 0.02f;
static const float kNeuralAppearanceOutputInitialBias = -1.0f;

struct NeuralAppearanceInitializePushConstants
{
    uint trainingSeed;
};

[[vk::binding(0, 1)]]
StructuredBuffer<uint4> gTrainingControl;

[[vk::binding(0, 2)]]
RWStructuredBuffer<float4> gModelParameters;

[[vk::binding(1, 2)]]
RWStructuredBuffer<float4> gModelMoments;

[[vk::binding(2, 2)]]
RWStructuredBuffer<float4> gTrainingStatus;

[[vk::binding(3, 2)]]
RWByteAddressBuffer gInferenceParameters;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceInitializePushConstants> gInitialize;

float neuralAppearanceInitialRandom(uint scalarIndex, float scale)
{
    float unitRandom = neuralAppearanceTrainingRandom(
        neuralAppearanceTrainingHash(gInitialize.trainingSeed + 0x6a09e667u), scalarIndex);
    return (2.0f * unitRandom - 1.0f) * scale;
}

float neuralAppearanceInitialParameter(uint scalarIndex)
{
    for (uint layerIndex = 0u; layerIndex < kNeuralAppearanceAffineLayerCount; ++layerIndex)
    {
        NeuralAppearanceAffineLayer layer = neuralAppearanceAffineLayer(layerIndex);
        uint weightLocal = scalarIndex - layer.weightScalarOffset;
        if (scalarIndex >= layer.weightScalarOffset && weightLocal < layer.weightScalarCount())
        {
            float scale = layerIndex == kNeuralAppearanceFrameLayer
                              ? kNeuralAppearanceFrameInitialScale
                              : sqrt(6.0f / float(layer.inputCount + layer.outputCount));
            return neuralAppearanceInitialRandom(scalarIndex, scale);
        }
        uint biasLocal = scalarIndex - layer.biasScalarOffset;
        if (layer.hasBias && scalarIndex >= layer.biasScalarOffset && biasLocal < layer.outputCount)
        {
            // Both decoded lobes start near a third of their useful range.
            return layerIndex == kNeuralAppearanceOutputLayer ? kNeuralAppearanceOutputInitialBias : 0.0f;
        }
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
    // The fixed V3 model blob contains alignment padding. It is part of the
    // portable ABI and must remain zero for both fresh and restored mirrors.
    if (chunkIndex < (kNeuralAppearanceInferenceParameterBytes / 4u))
    {
        uint byteOffset = chunkIndex * 4u;
        [ForceUnroll]
        for (uint scalar = 0u; scalar < 2u; ++scalar)
        {
            uint scalarByteOffset = byteOffset + scalar * kNeuralAppearanceInferenceScalarBytes;
            if (neuralAppearanceInferenceByteIsPadding(scalarByteOffset))
            {
                gInferenceParameters.Store<half>(scalarByteOffset, half(0.0h));
            }
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
                gInferenceParameters.Store<half>(
                    neuralAppearanceInferenceByteOffset(scalarIndex), half(values[lane]));
            }
        }
    }

    if (chunkIndex == 0u && control.y == 1u)
    {
        gTrainingStatus[0] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        gTrainingStatus[1] = float4(0.0f);
    }
}
