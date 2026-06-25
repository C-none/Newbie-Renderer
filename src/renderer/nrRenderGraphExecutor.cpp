module nr.renderer;
import :renderGraphExecutor;
import dependency.vulkan;
import nr.rhi;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;
import :rendererSubmission;

namespace nr::renderer::detail
{
inline constexpr std::uint32_t kExecuteCpuTimingLogFrameInterval = 2000;

[[nodiscard]] double elapsedMilliseconds(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

[[nodiscard]] std::uint32_t secondaryPoolSlotForRecordWorker(std::uint32_t recordWorkerId) noexcept
{
    return recordWorkerId + kWorkerSecondaryPoolSlotBase;
}

[[nodiscard]] std::string_view queueDomainLabel(QueueDomain queue) noexcept
{
    if (queue == QueueDomain::Graphics)
    {
        return "Graphics";
    }
    if (queue == QueueDomain::Compute)
    {
        return "Compute";
    }
    return "Transfer";
}

[[nodiscard]] std::string nodeScopeLabel(std::string_view passDebugName)
{
    // Pass labels follow "Node.Pass". If a node prefix is unavailable,
    // keep the scope at the renderer level instead of inventing new ownership metadata.
    auto const separator = passDebugName.find('.');
    if (separator == std::string_view::npos || separator == 0)
    {
        return "Renderer";
    }

    return std::string{passDebugName.substr(0, separator)};
}

[[nodiscard]] std::string rendererBatchScopeLabel(
    std::uint32_t batchIndex,
    QueueDomain queue,
    std::string_view openedBySubmitNodeDebugName)
{
    if (!openedBySubmitNodeDebugName.empty())
    {
        return std::format(
            "Renderer.Submit.{}.{}.{}",
            openedBySubmitNodeDebugName,
            queueDomainLabel(queue),
            batchIndex);
    }

    return std::format("Renderer.Batch.{}.{}", queueDomainLabel(queue), batchIndex);
}

RenderRecordThreadPool::~RenderRecordThreadPool()
{
        stop();
    }

void RenderRecordThreadPool::ensureWorkerCount(std::uint32_t workerCount)
{
        auto const targetWorkerCount = std::min<std::uint32_t>(std::max(workerCount, 1u), nr::maxThreads);
        auto const currentWorkerCount = static_cast<std::uint32_t>(workers_.size());
        if (targetWorkerCount <= currentWorkerCount)
        {
            return;
        }

        auto workerIds = std::views::iota(currentWorkerCount, targetWorkerCount);
        std::ranges::for_each(workerIds, [&](std::uint32_t workerId) {
            workers_.emplace_back([this, workerId](std::stop_token stopToken) {
                workerLoop(workerId, stopToken);
            });
        });
    }

[[nodiscard]] std::uint32_t RenderRecordThreadPool::workerCount() const noexcept
{
        return static_cast<std::uint32_t>(workers_.size());
    }

void RenderRecordThreadPool::stop()
{
        if (stopping_.exchange(true))
        {
            return;
        }

        std::ranges::for_each(workerQueues_, [](WorkerQueue& queue) {
            queue.wake.notify_all();
        });
        workers_.clear();
    }

void RenderRecordThreadPool::workerLoop(std::uint32_t workerId, const std::stop_token& stopToken)
{
        auto& worker = workerQueues_[workerId];
        while (true)
        {
            auto task = std::packaged_task<RecordTaskResult()>{};
            {
                std::unique_lock lock(worker.mutex);
                worker.wake.wait(lock, [&]() {
                    return stopping_.load() || stopToken.stop_requested() || !worker.tasks.empty();
                });

                if ((stopping_.load() || stopToken.stop_requested()) && worker.tasks.empty())
                {
                    return;
                }

                task = std::move(worker.tasks.front());
                worker.tasks.pop();
            }

            task();
        }
    }
} // namespace nr::renderer::detail

namespace nr::renderer
{
[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassRecordContext& recordContext)
{
    return [&recordContext](
               const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
               const nr::rhi::DescriptorBindingInfo& binding,
               std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, recordContext);
    };
}

[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassPrepareContext& prepareContext)
{
    return [&prepareContext](
               const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
               const nr::rhi::DescriptorBindingInfo& binding,
               std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, prepareContext);
    };
}

[[nodiscard]] ExecutorPlan RenderGraphExecutor::buildPlan(const CompiledGraphFrame& compiled) const
{
        auto plan = ExecutorPlan{};
        if (compiled.submitBatches.empty())
        {
            plan.finalQueueIsCompute = true;
            plan.requiresSyntheticPresentBatch = true;
            return plan;
        }

        struct LastResourceUse
        {
            std::uint32_t batchIndex = 0;
            QueueDomain queue = QueueDomain::Graphics;
        };

        auto releaseByBatch = std::map<std::uint32_t, std::vector<ResourceStateTransition>>{};
        auto acquireByBatch = std::map<std::uint32_t, std::vector<ResourceStateTransition>>{};
        auto dedupe = std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t>>{};
        auto lastUse = std::map<GraphResourceHandle, LastResourceUse>{};

        std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch& batch) {
            std::ranges::for_each(batch.passes, [&](const CompiledPass& pass) {
                std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition& transition) {
                    if (transition.strength != DependencyStrength::ReleaseAcquireRequired)
                    {
                        return;
                    }

                    auto previousUse = lastUse.find(transition.resource);
                    if (previousUse == lastUse.end())
                    {
                        return;
                    }

                    auto srcBatchIndex = previousUse->second.batchIndex;
                    auto dstBatchIndex = pass.submitBatchIndex;

                    auto key = std::tuple{
                        transition.resource.value,
                        static_cast<std::uint32_t>(transition.srcQueue),
                        static_cast<std::uint32_t>(transition.dstQueue),
                        static_cast<std::uint32_t>(transition.oldLayout),
                        static_cast<std::uint32_t>(transition.newLayout),
                        srcBatchIndex,
                        dstBatchIndex,
                    };

                    if (!dedupe.insert(key).second)
                    {
                        return;
                    }

                    releaseByBatch[srcBatchIndex].push_back(transition);
                    acquireByBatch[dstBatchIndex].push_back(transition);
                });

                std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc& use) {
                    lastUse.insert_or_assign(use.resource, LastResourceUse{
                        .batchIndex = pass.submitBatchIndex,
                        .queue = pass.queue,
                    });
                });
            });
        });

        auto batchIndices = std::views::iota(std::size_t{0}, compiled.submitBatches.size());
        std::ranges::for_each(batchIndices, [&](std::size_t batchOrdinal) {
            const auto& batch = compiled.submitBatches[batchOrdinal];
            auto inPassBarrierCount = std::size_t{0};

            std::ranges::for_each(batch.passes, [&](const CompiledPass& pass) {
                inPassBarrierCount += static_cast<std::size_t>(
                    std::ranges::count_if(pass.preBarriers, [](const ResourceStateTransition& transition) {
                        return transition.strength == DependencyStrength::BarrierRequired;
                    }));
            });

            auto waitsForPreviousBatch = batchOrdinal > 0;
            auto isLastBatch = batchOrdinal + 1 == compiled.submitBatches.size();
            auto signalsPresent = isLastBatch && batch.queue == QueueDomain::Compute;
            auto signalsNextBatch = !signalsPresent;

            plan.totalPassCount += batch.passes.size();
            plan.totalInPassBarrierCount += inPassBarrierCount;

            plan.batches.push_back(ExecutorBatchPlan{
                .batchIndex = batch.batchIndex,
                .queue = batch.queue,
                .passCount = batch.passes.size(),
                .inPassBarrierCount = inPassBarrierCount,
                .headAcquireTransitions = acquireByBatch[batch.batchIndex],
                .tailReleaseTransitions = releaseByBatch[batch.batchIndex],
                .waitsForPreviousBatch = waitsForPreviousBatch,
                .signalsNextBatch = signalsNextBatch,
                .signalsPresent = signalsPresent,
            });
        });

        plan.finalQueueIsCompute = compiled.submitBatches.back().queue == QueueDomain::Compute;
        plan.requiresSyntheticPresentBatch = !plan.finalQueueIsCompute;
        return plan;
    }

[[nodiscard]] PreparedGraphFrame RenderGraphExecutor::prepareFrame(const CompiledGraphFrame& compiled, const ExecuteContext& context) const
{
        return prepareFrame(CompiledGraphFrame{compiled}, context);
    }

[[nodiscard]] PreparedGraphFrame RenderGraphExecutor::prepareFrame(CompiledGraphFrame&& compiled, const ExecuteContext& context) const
{
        auto prepared = PreparedGraphFrame{};
        prepared.plan = buildPlan(compiled);
        prepared.runtimeBindings = resolveRuntimeResources(compiled, context);
        prepared.invokedPassPrepareCount = invokePassPrepareCallbacks(compiled, context, prepared.runtimeBindings);
        prepared.compiled = std::move(compiled);
        return prepared;
    }

[[nodiscard]] ExecuteReport RenderGraphExecutor::execute(const CompiledGraphFrame& compiled, const ExecuteContext& context)
{
        auto prepared = prepareFrame(compiled, context);
        return executePrepared(prepared, context);
    }

[[nodiscard]] ExecuteReport RenderGraphExecutor::executePrepared(const PreparedGraphFrame& prepared, const ExecuteContext& context)
{
        auto executeCpuTimings = ExecuteCpuTimings{};
        auto const executeCpuStart = std::chrono::steady_clock::now();
        auto phaseStart = executeCpuStart;
        auto recordPhase = [&]() {
            auto const phaseEnd = std::chrono::steady_clock::now();
            auto const elapsed = detail::elapsedMilliseconds(phaseStart, phaseEnd);
            phaseStart = phaseEnd;
            return elapsed;
        };

        auto report = ExecuteReport{};
        report.plan = prepared.plan;
        report.invokedPassPrepareCount = prepared.invokedPassPrepareCount;

        auto const& compiled = prepared.compiled;
        auto const& runtimeBindings = prepared.runtimeBindings;
        auto const frameBoundaryFrameID = context.device.frameBoundaryEnabled() ? nextFrameBoundaryId_++ : std::uint64_t{0};

        auto frameCount = context.device.frameManager.frameCount();
        nrAssert(frameCount > 0, "RenderGraphExecutor::execute requires at least one frame context.");

        if (primaryCommandBuffersByFrame_.size() != frameCount)
        {
            primaryCommandBuffersByFrame_.clear();
            primaryCommandBuffersByFrame_.resize(frameCount);
        }

        if (secondaryCommandBuffersByFrame_.size() != frameCount)
        {
            secondaryCommandBuffersByFrame_.clear();
            secondaryCommandBuffersByFrame_.resize(frameCount);
        }

        if (gpuPassTimingStatesByFrame_.size() != frameCount)
        {
            gpuPassTimingStatesByFrame_.clear();
            gpuPassTimingStatesByFrame_.resize(frameCount);
        }

        auto frameSlot = static_cast<std::size_t>(context.frameIndex % static_cast<std::uint32_t>(frameCount));
        executeCpuTimings.retainedStateMilliseconds += recordPhase();

        auto& frameTimingState = gpuPassTimingStatesByFrame_[frameSlot];
        report.completedGpuPassTimingFrame = collectCompletedGpuPassTimings(context.device, frameTimingState);

        auto currentTimingSamples = buildPassTimingSamples(compiled);
        if (!currentTimingSamples.empty())
        {
            static_cast<void>(timestampValidBitsForQueues(
                context.device,
                std::span<const GpuPassTimingSample>{currentTimingSamples.data(), currentTimingSamples.size()}));
        }
        ensureTimingQueryPool(
            context.device,
            frameTimingState,
            timingQueryCountForPassCount(currentTimingSamples.size()));
        executeCpuTimings.gpuTimingSetupMilliseconds += recordPhase();

        auto desiredWorkerCount = resolvedRecordWorkerCount(context.device.frameManager.current());
        recordThreadPool_.ensureWorkerCount(desiredWorkerCount);
        report.recordWorkerCount = recordThreadPool_.workerCount();
        executeCpuTimings.workerSetupMilliseconds += recordPhase();

        auto compiledResourceByHandle = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>{};
        std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc& resource) {
            compiledResourceByHandle.emplace(resource.handle, std::cref(resource));
        });

        auto frameDataByHandle = CompiledFrameDataLookup{};
        std::ranges::for_each(compiled.frameData, [&](const GraphFrameDataDesc& frameData) {
            frameDataByHandle.emplace(frameData.handle, std::cref(frameData));
        });

        auto timeline = context.submissionTimeline.has_value()
                            ? std::optional<std::reference_wrapper<RendererSubmissionTimeline>>(context.submissionTimeline->get())
                            : std::nullopt;
        auto timelineValid = timeline.has_value() && timeline->get().valid();

        if (report.plan.batches.size() > 1)
        {
            nrAssert(
                timelineValid,
                "RenderGraphExecutor::execute requires a valid submission timeline when more than one submit batch exists.");
        }

        if (report.plan.requiresSyntheticPresentBatch && !report.plan.batches.empty())
        {
            nrAssert(
                timelineValid,
                "RenderGraphExecutor::execute requires a valid submission timeline when inserting a synthetic compute-final present batch.");
        }
        executeCpuTimings.lookupSetupMilliseconds += recordPhase();

        auto const launchRecordTasksStart = std::chrono::steady_clock::now();
        auto recordTasksByBatch = launchRecordTasksForAllBatches(
            context,
            frameSlot,
            compiled,
            compiledResourceByHandle,
            runtimeBindings,
            frameDataByHandle,
            report);
        executeCpuTimings.launchRecordTasksMilliseconds += detail::elapsedMilliseconds(
            launchRecordTasksStart,
            std::chrono::steady_clock::now());
        nrAssert(
            recordTasksByBatch.size() == report.plan.batches.size(),
            "RenderGraphExecutor::execute expected one record task group per submit batch.");

        auto previousSignalToken = RendererSubmitToken{};
        auto timedPassOffset = std::size_t{0};

        auto batchOrdinals = std::views::iota(std::size_t{0}, report.plan.batches.size());
        std::ranges::for_each(batchOrdinals, [&](std::size_t batchOrdinal) {
            const auto& planBatch = report.plan.batches[batchOrdinal];
            const auto& compiledBatch = compiled.submitBatches[batchOrdinal];
            auto const batchTimedPassOffset = timedPassOffset;
            auto const batchTimedPassCount = compiledBatch.passes.size();
            timedPassOffset += batchTimedPassCount;

            auto const primarySetupStart = std::chrono::steady_clock::now();
            auto& commandBuffer = primaryCommandBufferForQueue(context, frameSlot, planBatch.queue, batchOrdinal);

            commandBuffer.reset();
            nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            {
                auto batchDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
                    commandBuffer,
                    detail::rendererBatchScopeLabel(
                        planBatch.batchIndex,
                        planBatch.queue,
                        compiledBatch.openedBySubmitNodeDebugName),
                };
                auto commandBufferHandle = *commandBuffer;

                static auto loggedBatchIndices = std::set<std::uint32_t>{};
                if (loggedBatchIndices.insert(planBatch.batchIndex).second)
                {
                    auto passList = std::string{};
                    std::ranges::for_each(compiledBatch.passes, [&](const CompiledPass& pass) {
                        if (!passList.empty())
                        {
                            passList += ",";
                        }
                        passList += pass.debugName;
                    });

                    auto commandBufferRaw = std::bit_cast<std::uint64_t>(static_cast<VkCommandBuffer>(commandBufferHandle));
                    nrInfo(std::format(
                        "RenderGraphExecutor batch={} queue={} cmd=0x{:x} passes=[{}]",
                        planBatch.batchIndex,
                        detail::queueDomainLabel(planBatch.queue),
                        commandBufferRaw,
                        passList));
                }

                executeCpuTimings.primarySetupMilliseconds += detail::elapsedMilliseconds(
                    primarySetupStart,
                    std::chrono::steady_clock::now());

                auto const queryResetStart = std::chrono::steady_clock::now();
                if (batchTimedPassCount > 0u)
                {
                    commandBuffer.resetQueryPool(
                        *frameTimingState.queryPool,
                        beginTimingQueryForPass(batchTimedPassOffset),
                        timingQueryCountForPassCount(batchTimedPassCount));
                }
                executeCpuTimings.queryResetMilliseconds += detail::elapsedMilliseconds(
                    queryResetStart,
                    std::chrono::steady_clock::now());

                auto const acquireBarrierStart = std::chrono::steady_clock::now();
                if (!planBatch.headAcquireTransitions.empty())
                {
                    auto barriers = nr::rhi::ops::BarrierBatch{};
                    std::ranges::for_each(planBatch.headAcquireTransitions, [&](const ResourceStateTransition& transition) {
                        auto resourceIt = compiledResourceByHandle.find(transition.resource);
                        nrAssert(
                            resourceIt != compiledResourceByHandle.end(),
                            "RenderGraphExecutor::execute acquire transition references an unknown resource handle.");

                        auto bindingIt = runtimeBindings.find(transition.resource);
                        nrAssert(
                            bindingIt != runtimeBindings.end(),
                            "RenderGraphExecutor::execute acquire transition cannot resolve runtime resource binding.");

                        addTransitionBarrier(
                            barriers,
                            resourceIt->second.get(),
                            bindingIt->second,
                            transition,
                            TransitionPlacement::Acquire,
                            queueFamilyIndexFor(context.device, transition.srcQueue),
                            queueFamilyIndexFor(context.device, transition.dstQueue));

                        ++report.appliedAcquireBarrierCount;
                    });

                    if (!barriers.empty())
                    {
                        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
                    }
                }
                executeCpuTimings.acquireBarrierMilliseconds += detail::elapsedMilliseconds(
                    acquireBarrierStart,
                    std::chrono::steady_clock::now());

                auto const collectRecordTasksStart = std::chrono::steady_clock::now();
                auto recordResults = collectRecordTaskResults(
                    recordTasksByBatch[batchOrdinal],
                    batchOrdinal,
                    report);
                executeCpuTimings.collectRecordTasksMilliseconds += detail::elapsedMilliseconds(
                    collectRecordTasksStart,
                    std::chrono::steady_clock::now());

                auto const executeSecondariesStart = std::chrono::steady_clock::now();
                executeRecordedSecondaries(
                    commandBuffer,
                    std::span<const RecordTaskResult>{recordResults.data(), recordResults.size()},
                    *frameTimingState.queryPool,
                    batchTimedPassOffset);
                executeCpuTimings.executeSecondariesMilliseconds += detail::elapsedMilliseconds(
                    executeSecondariesStart,
                    std::chrono::steady_clock::now());

                auto const releaseBarrierStart = std::chrono::steady_clock::now();
                if (!planBatch.tailReleaseTransitions.empty())
                {
                    auto barriers = nr::rhi::ops::BarrierBatch{};
                    std::ranges::for_each(planBatch.tailReleaseTransitions, [&](const ResourceStateTransition& transition) {
                        auto resourceIt = compiledResourceByHandle.find(transition.resource);
                        nrAssert(
                            resourceIt != compiledResourceByHandle.end(),
                            "RenderGraphExecutor::execute release transition references an unknown resource handle.");

                        auto bindingIt = runtimeBindings.find(transition.resource);
                        nrAssert(
                            bindingIt != runtimeBindings.end(),
                            "RenderGraphExecutor::execute release transition cannot resolve runtime resource binding.");

                        addTransitionBarrier(
                            barriers,
                            resourceIt->second.get(),
                            bindingIt->second,
                            transition,
                            TransitionPlacement::Release,
                            queueFamilyIndexFor(context.device, transition.srcQueue),
                            queueFamilyIndexFor(context.device, transition.dstQueue));

                        ++report.appliedReleaseBarrierCount;
                    });

                    if (!barriers.empty())
                    {
                        nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
                    }
                }
                executeCpuTimings.releaseBarrierMilliseconds += detail::elapsedMilliseconds(
                    releaseBarrierStart,
                    std::chrono::steady_clock::now());
            }

            auto const primaryEndStart = std::chrono::steady_clock::now();
            nr::rhi::CommandRecorder::end(commandBuffer);
            executeCpuTimings.primaryEndMilliseconds += detail::elapsedMilliseconds(
                primaryEndStart,
                std::chrono::steady_clock::now());

            auto const submitStart = std::chrono::steady_clock::now();
            auto submitBatch = nr::rhi::CommandBatch{};
            submitBatch.addCommandBuffer(commandBuffer);

            if (timelineValid && planBatch.waitsForPreviousBatch && previousSignalToken.valid())
            {
                submitBatch.addWait(
                    timeline->get().semaphore(),
                    submissionWaitStage(planBatch.queue),
                    previousSignalToken.value);
            }

            if (timelineValid && planBatch.signalsNextBatch)
            {
                auto signalToken = timeline->get().acquireSignalToken();
                submitBatch.addSignal(
                    timeline->get().semaphore(),
                    signalToken.value,
                    0,
                    vk::PipelineStageFlagBits2::eAllCommands);
                previousSignalToken = signalToken;
            }

            auto submitRole = toQueueRole(planBatch.queue);
            auto imageAvailableWaitStage = planBatch.signalsPresent
                                               ? imageAvailableWaitStageForBatch(compiledBatch, compiledResourceByHandle)
                                               : vk::PipelineStageFlags2{};
            attachFrameBoundaryMetadata(submitBatch, context, frameBoundaryFrameID, planBatch.signalsPresent);
            context.device.submitFrameBatch(std::move(submitBatch), submitRole, planBatch.signalsPresent, imageAvailableWaitStage);
            ++report.submittedBatchCount;
            executeCpuTimings.submitMilliseconds += detail::elapsedMilliseconds(
                submitStart,
                std::chrono::steady_clock::now());
        });

        if (report.plan.requiresSyntheticPresentBatch)
        {
            auto const syntheticPresentStart = std::chrono::steady_clock::now();
            auto& commandBuffer = primaryCommandBufferForQueue(
                context,
                frameSlot,
                QueueDomain::Compute,
                report.plan.batches.size());

            commandBuffer.reset();
            nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            nr::rhi::CommandRecorder::end(commandBuffer);

            auto submitBatch = nr::rhi::CommandBatch{};
            submitBatch.addCommandBuffer(commandBuffer);

            if (timelineValid && previousSignalToken.valid())
            {
                submitBatch.addWait(
                    timeline->get().semaphore(),
                    submissionWaitStage(QueueDomain::Compute),
                    previousSignalToken.value);
            }

            attachFrameBoundaryMetadata(submitBatch, context, frameBoundaryFrameID, true);
            context.device.submitFrameBatch(std::move(submitBatch), nr::rhi::QueueRole::Compute, true);
            ++report.submittedBatchCount;
            executeCpuTimings.syntheticPresentMilliseconds += detail::elapsedMilliseconds(
                syntheticPresentStart,
                std::chrono::steady_clock::now());
        }

        frameTimingState.pendingFrameIndex = context.frameIndex;
        frameTimingState.pendingPasses = std::move(currentTimingSamples);
        executeCpuTimings.totalMilliseconds = detail::elapsedMilliseconds(
            executeCpuStart,
            std::chrono::steady_clock::now());
        executeCpuTimings.submittedBatchCount = report.submittedBatchCount;
        executeCpuTimings.submittedRecordTaskCount = report.submittedRecordTaskCount;
        recordExecuteCpuTimingSample(executeCpuTimings, context.frameIndex);

        return report;
    }

void RenderGraphExecutor::clearRetainedState()
{
        primaryCommandBuffersByFrame_.clear();
        secondaryCommandBuffersByFrame_.clear();
        gpuPassTimingStatesByFrame_.clear();
        executeCpuTimingAccumulator_ = {};
    }

void RenderGraphExecutor::recordExecuteCpuTimingSample(
        const RenderGraphExecutor::ExecuteCpuTimings& timings,
        std::uint32_t frameIndex)
{
        auto& total = executeCpuTimingAccumulator_.total;
        total.retainedStateMilliseconds += timings.retainedStateMilliseconds;
        total.gpuTimingSetupMilliseconds += timings.gpuTimingSetupMilliseconds;
        total.workerSetupMilliseconds += timings.workerSetupMilliseconds;
        total.lookupSetupMilliseconds += timings.lookupSetupMilliseconds;
        total.launchRecordTasksMilliseconds += timings.launchRecordTasksMilliseconds;
        total.primarySetupMilliseconds += timings.primarySetupMilliseconds;
        total.queryResetMilliseconds += timings.queryResetMilliseconds;
        total.acquireBarrierMilliseconds += timings.acquireBarrierMilliseconds;
        total.collectRecordTasksMilliseconds += timings.collectRecordTasksMilliseconds;
        total.executeSecondariesMilliseconds += timings.executeSecondariesMilliseconds;
        total.releaseBarrierMilliseconds += timings.releaseBarrierMilliseconds;
        total.primaryEndMilliseconds += timings.primaryEndMilliseconds;
        total.submitMilliseconds += timings.submitMilliseconds;
        total.syntheticPresentMilliseconds += timings.syntheticPresentMilliseconds;
        total.totalMilliseconds += timings.totalMilliseconds;
        total.submittedBatchCount += timings.submittedBatchCount;
        total.submittedRecordTaskCount += timings.submittedRecordTaskCount;

        ++executeCpuTimingAccumulator_.sampleCount;
        if (executeCpuTimingAccumulator_.sampleCount < detail::kExecuteCpuTimingLogFrameInterval)
        {
            return;
        }

        auto const divisor = static_cast<double>(executeCpuTimingAccumulator_.sampleCount);
        nrInfo(std::format(
            "RenderGraphExecutor execute CPU average over {} frames ending frameSlot {}: "
            "total={:.3f}ms retainedState={:.3f}ms gpuTimingSetup={:.3f}ms workerSetup={:.3f}ms "
            "lookupSetup={:.3f}ms launchRecordTasks={:.3f}ms primarySetup={:.3f}ms "
            "queryReset={:.3f}ms acquireBarriers={:.3f}ms collectRecordTasks={:.3f}ms "
            "executeSecondaries={:.3f}ms releaseBarriers={:.3f}ms primaryEnd={:.3f}ms "
            "submit={:.3f}ms syntheticPresent={:.3f}ms submittedBatches={:.2f} recordTasks={:.2f}",
            executeCpuTimingAccumulator_.sampleCount,
            frameIndex,
            total.totalMilliseconds / divisor,
            total.retainedStateMilliseconds / divisor,
            total.gpuTimingSetupMilliseconds / divisor,
            total.workerSetupMilliseconds / divisor,
            total.lookupSetupMilliseconds / divisor,
            total.launchRecordTasksMilliseconds / divisor,
            total.primarySetupMilliseconds / divisor,
            total.queryResetMilliseconds / divisor,
            total.acquireBarrierMilliseconds / divisor,
            total.collectRecordTasksMilliseconds / divisor,
            total.executeSecondariesMilliseconds / divisor,
            total.releaseBarrierMilliseconds / divisor,
            total.primaryEndMilliseconds / divisor,
            total.submitMilliseconds / divisor,
            total.syntheticPresentMilliseconds / divisor,
            static_cast<double>(total.submittedBatchCount) / divisor,
            static_cast<double>(total.submittedRecordTaskCount) / divisor));

        executeCpuTimingAccumulator_ = {};
    }

[[nodiscard]] nr::rhi::QueueRole RenderGraphExecutor::toQueueRole(QueueDomain queue)
{
        if (queue == QueueDomain::Graphics)
        {
            return nr::rhi::QueueRole::Graphics;
        }
        if (queue == QueueDomain::Compute)
        {
            return nr::rhi::QueueRole::Compute;
        }
        return nr::rhi::QueueRole::Transfer;
    }

void RenderGraphExecutor::attachFrameBoundaryMetadata(
        nr::rhi::CommandBatch& submitBatch,
        const ExecuteContext& context,
        std::uint64_t frameBoundaryFrameID,
        bool isFrameEnd)
{
        if (!context.device.frameBoundaryEnabled())
        {
            return;
        }

        auto flags = vk::FrameBoundaryFlagsEXT{};
        if (isFrameEnd)
        {
            flags |= vk::FrameBoundaryFlagBitsEXT::eFrameEnd;
        }

        auto swapchainImages = std::array<vk::Image, 1>{};
        auto imageSpan = std::span<const vk::Image>{};
        if (isFrameEnd && context.swapchainImageIndex.has_value())
        {
            swapchainImages[0] = context.device.presentationContext.swapchainImage(*context.swapchainImageIndex);
            imageSpan = std::span<const vk::Image>{swapchainImages.data(), swapchainImages.size()};
        }

        submitBatch.setFrameBoundary(frameBoundaryFrameID, flags, imageSpan);
    }

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::submissionWaitStage(QueueDomain queue)
{
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        }
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eAllCommands;
        }
        return vk::PipelineStageFlagBits2::eTransfer;
    }

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::shaderWaitStageForQueue(QueueDomain queue)
{
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eComputeShader |
                   vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        }
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eFragmentShader;
        }
        return vk::PipelineStageFlagBits2::eTransfer;
    }

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::imageAccessWaitStage(
        QueueDomain queue,
        const PassResourceUseDesc& use)
{
        if (use.imageAccess == ImageAccessIntent::TransferRead ||
            use.imageAccess == ImageAccessIntent::TransferWrite ||
            use.imageUsage == ImageUsageIntent::TransferSrc ||
            use.imageUsage == ImageUsageIntent::TransferDst ||
            use.imageUsage == ImageUsageIntent::CopySource ||
            use.imageUsage == ImageUsageIntent::CopyDestination)
        {
            return vk::PipelineStageFlagBits2::eTransfer;
        }

        if (use.imageAccess == ImageAccessIntent::ColorAttachmentRead ||
            use.imageAccess == ImageAccessIntent::ColorAttachmentWrite ||
            use.imageAccess == ImageAccessIntent::ColorAttachmentReadWrite ||
            use.imageUsage == ImageUsageIntent::ColorAttachment)
        {
            return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        }

        if (use.imageAccess == ImageAccessIntent::DepthStencilRead ||
            use.imageAccess == ImageAccessIntent::DepthStencilWrite ||
            use.imageAccess == ImageAccessIntent::DepthStencilReadWrite ||
            use.imageUsage == ImageUsageIntent::DepthStencilAttachment ||
            use.imageUsage == ImageUsageIntent::DepthStencilReadOnly)
        {
            return vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                   vk::PipelineStageFlagBits2::eLateFragmentTests;
        }

        if (use.imageAccess == ImageAccessIntent::SampledRead ||
            use.imageAccess == ImageAccessIntent::StorageRead ||
            use.imageAccess == ImageAccessIntent::StorageWrite ||
            use.imageAccess == ImageAccessIntent::StorageReadWrite ||
            use.imageAccess == ImageAccessIntent::InputAttachmentRead ||
            use.imageUsage == ImageUsageIntent::Sampled ||
            use.imageUsage == ImageUsageIntent::StorageRead ||
            use.imageUsage == ImageUsageIntent::StorageWrite ||
            use.imageUsage == ImageUsageIntent::StorageReadWrite ||
            use.imageUsage == ImageUsageIntent::InputAttachment)
        {
            return shaderWaitStageForQueue(queue);
        }

        return {};
    }

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::imageAvailableWaitStageForBatch(
        const CompiledSubmitBatch& batch,
        const std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>& compiledResourceByHandle)
{
        auto stages = vk::PipelineStageFlags2{};

        std::ranges::for_each(batch.passes, [&](const CompiledPass& pass) {
            std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc& use) {
                auto resourceIt = compiledResourceByHandle.find(use.resource);
                if (resourceIt == compiledResourceByHandle.end() || !resourceIt->second.get().isSwapchain)
                {
                    return;
                }
                stages |= imageAccessWaitStage(pass.queue, use);
            });
        });

        if (stages == vk::PipelineStageFlags2{})
        {
            return vk::PipelineStageFlagBits2::eAllCommands;
        }
        return stages;
    }

[[nodiscard]] std::uint32_t RenderGraphExecutor::queueFamilyIndexFor(const nr::rhi::Device& device, QueueDomain queue)
{
        if (queue == QueueDomain::Graphics)
        {
            return device.queueManager.graphics().queueFamilyIndex();
        }
        if (queue == QueueDomain::Compute)
        {
            return device.queueManager.compute().queueFamilyIndex();
        }
        return device.queueManager.transfer().queueFamilyIndex();
    }

[[nodiscard]] vk::ImageSubresourceRange RenderGraphExecutor::subresourceRangeFor(const CompiledResourceDesc& resource)
{
        return vk::ImageSubresourceRange{
            RenderGraphCompiler::mapImageAspectIntent(resource.resolvedAspect),
            0,
            1,
            0,
            1,
        };
    }

[[nodiscard]] std::map<GraphResourceHandle, PreparedResourceBinding> RenderGraphExecutor::resolveRuntimeResources(
        const CompiledGraphFrame& compiled,
        const ExecuteContext& context)
{
        auto bindings = std::map<GraphResourceHandle, PreparedResourceBinding>{};

        std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc& resource) {
            auto binding = PreparedResourceBinding{};
            binding.isBuffer = resource.isBuffer;
            binding.isImage = resource.isImage;
            binding.isAccelerationStructure = resource.isAccelerationStructure;
            binding.bufferSize = resource.resolvedBufferSize;
            binding.accelerationStructureType = resource.resolvedAccelerationStructureType;
            binding.accelerationStructureSize = resource.resolvedAccelerationStructureSize;
            binding.extent = resource.resolvedExtent;
            binding.subresourceRange = subresourceRangeFor(resource);

            if (resource.isBuffer)
            {
                auto isTransientManaged = resource.lifetime == ResourceLifetime::GraphTransient &&
                                          resource.residency == ResourceResidency::Managed;
                if (isTransientManaged)
                {
                    auto createInfo = vk::BufferCreateInfo{};
                    createInfo.size = std::max<vk::DeviceSize>(resource.resolvedBufferSize, 1);
                    createInfo.usage = resource.resolvedBufferUsage;
                    if (createInfo.usage == vk::BufferUsageFlags{})
                    {
                        createInfo.usage = vk::BufferUsageFlagBits::eTransferDst;
                    }
                    createInfo.sharingMode = vk::SharingMode::eExclusive;

                    auto& buffer = context.device.resourcePool.allocateTransientBuffer(
                        createInfo,
                        resource.resolvedBufferMemoryUsage,
                        context.frameIndex,
                        resource.debugName);
                    binding.buffer = buffer.handle();
                    binding.bufferSize = buffer.size();
                    binding.bufferResource = std::ref(buffer);
                }
                else if (resource.importedBufferResource.has_value())
                {
                    // Use pre-allocated imported buffer from node
                    auto& buffer = resource.importedBufferResource->get();
                    nrAssert(buffer.valid(),
                        "RenderGraphExecutor::resolveRuntimeResources: importedBufferResource reference is invalid for resource: " + resource.debugName);
                    binding.buffer = buffer.handle();
                    binding.bufferSize = buffer.size();
                    binding.bufferResource = std::ref(buffer);
                }
                else
                {
                    // Fallback to legacy imported buffers map
                    auto imported = context.importedBuffers.find(resource.handle);
                    if (imported != context.importedBuffers.end())
                    {
                        binding.buffer = imported->second.get().handle();
                        binding.bufferSize = imported->second.get().size();
                        binding.bufferResource = imported->second;
                    }
                }
            }

            if (resource.isAccelerationStructure)
            {
                auto resolveImportedAccelerationStructure =
                    [&](const nr::rhi::AccelerationStructureResource& accelerationStructure) {
                        nrAssert(
                            accelerationStructure.valid(),
                            "RenderGraphExecutor::resolveRuntimeResources: imported acceleration structure reference is invalid for resource: " + resource.debugName);
                        binding.accelerationStructure = accelerationStructure.raw();
                        binding.accelerationStructureType = accelerationStructure.type();
                        binding.accelerationStructureSize = accelerationStructure.size();
                        binding.accelerationStructureStorageBuffer = accelerationStructure.storageBuffer().handle();
                        binding.accelerationStructureStorageOffset = accelerationStructure.storageOffset();
                        binding.accelerationStructureResource = std::cref(accelerationStructure);
                    };

                if (resource.importedAccelerationStructureResource.has_value())
                {
                    resolveImportedAccelerationStructure(resource.importedAccelerationStructureResource->get());
                }
                else
                {
                    auto imported = context.importedAccelerationStructures.find(resource.handle);
                    if (imported != context.importedAccelerationStructures.end())
                    {
                        resolveImportedAccelerationStructure(imported->second.get());
                    }
                }
            }

            if (resource.isImage)
            {
                if (resource.isSwapchain)
                {
                    nrAssert(
                        context.swapchainImageIndex.has_value(),
                        "RenderGraphExecutor::prepareFrame requires swapchainImageIndex in ExecuteContext when swapchain resources are present.");
                    binding.image = context.device.presentationContext.swapchainImage(*context.swapchainImageIndex);
                    binding.imageView = context.device.presentationContext.swapchainImageView(*context.swapchainImageIndex);
                }
                else
                {
                    auto isTransientManaged = resource.lifetime == ResourceLifetime::GraphTransient &&
                                              resource.residency == ResourceResidency::Managed;
                    if (isTransientManaged)
                    {
                        auto createInfo = vk::ImageCreateInfo{};
                        createInfo.imageType = vk::ImageType::e2D;
                        createInfo.format = resource.resolvedFormat;
                        createInfo.extent = resource.resolvedExtent;
                        createInfo.mipLevels = 1;
                        createInfo.arrayLayers = 1;
                        createInfo.samples = vk::SampleCountFlagBits::e1;
                        createInfo.tiling = vk::ImageTiling::eOptimal;
                        createInfo.usage = resource.resolvedImageUsage;
                        if (createInfo.usage == vk::ImageUsageFlags{})
                        {
                            createInfo.usage = vk::ImageUsageFlagBits::eTransferDst;
                        }
                        createInfo.sharingMode = vk::SharingMode::eExclusive;
                        // Vulkan image creation only permits Undefined/Preinitialized layouts.
                        createInfo.initialLayout = vk::ImageLayout::eUndefined;

                        auto& image = context.device.resourcePool.allocateTransientImage(
                            createInfo,
                            nr::rhi::MemoryUsage::GpuOnly,
                            context.frameIndex,
                            resource.debugName);
                        binding.image = image.handle();
                        binding.imageView = *image.view();
                        binding.imageResource = std::cref(image);
                    }
                    else if (resource.importedImageResource.has_value())
                    {
                        // Use pre-allocated imported image from node
                        const auto& image = resource.importedImageResource->get();
                        nrAssert(image.valid(),
                            "RenderGraphExecutor::resolveRuntimeResources: importedImageResource reference is invalid for resource: " + resource.debugName);
                        binding.image = image.handle();
                        binding.imageView = *image.view();
                        binding.imageResource = std::cref(image);
                        // Use the declared extent from the resource descriptor, not the actual
                        // image extent (which may be larger due to pre-allocation strategies).
                        // The descriptor extent represents the valid region for rendering.
                        binding.extent = resource.resolvedExtent;
                    }
                    else
                    {
                        // Fallback to legacy imported images map
                        auto imported = context.importedImages.find(resource.handle);
                        if (imported != context.importedImages.end())
                        {
                            binding.image = imported->second;
                        }
                    }
                }
            }

            bindings.insert_or_assign(resource.handle, binding);
        });

        return bindings;
    }

[[nodiscard]] std::size_t RenderGraphExecutor::invokePassPrepareCallbacks(
        CompiledGraphFrame& compiled,
        const ExecuteContext& context,
        const std::map<GraphResourceHandle, PreparedResourceBinding>& runtimeBindings)
{
        auto resolveBuffer = [&](GraphResourceHandle handle) -> std::optional<PassBufferResource> {
            auto bindingIt = runtimeBindings.find(handle);
            if (bindingIt == runtimeBindings.end() || !bindingIt->second.isBuffer)
            {
                return std::nullopt;
            }

            return PassBufferResource{
                .buffer = bindingIt->second.buffer,
                .size = bindingIt->second.bufferSize,
                .resource = bindingIt->second.bufferResource,
            };
        };

        auto resolveImage = [&](GraphResourceHandle handle) -> std::optional<PassImageResource> {
            auto bindingIt = runtimeBindings.find(handle);
            if (bindingIt == runtimeBindings.end() || !bindingIt->second.isImage)
            {
                return std::nullopt;
            }

            return PassImageResource{
                .image = bindingIt->second.image,
                .view = bindingIt->second.imageView,
                .extent = bindingIt->second.extent,
                .subresourceRange = bindingIt->second.subresourceRange,
                .resource = bindingIt->second.imageResource,
            };
        };

        auto resolveAccelerationStructure = [&](GraphResourceHandle handle) -> std::optional<PassAccelerationStructureResource> {
            auto bindingIt = runtimeBindings.find(handle);
            if (bindingIt == runtimeBindings.end() || !bindingIt->second.isAccelerationStructure)
            {
                return std::nullopt;
            }

            return PassAccelerationStructureResource{
                .accelerationStructure = bindingIt->second.accelerationStructure,
                .type = bindingIt->second.accelerationStructureType,
                .size = bindingIt->second.accelerationStructureSize,
                .storageBuffer = bindingIt->second.accelerationStructureStorageBuffer,
                .storageOffset = bindingIt->second.accelerationStructureStorageOffset,
                .resource = bindingIt->second.accelerationStructureResource,
            };
        };

        auto frameDataByHandle = CompiledFrameDataLookup{};
        std::ranges::for_each(compiled.frameData, [&](const GraphFrameDataDesc& frameData) {
            frameDataByHandle.emplace(frameData.handle, std::cref(frameData));
        });

        auto resolveFrameDataPayload = [&](GraphFrameDataHandle handle) -> std::optional<std::reference_wrapper<const std::any>> {
            auto frameDataIt = frameDataByHandle.find(handle);
            if (frameDataIt == frameDataByHandle.end())
            {
                return {};
            }

            return std::cref(frameDataIt->second.get().payload);
        };

        auto invokedPrepareCount = std::size_t{0};
        std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch& batch) {
            std::ranges::for_each(batch.passes, [&](const CompiledPass& pass) {
                if (!pass.prepare)
                {
                    return;
                }

                pass.prepare(PassPrepareContext{
                    .frameIndex = context.frameIndex,
                    .device = std::ref(context.device),
                    .resolveBuffer = resolveBuffer,
                    .resolveImage = resolveImage,
                    .resolveAccelerationStructure = resolveAccelerationStructure,
                    .resolveFrameDataPayload = resolveFrameDataPayload,
                });
                ++invokedPrepareCount;
            });
        });

        return invokedPrepareCount;
    }

[[nodiscard]] nr::rhi::CommandPool& RenderGraphExecutor::primaryPoolForQueue(nr::rhi::FrameContext& frame, QueueDomain queue)
{
        if (queue == QueueDomain::Graphics)
        {
            return frame.primary<nr::rhi::QueueRole::Graphics>();
        }
        if (queue == QueueDomain::Compute)
        {
            return frame.primary<nr::rhi::QueueRole::Compute>();
        }
        return frame.primary<nr::rhi::QueueRole::Transfer>();
    }

[[nodiscard]] nr::rhi::CommandPool& RenderGraphExecutor::secondaryPoolForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue,
        std::uint32_t secondaryPoolSlot)
{
        nrAssert(
            secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
            "RenderGraphExecutor must not assign pass recording work to the main-thread secondary pool slot.");

        if (queue == QueueDomain::Graphics)
        {
            return frame.secondary<nr::rhi::QueueRole::Graphics>(secondaryPoolSlot);
        }
        if (queue == QueueDomain::Compute)
        {
            return frame.secondary<nr::rhi::QueueRole::Compute>(secondaryPoolSlot);
        }
        return frame.secondary<nr::rhi::QueueRole::Transfer>(secondaryPoolSlot);
    }

[[nodiscard]] std::uint32_t RenderGraphExecutor::preparedSecondaryPoolSlotCountForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue)
{
        if (queue == QueueDomain::Graphics)
        {
            return static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Graphics>());
        }
        if (queue == QueueDomain::Compute)
        {
            return static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Compute>());
        }
        return static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Transfer>());
    }

[[nodiscard]] std::uint32_t RenderGraphExecutor::preparedRecordWorkerCountForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue)
{
        auto preparedPoolSlots = preparedSecondaryPoolSlotCountForQueue(frame, queue);
        if (preparedPoolSlots <= detail::kWorkerSecondaryPoolSlotBase)
        {
            return 0;
        }

        return preparedPoolSlots - detail::kWorkerSecondaryPoolSlotBase;
    }

[[nodiscard]] std::uint32_t RenderGraphExecutor::resolvedRecordWorkerCount(nr::rhi::FrameContext& frame)
{
        auto preparedPoolSlots = std::max({
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Graphics>()),
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Compute>()),
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Transfer>()),
        });
        nrAssert(
            preparedPoolSlots > detail::kWorkerSecondaryPoolSlotBase,
            "RenderGraphExecutor requires at least one worker-only secondary command pool beyond the main-thread slot before execute.");

        auto hardwareWorkers = std::max(1u, std::thread::hardware_concurrency());
        auto availableRecordWorkers = preparedPoolSlots - detail::kWorkerSecondaryPoolSlotBase;
        return std::min(nr::maxThreads, std::min(hardwareWorkers, availableRecordWorkers));
    }

[[nodiscard]] vk::raii::CommandBuffer& RenderGraphExecutor::primaryCommandBufferForQueue(
        const ExecuteContext& context,
        std::size_t frameSlot,
        QueueDomain queue,
        std::size_t ordinal)
{
        nrAssert(frameSlot < primaryCommandBuffersByFrame_.size(), "RenderGraphExecutor command buffer frame slot is out of range.");

        auto& frameCache = primaryCommandBuffersByFrame_[frameSlot];
        if (frameCache.size() <= ordinal)
        {
            frameCache.resize(ordinal + 1u);
        }

        auto& cached = frameCache[ordinal];
        if (cached.buffers.empty() || cached.queue != queue)
        {
            cached.queue = queue;
            auto& frame = context.device.frameManager.current();
            auto& pool = primaryPoolForQueue(frame, queue);
            cached.buffers = pool.allocatePrimary(1);
        }

        nrAssert(!cached.buffers.empty(), "RenderGraphExecutor cached command buffer allocation failed.");
        return cached.buffers.front();
    }

[[nodiscard]] vk::raii::CommandBuffer& RenderGraphExecutor::secondaryCommandBufferForPass(
        const ExecuteContext& context,
        std::size_t frameSlot,
        QueueDomain queue,
        std::size_t batchOrdinal,
        std::size_t passOrdinal,
        std::uint32_t secondaryPoolSlot)
{
        nrAssert(
            secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
            "RenderGraphExecutor must not allocate a pass secondary command buffer from the main-thread slot.");
        nrAssert(frameSlot < secondaryCommandBuffersByFrame_.size(), "RenderGraphExecutor secondary command buffer frame slot is out of range.");

        auto& frameCache = secondaryCommandBuffersByFrame_[frameSlot];
        if (frameCache.size() <= batchOrdinal)
        {
            frameCache.resize(batchOrdinal + 1u);
        }

        auto& batchCache = frameCache[batchOrdinal];
        if (batchCache.size() <= passOrdinal)
        {
            batchCache.resize(passOrdinal + 1u);
        }

        auto& cached = batchCache[passOrdinal];
        if (cached.buffers.empty() || cached.queue != queue || cached.secondaryPoolSlot != secondaryPoolSlot)
        {
            cached.queue = queue;
            cached.secondaryPoolSlot = secondaryPoolSlot;
            auto& frame = context.device.frameManager.current();
            auto& pool = secondaryPoolForQueue(frame, queue, secondaryPoolSlot);
            cached.buffers = pool.allocateSecondary(1);
        }

        nrAssert(!cached.buffers.empty(), "RenderGraphExecutor cached secondary command buffer allocation failed.");
        return cached.buffers.front();
    }

void RenderGraphExecutor::addTransitionBarrier(
        nr::rhi::ops::BarrierBatch& barriers,
        const CompiledResourceDesc& resource,
        const PreparedResourceBinding& binding,
        const ResourceStateTransition& transition,
        TransitionPlacement placement,
        std::uint32_t srcQueueFamilyIndex,
        std::uint32_t dstQueueFamilyIndex)
{
        constexpr auto kReadWriteAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;

        // Resolve each side from the precise declared scope when available, falling
        // back to a conservative all-commands scope only for unresolved sides so
        // resources without a declared access intent stay correct.
        auto srcStageMask = transition.srcScope.resolved()
                                ? transition.srcScope.stages
                                : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        auto srcAccessMask = transition.srcScope.resolved()
                                 ? transition.srcScope.access
                                 : vk::AccessFlags2{kReadWriteAccess};
        auto dstStageMask = transition.dstScope.resolved()
                                ? transition.dstScope.stages
                                : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        auto dstAccessMask = transition.dstScope.resolved()
                                 ? transition.dstScope.access
                                 : vk::AccessFlags2{kReadWriteAccess};

        if (resource.isImage && transition.oldLayout == ImageLayoutIntent::Undefined)
        {
            // Undefined old layout discards prior image contents; there is no
            // producer-side access to make visible for the transition.
            srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            srcAccessMask = vk::AccessFlags2{};
        }

        if (placement == TransitionPlacement::Release)
        {
            // Queue-release half: the destination side is the release boundary.
            dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
            dstAccessMask = vk::AccessFlags2{};
        }
        else if (placement == TransitionPlacement::Acquire)
        {
            // Queue-acquire half: the source side is the acquire boundary.
            srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
            srcAccessMask = vk::AccessFlags2{};
        }

        auto barrierSrcQueue = placement == TransitionPlacement::InPass
                                   ? nr::rhi::ops::kIgnoredQueueFamilyIndex
                                   : srcQueueFamilyIndex;
        auto barrierDstQueue = placement == TransitionPlacement::InPass
                                   ? nr::rhi::ops::kIgnoredQueueFamilyIndex
                                   : dstQueueFamilyIndex;

        if (resource.isBuffer)
        {
            nrAssert(binding.buffer != vk::Buffer{}, "RenderGraphExecutor::addTransitionBarrier requires a valid buffer binding.");
            barriers.add(vk::BufferMemoryBarrier2{
                srcStageMask,
                srcAccessMask,
                dstStageMask,
                dstAccessMask,
                barrierSrcQueue,
                barrierDstQueue,
                binding.buffer,
                0,
                std::numeric_limits<vk::DeviceSize>::max(),
                nullptr,
            });
            return;
        }

        if (resource.isAccelerationStructure)
        {
            nrAssert(
                binding.accelerationStructureStorageBuffer != vk::Buffer{},
                "RenderGraphExecutor::addTransitionBarrier requires a valid acceleration-structure storage buffer binding.");
            auto barrierSize = binding.accelerationStructureSize > 0
                                   ? binding.accelerationStructureSize
                                   : std::numeric_limits<vk::DeviceSize>::max();
            barriers.add(vk::BufferMemoryBarrier2{
                srcStageMask,
                srcAccessMask,
                dstStageMask,
                dstAccessMask,
                barrierSrcQueue,
                barrierDstQueue,
                binding.accelerationStructureStorageBuffer,
                binding.accelerationStructureStorageOffset,
                barrierSize,
                nullptr,
            });
            return;
        }

        if (resource.isImage)
        {
            nrAssert(binding.image != vk::Image{}, "RenderGraphExecutor::addTransitionBarrier requires a valid image binding.");
            barriers.add(vk::ImageMemoryBarrier2{
                srcStageMask,
                srcAccessMask,
                dstStageMask,
                dstAccessMask,
                RenderGraphCompiler::mapImageLayoutIntent(transition.oldLayout),
                RenderGraphCompiler::mapImageLayoutIntent(transition.newLayout),
                barrierSrcQueue,
                barrierDstQueue,
                binding.image,
                binding.subresourceRange,
                nullptr,
            });
        }
    }

[[nodiscard]] RecordTaskResult RenderGraphExecutor::recordPassToSecondary(const RecordTaskDesc& desc)
{
        nrAssert(
            desc.secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
            "RenderGraphExecutor::recordPassToSecondary requires a worker-only secondary pool slot.");

        auto result = RecordTaskResult{
            .batchOrdinal = desc.batchOrdinal,
            .passOrdinal = desc.passOrdinal,
            .workerId = desc.workerId,
            .queue = desc.queue,
            .commandBuffer = *desc.commandBuffer.get(),
        };

        auto& commandBuffer = desc.commandBuffer.get();
        auto const& pass = desc.pass.get();
        auto const& compiledResourceByHandle = desc.compiledResourceByHandle.get();
        auto const& runtimeBindings = desc.runtimeBindings.get();
        auto const& frameDataByHandle = desc.frameDataByHandle.get();

        commandBuffer.reset();
        auto inheritanceInfo = vk::CommandBufferInheritanceInfo{};
        nr::rhi::CommandRecorder::beginSecondary(
            commandBuffer,
            inheritanceInfo,
            vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto nodeDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
                commandBuffer,
                detail::nodeScopeLabel(pass.debugName),
            };
            auto passDebugLabelScope = detail::ScopedCommandBufferDebugLabel{commandBuffer, pass.debugName};

            auto inPassBarriers = nr::rhi::ops::BarrierBatch{};
            std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition& transition) {
                if (transition.strength != DependencyStrength::BarrierRequired)
                {
                    return;
                }

                auto resourceIt = compiledResourceByHandle.find(transition.resource);
                nrAssert(
                    resourceIt != compiledResourceByHandle.end(),
                    "RenderGraphExecutor::recordPassToSecondary pass barrier references an unknown resource handle.");

                auto bindingIt = runtimeBindings.find(transition.resource);
                nrAssert(
                    bindingIt != runtimeBindings.end(),
                    "RenderGraphExecutor::recordPassToSecondary pass barrier cannot resolve runtime resource binding.");

                addTransitionBarrier(
                    inPassBarriers,
                    resourceIt->second.get(),
                    bindingIt->second,
                    transition,
                    TransitionPlacement::InPass,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex);

                ++result.appliedInPassBarrierCount;
            });

            if (!inPassBarriers.empty())
            {
                nr::rhi::ops::pipelineBarrier(commandBuffer, inPassBarriers);
            }

            if (pass.record)
            {
                auto resolveBuffer = [&](GraphResourceHandle handle) -> std::optional<PassBufferResource> {
                    auto bindingIt = runtimeBindings.find(handle);
                    if (bindingIt == runtimeBindings.end() || !bindingIt->second.isBuffer)
                    {
                        return std::nullopt;
                    }

                    return PassBufferResource{
                        .buffer = bindingIt->second.buffer,
                        .size = bindingIt->second.bufferSize,
                        .resource = bindingIt->second.bufferResource,
                    };
                };

                auto resolveImage = [&](GraphResourceHandle handle) -> std::optional<PassImageResource> {
                    auto bindingIt = runtimeBindings.find(handle);
                    if (bindingIt == runtimeBindings.end() || !bindingIt->second.isImage)
                    {
                        return std::nullopt;
                    }

                    return PassImageResource{
                        .image = bindingIt->second.image,
                        .view = bindingIt->second.imageView,
                        .extent = bindingIt->second.extent,
                        .subresourceRange = bindingIt->second.subresourceRange,
                        .resource = bindingIt->second.imageResource,
                    };
                };

                auto resolveAccelerationStructure = [&](GraphResourceHandle handle) -> std::optional<PassAccelerationStructureResource> {
                    auto bindingIt = runtimeBindings.find(handle);
                    if (bindingIt == runtimeBindings.end() || !bindingIt->second.isAccelerationStructure)
                    {
                        return std::nullopt;
                    }

                    return PassAccelerationStructureResource{
                        .accelerationStructure = bindingIt->second.accelerationStructure,
                        .type = bindingIt->second.accelerationStructureType,
                        .size = bindingIt->second.accelerationStructureSize,
                        .storageBuffer = bindingIt->second.accelerationStructureStorageBuffer,
                        .storageOffset = bindingIt->second.accelerationStructureStorageOffset,
                        .resource = bindingIt->second.accelerationStructureResource,
                    };
                };

                auto resolveFrameDataPayload = [&](GraphFrameDataHandle handle) -> std::optional<std::reference_wrapper<const std::any>> {
                    auto frameDataIt = frameDataByHandle.find(handle);
                    if (frameDataIt == frameDataByHandle.end())
                    {
                        return {};
                    }

                    return std::cref(frameDataIt->second.get().payload);
                };

                pass.record(PassRecordContext{
                    .commandBuffer = std::cref(commandBuffer),
                    .frameIndex = desc.frameIndex,
                    .device = desc.device,
                    .resolveBuffer = resolveBuffer,
                    .resolveImage = resolveImage,
                    .resolveAccelerationStructure = resolveAccelerationStructure,
                    .resolveFrameDataPayload = resolveFrameDataPayload,
                });
                ++result.invokedPassRecordCount;
            }
            else
            {
                recordImplicitCopyPass(pass, commandBuffer, runtimeBindings);
            }
        }
        nr::rhi::CommandRecorder::end(commandBuffer);

        return result;
    }

[[nodiscard]] std::uint32_t RenderGraphExecutor::timestampValidBitsForQueue(
        const nr::rhi::Device& device,
        QueueDomain queue)
{
        auto const queueFamilyIndex = queueFamilyIndexFor(device, queue);
        auto const queueFamilyProperties = device.physicalDevice.getQueueFamilyProperties();
        nrAssert(
            queueFamilyIndex < queueFamilyProperties.size(),
            "RenderGraphExecutor timestamp query queue family index is out of range.");

        auto const validBits = queueFamilyProperties[queueFamilyIndex].timestampValidBits;
        nrAssert(
            validBits > 0u,
            std::format(
                "RenderGraphExecutor timestamp queries require non-zero timestampValidBits for {} queue family {}.",
                detail::queueDomainLabel(queue),
                queueFamilyIndex));
        return validBits;
    }

[[nodiscard]] std::map<QueueDomain, std::uint32_t> RenderGraphExecutor::timestampValidBitsForQueues(
        const nr::rhi::Device& device,
        std::span<const GpuPassTimingSample> passes)
{
        auto validBitsByQueue = std::map<QueueDomain, std::uint32_t>{};
        std::ranges::for_each(passes, [&](const GpuPassTimingSample& pass) {
            if (validBitsByQueue.contains(pass.queue))
            {
                return;
            }

            validBitsByQueue.emplace(pass.queue, timestampValidBitsForQueue(device, pass.queue));
        });
        return validBitsByQueue;
    }

[[nodiscard]] std::uint64_t RenderGraphExecutor::timestampDeltaTicks(
        std::uint64_t begin,
        std::uint64_t end,
        std::uint32_t validBits) noexcept
{
        if (validBits >= 64u)
        {
            return end - begin;
        }

        auto const mask = (std::uint64_t{1} << validBits) - 1u;
        return ((end & mask) - (begin & mask)) & mask;
    }

[[nodiscard]] std::vector<GpuPassTimingSample> RenderGraphExecutor::buildPassTimingSamples(
        const CompiledGraphFrame& compiled)
{
        return compiled.submitBatches |
               std::views::transform(&CompiledSubmitBatch::passes) |
               std::views::join |
               std::views::transform([](const CompiledPass& pass) {
                   return GpuPassTimingSample{
                       .pass = pass.handle,
                       .debugName = pass.debugName,
                       .queue = pass.queue,
                       .isCopyPass = pass.isCopyPass,
                   };
               }) |
               std::ranges::to<std::vector>();
    }

void RenderGraphExecutor::ensureTimingQueryPool(
        const nr::rhi::Device& device,
        FrameGpuPassTimingState& state,
        std::uint32_t requiredQueryCount)
{
        if (requiredQueryCount == 0u)
        {
            state.pendingPasses.clear();
            return;
        }

        if (*state.queryPool != vk::QueryPool{} && state.queryCapacity >= requiredQueryCount)
        {
            return;
        }

        auto createInfo = vk::QueryPoolCreateInfo{};
        createInfo.queryType = vk::QueryType::eTimestamp;
        createInfo.queryCount = requiredQueryCount;
        state.queryPool = device.device.createQueryPool(createInfo);
        state.queryCapacity = requiredQueryCount;
        state.pendingPasses.clear();
    }

[[nodiscard]] std::optional<GpuPassTimingFrame> RenderGraphExecutor::collectCompletedGpuPassTimings(
        const nr::rhi::Device& device,
        FrameGpuPassTimingState& state)
{
        if (state.pendingPasses.empty() || *state.queryPool == vk::QueryPool{})
        {
            state.pendingPasses.clear();
            return std::nullopt;
        }

        auto const queryCount = timingQueryCountForPassCount(state.pendingPasses.size());
        auto const valuesPerQuery = std::size_t{2};
        auto const resultStride = static_cast<vk::DeviceSize>(sizeof(std::uint64_t) * valuesPerQuery);
        auto [result, data] = state.queryPool.getResults<std::uint64_t>(
            0u,
            queryCount,
            sizeof(std::uint64_t) * valuesPerQuery * queryCount,
            resultStride,
            vk::QueryResultFlagBits::e64 |
                vk::QueryResultFlagBits::eWithAvailability);

        if (result != vk::Result::eSuccess && result != vk::Result::eNotReady)
        {
            nrAssert(false, std::format("RenderGraphExecutor timestamp query read failed: {}", vk::to_string(result)));
        }

        auto const validBitsByQueue = timestampValidBitsForQueues(
            device,
            std::span<const GpuPassTimingSample>{state.pendingPasses.data(), state.pendingPasses.size()});
        auto const timestampPeriodNanoseconds = static_cast<double>(device.physicalDevice.getProperties().limits.timestampPeriod);

        auto frame = GpuPassTimingFrame{
            .frameIndex = state.pendingFrameIndex,
        };
        frame.passes.reserve(state.pendingPasses.size());

        auto passIndices = std::views::iota(std::size_t{0}, state.pendingPasses.size());
        std::ranges::for_each(passIndices, [&](std::size_t passIndex) {
            auto const beginQuery = beginTimingQueryForPass(passIndex);
            auto const beginOffset = beginQuery * valuesPerQuery;
            auto const endOffset = (beginQuery + 1u) * valuesPerQuery;
            nrAssert(
                endOffset + 1u < data.size(),
                "RenderGraphExecutor timestamp query result buffer is smaller than the recorded query range.");

            auto const beginAvailable = data[beginOffset + 1u] != 0u;
            auto const endAvailable = data[endOffset + 1u] != 0u;
            if (!beginAvailable || !endAvailable)
            {
                return;
            }

            auto sample = state.pendingPasses[passIndex];
            auto const validBitsIt = validBitsByQueue.find(sample.queue);
            nrAssert(validBitsIt != validBitsByQueue.end(), "RenderGraphExecutor timestamp queue valid-bit lookup failed.");
            auto const elapsedTicks = timestampDeltaTicks(
                data[beginOffset],
                data[endOffset],
                validBitsIt->second);
            sample.milliseconds =
                static_cast<double>(elapsedTicks) * timestampPeriodNanoseconds / 1'000'000.0;
            frame.passes.push_back(std::move(sample));
        });

        state.pendingPasses.clear();
        if (frame.passes.empty())
        {
            return std::nullopt;
        }

        return frame;
    }

[[nodiscard]] std::vector<RenderGraphExecutor::RecordBatchTasks> RenderGraphExecutor::launchRecordTasksForAllBatches(
        const ExecuteContext& context,
        std::size_t frameSlot,
        const CompiledGraphFrame& compiled,
        const CompiledResourceLookup& compiledResourceByHandle,
        const RuntimeBindingMap& runtimeBindings,
        const CompiledFrameDataLookup& frameDataByHandle,
        ExecuteReport& report)
{
        nrAssert(
            report.plan.batches.size() == compiled.submitBatches.size(),
            "RenderGraphExecutor::launchRecordTasksForAllBatches requires plan and compiled batch counts to match.");

        auto& frame = context.device.frameManager.current();
        auto tasksByBatch = std::vector<RecordBatchTasks>{};
        tasksByBatch.reserve(compiled.submitBatches.size());
        auto usedWorkerIds = std::set<std::uint32_t>{};
        auto recordTaskOrdinal = std::size_t{0};

        auto batchOrdinals = std::views::iota(std::size_t{0}, compiled.submitBatches.size());
        std::ranges::for_each(batchOrdinals, [&](std::size_t batchOrdinal) {
            const auto& planBatch = report.plan.batches[batchOrdinal];
            const auto& compiledBatch = compiled.submitBatches[batchOrdinal];

            auto batchTasks = RecordBatchTasks{
                .batchOrdinal = batchOrdinal,
            };
            batchTasks.futures.reserve(compiledBatch.passes.size());

            if (!compiledBatch.passes.empty())
            {
                auto queueWorkerCount = std::min(
                    recordThreadPool_.workerCount(),
                    preparedRecordWorkerCountForQueue(frame, planBatch.queue));
                nrAssert(
                    queueWorkerCount > 0,
                    "RenderGraphExecutor::launchRecordTasksForAllBatches requires at least one worker-only secondary command pool for the batch queue.");

                auto passOrdinals = std::views::iota(std::size_t{0}, compiledBatch.passes.size());
                std::ranges::for_each(passOrdinals, [&](std::size_t passOrdinal) {
                    auto workerId = static_cast<std::uint32_t>(recordTaskOrdinal % queueWorkerCount);
                    ++recordTaskOrdinal;
                    usedWorkerIds.insert(workerId);

                    auto secondaryPoolSlot = detail::secondaryPoolSlotForRecordWorker(workerId);
                    auto& secondaryCommandBuffer = secondaryCommandBufferForPass(
                        context,
                        frameSlot,
                        planBatch.queue,
                        batchOrdinal,
                        passOrdinal,
                        secondaryPoolSlot);

                    auto desc = RecordTaskDesc{
                        .batchOrdinal = batchOrdinal,
                        .passOrdinal = passOrdinal,
                        .workerId = workerId,
                        .secondaryPoolSlot = secondaryPoolSlot,
                        .queue = planBatch.queue,
                        .pass = std::cref(compiledBatch.passes[passOrdinal]),
                        .commandBuffer = std::cref(secondaryCommandBuffer),
                        .frameIndex = context.frameIndex,
                        .device = std::ref(context.device),
                        .compiledResourceByHandle = std::cref(compiledResourceByHandle),
                        .runtimeBindings = std::cref(runtimeBindings),
                        .frameDataByHandle = std::cref(frameDataByHandle),
                    };

                    batchTasks.futures.push_back(recordThreadPool_.submit(desc.workerId, [desc]() {
                        return recordPassToSecondary(desc);
                    }));
                    ++report.submittedRecordTaskCount;
                });
            }

            tasksByBatch.push_back(std::move(batchTasks));
        });

        report.parallelPassRecording = usedWorkerIds.size() > 1u;
        return tasksByBatch;
    }

[[nodiscard]] std::vector<RecordTaskResult> RenderGraphExecutor::collectRecordTaskResults(
        RecordBatchTasks& tasks,
        std::size_t batchOrdinal,
        ExecuteReport& report)
{
        nrAssert(
            tasks.batchOrdinal == batchOrdinal,
            "RenderGraphExecutor::collectRecordTaskResults received a task group for the wrong submit batch.");

        auto results = std::vector<RecordTaskResult>{};
        results.reserve(tasks.futures.size());
        std::ranges::for_each(tasks.futures, [&](std::future<RecordTaskResult>& future) {
            auto result = future.get();
            report.invokedPassRecordCount += result.invokedPassRecordCount;
            report.appliedInPassBarrierCount += result.appliedInPassBarrierCount;
            ++report.recordedSecondaryCommandBufferCount;
            results.push_back(result);
        });

        std::ranges::sort(results, {}, &RecordTaskResult::passOrdinal);
        auto resultOrdinals = std::views::iota(std::size_t{0}, results.size());
        std::ranges::for_each(resultOrdinals, [&](std::size_t resultOrdinal) {
            nrAssert(
                results[resultOrdinal].batchOrdinal == batchOrdinal &&
                    results[resultOrdinal].passOrdinal == resultOrdinal,
                "RenderGraphExecutor::collectRecordTaskResults collected an out-of-order or mismatched record task result.");
        });

        tasks.futures.clear();
        return results;
    }

void RenderGraphExecutor::executeRecordedSecondaries(
        const vk::raii::CommandBuffer& primaryCommandBuffer,
        std::span<const RecordTaskResult> results,
        vk::QueryPool timingQueryPool,
        std::size_t firstTimedPassIndex)
{
        if (results.empty())
        {
            return;
        }

        nrAssert(
            timingQueryPool != vk::QueryPool{},
            "RenderGraphExecutor::executeRecordedSecondaries requires a valid timestamp query pool.");

        auto resultOrdinals = std::views::iota(std::size_t{0}, results.size());
        std::ranges::for_each(resultOrdinals, [&](std::size_t resultOrdinal) {
            auto const& result = results[resultOrdinal];
            nrAssert(
                result.passOrdinal == resultOrdinal,
                "RenderGraphExecutor::executeRecordedSecondaries requires recorded passes in compiled pass order.");

            auto const beginQuery = beginTimingQueryForPass(firstTimedPassIndex + resultOrdinal);
            primaryCommandBuffer.writeTimestamp2(
                vk::PipelineStageFlagBits2::eTopOfPipe,
                timingQueryPool,
                beginQuery);
            auto commandBuffers = std::array{result.commandBuffer};
            primaryCommandBuffer.executeCommands(commandBuffers);
            primaryCommandBuffer.writeTimestamp2(
                vk::PipelineStageFlagBits2::eBottomOfPipe,
                timingQueryPool,
                beginQuery + 1u);
        });
    }

[[nodiscard]] vk::ImageSubresourceLayers RenderGraphExecutor::toSubresourceLayers(const vk::ImageSubresourceRange& range)
{
        return vk::ImageSubresourceLayers{
            range.aspectMask,
            range.baseMipLevel,
            range.baseArrayLayer,
            std::max(range.layerCount, 1u),
        };
    }

void RenderGraphExecutor::recordImplicitCopyPass(
        const CompiledPass& pass,
        const vk::raii::CommandBuffer& commandBuffer,
        const std::map<GraphResourceHandle, PreparedResourceBinding>& runtimeBindings)
{
        if (!pass.isCopyPass)
        {
            return;
        }

        auto srcUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc& use) {
            return use.imageUsage == ImageUsageIntent::TransferSrc ||
                   use.imageUsage == ImageUsageIntent::CopySource;
        });

        auto dstUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc& use) {
            return use.imageUsage == ImageUsageIntent::TransferDst ||
                   use.imageUsage == ImageUsageIntent::CopyDestination;
        });

        auto presentUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc& use) {
            return use.imageUsage == ImageUsageIntent::PresentSource;
        });

        if (srcUse == pass.resourceUses.end() || dstUse == pass.resourceUses.end())
        {
            return;
        }

        auto srcBindingIt = runtimeBindings.find(srcUse->resource);
        auto dstBindingIt = runtimeBindings.find(dstUse->resource);
        if (srcBindingIt == runtimeBindings.end() || dstBindingIt == runtimeBindings.end())
        {
            return;
        }

        auto srcBinding = srcBindingIt->second;
        auto dstBinding = dstBindingIt->second;
        if (srcBinding.image == vk::Image{} || dstBinding.image == vk::Image{})
        {
            return;
        }

        auto srcLayout = srcUse->imageLayout.has_value()
                             ? RenderGraphCompiler::mapImageLayoutIntent(*srcUse->imageLayout)
                             : vk::ImageLayout::eTransferSrcOptimal;
        auto dstLayout = dstUse->imageLayout.has_value()
                             ? RenderGraphCompiler::mapImageLayoutIntent(*dstUse->imageLayout)
                             : vk::ImageLayout::eTransferDstOptimal;

        auto region = vk::ImageCopy{};
        region.srcSubresource = toSubresourceLayers(srcBinding.subresourceRange);
        region.dstSubresource = toSubresourceLayers(dstBinding.subresourceRange);
        region.extent = vk::Extent3D{
            std::min(srcBinding.extent.width, dstBinding.extent.width),
            std::min(srcBinding.extent.height, dstBinding.extent.height),
            std::min(srcBinding.extent.depth, dstBinding.extent.depth),
        };

        nr::rhi::ops::copyImage2(
            commandBuffer,
            srcBinding.image,
            srcLayout,
            dstBinding.image,
            dstLayout,
            nr::rhi::ops::toImageCopy2(region));

        if (presentUse != pass.resourceUses.end() && presentUse->resource == dstUse->resource)
        {
            auto presentLayout = presentUse->imageLayout.has_value()
                                     ? RenderGraphCompiler::mapImageLayoutIntent(*presentUse->imageLayout)
                                     : vk::ImageLayout::ePresentSrcKHR;

            if (dstLayout != presentLayout)
            {
                auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};
                postCopyBarriers.add(vk::ImageMemoryBarrier2{
                    vk::PipelineStageFlagBits2::eTransfer,
                    vk::AccessFlagBits2::eTransferWrite,
                    vk::PipelineStageFlagBits2::eBottomOfPipe,
                    vk::AccessFlags2{},
                    dstLayout,
                    presentLayout,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    nr::rhi::ops::kIgnoredQueueFamilyIndex,
                    dstBinding.image,
                    dstBinding.subresourceRange,
                    nullptr,
                });

                nr::rhi::ops::pipelineBarrier(commandBuffer, postCopyBarriers);
            }
        }
    }
} // namespace nr::renderer
