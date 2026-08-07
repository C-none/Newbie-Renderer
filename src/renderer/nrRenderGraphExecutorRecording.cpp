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
[[nodiscard]] std::uint32_t secondaryPoolSlotForRecordWorker(std::uint32_t recordWorkerId) noexcept
{
    return recordWorkerId + kWorkerSecondaryPoolSlotBase;
}
} // namespace nr::renderer::detail

namespace nr::renderer
{
[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(
    const PassRecordContext &recordContext)
{
    return [&recordContext](const nr::rhi::LogicalResourceDescriptorWrite &logicalResource,
                            const nr::rhi::DescriptorBindingInfo &binding,
                            std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, recordContext);
    };
}

[[nodiscard]] PassRecordContext RenderGraphExecutor::makePassRecordContext(
    const CompiledPass &pass, std::optional<std::reference_wrapper<const vk::raii::CommandBuffer>> commandBuffer,
    std::uint32_t frameIndex, nr::rhi::Device &device, const RuntimeBindingMap &runtimeBindings,
    const CompiledFrameDataLookup &frameDataByHandle)
{
    auto const *passPtr = std::addressof(pass);
    auto const *runtimeBindingsPtr = std::addressof(runtimeBindings);
    auto const *frameDataByHandlePtr = std::addressof(frameDataByHandle);

    return PassRecordContext{
        .commandBuffer = commandBuffer,
        .frameIndex = frameIndex,
        .device = std::ref(device),
        .resolveBuffer = [passPtr, runtimeBindingsPtr](GraphResourceHandle handle)
            -> std::optional<PassBufferResource> {
        nrAssert(passDeclaresResource(*passPtr, handle),
                 "RenderGraph pass '{}' record resolver rejected undeclared resource handle {} (pass handle {}).",
                 passPtr->debugName, handle.value, passPtr->handle.value);
            auto bindingIt = runtimeBindingsPtr->find(handle);
            if (bindingIt == runtimeBindingsPtr->end() || !bindingIt->second.isBuffer)
            {
                return std::nullopt;
            }

            return PassBufferResource{
                .buffer = bindingIt->second.buffer,
                .size = bindingIt->second.bufferSize,
                .resource = bindingIt->second.bufferResource,
            };
        },
        .resolveImage = [passPtr, runtimeBindingsPtr](GraphResourceHandle handle)
            -> std::optional<PassImageResource> {
        nrAssert(passDeclaresResource(*passPtr, handle),
                 "RenderGraph pass '{}' record resolver rejected undeclared resource handle {} (pass handle {}).",
                 passPtr->debugName, handle.value, passPtr->handle.value);
            auto bindingIt = runtimeBindingsPtr->find(handle);
            if (bindingIt == runtimeBindingsPtr->end() || !bindingIt->second.isImage)
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
        },
        .resolveAccelerationStructure =
            [passPtr, runtimeBindingsPtr](GraphResourceHandle handle)
            -> std::optional<PassAccelerationStructureResource> {
        nrAssert(passDeclaresResource(*passPtr, handle),
                 "RenderGraph pass '{}' record resolver rejected undeclared resource handle {} (pass handle {}).",
                 passPtr->debugName, handle.value, passPtr->handle.value);
            auto bindingIt = runtimeBindingsPtr->find(handle);
            if (bindingIt == runtimeBindingsPtr->end() || !bindingIt->second.isAccelerationStructure)
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
        },
        .resolveFrameDataPayload = [passPtr, frameDataByHandlePtr](GraphFrameDataHandle handle)
            -> std::optional<std::reference_wrapper<const std::any>> {
        nrAssert(passDeclaresFrameData(*passPtr, handle),
                 "RenderGraph pass '{}' record resolver rejected undeclared frame-data handle {} (pass handle {}).",
                 passPtr->debugName, handle.value, passPtr->handle.value);
            auto frameDataIt = frameDataByHandlePtr->find(handle);
            if (frameDataIt == frameDataByHandlePtr->end())
            {
                return {};
            }

            return std::cref(frameDataIt->second.get().payload);
        },
    };
}

[[nodiscard]] RecordTaskResult RenderGraphExecutor::recordPassToSecondary(const RecordTaskDesc &desc)
{
    nrAssert(desc.secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
             "RenderGraphExecutor::recordPassToSecondary requires a worker-only secondary pool slot.");

    auto result = RecordTaskResult{
        .batchOrdinal = desc.batchOrdinal,
        .passOrdinal = desc.passOrdinal,
        .chunkIndex = desc.chunkIndex,
        .chunkCount = 1,
        .workerId = desc.workerId,
        .queue = desc.queue,
        .parallel = false,
        .commandBuffer = *desc.commandBuffer.get(),
    };

    auto &commandBuffer = desc.commandBuffer.get();
    auto const &pass = desc.pass.get();
    auto const &compiledResourceByHandle = desc.compiledResourceByHandle.get();
    auto const &runtimeBindings = desc.runtimeBindings.get();
    auto const &frameDataByHandle = desc.frameDataByHandle.get();

    commandBuffer.reset();
    auto inheritanceInfo = vk::CommandBufferInheritanceInfo{};
    nr::rhi::CommandRecorder::beginSecondary(commandBuffer, inheritanceInfo,
                                             vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    {
        auto nodeDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
            commandBuffer,
            detail::nodeScopeLabel(pass.debugName),
        };
        auto passDebugLabelScope = detail::ScopedCommandBufferDebugLabel{commandBuffer, pass.debugName};

        auto inPassBarriers = nr::rhi::ops::BarrierBatch{};
        std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition &transition) {
            if (transition.strength != DependencyStrength::BarrierRequired)
            {
                return;
            }

            auto resourceIt = compiledResourceByHandle.find(transition.resource);
            nrAssert(resourceIt != compiledResourceByHandle.end(),
                     "RenderGraphExecutor::recordPassToSecondary pass barrier references an unknown resource handle.");

            auto bindingIt = runtimeBindings.find(transition.resource);
            nrAssert(
                bindingIt != runtimeBindings.end(),
                "RenderGraphExecutor::recordPassToSecondary pass barrier cannot resolve runtime resource binding.");

            auto const addedBarrier = addTransitionBarrier(
                inPassBarriers, resourceIt->second.get(), bindingIt->second, transition, TransitionPlacement::InPass,
                nr::rhi::ops::kIgnoredQueueFamilyIndex, nr::rhi::ops::kIgnoredQueueFamilyIndex);

            if (addedBarrier)
            {
                ++result.appliedInPassBarrierCount;
            }
        });

        if (!inPassBarriers.empty())
        {
            nr::rhi::ops::pipelineBarrier(commandBuffer, inPassBarriers);
        }

        if (pass.record)
        {
            auto recordContext = makePassRecordContext(pass, std::cref(commandBuffer), desc.frameIndex,
                                                       desc.device.get(), runtimeBindings, frameDataByHandle);
            pass.record(recordContext);
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

[[nodiscard]] RecordTaskResult RenderGraphExecutor::recordPassRangeToSecondary(const RecordTaskDesc &desc)
{
    nrAssert(desc.secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
             "RenderGraphExecutor::recordPassRangeToSecondary requires a worker-only secondary pool slot.");
    nrAssert(desc.parallel, "RenderGraphExecutor::recordPassRangeToSecondary requires a parallel task descriptor.");

    auto result = RecordTaskResult{
        .batchOrdinal = desc.batchOrdinal,
        .passOrdinal = desc.passOrdinal,
        .chunkIndex = desc.chunkIndex,
        .chunkCount = desc.parallelPlan.ranges.size(),
        .workerId = desc.workerId,
        .queue = desc.queue,
        .parallel = true,
        .commandBuffer = *desc.commandBuffer.get(),
    };

    auto &commandBuffer = desc.commandBuffer.get();
    auto const &pass = desc.pass.get();
    auto const &runtimeBindings = desc.runtimeBindings.get();
    auto const &frameDataByHandle = desc.frameDataByHandle.get();

    nrAssert(pass.parallelRecord.has_value() && static_cast<bool>(pass.parallelRecord->recordRange),
             "RenderGraphExecutor::recordPassRangeToSecondary requires a parallel range record callback.");

    commandBuffer.reset();

    auto inheritanceRenderingInfo = vk::CommandBufferInheritanceRenderingInfo{};
    auto inheritanceInfo = vk::CommandBufferInheritanceInfo{};
    auto beginFlags = vk::CommandBufferUsageFlags{vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
    if (desc.primaryScope.kind == PassPrimaryRecordScopeKind::DynamicRenderingSecondaryContents)
    {
        inheritanceRenderingInfo = desc.primaryScope.dynamicRendering.inheritanceRenderingInfo();
        inheritanceInfo.pNext = &inheritanceRenderingInfo;
        beginFlags |= vk::CommandBufferUsageFlagBits::eRenderPassContinue;
    }

    nr::rhi::CommandRecorder::beginSecondary(commandBuffer, inheritanceInfo, beginFlags);
    {
        auto nodeDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
            commandBuffer,
            detail::nodeScopeLabel(pass.debugName),
        };
        auto passDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
            commandBuffer,
            std::format("{}[{}]", pass.debugName, desc.chunkIndex),
        };

        auto recordContext = makePassRecordContext(pass, std::cref(commandBuffer), desc.frameIndex, desc.device.get(),
                                                   runtimeBindings, frameDataByHandle);
        pass.parallelRecord->recordRange(PassRangeRecordContext{
            .pass = std::move(recordContext),
            .commandBuffer = std::cref(commandBuffer),
            .plan = std::cref(desc.parallelPlan),
            .chunkIndex = desc.chunkIndex,
            .range = desc.range,
        });
        ++result.invokedPassRecordCount;
    }
    nr::rhi::CommandRecorder::end(commandBuffer);

    return result;
}

[[nodiscard]] std::uint32_t RenderGraphExecutor::timestampValidBitsForQueue(const nr::rhi::Device &device,
                                                                            QueueDomain queue)
{
    auto const queueFamilyIndex = queueFamilyIndexFor(device, queue);
    auto const queueFamilyProperties = device.physicalDevice.getQueueFamilyProperties();
    nrAssert(queueFamilyIndex < queueFamilyProperties.size(),
             "RenderGraphExecutor timestamp query queue family index is out of range.");

    auto const validBits = queueFamilyProperties[queueFamilyIndex].timestampValidBits;
    nrAssert(validBits > 0u,
             "RenderGraphExecutor timestamp queries require non-zero timestampValidBits for {} queue family {}.",
             detail::queueDomainLabel(queue), queueFamilyIndex);
    return validBits;
}

[[nodiscard]] std::map<QueueDomain, std::uint32_t> RenderGraphExecutor::timestampValidBitsForQueues(
    const nr::rhi::Device &device, std::span<const GpuPassTimingSample> passes)
{
    auto validBitsByQueue = std::map<QueueDomain, std::uint32_t>{};
    std::ranges::for_each(passes, [&](const GpuPassTimingSample &pass) {
        if (validBitsByQueue.contains(pass.queue))
        {
            return;
        }

        validBitsByQueue.emplace(pass.queue, timestampValidBitsForQueue(device, pass.queue));
    });
    return validBitsByQueue;
}

[[nodiscard]] std::uint64_t RenderGraphExecutor::timestampDeltaTicks(std::uint64_t begin, std::uint64_t end,
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
    const CompiledGraphFrame &compiled)
{
    auto samples = std::vector<GpuPassTimingSample>{};
    auto batchIndices = std::views::iota(std::size_t{0u}, compiled.submitBatches.size());
    std::ranges::for_each(batchIndices, [&](std::size_t batchIndex) {
        auto const &batch = compiled.submitBatches[batchIndex];
        std::ranges::for_each(batch.passes, [&](const CompiledPass &pass) {
            samples.push_back(GpuPassTimingSample{
                .pass = pass.handle,
                .debugName = pass.debugName,
                .queue = pass.queue,
                .batchIndex = batch.batchIndex,
                .isCopyPass = pass.isCopyPass,
            });
        });
    });
    return samples;
}

void RenderGraphExecutor::ensureTimingQueryPool(const nr::rhi::Device &device, FrameGpuPassTimingState &state,
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
    const nr::rhi::Device &device, FrameGpuPassTimingState &state)
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
        0u, queryCount, sizeof(std::uint64_t) * valuesPerQuery * queryCount, resultStride,
        vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWithAvailability);

    if (result != vk::Result::eSuccess && result != vk::Result::eNotReady)
    {
        nrAssert(false, "RenderGraphExecutor timestamp query read failed: {}", vk::to_string(result));
    }

    auto const validBitsByQueue = timestampValidBitsForQueues(
        device, std::span<const GpuPassTimingSample>{state.pendingPasses.data(), state.pendingPasses.size()});
    auto const timestampPeriodNanoseconds =
        static_cast<double>(device.physicalDevice.getProperties().limits.timestampPeriod);

    auto frame = GpuPassTimingFrame{
        .frameOrdinal = state.pendingFrameOrdinal,
        .expectedPassCount = state.pendingPasses.size(),
    };
    frame.passes.reserve(state.pendingPasses.size());

    auto passIndices = std::views::iota(std::size_t{0}, state.pendingPasses.size());
    std::ranges::for_each(passIndices, [&](std::size_t passIndex) {
        auto const beginQuery = beginTimingQueryForPass(passIndex);
        auto const beginOffset = beginQuery * valuesPerQuery;
        auto const endOffset = (beginQuery + 1u) * valuesPerQuery;
        nrAssert(endOffset + 1u < data.size(),
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
        auto const elapsedTicks = timestampDeltaTicks(data[beginOffset], data[endOffset], validBitsIt->second);
        sample.milliseconds = static_cast<double>(elapsedTicks) * timestampPeriodNanoseconds / 1'000'000.0;
        frame.passes.push_back(std::move(sample));
    });

    state.pendingPasses.clear();
    frame.availablePassCount = frame.passes.size();
    frame.complete = result == vk::Result::eSuccess && frame.availablePassCount == frame.expectedPassCount;

    return frame;
}

[[nodiscard]] RenderGraphExecutor::RecordBatchTasks RenderGraphExecutor::launchRecordTasksForBatch(
    const ExecuteContext &context, std::size_t frameSlot, std::size_t batchOrdinal, const CompiledGraphFrame &compiled,
    const CompiledResourceLookup &compiledResourceByHandle, const RuntimeBindingMap &runtimeBindings,
    const CompiledFrameDataLookup &frameDataByHandle, ExecuteReport &report)
{
    nrAssert(report.plan.batches.size() == compiled.submitBatches.size(),
             "RenderGraphExecutor::launchRecordTasksForBatch requires plan and compiled batch counts to match.");
    nrAssert(batchOrdinal < compiled.submitBatches.size(),
             "RenderGraphExecutor::launchRecordTasksForBatch batch ordinal is out of range.");

    auto &frame = context.device.frameManager.current();
    auto usedWorkerIds = std::set<std::uint32_t>{};
    auto recordTaskOrdinal = std::size_t{0};

    const auto &planBatch = report.plan.batches[batchOrdinal];
    const auto &compiledBatch = compiled.submitBatches[batchOrdinal];

    auto batchTasks = RecordBatchTasks{
        .batchOrdinal = batchOrdinal,
    };
    auto batchTaskDescriptions = std::vector<RecordTaskDesc>{};
    batchTaskDescriptions.reserve(compiledBatch.passes.size());
    batchTasks.passPlans.reserve(compiledBatch.passes.size());

    if (!compiledBatch.passes.empty())
    {
        auto queueWorkerCount =
            std::min(recordThreadPool_.workerCount(), preparedRecordWorkerCountForQueue(frame, planBatch.queue));
        nrAssert(queueWorkerCount > 0, "RenderGraphExecutor::launchRecordTasksForBatch requires at least one "
                                       "worker-only secondary command pool for the batch queue.");

        auto passOrdinals = std::views::iota(std::size_t{0}, compiledBatch.passes.size());
        std::ranges::for_each(passOrdinals, [&](std::size_t passOrdinal) {
            auto const &pass = compiledBatch.passes[passOrdinal];
            if (pass.parallelRecord.has_value())
            {
                auto planningContext = makePassRecordContext(pass, std::nullopt, context.frameIndex, context.device,
                                                             runtimeBindings, frameDataByHandle);
                auto const itemCount = pass.parallelRecord->itemCount(planningContext);
                auto const parallelAvailableThreadCount =
                    queueWorkerCount > 1u ? queueWorkerCount - 1u : queueWorkerCount;
                auto passPlan = RecordPassExecutionPlan{
                    .parallel = true,
                    .parallelPlan =
                        ParallelRecordPlanner::planContiguousRanges(itemCount, parallelAvailableThreadCount),
                };
                if (pass.parallelRecord->primaryScope)
                {
                    passPlan.primaryScope = pass.parallelRecord->primaryScope(planningContext);
                }

                auto chunkIndices = std::views::iota(std::size_t{0}, passPlan.parallelPlan.ranges.size());
                std::ranges::for_each(chunkIndices, [&](std::size_t chunkIndex) {
                    auto workerId = static_cast<std::uint32_t>(recordTaskOrdinal % parallelAvailableThreadCount);
                    ++recordTaskOrdinal;
                    usedWorkerIds.insert(workerId);

                    auto secondaryPoolSlot = detail::secondaryPoolSlotForRecordWorker(workerId);
                    auto &secondaryCommandBuffer = secondaryCommandBufferForPass(
                        context, frameSlot, planBatch.queue, batchOrdinal, passOrdinal, chunkIndex, secondaryPoolSlot);

                    auto desc = RecordTaskDesc{
                        .batchOrdinal = batchOrdinal,
                        .passOrdinal = passOrdinal,
                        .chunkIndex = chunkIndex,
                        .workerId = workerId,
                        .secondaryPoolSlot = secondaryPoolSlot,
                        .queue = planBatch.queue,
                        .pass = std::cref(pass),
                        .commandBuffer = std::cref(secondaryCommandBuffer),
                        .frameIndex = context.frameIndex,
                        .device = std::ref(context.device),
                        .compiledResourceByHandle = std::cref(compiledResourceByHandle),
                        .runtimeBindings = std::cref(runtimeBindings),
                        .frameDataByHandle = std::cref(frameDataByHandle),
                        .parallel = true,
                        .parallelPlan = passPlan.parallelPlan,
                        .range = passPlan.parallelPlan.ranges[chunkIndex],
                        .primaryScope = passPlan.primaryScope,
                    };

                    batchTaskDescriptions.push_back(std::move(desc));
                });

                batchTasks.passPlans.push_back(std::move(passPlan));
                return;
            }

            batchTasks.passPlans.push_back(RecordPassExecutionPlan{});

            auto workerId = static_cast<std::uint32_t>(recordTaskOrdinal % queueWorkerCount);
            ++recordTaskOrdinal;
            usedWorkerIds.insert(workerId);

            auto secondaryPoolSlot = detail::secondaryPoolSlotForRecordWorker(workerId);
            auto &secondaryCommandBuffer = secondaryCommandBufferForPass(
                context, frameSlot, planBatch.queue, batchOrdinal, passOrdinal, std::size_t{0}, secondaryPoolSlot);

            auto desc = RecordTaskDesc{
                .batchOrdinal = batchOrdinal,
                .passOrdinal = passOrdinal,
                .workerId = workerId,
                .secondaryPoolSlot = secondaryPoolSlot,
                .queue = planBatch.queue,
                .pass = std::cref(pass),
                .commandBuffer = std::cref(secondaryCommandBuffer),
                .frameIndex = context.frameIndex,
                .device = std::ref(context.device),
                .compiledResourceByHandle = std::cref(compiledResourceByHandle),
                .runtimeBindings = std::cref(runtimeBindings),
                .frameDataByHandle = std::cref(frameDataByHandle),
            };

            batchTaskDescriptions.push_back(std::move(desc));
        });
    }

    batchTasks.futures.reserve(batchTaskDescriptions.size());
    std::ranges::for_each(batchTaskDescriptions, [&](RecordTaskDesc const &desc) {
        if (desc.parallel)
        {
            batchTasks.futures.push_back(
                recordThreadPool_.submitTo(desc.workerId, [desc]() { return recordPassRangeToSecondary(desc); }));
        }
        else
        {
            batchTasks.futures.push_back(
                recordThreadPool_.submitTo(desc.workerId, [desc]() { return recordPassToSecondary(desc); }));
        }
        ++report.submittedRecordTaskCount;
    });

    report.parallelPassRecording = report.parallelPassRecording || usedWorkerIds.size() > 1u;
    return batchTasks;
}

[[nodiscard]] std::vector<RecordTaskResult> RenderGraphExecutor::collectRecordTaskResults(RecordBatchTasks &tasks,
                                                                                          std::size_t batchOrdinal,
                                                                                          ExecuteReport &report)
{
    nrAssert(tasks.batchOrdinal == batchOrdinal,
             "RenderGraphExecutor::collectRecordTaskResults received a task group for the wrong submit batch.");

    auto results = std::vector<RecordTaskResult>{};
    results.reserve(tasks.futures.size());
    std::ranges::for_each(tasks.futures, [&](std::future<RecordTaskResult> &future) {
        auto result = future.get();
        report.invokedPassRecordCount += result.invokedPassRecordCount;
        report.appliedInPassBarrierCount += result.appliedInPassBarrierCount;
        ++report.recordedSecondaryCommandBufferCount;
        results.push_back(result);
    });

    std::ranges::sort(results, [](const RecordTaskResult &lhs, const RecordTaskResult &rhs) {
        return std::tie(lhs.passOrdinal, lhs.chunkIndex) < std::tie(rhs.passOrdinal, rhs.chunkIndex);
    });
    std::ranges::for_each(results, [&](const RecordTaskResult &result) {
        nrAssert(result.batchOrdinal == batchOrdinal,
                 "RenderGraphExecutor::collectRecordTaskResults collected a task result for the wrong submit batch.");
    });

    tasks.futures.clear();
    return results;
}

void RenderGraphExecutor::executeRecordedSecondaries(const vk::raii::CommandBuffer &primaryCommandBuffer,
                                                     const CompiledSubmitBatch &batch,
                                                     std::span<const RecordPassExecutionPlan> passPlans,
                                                     std::span<const RecordTaskResult> results,
                                                     vk::QueryPool timingQueryPool, std::size_t firstTimedPassIndex,
                                                     const CompiledResourceLookup &compiledResourceByHandle,
                                                     const RuntimeBindingMap &runtimeBindings, ExecuteReport &report)
{
    if (batch.passes.empty())
    {
        return;
    }

    nrAssert(passPlans.size() == batch.passes.size(),
             "RenderGraphExecutor::executeRecordedSecondaries requires one execution plan per compiled pass.");
    nrAssert(timingQueryPool != vk::QueryPool{},
             "RenderGraphExecutor::executeRecordedSecondaries requires a valid timestamp query pool.");

    auto resultCursor = std::size_t{0};
    auto passOrdinals = std::views::iota(std::size_t{0}, batch.passes.size());
    std::ranges::for_each(passOrdinals, [&](std::size_t passOrdinal) {
        nrAssert(resultCursor == results.size() || results[resultCursor].passOrdinal >= passOrdinal,
                 "RenderGraphExecutor::executeRecordedSecondaries received a result for an earlier compiled pass.");

        auto const resultBegin = resultCursor;
        while (resultCursor < results.size() && results[resultCursor].passOrdinal == passOrdinal)
        {
            ++resultCursor;
        }
        auto passResults = results.subspan(resultBegin, resultCursor - resultBegin);
        auto const &pass = batch.passes[passOrdinal];
        auto const &passPlan = passPlans[passOrdinal];

        auto const beginQuery = beginTimingQueryForPass(firstTimedPassIndex + passOrdinal);
        primaryCommandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eTopOfPipe, timingQueryPool, beginQuery);

        if (passPlan.parallel)
        {
            nrAssert(passResults.size() == passPlan.parallelPlan.ranges.size(),
                     "RenderGraphExecutor::executeRecordedSecondaries requires one recorded secondary per planned "
                     "parallel range.");

            auto inPassBarriers = nr::rhi::ops::BarrierBatch{};
            std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition &transition) {
                if (transition.strength != DependencyStrength::BarrierRequired)
                {
                    return;
                }

                auto resourceIt = compiledResourceByHandle.find(transition.resource);
                nrAssert(resourceIt != compiledResourceByHandle.end(),
                         "RenderGraphExecutor::executeRecordedSecondaries pass barrier references an unknown resource "
                         "handle.");

                auto bindingIt = runtimeBindings.find(transition.resource);
                nrAssert(bindingIt != runtimeBindings.end(), "RenderGraphExecutor::executeRecordedSecondaries pass "
                                                             "barrier cannot resolve runtime resource binding.");

                auto const addedBarrier =
                    addTransitionBarrier(inPassBarriers, resourceIt->second.get(), bindingIt->second, transition,
                                         TransitionPlacement::InPass, nr::rhi::ops::kIgnoredQueueFamilyIndex,
                                         nr::rhi::ops::kIgnoredQueueFamilyIndex);

                if (addedBarrier)
                {
                    ++report.appliedInPassBarrierCount;
                }
            });

            if (!inPassBarriers.empty())
            {
                nr::rhi::ops::pipelineBarrier(primaryCommandBuffer, inPassBarriers);
            }

            auto commandBuffers =
                passResults | std::views::transform(&RecordTaskResult::commandBuffer) | std::ranges::to<std::vector>();
            if constexpr (nr::isDebugMode)
            {
                if (commandBuffers.size() > 1u)
                {
                    auto const rotateOffset = (firstTimedPassIndex + passOrdinal) % commandBuffers.size();
                    std::ranges::rotate(commandBuffers,
                                        commandBuffers.begin() + static_cast<std::ptrdiff_t>(rotateOffset));
                }
            }

            auto executeChunkCommands = [&]() {
                if (!commandBuffers.empty())
                {
                    primaryCommandBuffer.executeCommands(commandBuffers);
                }
            };

            if (passPlan.primaryScope.kind == PassPrimaryRecordScopeKind::DynamicRenderingSecondaryContents)
            {
                auto renderingScope = passPlan.primaryScope.dynamicRendering.renderingScope();
                auto scopedRendering = nr::rhi::ops::ScopedRendering(primaryCommandBuffer, renderingScope);
                executeChunkCommands();
            }
            else
            {
                executeChunkCommands();
            }
            report.replayedSecondaryCommandBufferCount += commandBuffers.size();
        }
        else
        {
            nrAssert(passResults.size() == 1u, "RenderGraphExecutor::executeRecordedSecondaries requires exactly one "
                                               "recorded secondary for a serial pass.");
            auto commandBuffers = std::array{passResults.front().commandBuffer};
            primaryCommandBuffer.executeCommands(commandBuffers);
            report.replayedSecondaryCommandBufferCount += commandBuffers.size();
        }

        primaryCommandBuffer.writeTimestamp2(vk::PipelineStageFlagBits2::eBottomOfPipe, timingQueryPool,
                                             beginQuery + 1u);
    });

    nrAssert(
        resultCursor == results.size(),
        "RenderGraphExecutor::executeRecordedSecondaries received trailing task results after replaying all passes.");
}
} // namespace nr::renderer
