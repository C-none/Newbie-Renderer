module nr.renderer;
import :renderGraphExecutor;
import dependency.vulkan;
import nr.rhi;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;
import :rendererType;
import :rendererSubmission;

namespace nr::renderer::detail
{
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

[[nodiscard]] std::string rendererBatchScopeLabel(std::uint32_t batchIndex, QueueDomain queue,
                                                  std::string_view openedBySubmitNodeDebugName)
{
    if (!openedBySubmitNodeDebugName.empty())
    {
        return std::format("Renderer.Submit.{}.{}.{}", openedBySubmitNodeDebugName, queueDomainLabel(queue),
                           batchIndex);
    }

    return std::format("Renderer.Batch.{}.{}", queueDomainLabel(queue), batchIndex);
}

} // namespace nr::renderer::detail

namespace nr::renderer
{
[[nodiscard]] ExecutorPlan RenderGraphExecutor::buildPlan(const CompiledGraphFrame &compiled) const
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

    struct PendingTimelineWait
    {
        std::uint64_t value = 0;
        vk::PipelineStageFlags2 stages{};
    };

    auto releaseByBatch = std::map<std::uint32_t, std::vector<ResourceStateTransition>>{};
    auto acquireByBatch = std::map<std::uint32_t, std::vector<ResourceStateTransition>>{};
    auto initialReleaseByQueue = std::map<QueueDomain, std::vector<ResourceStateTransition>>{};
    auto initialResourceWaitsByBatch = std::map<std::uint32_t, std::map<QueueDomain, PendingTimelineWait>>{};
    auto waitStagesByBatch = std::map<std::uint32_t, vk::PipelineStageFlags2>{};
    auto dedupe = std::set<std::tuple<std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                      std::uint32_t, std::uint32_t>>{};
    auto lastUse = std::map<GraphResourceHandle, LastResourceUse>{};

    std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch &batch) {
        std::ranges::for_each(batch.passes, [&](const CompiledPass &pass) {
            std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition &transition) {
                auto previousUse = lastUse.find(transition.resource);
                if (previousUse == lastUse.end())
                {
                    auto dstBatchIndex = pass.submitBatchIndex;
                    auto waitStages = transition.dstScope.stages != vk::PipelineStageFlags2{}
                                          ? transition.dstScope.stages
                                          : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
                    if (transition.sourceSubmissionTimelineValue > 0)
                    {
                        auto &pendingWait = initialResourceWaitsByBatch[dstBatchIndex][transition.srcQueue];
                        pendingWait.value = std::max(pendingWait.value, transition.sourceSubmissionTimelineValue);
                        pendingWait.stages |= waitStages;
                    }

                    if (transition.strength == DependencyStrength::ReleaseAcquireRequired)
                    {
                        waitStagesByBatch[dstBatchIndex] |= waitStages;
                        initialReleaseByQueue[transition.srcQueue].push_back(transition);
                        acquireByBatch[dstBatchIndex].push_back(transition);
                    }
                    return;
                }

                auto srcBatchIndex = previousUse->second.batchIndex;
                auto dstBatchIndex = pass.submitBatchIndex;
                if (srcBatchIndex != dstBatchIndex)
                {
                    auto waitStages = transition.dstScope.stages != vk::PipelineStageFlags2{}
                                          ? transition.dstScope.stages
                                          : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
                    waitStagesByBatch[dstBatchIndex] |= waitStages;
                }

                if (transition.strength != DependencyStrength::ReleaseAcquireRequired)
                {
                    return;
                }

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

            std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc &use) {
                lastUse.insert_or_assign(use.resource, LastResourceUse{
                                                           .batchIndex = pass.submitBatchIndex,
                                                           .queue = pass.queue,
                                                       });
            });
        });
    });

    auto initialReleaseBatchIndex = static_cast<std::uint32_t>(compiled.submitBatches.size());
    std::ranges::for_each(initialReleaseByQueue, [&](auto &pair) {
        plan.initialReleaseBatches.push_back(ExecutorBatchPlan{
            .batchIndex = initialReleaseBatchIndex++,
            .queue = pair.first,
            .tailReleaseTransitions = std::move(pair.second),
            .waitsForPreviousBatch = !plan.initialReleaseBatches.empty(),
            .signalsNextBatch = true,
        });
    });

    auto batchIndices = std::views::iota(std::size_t{0}, compiled.submitBatches.size());
    std::ranges::for_each(batchIndices, [&](std::size_t batchOrdinal) {
        const auto &batch = compiled.submitBatches[batchOrdinal];
        auto inPassBarrierCount = std::size_t{0};

        std::ranges::for_each(batch.passes, [&](const CompiledPass &pass) {
            inPassBarrierCount += static_cast<std::size_t>(
                std::ranges::count_if(pass.preBarriers, [](const ResourceStateTransition &transition) {
                    return transition.strength == DependencyStrength::BarrierRequired;
                }));
        });

        auto waitsForPreviousBatch = batchOrdinal > 0 || !plan.initialReleaseBatches.empty();
        auto isLastBatch = batchOrdinal + 1 == compiled.submitBatches.size();
        auto signalsPresent = isLastBatch && batch.queue == QueueDomain::Compute;
        auto signalsNextBatch = !signalsPresent;
        auto waitStageIt = waitStagesByBatch.find(batch.batchIndex);
        auto waitStageMask =
            waitStageIt != waitStagesByBatch.end() ? waitStageIt->second : submissionWaitStage(batch.queue);
        auto initialResourceWaits = std::vector<ExecutorTimelineWait>{};
        auto initialResourceWaitIt = initialResourceWaitsByBatch.find(batch.batchIndex);
        if (initialResourceWaitIt != initialResourceWaitsByBatch.end())
        {
            initialResourceWaits = initialResourceWaitIt->second | std::views::transform([](const auto &pair) {
                                       return ExecutorTimelineWait{
                                           .token =
                                               RendererSubmitToken{
                                                   .queue = pair.first,
                                                   .value = pair.second.value,
                                               },
                                           .stageMask = pair.second.stages,
                                       };
                                   }) |
                                   std::ranges::to<std::vector>();
        }

        plan.totalPassCount += batch.passes.size();
        plan.totalInPassBarrierCount += inPassBarrierCount;

        plan.batches.push_back(ExecutorBatchPlan{
            .batchIndex = batch.batchIndex,
            .queue = batch.queue,
            .passCount = batch.passes.size(),
            .inPassBarrierCount = inPassBarrierCount,
            .headAcquireTransitions = acquireByBatch[batch.batchIndex],
            .tailReleaseTransitions = releaseByBatch[batch.batchIndex],
            .initialResourceWaits = std::move(initialResourceWaits),
            .waitStageMask = waitStageMask,
            .waitsForPreviousBatch = waitsForPreviousBatch,
            .signalsNextBatch = signalsNextBatch,
            .signalsPresent = signalsPresent,
            .acquiresSwapchainBeforeSubmit = batch.openedBySubmitNodeKind == SubmitBoundaryKind::SwapchainAcquire,
        });
    });

    plan.finalQueueIsCompute = compiled.submitBatches.back().queue == QueueDomain::Compute;
    plan.requiresSyntheticPresentBatch = !plan.finalQueueIsCompute;
    return plan;
}

[[nodiscard]] PreparedGraphFrame RenderGraphExecutor::prepareFrame(const CompiledGraphFrame &compiled,
                                                                   const ExecuteContext &context) const
{
    return prepareFrame(CompiledGraphFrame{compiled}, context);
}

[[nodiscard]] PreparedGraphFrame RenderGraphExecutor::prepareFrame(CompiledGraphFrame &&compiled,
                                                                   const ExecuteContext &context) const
{
    applyQueueFamilyTransferPolicy(compiled, context.device);

    auto prepared = PreparedGraphFrame{};
    prepared.plan = buildPlan(compiled);
    prepared.runtimeBindings = resolveRuntimeResources(compiled, context);
    auto acquireBatch = std::ranges::find_if(compiled.submitBatches, [](const CompiledSubmitBatch &batch) {
        return batch.openedBySubmitNodeKind == SubmitBoundaryKind::SwapchainAcquire;
    });
    if (acquireBatch != compiled.submitBatches.end())
    {
        prepared.firstDeferredPrepareBatch =
            static_cast<std::size_t>(std::ranges::distance(compiled.submitBatches.begin(), acquireBatch));
    }
    auto const immediatePrepareBatchCount = prepared.firstDeferredPrepareBatch.value_or(compiled.submitBatches.size());
    prepared.invokedPassPrepareCount =
        invokePassPrepareCallbacks(compiled, context, prepared.runtimeBindings, 0u, immediatePrepareBatchCount);
    prepared.compiled = std::move(compiled);
    return prepared;
}

[[nodiscard]] ExecuteReport RenderGraphExecutor::execute(const CompiledGraphFrame &compiled,
                                                         const ExecuteContext &context)
{
    auto prepared = prepareFrame(compiled, context);
    return executePrepared(prepared, context);
}

[[nodiscard]] ExecuteReport RenderGraphExecutor::executePrepared(const PreparedGraphFrame &prepared,
                                                                 const ExecuteContext &context)
{
    auto telemetry = context.benchmarkTelemetry;
    if (telemetry.has_value())
    {
        telemetry->get() = {};
    }
    auto const timingStart = [&] {
        return telemetry.has_value() ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    };
    auto const elapsedMilliseconds = [](std::chrono::steady_clock::time_point start) {
        return std::chrono::duration<double, std::milli>{std::chrono::steady_clock::now() - start}.count();
    };
    auto const recordTiming = [&](double ExecutorBenchmarkTelemetry::*field,
                                  std::chrono::steady_clock::time_point start) {
        if (telemetry.has_value())
        {
            telemetry->get().*field += elapsedMilliseconds(start);
        }
    };
    auto const executorSetupStart = timingStart();
    auto report = ExecuteReport{};
    report.plan = prepared.plan;
    report.invokedPassPrepareCount = prepared.invokedPassPrepareCount;

    auto const &compiled = prepared.compiled;
    auto runtimeBindings = prepared.runtimeBindings;
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
    recordTiming(&ExecutorBenchmarkTelemetry::executorSetupMilliseconds, executorSetupStart);

    auto &frameTimingState = gpuPassTimingStatesByFrame_[frameSlot];
    auto const completedGpuTimingReadbackStart = timingStart();
    report.completedGpuPassTimingFrame = collectCompletedGpuPassTimings(context.device, frameTimingState);
    recordTiming(&ExecutorBenchmarkTelemetry::completedGpuTimingReadbackMilliseconds, completedGpuTimingReadbackStart);

    auto const timingSetupStart = timingStart();
    auto currentTimingSamples = buildPassTimingSamples(compiled);
    if (!currentTimingSamples.empty())
    {
        static_cast<void>(timestampValidBitsForQueues(
            context.device,
            std::span<const GpuPassTimingSample>{currentTimingSamples.data(), currentTimingSamples.size()}));
    }
    ensureTimingQueryPool(context.device, frameTimingState, timingQueryCountForPassCount(currentTimingSamples.size()));
    recordTiming(&ExecutorBenchmarkTelemetry::timingSetupMilliseconds, timingSetupStart);

    auto const perFrameLookupStart = timingStart();
    auto desiredWorkerCount = resolvedRecordWorkerCount(context.device.frameManager.current());
    recordThreadPool_.ensureWorkerCount(desiredWorkerCount);
    report.recordWorkerCount = recordThreadPool_.workerCount();

    auto compiledResourceByHandle = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>{};
    std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc &resource) {
        compiledResourceByHandle.emplace(resource.handle, std::cref(resource));
    });

    auto frameDataByHandle = CompiledFrameDataLookup{};
    std::ranges::for_each(compiled.frameData, [&](const GraphFrameDataDesc &frameData) {
        frameDataByHandle.emplace(frameData.handle, std::cref(frameData));
    });

    auto timelines =
        context.submissionTimelines.has_value()
            ? std::optional<std::reference_wrapper<RendererSubmissionTimelines>>(context.submissionTimelines->get())
            : std::nullopt;
    auto timelinesValid = timelines.has_value() && timelines->get().valid();

    if (report.plan.batches.size() > 1)
    {
        nrAssert(timelinesValid, "RenderGraphExecutor::execute requires valid per-queue submission timelines when more "
                                 "than one submit batch exists.");
    }

    if (!report.plan.initialReleaseBatches.empty())
    {
        nrAssert(timelinesValid, "RenderGraphExecutor::execute requires valid per-queue submission timelines for "
                                 "retained-resource initial ownership transfers.");
    }

    if (std::ranges::any_of(report.plan.batches,
                            [](const ExecutorBatchPlan &batch) { return !batch.initialResourceWaits.empty(); }))
    {
        nrAssert(timelinesValid, "RenderGraphExecutor::execute requires valid per-queue submission timelines for "
                                 "implicit retained-resource acquisition.");
    }

    if (report.plan.requiresSyntheticPresentBatch && !report.plan.batches.empty())
    {
        nrAssert(timelinesValid, "RenderGraphExecutor::execute requires valid per-queue submission timelines when "
                                 "inserting a synthetic compute-final present batch.");
    }

    auto const swapchainResourceCount = static_cast<std::size_t>(std::ranges::count_if(
        compiled.resources, [](const CompiledResourceDesc &resource) { return resource.isSwapchain; }));
    auto const acquireBoundaryCount = static_cast<std::size_t>(std::ranges::count_if(
        report.plan.batches, [](const ExecutorBatchPlan &batch) { return batch.acquiresSwapchainBeforeSubmit; }));
    if (telemetry.has_value())
    {
        telemetry->get().compiledSubmitBatchCount = compiled.submitBatches.size();
        telemetry->get().acquireBatchCount = acquireBoundaryCount;
    }
    nrAssert(swapchainResourceCount == 0u || acquireBoundaryCount == 1u,
             "RenderGraphExecutor::execute requires exactly one swapchain-acquire boundary when swapchain resources "
             "are present.");
    nrAssert(acquireBoundaryCount <= 1u,
             "RenderGraphExecutor::execute supports at most one swapchain-acquire boundary per frame.");
    if (context.preAcquiredFrameImage.has_value())
    {
        nrAssert(swapchainResourceCount > 0u && acquireBoundaryCount == 1u,
                 "RenderGraphExecutor::execute requires one compiled swapchain acquire boundary when a frame image was "
                 "pre-acquired.");
    }
    if (swapchainResourceCount > 0u)
    {
        auto firstSwapchainBatch = std::ranges::find_if(compiled.submitBatches, [&](const CompiledSubmitBatch &batch) {
            return std::ranges::any_of(batch.passes, [&](const CompiledPass &pass) {
                return std::ranges::any_of(pass.resolvedResourceIndices, [&](std::size_t resourceIndex) {
                    nrAssert(resourceIndex < compiled.resources.size(),
                             "RenderGraphExecutor::execute pass resource index is out of range.");
                    return compiled.resources[resourceIndex].isSwapchain;
                });
            });
        });
        nrAssert(firstSwapchainBatch != compiled.submitBatches.end(),
                 "RenderGraphExecutor::execute compiled a swapchain resource that no pass accesses.");
        auto const firstSwapchainBatchOrdinal =
            static_cast<std::size_t>(std::ranges::distance(compiled.submitBatches.begin(), firstSwapchainBatch));
        nrAssert(report.plan.batches[firstSwapchainBatchOrdinal].acquiresSwapchainBeforeSubmit,
                 "RenderGraphExecutor::execute requires the swapchain acquire boundary immediately before the first "
                 "swapchain access.");
    }
    recordTiming(&ExecutorBenchmarkTelemetry::perFrameLookupMilliseconds, perFrameLookupStart);

    auto previousSignalToken = RendererSubmitToken{};
    auto signalTokenByBatch = std::map<std::uint32_t, RendererSubmitToken>{};
    auto timedPassOffset = std::size_t{0};
    auto deferredPrepareInvoked = false;

    auto recordOwnershipTransitions = [&](const vk::raii::CommandBuffer &commandBuffer,
                                          const std::vector<ResourceStateTransition> &transitions,
                                          TransitionPlacement placement) {
        auto barriers = nr::rhi::ops::BarrierBatch{};
        auto appliedBarrierCount = std::size_t{0};
        std::ranges::for_each(transitions, [&](const ResourceStateTransition &transition) {
            auto resourceIt = compiledResourceByHandle.find(transition.resource);
            nrAssert(resourceIt != compiledResourceByHandle.end(),
                     "RenderGraphExecutor::execute ownership transition references an unknown resource handle.");

            auto bindingIt = runtimeBindings.find(transition.resource);
            nrAssert(bindingIt != runtimeBindings.end(),
                     "RenderGraphExecutor::execute ownership transition cannot resolve runtime resource binding.");

            auto const addedBarrier =
                addTransitionBarrier(barriers, resourceIt->second.get(), bindingIt->second, transition, placement,
                                     queueFamilyIndexFor(context.device, transition.srcQueue),
                                     queueFamilyIndexFor(context.device, transition.dstQueue));
            appliedBarrierCount += static_cast<std::size_t>(addedBarrier);
        });

        if (!barriers.empty())
        {
            nr::rhi::ops::pipelineBarrier(commandBuffer, barriers);
        }
        return appliedBarrierCount;
    };

    auto const initialReleaseRecordSubmitStart = timingStart();
    auto initialReleaseOrdinals = std::views::iota(std::size_t{0}, report.plan.initialReleaseBatches.size());
    std::ranges::for_each(initialReleaseOrdinals, [&](std::size_t initialReleaseOrdinal) {
        const auto &planBatch = report.plan.initialReleaseBatches[initialReleaseOrdinal];
        auto commandBufferOrdinal = report.plan.batches.size() + initialReleaseOrdinal;
        auto &commandBuffer = primaryCommandBufferForQueue(context, frameSlot, planBatch.queue, commandBufferOrdinal);

        commandBuffer.reset();
        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto debugLabelScope = detail::ScopedCommandBufferDebugLabel{
                commandBuffer,
                "Renderer.InitialOwnershipRelease",
            };
            report.appliedReleaseBarrierCount += recordOwnershipTransitions(
                commandBuffer, planBatch.tailReleaseTransitions, TransitionPlacement::Release);
        }
        nr::rhi::CommandRecorder::end(commandBuffer);

        auto submitBatch = nr::rhi::CommandBatch{};
        submitBatch.addCommandBuffer(commandBuffer);
        if (planBatch.waitsForPreviousBatch && previousSignalToken.valid())
        {
            submitBatch.addWait(timelines->get().semaphore(previousSignalToken.queue), planBatch.waitStageMask,
                                previousSignalToken.value);
        }

        auto signalToken = timelines->get().acquireSignalToken(planBatch.queue);
        submitBatch.addSignal(timelines->get().semaphore(signalToken.queue), signalToken.value, 0,
                              vk::PipelineStageFlagBits2::eAllCommands);
        previousSignalToken = signalToken;

        attachFrameBoundaryMetadata(submitBatch, context, frameBoundaryFrameID, false, {});
        context.device.submitFrameBatch(std::move(submitBatch), toQueueRole(planBatch.queue), false);
        ++report.submittedBatchCount;
    });
    recordTiming(&ExecutorBenchmarkTelemetry::initialReleaseRecordSubmitMilliseconds, initialReleaseRecordSubmitStart);

    auto batchOrdinals = std::views::iota(std::size_t{0}, report.plan.batches.size());
    std::ranges::for_each(batchOrdinals, [&](std::size_t batchOrdinal) {
        const auto &planBatch = report.plan.batches[batchOrdinal];
        const auto &compiledBatch = compiled.submitBatches[batchOrdinal];
        auto const batchTimedPassOffset = timedPassOffset;
        auto const batchTimedPassCount = compiledBatch.passes.size();
        timedPassOffset += batchTimedPassCount;

        if (planBatch.acquiresSwapchainBeforeSubmit)
        {
            nrAssert(!report.swapchainImageIndex.has_value(),
                     "RenderGraphExecutor::execute encountered more than one swapchain acquire.");
            auto const swapchainAcquireStart = timingStart();
            auto acquire = context.preAcquiredFrameImage.has_value()
                               ? *context.preAcquiredFrameImage
                               : context.device.acquireFrameImage(context.acquireTimeout);
            if (context.preAcquiredFrameImage.has_value())
            {
                nrAssert(context.device.presentationContext.hasActiveSwapchainImage(),
                         "RenderGraphExecutor::execute pre-acquired swapchain image is no longer active.");
                nrAssert(context.device.presentationContext.activeSwapchainImageIndex() == acquire.swapchainImageIndex,
                         "RenderGraphExecutor::execute pre-acquired swapchain image index no longer matches the active "
                         "image.");
            }
            report.swapchainImageIndex = acquire.swapchainImageIndex;
            report.swapchainAcquireResult = acquire.swapchainResult;
            resolveSwapchainRuntimeResources(compiled, context, acquire.swapchainImageIndex, runtimeBindings);
            recordTiming(&ExecutorBenchmarkTelemetry::swapchainAcquireMilliseconds, swapchainAcquireStart);

            auto const deferredPrepareStart = timingStart();
            nrAssert(
                !prepared.firstDeferredPrepareBatch.has_value() || *prepared.firstDeferredPrepareBatch == batchOrdinal,
                "RenderGraphExecutor::execute deferred prepare boundary does not match the swapchain acquire batch.");
            report.invokedPassPrepareCount += invokePassPrepareCallbacks(compiled, context, runtimeBindings,
                                                                         batchOrdinal, compiled.submitBatches.size());
            deferredPrepareInvoked = true;
            recordTiming(&ExecutorBenchmarkTelemetry::deferredPrepareMilliseconds, deferredPrepareStart);
        }

        auto const taskPlanLaunchStart = timingStart();
        auto recordTasks =
            launchRecordTasksForBatch(context, frameSlot, batchOrdinal, compiled, compiledResourceByHandle,
                                      runtimeBindings, frameDataByHandle, report);
        recordTiming(&ExecutorBenchmarkTelemetry::taskPlanLaunchMilliseconds, taskPlanLaunchStart);

        auto const primaryRecordBeforeCollectStart = timingStart();
        auto &commandBuffer = primaryCommandBufferForQueue(context, frameSlot, planBatch.queue, batchOrdinal);

        commandBuffer.reset();
        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        {
            auto batchDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
                commandBuffer,
                detail::rendererBatchScopeLabel(planBatch.batchIndex, planBatch.queue,
                                                compiledBatch.openedBySubmitNodeDebugName),
            };

            if (batchTimedPassCount > 0u)
            {
                commandBuffer.resetQueryPool(*frameTimingState.queryPool, beginTimingQueryForPass(batchTimedPassOffset),
                                             timingQueryCountForPassCount(batchTimedPassCount));
            }

            if (!planBatch.headAcquireTransitions.empty())
            {
                report.appliedAcquireBarrierCount += recordOwnershipTransitions(
                    commandBuffer, planBatch.headAcquireTransitions, TransitionPlacement::Acquire);
            }
            recordTiming(&ExecutorBenchmarkTelemetry::primaryRecordBeforeCollectMilliseconds,
                         primaryRecordBeforeCollectStart);

            auto const recordCompletionWaitStart = timingStart();
            auto recordResults = collectRecordTaskResults(recordTasks, batchOrdinal, report);
            recordTiming(&ExecutorBenchmarkTelemetry::recordCompletionWaitMilliseconds, recordCompletionWaitStart);

            auto const primaryReplayBarrierTimestampStart = timingStart();
            executeRecordedSecondaries(
                commandBuffer, compiledBatch,
                std::span<const RecordPassExecutionPlan>{recordTasks.passPlans.data(), recordTasks.passPlans.size()},
                std::span<const RecordTaskResult>{recordResults.data(), recordResults.size()},
                *frameTimingState.queryPool, batchTimedPassOffset, compiledResourceByHandle, runtimeBindings, report);

            if (!planBatch.tailReleaseTransitions.empty())
            {
                report.appliedReleaseBarrierCount += recordOwnershipTransitions(
                    commandBuffer, planBatch.tailReleaseTransitions, TransitionPlacement::Release);
            }
            recordTiming(&ExecutorBenchmarkTelemetry::primaryReplayBarrierTimestampMilliseconds,
                         primaryReplayBarrierTimestampStart);
        }

        auto const primaryEndAndSubmitBuildStart = timingStart();
        nr::rhi::CommandRecorder::end(commandBuffer);

        auto submitBatch = nr::rhi::CommandBatch{};
        submitBatch.addCommandBuffer(commandBuffer);

        if (timelinesValid)
        {
            struct PendingSubmitWait
            {
                std::uint64_t value = 0;
                vk::PipelineStageFlags2 stages{};
            };

            auto pendingWaits = std::map<QueueDomain, PendingSubmitWait>{};
            auto mergeWait = [&](RendererSubmitToken token, vk::PipelineStageFlags2 stages) {
                auto &pendingWait = pendingWaits[token.queue];
                pendingWait.value = std::max(pendingWait.value, token.value);
                pendingWait.stages |= stages;
            };

            std::ranges::for_each(planBatch.initialResourceWaits,
                                  [&](const ExecutorTimelineWait &wait) { mergeWait(wait.token, wait.stageMask); });
            if (planBatch.waitsForPreviousBatch && previousSignalToken.valid())
            {
                mergeWait(previousSignalToken, planBatch.waitStageMask);
            }
            std::ranges::for_each(pendingWaits, [&](const auto &pair) {
                submitBatch.addWait(timelines->get().semaphore(pair.first), pair.second.stages, pair.second.value);
            });

            auto signalToken = timelines->get().acquireSignalToken(planBatch.queue);
            submitBatch.addSignal(timelines->get().semaphore(signalToken.queue), signalToken.value, 0,
                                  vk::PipelineStageFlagBits2::eAllCommands);
            signalTokenByBatch.insert_or_assign(compiledBatch.batchIndex, signalToken);
            if (planBatch.signalsNextBatch)
            {
                previousSignalToken = signalToken;
            }
        }

        auto submitRole = toQueueRole(planBatch.queue);
        auto imageAvailableWaitStage = planBatch.signalsPresent
                                           ? imageAvailableWaitStageForBatch(compiledBatch, compiledResourceByHandle)
                                           : vk::PipelineStageFlags2{};
        attachFrameBoundaryMetadata(submitBatch, context, frameBoundaryFrameID, planBatch.signalsPresent,
                                    report.swapchainImageIndex);
        recordTiming(&ExecutorBenchmarkTelemetry::primaryEndAndSubmitBuildMilliseconds, primaryEndAndSubmitBuildStart);
        auto const queueSubmitStart = timingStart();
        context.device.submitFrameBatch(std::move(submitBatch), submitRole, planBatch.signalsPresent,
                                        imageAvailableWaitStage);
        ++report.submittedBatchCount;
        report.submittedCompiledBatchIndices.push_back(compiledBatch.batchIndex);
        recordTiming(&ExecutorBenchmarkTelemetry::queueSubmitMilliseconds, queueSubmitStart);
    });

    if (report.plan.requiresSyntheticPresentBatch)
    {
        auto const syntheticPresentRecordSubmitStart = timingStart();
        auto &commandBuffer =
            primaryCommandBufferForQueue(context, frameSlot, QueueDomain::Compute,
                                         report.plan.batches.size() + report.plan.initialReleaseBatches.size());

        commandBuffer.reset();
        nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
        nr::rhi::CommandRecorder::end(commandBuffer);

        auto submitBatch = nr::rhi::CommandBatch{};
        submitBatch.addCommandBuffer(commandBuffer);

        if (timelinesValid && previousSignalToken.valid())
        {
            submitBatch.addWait(timelines->get().semaphore(previousSignalToken.queue),
                                submissionWaitStage(QueueDomain::Compute), previousSignalToken.value);
        }

        attachFrameBoundaryMetadata(submitBatch, context, frameBoundaryFrameID, true, report.swapchainImageIndex);
        context.device.submitFrameBatch(std::move(submitBatch), nr::rhi::QueueRole::Compute, true);
        ++report.submittedBatchCount;
        recordTiming(&ExecutorBenchmarkTelemetry::syntheticPresentRecordSubmitMilliseconds,
                     syntheticPresentRecordSubmitStart);
    }

    auto const finalizationStart = timingStart();
    updateRetainedExternalResourceStates(compiled, signalTokenByBatch);

    nrAssert(!prepared.firstDeferredPrepareBatch.has_value() || deferredPrepareInvoked,
             "RenderGraphExecutor::execute did not reach the deferred swapchain prepare boundary.");

    frameTimingState.pendingFrameOrdinal = context.frameOrdinal;
    frameTimingState.pendingPasses = std::move(currentTimingSamples);
    if (telemetry.has_value())
    {
        telemetry->get().recordTaskCount = report.submittedRecordTaskCount;
        telemetry->get().replayedSecondaryCommandBufferCount = report.replayedSecondaryCommandBufferCount;
        telemetry->get().queueSubmitCount = report.submittedBatchCount;
    }
    recordTiming(&ExecutorBenchmarkTelemetry::finalizationMilliseconds, finalizationStart);

    return report;
}

void RenderGraphExecutor::clearRetainedState()
{
    primaryCommandBuffersByFrame_.clear();
    secondaryCommandBuffersByFrame_.clear();
    gpuPassTimingStatesByFrame_.clear();
}

[[nodiscard]] nr::rhi::QueueRole RenderGraphExecutor::toQueueRole(QueueDomain queue)
{
    return rhiQueueRoleFromDomain(queue);
}

void RenderGraphExecutor::updateRetainedExternalResourceStates(
    const CompiledGraphFrame &compiled, const std::map<std::uint32_t, RendererSubmitToken> &signalTokenByBatch)
{
    auto finalBatchByResource = std::map<GraphResourceHandle, std::uint32_t>{};
    std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch &batch) {
        std::ranges::for_each(batch.passes, [&](const CompiledPass &pass) {
            std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc &use) {
                finalBatchByResource.insert_or_assign(use.resource, batch.batchIndex);
            });
        });
    });

    std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc &resource) {
        if (!resource.finalAccessScope.resolved())
        {
            return;
        }

        auto commonState = std::optional<std::reference_wrapper<RetainedExternalResourceState>>{};
        if (resource.retainedBufferState.has_value())
        {
            commonState = std::ref(resource.retainedBufferState->get().common);
        }
        else if (resource.retainedImageState.has_value())
        {
            commonState = std::ref(resource.retainedImageState->get().common);
            resource.retainedImageState->get().layout = resource.finalLayout;
        }
        else if (resource.retainedAccelerationStructureState.has_value())
        {
            commonState = std::ref(resource.retainedAccelerationStructureState->get().common);
        }
        if (!commonState.has_value())
        {
            return;
        }

        auto &state = commonState->get();
        state.initialized = true;
        state.ownership = resource.finalOwnership;
        state.access = resource.finalAccessScope;
        state.lastSubmissionTimelineValue = 0;

        auto finalBatchIt = finalBatchByResource.find(resource.handle);
        if (finalBatchIt == finalBatchByResource.end())
        {
            return;
        }

        auto signalTokenIt = signalTokenByBatch.find(finalBatchIt->second);
        if (signalTokenIt == signalTokenByBatch.end())
        {
            return;
        }

        nrAssert(ownershipDomainFromQueue(signalTokenIt->second.queue) == resource.finalOwnership,
                 "RenderGraphExecutor retained resource final ownership must match its final submit queue.");
        state.lastSubmissionTimelineValue = signalTokenIt->second.value;
    });
}

void RenderGraphExecutor::attachFrameBoundaryMetadata(nr::rhi::CommandBatch &submitBatch, const ExecuteContext &context,
                                                      std::uint64_t frameBoundaryFrameID, bool isFrameEnd,
                                                      std::optional<std::uint32_t> swapchainImageIndex)
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
    if (isFrameEnd && swapchainImageIndex.has_value())
    {
        swapchainImages[0] = context.device.presentationContext.swapchainImage(*swapchainImageIndex);
        imageSpan = std::span<const vk::Image>{swapchainImages.data(), swapchainImages.size()};
    }

    submitBatch.setFrameBoundary(frameBoundaryFrameID, flags, imageSpan);
}

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::submissionWaitStage(QueueDomain queue)
{
    if (queue == QueueDomain::Graphics)
    {
        return vk::PipelineStageFlagBits2::eAllCommands;
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
        return vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
    }
    if (queue == QueueDomain::Graphics)
    {
        return vk::PipelineStageFlagBits2::eFragmentShader;
    }
    return vk::PipelineStageFlagBits2::eTransfer;
}

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::imageAccessWaitStage(
    QueueDomain queue, const PassResourceUseDesc &use, vk::PipelineStageFlags2 passShaderStages)
{
    if (use.imageAccess == ImageAccessIntent::TransferRead || use.imageAccess == ImageAccessIntent::TransferWrite ||
        use.imageUsage == ImageUsageIntent::TransferSrc || use.imageUsage == ImageUsageIntent::TransferDst ||
        use.imageUsage == ImageUsageIntent::CopySource || use.imageUsage == ImageUsageIntent::CopyDestination)
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
        return vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests;
    }

    if (use.imageAccess == ImageAccessIntent::SampledRead || use.imageAccess == ImageAccessIntent::StorageRead ||
        use.imageAccess == ImageAccessIntent::StorageWrite || use.imageAccess == ImageAccessIntent::StorageReadWrite ||
        use.imageAccess == ImageAccessIntent::InputAttachmentRead || use.imageUsage == ImageUsageIntent::Sampled ||
        use.imageUsage == ImageUsageIntent::StorageRead || use.imageUsage == ImageUsageIntent::StorageWrite ||
        use.imageUsage == ImageUsageIntent::StorageReadWrite || use.imageUsage == ImageUsageIntent::InputAttachment)
    {
        if (use.shaderStages != vk::PipelineStageFlags2{})
        {
            return use.shaderStages;
        }
        if (passShaderStages != vk::PipelineStageFlags2{})
        {
            return passShaderStages;
        }
        return shaderWaitStageForQueue(queue);
    }

    return {};
}

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphExecutor::imageAvailableWaitStageForBatch(
    const CompiledSubmitBatch &batch,
    const std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>> &compiledResourceByHandle)
{
    auto firstPass = std::ranges::find_if(batch.passes, [&](const CompiledPass &pass) {
        return std::ranges::any_of(pass.resourceUses, [&](const PassResourceUseDesc &use) {
            auto resourceIt = compiledResourceByHandle.find(use.resource);
            return resourceIt != compiledResourceByHandle.end() && resourceIt->second.get().isSwapchain;
        });
    });
    if (firstPass == batch.passes.end())
    {
        return vk::PipelineStageFlagBits2::eAllCommands;
    }

    auto firstUse = std::ranges::find_if(firstPass->resourceUses, [&](const PassResourceUseDesc &use) {
        auto resourceIt = compiledResourceByHandle.find(use.resource);
        return resourceIt != compiledResourceByHandle.end() && resourceIt->second.get().isSwapchain;
    });
    nrAssert(firstUse != firstPass->resourceUses.end(),
             "RenderGraphExecutor failed to resolve the first swapchain image use.");

    auto stages = imageAccessWaitStage(firstPass->queue, *firstUse, firstPass->shaderStages);
    return stages != vk::PipelineStageFlags2{} ? stages
                                               : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
}

[[nodiscard]] std::uint32_t RenderGraphExecutor::queueFamilyIndexFor(const nr::rhi::Device &device, QueueDomain queue)
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

[[nodiscard]] bool RenderGraphExecutor::canOmitQueueFamilyOwnershipTransfer(const CompiledResourceDesc &resource,
                                                                            const ResourceStateTransition &transition,
                                                                            const nr::rhi::Device &device)
{
    auto const srcQueueFamilyIndex = queueFamilyIndexFor(device, transition.srcQueue);
    auto const dstQueueFamilyIndex = queueFamilyIndexFor(device, transition.dstQueue);
    if (srcQueueFamilyIndex == dstQueueFamilyIndex)
    {
        return true;
    }

    auto const &policy = device.queueFamilyTransferPolicy();
    if (resource.isBuffer)
    {
        if (resource.importedBufferResource.has_value() &&
            resource.importedBufferResource->get().sharingMode() == vk::SharingMode::eConcurrent)
        {
            return true;
        }

        return policy.canOmitBufferQueueFamilyTransfer(srcQueueFamilyIndex, dstQueueFamilyIndex);
    }

    if (resource.isAccelerationStructure)
    {
        if (resource.importedAccelerationStructureResource.has_value() &&
            resource.importedAccelerationStructureResource->get().storageBuffer().sharingMode() ==
                vk::SharingMode::eConcurrent)
        {
            return true;
        }

        return policy.canOmitBufferQueueFamilyTransfer(srcQueueFamilyIndex, dstQueueFamilyIndex);
    }

    if (!resource.isImage)
    {
        return false;
    }

    if (resource.importedImageResource.has_value())
    {
        auto const &image = resource.importedImageResource->get();
        if (image.sharingMode() == vk::SharingMode::eConcurrent)
        {
            return true;
        }

        return policy.canOmitImageQueueFamilyTransfer(image, srcQueueFamilyIndex, dstQueueFamilyIndex,
                                                      resource.isSwapchain);
    }

    auto const isManagedTransient =
        resource.lifetime == ResourceLifetime::GraphTransient && resource.residency == ResourceResidency::Managed;
    if (!isManagedTransient || resource.isSwapchain)
    {
        return false;
    }

    return policy.canOmitImageQueueFamilyTransfer(srcQueueFamilyIndex, dstQueueFamilyIndex, vk::ImageTiling::eOptimal,
                                                  resource.resolvedImageUsage);
}
} // namespace nr::renderer
