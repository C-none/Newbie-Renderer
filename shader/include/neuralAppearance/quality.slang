module quality;
import common;
import include.neuralAppearance.layout;
import include.neuralAppearance.trainingSampling;

// The published artifact is evaluated against fixed held-out samples. These
// domains remain independent from the training step/sample sequence and are
// deliberately small enough to run in the existing post-training quality pass.
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

// Optimization loss floor for the cube-root domain. Below it the derivative of
// the power-log map would dominate the update for values that are visually zero.
public static const float kNeuralAppearanceLossFloor = 1.0e-6f;
public static const float kNeuralAppearanceDiffuseLossWeight = 0.5f;
public static const float kNeuralAppearanceSpecularLossWeight = 0.5f;

// Stratum 0 sweeps the whole hemisphere, stratum 1 concentrates on the specular
// highlight, and stratum 2 stays just above the horizon where the folded
// reflection preimage changes fastest.
public NeuralAppearanceDirectionPair neuralAppearanceStratumDirectionPair(uint stratum, uint sampleSeed)
{
    if (stratum == 0u)
    {
        return neuralAppearanceTrainingDirectionPair(sampleSeed ^ 0xa24baed4u, 2u);
    }

    float thetaHBase = stratum == 1u ? 0.02f : 0.40f;
    float thetaHRange = stratum == 1u ? 0.35f : 0.075f;
    float thetaDRange = stratum == 1u ? 0.06f : 0.02f;
    float thetaH = (thetaHBase + thetaHRange * neuralAppearanceTrainingRandom(sampleSeed, 2u)) *
                   kNeuralAppearanceTrainingPi;
    float thetaD = thetaDRange * neuralAppearanceTrainingRandom(sampleSeed, 3u);
    float phiH = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 4u);
    float phiD = kNeuralAppearanceTrainingTwoPi * neuralAppearanceTrainingRandom(sampleSeed, 5u);
    return neuralAppearanceRusinkiewiczDirectionPair(thetaH, thetaD, phiH, phiD);
}

public NeuralAppearanceTrainingSample neuralAppearanceHeldOutQualitySample(uint sampleIndex)
{
    uint stratum = sampleIndex / kNeuralAppearanceHeldOutQualitySamplesPerStratum;
    uint sampleInStratum = sampleIndex % kNeuralAppearanceHeldOutQualitySamplesPerStratum;
    uint sampleSeed = neuralAppearanceTrainingHash(0x9e3779b9u ^ (stratum * 0x85ebca6bu + sampleInStratum));
    return NeuralAppearanceTrainingSample(
        neuralAppearanceTrainingHash(sampleSeed ^ 0x5bf03635u),
        neuralAppearanceStratumDirectionPair(stratum, sampleSeed));
}

// Acceptance metric. Bounded, symmetric in both lobes, and comparable across the
// V2 and V3 gates so the published thresholds keep their meaning.
public float neuralAppearanceQualitySafeLogLoss(float3 prediction, float3 target)
{
    float3 predictionLog = log(float3(1.0f) + max(prediction, float3(0.0f)));
    float3 targetLog = log(float3(1.0f) + max(target, float3(0.0f)));
    return dot(abs(predictionLog - targetLog), float3(1.0f / 3.0f));
}

public float neuralAppearanceLobeSafeLogLoss(
    NeuralAppearanceLobes prediction, NeuralAppearanceLobes target)
{
    return kNeuralAppearanceDiffuseLossWeight *
               neuralAppearanceQualitySafeLogLoss(prediction.diffuse, target.diffuse) +
           kNeuralAppearanceSpecularLossWeight *
               neuralAppearanceQualitySafeLogLoss(prediction.specular, target.specular);
}

// Optimization loss. The cube-root domain keeps the high dynamic range specular
// peak from dominating while staying far more sensitive in the dark diffuse
// range than the bounded acceptance metric.
[Differentiable]
float3 neuralAppearancePowerLog(float3 value)
{
    // exp(log(x) / 3) instead of pow(x, 1/3): the log/exp pair has a reliable
    // reverse-mode derivative, while pow contributes no gradient at all.
    float3 floored = max(value, float3(kNeuralAppearanceLossFloor));
    return 3.0f * (exp(log(floored) * (1.0f / 3.0f)) - float3(1.0f));
}

[Differentiable]
public float neuralAppearanceChannelTrainingLoss(float3 prediction, no_diff float3 target)
{
    return dot(abs(neuralAppearancePowerLog(prediction) - neuralAppearancePowerLog(target)),
               float3(1.0f / 3.0f));
}

// Telemetry only: the training path calls the per-channel form directly so the
// differentiable chain stays on plain float3 values.
public float neuralAppearanceLobeTrainingLoss(
    NeuralAppearanceLobes prediction, NeuralAppearanceLobes target)
{
    return kNeuralAppearanceDiffuseLossWeight *
               neuralAppearanceChannelTrainingLoss(prediction.diffuse, target.diffuse) +
           kNeuralAppearanceSpecularLossWeight *
               neuralAppearanceChannelTrainingLoss(prediction.specular, target.specular);
}
