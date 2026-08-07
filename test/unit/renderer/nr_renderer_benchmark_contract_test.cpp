import dependency.vulkan;
import nr.renderer;
import nr.test;
import nr.utils;
import std;

namespace
{
[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto path = std::filesystem::path{std::string{nr::projectRoot}} / relativePath;
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));

    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

[[nodiscard]] std::string removeWhitespace(std::string_view value)
{
    return value |
           std::views::filter([](char character) { return std::isspace(static_cast<unsigned char>(character)) == 0; }) |
           std::ranges::to<std::string>();
}

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
    std::vector<double> nodeBuildMilliseconds{};
    std::vector<nr::renderer::RendererBenchmarkAsTelemetry> accelerationStructures{};
};

[[nodiscard]] AuditInput validAuditInput()
{
    return {
        .frames = {frame(1u, 1.0)},
        .passes = {{.frameOrdinal = 1u, .passIndex = 0u, .debugName = "main", .milliseconds = 1.0}},
        .statuses = {{.frameOrdinal = 1u, .expectedPassCount = 1u, .availablePassCount = 1u, .complete = true}},
        .accelerationStructures = {{.recorded = true, .available = true}},
    };
}

[[nodiscard]] bool validate(std::span<const nr::renderer::RendererBenchmarkFrame> frames)
{
    return nr::renderer::validateBenchmarkFrames(frames);
}

[[nodiscard]] nr::renderer::RendererBenchmarkQualityAudit audit(
    const AuditInput &input, std::size_t expectedNodeCount = 0u)
{
    return nr::renderer::auditRendererBenchmark(input.frames, input.passes, input.statuses, expectedNodeCount,
                                                input.nodeBuildMilliseconds, input.accelerationStructures);
}

void appendValidFrame(AuditInput &input)
{
    input.frames.emplace_back(frame(2u, 1.0));
    input.passes.emplace_back(nr::renderer::RendererBenchmarkGpuPass{
        .frameOrdinal = 2u, .passIndex = 0u, .debugName = "main", .milliseconds = 1.0});
    input.statuses.emplace_back(nr::renderer::RendererBenchmarkGpuFrameStatus{
        .frameOrdinal = 2u, .expectedPassCount = 1u, .availablePassCount = 1u, .complete = true});
    input.accelerationStructures.emplace_back(
        nr::renderer::RendererBenchmarkAsTelemetry{.recorded = true, .available = true});
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
    "renderer benchmark rejects duplicate ordinals and invalid durations", [] {
        auto valid = std::array{frame(3u, 1.0), frame(5u, 2.0)};
        valid.front().cpu.postSceneMilliseconds = 0.25;
        nr::test::require(validate(valid));
        nr::test::requireEqual(nr::renderer::rendererBenchmarkClassifiedCpuMilliseconds(valid.front().cpu), 0.25);

        auto duplicate = std::array{frame(3u, 1.0), frame(3u, 2.0)};
        nr::test::require(!validate(duplicate));

        auto negative = std::array{frame(3u, -1.0)};
        nr::test::require(!validate(negative));

        auto negativePostScene = std::array{frame(3u, 1.0)};
        negativePostScene.front().cpu.postSceneMilliseconds = -0.1;
        nr::test::require(!validate(negativePostScene));

        auto nonFinitePostScene = std::array{frame(3u, 1.0)};
        nonFinitePostScene.front().cpu.postSceneMilliseconds = std::numeric_limits<double>::quiet_NaN();
        nr::test::require(!validate(nonFinitePostScene));

        auto drift = std::array{frame(3u, 1.0), frame(4u, 2.0)};
        drift[1].configRevision = 2u;
        nr::test::require(!validate(drift));
    }};

const nr::test::CaseRegistrar executeTelemetryCase{
    "renderer benchmark validates execute telemetry accounting and counts", [] {
        auto input = validAuditInput();
        auto &sample = input.frames.front();
        sample.cpu.executeMilliseconds = 1.0;
        sample.execute.executorSetupMilliseconds = 0.2;
        sample.execute.timingSetupMilliseconds = 0.1;
        sample.execute.primaryReplayBarrierTimestampMilliseconds = 0.2;
        sample.execute.queueSubmitMilliseconds = 0.1;
        sample.execute.compiledSubmitBatchCount = 1u;
        sample.execute.recordTaskCount = 1u;
        sample.execute.replayedSecondaryCommandBufferCount = 1u;
        sample.execute.queueSubmitCount = 1u;
        sample.submitBatchCount = 1u;
        sample.recordTaskCount = 1u;
        sample.executeAccountedMainThreadMilliseconds =
            nr::renderer::rendererBenchmarkExecuteAccountedMainThreadMilliseconds(sample.execute);
        sample.executeUnclassifiedMilliseconds =
            sample.cpu.executeMilliseconds - sample.executeAccountedMainThreadMilliseconds;
        nr::test::require(nr::renderer::validateRendererBenchmarkExecuteTelemetry(sample));
        nr::test::require(audit(input).framesValid);

        auto nan = input;
        nan.frames.front().execute.queueSubmitMilliseconds = std::numeric_limits<double>::quiet_NaN();
        nr::test::require(!audit(nan).framesValid);

        auto negative = input;
        negative.frames.front().execute.queueSubmitMilliseconds = -0.1;
        nr::test::require(!audit(negative).framesValid);

        auto residual = input;
        residual.frames.front().cpu.executeMilliseconds = 0.1;
        residual.frames.front().executeUnclassifiedMilliseconds = 0.0;
        nr::test::require(!audit(residual).framesValid);

        auto inconsistentCounts = input;
        inconsistentCounts.frames.front().execute.replayedSecondaryCommandBufferCount = 0u;
        nr::test::require(!audit(inconsistentCounts).framesValid);
    }};

const nr::test::CaseRegistrar executeSchemaCase{
    "renderer benchmark execute CSV and summary schemas are complete", [] {
        auto const columns = nr::renderer::rendererBenchmarkExecuteCsvColumns();
        auto const sections = nr::renderer::rendererBenchmarkExecuteSummarySections();
        nr::test::requireEqual(columns.size(), std::size_t{22u});
        nr::test::require(std::ranges::contains(columns, std::string_view{"execute_executor_setup_ms"}));
        nr::test::require(std::ranges::contains(columns, std::string_view{"execute_unclassified_ms"}));
        nr::test::require(std::ranges::contains(columns, std::string_view{"execute_queue_submits"}));
        nr::test::requireEqual(sections.size(), std::size_t{2u});
        nr::test::require(std::ranges::contains(sections, std::string_view{"execute_substages"}));
        nr::test::require(std::ranges::contains(sections, std::string_view{"execute_counts"}));
    }};

const nr::test::CaseRegistrar frameCsvDelimiterCase{
    "renderer benchmark CSV joins execute and node columns with one delimiter", [] {
        auto const source = removeWhitespace(readProjectFile("src/renderer/nrRendererBenchmark.cpp"));
        constexpr auto writerToken = std::string_view{"autoconst&execute=frame.execute;"};
        constexpr auto literalToken = std::string_view{"frames<<std::format(\""};
        constexpr auto argumentsToken = std::string_view{"\",execute.executorSetupMilliseconds"};
        auto const writerBegin = source.find(writerToken);
        auto const literalBegin = source.find(literalToken, writerBegin);
        auto const argumentsBegin = source.find(argumentsToken, literalBegin);
        nr::test::require(writerBegin != std::string::npos && literalBegin != std::string::npos &&
                              argumentsBegin != std::string::npos,
                          "failed to locate the benchmark execute CSV writer");
        auto const formatLiteral = std::string_view{source}.substr(literalBegin + literalToken.size(),
                                                                   argumentsBegin - literalBegin - literalToken.size());
        nr::test::require(!formatLiteral.ends_with(','));
        nr::test::require(source.find("frames<<std::format(\",{:.9f}\",benchmarkNodeBuildMilliseconds_",
                                      argumentsBegin) != std::string::npos,
                          "node build CSV writer must retain its leading delimiter");
    }};

const nr::test::CaseRegistrar cpuSchemaCase{
    "renderer benchmark v4 preserves top-level CPU stages without Skeleton diagnostics", [] {
        auto const stages = nr::renderer::rendererBenchmarkCpuStageColumns();
        auto const substages = nr::renderer::rendererBenchmarkCpuSubstageColumns();
        nr::test::requireEqual(nr::renderer::rendererBenchmarkSchemaVersion(),
                               std::string_view{"nr-renderer-benchmark-v4"});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"post_scene_ms"}), std::ptrdiff_t{1});
        nr::test::requireEqual(std::ranges::count(substages, std::string_view{"post_scene_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"skeleton_patch_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(stages, std::string_view{"skeleton_rebuild_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(substages, std::string_view{"skeleton_patch_ms"}), std::ptrdiff_t{0});
        nr::test::requireEqual(std::ranges::count(substages, std::string_view{"skeleton_rebuild_ms"}),
                               std::ptrdiff_t{0});
        auto const scene = std::ranges::find(stages, std::string_view{"scene_ms"});
        auto const postScene = std::ranges::find(stages, std::string_view{"post_scene_ms"});
        auto const build = std::ranges::find(stages, std::string_view{"build_ms"});
        nr::test::require(scene != stages.end() && postScene == std::next(scene) && build == std::next(postScene));
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
                                                   nr::test::require(result.nodeTelemetryValid);
                                                   nr::test::require(result.accelerationStructureTelemetryValid);
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

                                                 auto materialNegative = validAuditInput();
                                                 materialNegative.frames.front().cpu.cpuWaitGpuMilliseconds = 2.0;
                                                 nr::test::require(!audit(materialNegative).framesValid);
                                             }};

const nr::test::CaseRegistrar nodeAndAsAuditCase{
    "renderer benchmark quality audit validates node and AS telemetry", [] {
        auto nodeSize = validAuditInput();
        nr::test::require(!audit(nodeSize, 1u).nodeTelemetryValid);

        auto nodeNegative = validAuditInput();
        nodeNegative.nodeBuildMilliseconds = {-1.0};
        nr::test::require(!audit(nodeNegative, 1u).nodeTelemetryValid);

        auto nodeNan = validAuditInput();
        nodeNan.nodeBuildMilliseconds = {std::numeric_limits<double>::quiet_NaN()};
        nr::test::require(!audit(nodeNan, 1u).nodeTelemetryValid);

        auto unavailable = validAuditInput();
        unavailable.accelerationStructures.front().available = false;
        nr::test::require(!audit(unavailable).accelerationStructureTelemetryValid);

        auto asNegative = validAuditInput();
        asNegative.accelerationStructures.front().cacheScanMilliseconds = -1.0;
        nr::test::require(!audit(asNegative).accelerationStructureTelemetryValid);

        auto asNan = validAuditInput();
        asNan.accelerationStructures.front().cacheScanMilliseconds = std::numeric_limits<double>::quiet_NaN();
        nr::test::require(!audit(asNan).accelerationStructureTelemetryValid);
    }};
} // namespace
