module presentConvert;
import common;

struct PresentConvertPushConstants
{
    uint width;
    uint height;
    uint swizzleBgr;
    uint outputEncoding;
    uint flipY;
    float uiOpacity;
};

Texture2D<float4> gSourceColor;
Texture2D<float4> gUiColor;
RWTexture2D<float4> gConvertedColor;

[[vk::push_constant]]
ConstantBuffer<PresentConvertPushConstants> gPresentConvert;

static const uint kOutputEncodingSrgb = 1u;
static const uint kOutputEncodingHdr10Pq = 2u;
static const uint kOutputEncodingScRgb = 3u;
static const float kHdr10ReferenceWhiteNits = 203.0f;
static const float kHdr10MaxOutputNits = 1000.0f;

float3 linearToSrgb(float3 linearColor)
{
    // Final screen conversion: scene-linear RGB is clamped before the sRGB
    // transfer curve. No exposure or tone mapping is applied in this pass.
    float3 color = saturate(linearColor);
    float3 low = color * 12.92f;
    float3 high = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    return lerp(low, high, step(float3(0.0031308f), color));
}

float3 linearSrgbToBt2020(float3 linearColor)
{
    return float3(
        0.6274040f * linearColor.r + 0.3292820f * linearColor.g + 0.0433136f * linearColor.b,
        0.0690970f * linearColor.r + 0.9195400f * linearColor.g + 0.0113612f * linearColor.b,
        0.0163916f * linearColor.r + 0.0880132f * linearColor.g + 0.8955950f * linearColor.b);
}

float3 nitsToSt2084Pq(float3 nits)
{
    static const float m1 = 2610.0f / 16384.0f;
    static const float m2 = 2523.0f / 32.0f;
    static const float c1 = 3424.0f / 4096.0f;
    static const float c2 = 2413.0f / 128.0f;
    static const float c3 = 2392.0f / 128.0f;

    float3 normalizedNits = saturate(nits / 10000.0f);
    float3 y = pow(normalizedNits, m1);
    return pow((c1 + c2 * y) / (1.0f + c3 * y), m2);
}

float3 linearToHdr10Pq(float3 linearColor)
{
    float3 bt2020 = max(linearSrgbToBt2020(max(linearColor, 0.0f)), 0.0f);
    float3 nits = min(bt2020 * kHdr10ReferenceWhiteNits, kHdr10MaxOutputNits);
    return nitsToSt2084Pq(nits);
}

float3 encodePresentOutput(float3 linearColor, uint outputEncoding)
{
    if (outputEncoding == kOutputEncodingSrgb)
    {
        return linearToSrgb(linearColor);
    }
    if (outputEncoding == kOutputEncodingHdr10Pq)
    {
        return linearToHdr10Pq(linearColor);
    }
    if (outputEncoding == kOutputEncodingScRgb)
    {
        return max(linearColor, 0.0f);
    }
    return linearColor;
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
    float3 linearOutputRgb = sourceColor.rgb;

    float effectiveUiOpacity = saturate(gPresentConvert.uiOpacity);
    if (effectiveUiOpacity > 0.0f)
    {
        // Keep UI in screen-space orientation so mouse coordinates stay consistent with rendered widgets.
        float4 uiColor = gUiColor.Load(int3(pixelCoord.x, pixelCoord.y, 0));
        float effectiveUiAlpha = saturate(uiColor.a * effectiveUiOpacity);
        if (effectiveUiAlpha > 0.0f && uiColor.a > 0.0f)
        {
            float3 uiStraightRgb = saturate(uiColor.rgb / max(uiColor.a, 1.0e-6f));
            linearOutputRgb = linearOutputRgb * (1.0f - effectiveUiAlpha) + uiStraightRgb * effectiveUiAlpha;
        }
    }

    float3 convertedRgb = encodePresentOutput(linearOutputRgb, gPresentConvert.outputEncoding);

    bool swizzleBgr = gPresentConvert.swizzleBgr != 0u;
    float3 outputRgb = swizzleBgr
                           ? float3(convertedRgb.b, convertedRgb.g, convertedRgb.r)
                           : convertedRgb;
    gConvertedColor[pixelCoord] = float4(outputRgb, 1.0f);
}
