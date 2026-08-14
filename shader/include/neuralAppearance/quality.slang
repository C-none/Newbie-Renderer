module quality;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

// The published artifact is evaluated against fixed held-out samples. These
// domains remain independent from the training step/sample sequence and are
// deliberately small enough to run in the existing post-training pack pass.
public static const uint kNeuralAppearanceHeldOutQualitySamplesPerStratum = 64u;
public static const uint kNeuralAppearanceHeldOutQualityStratumCount = 3u;
public static const uint kNeuralAppearanceHeldOutQualitySampleCount =
    kNeuralAppearanceHeldOutQualitySamplesPerStratum * kNeuralAppearanceHeldOutQualityStratumCount;

public static const uint kNeuralAppearanceQualityFp32Finite = 1u << 0u;
public static const uint kNeuralAppearanceQualityFp32Nonnegative = 1u << 1u;
public static const uint kNeuralAppearanceQualityFp16Finite = 1u << 2u;
public static const uint kNeuralAppearanceQualityFp16Nonnegative = 1u << 3u;
public static const uint kNeuralAppearanceQualityTargetFinite = 1u << 4u;
public static const uint kNeuralAppearanceQualityTargetNonnegative = 1u << 5u;

int neuralAppearanceQualityRepeatCoordinate(int coordinate, int extent)
{
    int wrapped = coordinate % extent;
    return wrapped < 0 ? wrapped + extent : wrapped;
}

uint neuralAppearanceQualityTexelIndex(int2 coordinate)
{
    int x = neuralAppearanceQualityRepeatCoordinate(coordinate.x, int(kNeuralAppearanceLatentWidth));
    int y = neuralAppearanceQualityRepeatCoordinate(coordinate.y, int(kNeuralAppearanceLatentHeight));
    return uint(y) * kNeuralAppearanceLatentWidth + uint(x);
}

float4 neuralAppearanceQualityBilinearWeights(float2 uv, out uint4 texelIndices)
{
    float2 texelPosition = frac(uv) * float2(kNeuralAppearanceLatentWidth, kNeuralAppearanceLatentHeight) - 0.5f;
    int2 baseTexel = int2(floor(texelPosition));
    texelIndices = uint4(
        neuralAppearanceQualityTexelIndex(baseTexel),
        neuralAppearanceQualityTexelIndex(baseTexel + int2(1, 0)),
        neuralAppearanceQualityTexelIndex(baseTexel + int2(0, 1)),
        neuralAppearanceQualityTexelIndex(baseTexel + int2(1, 1)));
    float2 fraction = frac(texelPosition);
    return float4(
        (1.0f - fraction.x) * (1.0f - fraction.y),
        fraction.x * (1.0f - fraction.y),
        (1.0f - fraction.x) * fraction.y,
        fraction.x * fraction.y);
}

public NeuralAppearanceLatent neuralAppearanceQualitySampleMasterLatent(
    StructuredBuffer<float4> trainingLatent, float2 uv)
{
    uint4 texelIndices;
    float4 weights = neuralAppearanceQualityBilinearWeights(uv, texelIndices);
    float4 latent0 = trainingLatent[texelIndices.x * 2u + 0u] * weights.x +
                     trainingLatent[texelIndices.y * 2u + 0u] * weights.y +
                     trainingLatent[texelIndices.z * 2u + 0u] * weights.z +
                     trainingLatent[texelIndices.w * 2u + 0u] * weights.w;
    float4 latent1 = trainingLatent[texelIndices.x * 2u + 1u] * weights.x +
                     trainingLatent[texelIndices.y * 2u + 1u] * weights.y +
                     trainingLatent[texelIndices.z * 2u + 1u] * weights.z +
                     trainingLatent[texelIndices.w * 2u + 1u] * weights.w;
    return NeuralAppearanceLatent(latent0, latent1);
}

public NeuralAppearanceLatent neuralAppearanceQualitySampleInferenceLatent(
    ByteAddressBuffer inferenceLatent, float2 uv)
{
    uint4 texelIndices;
    float4 weights = neuralAppearanceQualityBilinearWeights(uv, texelIndices);
    float4 values[8];
    for (uint tap = 0u; tap < 4u; ++tap)
    {
        for (uint texture = 0u; texture < 2u; ++texture)
        {
            uint byteOffset = (texelIndices[tap] * 2u + texture) * 8u;
            values[tap * 2u + texture] = float4(
                float(inferenceLatent.Load<half>(byteOffset + 0u)),
                float(inferenceLatent.Load<half>(byteOffset + 2u)),
                float(inferenceLatent.Load<half>(byteOffset + 4u)),
                float(inferenceLatent.Load<half>(byteOffset + 6u)));
        }
    }
    return NeuralAppearanceLatent(
        values[0] * weights.x + values[2] * weights.y + values[4] * weights.z + values[6] * weights.w,
        values[1] * weights.x + values[3] * weights.y + values[5] * weights.z + values[7] * weights.w);
}

public NeuralAppearanceTrainingSample neuralAppearanceHeldOutQualitySample(uint sampleIndex)
{
    uint stratum = sampleIndex / kNeuralAppearanceHeldOutQualitySamplesPerStratum;
    uint sampleInStratum = sampleIndex % kNeuralAppearanceHeldOutQualitySamplesPerStratum;
    uint sampleSeed = neuralAppearanceTrainingHash(0x9e3779b9u ^ (stratum * 0x85ebca6bu + sampleInStratum));
    float2 uv = float2(
        neuralAppearanceTrainingRandom(sampleSeed, 0u),
        neuralAppearanceTrainingRandom(sampleSeed, 1u));
    NeuralAppearanceDirectionPair directions;
    if (stratum == 0u)
    {
        directions = neuralAppearanceTrainingDirectionPair(sampleSeed ^ 0xa24baed4u, 2u);
    }
    else if (stratum == 1u)
    {
        // Near-mirror directions cover the analytic fixture's specular highlights.
        float thetaH = (0.02f + 0.35f * neuralAppearanceTrainingRandom(sampleSeed, 2u)) *
                       kNeuralAppearanceTrainingPi;
        float thetaD = 0.06f * neuralAppearanceTrainingRandom(sampleSeed, 3u);
        float phiH = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 4u);
        float phiD = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 5u);
        directions = neuralAppearanceRusinkiewiczDirectionPair(thetaH, thetaD, phiH, phiD);
    }
    else
    {
        // The half-vector stays just above the horizon, with a small difference
        // angle so both directions remain in the visible hemisphere.
        float thetaH = (0.40f + 0.075f * neuralAppearanceTrainingRandom(sampleSeed, 2u)) *
                       kNeuralAppearanceTrainingPi;
        float thetaD = 0.02f * neuralAppearanceTrainingRandom(sampleSeed, 3u);
        float phiH = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 4u);
        float phiD = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 5u);
        directions = neuralAppearanceRusinkiewiczDirectionPair(thetaH, thetaD, phiH, phiD);
    }
    return NeuralAppearanceTrainingSample(sampleSeed, uv, directions, 0.0f);
}

public float neuralAppearanceQualitySafeLogLoss(float3 prediction, float3 target)
{
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(target, float3(0.0f)));
    return dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
}
