implementing common;

// The owning path-tracing root variant provides this link-time constant. It is
// declared in a common include so linked CHS specializations see the same
// compile-time policy without adding another MaterialCHS generic dimension.
public extern static const bool kEnableFilterAfterShading;

// One material CHS can read eleven texture semantics. Three 4D packets reserve
// twelve scalar dimensions so optional layer branches never shift the mapping:
//   packet 0: baseColor, metallicRoughness, emissive, base normal
//   packet 1: anisotropy, clearcoat, clearcoat roughness, clearcoat normal
//   packet 2: sheen color, sheen roughness, transmission, padding
public bool selectStochasticFilterUpperTap(
    float upperWeight,
    inout float uniformValue)
{
    if (upperWeight <= 0.0f)
    {
        return false;
    }
    if (upperWeight >= 1.0f)
    {
        return true;
    }

    bool selectUpper = uniformValue < upperWeight;
    uniformValue = selectUpper
        ? uniformValue / upperWeight
        : (uniformValue - upperWeight) / (1.0f - upperWeight);
    return selectUpper;
}

// Sample one of the four bilinear reconstruction taps with probability equal
// to its bilinear weight. The remapped scalar remains uniform after the X
// decision and is reused for Y, preserving the one-dimensional stratification
// of the semantic slot.
public float2 stochasticBilinearTexelCenterUv(
    float2 uv,
    uint2 dimensions,
    float uniformValue)
{
    float2 safeDimensions = float2(max(dimensions, uint2(1u)));
    float2 texelPosition = uv * safeDimensions - 0.5f;
    float2 lowerTexel = floor(texelPosition);
    float2 upperWeight = texelPosition - lowerTexel;

    bool selectUpperX = selectStochasticFilterUpperTap(
        upperWeight.x,
        uniformValue);
    bool selectUpperY = selectStochasticFilterUpperTap(
        upperWeight.y,
        uniformValue);
    float2 selectedTexel =
        lowerTexel +
        float2(selectUpperX ? 1.0f : 0.0f, selectUpperY ? 1.0f : 0.0f);
    return (selectedTexel + 0.5f) / safeDimensions;
}
