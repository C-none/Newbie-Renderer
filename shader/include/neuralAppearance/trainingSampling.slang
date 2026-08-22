module trainingSampling;
import common;
import include.neuralAppearance.halfVectorConditionalCdf;
import include.neuralAppearance.layout;

public static const float kNeuralAppearanceTrainingPi = 3.14159265358979323846f;
public static const float kNeuralAppearanceTrainingTwoPi =
    2.0f * kNeuralAppearanceTrainingPi;

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

// Rusinkiewicz parameterization exposes the half vector directly, keeping
// specular-peak coverage independent from the analytic material lobe.
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

float neuralAppearanceMaximumDifferenceAngle(float thetaH, float phiD)
{
    float cosineH = cos(thetaH);
    float radialY = sin(thetaH) * abs(cos(phiD));
    float radius = length(float2(cosineH, radialY));
    float boundaryAngle = acos(saturate(kNeuralAppearanceHalfVectorDirectionMinimumCosine / radius));
    return max(boundaryAngle - atan2(radialY, cosineH), 0.0f);
}

float neuralAppearanceConditionalDifferenceAzimuth(float thetaH, float unitValue)
{
    float quadrantCoordinate = unitValue * 4.0f;
    uint quadrant = min(uint(quadrantCoordinate), 3u);
    float localUnit = frac(quadrantCoordinate);
    float quarterAzimuth = neuralAppearanceHalfVectorDifferenceAzimuthInverseCdf(thetaH, localUnit);
    if (quadrant == 0u)
    {
        return quarterAzimuth;
    }
    if (quadrant == 1u)
    {
        return kNeuralAppearanceTrainingPi - quarterAzimuth;
    }
    if (quadrant == 2u)
    {
        return kNeuralAppearanceTrainingPi + quarterAzimuth;
    }
    return kNeuralAppearanceTrainingTwoPi - quarterAzimuth;
}

// Uniform thetaH plus the conditional valid-domain CDF is intentional. In the
// paired eight-seed experiment it reduced balanced held-out FP16 loss versus
// rejection sampling, while solid-angle-uniform half vectors over-allocated
// grazing samples and regressed the full-domain stratum. The inverse-CDF LUT is
// needed only for phiD: thetaDMaximum is analytic, but its integral over phiD
// has no practical closed-form inverse. Sampling thetaD inside that boundary
// produces one valid upper-hemisphere View/Light pair without retries or a
// wave-divergent fallback path.
public NeuralAppearanceDirectionPair neuralAppearanceTrainingDirectionPair(
    uint sampleSeed, uint dimensionOffset)
{
    float thetaH = neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 0u) *
                   kNeuralAppearanceHalfVectorThetaMaximum;
    float thetaDUnit = neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 1u);
    float phiH = neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 2u) *
                 kNeuralAppearanceTrainingTwoPi;
    float phiD = neuralAppearanceConditionalDifferenceAzimuth(
        thetaH, neuralAppearanceTrainingRandom(sampleSeed, dimensionOffset + 3u));
    float thetaD = thetaDUnit * neuralAppearanceMaximumDifferenceAngle(thetaH, phiD);
    return neuralAppearanceRusinkiewiczDirectionPair(thetaH, thetaD, phiH, phiD);
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
