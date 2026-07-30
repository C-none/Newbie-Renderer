implementing common;

// ============================================================================
//  Low-discrepancy random sequence (Owen-scrambled Sobol').
//
//  Ported from the Unreal Engine path tracer (PathTracingRandomSequence.ush,
//  RANDSEQ_OWENSOBOL) as a tiled screen-space low-discrepancy sampler. One
//  sample per pixel per frame (no in-frame sample count / MaxSamples).
//
//    * (dispatch.xy, frame) -> Sobol' sample index:
//        - dispatch.xy through a Hilbert space-filling curve, preserving
//          hierarchical locality within each tile while assigning one complete
//          tile to one contiguous sequence block;
//        - the frame index is added above the tile bits as a large stride
//          (frame << 2*tileBits, i.e. frame * 4194304 for the default
//          tileBits == 11) so
//          successive frames step far along the sequence and fastOwenScrambling
//          shatters that stride into decorrelated (white) cross-frame samples.
//    * The scrambling seed is shared across the whole frame. It stays at 0 while
//      the frame ordinal fits beside the spatial bits in the 32-bit Sobol' index,
//      then changes once per frame era so the pattern does not repeat on wrap.
//      Each 4D packet consumes five successive LCG keys: one for the scrambled
//      Sobol' index and one for each output lane. Successive packets therefore
//      use distinct deterministic Owen scrambles without extra RNG state.
//
//  The whole sampler is pure ALU: no lookup textures or extra GPU resources.
// ============================================================================

// 2^-24. Only the top 24 bits of a 32-bit word are used when converting to a
// float in [0,1): float represents integers up to 2^24 exactly, so the cast is
// lossless and the good low-discrepancy high bits are preserved.
public static const float kInvTwoPow24 = 5.9604644775390625e-08f;

// ---------------------------------------------------------------------------
//  Integer hashes (bit mixers) used for seeding / decorrelation.
// ---------------------------------------------------------------------------

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
//  One rand4() packet advances the LCG five times: once for the scrambled
//  sample index and once for each of the four output-lane scramble keys.
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
//  forming a well-stratified point; advances the seed through five LCG steps.
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
//  Sequence abstraction. Every draw returns one complete 4D packet in [0,1)^4.
//  Packet consumers split the four lanes without retaining a lane cursor or
//  cached packet in per-path state.
// ---------------------------------------------------------------------------
public struct RandomSequence
{
    public uint sampleIndex = 0u; // low-discrepancy index (spatial), pre-bit-reversed
    public uint sampleSeed = 0u;  // scrambling seed; shared per frame, evolves per 4D packet

    // Draw one complete low-discrepancy packet. The core advances the scramble
    // seed through the five keys used to construct it.
    [mutating]
    public float4 rand4()
    {
        uint4 bits = sobolSamplerCore(sampleIndex, sampleSeed);
        return float4(
            float(bits.x >> 8u),
            float(bits.y >> 8u),
            float(bits.z >> 8u),
            float(bits.w >> 8u)) * kInvTwoPow24;
    }
}

// Tiled screen-space low-discrepancy initialisation (UE-aligned, one sample per frame).
//   dispatch : per-thread 2D position (e.g. DispatchRaysIndex().xy)
//   sampleFrameOrdinal : monotonic 64-bit frame ordinal as low/high uint lanes
//   tileBits           : Hilbert tile size = 2^tileBits per axis (1..15);
//                        the default 11-bit axes leave 10 frame bits
public RandomSequence makeErrorDiffusionSequence(
    uint2 dispatch,
    uint2 sampleFrameOrdinal,
    int tileBits = 11)
{
    RandomSequence seq;
    // Spatial low discrepancy: encode the low dispatch.xy tile coordinates along
    // a Hilbert curve so one complete tile spans one contiguous Sobol' index
    // block. With the default tileBits == 11, the 32-bit index dedicates 22 bits
    // to a 2048x2048 spatial tile and 10 bits to the in-era frame ordinal. The
    // resulting frame * 4194304 stride is shattered into white cross-frame noise
    // by fastOwenScrambling. The 1..15 tileBits contract keeps every shift
    // strictly below 32 bits.
    uint spatialBitCount = uint(2 * tileBits);
    uint frameBitCount = 32u - spatialBitCount;
    uint frameInEraMask = 0xffffffffu >> spatialBitCount;
    uint frameInEra = sampleFrameOrdinal.x & frameInEraMask;
    uint tileId = hilbertSFCInverse(dispatch.x, dispatch.y, tileBits);
    tileId += frameInEra << spatialBitCount;
    seq.sampleIndex = reversebits(tileId);

    // Fold the ordinal bits that do not fit in the Sobol' index into a frame-era
    // seed. It is independent of dispatch, preserving the tile-local
    // low-discrepancy structure. Era zero deliberately retains the zero seed;
    // with the default tileBits == 11, each era spans 1,024 frames.
    uint2 frameEra = uint2(
        (sampleFrameOrdinal.x >> frameBitCount) |
            (sampleFrameOrdinal.y << spatialBitCount),
        sampleFrameOrdinal.y >> frameBitCount);
    // strongIntegerHash(0u) == 0u, so frame era zero naturally keeps the legacy
    // seed while every nonzero era is mixed without a special-case branch.
    seq.sampleSeed = strongIntegerHash(frameEra.x ^ strongIntegerHash(frameEra.y));
    return seq;
}
