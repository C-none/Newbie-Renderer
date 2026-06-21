export module nr.renderer:renderGraphExecutor;
import dependency;

import nr.rhi;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;
import :rendererSubmission;

namespace nr::renderer
{
struct RecordTaskResult
{
    std::size_t batchOrdinal = 0;
    std::size_t passOrdinal = 0;
    std::uint32_t workerId = 0;
    QueueDomain queue = QueueDomain::Graphics;
    vk::CommandBuffer commandBuffer{};
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
};
} // namespace nr::renderer

namespace nr::renderer::detail
{
using nr::rhi::ScopedCommandBufferDebugLabel;

[[nodiscard]] inline std::string_view queueDomainLabel(QueueDomain queue) noexcept
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

[[nodiscard]] inline std::string nodeScopeLabel(std::string_view passDebugName)
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

[[nodiscard]] inline std::string rendererBatchScopeLabel(std::uint32_t batchIndex, QueueDomain queue)
{
    return std::format("Renderer.Batch.{}.{}", queueDomainLabel(queue), batchIndex);
}

class RenderRecordThreadPool
{
  public:
    RenderRecordThreadPool() = default;

    RenderRecordThreadPool(const RenderRecordThreadPool&) = delete;
    RenderRecordThreadPool& operator=(const RenderRecordThreadPool&) = delete;

    ~RenderRecordThreadPool()
    {
        stop();
    }

    void ensureWorkerCount(std::uint32_t workerCount)
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

    [[nodiscard]] std::uint32_t workerCount() const noexcept
    {
        return static_cast<std::uint32_t>(workers_.size());
    }

    template <typename Fn>
    requires std::invocable<std::decay_t<Fn>&> &&
             std::same_as<std::invoke_result_t<std::decay_t<Fn>&>, RecordTaskResult>
    [[nodiscard]] std::future<RecordTaskResult> submit(std::uint32_t workerId, Fn&& fn)
    {
        nrAssert(workerId < workerCount(), "RenderRecordThreadPool::submit workerId is out of range.");
        nrAssert(!stopping_.load(), "RenderRecordThreadPool::submit cannot accept tasks after stop.");

        auto recordTask = std::packaged_task<RecordTaskResult()>{std::forward<Fn>(fn)};
        auto future = recordTask.get_future();

        auto& worker = workerQueues_[workerId];
        {
            std::scoped_lock lock(worker.mutex);
            worker.tasks.push(std::move(recordTask));
        }
        worker.wake.notify_one();
        return future;
    }

  private:
    struct WorkerQueue
    {
        std::mutex mutex{};
        std::condition_variable wake{};
        std::queue<std::packaged_task<RecordTaskResult()>> tasks{};
    };

    void stop()
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

    void workerLoop(std::uint32_t workerId, const std::stop_token& stopToken)
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

    std::array<WorkerQueue, nr::maxThreads> workerQueues_{};
    std::vector<std::jthread> workers_{};
    std::atomic_bool stopping_ = false;
};
} // namespace nr::renderer::detail

export namespace nr::renderer
{
struct ExecutorBatchPlan
{
    std::uint32_t batchIndex = 0;
    QueueDomain queue = QueueDomain::Graphics;

    std::size_t passCount = 0;
    std::size_t inPassBarrierCount = 0;

    std::vector<ResourceStateTransition> headAcquireTransitions{};
    std::vector<ResourceStateTransition> tailReleaseTransitions{};

    bool waitsForPreviousBatch = false;
    bool signalsNextBatch = false;
    bool signalsPresent = false;
};

struct ExecutorPlan
{
    std::vector<ExecutorBatchPlan> batches{};
    std::size_t totalPassCount = 0;
    std::size_t totalInPassBarrierCount = 0;
    bool finalQueueIsCompute = false;
    bool requiresSyntheticPresentBatch = false;
};

struct ExecuteReport
{
    ExecutorPlan plan{};

    std::size_t invokedPassPrepareCount = 0;
    std::size_t invokedPassRecordCount = 0;
    std::size_t appliedInPassBarrierCount = 0;
    std::size_t appliedAcquireBarrierCount = 0;
    std::size_t appliedReleaseBarrierCount = 0;
    std::size_t submittedBatchCount = 0;
    std::size_t submittedRecordTaskCount = 0;
    std::size_t recordedSecondaryCommandBufferCount = 0;
    std::size_t recordWorkerCount = 0;
    bool parallelPassRecording = false;
};

struct PreparedResourceBinding
{
    bool isBuffer = false;
    bool isImage = false;

    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize bufferSize = 0;
    std::optional<std::reference_wrapper<nr::rhi::Buffer>> bufferResource{};
    vk::Image image = vk::Image{};
    vk::ImageView imageView = vk::ImageView{};
    std::optional<std::reference_wrapper<const nr::rhi::Image>> imageResource{};

    vk::Extent3D extent{1, 1, 1};
    vk::ImageSubresourceRange subresourceRange{
        vk::ImageAspectFlagBits::eColor,
        0,
        1,
        0,
        1,
    };
};

struct PreparedGraphFrame
{
    CompiledGraphFrame compiled{};
    ExecutorPlan plan{};
    std::map<GraphResourceHandle, PreparedResourceBinding> runtimeBindings{};
    std::size_t invokedPassPrepareCount = 0;
};

template <typename TContext>
[[nodiscard]] inline std::optional<nr::rhi::DescriptorWritePayload> resolveLogicalDescriptorWriteDefault(
    const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
    const nr::rhi::DescriptorBindingInfo& binding,
    [[maybe_unused]] std::uint32_t arrayElement,
    const TContext& bindingContext)
{
    nrAssert(
        static_cast<bool>(bindingContext.resolveBuffer),
        "resolveLogicalDescriptorWriteDefault requires resolveBuffer callback.");
    nrAssert(
        static_cast<bool>(bindingContext.resolveImage),
        "resolveLogicalDescriptorWriteDefault requires resolveImage callback.");

    nrAssert(
        logicalResource.logicalResourceId <= static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()),
        std::format(
            "resolveLogicalDescriptorWriteDefault logical resource id {} exceeds GraphResourceHandle capacity.",
            logicalResource.logicalResourceId));

    auto resourceHandle = GraphResourceHandle{
        static_cast<std::uint32_t>(logicalResource.logicalResourceId),
    };

    auto resolveBufferDescriptor = [&]() -> std::optional<nr::rhi::DescriptorWritePayload> {
        auto resolvedBuffer = bindingContext.resolveBuffer(resourceHandle);
        nrAssert(
            resolvedBuffer.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical buffer '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            logicalResource.offset <= resolvedBuffer->size,
            std::format(
                "resolveLogicalDescriptorWriteDefault buffer offset out of range. name='{}' offset={} size={}",
                logicalResource.debugName,
                logicalResource.offset,
                resolvedBuffer->size));

        auto resolvedRange = resolvedBuffer->size - logicalResource.offset;

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::BufferDescriptorWrite{
                .buffer = resolvedBuffer->buffer,
                .offset = logicalResource.offset,
                .range = resolvedRange,
            }};
    };

    switch (binding.descriptorType)
    {
    case vk::DescriptorType::eUniformBuffer:
    case vk::DescriptorType::eUniformBufferDynamic:
        // Uniform descriptors use the logical offset and resolve range to [offset, bufferEnd).
        return resolveBufferDescriptor();

    case vk::DescriptorType::eStorageBuffer:
        // Storage buffer descriptors share DescriptorBufferInfo payload shape with uniforms.
        // The descriptor type itself remains `eStorageBuffer` through `binding.descriptorType`.
        return resolveBufferDescriptor();

    case vk::DescriptorType::eSampledImage:
    case vk::DescriptorType::eStorageImage:
    case vk::DescriptorType::eInputAttachment:
    {
        auto resolvedImage = bindingContext.resolveImage(resourceHandle);
        nrAssert(
            resolvedImage.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical image '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            resolvedImage->view != vk::ImageView{},
            std::format(
                "resolveLogicalDescriptorWriteDefault resolved logical image '{}' ({}) without a valid image view.",
                logicalResource.debugName,
                logicalResource.logicalResourceId));

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageView = resolvedImage->view,
                .imageLayout = logicalResource.imageLayout,
                .sampler = {},
            }};
    }
    case vk::DescriptorType::eSampler:
    {
        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageView = {},
                .imageLayout = vk::ImageLayout::eUndefined,
                .sampler = logicalResource.sampler,
            }};
    }
    case vk::DescriptorType::eCombinedImageSampler:
    {
        auto resolvedImage = bindingContext.resolveImage(resourceHandle);
        nrAssert(
            resolvedImage.has_value(),
            std::format(
                "resolveLogicalDescriptorWriteDefault failed to resolve logical combined-image '{}' ({}) for descriptor set={}, binding={}.",
                logicalResource.debugName,
                logicalResource.logicalResourceId,
                binding.set,
                binding.binding));
        nrAssert(
            resolvedImage->view != vk::ImageView{},
            std::format(
                "resolveLogicalDescriptorWriteDefault resolved logical combined-image '{}' ({}) without a valid image view.",
                logicalResource.debugName,
                logicalResource.logicalResourceId));

        return nr::rhi::DescriptorWritePayload{
            nr::rhi::ImageDescriptorWrite{
                .imageView = resolvedImage->view,
                .imageLayout = logicalResource.imageLayout,
                .sampler = logicalResource.sampler,
            }};
    }
    default:
        return std::nullopt;
    }
}

[[nodiscard]] inline nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassRecordContext& recordContext)
{
    return [&recordContext](
               const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
               const nr::rhi::DescriptorBindingInfo& binding,
               std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, recordContext);
    };
}

[[nodiscard]] inline nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(const PassPrepareContext& prepareContext)
{
    return [&prepareContext](
               const nr::rhi::LogicalResourceDescriptorWrite& logicalResource,
               const nr::rhi::DescriptorBindingInfo& binding,
               std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, prepareContext);
    };
}

class RenderGraphExecutor
{
  public:
    struct ExecuteContext
    {
        nr::rhi::Device& device;
        std::uint32_t frameIndex = 0;
        std::optional<std::uint32_t> swapchainImageIndex{};
        std::optional<std::reference_wrapper<RendererSubmissionTimeline>> submissionTimeline{};

        std::map<GraphResourceHandle, std::reference_wrapper<nr::rhi::Buffer>> importedBuffers{};
        std::map<GraphResourceHandle, vk::Image> importedImages{};
    };

    [[nodiscard]] ExecutorPlan buildPlan(const CompiledGraphFrame& compiled) const
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

    [[nodiscard]] PreparedGraphFrame prepareFrame(const CompiledGraphFrame& compiled, const ExecuteContext& context) const
    {
        return prepareFrame(CompiledGraphFrame{compiled}, context);
    }

    [[nodiscard]] PreparedGraphFrame prepareFrame(CompiledGraphFrame&& compiled, const ExecuteContext& context) const
    {
        auto prepared = PreparedGraphFrame{};
        prepared.plan = buildPlan(compiled);
        prepared.runtimeBindings = resolveRuntimeResources(compiled, context);
        prepared.invokedPassPrepareCount = invokePassPrepareCallbacks(compiled, context, prepared.runtimeBindings);
        prepared.compiled = std::move(compiled);
        return prepared;
    }

    [[nodiscard]] ExecuteReport execute(const CompiledGraphFrame& compiled, const ExecuteContext& context)
    {
        auto prepared = prepareFrame(compiled, context);
        return executePrepared(prepared, context);
    }

    [[nodiscard]] ExecuteReport executePrepared(const PreparedGraphFrame& prepared, const ExecuteContext& context)
    {
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

        auto frameSlot = static_cast<std::size_t>(context.frameIndex % static_cast<std::uint32_t>(frameCount));
        auto desiredWorkerCount = resolvedRecordWorkerCount(context.device.frameManager.current());
        recordThreadPool_.ensureWorkerCount(desiredWorkerCount);
        report.recordWorkerCount = recordThreadPool_.workerCount();

        auto compiledResourceByHandle = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>{};
        std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc& resource) {
            compiledResourceByHandle.emplace(resource.handle, std::cref(resource));
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

        auto previousSignalToken = RendererSubmitToken{};

        auto batchOrdinals = std::views::iota(std::size_t{0}, report.plan.batches.size());
        std::ranges::for_each(batchOrdinals, [&](std::size_t batchOrdinal) {
            const auto& planBatch = report.plan.batches[batchOrdinal];
            const auto& compiledBatch = compiled.submitBatches[batchOrdinal];

            auto& commandBuffer = primaryCommandBufferForQueue(context, frameSlot, planBatch.queue, batchOrdinal);

            commandBuffer.reset();
            nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            {
                auto batchDebugLabelScope = detail::ScopedCommandBufferDebugLabel{
                    commandBuffer,
                    detail::rendererBatchScopeLabel(planBatch.batchIndex, planBatch.queue),
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

                auto recordResults = recordBatchPasses(
                    context,
                    frameSlot,
                    planBatch,
                    compiledBatch,
                    batchOrdinal,
                    compiledResourceByHandle,
                    runtimeBindings,
                    report);
                executeRecordedSecondaries(
                    commandBuffer,
                    std::span<const RecordTaskResult>{recordResults.data(), recordResults.size()});

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
            }

            nr::rhi::CommandRecorder::end(commandBuffer);

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
            context.device.submitFrameBatch(submitBatch, submitRole, planBatch.signalsPresent, imageAvailableWaitStage);
            ++report.submittedBatchCount;
        });

        if (report.plan.requiresSyntheticPresentBatch)
        {
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
            context.device.submitFrameBatch(submitBatch, nr::rhi::QueueRole::Compute, true);
            ++report.submittedBatchCount;
        }

        return report;
    }

    void clearRetainedState()
    {
        primaryCommandBuffersByFrame_.clear();
        secondaryCommandBuffersByFrame_.clear();
    }

  private:
    enum class TransitionPlacement : std::uint8_t
    {
        InPass,
        Release,
        Acquire,
    };

    using RuntimeBindingMap = std::map<GraphResourceHandle, PreparedResourceBinding>;
    using CompiledResourceLookup = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>;

    struct RecordTaskDesc
    {
        std::size_t batchOrdinal = 0;
        std::size_t passOrdinal = 0;
        std::uint32_t workerId = 0;
        QueueDomain queue = QueueDomain::Graphics;
        std::reference_wrapper<const CompiledPass> pass;
        std::reference_wrapper<const vk::raii::CommandBuffer> commandBuffer;
        std::uint32_t frameIndex = 0;
        std::reference_wrapper<nr::rhi::Device> device;
        std::reference_wrapper<const CompiledResourceLookup> compiledResourceByHandle;
        std::reference_wrapper<const RuntimeBindingMap> runtimeBindings;
    };

    [[nodiscard]] static nr::rhi::QueueRole toQueueRole(QueueDomain queue)
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

    static void attachFrameBoundaryMetadata(
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

    [[nodiscard]] static vk::PipelineStageFlags2 submissionWaitStage(QueueDomain queue)
    {
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        }
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eComputeShader;
        }
        return vk::PipelineStageFlagBits2::eTransfer;
    }

    [[nodiscard]] static vk::PipelineStageFlags2 shaderWaitStageForQueue(QueueDomain queue)
    {
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eComputeShader;
        }
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eFragmentShader;
        }
        return vk::PipelineStageFlagBits2::eTransfer;
    }

    [[nodiscard]] static vk::PipelineStageFlags2 imageAccessWaitStage(
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
            use.imageUsage == ImageUsageIntent::ColorAttachment)
        {
            return vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        }

        if (use.imageAccess == ImageAccessIntent::DepthStencilRead ||
            use.imageAccess == ImageAccessIntent::DepthStencilWrite ||
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

    [[nodiscard]] static vk::PipelineStageFlags2 imageAvailableWaitStageForBatch(
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

    [[nodiscard]] static std::uint32_t queueFamilyIndexFor(const nr::rhi::Device& device, QueueDomain queue)
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

    [[nodiscard]] static vk::ImageSubresourceRange subresourceRangeFor(const CompiledResourceDesc& resource)
    {
        return vk::ImageSubresourceRange{
            RenderGraphCompiler::mapImageAspectIntent(resource.resolvedAspect),
            0,
            1,
            0,
            1,
        };
    }

    [[nodiscard]] static std::map<GraphResourceHandle, PreparedResourceBinding> resolveRuntimeResources(
        const CompiledGraphFrame& compiled,
        const ExecuteContext& context)
    {
        auto bindings = std::map<GraphResourceHandle, PreparedResourceBinding>{};

        std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc& resource) {
            auto binding = PreparedResourceBinding{};
            binding.isBuffer = resource.isBuffer;
            binding.isImage = resource.isImage;
            binding.bufferSize = resource.resolvedBufferSize;
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

    [[nodiscard]] static std::size_t invokePassPrepareCallbacks(
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
                });
                ++invokedPrepareCount;
            });
        });

        return invokedPrepareCount;
    }

    [[nodiscard]] static nr::rhi::CommandPool& primaryPoolForQueue(nr::rhi::FrameContext& frame, QueueDomain queue)
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

    [[nodiscard]] static nr::rhi::CommandPool& secondaryPoolForQueue(
        nr::rhi::FrameContext& frame,
        QueueDomain queue,
        std::uint32_t workerId)
    {
        if (queue == QueueDomain::Graphics)
        {
            return frame.secondary<nr::rhi::QueueRole::Graphics>(workerId);
        }
        if (queue == QueueDomain::Compute)
        {
            return frame.secondary<nr::rhi::QueueRole::Compute>(workerId);
        }
        return frame.secondary<nr::rhi::QueueRole::Transfer>(workerId);
    }

    [[nodiscard]] static std::uint32_t preparedSecondaryWorkerCountForQueue(
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

    [[nodiscard]] static std::uint32_t resolvedRecordWorkerCount(nr::rhi::FrameContext& frame)
    {
        auto preparedWorkers = std::max({
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Graphics>()),
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Compute>()),
            static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Transfer>()),
        });
        nrAssert(preparedWorkers > 0, "RenderGraphExecutor requires prepared secondary command pools before execute.");

        auto hardwareWorkers = std::max(1u, std::thread::hardware_concurrency());
        return std::min(nr::maxThreads, std::min(hardwareWorkers, preparedWorkers));
    }

    struct CachedPrimaryCommandBuffer
    {
        QueueDomain queue = QueueDomain::Graphics;
        vk::raii::CommandBuffers buffers{nullptr};
    };

    struct CachedSecondaryCommandBuffer
    {
        QueueDomain queue = QueueDomain::Graphics;
        std::uint32_t workerId = 0;
        vk::raii::CommandBuffers buffers{nullptr};
    };

    [[nodiscard]] vk::raii::CommandBuffer& primaryCommandBufferForQueue(
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

    [[nodiscard]] vk::raii::CommandBuffer& secondaryCommandBufferForPass(
        const ExecuteContext& context,
        std::size_t frameSlot,
        QueueDomain queue,
        std::size_t batchOrdinal,
        std::size_t passOrdinal,
        std::uint32_t workerId)
    {
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
        if (cached.buffers.empty() || cached.queue != queue || cached.workerId != workerId)
        {
            cached.queue = queue;
            cached.workerId = workerId;
            auto& frame = context.device.frameManager.current();
            auto& pool = secondaryPoolForQueue(frame, queue, workerId);
            cached.buffers = pool.allocateSecondary(1);
        }

        nrAssert(!cached.buffers.empty(), "RenderGraphExecutor cached secondary command buffer allocation failed.");
        return cached.buffers.front();
    }

    static void addTransitionBarrier(
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

    [[nodiscard]] static RecordTaskResult recordPassToSecondary(const RecordTaskDesc& desc)
    {
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

                pass.record(PassRecordContext{
                    .commandBuffer = std::cref(commandBuffer),
                    .frameIndex = desc.frameIndex,
                    .device = desc.device,
                    .resolveBuffer = resolveBuffer,
                    .resolveImage = resolveImage,
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

    [[nodiscard]] std::vector<RecordTaskResult> recordBatchPasses(
        const ExecuteContext& context,
        std::size_t frameSlot,
        const ExecutorBatchPlan& planBatch,
        const CompiledSubmitBatch& compiledBatch,
        std::size_t batchOrdinal,
        const CompiledResourceLookup& compiledResourceByHandle,
        const RuntimeBindingMap& runtimeBindings,
        ExecuteReport& report)
    {
        auto results = std::vector<RecordTaskResult>{};
        if (compiledBatch.passes.empty())
        {
            return results;
        }

        auto& frame = context.device.frameManager.current();
        auto queueWorkerCount = std::min(
            recordThreadPool_.workerCount(),
            preparedSecondaryWorkerCountForQueue(frame, planBatch.queue));
        nrAssert(queueWorkerCount > 0, "RenderGraphExecutor::recordBatchPasses requires at least one prepared worker for the batch queue.");

        report.parallelPassRecording = report.parallelPassRecording ||
                                       (queueWorkerCount > 1u && compiledBatch.passes.size() > 1u);

        auto futures = std::vector<std::future<RecordTaskResult>>{};
        futures.reserve(compiledBatch.passes.size());
        auto taskDescs = std::vector<RecordTaskDesc>{};
        taskDescs.reserve(compiledBatch.passes.size());

        auto passOrdinals = std::views::iota(std::size_t{0}, compiledBatch.passes.size());
        std::ranges::for_each(passOrdinals, [&](std::size_t passOrdinal) {
            auto workerId = static_cast<std::uint32_t>(passOrdinal % queueWorkerCount);
            auto& secondaryCommandBuffer = secondaryCommandBufferForPass(
                context,
                frameSlot,
                planBatch.queue,
                batchOrdinal,
                passOrdinal,
                workerId);

            auto desc = RecordTaskDesc{
                .batchOrdinal = batchOrdinal,
                .passOrdinal = passOrdinal,
                .workerId = workerId,
                .queue = planBatch.queue,
                .pass = std::cref(compiledBatch.passes[passOrdinal]),
                .commandBuffer = std::cref(secondaryCommandBuffer),
                .frameIndex = context.frameIndex,
                .device = std::ref(context.device),
                .compiledResourceByHandle = std::cref(compiledResourceByHandle),
                .runtimeBindings = std::cref(runtimeBindings),
            };

            taskDescs.push_back(desc);
        });

        std::ranges::for_each(taskDescs, [&](const RecordTaskDesc& desc) {
            futures.push_back(recordThreadPool_.submit(desc.workerId, [desc]() {
                return recordPassToSecondary(desc);
            }));
            ++report.submittedRecordTaskCount;
        });

        results.reserve(futures.size());
        std::ranges::for_each(futures, [&](std::future<RecordTaskResult>& future) {
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
                "RenderGraphExecutor::recordBatchPasses collected an out-of-order or mismatched record task result.");
        });

        return results;
    }

    static void executeRecordedSecondaries(
        const vk::raii::CommandBuffer& primaryCommandBuffer,
        std::span<const RecordTaskResult> results)
    {
        if (results.empty())
        {
            return;
        }

        auto commandBuffers = results |
                              std::views::transform([](const RecordTaskResult& result) {
                                  return result.commandBuffer;
                              }) |
                              std::ranges::to<std::vector>();
        primaryCommandBuffer.executeCommands(commandBuffers);
    }

    [[nodiscard]] static vk::ImageSubresourceLayers toSubresourceLayers(const vk::ImageSubresourceRange& range)
    {
        return vk::ImageSubresourceLayers{
            range.aspectMask,
            range.baseMipLevel,
            range.baseArrayLayer,
            std::max(range.layerCount, 1u),
        };
    }

    static void recordImplicitCopyPass(
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

    std::vector<std::vector<CachedPrimaryCommandBuffer>> primaryCommandBuffersByFrame_{};
    std::vector<std::vector<std::vector<CachedSecondaryCommandBuffer>>> secondaryCommandBuffersByFrame_{};
    detail::RenderRecordThreadPool recordThreadPool_{};
    std::uint64_t nextFrameBoundaryId_ = 1;
};
} // namespace nr::renderer
