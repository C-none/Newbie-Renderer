import dependency.vulkan;
import nr.renderer;
import nr.test;
import nr.utils;
import std;

namespace
{
[[nodiscard]] nr::renderer::RendererBenchmarkFrame frame(std::uint64_t ordinal, double totalMilliseconds)
{
    auto sample = nr::renderer::RendererBenchmarkFrame{};
    sample.frameOrdinal = ordinal;
    sample.cpu.totalMilliseconds = totalMilliseconds;
    return sample;
}

struct AuditInput
{
    std::vector<nr::renderer::RendererBenchmarkFrame> frames{};
    std::vector<nr::renderer::RendererBenchmarkGpuPass> passes{};
    std::vector<nr::renderer::RendererBenchmarkGpuFrameStatus> statuses{};
};

[[nodiscard]] AuditInput validAuditInput()
{
    return {
        .frames = {frame(1u, 1.0)},
        .passes = {{.frameOrdinal = 1u, .passIndex = 0u, .debugName = "main", .milliseconds = 1.0}},
        .statuses = {{.frameOrdinal = 1u, .expectedPassCount = 1u, .availablePassCount = 1u, .complete = true}},
    };
}

[[nodiscard]] bool validate(std::span<const nr::renderer::RendererBenchmarkFrame> frames)
{
    return nr::renderer::validateBenchmarkFrames(frames);
}

[[nodiscard]] nr::renderer::RendererBenchmarkQualityAudit audit(const AuditInput &input)
{
    return nr::renderer::auditRendererBenchmark(input.frames, input.passes, input.statuses);
}

void appendValidFrame(AuditInput &input)
{
    input.frames.emplace_back(frame(2u, 1.0));
    input.passes.emplace_back(nr::renderer::RendererBenchmarkGpuPass{
        .frameOrdinal = 2u, .passIndex = 0u, .debugName = "main", .milliseconds = 1.0});
    input.statuses.emplace_back(nr::renderer::RendererBenchmarkGpuFrameStatus{
        .frameOrdinal = 2u, .expectedPassCount = 1u, .availablePassCount = 1u, .complete = true});
}

const nr::test::CaseRegistrar quantileCase{
    "renderer benchmark type 7 quantiles handle empty and singleton samples", [] {
        nr::test::require(std::isnan(nr::renderer::benchmarkType7Quantile({}, 0.5)));
        nr::test::requireEqual(nr::renderer::benchmarkType7Quantile({4.0}, 0.99), 4.0);
        nr::test::requireEqual(nr::renderer::benchmarkType7Quantile({1.0, 2.0, 10.0}, 0.95), 9.2);
    }};

const nr::test::CaseRegistrar distributionCase{
    "renderer benchmark distribution uses population standard deviation", [] {
        auto distribution = nr::renderer::makeRendererBenchmarkDistribution({1.0, 2.0, 3.0});
        nr::test::requireEqual(distribution.count, std::size_t{3u});
        nr::test::requireEqual(distribution.minimum, 1.0);
        nr::test::requireEqual(distribution.maximum, 3.0);
        nr::test::requireEqual(distribution.mean, 2.0);
        nr::test::requireEqual(distribution.p50, 2.0);
        nr::test::requireEqual(distribution.p95, 2.9);
        nr::test::requireEqual(distribution.p99, 2.98);
        nr::test::require(std::abs(distribution.populationStddev - std::sqrt(2.0 / 3.0)) < 0.000001);
    }};

const nr::test::CaseRegistrar validationCase{
    "renderer benchmark accepts raw top-level CPU timings and rejects invalid frames", [] {
        auto valid = std::array{frame(3u, 1.0), frame(5u, 2.0)};
        valid.front().cpu.postSceneMilliseconds = 0.25;
        valid.front().cpu.buildMilliseconds = 0.25;
        nr::test::require(validate(valid));

        auto duplicate = std::array{frame(3u, 1.0), frame(3u, 2.0)};
        nr::test::require(!validate(duplicate));

        auto negative = std::array{frame(3u, -1.0)};
        nr::test::require(!validate(negative));

        auto negativeBuild = std::array{frame(3u, 1.0)};
        negativeBuild.front().cpu.buildMilliseconds = -0.1;
        nr::test::require(!validate(negativeBuild));

        auto nonFinitePrepare = std::array{frame(3u, 1.0)};
        nonFinitePrepare.front().cpu.prepareMilliseconds = std::numeric_limits<double>::quiet_NaN();
        nr::test::require(!validate(nonFinitePrepare));

        auto drift = std::array{frame(3u, 1.0), frame(4u, 2.0)};
        drift[1].configRevision = 2u;
        nr::test::require(!validate(drift));
    }};

const nr::test::CaseRegistrar cpuSchemaCase{
    "renderer benchmark v8 exports only raw top-level CPU stages", [] {
        auto const stages = nr::renderer::rendererBenchmarkCpuStageColumns();
        nr::test::requireEqual(nr::renderer::rendererBenchmarkSchemaVersion(),
                               std::string_view{"nr-renderer-benchmark-v8"});
        nr::test::requireEqual(stages.size(), std::size_t{10u});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"build_ms"}), std::ptrdiff_t{1});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"cpu_work_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"classified_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"unclassified_ms"}), std::ptrdiff_t{0});
        auto const postScene = std::ranges::find(stages, std::string_view{"post_scene_ms"});
        auto const build = std::ranges::find(stages, std::string_view{"build_ms"});
        auto const compile = std::ranges::find(stages, std::string_view{"compile_ms"});
        nr::test::require(postScene != stages.end() && build == std::next(postScene) && compile == std::next(build));
    }};

const nr::test::CaseRegistrar disabledFinalizationCase{
    "renderer benchmark finalization is stable when benchmarking is disabled", [] {
        auto renderer = nr::renderer::Renderer{};
        renderer.configureBenchmark({});

        auto const first = renderer.finalizeBenchmark();
        auto const second = renderer.finalizeBenchmark();

        nr::test::require(first);
        nr::test::requireEqual(second, first);
    }};

const nr::test::CaseRegistrar qualityAuditCase{"renderer benchmark quality audit accepts a fully matched capture", [] {
                                                   auto const result = audit(validAuditInput());
                                                   nr::test::require(result.valid);
                                                   nr::test::require(result.framesValid);
                                               }};

const nr::test::CaseRegistrar gpuStatusAuditCase{
    "renderer benchmark quality audit classifies GPU status failures", [] {
        auto missing = validAuditInput();
        missing.statuses.clear();
        nr::test::require(audit(missing).missingGpuFrames > 0u);

        auto partial = validAuditInput();
        partial.statuses.front().availablePassCount = 0u;
        partial.statuses.front().complete = false;
        nr::test::require(audit(partial).partialGpuFrames > 0u);

        auto extra = validAuditInput();
        extra.statuses.emplace_back(nr::renderer::RendererBenchmarkGpuFrameStatus{
            .frameOrdinal = 2u, .expectedPassCount = 1u, .availablePassCount = 1u, .complete = true});
        nr::test::require(audit(extra).extraGpuStatuses > 0u);

        auto duplicate = validAuditInput();
        duplicate.statuses.emplace_back(duplicate.statuses.front());
        nr::test::require(audit(duplicate).duplicateGpuStatuses > 0u);
    }};

const nr::test::CaseRegistrar gpuPassAuditCase{
    "renderer benchmark quality audit classifies GPU pass failures", [] {
        auto duplicate = validAuditInput();
        duplicate.passes.emplace_back(duplicate.passes.front());
        duplicate.statuses.front().expectedPassCount = 2u;
        duplicate.statuses.front().availablePassCount = 2u;
        nr::test::require(audit(duplicate).duplicateGpuPasses > 0u);

        auto missingRow = validAuditInput();
        missingRow.statuses.front().expectedPassCount = 2u;
        missingRow.statuses.front().availablePassCount = 2u;
        nr::test::require(audit(missingRow).passRowCountMismatchFrames > 0u);

        auto extraFrame = validAuditInput();
        extraFrame.passes.emplace_back(nr::renderer::RendererBenchmarkGpuPass{
            .frameOrdinal = 2u, .passIndex = 0u, .debugName = "main", .milliseconds = 1.0});
        nr::test::require(audit(extraFrame).extraGpuPassFrames > 0u);

        auto negativeDuration = validAuditInput();
        negativeDuration.passes.front().milliseconds = -1.0;
        nr::test::require(audit(negativeDuration).invalidGpuDurations > 0u);

        auto nanDuration = validAuditInput();
        nanDuration.passes.front().milliseconds = std::numeric_limits<double>::quiet_NaN();
        nr::test::require(audit(nanDuration).invalidGpuDurations > 0u);
    }};

const nr::test::CaseRegistrar gpuSchemaAuditCase{
    "renderer benchmark quality audit detects every GPU schema field drift", [] {
        auto name = validAuditInput();
        appendValidFrame(name);
        name.passes.back().debugName = "renamed";
        nr::test::require(audit(name).schemaDriftFrames > 0u);

        auto queue = validAuditInput();
        appendValidFrame(queue);
        queue.passes.back().queue = nr::renderer::QueueDomain::Compute;
        nr::test::require(audit(queue).schemaDriftFrames > 0u);

        auto batch = validAuditInput();
        appendValidFrame(batch);
        batch.passes.back().batchIndex = 1u;
        nr::test::require(audit(batch).schemaDriftFrames > 0u);

        auto copy = validAuditInput();
        appendValidFrame(copy);
        copy.passes.back().isCopyPass = true;
        nr::test::require(audit(copy).schemaDriftFrames > 0u);
    }};

const nr::test::CaseRegistrar frameAuditCase{"renderer benchmark quality audit rejects frame contract violations", [] {
                                                 auto ordinal = validAuditInput();
                                                 appendValidFrame(ordinal);
                                                 ordinal.frames.back().frameOrdinal =
                                                     ordinal.frames.front().frameOrdinal;
                                                 nr::test::require(!audit(ordinal).framesValid);

                                                 auto configuration = validAuditInput();
                                                 appendValidFrame(configuration);
                                                 configuration.frames.back().configRevision = 2u;
                                                 nr::test::require(!audit(configuration).framesValid);

                                                 auto negative = validAuditInput();
                                                 negative.frames.front().cpu.totalMilliseconds = -1.0;
                                                 nr::test::require(!audit(negative).framesValid);

                                                 auto nan = validAuditInput();
                                                 nan.frames.front().cpu.totalMilliseconds =
                                                     std::numeric_limits<double>::quiet_NaN();
                                                 nr::test::require(!audit(nan).framesValid);

                                                 auto invalidWait = validAuditInput();
                                                 invalidWait.frames.front().cpu.cpuWaitGpuMilliseconds = 2.0;
                                                 nr::test::require(!audit(invalidWait).framesValid);
                                             }};

} // namespace
