module accumulate;
import common;

struct AccumulatePushConstants
{
    uint width;
    uint height;
    uint resetHistory;
    uint historySampleCount;
    uint maxHistorySampleCount;
};

[[vk::binding(0, 1)]]
Texture2D<float4> gCurrentColor;

[[vk::binding(1, 1)]]
Texture2D<float4> gHistoryColor;

[[vk::binding(0, 2)]]
RWTexture2D<float4> gAccumulatedColor;

[[vk::push_constant]]
ConstantBuffer<AccumulatePushConstants> gAccumulate;

[shader("compute")]
[numthreads(16, 16, 1)]
void accumulateMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gAccumulate.width || dispatchThreadID.y >= gAccumulate.height)
    {
        return;
    }

    int2 pixelCoord = int2(dispatchThreadID.xy);
    float4 currentColor = gCurrentColor.Load(int3(pixelCoord, 0));
    if (gAccumulate.resetHistory != 0u)
    {
        gAccumulatedColor[pixelCoord] = currentColor;
        return;
    }

    float4 historyColor = gHistoryColor.Load(int3(pixelCoord, 0));
    float exactWeight = 1.0f / float(gAccumulate.historySampleCount + 1u);
    float cappedWeight = 1.0f / float(max(gAccumulate.maxHistorySampleCount, 1u));
    float currentWeight = max(exactWeight, cappedWeight);
    gAccumulatedColor[pixelCoord] = lerp(historyColor, currentColor, currentWeight);
}
