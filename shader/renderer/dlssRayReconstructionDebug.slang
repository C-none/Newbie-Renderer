module dlssRayReconstructionDebug;
import common;

struct DlssMotionVectorDebugPushConstants
{
    uint2 outputBase;
    uint2 outputSize;
    uint2 motionVectorBase;
    uint2 motionVectorSize;
    float2 motionVectorScale;
};

[[vk::binding(0, 1)]]
Texture2D<float2> gMotionVectors;

[[vk::binding(0, 2)]]
RWTexture2D<float4> gMotionVectorVisualization;

[[vk::push_constant]]
ConstantBuffer<DlssMotionVectorDebugPushConstants> gMotionVectorDebug;

[shader("compute")]
[numthreads(8, 8, 1)]
void visualizeMotionVectorsMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    uint outputWidth;
    uint outputHeight;
    gMotionVectorVisualization.GetDimensions(outputWidth, outputHeight);
    uint2 pixel = dispatchThreadID.xy;
    if (pixel.x >= outputWidth || pixel.y >= outputHeight)
    {
        return;
    }

    int2 localPixel = int2(pixel) - int2(gMotionVectorDebug.outputBase);
    if (any(localPixel < int2(0)) || any(uint2(localPixel) >= gMotionVectorDebug.outputSize))
    {
        gMotionVectorVisualization[int2(pixel)] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    float2 outputSize = max(float2(gMotionVectorDebug.outputSize), float2(1.0f));
    float2 motionVectorSize = max(float2(gMotionVectorDebug.motionVectorSize), float2(1.0f));
    float2 motionUv = (float2(localPixel) + 0.5f) / outputSize;
    uint2 motionLocalPixel = min(uint2(motionUv * motionVectorSize), gMotionVectorDebug.motionVectorSize - uint2(1u));
    int2 motionPixel = int2(gMotionVectorDebug.motionVectorBase + motionLocalPixel);
    float2 motionPixels = gMotionVectors.Load(int3(motionPixel, 0)) * gMotionVectorDebug.motionVectorScale;

    if (any(motionPixels != motionPixels) || any(abs(motionPixels) > 65504.0f))
    {
        gMotionVectorVisualization[int2(pixel)] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        return;
    }

    // Match Messiah's RR debug view after accounting for this renderer's pixel-space MV contract.
    float2 amplifiedMotion = sign(motionPixels) * saturate(log2(float2(1.0f) + abs(motionPixels) * 64.0f) / 8.0f);
    gMotionVectorVisualization[int2(pixel)] = float4(saturate(amplifiedMotion * 0.5f + 0.5f), 0.0f, 1.0f);
}
