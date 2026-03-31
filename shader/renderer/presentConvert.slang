module presentConvert;
struct PresentConvertPushConstants
{
    uint width;
    uint height;
    uint swizzleBgr;
    uint outputSrgb;
    uint flipY;
    float uiOpacity;
};

Texture2D<float4> gSourceColor;
Texture2D<float4> gUiColor;
RWTexture2D<float4> gConvertedColor;

[[vk::push_constant]]
ConstantBuffer<PresentConvertPushConstants> gPresentConvert;

float3 linearToSrgb(float3 linearColor)
{
    float3 color = saturate(linearColor);
    float3 low = color * 12.92f;
    float3 high = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    return lerp(low, high, step(float3(0.0031308f), color));
}

[shader("compute")]
[numthreads(16, 16, 1)]
void presentConvertMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gPresentConvert.width || dispatchThreadID.y >= gPresentConvert.height)
    {
        return;
    }

    int2 pixelCoord = int2(dispatchThreadID.xy);
    int sourceY = gPresentConvert.flipY != 0u
                      ? int(gPresentConvert.height - 1u - dispatchThreadID.y)
                      : pixelCoord.y;
    float4 sourceColor = gSourceColor.Load(int3(pixelCoord.x, sourceY, 0));
    // Keep UI in screen-space orientation so mouse coordinates stay consistent with rendered widgets.
    float4 uiColor = gUiColor.Load(int3(pixelCoord.x, pixelCoord.y, 0));
    float3 convertedRgb = sourceColor.rgb;
    if (gPresentConvert.outputSrgb != 0u)
    {
        convertedRgb = linearToSrgb(convertedRgb);
    }

    float effectiveUiAlpha = saturate(uiColor.a * gPresentConvert.uiOpacity);
    float3 uiCompositeRgb = float3(0.0f, 0.0f, 0.0f);
    if (effectiveUiAlpha > 0.0f && uiColor.a > 0.0f)
    {
        float3 uiStraightRgb = saturate(uiColor.rgb / max(uiColor.a, 1.0e-6f));
        float3 uiOutputRgb = gPresentConvert.outputSrgb != 0u
                                 ? linearToSrgb(uiStraightRgb)
                                 : uiStraightRgb;
        uiCompositeRgb = uiOutputRgb * effectiveUiAlpha;
        convertedRgb = convertedRgb * (1.0f - effectiveUiAlpha) + uiCompositeRgb;
    }

    bool swizzleBgr = gPresentConvert.swizzleBgr != 0u;
    float3 outputRgb = swizzleBgr
                           ? float3(convertedRgb.b, convertedRgb.g, convertedRgb.r)
                           : convertedRgb;
    gConvertedColor[pixelCoord] = float4(outputRgb, 1.0f);
}
