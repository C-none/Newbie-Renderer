module presentConvert;
import common;

enum PresentOutputEncoding : uint
{
    linear = 0u,
    srgb = 1u,
    hdr10Pq = 2u,
    scRgb = 3u,
}

enum PresentToneMappingMethod : uint
{
    none = 0u,
    reinhard = 1u,
    acesFilmic = 2u,
    bt2390 = 3u,
}

struct PresentConvertPushConstants
{
    uint width;
    uint height;
    uint swizzleBgr;
    PresentOutputEncoding outputEncoding;
    PresentToneMappingMethod toneMapping;
    float uiOpacity;
};

[[vk::binding(0, 1)]]
Texture2D<float4> gSourceColor;

[[vk::binding(1, 1)]]
Texture2D<float4> gUiColor;

[[vk::binding(0, 2)]]
RWTexture2D<float4> gConvertedColor;

[[vk::push_constant]]
ConstantBuffer<PresentConvertPushConstants> gPresentConvert;

static const float kHdr10ReferenceWhiteNits = 203.0f;
static const float kHdr10MaxOutputNits = 1000.0f;
static const float kScRgbReferenceWhiteNits = 80.0f;
static const float kScRgbMaxOutputNits = 1000.0f;
static const float kSdrReferenceWhiteNits = 100.0f;
static const float kSdrMaxOutputNits = 100.0f;
static const float kPqM1 = 2610.0f / 16384.0f;
static const float kPqM2 = 2523.0f / 32.0f;
static const float kPqC1 = 3424.0f / 4096.0f;
static const float kPqC2 = 2413.0f / 128.0f;
static const float kPqC3 = 2392.0f / 128.0f;

float3 linearToSrgb(float3 linearColor)
{
    // Precise sRGB piecewise OETF. Values are clamped to the display range before encoding.
    float3 color = saturate(linearColor);
    float3 low = color * 12.92f;
    float3 high = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    return lerp(low, high, step(float3(0.0031308f), color));
}

float3 linearSrgbToBt2020(float3 linearColor)
{
    return float3(0.6274040f * linearColor.r + 0.3292820f * linearColor.g + 0.0433136f * linearColor.b,
                  0.0690970f * linearColor.r + 0.9195400f * linearColor.g + 0.0113612f * linearColor.b,
                  0.0163916f * linearColor.r + 0.0880132f * linearColor.g + 0.8955950f * linearColor.b);
}

// ST 2084 / PQ OETF on a signal normalized so 1.0 == 10000 nits.
float pqSignalFromNormalized(float y)
{
    float yc = pow(saturate(y), kPqM1);
    return pow((kPqC1 + kPqC2 * yc) / (1.0f + kPqC3 * yc), kPqM2);
}

// Inverse ST 2084 / PQ; returns a normalized signal where 1.0 == 10000 nits.
float normalizedFromPqSignal(float e)
{
    float ep = pow(saturate(e), 1.0f / kPqM2);
    float num = max(ep - kPqC1, 0.0f);
    float den = max(kPqC2 - kPqC3 * ep, 1.0e-6f);
    return pow(num / den, 1.0f / kPqM1);
}

float3 nitsToSt2084Pq(float3 nits)
{
    return float3(pqSignalFromNormalized(nits.r / 10000.0f), pqSignalFromNormalized(nits.g / 10000.0f),
                  pqSignalFromNormalized(nits.b / 10000.0f));
}

float3 linearToHdr10Pq(float3 linearColor)
{
    float3 bt2020 = max(linearSrgbToBt2020(max(linearColor, 0.0f)), 0.0f);
    float3 nits = min(bt2020 * kHdr10ReferenceWhiteNits, kHdr10MaxOutputNits);
    return nitsToSt2084Pq(nits);
}

// Reference-white and display-peak luminance the tone mapper targets for each output encoding.
// Tone-mapped values are expressed in units where 1.0 == reference white, matching the encoders.
void presentReferenceNits(PresentOutputEncoding outputEncoding, out float whiteNits, out float maxNits)
{
    if (outputEncoding == PresentOutputEncoding.hdr10Pq)
    {
        whiteNits = kHdr10ReferenceWhiteNits;
        maxNits = kHdr10MaxOutputNits;
    }
    else if (outputEncoding == PresentOutputEncoding.scRgb)
    {
        whiteNits = kScRgbReferenceWhiteNits;
        maxNits = kScRgbMaxOutputNits;
    }
    else
    {
        whiteNits = kSdrReferenceWhiteNits;
        maxNits = kSdrMaxOutputNits;
    }
}

// Reinhard operator; compresses scene-linear into the [0,1) reference-white range.
float3 toneMapReinhard(float3 x)
{
    return x / (1.0f + x);
}

// Narkowicz 2015 ACES filmic approximation; compresses scene-linear into the [0,1] reference-white range.
float3 toneMapAcesFilmic(float3 x)
{
    static const float a = 2.51f;
    static const float b = 0.03f;
    static const float c = 2.43f;
    static const float d = 0.59f;
    static const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

// ITU-R BT.2390 EETF. Rolls a scene-referred luminance (mastered to 10000 nits) down to the
// target display peak with a Hermite knee, returning display luminance in nits.
float bt2390EetfNits(float lumNits, float maxDisplayNits)
{
    float e1 = pqSignalFromNormalized(lumNits / 10000.0f);
    float maxLum = pqSignalFromNormalized(maxDisplayNits / 10000.0f);
    float ks = 1.5f * maxLum - 0.5f;

    float e2 = e1;
    if (e1 > ks)
    {
        float t = (e1 - ks) / max(1.0f - ks, 1.0e-6f);
        float t2 = t * t;
        float t3 = t2 * t;
        e2 = (2.0f * t3 - 3.0f * t2 + 1.0f) * ks + (t3 - 2.0f * t2 + t) * (1.0f - ks) +
             (-2.0f * t3 + 3.0f * t2) * maxLum;
    }
    return normalizedFromPqSignal(e2) * 10000.0f;
}

// Hue-preserving BT.2390: apply the EETF to the brightest channel, then scale RGB by the same ratio.
float3 toneMapBt2390(float3 x, PresentOutputEncoding outputEncoding)
{
    float whiteNits;
    float maxNits;
    presentReferenceNits(outputEncoding, whiteNits, maxNits);

    float peak = max(x.r, max(x.g, x.b));
    if (peak <= 0.0f)
    {
        return x;
    }

    float mappedNits = bt2390EetfNits(peak * whiteNits, maxNits);
    float mappedWhiteRelative = mappedNits / whiteNits;
    return x * (mappedWhiteRelative / peak);
}

float3 applyToneMapping(float3 sceneLinear, PresentToneMappingMethod method, PresentOutputEncoding outputEncoding)
{
    float3 x = max(sceneLinear, 0.0f);
    if (method == PresentToneMappingMethod.reinhard)
    {
        return toneMapReinhard(x);
    }
    if (method == PresentToneMappingMethod.acesFilmic)
    {
        return toneMapAcesFilmic(x);
    }
    if (method == PresentToneMappingMethod.bt2390)
    {
        return toneMapBt2390(x, outputEncoding);
    }
    return x;
}

float3 encodePresentOutput(float3 linearColor, PresentOutputEncoding outputEncoding)
{
    if (outputEncoding == PresentOutputEncoding.srgb)
    {
        return linearToSrgb(linearColor);
    }
    if (outputEncoding == PresentOutputEncoding.hdr10Pq)
    {
        return linearToHdr10Pq(linearColor);
    }
    if (outputEncoding == PresentOutputEncoding.scRgb)
    {
        return max(linearColor, 0.0f);
    }
    return linearColor;
}

[shader("compute")]
[numthreads(16, 16, 1)]
void presentConvertMain(uint3 dispatchThreadID: SV_DispatchThreadID)
{
    if (dispatchThreadID.x >= gPresentConvert.width || dispatchThreadID.y >= gPresentConvert.height)
    {
        return;
    }

    int2 pixelCoord = int2(dispatchThreadID.xy);
    float4 sourceColor = gSourceColor.Load(int3(pixelCoord.x, pixelCoord.y, 0));

    // Tone map the scene before compositing UI so the UI stays display-referred SDR reference white.
    float3 displayLinear =
        applyToneMapping(sourceColor.rgb, gPresentConvert.toneMapping, gPresentConvert.outputEncoding);

    float effectiveUiOpacity = saturate(gPresentConvert.uiOpacity);
    if (effectiveUiOpacity > 0.0f)
    {
        // Keep UI in screen-space orientation so mouse coordinates stay consistent with rendered widgets.
        float4 uiColor = gUiColor.Load(int3(pixelCoord.x, pixelCoord.y, 0));
        float effectiveUiAlpha = saturate(uiColor.a * effectiveUiOpacity);
        if (effectiveUiAlpha > 0.0f && uiColor.a > 0.0f)
        {
            float3 uiStraightRgb = saturate(uiColor.rgb / max(uiColor.a, 1.0e-6f));
            displayLinear = displayLinear * (1.0f - effectiveUiAlpha) + uiStraightRgb * effectiveUiAlpha;
        }
    }

    float3 convertedRgb = encodePresentOutput(displayLinear, gPresentConvert.outputEncoding);

    bool swizzleBgr = gPresentConvert.swizzleBgr != 0u;
    float3 outputRgb = swizzleBgr ? float3(convertedRgb.b, convertedRgb.g, convertedRgb.r) : convertedRgb;
    gConvertedColor[pixelCoord] = float4(outputRgb, 1.0f);
}
