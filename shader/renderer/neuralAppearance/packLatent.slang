module packLatent;
import common;
import include.neuralAppearance.layout;

[[vk::binding(0, 1)]]
StructuredBuffer<float4> gTrainingLatent;

[[vk::binding(0, 2)]]
[format("rgba16f")]
RWTexture2D<float4> gLatentTexture0;

[[vk::binding(1, 2)]]
[format("rgba16f")]
RWTexture2D<float4> gLatentTexture1;

[shader("compute")]
[numthreads(8, 8, 1)]
void packLatentMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= kNeuralAppearanceLatentWidth ||
        dispatchThreadID.y >= kNeuralAppearanceLatentHeight)
    {
        return;
    }

    uint texelIndex = dispatchThreadID.y * kNeuralAppearanceLatentWidth + dispatchThreadID.x;
    int2 coordinate = int2(dispatchThreadID.xy);
    gLatentTexture0[coordinate] = gTrainingLatent[texelIndex * 2u + 0u];
    gLatentTexture1[coordinate] = gTrainingLatent[texelIndex * 2u + 1u];
}
