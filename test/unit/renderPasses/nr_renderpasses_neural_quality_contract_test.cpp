import nr.renderPasses;
import nr.test;
import std;

namespace
{
[[nodiscard]] nr::renderPasses::NeuralAppearanceLossDistribution distribution(float mean, float percentile95)
{
    return nr::renderPasses::NeuralAppearanceLossDistribution{
        .sampleCount = 64u,
        .meanSafeLogLoss = mean,
        .percentile95SafeLogLoss = percentile95,
    };
}

[[nodiscard]] nr::renderPasses::NeuralAppearanceHeldOutStratumQuality passingStratum()
{
    return nr::renderPasses::NeuralAppearanceHeldOutStratumQuality{
        .fp32Master = distribution(0.010f, 0.020f),
        .fp16CooperativeVector = distribution(0.0104f, 0.021f),
        .zeroPrediction = distribution(0.040f, 0.080f),
    };
}

[[nodiscard]] nr::renderPasses::NeuralAppearanceQualityReport passingReport()
{
    auto const stratum = passingStratum();
    return nr::renderPasses::NeuralAppearanceQualityReport{
        .outputsFinite = true,
        .outputsNonnegative = true,
        .emaSafeLogLoss = 0.020f,
        .initialSafeLogLoss = 0.060f,
        .overall = stratum,
        .uniform = stratum,
        .highlight = stratum,
        .grazing = stratum,
    };
}

[[nodiscard]] bool contains(const nr::renderPasses::NeuralAppearanceQualityGateResult &result,
                            nr::renderPasses::NeuralAppearanceQualityViolation violation)
{
    return std::ranges::contains(result.violations, violation);
}

const nr::test::CaseRegistrar passingQualityCase{
    "neural appearance quality gate accepts a completed V2 result", [] {
        auto const result = nr::renderPasses::evaluateNeuralAppearanceQuality(passingReport());
        nr::test::require(result.passed, "a result meeting every published P0 quality threshold must pass");
        nr::test::require(result.violations.empty(), "a passing quality result must not carry violations");
    }};

const nr::test::CaseRegistrar trainingTelemetryGateCase{
    "neural appearance quality gate enforces finite output and EMA gates", [] {
        auto report = passingReport();
        report.outputsFinite = false;
        report.outputsNonnegative = false;
        report.emaSafeLogLoss = 0.030f;
        report.initialSafeLogLoss = 0.050f;

        auto const result = nr::renderPasses::evaluateNeuralAppearanceQuality(report);
        nr::test::require(!result.passed, "invalid outputs or unsuccessful EMA convergence must reject publication");
        nr::test::require(contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::OutputsNotFinite),
                          "non-finite output must be reported");
        nr::test::require(contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::OutputsNegative),
                          "negative output must be reported");
        nr::test::require(contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::EmaLossThreshold),
                          "EMA above 0.025 must be rejected");
        nr::test::require(
            contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::EmaImprovementThreshold),
            "EMA above 40 percent of initial loss must be rejected");
    }};

const nr::test::CaseRegistrar heldOutQualityGateCase{
    "neural appearance quality gate enforces every held-out stratum and FP16 budget", [] {
        auto report = passingReport();
        report.uniform.fp16CooperativeVector.meanSafeLogLoss = report.uniform.zeroPrediction.meanSafeLogLoss;
        report.highlight.fp16CooperativeVector.percentile95SafeLogLoss =
            report.highlight.zeroPrediction.percentile95SafeLogLoss;
        report.overall.fp16CooperativeVector.meanSafeLogLoss = 0.0106f;
        report.overall.fp32Master.meanSafeLogLoss = 0.0100f;

        auto const result = nr::renderPasses::evaluateNeuralAppearanceQuality(report);
        nr::test::require(!result.passed, "zero-baseline ties and an FP16 loss over 105 percent must reject publication");
        nr::test::require(
            contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::HeldOutMeanDoesNotBeatZero),
            "each held-out mean must strictly improve on zero prediction");
        nr::test::require(
            contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::HeldOutPercentileDoesNotBeatZero),
            "each held-out P95 must strictly improve on zero prediction");
        nr::test::require(
            contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::Fp16MeanExceedsFp32Budget),
            "each FP16 held-out mean must stay within the 105 percent FP32 master budget");
    }};

const nr::test::CaseRegistrar malformedDistributionGateCase{
    "neural appearance quality gate rejects malformed or mismatched held-out distributions", [] {
        auto report = passingReport();
        report.highlight.fp16CooperativeVector.sampleCount = 63u;
        report.grazing.zeroPrediction.meanSafeLogLoss = std::numeric_limits<float>::quiet_NaN();

        auto const result = nr::renderPasses::evaluateNeuralAppearanceQuality(report);
        nr::test::require(!result.passed, "missing or non-finite held-out data must reject publication");
        nr::test::require(
            contains(result, nr::renderPasses::NeuralAppearanceQualityViolation::InvalidHeldOutDistribution),
            "the quality gate must distinguish invalid held-out telemetry from a failed threshold");
    }};
} // namespace
