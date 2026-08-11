module trainingSampling;
import common;
import include.neuralAppearance.layout;

public static const float kNeuralAppearanceTrainingPi = 3.14159265358979323846f;
public static const float kNeuralAppearanceTrainingTwoPi =
    2.0f * kNeuralAppearanceTrainingPi;
public static const uint kNeuralAppearanceDirectionSamplingAttempts = 64u;
public static const uint kNeuralAppearanceMollificationSampleCountMax = 256u;

public struct NeuralAppearanceDirectionPair
{
    public float3 viewDirection;
    public float3 lightDirection;
};

public struct NeuralAppearanceTrainingSample
{
    public uint sampleSeed;
    public float2 uv;
    public NeuralAppearanceDirectionPair directions;
    public float mollificationAngle;
};

public uint neuralAppearanceTrainingHash(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    return value ^ (value >> 16u);
}

public float neuralAppearanceTrainingRandom(uint sampleSeed, uint dimension)
{
    uint bits = neuralAppearanceTrainingHash(
        sampleSeed + dimension * 0x9e3779b9u) & 0x00ffffffu;
    return (float(bits) + 0.5f) / 16777216.0f;
}

float3 neuralAppearanceTrainingNormalize(float3 value, float3 fallback)
{
    float lengthSquared = dot(value, value);
    return lengthSquared >= 1.0e-12f
        ? value * rsqrt(lengthSquared)
        : fallback;
}

public float3 neuralAppearanceTrainingOrthogonal(float3 direction)
{
    float3 normalizedDirection = neuralAppearanceTrainingNormalize(
        direction, float3(0.0f, 1.0f, 0.0f));
    float3 referenceAxis = abs(normalizedDirection.y) < 0.999f
        ? float3(0.0f, 1.0f, 0.0f)
        : float3(1.0f, 0.0f, 0.0f);
    return neuralAppearanceTrainingNormalize(
        cross(referenceAxis, normalizedDirection), float3(0.0f, 0.0f, 1.0f));
}

NeuralAppearanceDirectionPair neuralAppearanceRusinkiewiczDirectionPair(
    float thetaH, float thetaD, float phiH, float phiD)
{
    float sinThetaH = sin(thetaH);
    float cosThetaH = cos(thetaH);
    float sinPhiH = sin(phiH);
    float cosPhiH = cos(phiH);

    float3 halfDirection = float3(
        sinThetaH * cosPhiH, cosThetaH, sinThetaH * sinPhiH);
    float3 halfTangent = float3(
        cosThetaH * cosPhiH, -sinThetaH, cosThetaH * sinPhiH);
    float3 halfBitangent = float3(-sinPhiH, 0.0f, cosPhiH);

    float sinThetaD = sin(thetaD);
    float cosThetaD = cos(thetaD);
    float radialCos = sinThetaD * cos(phiD);
    float radialSin = sinThetaD * sin(phiD);
    float3 radial = halfTangent * radialCos + halfBitangent * radialSin;

    NeuralAppearanceDirectionPair pair;
    pair.viewDirection = halfDirection * cosThetaD + radial;
    pair.lightDirection = halfDirection * cosThetaD - radial;
    return pair;
}

public NeuralAppearanceDirectionPair neuralAppearanceTrainingDirectionPair(
    uint sampleSeed, uint dimensionOffset)
{
    for (uint attempt = 0u; attempt < kNeuralAppearanceDirectionSamplingAttempts; ++attempt)
    {
        uint dimension = dimensionOffset + attempt * 4u;
        float thetaH = neuralAppearanceTrainingRandom(sampleSeed, dimension + 0u) *
                       (0.5f * kNeuralAppearanceTrainingPi);
        float thetaD = neuralAppearanceTrainingRandom(sampleSeed, dimension + 1u) *
                       (0.5f * kNeuralAppearanceTrainingPi);
        float phiH = neuralAppearanceTrainingRandom(sampleSeed, dimension + 2u) *
                     kNeuralAppearanceTrainingTwoPi;
        float phiD = neuralAppearanceTrainingRandom(sampleSeed, dimension + 3u) *
                     kNeuralAppearanceTrainingTwoPi;
        NeuralAppearanceDirectionPair pair = neuralAppearanceRusinkiewiczDirectionPair(
            thetaH, thetaD, phiH, phiD);
        if (pair.viewDirection.y >= 1.0e-3f && pair.lightDirection.y >= 0.0f)
        {
            return pair;
        }
    }

    // This path is deterministic and valid even if every rejection sample is grazing.
    NeuralAppearanceDirectionPair fallback;
    fallback.viewDirection = float3(0.0f, 1.0f, 0.0f);
    fallback.lightDirection = float3(0.0f, 1.0f, 0.0f);
    return fallback;
}

public float3 neuralAppearanceTrainingUniformConeDirection(
    float3 axis, float angleRadians, uint sampleSeed, uint dimensionOffset)
{
    float3 normalizedAxis = neuralAppearanceTrainingNormalize(
        axis, float3(0.0f, 1.0f, 0.0f));
    if (angleRadians <= 0.0f)
    {
        return normalizedAxis;
    }

    float clampedAngle = min(angleRadians, 0.5f * kNeuralAppearanceTrainingPi);
    float minimumCosine = cos(clampedAngle);
    float cosine = lerp(
        1.0f, minimumCosine,
        neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 0u));
    float sine = sqrt(max(1.0f - cosine * cosine, 0.0f));
    float phase = kNeuralAppearanceTrainingTwoPi *
                  neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 1u);
    float3 tangent = neuralAppearanceTrainingOrthogonal(normalizedAxis);
    float3 bitangent = cross(normalizedAxis, tangent);
    return normalizedAxis * cosine +
           sine * (tangent * cos(phase) + bitangent * sin(phase));
}

public float neuralAppearanceTrainingCosineSchedule(
    float initialValue, float finalValue, uint step, uint stepCount)
{
    if (stepCount == 0u || step >= stepCount)
    {
        return finalValue;
    }
    if (step == 0u)
    {
        return initialValue;
    }

    float progress = float(step) / float(stepCount);
    float weight = 0.5f * (1.0f + cos(kNeuralAppearanceTrainingPi * progress));
    return finalValue + (initialValue - finalValue) * weight;
}

public float neuralAppearanceTrainingMollificationAngle(
    uint trainingStep, uint mollificationStepCount, float initialAngleRadians)
{
    return neuralAppearanceTrainingCosineSchedule(
        max(initialAngleRadians, 0.0f), 0.0f,
        trainingStep, mollificationStepCount);
}

public NeuralAppearanceTrainingSample neuralAppearanceTrainingSample(
    uint trainingStep, uint sampleIndex,
    uint mollificationStepCount, float initialAngleRadians)
{
    NeuralAppearanceTrainingSample sample;
    sample.sampleSeed = neuralAppearanceTrainingHash(
        trainingStep * kNeuralAppearanceBatchSize + sampleIndex + 1u);
    sample.uv = float2(
        neuralAppearanceTrainingRandom(sample.sampleSeed, 0u),
        neuralAppearanceTrainingRandom(sample.sampleSeed, 1u));
    sample.directions = neuralAppearanceTrainingDirectionPair(
        neuralAppearanceTrainingHash(sample.sampleSeed ^ 0x243f6a88u), 0u);
    sample.mollificationAngle = neuralAppearanceTrainingMollificationAngle(
        trainingStep, mollificationStepCount, initialAngleRadians);
    return sample;
}
