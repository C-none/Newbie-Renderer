export module nr.renderer:renderGraphCompiler;
import dependency.vulkan;

import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

export namespace nr::renderer
{
class RenderGraphCompiler
{
  public:
    [[nodiscard]] static bool hasExplicitSubmitBoundariesForQueueTransitions(const RenderGraphFrameDescription& frame);

    [[nodiscard]] static vk::BufferUsageFlags mapBufferUsageIntent(BufferUsageIntent intent);

    [[nodiscard]] static vk::ImageUsageFlags mapImageUsageIntent(ImageUsageIntent intent);

    [[nodiscard]] static vk::ImageLayout mapImageLayoutIntent(ImageLayoutIntent intent);

    [[nodiscard]] static vk::ImageAspectFlags mapImageAspectIntent(ImageAspectIntent intent);

    /**
     * @brief Resolve the shader pipeline stages a given queue may issue a descriptor/buffer access from.
     *
     * Access intents do not carry per-shader-stage granularity, so graphics-queue
     * shader access widens to all graphics stages to stay correct for vertex-stage
     * reads (for example camera uniforms). Compute-domain shader work also covers
     * ray tracing dispatches; AS build operations use explicit AS access intents.
     */
    [[nodiscard]] static vk::PipelineStageFlags2 shaderStagesForQueue(QueueDomain queue);

    /**
     * @brief Strict buffer access intent -> sync2 stage+access scope.
     *
     * Mapping follows the declared intent exactly; it does not widen writes to
     * include implicit reads. An unset/None intent yields an unresolved scope.
     */
    [[nodiscard]] static AccessScope mapBufferAccessIntent(BufferAccessIntent intent, QueueDomain queue);

    /**
     * @brief Strict image access intent -> sync2 stage+access scope.
     *
     * Mapping follows the declared intent exactly; write intents are not widened
     * to include implicit reads. `PresentRead` maps to the bottom-of-pipe boundary
     * with no access, since presentation is ordered by semaphore, not a barrier.
     */
    [[nodiscard]] static AccessScope mapImageAccessIntent(ImageAccessIntent intent, QueueDomain queue);

    [[nodiscard]] static AccessScope mapAccelerationStructureAccessIntent(AccelerationStructureAccessIntent intent);

    /**
     * @brief Resolve the precise stage+access scope a pass applies to one resource use.
     *
     * Picks the buffer, image, or acceleration-structure access intent declared
     * on the use. Returns an unresolved scope when no access intent is set,
     * leaving conservative fallback to the barrier emitter.
     */
    [[nodiscard]] static AccessScope resolveUseAccessScope(const PassResourceUseDesc& use, QueueDomain queue);

    [[nodiscard]] CompiledGraphFrame compile(const RenderGraphFrameDescription& frame) const;

    [[nodiscard]] CompiledGraphFrame compileConsuming(RenderGraphFrameDescription& frame) const;

  private:
    template <bool MoveFramePayloads, typename FrameT>
    [[nodiscard]] CompiledGraphFrame compileImpl(FrameT& frame) const
    {
        nrAssert(
            hasExplicitSubmitBoundariesForQueueTransitions(frame),
            "RenderGraphCompiler::compile requires explicit submitNode boundaries before cross-queue pass transitions.");

        auto resources = compileResources<MoveFramePayloads>(frame);
        auto frameData = transferPayload<MoveFramePayloads>(frame.frameData);
        auto submitBatches = compileSubmitBatches<MoveFramePayloads>(frame, resources);

        auto compiled = CompiledGraphFrame{
            .resources = std::move(resources),
            .frameData = std::move(frameData),
            .submitBatches = std::move(submitBatches),
        };

        annotateResourceTransitions(compiled);
        compiled.debugView = makeDebugView(compiled);
        return compiled;
    }

    struct LastResourceUse
    {
        QueueDomain queue = QueueDomain::Graphics;
        std::optional<ImageLayoutIntent> layout{};
        std::uint32_t batchIndex = 0;
        ResourceOwnershipDomain ownership = ResourceOwnershipDomain::Undefined;
        AccessScope scope{};
    };

    template <bool MovePayload, typename T>
    [[nodiscard]] static std::remove_cvref_t<T> transferPayload(T& value)
    {
        if constexpr (MovePayload)
        {
            static_assert(
                !std::is_const_v<std::remove_reference_t<T>>,
                "RenderGraphCompiler::transferPayload cannot move from const input.");
            return std::move(value);
        }
        else
        {
            return value;
        }
    }

    template <typename IntentRange>
    static void mergeUsageIntents(
        CompiledResourceDesc& resource,
        const IntentRange& usageIntents)
    {
        using IntentT = std::ranges::range_value_t<IntentRange>;

        std::ranges::for_each(usageIntents, [&](IntentT intent) {
            if constexpr (std::same_as<IntentT, BufferUsageIntent>)
            {
                resource.resolvedBufferUsage |= mapBufferUsageIntent(intent);
            }
            else if constexpr (std::same_as<IntentT, ImageUsageIntent>)
            {
                resource.resolvedImageUsage |= mapImageUsageIntent(intent);
            }
        });
    }

    template <bool MoveFramePayloads, typename FrameT>
    [[nodiscard]] static std::vector<CompiledResourceDesc> compileResources(FrameT& frame)
    {
        auto resources = std::vector<CompiledResourceDesc>{};
        resources.reserve(frame.resources.size());

        std::ranges::for_each(frame.resources, [&](auto& resource) {
            auto compiledResource = CompiledResourceDesc{
                .handle = resource.handle,
            };

            std::visit(
                [&](auto& desc) {
                    using DescT = std::remove_cvref_t<decltype(desc)>;
                    compiledResource.debugName = transferPayload<MoveFramePayloads>(desc.debugName);

                    if constexpr (std::same_as<DescT, GraphImportedBufferDesc>)
                    {
                        compiledResource.isBuffer = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.resolvedBufferSize = desc.size;
                        compiledResource.initialOwnership = desc.initialOwnership;
                        compiledResource.finalOwnership = desc.initialOwnership;
                        compiledResource.importedBufferResource = desc.importedResource;
                        mergeUsageIntents(compiledResource, desc.usageIntents);
                    }
                    else if constexpr (std::same_as<DescT, GraphTransientBufferDesc>)
                    {
                        compiledResource.isBuffer = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.resolvedBufferSize = desc.size;
                        compiledResource.resolvedBufferMemoryUsage = desc.memoryUsage;
                        mergeUsageIntents(compiledResource, desc.usageIntents);
                    }
                    else if constexpr (std::same_as<DescT, GraphImportedImageDesc>)
                    {
                        compiledResource.isImage = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.resolvedExtent = desc.extent;
                        compiledResource.resolvedFormat = desc.format;
                        compiledResource.resolvedAspect = desc.aspect;
                        compiledResource.initialLayout = desc.initialLayout;
                        compiledResource.finalLayout = desc.initialLayout;
                        compiledResource.initialOwnership = desc.initialOwnership;
                        compiledResource.finalOwnership = desc.initialOwnership;
                        compiledResource.importedImageResource = desc.importedResource;
                        mergeUsageIntents(compiledResource, desc.usageIntents);
                    }
                    else if constexpr (std::same_as<DescT, GraphImportedAccelerationStructureDesc>)
                    {
                        compiledResource.isAccelerationStructure = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.initialOwnership = desc.initialOwnership;
                        compiledResource.finalOwnership = desc.initialOwnership;
                        compiledResource.importedAccelerationStructureResource = desc.importedResource;
                        if (desc.importedResource.has_value())
                        {
                            compiledResource.resolvedAccelerationStructureType = desc.importedResource->get().type();
                            compiledResource.resolvedAccelerationStructureSize = desc.importedResource->get().size();
                        }
                        else
                        {
                            compiledResource.resolvedAccelerationStructureType = desc.type;
                            compiledResource.resolvedAccelerationStructureSize = desc.size;
                        }
                    }
                    else if constexpr (std::same_as<DescT, GraphTransientImageDesc>)
                    {
                        compiledResource.isImage = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.resolvedExtent = desc.extent;
                        compiledResource.resolvedFormat = desc.format;
                        compiledResource.resolvedAspect = desc.aspect;
                        mergeUsageIntents(compiledResource, desc.usageIntents);
                    }
                    else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
                    {
                        compiledResource.isImage = true;
                        compiledResource.isSwapchain = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.resolvedExtent = desc.extent;
                        compiledResource.resolvedFormat = desc.format;
                        compiledResource.finalLayout = ImageLayoutIntent::PresentSrc;
                        compiledResource.initialOwnership = desc.initialOwnership;
                        compiledResource.finalOwnership = desc.initialOwnership;
                        compiledResource.resolvedImageUsage |= vk::ImageUsageFlagBits::eTransferDst;
                    }
                },
                resource.desc);

            resources.push_back(std::move(compiledResource));
        });

        auto indexByHandle = std::map<GraphResourceHandle, std::size_t>{};
        auto resourceIndices = std::views::iota(std::size_t{0}, resources.size());
        std::ranges::for_each(resourceIndices, [&](std::size_t index) {
            indexByHandle.emplace(resources[index].handle, index);
        });

        std::ranges::for_each(frame.passes, [&](const PassExecutionDesc& pass) {
            std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc& use) {
                auto indexIt = indexByHandle.find(use.resource);
                nrAssert(indexIt != indexByHandle.end(), "RenderGraphCompiler::compileResources found pass use for unknown resource.");
                auto& compiledResource = resources[indexIt->second];

                if (use.bufferUsage.has_value())
                {
                    compiledResource.resolvedBufferUsage |= mapBufferUsageIntent(*use.bufferUsage);
                }

                if (use.imageUsage.has_value())
                {
                    compiledResource.resolvedImageUsage |= mapImageUsageIntent(*use.imageUsage);
                }

                if (use.imageLayout.has_value())
                {
                    compiledResource.finalLayout = *use.imageLayout;
                }

                if (use.imageAspect.has_value())
                {
                    compiledResource.resolvedAspect = *use.imageAspect;
                }

                if (use.ownershipDomain != ResourceOwnershipDomain::Undefined)
                {
                    compiledResource.finalOwnership = use.ownershipDomain;
                }
                else
                {
                    compiledResource.finalOwnership = ownershipDomainFromQueue(pass.queue);
                }
            });
        });

        return resources;
    }

    [[nodiscard]] static std::vector<std::size_t> resolvePassResourceIndices(
        const std::vector<PassResourceUseDesc>& resourceUses,
        const std::map<GraphResourceHandle, std::size_t>& resourceIndexByHandle);

    template <bool MovePassPayloads, typename FrameT>
    [[nodiscard]] static std::vector<CompiledSubmitBatch> compileSubmitBatches(
        FrameT& frame,
        const std::vector<CompiledResourceDesc>& compiledResources)
    {
        using PassRef = std::conditional_t<MovePassPayloads, PassExecutionDesc, const PassExecutionDesc>;
        auto passByHandle = std::map<GraphPassHandle, std::reference_wrapper<PassRef>>{};
        std::ranges::for_each(frame.passes, [&](auto& pass) {
            passByHandle.emplace(pass.handle, std::ref(pass));
        });

        auto submitBoundaryByHandle = std::map<GraphSubmitHandle, std::reference_wrapper<const SubmitBoundaryDesc>>{};
        std::ranges::for_each(frame.submitBoundaries, [&](const SubmitBoundaryDesc& boundary) {
            submitBoundaryByHandle.emplace(boundary.handle, std::cref(boundary));
        });

        auto resourceIndexByHandle = std::map<GraphResourceHandle, std::size_t>{};
        auto resourceIndices = std::views::iota(std::size_t{0}, compiledResources.size());
        std::ranges::for_each(resourceIndices, [&](std::size_t index) {
            resourceIndexByHandle.emplace(compiledResources[index].handle, index);
        });

        auto batches = std::vector<CompiledSubmitBatch>{};
        std::optional<CompiledSubmitBatch> currentBatch{};
        std::optional<GraphSubmitHandle> pendingBoundary{};
        std::uint32_t nextBatchIndex = 0;

        auto flushCurrentBatch = [&]() {
            if (currentBatch.has_value() && !currentBatch->passes.empty())
            {
                batches.push_back(std::move(*currentBatch));
                currentBatch.reset();
            }
        };

        std::ranges::for_each(frame.executionOrder, [&](const GraphExecutionStep& step) {
            if (std::holds_alternative<GraphSubmitHandle>(step))
            {
                auto submitHandle = std::get<GraphSubmitHandle>(step);
                nrAssert(
                    submitBoundaryByHandle.contains(submitHandle),
                    "RenderGraphCompiler::compileSubmitBatches execution order references unknown submit boundary.");
                pendingBoundary = submitHandle;
                flushCurrentBatch();
                return;
            }

            auto passHandle = std::get<GraphPassHandle>(step);
            auto passIt = passByHandle.find(passHandle);
            nrAssert(passIt != passByHandle.end(), "RenderGraphCompiler::compileSubmitBatches execution order references unknown pass handle.");
            auto& pass = passIt->second.get();

            if (currentBatch.has_value() &&
                currentBatch->queue != pass.queue &&
                !pendingBoundary.has_value())
            {
                nrAssert(
                    false,
                    "RenderGraphCompiler::compileSubmitBatches requires explicit submitNode before queue-domain transition.");
            }

            auto mustStartNewBatch = !currentBatch.has_value() ||
                                     currentBatch->queue != pass.queue ||
                                     pendingBoundary.has_value();

            if (mustStartNewBatch)
            {
                flushCurrentBatch();
                auto batch = CompiledSubmitBatch{
                    .batchIndex = nextBatchIndex++,
                    .queue = pass.queue,
                };
                if (pendingBoundary.has_value())
                {
                    auto boundaryIt = submitBoundaryByHandle.find(*pendingBoundary);
                    nrAssert(
                        boundaryIt != submitBoundaryByHandle.end(),
                        "RenderGraphCompiler::compileSubmitBatches pending submit boundary disappeared.");
                    batch.openedBySubmitNode = pendingBoundary;
                    batch.openedBySubmitNodeDebugName = boundaryIt->second.get().debugName;
                }
                currentBatch = std::move(batch);
                pendingBoundary.reset();
            }

            auto resolvedResourceIndices = resolvePassResourceIndices(pass.resourceUses, resourceIndexByHandle);
            currentBatch->passes.push_back(CompiledPass{
                .handle = pass.handle,
                .node = pass.node,
                .debugName = transferPayload<MovePassPayloads>(pass.debugName),
                .isCopyPass = pass.isCopyPass,
                .queue = pass.queue,
                .submitBatchIndex = currentBatch->batchIndex,
                .resourceUses = transferPayload<MovePassPayloads>(pass.resourceUses),
                .resolvedResourceIndices = std::move(resolvedResourceIndices),
                .prepare = transferPayload<MovePassPayloads>(pass.prepare),
                .record = transferPayload<MovePassPayloads>(pass.record),
                .parallelRecord = transferPayload<MovePassPayloads>(pass.parallelRecord),
            });
        });

        flushCurrentBatch();
        return batches;
    }

    static void annotateResourceTransitions(CompiledGraphFrame& compiled);

    [[nodiscard]] static std::string_view queueName(QueueDomain queue);

    [[nodiscard]] static std::string makeDebugView(const CompiledGraphFrame& compiled);
};
} // namespace nr::renderer
