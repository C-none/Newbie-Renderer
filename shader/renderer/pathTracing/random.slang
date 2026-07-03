module random;
import common;

public uint hashPathTracingSample(uint value)
{
    value ^= value >> 16u;
    value *= 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return value;
}

public uint mixPathTracingSample(uint state, uint value)
{
    return hashPathTracingSample(state ^ (value + 0x9E3779B9u + (state << 6u) + (state >> 2u)));
}

public uint hashPathTracingState(uint4 state, uint salt)
{
    uint mixed = mixPathTracingSample(0xB5297A4Du, salt);
    mixed = mixPathTracingSample(mixed, state.x);
    mixed = mixPathTracingSample(mixed, state.y);
    mixed = mixPathTracingSample(mixed, state.z);
    mixed = mixPathTracingSample(mixed, state.w);
    return mixed;
}

public uint4 makePathTracingSeed(uint2 pixel, uint2 dimensions, uint2 sampleFrameOrdinal)
{
    uint extentState = mixPathTracingSample(0x68E31DA4u, dimensions.x);
    extentState = mixPathTracingSample(extentState, dimensions.y);
    return uint4(
        pixel.x,
        pixel.y,
        sampleFrameOrdinal.x,
        sampleFrameOrdinal.y ^ extentState);
}

public float pathTracingRandom01FromHash(uint value)
{
    return float(hashPathTracingSample(value) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

public float nextPathTracingRandom01(inout uint4 state)
{
    state.w += 0x9E3779B9u;
    uint value = hashPathTracingState(state, 0xA511E9B3u);
    return float(value & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

public float2 nextPathTracingRandom02(inout uint4 state)
{
    return float2(nextPathTracingRandom01(state), nextPathTracingRandom01(state));
}

public float3 nextPathTracingRandom03(inout uint4 state)
{
    return float3(
        nextPathTracingRandom01(state),
        nextPathTracingRandom01(state),
        nextPathTracingRandom01(state));
}
