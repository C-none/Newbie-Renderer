implementing common;

// ============================================================================
//  Low-discrepancy random sequence (Owen-scrambled Sobol').
//
//  Ported from the Unreal Engine path tracer (PathTracingRandomSequence.ush,
//  RANDSEQ_OWENSOBOL) as a blue-noise error-diffusion sampler. One sample per
//  pixel per frame (no in-frame sample count / MaxSamples).
//
//    * (dispatch.xy, frame) -> Sobol' sample index:
//        - dispatch.xy through a Hilbert space-filling curve, so neighbouring
//          pixels are adjacent in the sequence -> a single frame is spatial
//          blue noise (great for denoising);
//        - the frame index is added above the tile bits as a large stride
//          (frame << 2*tileBits, i.e. frame * 65536 for tileBits == 8) so
//          successive frames step far along the sequence and fastOwenScrambling
//          shatters that stride into decorrelated (white) cross-frame samples.
//    * The scrambling seed is shared across the whole frame (starts at 0), which
//      preserves the spatial blue-noise structure; it is evolved per draw (the
//      increment counter) via an LCG so each dimension/bounce picks a fresh Owen
//      scramble -> cross-draw values are decorrelated (white).
//
//  The whole sampler is pure ALU: no lookup textures or extra GPU resources.
// ============================================================================

// 2^-24. Only the top 24 bits of a 32-bit word are used when converting to a
// float in [0,1): float represents integers up to 2^24 exactly, so the cast is
// lossless and the good low-discrepancy high bits are preserved.
public static const float kInvTwoPow24 = 5.9604644775390625e-08f;

// ---------------------------------------------------------------------------
//  Legacy white-noise hash (kept: consumed by makeAliasLightSample).
// ---------------------------------------------------------------------------
public uint hashSample(uint value)
{
    value ^= value >> 16u;
    value *= 2246822519u;
    value ^= value >> 13u;
    value *= 3266489917u;
    value ^= value >> 16u;
    return value;
}

public float random01FromHash(uint value)
{
    return float(hashSample(value) & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// ---------------------------------------------------------------------------
//  Integer hashes (bit mixers) used for seeding / decorrelation.
// ---------------------------------------------------------------------------

// Mixes bits at the theoretical bias limit. https://nullprogram.com/blog/2018/07/31/
public uint perfectIntegerHash(uint x)
{
    x ^= x >> 17u;
    x *= 0xed5ad4bbu;
    x ^= x >> 11u;
    x *= 0xac4c1b51u;
    x ^= x >> 15u;
    x *= 0x31848babu;
    x ^= x >> 14u;
    return x;
}

// High quality integer hash (skeeto/hash-prospector).
public uint strongIntegerHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x21f0aaadu;
    x ^= x >> 15u;
    x *= 0xf35a2d97u;
    x ^= x >> 15u;
    return x;
}

// ---------------------------------------------------------------------------
//  Fast Owen scrambling. A cheap approximation of per-bit Owen scrambling that
//  is nearly as good in practice. Randomises a Sobol' index while preserving
//  the low-discrepancy structure.
// ---------------------------------------------------------------------------
public uint fastOwenScramblingCore(uint index, uint seed)
{
    index ^= index * 0xe0705d72u;
    index += seed;
    seed ^= seed >> 16u;
    index *= seed | 1u;
    return index;
}

// Reverse the result so scrambling runs left-to-right, emulating Owen scrambling.
public uint fastOwenScrambling(uint index, uint seed)
{
    return reversebits(fastOwenScramblingCore(index, seed));
}

// ---------------------------------------------------------------------------
//  Sobol' seed evolution. LCG multiplier/increment from Steele & Vigna,
//  "Computationally Easy, Spectrally Good Multipliers ...", 2022.
//  Advancing the seed once per draw decorrelates successive dimensions and is
//  what we drive with the "increment count" here.
// ---------------------------------------------------------------------------
public uint evolveSobolSeed(inout uint seed)
{
    seed = seed * 0x915f77f5u + 0x93d765ddu;
    return seed;
}

// ---------------------------------------------------------------------------
//  Hilbert space-filling curve inverse: map a 2D grid point (X,Y) to its index
//  along the Hilbert curve over a 2^numBits x 2^numBits tile. Because it only
//  loops over the bits it uses, higher coordinate bits simply tile the pattern.
//  https://github.com/hcs0/Hackers-Delight/blob/master/hilbert/hil_s_from_xy.c.txt
// ---------------------------------------------------------------------------
public uint hilbertSFCInverse(uint x, uint y, int numBits)
{
    uint hilbertIndex = 0u;
    uint hilbertState = 0u;
    for (int i = numBits - 1; i >= 0; --i)
    {
        uint xi = (x >> uint(i)) & 1u;
        uint yi = (y >> uint(i)) & 1u;
        uint row = 8u * hilbertState + 4u * xi + 2u * yi;
        hilbertIndex = hilbertIndex * 4u + ((0x361E9CB4u >> row) & 3u);
        hilbertState = (0x8FE65831u >> row) & 3u;
    }
    return hilbertIndex;
}

// ---------------------------------------------------------------------------
//  Owen-scrambled Sobol' 4D core. Returns four decorrelated 32-bit components
//  forming a well-stratified point; advances the seed as it goes.
//
//  Dimensions 1/2 are derived from the scrambled index with pure bit ops:
//    * dim 2 via SZ-sequence Eq.42 (Ahmed et al., SIGGRAPH Asia 2025)
//    * y/w derived from x/z (Ahmed, "An Implementation Algorithm of 2D Sobol
//      Sequence ...", EGSR 2024, listing 19)
//  so no direction-number matrix loop is required.
// ---------------------------------------------------------------------------
public uint4 sobolSamplerCore(uint sampleIndex, inout uint seed)
{
    // Scramble the index to decorrelate this 4-tuple from the others.
    uint sobolIndex = fastOwenScrambling(sampleIndex, evolveSobolSeed(seed));

    uint4 result = uint4(sobolIndex, sobolIndex, 0u, 0u);

    // Dimension 2 (result.z) via Eq.42.
    result.z = ((sobolIndex >> 1u) & 0x50550550u) ^
               ((sobolIndex      ) & 0xaf5af5afu) ^
               ((sobolIndex << 1u) & 0xa0aa0aa0u);
    result.z ^=  result.z                << 16u;
    result.z ^= (result.z & 0x00FF00FFu) <<  8u;
    result.z ^= (result.z & 0x0F0F0F0Fu) <<  4u;
    result.z = ((result.z >> 1u) & 0x50550550u) ^
               ((result.z      ) & 0x5fa5fa5fu) ^
               ((result.z << 1u) & 0xa4ea4ea4u) ^
               ((result.z << 2u) & 0xc84c84c8u) ^
               ((result.z << 3u) & 0x08808808u);
    result.zw = reversebits(result.z);

    // result.y / result.w derived from result.x / result.z (bit-reversed form).
    result.yw ^=  result.yw                >> 16u;
    result.yw ^= (result.yw & 0xFF00FF00u) >>  8u;
    result.yw ^= (result.yw & 0xF0F0F0F0u) >>  4u;
    result.yw ^= (result.yw & 0xCCCCCCCCu) >>  2u;
    result.yw ^= (result.yw & 0xAAAAAAAAu) >>  1u;

    // Scramble each output to avoid structured artifacts.
    result.x = fastOwenScrambling(result.x, evolveSobolSeed(seed));
    result.y = fastOwenScrambling(result.y, evolveSobolSeed(seed));
    result.z = fastOwenScrambling(result.z, evolveSobolSeed(seed));
    result.w = fastOwenScrambling(result.w, evolveSobolSeed(seed));
    return result;
}

// ---------------------------------------------------------------------------
//  Sequence abstraction. Draw 1..4 dimensional samples in the unit square,
//  either as raw bits or as floats in [0,1). Values from a single get<N>() call
//  are well stratified; separate calls are statistically independent.
// ---------------------------------------------------------------------------
public interface IRandomSequence
{
    // Draw an N-dimensional sample in [0,1)^N and advance the sequence.
    [mutating]
    vector<float, N> get<let N : int>();
}

public struct RandomSequence : IRandomSequence
{
    public uint sampleIndex = 0u; // low-discrepancy index (spatial), pre-bit-reversed
    public uint sampleSeed = 0u;  // scrambling seed; shared per frame, evolves per draw (increment count)

    // Raw N-dimensional bits in [0,2^32)^N (N in [1,4]).
    [mutating]
    public vector<uint, N> getBits<let N : int>()
    {
        uint4 packed = sobolSamplerCore(sampleIndex, sampleSeed);
        vector<uint, N> result;
        [ForceUnroll]
        for (int i = 0; i < N; ++i)
        {
            result[i] = packed[i];
        }
        return result;
    }

    // N-dimensional floats in [0,1)^N (N in [1,4]).
    [mutating]
    public vector<float, N> get<let N : int>()
    {
        vector<uint, N> bits = getBits<N>();
        vector<float, N> result;
        [ForceUnroll]
        for (int i = 0; i < N; ++i)
        {
            result[i] = float(bits[i] >> 8u) * kInvTwoPow24;
        }
        return result;
    }

    // Convenience aliases.
    [mutating]
    public float get1D()
    {
        // Dedicated 1D path avoids instantiating vector<float,1>.
        uint bits = sobolSamplerCore(sampleIndex, sampleSeed).x;
        return float(bits >> 8u) * kInvTwoPow24;
    }
    [mutating] public float2 get2D() { return get<2>(); }
    [mutating] public float3 get3D() { return get<3>(); }
    [mutating] public float4 get4D() { return get<4>(); }

    // Split into Num sub-streams and keep sampling along the Index branch.
    // The sub-stream stays stratified relative to the parent (Index in [0,Num)).
    public RandomSequence split(uint index, uint num)
    {
        RandomSequence sub;
        sub.sampleSeed = sampleSeed;
        sub.sampleIndex = reversebits(reversebits(sampleIndex) * num + index);
        return sub;
    }
}

// Blue-noise error-diffusion initialisation (UE-aligned, one sample per frame).
//   dispatch : per-thread 2D position (e.g. DispatchRaysIndex().xy)
//   frameId  : monotonic frame index (added to the Sobol' index as a stride)
//   tileBits : Hilbert tile size = 2^tileBits per axis (pattern tiles beyond it)
public RandomSequence makeErrorDiffusionSequence(uint2 dispatch, uint frameId, int tileBits = 8)
{
    RandomSequence seq;
    // Spatial low discrepancy: encode dispatch.xy along a Hilbert curve so that
    // neighbouring pixels are adjacent in the Sobol' index. The frame index is
    // added above the tile bits (frame * 65536 when tileBits == 8) as a large
    // stride that fastOwenScrambling shatters into white cross-frame noise.
    uint tileId = hilbertSFCInverse(dispatch.x, dispatch.y, tileBits);
    tileId += frameId << uint(2 * tileBits);
    seq.sampleIndex = reversebits(tileId);
    // Seed shared across the frame (evolved only per draw) keeps the spatial
    // blue-noise structure intact.
    seq.sampleSeed = 0u;
    return seq;
}
