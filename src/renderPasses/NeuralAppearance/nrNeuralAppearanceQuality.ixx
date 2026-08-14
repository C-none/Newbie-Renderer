export module nr.renderPasses:neuralAppearanceQuality;

import std;

export namespace nr::renderPasses
{
// This is the host-side, post-readback quality contract for a completed V2
// training run. The producer must use independent held-out samples for every
// distribution; the gate deliberately has no access to mutable training state.
struct NeuralAppearanceLossDistribution
{
    std::uint32_t sampleCount = 0u;
    float meanSafeLogLoss = 0.0f;
    float percentile95SafeLogLoss = 0.0f;
};

struct NeuralAppearanceHeldOutStratumQuality
{
    NeuralAppearanceLossDistribution fp32Master{};
    NeuralAppearanceLossDistribution fp16CooperativeVector{};
    NeuralAppearanceLossDistribution zeroPrediction{};
};

struct NeuralAppearanceQualityReport
{
    bool outputsFinite = false;
    bool outputsNonnegative = false;
    float emaSafeLogLoss = 0.0f;
    float initialSafeLogLoss = 0.0f;
    NeuralAppearanceHeldOutStratumQuality overall{};
    NeuralAppearanceHeldOutStratumQuality uniform{};
    NeuralAppearanceHeldOutStratumQuality highlight{};
    NeuralAppearanceHeldOutStratumQuality grazing{};
};

enum class NeuralAppearanceQualityViolation : std::uint8_t
{
    OutputsNotFinite,
    OutputsNegative,
    InvalidTrainingLossTelemetry,
    EmaLossThreshold,
    EmaImprovementThreshold,
    InvalidHeldOutDistribution,
    HeldOutMeanDoesNotBeatZero,
    HeldOutPercentileDoesNotBeatZero,
    Fp16MeanExceedsFp32Budget,
};

struct NeuralAppearanceQualityGateResult
{
    bool passed = false;
    std::vector<NeuralAppearanceQualityViolation> violations{};
};

inline constexpr float neuralAppearanceMaximumEmaSafeLogLoss = 0.025f;
inline constexpr float neuralAppearanceMaximumEmaInitialLossRatio = 0.40f;
inline constexpr float neuralAppearanceMaximumFp16ToFp32MeanLossRatio = 1.05f;

[[nodiscard]] inline NeuralAppearanceQualityGateResult evaluateNeuralAppearanceQuality(
    const NeuralAppearanceQualityReport &report)
{
    auto result = NeuralAppearanceQualityGateResult{};
    auto addViolation = [&](NeuralAppearanceQualityViolation violation) {
        result.violations.push_back(violation);
    };

    if (!report.outputsFinite)
    {
        addViolation(NeuralAppearanceQualityViolation::OutputsNotFinite);
    }
    if (!report.outputsNonnegative)
    {
        addViolation(NeuralAppearanceQualityViolation::OutputsNegative);
    }

    auto const trainingLossValid = std::isfinite(report.emaSafeLogLoss) && report.emaSafeLogLoss >= 0.0f &&
                                   std::isfinite(report.initialSafeLogLoss) && report.initialSafeLogLoss > 0.0f;
    if (!trainingLossValid)
    {
        addViolation(NeuralAppearanceQualityViolation::InvalidTrainingLossTelemetry);
    }
    else
    {
        if (report.emaSafeLogLoss > neuralAppearanceMaximumEmaSafeLogLoss)
        {
            addViolation(NeuralAppearanceQualityViolation::EmaLossThreshold);
        }
        if (report.emaSafeLogLoss > report.initialSafeLogLoss * neuralAppearanceMaximumEmaInitialLossRatio)
        {
            addViolation(NeuralAppearanceQualityViolation::EmaImprovementThreshold);
        }
    }

    auto validateDistribution = [&](const NeuralAppearanceLossDistribution &distribution) {
        return distribution.sampleCount > 0u && std::isfinite(distribution.meanSafeLogLoss) &&
               distribution.meanSafeLogLoss >= 0.0f && std::isfinite(distribution.percentile95SafeLogLoss) &&
               distribution.percentile95SafeLogLoss >= 0.0f;
    };
    auto validateDistributions = [&](const NeuralAppearanceHeldOutStratumQuality &stratum) {
        if (!validateDistribution(stratum.fp32Master) || !validateDistribution(stratum.fp16CooperativeVector) ||
            !validateDistribution(stratum.zeroPrediction) ||
            stratum.fp32Master.sampleCount != stratum.fp16CooperativeVector.sampleCount ||
            stratum.fp32Master.sampleCount != stratum.zeroPrediction.sampleCount)
        {
            addViolation(NeuralAppearanceQualityViolation::InvalidHeldOutDistribution);
            return false;
        }
        return true;
    };

    auto validateStratum = [&](const NeuralAppearanceHeldOutStratumQuality &stratum) {
        if (!validateDistributions(stratum))
        {
            return;
        }

        if (stratum.fp16CooperativeVector.meanSafeLogLoss >= stratum.zeroPrediction.meanSafeLogLoss)
        {
            addViolation(NeuralAppearanceQualityViolation::HeldOutMeanDoesNotBeatZero);
        }
        if (stratum.fp16CooperativeVector.percentile95SafeLogLoss >=
            stratum.zeroPrediction.percentile95SafeLogLoss)
        {
            addViolation(NeuralAppearanceQualityViolation::HeldOutPercentileDoesNotBeatZero);
        }
    };

    if (validateDistributions(report.overall) &&
        report.overall.fp16CooperativeVector.meanSafeLogLoss >
            report.overall.fp32Master.meanSafeLogLoss * neuralAppearanceMaximumFp16ToFp32MeanLossRatio)
    {
        addViolation(NeuralAppearanceQualityViolation::Fp16MeanExceedsFp32Budget);
    }
    validateStratum(report.uniform);
    validateStratum(report.highlight);
    validateStratum(report.grazing);

    result.passed = result.violations.empty();
    return result;
}
} // namespace nr::renderPasses
