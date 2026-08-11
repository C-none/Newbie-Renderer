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

namespace nr::renderer
{
[[nodiscard]] nr::rhi::LogicalDescriptorResolver makeDefaultLogicalDescriptorResolver(
    const PassPrepareContext &prepareContext)
{
    return [&prepareContext](const nr::rhi::LogicalResourceDescriptorWrite &logicalResource,
                             const nr::rhi::DescriptorBindingInfo &binding,
                             std::uint32_t arrayElement) -> std::optional<nr::rhi::DescriptorWritePayload> {
        return resolveLogicalDescriptorWriteDefault(logicalResource, binding, arrayElement, prepareContext);
    };
}

void RenderGraphExecutor::applyQueueFamilyTransferPolicy(CompiledGraphFrame &compiled, const nr::rhi::Device &device)
{
    auto resourceByHandle = compiled.resources | std::views::transform([](const CompiledResourceDesc &resource) {
                                return std::pair{resource.handle, std::cref(resource)};
                            }) |
                            std::ranges::to<CompiledResourceLookup>();

    auto canOmit = [&](const ResourceStateTransition &transition) {
        if (transition.strength != DependencyStrength::ReleaseAcquireRequired)
        {
            return false;
        }

        auto resourceIt = resourceByHandle.find(transition.resource);
        nrAssert(
            resourceIt != resourceByHandle.end(),
            "RenderGraphExecutor::applyQueueFamilyTransferPolicy transition references an unknown resource handle.");
        return canOmitQueueFamilyOwnershipTransfer(resourceIt->second.get(), transition, device);
    };

    auto seenResourceUses = std::set<GraphResourceHandle>{};
    std::ranges::for_each(compiled.submitBatches, [&](CompiledSubmitBatch &batch) {
        std::ranges::for_each(batch.passes, [&](CompiledPass &pass) {
            std::ranges::for_each(pass.preBarriers, [&](ResourceStateTransition &transition) {
                if (!canOmit(transition))
                {
                    return;
                }

                auto const &resource = resourceByHandle.at(transition.resource).get();
                auto const isInitialTransition = !seenResourceUses.contains(transition.resource);
                if (isInitialTransition)
                {
                    auto sourceSubmissionTimelineValue = std::uint64_t{0};
                    if (resource.retainedBufferState.has_value())
                    {
                        sourceSubmissionTimelineValue =
                            resource.retainedBufferState->get().common.lastSubmissionTimelineValue;
                    }
                    else if (resource.retainedImageState.has_value())
                    {
                        sourceSubmissionTimelineValue =
                            resource.retainedImageState->get().common.lastSubmissionTimelineValue;
                    }
                    else if (resource.retainedAccelerationStructureState.has_value())
                    {
                        sourceSubmissionTimelineValue =
                            resource.retainedAccelerationStructureState->get().common.lastSubmissionTimelineValue;
                    }
                    if (sourceSubmissionTimelineValue == 0)
                    {
                        return;
                    }
                    transition.sourceSubmissionTimelineValue = sourceSubmissionTimelineValue;
                }

                auto const needsLayoutTransition = resource.isImage && transition.oldLayout != transition.newLayout;
                transition.strength =
                    needsLayoutTransition ? DependencyStrength::BarrierRequired : DependencyStrength::InOrder;
                if (needsLayoutTransition)
                {
                    // Submission timeline waits provide the cross-queue memory
                    // dependency. Keep only the consumer-side layout transition.
                    transition.srcScope = AccessScope{
                        .stages = transition.dstScope.resolved()
                                      ? transition.dstScope.stages
                                      : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands},
                    };
                }
            });

            std::ranges::for_each(pass.resourceUses,
                                  [&](const PassResourceUseDesc &use) { seenResourceUses.insert(use.resource); });
        });
    });

    auto remainingOwnershipTransitions = std::vector<ResourceStateTransition>{};
    std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch &batch) {
        std::ranges::for_each(batch.passes, [&](const CompiledPass &pass) {
            std::ranges::copy_if(pass.preBarriers, std::back_inserter(remainingOwnershipTransitions),
                                 [](const ResourceStateTransition &transition) {
                                     return transition.strength == DependencyStrength::ReleaseAcquireRequired;
                                 });
        });
    });
    compiled.ownershipTransitions = std::move(remainingOwnershipTransitions);
}

[[nodiscard]] vk::ImageSubresourceRange RenderGraphExecutor::subresourceRangeFor(const CompiledResourceDesc &resource)
{
    return vk::ImageSubresourceRange{
        RenderGraphCompiler::mapImageAspectIntent(resource.resolvedAspect), 0, 1, 0, 1,
    };
}

[[nodiscard]] std::map<GraphResourceHandle, PreparedResourceBinding> RenderGraphExecutor::resolveRuntimeResources(
    const CompiledGraphFrame &compiled, const ExecuteContext &context)
{
    auto bindings = std::map<GraphResourceHandle, PreparedResourceBinding>{};

    std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc &resource) {
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

                auto &buffer = context.device.resourcePool.allocateTransientBuffer(
                    createInfo, resource.resolvedBufferMemoryUsage, context.frameIndex, resource.debugName);
                binding.buffer = buffer.handle();
                binding.bufferSize = buffer.size();
                binding.bufferResource = std::cref(buffer);
            }
            else if (resource.importedBufferResource.has_value())
            {
                // Use pre-allocated imported buffer from node
                const auto &buffer = resource.importedBufferResource->get();
                nrAssert(buffer.valid(),
                         "{}",
                         "RenderGraphExecutor::resolveRuntimeResources: importedBufferResource reference is invalid for "
                         "resource: " +
                             resource.debugName);
                binding.buffer = buffer.handle();
                binding.bufferSize = buffer.size();
                binding.bufferResource = std::cref(buffer);
            }
        }

        if (resource.isAccelerationStructure)
        {
            auto resolveImportedAccelerationStructure =
                [&](const nr::rhi::AccelerationStructureResource &accelerationStructure) {
                    nrAssert(accelerationStructure.valid(),
                             "{}",
                             "RenderGraphExecutor::resolveRuntimeResources: imported acceleration structure reference "
                             "is invalid for resource: " +
                                 resource.debugName);
                    binding.accelerationStructure = accelerationStructure.raw();
                    binding.accelerationStructureType = accelerationStructure.type();
                    binding.accelerationStructureSize = accelerationStructure.size();
                    binding.accelerationStructureStorageBuffer = accelerationStructure.storageBufferHandle();
                    binding.accelerationStructureStorageOffset = accelerationStructure.storageOffset();
                    binding.accelerationStructureResource = std::cref(accelerationStructure);
                };

            if (resource.importedAccelerationStructureResource.has_value())
            {
                resolveImportedAccelerationStructure(resource.importedAccelerationStructureResource->get());
            }
        }

        if (resource.isImage)
        {
            if (resource.isSwapchain)
            {
                return;
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

                    auto &image = context.device.resourcePool.allocateTransientImage(
                        createInfo, nr::rhi::MemoryUsage::GpuOnly, context.frameIndex, resource.debugName);
                    binding.image = image.handle();
                    binding.imageView = *image.view();
                    binding.imageResource = std::cref(image);
                }
                else if (resource.importedImageResource.has_value())
                {
                    // Use pre-allocated imported image from node
                    const auto &image = resource.importedImageResource->get();
                    nrAssert(image.valid(),
                             "RenderGraphExecutor::resolveRuntimeResources: importedImageResource reference is "
                             "invalid for resource: {}",
                             resource.debugName);
                    binding.image = image.handle();
                    binding.imageView = *image.view();
                    binding.imageResource = std::cref(image);
                    // Use the declared extent from the resource descriptor, not the actual
                    // image extent (which may be larger due to pre-allocation strategies).
                    // The descriptor extent represents the valid region for rendering.
                    binding.extent = resource.resolvedExtent;
                }
            }
        }

        bindings.insert_or_assign(resource.handle, binding);
    });

    return bindings;
}

void RenderGraphExecutor::resolveSwapchainRuntimeResources(const CompiledGraphFrame &compiled,
                                                           const ExecuteContext &context,
                                                           std::uint32_t swapchainImageIndex,
                                                           RuntimeBindingMap &runtimeBindings)
{
    auto const currentExtent = context.device.presentationContext.swapchainExtent();
    auto const currentFormat = context.device.presentationContext.swapchainFormat();
    std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc &resource) {
        if (!resource.isSwapchain)
        {
            return;
        }

        nrAssert(resource.resolvedFormat == currentFormat,
                 "RenderGraphExecutor swapchain format changed between graph build and acquire boundary: compiled={} "
                 "acquired={}.",
                 vk::to_string(resource.resolvedFormat), vk::to_string(currentFormat));
        nrAssert(resource.resolvedExtent.width == std::max(1u, currentExtent.width) &&
                     resource.resolvedExtent.height == std::max(1u, currentExtent.height) &&
                     resource.resolvedExtent.depth == 1u,
                 "RenderGraphExecutor swapchain extent changed between graph build and acquire boundary: compiled={}x{} "
                 "acquired={}x{}.",
                 resource.resolvedExtent.width, resource.resolvedExtent.height, currentExtent.width, currentExtent.height);

        auto binding = PreparedResourceBinding{};
        binding.isImage = true;
        binding.image = context.device.presentationContext.swapchainImage(swapchainImageIndex);
        binding.imageView = context.device.presentationContext.swapchainImageView(swapchainImageIndex);
        binding.extent = vk::Extent3D{
            std::max(1u, currentExtent.width),
            std::max(1u, currentExtent.height),
            1u,
        };
        binding.subresourceRange = subresourceRangeFor(resource);
        runtimeBindings.insert_or_assign(resource.handle, binding);
    });
}

[[nodiscard]] std::size_t RenderGraphExecutor::invokePassPrepareCallbacks(const CompiledGraphFrame &compiled,
                                                                          const ExecuteContext &context,
                                                                          const RuntimeBindingMap &runtimeBindings,
                                                                          std::size_t firstBatchOrdinal,
                                                                          std::size_t lastBatchOrdinal)
{
    nrAssert(firstBatchOrdinal <= lastBatchOrdinal && lastBatchOrdinal <= compiled.submitBatches.size(),
             "RenderGraphExecutor::invokePassPrepareCallbacks received an invalid submit-batch range.");
    auto frameDataByHandle = CompiledFrameDataLookup{};
    std::ranges::for_each(compiled.frameData, [&](const GraphFrameDataDesc &frameData) {
        frameDataByHandle.emplace(frameData.handle, std::cref(frameData));
    });

    auto invokedPrepareCount = std::size_t{0};
    auto batchOrdinals = std::views::iota(firstBatchOrdinal, lastBatchOrdinal);
    std::ranges::for_each(batchOrdinals, [&](std::size_t batchOrdinal) {
        auto const &batch = compiled.submitBatches[batchOrdinal];
        auto passOrdinals = std::views::iota(std::size_t{0u}, batch.passes.size());
        std::ranges::for_each(passOrdinals, [&](std::size_t passOrdinal) {
            auto const &pass = batch.passes[passOrdinal];
            if (!pass.prepare)
            {
                return;
            }

            auto resolveBuffer = [&](GraphResourceHandle handle) -> std::optional<PassBufferResource> {
                nrAssert(passDeclaresResource(pass, handle),
                         "RenderGraph pass '{}' prepare resolver rejected undeclared resource handle {} (pass handle {}).",
                         pass.debugName, handle.value, pass.handle.value);
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
                nrAssert(passDeclaresResource(pass, handle),
                         "RenderGraph pass '{}' prepare resolver rejected undeclared resource handle {} (pass handle {}).",
                         pass.debugName, handle.value, pass.handle.value);
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
            auto resolveAccelerationStructure =
                [&](GraphResourceHandle handle) -> std::optional<PassAccelerationStructureResource> {
                nrAssert(passDeclaresResource(pass, handle),
                         "RenderGraph pass '{}' prepare resolver rejected undeclared resource handle {} (pass handle {}).",
                         pass.debugName, handle.value, pass.handle.value);
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
            auto resolveFrameDataPayload =
                [&](GraphFrameDataHandle handle) -> std::optional<std::reference_wrapper<const std::any>> {
                nrAssert(passDeclaresFrameData(pass, handle),
                         "RenderGraph pass '{}' prepare resolver rejected undeclared frame-data handle {} (pass handle {}).",
                         pass.debugName, handle.value, pass.handle.value);
                auto frameDataIt = frameDataByHandle.find(handle);
                if (frameDataIt == frameDataByHandle.end())
                {
                    return {};
                }

                return std::cref(frameDataIt->second.get().payload);
            };

            auto prepareContext = PassPrepareContext{
                .frameIndex = context.frameIndex,
                .device = std::ref(context.device),
                .resolveBuffer = resolveBuffer,
                .resolveImage = resolveImage,
                .resolveAccelerationStructure = resolveAccelerationStructure,
                .resolveFrameDataPayload = resolveFrameDataPayload,
            };
            pass.prepare(prepareContext);
            ++invokedPrepareCount;
        });
    });

    return invokedPrepareCount;
}

[[nodiscard]] bool RenderGraphExecutor::passDeclaresResource(const CompiledPass &pass,
                                                              GraphResourceHandle handle) noexcept
{
    return std::ranges::any_of(pass.resourceUses,
                               [handle](const PassResourceUseDesc &use) { return use.resource == handle; });
}

[[nodiscard]] bool RenderGraphExecutor::passDeclaresFrameData(const CompiledPass &pass,
                                                               GraphFrameDataHandle handle) noexcept
{
    return std::ranges::contains(pass.frameDataUses, handle);
}

[[nodiscard]] nr::rhi::CommandPool &RenderGraphExecutor::primaryPoolForQueue(nr::rhi::FrameContext &frame,
                                                                             QueueDomain queue)
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

[[nodiscard]] nr::rhi::CommandPool &RenderGraphExecutor::secondaryPoolForQueue(nr::rhi::FrameContext &frame,
                                                                               QueueDomain queue,
                                                                               std::uint32_t secondaryPoolSlot)
{
    nrAssert(secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
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

[[nodiscard]] std::uint32_t RenderGraphExecutor::preparedSecondaryPoolSlotCountForQueue(nr::rhi::FrameContext &frame,
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

[[nodiscard]] std::uint32_t RenderGraphExecutor::preparedRecordWorkerCountForQueue(nr::rhi::FrameContext &frame,
                                                                                   QueueDomain queue)
{
    auto preparedPoolSlots = preparedSecondaryPoolSlotCountForQueue(frame, queue);
    if (preparedPoolSlots <= detail::kWorkerSecondaryPoolSlotBase)
    {
        return 0;
    }

    return preparedPoolSlots - detail::kWorkerSecondaryPoolSlotBase;
}

[[nodiscard]] std::uint32_t RenderGraphExecutor::resolvedRecordWorkerCount(nr::rhi::FrameContext &frame)
{
    auto preparedPoolSlots = std::max({
        static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Graphics>()),
        static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Compute>()),
        static_cast<std::uint32_t>(frame.registeredThreads<nr::rhi::QueueRole::Transfer>()),
    });
    nrAssert(preparedPoolSlots > detail::kWorkerSecondaryPoolSlotBase,
             "RenderGraphExecutor requires at least one worker-only secondary command pool beyond the main-thread slot "
             "before execute.");

    auto availableRecordWorkers = preparedPoolSlots - detail::kWorkerSecondaryPoolSlotBase;
    return nr::threading::resolveWorkerCount(0, availableRecordWorkers);
}

[[nodiscard]] vk::raii::CommandBuffer &RenderGraphExecutor::primaryCommandBufferForQueue(const ExecuteContext &context,
                                                                                         std::size_t frameSlot,
                                                                                         QueueDomain queue,
                                                                                         std::size_t ordinal)
{
    nrAssert(frameSlot < primaryCommandBuffersByFrame_.size(),
             "RenderGraphExecutor command buffer frame slot is out of range.");

    auto &frameCache = primaryCommandBuffersByFrame_[frameSlot];
    if (frameCache.size() <= ordinal)
    {
        frameCache.resize(ordinal + 1u);
    }

    auto &cached = frameCache[ordinal];
    if (cached.buffers.empty() || cached.queue != queue)
    {
        cached.queue = queue;
        auto &frame = context.device.frameManager.current();
        auto &pool = primaryPoolForQueue(frame, queue);
        cached.buffers = pool.allocatePrimary(1);
    }

    nrAssert(!cached.buffers.empty(), "RenderGraphExecutor cached command buffer allocation failed.");
    return cached.buffers.front();
}

[[nodiscard]] vk::raii::CommandBuffer &RenderGraphExecutor::secondaryCommandBufferForPass(
    const ExecuteContext &context, std::size_t frameSlot, QueueDomain queue, std::size_t batchOrdinal,
    std::size_t passOrdinal, std::size_t chunkIndex, std::uint32_t secondaryPoolSlot)
{
    nrAssert(secondaryPoolSlot >= detail::kWorkerSecondaryPoolSlotBase,
             "RenderGraphExecutor must not allocate a pass secondary command buffer from the main-thread slot.");
    nrAssert(frameSlot < secondaryCommandBuffersByFrame_.size(),
             "RenderGraphExecutor secondary command buffer frame slot is out of range.");

    auto &frameCache = secondaryCommandBuffersByFrame_[frameSlot];
    if (frameCache.size() <= batchOrdinal)
    {
        frameCache.resize(batchOrdinal + 1u);
    }

    auto &batchCache = frameCache[batchOrdinal];
    if (batchCache.size() <= passOrdinal)
    {
        batchCache.resize(passOrdinal + 1u);
    }

    auto &passCache = batchCache[passOrdinal];
    if (passCache.size() <= chunkIndex)
    {
        passCache.resize(chunkIndex + 1u);
    }

    auto &cached = passCache[chunkIndex];
    if (cached.buffers.empty() || cached.queue != queue || cached.secondaryPoolSlot != secondaryPoolSlot)
    {
        cached.queue = queue;
        cached.secondaryPoolSlot = secondaryPoolSlot;
        auto &frame = context.device.frameManager.current();
        auto &pool = secondaryPoolForQueue(frame, queue, secondaryPoolSlot);
        cached.buffers = pool.allocateSecondary(1);
    }

    nrAssert(!cached.buffers.empty(), "RenderGraphExecutor cached secondary command buffer allocation failed.");
    return cached.buffers.front();
}

bool RenderGraphExecutor::addTransitionBarrier(nr::rhi::ops::BarrierBatch &barriers,
                                               const CompiledResourceDesc &resource,
                                               const PreparedResourceBinding &binding,
                                               const ResourceStateTransition &transition, TransitionPlacement placement,
                                               std::uint32_t srcQueueFamilyIndex, std::uint32_t dstQueueFamilyIndex)
{
    constexpr auto kReadWriteAccess = vk::AccessFlagBits2::eMemoryRead | vk::AccessFlagBits2::eMemoryWrite;
    const auto isOwnershipPlacement = placement != TransitionPlacement::InPass;
    const auto sameQueueFamily = isOwnershipPlacement && srcQueueFamilyIndex == dstQueueFamilyIndex;

    // Resolve each side from the precise declared scope when available, falling
    // back to a conservative all-commands scope only for unresolved sides so
    // resources without a declared access intent stay correct.
    auto srcStageMask = transition.srcScope.resolved()
                            ? transition.srcScope.stages
                            : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
    auto srcAccessMask =
        transition.srcScope.resolved() ? transition.srcScope.access : vk::AccessFlags2{kReadWriteAccess};
    auto dstStageMask = transition.dstScope.resolved()
                            ? transition.dstScope.stages
                            : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
    auto dstAccessMask =
        transition.dstScope.resolved() ? transition.dstScope.access : vk::AccessFlags2{kReadWriteAccess};

    if (resource.isImage && transition.oldLayout == ImageLayoutIntent::Undefined)
    {
        // Undefined old layout discards prior image contents; there is no
        // producer-side access to make visible for the transition. Swapchain
        // acquire still requires an execution dependency from the semaphore
        // wait stage into the first layout transition, so keep the source
        // stage aligned with the consumer side for presentable images.
        srcStageMask =
            resource.isSwapchain ? dstStageMask : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eTopOfPipe};
        srcAccessMask = vk::AccessFlags2{};
    }

    if (placement == TransitionPlacement::Release)
    {
        // Maintenance8 makes both QFOT synchronization scopes meaningful. Keep
        // the release operation at the producer stage while leaving only the
        // producer access scope populated.
        dstStageMask = srcStageMask;
        dstAccessMask = vk::AccessFlags2{};
    }
    else if (placement == TransitionPlacement::Acquire)
    {
        // Keep the acquire operation at the consumer stage so the submission
        // semaphore can wait at the same precise stage.
        srcStageMask = dstStageMask;
        srcAccessMask = vk::AccessFlags2{};
    }

    auto barrierSrcQueue =
        placement == TransitionPlacement::InPass ? nr::rhi::ops::kIgnoredQueueFamilyIndex : srcQueueFamilyIndex;
    auto barrierDstQueue =
        placement == TransitionPlacement::InPass ? nr::rhi::ops::kIgnoredQueueFamilyIndex : dstQueueFamilyIndex;

    if (isOwnershipPlacement && !sameQueueFamily)
    {
        barriers.addDependencyFlags(vk::DependencyFlagBits::eQueueFamilyOwnershipTransferUseAllStagesKHR);
    }

    if (resource.isBuffer)
    {
        if (sameQueueFamily)
        {
            return false;
        }

        nrAssert(binding.buffer != vk::Buffer{},
                 "RenderGraphExecutor::addTransitionBarrier requires a valid buffer binding.");
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
        return true;
    }

    if (resource.isAccelerationStructure)
    {
        if (sameQueueFamily)
        {
            return false;
        }

        nrAssert(binding.accelerationStructureStorageBuffer != vk::Buffer{},
                 "RenderGraphExecutor::addTransitionBarrier requires a valid acceleration-structure storage buffer "
                 "binding.");
        auto barrierSize = binding.accelerationStructureSize > 0 ? binding.accelerationStructureSize
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
        return true;
    }

    if (resource.isImage)
    {
        if (sameQueueFamily)
        {
            if (placement == TransitionPlacement::Acquire || transition.oldLayout == transition.newLayout)
            {
                return false;
            }

            barrierSrcQueue = nr::rhi::ops::kIgnoredQueueFamilyIndex;
            barrierDstQueue = nr::rhi::ops::kIgnoredQueueFamilyIndex;
        }

        nrAssert(binding.image != vk::Image{},
                 "RenderGraphExecutor::addTransitionBarrier requires a valid image binding.");
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
        return true;
    }

    return false;
}

[[nodiscard]] vk::ImageSubresourceLayers subresourceLayersFromRange(const vk::ImageSubresourceRange &range)
{
    return vk::ImageSubresourceLayers{
        range.aspectMask,
        range.baseMipLevel,
        range.baseArrayLayer,
        std::max(range.layerCount, 1u),
    };
}

[[nodiscard]] const PreparedResourceBinding &requireBinding(
    const std::map<GraphResourceHandle, PreparedResourceBinding> &runtimeBindings, GraphResourceHandle resource,
    std::string_view operation)
{
    auto bindingIt = runtimeBindings.find(resource);
    nrAssert(bindingIt != runtimeBindings.end(), "{} failed to resolve graph resource {}.", operation, resource.value);
    return bindingIt->second;
}

[[nodiscard]] const PreparedResourceBinding &requireBufferBinding(
    const std::map<GraphResourceHandle, PreparedResourceBinding> &runtimeBindings, GraphResourceHandle resource,
    std::string_view operation)
{
    auto const &binding = requireBinding(runtimeBindings, resource, operation);
    nrAssert(binding.isBuffer && binding.buffer != vk::Buffer{}, "{} requires graph resource {} to resolve to a buffer.",
             operation, resource.value);
    return binding;
}

[[nodiscard]] const PreparedResourceBinding &requireImageBinding(
    const std::map<GraphResourceHandle, PreparedResourceBinding> &runtimeBindings, GraphResourceHandle resource,
    std::string_view operation)
{
    auto const &binding = requireBinding(runtimeBindings, resource, operation);
    nrAssert(binding.isImage && binding.image != vk::Image{}, "{} requires graph resource {} to resolve to an image.",
             operation, resource.value);
    return binding;
}

[[nodiscard]] vk::DeviceSize remainingBufferBytes(const PreparedResourceBinding &binding, vk::DeviceSize offset,
                                                  std::string_view operation)
{
    nrAssert(offset <= binding.bufferSize, "{} buffer offset {} exceeds buffer size {}.", operation, offset,
             binding.bufferSize);
    return binding.bufferSize - offset;
}

[[nodiscard]] vk::DeviceSize normalizeBufferCopySize(vk::DeviceSize requestedSize,
                                                     const PreparedResourceBinding &source, vk::DeviceSize sourceOffset,
                                                     const PreparedResourceBinding &destination,
                                                     vk::DeviceSize destinationOffset)
{
    auto const sourceRemaining = remainingBufferBytes(source, sourceOffset, "RDG copy-buffer");
    auto const destinationRemaining = remainingBufferBytes(destination, destinationOffset, "RDG copy-buffer");
    auto const maxCopySize = std::min(sourceRemaining, destinationRemaining);
    auto const size = (requestedSize == 0 || requestedSize == vk::WholeSize) ? maxCopySize : requestedSize;
    nrAssert(size <= maxCopySize, "RDG copy-buffer size {} exceeds available source/destination range {}.", size,
             maxCopySize);
    return size;
}

[[nodiscard]] vk::BufferCopy normalizeBufferCopyRegion(vk::BufferCopy region, const PreparedResourceBinding &source,
                                                       const PreparedResourceBinding &destination)
{
    region.size = normalizeBufferCopySize(region.size, source, region.srcOffset, destination, region.dstOffset);
    return region;
}

[[nodiscard]] vk::BufferImageCopy normalizeBufferImageCopyRegion(
    vk::BufferImageCopy region, const PreparedResourceBinding &image,
    vk::ImageAspectFlags aspectOverride = vk::ImageAspectFlags{})
{
    auto const fullLayers = subresourceLayersFromRange(image.subresourceRange);
    auto effectiveLayers = fullLayers;
    if (aspectOverride != vk::ImageAspectFlags{})
    {
        effectiveLayers.aspectMask = aspectOverride;
    }

    if (region.imageSubresource.aspectMask == vk::ImageAspectFlags{})
    {
        region.imageSubresource = effectiveLayers;
    }
    else if (region.imageSubresource.layerCount == 0u)
    {
        region.imageSubresource.layerCount = fullLayers.layerCount;
    }

    if (region.imageExtent == vk::Extent3D{})
    {
        region.imageExtent = image.extent;
    }

    return region;
}

template <typename TPredicate>
[[nodiscard]] vk::ImageLayout imageLayoutForUse(const CompiledPass &pass, GraphResourceHandle resource,
                                                TPredicate predicate, vk::ImageLayout fallback)
{
    auto useIt = std::ranges::find_if(
        pass.resourceUses, [&](const PassResourceUseDesc &use) { return use.resource == resource && predicate(use); });
    if (useIt == pass.resourceUses.end() || !useIt->imageLayout.has_value())
    {
        return fallback;
    }
    return RenderGraphCompiler::mapImageLayoutIntent(*useIt->imageLayout);
}

template <typename TPredicate>
[[nodiscard]] vk::ImageAspectFlags imageAspectForUse(const CompiledPass &pass, GraphResourceHandle resource,
                                                     TPredicate predicate, vk::ImageAspectFlags fallback)
{
    auto useIt = std::ranges::find_if(
        pass.resourceUses, [&](const PassResourceUseDesc &use) { return use.resource == resource && predicate(use); });
    if (useIt == pass.resourceUses.end() || !useIt->imageAspect.has_value())
    {
        return fallback;
    }
    return RenderGraphCompiler::mapImageAspectIntent(*useIt->imageAspect);
}

void recordHostReadBarrier(const vk::raii::CommandBuffer &commandBuffer, const PreparedResourceBinding &destination,
                           vk::DeviceSize offset, vk::DeviceSize size)
{
    auto const barrierSize =
        size == 0 || size == vk::WholeSize ? remainingBufferBytes(destination, offset, "RDG readback copy") : size;
    nrAssert(barrierSize <= remainingBufferBytes(destination, offset, "RDG readback copy"),
             "RDG readback copy host-read barrier range exceeds destination buffer size.");

    auto hostReadBarrier = nr::rhi::ops::BarrierBatch{};
    hostReadBarrier.add(vk::BufferMemoryBarrier2{
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eHost,
        vk::AccessFlagBits2::eHostRead,
        nr::rhi::ops::kIgnoredQueueFamilyIndex,
        nr::rhi::ops::kIgnoredQueueFamilyIndex,
        destination.buffer,
        offset,
        barrierSize,
        nullptr,
    });
    nr::rhi::ops::pipelineBarrier(commandBuffer, hostReadBarrier);
}

void recordPresentTransitionIfNeeded(const CompiledPass &pass, GraphResourceHandle destination,
                                     const PreparedResourceBinding &destinationBinding,
                                     vk::ImageLayout copyDestinationLayout,
                                     const vk::raii::CommandBuffer &commandBuffer)
{
    auto presentUse = std::ranges::find_if(pass.resourceUses, [&](const PassResourceUseDesc &use) {
        return use.resource == destination && use.imageUsage == ImageUsageIntent::PresentSource;
    });
    if (presentUse == pass.resourceUses.end())
    {
        return;
    }

    auto presentLayout = presentUse->imageLayout.has_value()
                             ? RenderGraphCompiler::mapImageLayoutIntent(*presentUse->imageLayout)
                             : vk::ImageLayout::ePresentSrcKHR;
    if (copyDestinationLayout == presentLayout)
    {
        return;
    }

    auto postCopyBarriers = nr::rhi::ops::BarrierBatch{};
    postCopyBarriers.add(vk::ImageMemoryBarrier2{
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::PipelineStageFlagBits2::eBottomOfPipe,
        vk::AccessFlags2{},
        copyDestinationLayout,
        presentLayout,
        nr::rhi::ops::kIgnoredQueueFamilyIndex,
        nr::rhi::ops::kIgnoredQueueFamilyIndex,
        destinationBinding.image,
        destinationBinding.subresourceRange,
        nullptr,
    });

    nr::rhi::ops::pipelineBarrier(commandBuffer, postCopyBarriers);
}

void recordCopyPassDesc(const CopyPassDesc &copy, const CompiledPass &pass,
                        const vk::raii::CommandBuffer &commandBuffer,
                        const std::map<GraphResourceHandle, PreparedResourceBinding> &runtimeBindings)
{
    std::visit(
        [&](const auto &desc) {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, CopyBufferToBufferPassDesc>)
            {
                auto const &source = requireBufferBinding(runtimeBindings, desc.source, "RDG copy-buffer");
                auto const &destination = requireBufferBinding(runtimeBindings, desc.destination, "RDG copy-buffer");
                auto const region = normalizeBufferCopyRegion(desc.region, source, destination);
                nr::rhi::ops::copyBuffer2(commandBuffer, source.buffer, destination.buffer,
                                          nr::rhi::ops::toBufferCopy2(region));
                if (desc.destinationIntent == CopyBufferDestinationIntent::Readback)
                {
                    recordHostReadBarrier(commandBuffer, destination, region.dstOffset, region.size);
                }
            }
            else if constexpr (std::same_as<DescT, CopyBufferToImagePassDesc>)
            {
                auto const &source =
                    requireBufferBinding(runtimeBindings, desc.sourceBuffer, "RDG copy-buffer-to-image");
                auto const &destination =
                    requireImageBinding(runtimeBindings, desc.destinationImage, "RDG copy-buffer-to-image");
                auto const destinationAspect = imageAspectForUse(
                    pass, desc.destinationImage,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferDst ||
                               use.imageUsage == ImageUsageIntent::CopyDestination;
                    },
                    destination.subresourceRange.aspectMask);
                auto const region = normalizeBufferImageCopyRegion(desc.region, destination, destinationAspect);
                auto const dstLayout = imageLayoutForUse(
                    pass, desc.destinationImage,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferDst ||
                               use.imageUsage == ImageUsageIntent::CopyDestination;
                    },
                    vk::ImageLayout::eTransferDstOptimal);
                nr::rhi::ops::copyBufferToImage2(commandBuffer, source.buffer, destination.image, dstLayout,
                                                 nr::rhi::ops::toBufferImageCopy2(region));
            }
            else if constexpr (std::same_as<DescT, CopyImageToBufferPassDesc>)
            {
                auto const &source = requireImageBinding(runtimeBindings, desc.sourceImage, "RDG copy-image-to-buffer");
                auto const &destination =
                    requireBufferBinding(runtimeBindings, desc.destinationBuffer, "RDG copy-image-to-buffer");
                auto const sourceAspect = imageAspectForUse(
                    pass, desc.sourceImage,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferSrc ||
                               use.imageUsage == ImageUsageIntent::CopySource;
                    },
                    source.subresourceRange.aspectMask);
                auto const region = normalizeBufferImageCopyRegion(desc.region, source, sourceAspect);
                auto const srcLayout = imageLayoutForUse(
                    pass, desc.sourceImage,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferSrc ||
                               use.imageUsage == ImageUsageIntent::CopySource;
                    },
                    vk::ImageLayout::eTransferSrcOptimal);
                nr::rhi::ops::copyImageToBuffer2(commandBuffer, source.image, srcLayout, destination.buffer,
                                                 nr::rhi::ops::toBufferImageCopy2(region));
                if (desc.destinationIntent == CopyBufferDestinationIntent::Readback)
                {
                    recordHostReadBarrier(commandBuffer, destination, region.bufferOffset,
                                          desc.destinationBufferRangeSize);
                }
            }
            else
            {
                auto const &source = requireImageBinding(runtimeBindings, desc.source, "RDG copy-image-to-image");
                auto const &destination =
                    requireImageBinding(runtimeBindings, desc.destination, "RDG copy-image-to-image");
                auto const sourceAspect = imageAspectForUse(
                    pass, desc.source,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferSrc ||
                               use.imageUsage == ImageUsageIntent::CopySource;
                    },
                    source.subresourceRange.aspectMask);
                auto const destinationAspect = imageAspectForUse(
                    pass, desc.destination,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferDst ||
                               use.imageUsage == ImageUsageIntent::CopyDestination;
                    },
                    destination.subresourceRange.aspectMask);
                auto const srcLayout = imageLayoutForUse(
                    pass, desc.source,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferSrc ||
                               use.imageUsage == ImageUsageIntent::CopySource;
                    },
                    vk::ImageLayout::eTransferSrcOptimal);
                auto const dstLayout = imageLayoutForUse(
                    pass, desc.destination,
                    [](const PassResourceUseDesc &use) {
                        return use.imageUsage == ImageUsageIntent::TransferDst ||
                               use.imageUsage == ImageUsageIntent::CopyDestination;
                    },
                    vk::ImageLayout::eTransferDstOptimal);
                nr::rhi::ops::copyImageToImage(commandBuffer, source.image, source.extent, sourceAspect,
                                               destination.image, destination.extent, destinationAspect, srcLayout,
                                               dstLayout, desc.region);
                recordPresentTransitionIfNeeded(pass, desc.destination, destination, dstLayout, commandBuffer);
            }
        },
        copy);
}

[[nodiscard]] vk::ImageSubresourceLayers RenderGraphExecutor::toSubresourceLayers(
    const vk::ImageSubresourceRange &range)
{
    return subresourceLayersFromRange(range);
}

void RenderGraphExecutor::recordImplicitCopyPass(
    const CompiledPass &pass, const vk::raii::CommandBuffer &commandBuffer,
    const std::map<GraphResourceHandle, PreparedResourceBinding> &runtimeBindings)
{
    if (!pass.isCopyPass)
    {
        return;
    }

    if (pass.copy.has_value())
    {
        recordCopyPassDesc(*pass.copy, pass, commandBuffer, runtimeBindings);
        return;
    }

    auto srcUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc &use) {
        return use.imageUsage == ImageUsageIntent::TransferSrc || use.imageUsage == ImageUsageIntent::CopySource;
    });

    auto dstUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc &use) {
        return use.imageUsage == ImageUsageIntent::TransferDst || use.imageUsage == ImageUsageIntent::CopyDestination;
    });

    auto presentUse = std::ranges::find_if(pass.resourceUses, [](const PassResourceUseDesc &use) {
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

    auto srcLayout = srcUse->imageLayout.has_value() ? RenderGraphCompiler::mapImageLayoutIntent(*srcUse->imageLayout)
                                                     : vk::ImageLayout::eTransferSrcOptimal;
    auto dstLayout = dstUse->imageLayout.has_value() ? RenderGraphCompiler::mapImageLayoutIntent(*dstUse->imageLayout)
                                                     : vk::ImageLayout::eTransferDstOptimal;

    auto region = vk::ImageCopy{};
    region.srcSubresource = toSubresourceLayers(srcBinding.subresourceRange);
    region.dstSubresource = toSubresourceLayers(dstBinding.subresourceRange);
    region.extent = vk::Extent3D{
        std::min(srcBinding.extent.width, dstBinding.extent.width),
        std::min(srcBinding.extent.height, dstBinding.extent.height),
        std::min(srcBinding.extent.depth, dstBinding.extent.depth),
    };

    nr::rhi::ops::copyImage2(commandBuffer, srcBinding.image, srcLayout, dstBinding.image, dstLayout,
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
