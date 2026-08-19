module trainingSampling;
import common;
import include.neuralAppearance.layout;

public static const float kNeuralAppearanceTrainingPi = 3.14159265358979323846f;
public static const float kNeuralAppearanceTrainingTwoPi =
    2.0f * kNeuralAppearanceTrainingPi;
public static const uint kNeuralAppearanceDirectionSamplingAttempts = 64u;

public struct NeuralAppearanceDirectionPair
{
    public float3 viewDirection = float3(0.0f, 1.0f, 0.0f);
    public float3 lightDirection = float3(0.0f, 1.0f, 0.0f);

    public __init()
    {
    }

    public __init(float3 inputViewDirection, float3 inputLightDirection)
    {
        viewDirection = inputViewDirection;
        lightDirection = inputLightDirection;
    }
};

public struct NeuralAppearanceTrainingSample
{
    public uint materialSeed = 0u;
    public NeuralAppearanceDirectionPair directions = NeuralAppearanceDirectionPair();

    public __init()
    {
    }

    public __init(uint inputMaterialSeed, NeuralAppearanceDirectionPair inputDirections)
    {
        materialSeed = inputMaterialSeed;
        directions = inputDirections;
    }
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

// Rusinkiewicz parameterization: the half vector pins the specular peak so a
// uniform sweep of (thetaH, thetaD, phiH, phiD) keeps highlight coverage dense
// without importance sampling the analytic lobe.
public NeuralAppearanceDirectionPair neuralAppearanceRusinkiewiczDirectionPair(
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

    return NeuralAppearanceDirectionPair(
        halfDirection * cosThetaD + radial,
        halfDirection * cosThetaD - radial);
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
        // Both directions stay in the geometric upper hemisphere, which is the
        // domain the folded reflection is defined on.
        if (pair.viewDirection.y >= 1.0e-3f && pair.lightDirection.y >= 1.0e-3f)
        {
            return pair;
        }
    }

    // This path is deterministic and valid even if every rejection sample is grazing.
    return NeuralAppearanceDirectionPair(
        float3(0.0f, 1.0f, 0.0f),
        float3(0.0f, 1.0f, 0.0f));
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

// The seed separates otherwise identical runs. Without it the initial weights and
// the whole sample sequence are fixed constants, so repeated training is bit
// identical and run-to-run variance cannot be measured at all.
public NeuralAppearanceTrainingSample neuralAppearanceTrainingSample(
    uint trainingStep, uint sampleIndex, uint trainingSeed)
{
    uint sampleSeed = neuralAppearanceTrainingHash(
        neuralAppearanceTrainingHash(trainingSeed + 0x9e3779b9u) +
        trainingStep * kNeuralAppearanceBatchSize + sampleIndex + 1u);
    return NeuralAppearanceTrainingSample(
        neuralAppearanceTrainingHash(sampleSeed ^ 0x5bf03635u),
        neuralAppearanceTrainingDirectionPair(
            neuralAppearanceTrainingHash(sampleSeed ^ 0x243f6a88u), 0u));
}
