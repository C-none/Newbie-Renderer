module;
export module nr.renderer:renderGraphExecutor;

import dependency;
import nr.rhi;
import nr.utils;
import std;
import :renderGraphCompiler;
import :renderGraphType;
import :rendererSubmission;

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
};

struct PreparedResourceBinding
{
    bool isBuffer = false;
    bool isImage = false;

    vk::Buffer buffer = vk::Buffer{};
    vk::DeviceSize bufferSize = 0;
    std::optional<std::reference_wrapper<const nr::rhi::Buffer>> bufferResource{};
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

class RenderGraphExecutor
{
  public:
    struct ExecuteContext
    {
        nr::rhi::Device& device;
        std::uint32_t frameIndex = 0;
        std::optional<std::uint32_t> swapchainImageIndex{};
        std::optional<std::reference_wrapper<RendererSubmissionTimeline>> submissionTimeline{};

        std::map<GraphResourceHandle, std::reference_wrapper<const nr::rhi::Buffer>> importedBuffers{};
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

        class ScopedPassDebugLabel
        {
          public:
            ScopedPassDebugLabel(vk::raii::CommandBuffer& commandBuffer, std::string_view label)
                : commandBuffer_(std::ref(commandBuffer))
            {
                if (label.empty())
                {
                    return;
                }

                auto debugLabel = vk::DebugUtilsLabelEXT{};
                debugLabel.pLabelName = label.data();

                try
                {
                    commandBuffer_->get().beginDebugUtilsLabelEXT(debugLabel);
                    active_ = true;
                }
                catch (const vk::SystemError&)
                {
                }
            }

            ~ScopedPassDebugLabel()
            {
                if (!active_ || !commandBuffer_.has_value())
                {
                    return;
                }

                try
                {
                    commandBuffer_->get().endDebugUtilsLabelEXT();
                }
                catch (const vk::SystemError&)
                {
                }
            }

            ScopedPassDebugLabel(const ScopedPassDebugLabel&) = delete;
            ScopedPassDebugLabel& operator=(const ScopedPassDebugLabel&) = delete;

          private:
            std::optional<std::reference_wrapper<vk::raii::CommandBuffer>> commandBuffer_{};
            bool active_ = false;
        };

        auto const& compiled = prepared.compiled;
        auto const& runtimeBindings = prepared.runtimeBindings;

        auto frameCount = context.device.frameManager.frameCount();
        nrAssert(frameCount > 0, "RenderGraphExecutor::execute requires at least one frame context.");

        if (retainedCommandBuffersByFrame_.size() != frameCount)
        {
            retainedCommandBuffersByFrame_.clear();
            retainedCommandBuffersByFrame_.resize(frameCount);
        }

        auto frameSlot = static_cast<std::size_t>(context.frameIndex % static_cast<std::uint32_t>(frameCount));
        retainedCommandBuffersByFrame_[frameSlot].clear();

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

            auto commandBuffers = allocatePrimaryForQueue(context, planBatch.queue);
            auto& commandBuffer = commandBuffers.front();

            nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            auto rawCommandBuffer = *commandBuffer;

            static auto loggedBatchIndices = std::set<std::uint32_t>{};
            if (loggedBatchIndices.insert(planBatch.batchIndex).second)
            {
                auto queueLabel = std::string_view{"Transfer"};
                if (planBatch.queue == QueueDomain::Graphics)
                {
                    queueLabel = "Graphics";
                }
                else if (planBatch.queue == QueueDomain::Compute)
                {
                    queueLabel = "Compute";
                }

                auto passList = std::string{};
                std::ranges::for_each(compiledBatch.passes, [&](const CompiledPass& pass) {
                    if (!passList.empty())
                    {
                        passList += ",";
                    }
                    passList += pass.debugName;
                });

                auto commandBufferRaw = std::bit_cast<std::uint64_t>(static_cast<VkCommandBuffer>(rawCommandBuffer));
                nrInfo(std::format(
                    "RenderGraphExecutor batch={} queue={} cmd=0x{:x} passes=[{}]",
                    planBatch.batchIndex,
                    queueLabel,
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
                    nr::rhi::ops::pipelineBarrier(rawCommandBuffer, barriers);
                }
            }

            std::ranges::for_each(compiledBatch.passes, [&](const CompiledPass& pass) {
                auto debugLabelScope = ScopedPassDebugLabel{commandBuffer, pass.debugName};

                auto inPassBarriers = nr::rhi::ops::BarrierBatch{};
                std::ranges::for_each(pass.preBarriers, [&](const ResourceStateTransition& transition) {
                    if (transition.strength != DependencyStrength::BarrierRequired)
                    {
                        return;
                    }

                    auto resourceIt = compiledResourceByHandle.find(transition.resource);
                    nrAssert(
                        resourceIt != compiledResourceByHandle.end(),
                        "RenderGraphExecutor::execute pass barrier references an unknown resource handle.");

                    auto bindingIt = runtimeBindings.find(transition.resource);
                    nrAssert(
                        bindingIt != runtimeBindings.end(),
                        "RenderGraphExecutor::execute pass barrier cannot resolve runtime resource binding.");

                    addTransitionBarrier(
                        inPassBarriers,
                        resourceIt->second.get(),
                        bindingIt->second,
                        transition,
                        TransitionPlacement::InPass,
                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                        nr::rhi::ops::kIgnoredQueueFamilyIndex);

                    ++report.appliedInPassBarrierCount;
                });

                if (!inPassBarriers.empty())
                {
                    nr::rhi::ops::pipelineBarrier(rawCommandBuffer, inPassBarriers);
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
                        .frameIndex = context.frameIndex,
                        .device = std::ref(context.device),
                        .resolveBuffer = resolveBuffer,
                        .resolveImage = resolveImage,
                    });
                    ++report.invokedPassRecordCount;
                    return;
                }

                recordImplicitCopyPass(pass, rawCommandBuffer, runtimeBindings);
            });

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
                    nr::rhi::ops::pipelineBarrier(rawCommandBuffer, barriers);
                }
            }

            nr::rhi::CommandRecorder::end(commandBuffer);

            retainedCommandBuffersByFrame_[frameSlot].push_back(std::move(commandBuffers));
            auto& stored = retainedCommandBuffersByFrame_[frameSlot].back();

            auto submitBatch = nr::rhi::CommandBatch{};
            submitBatch.addCommandBuffer(stored.front());

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
            context.device.submitFrameBatch(submitBatch, submitRole, planBatch.signalsPresent);
            ++report.submittedBatchCount;
        });

        if (report.plan.requiresSyntheticPresentBatch)
        {
            auto commandBuffers = allocatePrimaryForQueue(context, QueueDomain::Compute);
            auto& commandBuffer = commandBuffers.front();

            nr::rhi::CommandRecorder::beginPrimary(commandBuffer, vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
            nr::rhi::CommandRecorder::end(commandBuffer);

            retainedCommandBuffersByFrame_[frameSlot].push_back(std::move(commandBuffers));
            auto& stored = retainedCommandBuffersByFrame_[frameSlot].back();

            auto submitBatch = nr::rhi::CommandBatch{};
            submitBatch.addCommandBuffer(stored.front());

            if (timelineValid && previousSignalToken.valid())
            {
                submitBatch.addWait(
                    timeline->get().semaphore(),
                    submissionWaitStage(QueueDomain::Compute),
                    previousSignalToken.value);
            }

            context.device.submitFrameBatch(submitBatch, nr::rhi::QueueRole::Compute, true);
            ++report.submittedBatchCount;
        }

        return report;
    }

    void clearRetainedState()
    {
        retainedCommandBuffersByFrame_.clear();
    }

  private:
    enum class TransitionPlacement : std::uint8_t
    {
        InPass,
        Release,
        Acquire,
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
            auto binding = PreparedResourceBinding{
                .isBuffer = resource.isBuffer,
                .isImage = resource.isImage,
                .buffer = vk::Buffer{},
                .bufferSize = resource.resolvedBufferSize,
                .bufferResource = std::nullopt,
                .image = vk::Image{},
                .imageView = vk::ImageView{},
                .imageResource = std::nullopt,
                .extent = resource.resolvedExtent,
                .subresourceRange = subresourceRangeFor(resource),
            };

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
                        nr::rhi::MemoryUsage::GpuOnly,
                        context.frameIndex,
                        resource.debugName);
                    binding.buffer = buffer.handle();
                    binding.bufferSize = buffer.size();
                    binding.bufferResource = std::cref(buffer);
                }
                else
                {
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
                    else
                    {
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

    [[nodiscard]] static vk::raii::CommandBuffers allocatePrimaryForQueue(const ExecuteContext& context, QueueDomain queue)
    {
        auto& frame = context.device.frameManager.current();
        auto& pool = primaryPoolForQueue(frame, queue);
        return pool.allocatePrimary(1);
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

        auto srcStageMask = vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        auto srcAccessMask = vk::AccessFlags2{kReadWriteAccess};
        auto dstStageMask = vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        auto dstAccessMask = vk::AccessFlags2{kReadWriteAccess};

        if (placement == TransitionPlacement::Release)
        {
            dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
            dstAccessMask = vk::AccessFlags2{};
        }
        else if (placement == TransitionPlacement::Acquire)
        {
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
        vk::CommandBuffer commandBuffer,
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

        commandBuffer.copyImage(srcBinding.image, srcLayout, dstBinding.image, dstLayout, {region});

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

    std::vector<std::vector<vk::raii::CommandBuffers>> retainedCommandBuffersByFrame_{};
};
} // namespace nr::renderer
