module nr.renderer;
import :renderer;
import dependency.assets;
import dependency.json;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :frameServices;
import :renderGraphBuilder;
import :renderGraphCompiler;
import :renderGraphExecutor;
import :rendererSubmission;

namespace nr::renderer
{
[[nodiscard]] std::string_view renderGraphSkeletonModeName(RenderGraphSkeletonMode mode) noexcept
{
    switch (mode)
    {
    case RenderGraphSkeletonMode::Legacy:
        return "legacy";
    case RenderGraphSkeletonMode::Enabled:
        return "enabled";
    case RenderGraphSkeletonMode::Differential:
        return "differential";
    }
    return "unknown";
}

[[nodiscard]] std::string_view renderGraphSkeletonMissReasonName(RenderGraphSkeletonMissReason reason) noexcept
{
    switch (reason)
    {
    case RenderGraphSkeletonMissReason::None:
        return "none";
    case RenderGraphSkeletonMissReason::Disabled:
        return "disabled";
    case RenderGraphSkeletonMissReason::UnsupportedNode:
        return "unsupported_node";
    case RenderGraphSkeletonMissReason::KeyNotFound:
        return "key_not_found";
    case RenderGraphSkeletonMissReason::StructureMismatch:
        return "structure_mismatch";
    case RenderGraphSkeletonMissReason::PatchFailed:
        return "patch_failed";
    case RenderGraphSkeletonMissReason::Invalidated:
        return "invalidated";
    }
    return "unknown";
}
namespace
{
inline constexpr auto renderGraphSkeletonMissReasons = std::array{
    RenderGraphSkeletonMissReason::None,
    RenderGraphSkeletonMissReason::Disabled,
    RenderGraphSkeletonMissReason::UnsupportedNode,
    RenderGraphSkeletonMissReason::KeyNotFound,
    RenderGraphSkeletonMissReason::StructureMismatch,
    RenderGraphSkeletonMissReason::PatchFailed,
    RenderGraphSkeletonMissReason::Invalidated,
};

void accumulateCpuTimings(RendererCpuFrameTimings &target, const RendererCpuFrameTimings &sample) noexcept
{
    target.cpuWaitGpuMilliseconds += sample.cpuWaitGpuMilliseconds;
    target.frameSetupMilliseconds += sample.frameSetupMilliseconds;
    target.sceneMilliseconds += sample.sceneMilliseconds;
    target.postSceneMilliseconds += sample.postSceneMilliseconds;
    target.buildMilliseconds += sample.buildMilliseconds;
    target.compileMilliseconds += sample.compileMilliseconds;
    target.prepareMilliseconds += sample.prepareMilliseconds;
    target.executeMilliseconds += sample.executeMilliseconds;
    target.presentMilliseconds += sample.presentMilliseconds;
    target.totalMilliseconds += sample.totalMilliseconds;
}

[[nodiscard]] RendererCpuFrameTimings averageCpuTimings(const RendererCpuFrameTimings &total,
                                                        std::uint32_t frameCount) noexcept
{
    auto const divisor = static_cast<double>(std::max(frameCount, 1u));
    return RendererCpuFrameTimings{
        .cpuWaitGpuMilliseconds = total.cpuWaitGpuMilliseconds / divisor,
        .frameSetupMilliseconds = total.frameSetupMilliseconds / divisor,
        .sceneMilliseconds = total.sceneMilliseconds / divisor,
        .postSceneMilliseconds = total.postSceneMilliseconds / divisor,
        .buildMilliseconds = total.buildMilliseconds / divisor,
        .compileMilliseconds = total.compileMilliseconds / divisor,
        .prepareMilliseconds = total.prepareMilliseconds / divisor,
        .executeMilliseconds = total.executeMilliseconds / divisor,
        .presentMilliseconds = total.presentMilliseconds / divisor,
        .totalMilliseconds = total.totalMilliseconds / divisor,
    };
}

[[nodiscard]] std::vector<RendererGpuPassAverage> averageGpuPassTimings(
    const std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage> &totals)
{
    auto averages = totals | std::views::values | std::views::transform([](const RendererGpuPassAverage &total) {
                        auto average = total;
                        auto const divisor = static_cast<double>(std::max(average.sampleCount, 1u));
                        average.milliseconds /= divisor;
                        return average;
                    }) |
                    std::ranges::to<std::vector>();

    std::ranges::sort(averages, [](const RendererGpuPassAverage &lhs, const RendererGpuPassAverage &rhs) {
        return std::tie(lhs.pass.value, lhs.debugName) < std::tie(rhs.pass.value, rhs.debugName);
    });
    return averages;
}
} // namespace

void Renderer::recordCpuTimingSample(const RendererCpuFrameTimings &timings) noexcept
{
    accumulateCpuTimings(cpuTimingAccumulator_, timings);
    ++cpuStatistics_.pendingSampleFrameCount;

    if (cpuStatistics_.pendingSampleFrameCount < nr::statistics::sampleFrameCount())
    {
        return;
    }

    cpuStatistics_.average = averageCpuTimings(cpuTimingAccumulator_, cpuStatistics_.pendingSampleFrameCount);
    cpuStatistics_.averagedFrameCount = cpuStatistics_.pendingSampleFrameCount;
    cpuStatistics_.pendingSampleFrameCount = 0u;
    cpuStatistics_.valid = true;
    cpuTimingAccumulator_ = {};
}

void Renderer::recordGpuPassTimingSample(const GpuPassTimingFrame &timings)
{
    std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample &sample) {
        auto key = std::pair{sample.pass.value, sample.debugName};
        auto [entryIt, inserted] = gpuPassTimingAccumulator_.try_emplace(key);
        if (inserted)
        {
            entryIt->second.pass = sample.pass;
            entryIt->second.debugName = sample.debugName;
            entryIt->second.queue = sample.queue;
            entryIt->second.isCopyPass = sample.isCopyPass;
        }

        entryIt->second.milliseconds += sample.milliseconds;
        ++entryIt->second.sampleCount;
    });

    ++gpuPassStatistics_.pendingSampleFrameCount;
    if (gpuPassStatistics_.pendingSampleFrameCount < nr::statistics::sampleFrameCount())
    {
        return;
    }

    gpuPassStatistics_.averages = averageGpuPassTimings(gpuPassTimingAccumulator_);
    gpuPassStatistics_.averagedFrameCount = gpuPassStatistics_.pendingSampleFrameCount;
    gpuPassStatistics_.pendingSampleFrameCount = 0u;
    gpuPassStatistics_.valid = true;
    gpuPassTimingAccumulator_.clear();
}

[[nodiscard]] double benchmarkType7Quantile(std::vector<double> values, double probability)
{
    if (values.empty() || probability < 0.0 || probability > 1.0 ||
        !std::ranges::all_of(values, [](double value) { return std::isfinite(value); }))
    {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::ranges::sort(values);
    if (values.size() == 1u)
    {
        return values.front();
    }
    auto const position = probability * static_cast<double>(values.size() - 1u);
    auto const lower = static_cast<std::size_t>(std::floor(position));
    auto const upper = static_cast<std::size_t>(std::ceil(position));
    auto const fraction = position - static_cast<double>(lower);
    return values[lower] + (values[upper] - values[lower]) * fraction;
}

[[nodiscard]] RendererBenchmarkDistribution makeRendererBenchmarkDistribution(std::vector<double> values)
{
    auto distribution = RendererBenchmarkDistribution{};
    distribution.count = values.size();
    if (values.empty())
    {
        return distribution;
    }
    distribution.minimum = *std::ranges::min_element(values);
    distribution.maximum = *std::ranges::max_element(values);
    distribution.mean = std::ranges::fold_left(values, 0.0, std::plus{}) / static_cast<double>(values.size());
    auto const variance = std::ranges::fold_left(values | std::views::transform([&](double value) {
                                                     auto const delta = value - distribution.mean;
                                                     return delta * delta;
                                                 }),
                                                 0.0, std::plus{}) /
                          static_cast<double>(values.size());
    distribution.p50 = benchmarkType7Quantile(values, .5);
    distribution.p95 = benchmarkType7Quantile(values, .95);
    distribution.p99 = benchmarkType7Quantile(values, .99);
    distribution.populationStddev = std::sqrt(variance);
    return distribution;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteCsvColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 22u>{
        std::string_view{"execute_executor_setup_ms"},
        "execute_completed_gpu_timing_readback_ms",
        "execute_timing_setup_ms",
        "execute_per_frame_lookup_ms",
        "execute_swapchain_acquire_ms",
        "execute_deferred_prepare_ms",
        "execute_task_plan_launch_ms",
        "execute_primary_record_before_collect_ms",
        "execute_record_completion_wait_ms",
        "execute_primary_replay_barrier_timestamp_ms",
        "execute_primary_end_and_submit_build_ms",
        "execute_queue_submit_ms",
        "execute_initial_release_record_submit_ms",
        "execute_synthetic_present_record_submit_ms",
        "execute_finalization_ms",
        "execute_accounted_main_thread_ms",
        "execute_unclassified_ms",
        "execute_compiled_batches",
        "execute_acquire_batches",
        "execute_record_tasks",
        "execute_replayed_secondary_command_buffers",
        "execute_queue_submits",
    };
    return columns;
}

[[nodiscard]] std::string_view rendererBenchmarkSchemaVersion() noexcept
{
    return "nr-renderer-benchmark-v3";
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuStageColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 13u>{
        std::string_view{"wait_gpu_ms"},
        "frame_setup_ms",
        "scene_ms",
        "post_scene_ms",
        "build_ms",
        "compile_ms",
        "prepare_ms",
        "execute_ms",
        "present_ms",
        "total_ms",
        "cpu_work_ms",
        "classified_ms",
        "unclassified_ms",
    };
    return columns;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkCpuSubstageColumns() noexcept
{
    static constexpr auto columns = std::array<std::string_view, 10u>{
        std::string_view{"scene_begin_upload_ms"},
        "scene_raster_extract_ms",
        "scene_tlas_extract_ms",
        "scene_bridge_ms",
        "tlas_texture_collection_ms",
        "graph_prelude_ms",
        "ui_collect_ms",
        "node_loop_ms",
        "skeleton_patch_ms",
        "skeleton_rebuild_ms",
    };
    return columns;
}

[[nodiscard]] std::span<const std::string_view> rendererBenchmarkExecuteSummarySections() noexcept
{
    static constexpr auto sections = std::array<std::string_view, 2u>{
        std::string_view{"execute_substages"},
        "execute_counts",
    };
    return sections;
}

[[nodiscard]] double rendererBenchmarkClassifiedCpuMilliseconds(const RendererCpuFrameTimings &timings) noexcept
{
    return timings.cpuWaitGpuMilliseconds + timings.frameSetupMilliseconds + timings.sceneMilliseconds +
           timings.postSceneMilliseconds + timings.buildMilliseconds + timings.compileMilliseconds +
           timings.prepareMilliseconds + timings.executeMilliseconds + timings.presentMilliseconds;
}

[[nodiscard]] double rendererBenchmarkExecuteAccountedMainThreadMilliseconds(
    const ExecutorBenchmarkTelemetry &telemetry) noexcept
{
    return telemetry.executorSetupMilliseconds + telemetry.completedGpuTimingReadbackMilliseconds +
           telemetry.timingSetupMilliseconds + telemetry.perFrameLookupMilliseconds +
           telemetry.swapchainAcquireMilliseconds + telemetry.deferredPrepareMilliseconds +
           telemetry.taskPlanLaunchMilliseconds + telemetry.primaryRecordBeforeCollectMilliseconds +
           telemetry.recordCompletionWaitMilliseconds + telemetry.primaryReplayBarrierTimestampMilliseconds +
           telemetry.primaryEndAndSubmitBuildMilliseconds + telemetry.queueSubmitMilliseconds +
           telemetry.initialReleaseRecordSubmitMilliseconds + telemetry.syntheticPresentRecordSubmitMilliseconds +
           telemetry.finalizationMilliseconds;
}

[[nodiscard]] bool validateRendererBenchmarkExecuteTelemetry(const RendererBenchmarkFrame &frame) noexcept
{
    constexpr auto accountingEpsilonMilliseconds = 0.001;
    auto const &telemetry = frame.execute;
    auto durations = std::array{
        telemetry.executorSetupMilliseconds,
        telemetry.completedGpuTimingReadbackMilliseconds,
        telemetry.timingSetupMilliseconds,
        telemetry.perFrameLookupMilliseconds,
        telemetry.swapchainAcquireMilliseconds,
        telemetry.deferredPrepareMilliseconds,
        telemetry.taskPlanLaunchMilliseconds,
        telemetry.primaryRecordBeforeCollectMilliseconds,
        telemetry.recordCompletionWaitMilliseconds,
        telemetry.primaryReplayBarrierTimestampMilliseconds,
        telemetry.primaryEndAndSubmitBuildMilliseconds,
        telemetry.queueSubmitMilliseconds,
        telemetry.initialReleaseRecordSubmitMilliseconds,
        telemetry.syntheticPresentRecordSubmitMilliseconds,
        telemetry.finalizationMilliseconds,
        frame.executeAccountedMainThreadMilliseconds,
        frame.executeUnclassifiedMilliseconds,
    };
    auto const accounted = rendererBenchmarkExecuteAccountedMainThreadMilliseconds(telemetry);
    auto const residual = frame.cpu.executeMilliseconds - accounted;
    return std::ranges::all_of(durations, [](double value) { return std::isfinite(value) && value >= 0.0; }) &&
           std::abs(frame.executeAccountedMainThreadMilliseconds - accounted) <= accountingEpsilonMilliseconds &&
           residual >= -accountingEpsilonMilliseconds &&
           std::abs(frame.executeUnclassifiedMilliseconds - std::max(0.0, residual)) <= accountingEpsilonMilliseconds &&
           telemetry.acquireBatchCount <= telemetry.compiledSubmitBatchCount &&
           telemetry.replayedSecondaryCommandBufferCount == telemetry.recordTaskCount &&
           telemetry.queueSubmitCount >= telemetry.compiledSubmitBatchCount &&
           frame.submitBatchCount == telemetry.queueSubmitCount && frame.recordTaskCount == telemetry.recordTaskCount;
}

[[nodiscard]] bool validateRendererBenchmarkSkeletonTelemetry(const RendererBenchmarkFrame &frame,
                                                              RenderGraphSkeletonMode mode) noexcept
{
    auto const reasonKnown = std::ranges::contains(renderGraphSkeletonMissReasons, frame.skeletonMissReason);
    if (!reasonKnown)
    {
        return false;
    }

    switch (mode)
    {
    case RenderGraphSkeletonMode::Legacy:
        return !frame.skeletonHit && frame.skeletonMissReason == RenderGraphSkeletonMissReason::Disabled &&
               frame.skeletonPatchMilliseconds == 0.0 && frame.skeletonRebuildMilliseconds == 0.0;
    case RenderGraphSkeletonMode::Enabled:
        if (frame.skeletonHit)
        {
            return frame.skeletonMissReason == RenderGraphSkeletonMissReason::None &&
                   frame.skeletonRebuildMilliseconds == 0.0;
        }
        return frame.skeletonMissReason != RenderGraphSkeletonMissReason::None &&
               frame.skeletonMissReason != RenderGraphSkeletonMissReason::Disabled &&
               frame.skeletonPatchMilliseconds == 0.0;
    case RenderGraphSkeletonMode::Differential:
        return false;
    }
    return false;
}

[[nodiscard]] bool validateBenchmarkFrames(std::span<const RendererBenchmarkFrame> frames, RenderGraphSkeletonMode mode)
{
    auto const configurationStable =
        frames.empty() || std::ranges::all_of(frames, [&](const RendererBenchmarkFrame &frame) {
            auto const &first = frames.front();
            return frame.configRevision == first.configRevision && frame.displayExtent == first.displayExtent &&
                   frame.renderExtent == first.renderExtent;
        });
    return configurationStable &&
           std::ranges::adjacent_find(
               frames, [](const auto &lhs, const auto &rhs) { return lhs.frameOrdinal >= rhs.frameOrdinal; }) ==
               frames.end() &&
           std::ranges::all_of(frames, [mode](const RendererBenchmarkFrame &frame) {
               auto durations = std::array{frame.cpu.cpuWaitGpuMilliseconds,
                                           frame.cpu.frameSetupMilliseconds,
                                           frame.cpu.sceneMilliseconds,
                                           frame.cpu.postSceneMilliseconds,
                                           frame.cpu.buildMilliseconds,
                                           frame.cpu.compileMilliseconds,
                                           frame.cpu.prepareMilliseconds,
                                           frame.cpu.executeMilliseconds,
                                           frame.cpu.presentMilliseconds,
                                           frame.cpu.totalMilliseconds,
                                           frame.sceneBeginUploadMilliseconds,
                                           frame.sceneRasterExtractMilliseconds,
                                           frame.sceneTlasExtractMilliseconds,
                                           frame.sceneBridgeMilliseconds,
                                           frame.tlasTextureCollectionMilliseconds,
                                           frame.graphPreludeMilliseconds,
                                           frame.uiCollectMilliseconds,
                                           frame.nodeLoopMilliseconds,
                                           frame.skeletonPatchMilliseconds,
                                           frame.skeletonRebuildMilliseconds};
               return std::ranges::all_of(durations,
                                          [](double value) { return std::isfinite(value) && value >= 0.0; }) &&
                      [&] {
                          auto const classified = rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu);
                          return frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds >= -0.001 &&
                                 frame.cpu.totalMilliseconds - classified >= -0.001 &&
                                 validateRendererBenchmarkExecuteTelemetry(frame) &&
                                 validateRendererBenchmarkSkeletonTelemetry(frame, mode);
                      }();
           });
}

[[nodiscard]] RendererBenchmarkQualityAudit auditRendererBenchmark(
    std::span<const RendererBenchmarkFrame> frames, std::span<const RendererBenchmarkGpuPass> passes,
    std::span<const RendererBenchmarkGpuFrameStatus> statuses, std::size_t expectedNodeCount,
    std::span<const double> nodeBuildMilliseconds, std::span<const RendererBenchmarkAsTelemetry> asTelemetry,
    RenderGraphSkeletonMode skeletonMode)
{
    auto audit = RendererBenchmarkQualityAudit{};
    audit.framesValid = validateBenchmarkFrames(frames, skeletonMode);
    audit.nodeTelemetryValid = nodeBuildMilliseconds.size() == frames.size() * expectedNodeCount;
    audit.accelerationStructureTelemetryValid = asTelemetry.size() == frames.size();
    audit.missingGpuFrames = 0u;
    std::ranges::for_each(nodeBuildMilliseconds, [&](double value) {
        audit.nodeTelemetryValid = audit.nodeTelemetryValid && std::isfinite(value) && value >= 0.0;
    });
    std::ranges::for_each(asTelemetry, [&](const RendererBenchmarkAsTelemetry &as) {
        audit.accelerationStructureTelemetryValid =
            audit.accelerationStructureTelemetryValid && as.recorded && as.available &&
            std::isfinite(as.cacheScanMilliseconds) && as.cacheScanMilliseconds >= 0.0 &&
            std::isfinite(as.metadataPlanMilliseconds) && as.metadataPlanMilliseconds >= 0.0 &&
            std::isfinite(as.cpuWritesMilliseconds) && as.cpuWritesMilliseconds >= 0.0 &&
            std::isfinite(as.tlasSizingMilliseconds) && as.tlasSizingMilliseconds >= 0.0 &&
            std::isfinite(as.graphDeclareMilliseconds) && as.graphDeclareMilliseconds >= 0.0;
    });
    std::ranges::for_each(statuses, [&](const RendererBenchmarkGpuFrameStatus &status) {
        auto count = std::ranges::count(statuses, status.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (count != 1u)
        {
            ++audit.duplicateGpuStatuses;
        }
        auto frame = std::ranges::find(frames, status.frameOrdinal, &RendererBenchmarkFrame::frameOrdinal);
        if (frame == frames.end())
        {
            ++audit.extraGpuStatuses;
        }
        if (!status.complete || status.expectedPassCount != status.availablePassCount)
        {
            ++audit.partialGpuFrames;
        }
    });
    std::ranges::for_each(frames, [&](const RendererBenchmarkFrame &frame) {
        auto statusCount =
            std::ranges::count(statuses, frame.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (statusCount == 0u)
        {
            ++audit.missingGpuFrames;
        }
        auto framePasses = passes | std::views::filter([&](const RendererBenchmarkGpuPass &pass) {
                               return pass.frameOrdinal == frame.frameOrdinal;
                           });
        auto seen = std::set<std::uint32_t>{};
        auto rowCount = std::size_t{0u};
        std::ranges::for_each(framePasses, [&](const RendererBenchmarkGpuPass &pass) {
            ++rowCount;
            if (!seen.insert(pass.passIndex).second)
            {
                ++audit.duplicateGpuPasses;
            }
            if (!std::isfinite(pass.milliseconds) || pass.milliseconds < 0.0)
            {
                ++audit.invalidGpuDurations;
            }
        });
        auto status = std::ranges::find(statuses, frame.frameOrdinal, &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        if (status != statuses.end() && rowCount != status->availablePassCount)
        {
            ++audit.passRowCountMismatchFrames;
        }
    });
    auto baseline = std::map<std::uint32_t, std::tuple<std::string, QueueDomain, std::uint32_t, bool>>{};
    if (!frames.empty())
    {
        std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == frames.front().frameOrdinal)
            {
                baseline.emplace(pass.passIndex,
                                 std::tuple{pass.debugName, pass.queue, pass.batchIndex, pass.isCopyPass});
            }
        });
    }
    if (!frames.empty() && baseline.empty())
    {
        audit.valid = false;
    }
    std::ranges::for_each(frames, [&](const RendererBenchmarkFrame &frame) {
        auto schema = std::map<std::uint32_t, std::tuple<std::string, QueueDomain, std::uint32_t, bool>>{};
        std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == frame.frameOrdinal)
            {
                schema.emplace(pass.passIndex,
                               std::tuple{pass.debugName, pass.queue, pass.batchIndex, pass.isCopyPass});
            }
        });
        if (schema != baseline)
        {
            ++audit.schemaDriftFrames;
        }
    });
    std::ranges::for_each(passes, [&](const RendererBenchmarkGpuPass &pass) {
        if (std::ranges::find(frames, pass.frameOrdinal, &RendererBenchmarkFrame::frameOrdinal) == frames.end())
        {
            ++audit.extraGpuPassFrames;
        }
    });
    audit.valid = audit.framesValid && audit.nodeTelemetryValid && audit.accelerationStructureTelemetryValid &&
                  audit.missingGpuFrames == 0u && audit.partialGpuFrames == 0u && audit.extraGpuStatuses == 0u &&
                  audit.duplicateGpuStatuses == 0u && audit.invalidGpuDurations == 0u &&
                  audit.duplicateGpuPasses == 0u && audit.passRowCountMismatchFrames == 0u &&
                  audit.extraGpuPassFrames == 0u && audit.schemaDriftFrames == 0u;
    return audit;
}

void Renderer::configureBenchmark(RendererBenchmarkConfig config)
{
    benchmarkConfig_ = std::move(config);
    benchmarkFrames_.clear();
    benchmarkGpuPasses_.clear();
    benchmarkGpuFrameStatuses_.clear();
    benchmarkGpuPassNames_.clear();
    benchmarkNodeNames_.clear();
    benchmarkCurrentNodeBuildMilliseconds_.clear();
    benchmarkNodeBuildMilliseconds_.clear();
    benchmarkAsTelemetry_.clear();
    benchmarkSkeletonStatisticsBefore_ = renderGraphSkeletonStatistics();
    benchmarkWarmupAccepted_ = 0u;
    benchmarkDrainRendered_ = 0u;
    benchmarkStartedAt_ = std::chrono::system_clock::now();
    benchmarkFinalized_ = false;
    benchmarkSucceeded_ = false;
    if (!benchmarkConfig_.enabled)
    {
        benchmarkPhase_ = RendererBenchmarkPhase::disabled;
        return;
    }
    nrAssert(benchmarkConfig_.measureFrames > 0u, "Renderer benchmark requires measureFrames > 0.");
    nrAssert(!benchmarkConfig_.outputDirectory.empty(), "Renderer benchmark requires an output directory.");
    nrAssert(graphInstalled_, "Renderer benchmark must be configured after graph installation.");
    nrAssert(benchmarkConfig_.renderGraphSkeletonMode != RenderGraphSkeletonMode::Differential,
             "Renderer benchmark does not support Differential RenderGraph Skeleton timing.");
    nrAssert(benchmarkConfig_.renderGraphSkeletonMode == renderGraphSkeletonMode_,
             "Renderer benchmark Skeleton metadata must match the configured Renderer mode.");
    benchmarkNodeNames_ = installedNodes_ |
                          std::views::transform([](const InstalledNode &node) { return node.config.instanceName; }) |
                          std::ranges::to<std::vector>();
    benchmarkCurrentNodeBuildMilliseconds_.resize(benchmarkNodeNames_.size());
    benchmarkFrames_.reserve(benchmarkConfig_.measureFrames);
    benchmarkGpuPasses_.reserve(static_cast<std::size_t>(benchmarkConfig_.measureFrames) * 64u);
    benchmarkGpuFrameStatuses_.reserve(benchmarkConfig_.measureFrames);
    benchmarkGpuPassNames_.reserve(64u);
    benchmarkNodeBuildMilliseconds_.reserve(benchmarkConfig_.measureFrames * benchmarkNodeNames_.size());
    benchmarkAsTelemetry_.reserve(benchmarkConfig_.measureFrames);
    benchmarkPhase_ =
        benchmarkConfig_.warmupFrames == 0u ? RendererBenchmarkPhase::measure : RendererBenchmarkPhase::warmup;
}

void Renderer::configureRenderGraphSkeletonMode(RenderGraphSkeletonMode mode) noexcept
{
    if (renderGraphSkeletonMode_ == mode)
    {
        return;
    }
    renderGraphSkeletonMode_ = mode;
    cacheSuite_.skeletonCache.clear(RenderGraphSkeletonMissReason::Invalidated);
}

[[nodiscard]] RenderGraphSkeletonMode Renderer::renderGraphSkeletonMode() const noexcept
{
    return renderGraphSkeletonMode_;
}

[[nodiscard]] RenderGraphSkeletonCacheStatistics Renderer::renderGraphSkeletonStatistics() const noexcept
{
    return cacheSuite_.skeletonCache.statistics();
}

[[nodiscard]] bool Renderer::benchmarkComplete() const noexcept
{
    return benchmarkPhase_ == RendererBenchmarkPhase::finalized;
}

void Renderer::recordBenchmarkGpuPassTimings(const GpuPassTimingFrame &timings)
{
    if (!benchmarkConfig_.enabled || benchmarkFrames_.empty() ||
        timings.frameOrdinal < benchmarkFrames_.front().frameOrdinal ||
        timings.frameOrdinal > benchmarkFrames_.back().frameOrdinal)
    {
        return;
    }
    auto statusIt = std::ranges::lower_bound(benchmarkGpuFrameStatuses_, timings.frameOrdinal, {},
                                             &RendererBenchmarkGpuFrameStatus::frameOrdinal);
    if (statusIt != benchmarkGpuFrameStatuses_.end() && statusIt->frameOrdinal == timings.frameOrdinal)
    {
        return;
    }
    benchmarkGpuFrameStatuses_.insert(statusIt, RendererBenchmarkGpuFrameStatus{
                                                    .frameOrdinal = timings.frameOrdinal,
                                                    .expectedPassCount = timings.expectedPassCount,
                                                    .availablePassCount = timings.availablePassCount,
                                                    .complete = timings.complete,
                                                });
    std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample &sample) {
        auto const passIndex = sample.pass.value;
        if (benchmarkGpuPassNames_.size() <= passIndex)
        {
            benchmarkGpuPassNames_.resize(static_cast<std::size_t>(passIndex) + 1u);
        }
        if (benchmarkGpuPassNames_[passIndex].empty())
        {
            benchmarkGpuPassNames_[passIndex] = sample.debugName;
        }
        benchmarkGpuPasses_.push_back(RendererBenchmarkGpuPass{
            .frameOrdinal = timings.frameOrdinal,
            .passIndex = passIndex,
            .debugName = sample.debugName,
            .queue = sample.queue,
            .batchIndex = sample.batchIndex,
            .isCopyPass = sample.isCopyPass,
            .milliseconds = sample.milliseconds,
        });
    });
}

[[nodiscard]] bool Renderer::finalizeBenchmark()
{
    if (!benchmarkConfig_.enabled)
    {
        return true;
    }
    if (benchmarkFinalized_)
    {
        return benchmarkSucceeded_;
    }
    if (!benchmarkComplete())
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK", "Benchmark finalization requested before drain completed.");
        return false;
    }
    auto error = std::error_code{};
    std::filesystem::create_directories(benchmarkConfig_.outputDirectory, error);
    if (error)
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK",
                  std::format("Failed to create benchmark output directory '{}': {}",
                              benchmarkConfig_.outputDirectory.string(), error.message()));
        return false;
    }
    auto csv = [&](std::string_view value) {
        auto const needsQuotes =
            value.contains(',') || value.contains('"') || value.contains('\n') || value.contains('\r');
        if (!needsQuotes)
        {
            return std::string{value};
        }
        auto escaped = std::string{};
        std::ranges::for_each(value, [&](char character) {
            escaped += character;
            if (character == '"')
            {
                escaped += character;
            }
        });
        return std::format("\"{}\"", escaped);
    };
    auto const audit = auditRendererBenchmark(benchmarkFrames_, benchmarkGpuPasses_, benchmarkGpuFrameStatuses_,
                                              benchmarkNodeNames_.size(), benchmarkNodeBuildMilliseconds_,
                                              benchmarkAsTelemetry_, benchmarkConfig_.renderGraphSkeletonMode);
    auto const dataValid = audit.valid;
    auto const requested = benchmarkConfig_.measureFrames;
    auto const accepted = benchmarkFrames_.size();
    auto const runStatus = dataValid && accepted == requested ? "valid" : "invalid";
    auto const displayExtent = benchmarkFrames_.empty() ? vk::Extent2D{1u, 1u} : benchmarkFrames_.front().displayExtent;
    auto const renderExtent = benchmarkFrames_.empty() ? vk::Extent2D{1u, 1u} : benchmarkFrames_.front().renderExtent;
    auto const startEpochMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(benchmarkStartedAt_.time_since_epoch()).count();
    auto const endEpochMilliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    auto const props = device_->physicalDevice.getProperties();
    using Json = ::dependency::json::JsonValue;
    using JsonArray = Json::Array;
    using JsonObject = Json::Object;
    auto serializeBenchmarkArtifact = [](std::string_view artifactName, const Json &value, std::string &output) {
        constexpr auto maximumSerializedBenchmarkArtifactBytes = std::size_t{16u * 1024u * 1024u};
        auto const serializationError =
            ::dependency::json::serializeJson(value, output, maximumSerializedBenchmarkArtifactBytes - 1u);
        if (serializationError != ::dependency::json::JsonError::none)
        {
            nr::nrLog(nr::LogLevel::error, "BENCHMARK",
                      std::format("Failed to serialize benchmark artifact '{}': JSON error {}.", artifactName,
                                  static_cast<std::uint32_t>(serializationError)));
            return false;
        }
        output.push_back('\n');
        return true;
    };
    auto nodeSchema = JsonArray{};
    nodeSchema.reserve(benchmarkNodeNames_.size());
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) {
        nodeSchema.emplace_back(JsonObject{
            {"column", Json{std::format("node_build_{}_ms", nodeIndex)}},
            {"name", Json{benchmarkNodeNames_[nodeIndex]}},
        });
    });
    auto metadataDocument = Json{JsonObject{
        {"schema", Json{rendererBenchmarkSchemaVersion()}},
        {"run_status", Json{runStatus}},
        {"run_id", Json{std::format("{}", startEpochMilliseconds)}},
        {"os", Json{"Windows"}},
        {"start_epoch_ms", Json{static_cast<std::int64_t>(startEpochMilliseconds)}},
        {"end_epoch_ms", Json{static_cast<std::int64_t>(endEpochMilliseconds)}},
        {"argv", Json{benchmarkConfig_.commandLine}},
        {"pipeline", Json{benchmarkConfig_.pipelineId}},
        {"model", Json{benchmarkConfig_.modelPath}},
        {"dlss_quality", Json{benchmarkConfig_.dlssQuality}},
#if defined(NDEBUG)
        {"build_config", Json{"Release"}},
        {"validation_enabled", Json{false}},
#else
        {"build_config", Json{"Debug"}},
        {"validation_enabled", Json{true}},
#endif
        {"ui_mode", Json{"visible-static"}},
        {"display_extent", Json{JsonArray{
                               Json{static_cast<std::uint64_t>(displayExtent.width)},
                               Json{static_cast<std::uint64_t>(displayExtent.height)},
                           }}},
        {"render_extent", Json{JsonArray{
                              Json{static_cast<std::uint64_t>(renderExtent.width)},
                              Json{static_cast<std::uint64_t>(renderExtent.height)},
                          }}},
        {"node_build_columns", Json{std::move(nodeSchema)}},
        {"gpu", Json{std::string_view{props.deviceName.data()}}},
        {"driver_version", Json{static_cast<std::uint64_t>(props.driverVersion)}},
        {"cpu_logical_threads", Json{static_cast<std::uint64_t>(std::thread::hardware_concurrency())}},
        {"frames_in_flight", Json{static_cast<std::uint64_t>(nr::maxFrameInFlight)}},
        {"warmup_requested", Json{static_cast<std::uint64_t>(benchmarkConfig_.warmupFrames)}},
        {"warmup_accepted", Json{static_cast<std::uint64_t>(benchmarkWarmupAccepted_)}},
        {"measure_requested", Json{static_cast<std::uint64_t>(requested)}},
        {"measure_accepted", Json{static_cast<std::uint64_t>(accepted)}},
        {"drain_rendered", Json{static_cast<std::uint64_t>(benchmarkDrainRendered_)}},
        {"time_unit", Json{"milliseconds"}},
        {"cpu_nesting", Json{"top-level stages are mutually exclusive wall-clock intervals; Frame Setup excludes Wait "
                             "GPU; Post Scene is top-level; CPU substages, including Skeleton patch and rebuild, are "
                             "nested diagnostics and must not be summed with top-level stages"}},
        {"render_graph_skeleton_mode", Json{renderGraphSkeletonModeName(benchmarkConfig_.renderGraphSkeletonMode)}},
        {"gpu_semantics", Json{"per-pass timestamp durations only; cross-queue values are not a frame critical path"}},
        {"quantile", Json{"Hyndman-Fan type 7"}},
    }};
    auto metadataText = std::string{};
    if (!serializeBenchmarkArtifact("metadata.json", metadataDocument, metadataText))
    {
        return false;
    }
    auto metadata = std::ofstream{benchmarkConfig_.outputDirectory / "metadata.json", std::ios::binary};
    auto frames = std::ofstream{benchmarkConfig_.outputDirectory / "frames.csv", std::ios::binary};
    auto gpu = std::ofstream{benchmarkConfig_.outputDirectory / "gpu_passes.csv", std::ios::binary};
    auto summary = std::ofstream{benchmarkConfig_.outputDirectory / "summary.json", std::ios::binary};
    if (!metadata || !frames || !gpu || !summary)
    {
        nr::nrLog(nr::LogLevel::error, "BENCHMARK", "Failed to open one or more benchmark artifacts for writing.");
        return false;
    }
    metadata << metadataText;
    frames << "frame_ordinal,frame_slot,config_revision,display_width,display_height,render_width,render_height,dlss_"
              "quality";
    std::ranges::for_each(rendererBenchmarkCpuStageColumns(),
                          [&](std::string_view column) { frames << std::format(",{}", column); });
    std::ranges::for_each(rendererBenchmarkCpuSubstageColumns(),
                          [&](std::string_view column) { frames << std::format(",{}", column); });
    frames << ",skeleton_hit,skeleton_miss_reason,raster_packets,rt_packets,tlas_packets,submit_batches,record_tasks";
    std::ranges::for_each(rendererBenchmarkExecuteCsvColumns(),
                          [&](std::string_view column) { frames << std::format(",{}", column); });
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()),
                          [&](std::size_t nodeIndex) { frames << std::format(",node_build_{}_ms", nodeIndex); });
    frames << ",as_recorded,as_available,as_cache_scan_ms,as_metadata_plan_ms,as_cpu_writes_ms,as_tlas_sizing_ms,as_"
              "graph_declare_ms,as_packets,as_instances,as_dirty_blas\n";
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkFrames_.size()), [&](std::size_t frameIndex) {
        const auto &frame = benchmarkFrames_[frameIndex];
        auto const classified = rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu);
        frames << std::format(
            "{},{},{},{},{},{},{},{},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:."
            "9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{},{},{},{},{}",
            frame.frameOrdinal, frame.frameSlot, frame.configRevision, frame.displayExtent.width,
            frame.displayExtent.height, frame.renderExtent.width, frame.renderExtent.height,
            csv(benchmarkConfig_.dlssQuality), frame.cpu.cpuWaitGpuMilliseconds, frame.cpu.frameSetupMilliseconds,
            frame.cpu.sceneMilliseconds, frame.cpu.postSceneMilliseconds, frame.cpu.buildMilliseconds,
            frame.cpu.compileMilliseconds, frame.cpu.prepareMilliseconds, frame.cpu.executeMilliseconds,
            frame.cpu.presentMilliseconds, frame.cpu.totalMilliseconds,
            frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds, classified,
            frame.cpu.totalMilliseconds - classified, frame.sceneBeginUploadMilliseconds,
            frame.sceneRasterExtractMilliseconds, frame.sceneTlasExtractMilliseconds, frame.sceneBridgeMilliseconds,
            frame.tlasTextureCollectionMilliseconds, frame.graphPreludeMilliseconds, frame.uiCollectMilliseconds,
            frame.nodeLoopMilliseconds, frame.skeletonPatchMilliseconds, frame.skeletonRebuildMilliseconds,
            frame.skeletonHit, csv(renderGraphSkeletonMissReasonName(frame.skeletonMissReason)),
            frame.sceneRasterPacketCount, frame.sceneRtPacketCount, frame.sceneTlasPacketCount, frame.submitBatchCount,
            frame.recordTaskCount);
        auto const &execute = frame.execute;
        frames << std::format(
            ",{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},"
            "{:.9f},{:.9f},{},{},{},{},{}",
            execute.executorSetupMilliseconds, execute.completedGpuTimingReadbackMilliseconds,
            execute.timingSetupMilliseconds, execute.perFrameLookupMilliseconds, execute.swapchainAcquireMilliseconds,
            execute.deferredPrepareMilliseconds, execute.taskPlanLaunchMilliseconds,
            execute.primaryRecordBeforeCollectMilliseconds, execute.recordCompletionWaitMilliseconds,
            execute.primaryReplayBarrierTimestampMilliseconds, execute.primaryEndAndSubmitBuildMilliseconds,
            execute.queueSubmitMilliseconds, execute.initialReleaseRecordSubmitMilliseconds,
            execute.syntheticPresentRecordSubmitMilliseconds, execute.finalizationMilliseconds,
            frame.executeAccountedMainThreadMilliseconds, frame.executeUnclassifiedMilliseconds,
            execute.compiledSubmitBatchCount, execute.acquireBatchCount, execute.recordTaskCount,
            execute.replayedSecondaryCommandBufferCount, execute.queueSubmitCount);
        auto const nodeOffset = frameIndex * benchmarkNodeNames_.size();
        std::ranges::for_each(
            std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) {
                frames << std::format(",{:.9f}", benchmarkNodeBuildMilliseconds_[nodeOffset + nodeIndex]);
            });
        const auto &as = benchmarkAsTelemetry_[frameIndex];
        frames << std::format(",{},{},{:.9f},{:.9f},{:.9f},{:.9f},{:.9f},{},{},{}\n", as.recorded, as.available,
                              as.cacheScanMilliseconds, as.metadataPlanMilliseconds, as.cpuWritesMilliseconds,
                              as.tlasSizingMilliseconds, as.graphDeclareMilliseconds, as.packetCount, as.instanceCount,
                              as.dirtyBlasCount);
    });
    gpu << "frame_ordinal,pass_index,pass_name,queue,batch_index,is_copy_pass,milliseconds,expected_passes,available_"
           "passes,complete\n";
    std::ranges::for_each(benchmarkGpuPasses_, [&](const RendererBenchmarkGpuPass &pass) {
        auto const &name = pass.debugName;
        auto status = std::ranges::lower_bound(benchmarkGpuFrameStatuses_, pass.frameOrdinal, {},
                                               &RendererBenchmarkGpuFrameStatus::frameOrdinal);
        nrAssert(status != benchmarkGpuFrameStatuses_.end() && status->frameOrdinal == pass.frameOrdinal,
                 "Benchmark GPU pass must have a frame status.");
        gpu << std::format("{},{},{},{},{},{},{:.9f},{},{},{}\n", pass.frameOrdinal, pass.passIndex, csv(name),
                           static_cast<std::uint32_t>(pass.queue), pass.batchIndex, pass.isCopyPass, pass.milliseconds,
                           status->expectedPassCount, status->availablePassCount, status->complete);
    });
    auto const statisticsObject = [](std::vector<double> values) {
        auto const distribution = makeRendererBenchmarkDistribution(std::move(values));
        return Json{JsonObject{
            {"count", Json{static_cast<std::uint64_t>(distribution.count)}},
            {"min", Json{distribution.minimum}},
            {"p50", Json{distribution.p50}},
            {"p95", Json{distribution.p95}},
            {"p99", Json{distribution.p99}},
            {"max", Json{distribution.maximum}},
            {"mean", Json{distribution.mean}},
            {"stddev", Json{distribution.populationStddev}},
        }};
    };
    auto frameStatistics = [&](auto accessor) {
        return statisticsObject(benchmarkFrames_ | std::views::transform(accessor) |
                                std::ranges::to<std::vector<double>>());
    };
    auto asStatistics = [&](auto accessor) {
        return statisticsObject(benchmarkAsTelemetry_ | std::views::transform(accessor) |
                                std::ranges::to<std::vector<double>>());
    };
    auto cpuStages = JsonObject{
        {"wait_gpu_ms", frameStatistics([](const auto &frame) { return frame.cpu.cpuWaitGpuMilliseconds; })},
        {"frame_setup_ms", frameStatistics([](const auto &frame) { return frame.cpu.frameSetupMilliseconds; })},
        {"scene_ms", frameStatistics([](const auto &frame) { return frame.cpu.sceneMilliseconds; })},
        {"post_scene_ms", frameStatistics([](const auto &frame) { return frame.cpu.postSceneMilliseconds; })},
        {"build_ms", frameStatistics([](const auto &frame) { return frame.cpu.buildMilliseconds; })},
        {"compile_ms", frameStatistics([](const auto &frame) { return frame.cpu.compileMilliseconds; })},
        {"prepare_ms", frameStatistics([](const auto &frame) { return frame.cpu.prepareMilliseconds; })},
        {"execute_ms", frameStatistics([](const auto &frame) { return frame.cpu.executeMilliseconds; })},
        {"present_ms", frameStatistics([](const auto &frame) { return frame.cpu.presentMilliseconds; })},
        {"total_ms", frameStatistics([](const auto &frame) { return frame.cpu.totalMilliseconds; })},
        {"cpu_work_ms", frameStatistics([](const auto &frame) {
             return frame.cpu.totalMilliseconds - frame.cpu.cpuWaitGpuMilliseconds;
         })},
        {"classified_ms",
         frameStatistics([](const auto &frame) { return rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu); })},
        {"unclassified_ms", frameStatistics([](const auto &frame) {
             return frame.cpu.totalMilliseconds - rendererBenchmarkClassifiedCpuMilliseconds(frame.cpu);
         })},
    };
    auto cpuSubstages = JsonObject{
        {"scene_begin_upload_ms",
         frameStatistics([](const auto &frame) { return frame.sceneBeginUploadMilliseconds; })},
        {"scene_raster_extract_ms",
         frameStatistics([](const auto &frame) { return frame.sceneRasterExtractMilliseconds; })},
        {"scene_tlas_extract_ms",
         frameStatistics([](const auto &frame) { return frame.sceneTlasExtractMilliseconds; })},
        {"scene_bridge_ms", frameStatistics([](const auto &frame) { return frame.sceneBridgeMilliseconds; })},
        {"tlas_texture_collection_ms",
         frameStatistics([](const auto &frame) { return frame.tlasTextureCollectionMilliseconds; })},
        {"graph_prelude_ms", frameStatistics([](const auto &frame) { return frame.graphPreludeMilliseconds; })},
        {"ui_collect_ms", frameStatistics([](const auto &frame) { return frame.uiCollectMilliseconds; })},
        {"node_loop_ms", frameStatistics([](const auto &frame) { return frame.nodeLoopMilliseconds; })},
        {"skeleton_patch_ms", frameStatistics([](const auto &frame) { return frame.skeletonPatchMilliseconds; })},
        {"skeleton_rebuild_ms", frameStatistics([](const auto &frame) { return frame.skeletonRebuildMilliseconds; })},
    };
    auto executeSubstages = JsonObject{
        {"executor_setup_ms",
         frameStatistics([](const auto &frame) { return frame.execute.executorSetupMilliseconds; })},
        {"completed_gpu_timing_readback_ms",
         frameStatistics([](const auto &frame) { return frame.execute.completedGpuTimingReadbackMilliseconds; })},
        {"timing_setup_ms", frameStatistics([](const auto &frame) { return frame.execute.timingSetupMilliseconds; })},
        {"per_frame_lookup_ms",
         frameStatistics([](const auto &frame) { return frame.execute.perFrameLookupMilliseconds; })},
        {"swapchain_acquire_ms",
         frameStatistics([](const auto &frame) { return frame.execute.swapchainAcquireMilliseconds; })},
        {"deferred_prepare_ms",
         frameStatistics([](const auto &frame) { return frame.execute.deferredPrepareMilliseconds; })},
        {"task_plan_launch_ms",
         frameStatistics([](const auto &frame) { return frame.execute.taskPlanLaunchMilliseconds; })},
        {"primary_record_before_collect_ms",
         frameStatistics([](const auto &frame) { return frame.execute.primaryRecordBeforeCollectMilliseconds; })},
        {"record_completion_wait_ms",
         frameStatistics([](const auto &frame) { return frame.execute.recordCompletionWaitMilliseconds; })},
        {"primary_replay_barrier_timestamp_ms",
         frameStatistics([](const auto &frame) { return frame.execute.primaryReplayBarrierTimestampMilliseconds; })},
        {"primary_end_and_submit_build_ms",
         frameStatistics([](const auto &frame) { return frame.execute.primaryEndAndSubmitBuildMilliseconds; })},
        {"queue_submit_ms", frameStatistics([](const auto &frame) { return frame.execute.queueSubmitMilliseconds; })},
        {"initial_release_record_submit_ms",
         frameStatistics([](const auto &frame) { return frame.execute.initialReleaseRecordSubmitMilliseconds; })},
        {"synthetic_present_record_submit_ms",
         frameStatistics([](const auto &frame) { return frame.execute.syntheticPresentRecordSubmitMilliseconds; })},
        {"finalization_ms", frameStatistics([](const auto &frame) { return frame.execute.finalizationMilliseconds; })},
        {"accounted_main_thread_ms",
         frameStatistics([](const auto &frame) { return frame.executeAccountedMainThreadMilliseconds; })},
        {"unclassified_ms", frameStatistics([](const auto &frame) { return frame.executeUnclassifiedMilliseconds; })},
    };
    auto executeCounts = JsonObject{
        {"compiled_submit_batches", frameStatistics([](const auto &frame) {
             return static_cast<double>(frame.execute.compiledSubmitBatchCount);
         })},
        {"acquire_batches",
         frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.acquireBatchCount); })},
        {"record_tasks",
         frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.recordTaskCount); })},
        {"replayed_secondary_command_buffers", frameStatistics([](const auto &frame) {
             return static_cast<double>(frame.execute.replayedSecondaryCommandBufferCount);
         })},
        {"queue_submits",
         frameStatistics([](const auto &frame) { return static_cast<double>(frame.execute.queueSubmitCount); })},
    };
    auto asTimings = JsonObject{
        {"cache_scan_ms", asStatistics([](const auto &telemetry) { return telemetry.cacheScanMilliseconds; })},
        {"metadata_plan_ms", asStatistics([](const auto &telemetry) { return telemetry.metadataPlanMilliseconds; })},
        {"cpu_writes_ms", asStatistics([](const auto &telemetry) { return telemetry.cpuWritesMilliseconds; })},
        {"tlas_sizing_ms", asStatistics([](const auto &telemetry) { return telemetry.tlasSizingMilliseconds; })},
        {"graph_declare_ms", asStatistics([](const auto &telemetry) { return telemetry.graphDeclareMilliseconds; })},
    };
    auto asCounts = JsonObject{
        {"packets", asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.packetCount); })},
        {"instances", asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.instanceCount); })},
        {"dirty_blas",
         asStatistics([](const auto &telemetry) { return static_cast<double>(telemetry.dirtyBlasCount); })},
    };
    auto nodeSummary = JsonObject{};
    std::ranges::for_each(std::views::iota(std::size_t{0u}, benchmarkNodeNames_.size()), [&](std::size_t nodeIndex) {
        auto values = std::views::iota(std::size_t{0u}, benchmarkFrames_.size()) |
                      std::views::transform([&](std::size_t frameIndex) {
                          return benchmarkNodeBuildMilliseconds_[frameIndex * benchmarkNodeNames_.size() + nodeIndex];
                      }) |
                      std::ranges::to<std::vector<double>>();
        nodeSummary.emplace(std::format("node_build_{}_ms", nodeIndex), statisticsObject(std::move(values)));
    });
    auto skeletonMissReasonCounts = std::array<std::size_t, renderGraphSkeletonMissReasons.size()>{};
    std::ranges::for_each(benchmarkFrames_, [&](const RendererBenchmarkFrame &frame) {
        auto const reason = std::ranges::find(renderGraphSkeletonMissReasons, frame.skeletonMissReason);
        if (reason != renderGraphSkeletonMissReasons.end())
        {
            ++skeletonMissReasonCounts[static_cast<std::size_t>(
                std::ranges::distance(renderGraphSkeletonMissReasons.begin(), reason))];
        }
    });
    auto skeletonMissReasonSummary = JsonObject{};
    std::ranges::for_each(std::views::iota(std::size_t{0u}, renderGraphSkeletonMissReasons.size()),
                          [&](std::size_t reasonIndex) {
                              skeletonMissReasonSummary.emplace(
                                  renderGraphSkeletonMissReasonName(renderGraphSkeletonMissReasons[reasonIndex]),
                                  Json{static_cast<std::uint64_t>(skeletonMissReasonCounts[reasonIndex])});
                          });
    auto const skeletonFrameHits =
        static_cast<std::size_t>(std::ranges::count(benchmarkFrames_, true, &RendererBenchmarkFrame::skeletonHit));
    auto const skeletonDisabledFrames = static_cast<std::size_t>(std::ranges::count(
        benchmarkFrames_, RenderGraphSkeletonMissReason::Disabled, &RendererBenchmarkFrame::skeletonMissReason));
    auto const skeletonEnabledMisses =
        static_cast<std::size_t>(std::ranges::count_if(benchmarkFrames_, [](const RendererBenchmarkFrame &frame) {
            return !frame.skeletonHit && frame.skeletonMissReason != RenderGraphSkeletonMissReason::Disabled;
        }));
    auto const skeletonStatisticsAfter = renderGraphSkeletonStatistics();
    auto const counterDelta = [](std::uint64_t after, std::uint64_t before) {
        return after >= before ? after - before : std::uint64_t{0u};
    };
    auto skeletonSummary = JsonObject{
        {"mode", Json{renderGraphSkeletonModeName(benchmarkConfig_.renderGraphSkeletonMode)}},
        {"measurement_frames", Json{static_cast<std::uint64_t>(benchmarkFrames_.size())}},
        {"frame_hits", Json{static_cast<std::uint64_t>(skeletonFrameHits)}},
        {"enabled_misses", Json{static_cast<std::uint64_t>(skeletonEnabledMisses)}},
        {"disabled_frames", Json{static_cast<std::uint64_t>(skeletonDisabledFrames)}},
        {"patch_failures",
         Json{static_cast<std::uint64_t>(
             skeletonMissReasonCounts[static_cast<std::size_t>(RenderGraphSkeletonMissReason::PatchFailed)])}},
        {"structural_mismatches",
         Json{static_cast<std::uint64_t>(
             skeletonMissReasonCounts[static_cast<std::size_t>(RenderGraphSkeletonMissReason::StructureMismatch)])}},
        {"run_cache_hit_delta",
         Json{counterDelta(skeletonStatisticsAfter.hitCount, benchmarkSkeletonStatisticsBefore_.hitCount)}},
        {"run_cache_miss_delta",
         Json{counterDelta(skeletonStatisticsAfter.missCount, benchmarkSkeletonStatisticsBefore_.missCount)}},
        {"run_invalidation_delta", Json{counterDelta(skeletonStatisticsAfter.invalidationCount,
                                                     benchmarkSkeletonStatisticsBefore_.invalidationCount)}},
        {"run_structure_mismatch_delta", Json{counterDelta(skeletonStatisticsAfter.structureMismatchCount,
                                                           benchmarkSkeletonStatisticsBefore_.structureMismatchCount)}},
        {"entries", Json{static_cast<std::uint64_t>(skeletonStatisticsAfter.entryCount)}},
        {"miss_reasons", Json{std::move(skeletonMissReasonSummary)}},
    };
    auto gpuPassSummary = JsonObject{};
    if (!benchmarkFrames_.empty())
    {
        auto baseline = std::map<std::uint32_t, RendererBenchmarkGpuPass>{};
        std::ranges::for_each(benchmarkGpuPasses_, [&](const RendererBenchmarkGpuPass &pass) {
            if (pass.frameOrdinal == benchmarkFrames_.front().frameOrdinal)
            {
                baseline.emplace(pass.passIndex, pass);
            }
        });
        std::ranges::for_each(baseline, [&](const auto &entry) {
            auto const &pass = entry.second;
            auto values = benchmarkGpuPasses_ | std::views::filter([&](const RendererBenchmarkGpuPass &candidate) {
                              return candidate.passIndex == pass.passIndex;
                          }) |
                          std::views::transform(
                              [](const RendererBenchmarkGpuPass &candidate) { return candidate.milliseconds; }) |
                          std::ranges::to<std::vector<double>>();
            gpuPassSummary.emplace(std::format("pass_{}", pass.passIndex),
                                   Json{JsonObject{
                                       {"pass_index", Json{static_cast<std::uint64_t>(pass.passIndex)}},
                                       {"name", Json{pass.debugName}},
                                       {"queue", Json{static_cast<std::uint64_t>(pass.queue)}},
                                       {"batch_index", Json{static_cast<std::uint64_t>(pass.batchIndex)}},
                                       {"copy_to_swapchain", Json{pass.isCopyPass}},
                                       {"statistics", statisticsObject(std::move(values))},
                                   }});
        });
    }
    auto summaryDocument = Json{JsonObject{
        {"schema", Json{rendererBenchmarkSchemaVersion()}},
        {"run_status", Json{runStatus}},
        {"data_quality",
         Json{JsonObject{
             {"frames_valid", Json{audit.framesValid}},
             {"node_telemetry_valid", Json{audit.nodeTelemetryValid}},
             {"as_telemetry_valid", Json{audit.accelerationStructureTelemetryValid}},
             {"accepted_matches_requested", Json{accepted == requested}},
             {"missing_gpu_frames", Json{static_cast<std::uint64_t>(audit.missingGpuFrames)}},
             {"partial_gpu_frames", Json{static_cast<std::uint64_t>(audit.partialGpuFrames)}},
             {"extra_gpu_statuses", Json{static_cast<std::uint64_t>(audit.extraGpuStatuses)}},
             {"duplicate_gpu_statuses", Json{static_cast<std::uint64_t>(audit.duplicateGpuStatuses)}},
             {"invalid_gpu_durations", Json{static_cast<std::uint64_t>(audit.invalidGpuDurations)}},
             {"duplicate_gpu_passes", Json{static_cast<std::uint64_t>(audit.duplicateGpuPasses)}},
             {"schema_drift_frames", Json{static_cast<std::uint64_t>(audit.schemaDriftFrames)}},
             {"pass_row_count_mismatch_frames", Json{static_cast<std::uint64_t>(audit.passRowCountMismatchFrames)}},
             {"extra_gpu_pass_frames", Json{static_cast<std::uint64_t>(audit.extraGpuPassFrames)}},
         }}},
        {"cpu_stages", Json{std::move(cpuStages)}},
        {"cpu_substages", Json{std::move(cpuSubstages)}},
        {"render_graph_skeleton", Json{std::move(skeletonSummary)}},
        {"execute_substages", Json{std::move(executeSubstages)}},
        {"execute_counts", Json{std::move(executeCounts)}},
        {"as_timings", Json{std::move(asTimings)}},
        {"as_counts", Json{std::move(asCounts)}},
        {"node_build", Json{std::move(nodeSummary)}},
        {"gpu_passes", Json{std::move(gpuPassSummary)}},
    }};
    auto summaryText = std::string{};
    if (!serializeBenchmarkArtifact("summary.json", summaryDocument, summaryText))
    {
        return false;
    }
    summary << summaryText;
    metadata.flush();
    frames.flush();
    gpu.flush();
    summary.flush();
    auto const streamsGood = static_cast<bool>(metadata) && static_cast<bool>(frames) && static_cast<bool>(gpu) &&
                             static_cast<bool>(summary);
    benchmarkSucceeded_ = streamsGood && dataValid && accepted == requested;
    benchmarkFinalized_ = streamsGood;
    return benchmarkSucceeded_;
}
} // namespace nr::renderer
