module clearCoopGradients;
import common;

// Only the accumulated gradient buffers need clearing. The converted row-major
// destination is fully overwritten by the conversion pass every frame, so
// clearing it would be redundant work and would add a cross-frame
// write-after-write hazard on a buffer the optimizer only reads.
struct NeuralAppearanceClearCoopGradientsPushConstants
{
    uint optimalWeightGradientBytes;
    uint biasGradientBytes;
};

[[vk::binding(0, 2)]]
RWByteAddressBuffer gOptimalWeightGradients;

[[vk::binding(1, 2)]]
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
    if (byteOffset < gClear.biasGradientBytes)
    {
        gBiasGradients.Store(byteOffset, 0u);
    }
}
