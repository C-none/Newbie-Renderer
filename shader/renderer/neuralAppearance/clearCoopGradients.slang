module clearCoopGradients;
import common;

struct NeuralAppearanceClearCoopGradientsPushConstants
{
    uint optimalWeightGradientBytes;
    uint rowMajorWeightGradientBytes;
    uint biasGradientBytes;
    uint padding;
};

[[vk::binding(0, 2)]]
RWByteAddressBuffer gOptimalWeightGradients;

[[vk::binding(1, 2)]]
RWByteAddressBuffer gRowMajorWeightGradients;

[[vk::binding(2, 2)]]
RWByteAddressBuffer gBiasGradients;

[[vk::push_constant]]
ConstantBuffer<NeuralAppearanceClearCoopGradientsPushConstants> gClear;

[shader("compute")]
[numthreads(64, 1, 1)]
void clearCoopGradientsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint byteOffset = dispatchThreadID.x * 4u;
    if (byteOffset < gClear.optimalWeightGradientBytes)
    {
        gOptimalWeightGradients.Store(byteOffset, 0u);
    }
    if (byteOffset < gClear.rowMajorWeightGradientBytes)
    {
        gRowMajorWeightGradients.Store(byteOffset, 0u);
    }
    if (byteOffset < gClear.biasGradientBytes)
    {
        gBiasGradients.Store(byteOffset, 0u);
    }
}
