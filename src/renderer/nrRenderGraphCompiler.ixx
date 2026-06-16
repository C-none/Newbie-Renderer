export module nr.renderer:renderGraphCompiler;
import dependency;

import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

export namespace nr::renderer
{
class RenderGraphCompiler
{
  public:
    [[nodiscard]] static bool hasExplicitSubmitBoundariesForQueueTransitions(const RenderGraphFrameDescription& frame)
    {
        auto passQueueByHandle = std::map<GraphPassHandle, QueueDomain>{};
        std::ranges::for_each(frame.passes, [&](const PassExecutionDesc& pass) {
            passQueueByHandle.emplace(pass.handle, pass.queue);
        });

        auto lastQueue = std::optional<QueueDomain>{};
        auto boundaryPending = false;
        auto valid = true;

        std::ranges::for_each(frame.executionOrder, [&](const GraphExecutionStep& step) {
            if (!valid)
            {
                return;
            }

            if (std::holds_alternative<GraphSubmitHandle>(step))
            {
                boundaryPending = true;
                return;
            }

            auto passHandle = std::get<GraphPassHandle>(step);
            auto passQueueIt = passQueueByHandle.find(passHandle);
            if (passQueueIt == passQueueByHandle.end())
            {
                return;
            }

            auto currentQueue = passQueueIt->second;
            if (lastQueue.has_value() && *lastQueue != currentQueue && !boundaryPending)
            {
                valid = false;
                return;
            }

            lastQueue = currentQueue;
            boundaryPending = false;
        });

        return valid;
    }

    [[nodiscard]] static vk::BufferUsageFlags mapBufferUsageIntent(BufferUsageIntent intent)
    {
        switch (intent)
        {
        case BufferUsageIntent::TransferSrc:
            return vk::BufferUsageFlagBits::eTransferSrc;
        case BufferUsageIntent::TransferDst:
            return vk::BufferUsageFlagBits::eTransferDst;
        case BufferUsageIntent::Uniform:
            return vk::BufferUsageFlagBits::eUniformBuffer;
        case BufferUsageIntent::StorageRead:
        case BufferUsageIntent::StorageWrite:
        case BufferUsageIntent::StorageReadWrite:
            return vk::BufferUsageFlagBits::eStorageBuffer;
        case BufferUsageIntent::Vertex:
            return vk::BufferUsageFlagBits::eVertexBuffer;
        case BufferUsageIntent::Index:
            return vk::BufferUsageFlagBits::eIndexBuffer;
        case BufferUsageIntent::Indirect:
            return vk::BufferUsageFlagBits::eIndirectBuffer;
        case BufferUsageIntent::ShaderDeviceAddress:
            return vk::BufferUsageFlagBits::eShaderDeviceAddress;
        case BufferUsageIntent::UniformTexel:
            return vk::BufferUsageFlagBits::eUniformTexelBuffer;
        case BufferUsageIntent::StorageTexelRead:
        case BufferUsageIntent::StorageTexelWrite:
        case BufferUsageIntent::StorageTexelReadWrite:
            return vk::BufferUsageFlagBits::eStorageTexelBuffer;
        case BufferUsageIntent::AccelerationStructureBuildInput:
            return vk::BufferUsageFlagBits::eAccelerationStructureBuildInputReadOnlyKHR;
        case BufferUsageIntent::AccelerationStructureStorage:
            return vk::BufferUsageFlagBits::eAccelerationStructureStorageKHR;
        case BufferUsageIntent::AccelerationStructureScratch:
            return vk::BufferUsageFlagBits::eStorageBuffer;
        case BufferUsageIntent::ShaderBindingTable:
            return vk::BufferUsageFlagBits::eShaderBindingTableKHR;
        case BufferUsageIntent::HostUpload:
            return vk::BufferUsageFlagBits::eTransferSrc;
        case BufferUsageIntent::Readback:
            return vk::BufferUsageFlagBits::eTransferDst;
        }
        return {};
    }

    [[nodiscard]] static vk::ImageUsageFlags mapImageUsageIntent(ImageUsageIntent intent)
    {
        switch (intent)
        {
        case ImageUsageIntent::TransferSrc:
        case ImageUsageIntent::CopySource:
        case ImageUsageIntent::ResolveSrc:
            return vk::ImageUsageFlagBits::eTransferSrc;
        case ImageUsageIntent::TransferDst:
        case ImageUsageIntent::CopyDestination:
        case ImageUsageIntent::ResolveDst:
        case ImageUsageIntent::PresentSource:
            return vk::ImageUsageFlagBits::eTransferDst;
        case ImageUsageIntent::Sampled:
            return vk::ImageUsageFlagBits::eSampled;
        case ImageUsageIntent::StorageRead:
        case ImageUsageIntent::StorageWrite:
        case ImageUsageIntent::StorageReadWrite:
            return vk::ImageUsageFlagBits::eStorage;
        case ImageUsageIntent::ColorAttachment:
            return vk::ImageUsageFlagBits::eColorAttachment;
        case ImageUsageIntent::DepthStencilAttachment:
            return vk::ImageUsageFlagBits::eDepthStencilAttachment;
        case ImageUsageIntent::DepthStencilReadOnly:
            return vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled;
        case ImageUsageIntent::TransientAttachment:
            return vk::ImageUsageFlagBits::eTransientAttachment;
        case ImageUsageIntent::InputAttachment:
            return vk::ImageUsageFlagBits::eInputAttachment;
        }
        return {};
    }

    [[nodiscard]] static vk::ImageLayout mapImageLayoutIntent(ImageLayoutIntent intent)
    {
        switch (intent)
        {
        case ImageLayoutIntent::Undefined:
            return vk::ImageLayout::eUndefined;
        case ImageLayoutIntent::General:
            return vk::ImageLayout::eGeneral;
        case ImageLayoutIntent::TransferSrc:
            return vk::ImageLayout::eTransferSrcOptimal;
        case ImageLayoutIntent::TransferDst:
            return vk::ImageLayout::eTransferDstOptimal;
        case ImageLayoutIntent::ShaderReadOnly:
            return vk::ImageLayout::eShaderReadOnlyOptimal;
        case ImageLayoutIntent::ColorAttachment:
            return vk::ImageLayout::eColorAttachmentOptimal;
        case ImageLayoutIntent::DepthStencilAttachment:
            return vk::ImageLayout::eDepthStencilAttachmentOptimal;
        case ImageLayoutIntent::DepthStencilReadOnly:
            return vk::ImageLayout::eDepthStencilReadOnlyOptimal;
        case ImageLayoutIntent::PresentSrc:
            return vk::ImageLayout::ePresentSrcKHR;
        }
        return vk::ImageLayout::eUndefined;
    }

    [[nodiscard]] static vk::ImageAspectFlags mapImageAspectIntent(ImageAspectIntent intent)
    {
        switch (intent)
        {
        case ImageAspectIntent::Color:
            return vk::ImageAspectFlagBits::eColor;
        case ImageAspectIntent::Depth:
            return vk::ImageAspectFlagBits::eDepth;
        case ImageAspectIntent::Stencil:
            return vk::ImageAspectFlagBits::eStencil;
        case ImageAspectIntent::DepthStencil:
            return vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil;
        }
        return vk::ImageAspectFlagBits::eColor;
    }

    /**
     * @brief Resolve the shader pipeline stages a given queue may issue a descriptor/buffer access from.
     *
     * Access intents do not carry per-shader-stage granularity, so graphics-queue
     * shader access widens to all graphics stages to stay correct for vertex-stage
     * reads (for example camera uniforms). Compute narrows to the compute stage.
     */
    [[nodiscard]] static vk::PipelineStageFlags2 shaderStagesForQueue(QueueDomain queue)
    {
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eComputeShader;
        }
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eAllGraphics;
        }
        return vk::PipelineStageFlagBits2::eAllCommands;
    }

    /**
     * @brief Strict buffer access intent -> sync2 stage+access scope.
     *
     * Mapping follows the declared intent exactly; it does not widen writes to
     * include implicit reads. An unset/None intent yields an unresolved scope.
     */
    [[nodiscard]] static AccessScope mapBufferAccessIntent(BufferAccessIntent intent, QueueDomain queue)
    {
        switch (intent)
        {
        case BufferAccessIntent::None:
            return AccessScope{};
        case BufferAccessIntent::TransferRead:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};
        case BufferAccessIntent::TransferWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};
        case BufferAccessIntent::UniformRead:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eUniformRead};
        case BufferAccessIntent::ShaderSampleRead:
        case BufferAccessIntent::TexelRead:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderSampledRead};
        case BufferAccessIntent::ShaderStorageRead:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderStorageRead};
        case BufferAccessIntent::ShaderStorageWrite:
        case BufferAccessIntent::TexelWrite:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderStorageWrite};
        case BufferAccessIntent::VertexRead:
            return AccessScope{vk::PipelineStageFlagBits2::eVertexAttributeInput, vk::AccessFlagBits2::eVertexAttributeRead};
        case BufferAccessIntent::IndexRead:
            return AccessScope{vk::PipelineStageFlagBits2::eIndexInput, vk::AccessFlagBits2::eIndexRead};
        case BufferAccessIntent::IndirectRead:
            return AccessScope{vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead};
        case BufferAccessIntent::AccelerationStructureRead:
            return AccessScope{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        case BufferAccessIntent::AccelerationStructureWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
        case BufferAccessIntent::HostRead:
            return AccessScope{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        case BufferAccessIntent::HostWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite};
        }
        return AccessScope{};
    }

    /**
     * @brief Strict image access intent -> sync2 stage+access scope.
     *
     * Mapping follows the declared intent exactly; write intents are not widened
     * to include implicit reads. `PresentRead` maps to the bottom-of-pipe boundary
     * with no access, since presentation is ordered by semaphore, not a barrier.
     */
    [[nodiscard]] static AccessScope mapImageAccessIntent(ImageAccessIntent intent, QueueDomain queue)
    {
        switch (intent)
        {
        case ImageAccessIntent::None:
            return AccessScope{};
        case ImageAccessIntent::TransferRead:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};
        case ImageAccessIntent::TransferWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};
        case ImageAccessIntent::SampledRead:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderSampledRead};
        case ImageAccessIntent::StorageRead:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderStorageRead};
        case ImageAccessIntent::StorageWrite:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderStorageWrite};
        case ImageAccessIntent::StorageReadWrite:
            return AccessScope{shaderStagesForQueue(queue), vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        case ImageAccessIntent::ColorAttachmentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead};
        case ImageAccessIntent::ColorAttachmentWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite};
        case ImageAccessIntent::DepthStencilRead:
            return AccessScope{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentRead};
        case ImageAccessIntent::DepthStencilWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite};
        case ImageAccessIntent::InputAttachmentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eInputAttachmentRead};
        case ImageAccessIntent::PresentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlags2{}};
        }
        return AccessScope{};
    }

    /**
     * @brief Resolve the precise stage+access scope a pass applies to one resource use.
     *
     * Picks the buffer or image access intent declared on the use. Returns an
     * unresolved scope when no access intent is set, leaving conservative
     * fallback to the barrier emitter.
     */
    [[nodiscard]] static AccessScope resolveUseAccessScope(const PassResourceUseDesc& use, QueueDomain queue)
    {
        if (use.bufferAccess.has_value())
        {
            return mapBufferAccessIntent(*use.bufferAccess, queue);
        }
        if (use.imageAccess.has_value())
        {
            return mapImageAccessIntent(*use.imageAccess, queue);
        }
        return AccessScope{};
    }

    [[nodiscard]] CompiledGraphFrame compile(const RenderGraphFrameDescription& frame) const
    {
        nrAssert(
            hasExplicitSubmitBoundariesForQueueTransitions(frame),
            "RenderGraphCompiler::compile requires explicit submitNode boundaries before cross-queue pass transitions.");

        auto resources = compileResources(frame);
        auto submitBatches = compileSubmitBatches(frame, resources);

        auto compiled = CompiledGraphFrame{
            .resources = std::move(resources),
            .submitBatches = std::move(submitBatches),
            .ownershipTransitions = {},
            .debugView = {},
        };

        annotateResourceTransitions(compiled);
        compiled.debugView = makeDebugView(compiled);
        return compiled;
    }

  private:
    struct LastResourceUse
    {
        QueueDomain queue = QueueDomain::Graphics;
        std::optional<ImageLayoutIntent> layout{};
        std::uint32_t batchIndex = 0;
        ResourceOwnershipDomain ownership = ResourceOwnershipDomain::Undefined;
        AccessScope scope{};
    };

    [[nodiscard]] static std::vector<CompiledResourceDesc> compileResources(const RenderGraphFrameDescription& frame)
    {
        auto resources = std::vector<CompiledResourceDesc>{};
        resources.reserve(frame.resources.size());

        std::ranges::for_each(frame.resources, [&](const GraphResourceDesc& resource) {
            auto compiledResource = CompiledResourceDesc{
                .handle = resource.handle,
                .debugName = {},
                .isBuffer = false,
                .isImage = false,
                .isSwapchain = false,
                .lifetime = ResourceLifetime::GraphTransient,
                .residency = ResourceResidency::Managed,
                .resolvedBufferSize = 0,
                .resolvedExtent = vk::Extent3D{1, 1, 1},
                .resolvedFormat = vk::Format::eUndefined,
                .resolvedAspect = ImageAspectIntent::Color,
                .resolvedBufferUsage = {},
                .resolvedImageUsage = {},
                .initialLayout = ImageLayoutIntent::Undefined,
                .finalLayout = ImageLayoutIntent::Undefined,
                .initialOwnership = ResourceOwnershipDomain::Undefined,
                .finalOwnership = ResourceOwnershipDomain::Undefined,
                .resolvedBufferMemoryUsage = nr::rhi::MemoryUsage::GpuOnly,
            };

            std::visit(
                [&](const auto& desc) {
                    using DescT = std::remove_cvref_t<decltype(desc)>;
                    compiledResource.debugName = desc.debugName;

                    if constexpr (std::same_as<DescT, GraphImportedBufferDesc>)
                    {
                        compiledResource.isBuffer = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.resolvedBufferSize = desc.size;
                        compiledResource.initialOwnership = desc.initialOwnership;
                        compiledResource.finalOwnership = desc.initialOwnership;
                        compiledResource.importedBufferResource = desc.importedResource;
                        std::ranges::for_each(desc.usageIntents, [&](BufferUsageIntent intent) {
                            compiledResource.resolvedBufferUsage |= mapBufferUsageIntent(intent);
                        });
                    }
                    else if constexpr (std::same_as<DescT, GraphTransientBufferDesc>)
                    {
                        compiledResource.isBuffer = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = ResourceResidency::Managed;
                        compiledResource.resolvedBufferSize = desc.size;
                        compiledResource.initialOwnership = ResourceOwnershipDomain::Undefined;
                        compiledResource.finalOwnership = ResourceOwnershipDomain::Undefined;
                        compiledResource.resolvedBufferMemoryUsage = desc.memoryUsage;
                        std::ranges::for_each(desc.usageIntents, [&](BufferUsageIntent intent) {
                            compiledResource.resolvedBufferUsage |= mapBufferUsageIntent(intent);
                        });
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
                        std::ranges::for_each(desc.usageIntents, [&](ImageUsageIntent intent) {
                            compiledResource.resolvedImageUsage |= mapImageUsageIntent(intent);
                        });
                    }
                    else if constexpr (std::same_as<DescT, GraphTransientImageDesc>)
                    {
                        compiledResource.isImage = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = ResourceResidency::Managed;
                        compiledResource.resolvedExtent = desc.extent;
                        compiledResource.resolvedFormat = desc.format;
                        compiledResource.resolvedAspect = desc.aspect;
                        // Graph-transient images are allocated each frame and start in Undefined.
                        compiledResource.initialLayout = ImageLayoutIntent::Undefined;
                        compiledResource.finalLayout = ImageLayoutIntent::Undefined;
                        compiledResource.initialOwnership = ResourceOwnershipDomain::Undefined;
                        compiledResource.finalOwnership = ResourceOwnershipDomain::Undefined;
                        std::ranges::for_each(desc.usageIntents, [&](ImageUsageIntent intent) {
                            compiledResource.resolvedImageUsage |= mapImageUsageIntent(intent);
                        });
                    }
                    else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
                    {
                        compiledResource.isImage = true;
                        compiledResource.isSwapchain = true;
                        compiledResource.lifetime = desc.lifetime;
                        compiledResource.residency = desc.residency;
                        compiledResource.resolvedExtent = desc.extent;
                        compiledResource.resolvedFormat = desc.format;
                        compiledResource.resolvedAspect = ImageAspectIntent::Color;
                        // Newly acquired swapchain images may be undefined before first present/reuse.
                        compiledResource.initialLayout = ImageLayoutIntent::Undefined;
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
        const std::map<GraphResourceHandle, std::size_t>& resourceIndexByHandle)
    {
        auto indices = std::vector<std::size_t>{};
        indices.reserve(resourceUses.size());

        std::ranges::for_each(resourceUses, [&](const PassResourceUseDesc& use) {
            auto indexIt = resourceIndexByHandle.find(use.resource);
            nrAssert(
                indexIt != resourceIndexByHandle.end(),
                "RenderGraphCompiler::compileSubmitBatches found pass use for unknown resource handle.");
            indices.push_back(indexIt->second);
        });

        return indices;
    }

    [[nodiscard]] static std::vector<CompiledSubmitBatch> compileSubmitBatches(
        const RenderGraphFrameDescription& frame,
        const std::vector<CompiledResourceDesc>& compiledResources)
    {
        auto passByHandle = std::map<GraphPassHandle, std::reference_wrapper<const PassExecutionDesc>>{};
        std::ranges::for_each(frame.passes, [&](const PassExecutionDesc& pass) {
            passByHandle.emplace(pass.handle, std::cref(pass));
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
                pendingBoundary = std::get<GraphSubmitHandle>(step);
                flushCurrentBatch();
                return;
            }

            auto passHandle = std::get<GraphPassHandle>(step);
            auto passIt = passByHandle.find(passHandle);
            nrAssert(passIt != passByHandle.end(), "RenderGraphCompiler::compileSubmitBatches execution order references unknown pass handle.");
            const auto& pass = passIt->second.get();

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
                currentBatch = CompiledSubmitBatch{
                    .batchIndex = nextBatchIndex++,
                    .queue = pass.queue,
                    .openedBySubmitNode = pendingBoundary,
                    .passes = {},
                };
                pendingBoundary.reset();
            }

            currentBatch->passes.push_back(CompiledPass{
                .handle = pass.handle,
                .node = pass.node,
                .debugName = pass.debugName,
                .isCopyPass = pass.isCopyPass,
                .queue = pass.queue,
                .submitBatchIndex = currentBatch->batchIndex,
                .resourceUses = pass.resourceUses,
                .resolvedResourceIndices = resolvePassResourceIndices(pass.resourceUses, resourceIndexByHandle),
                .preBarriers = {},
                .prepare = pass.prepare,
                .record = pass.record,
            });
        });

        flushCurrentBatch();
        return batches;
    }

    static void annotateResourceTransitions(CompiledGraphFrame& compiled)
    {
        auto resourceIndexByHandle = std::map<GraphResourceHandle, std::size_t>{};
        auto resourceByHandle = std::map<GraphResourceHandle, std::reference_wrapper<const CompiledResourceDesc>>{};
        auto resourceIndices = std::views::iota(std::size_t{0}, compiled.resources.size());
        std::ranges::for_each(resourceIndices, [&](std::size_t index) {
            resourceIndexByHandle.emplace(compiled.resources[index].handle, index);
            resourceByHandle.emplace(compiled.resources[index].handle, std::cref(compiled.resources[index]));
        });

        auto lastUse = std::map<GraphResourceHandle, LastResourceUse>{};

        auto resolveLayout = [&](GraphResourceHandle handle,
                                 std::optional<ImageLayoutIntent> requestedLayout,
                                 const LastResourceUse* previousUse) -> std::optional<ImageLayoutIntent> {
            if (requestedLayout.has_value())
            {
                return requestedLayout;
            }

            if (previousUse != nullptr && previousUse->layout.has_value())
            {
                return previousUse->layout;
            }

            auto resourceIt = resourceByHandle.find(handle);
            if (resourceIt != resourceByHandle.end() && resourceIt->second.get().isImage)
            {
                return resourceIt->second.get().initialLayout;
            }

            return std::nullopt;
        };

        std::ranges::for_each(compiled.submitBatches, [&](CompiledSubmitBatch& batch) {
            std::ranges::for_each(batch.passes, [&](CompiledPass& pass) {
                auto passFinalUse = std::map<GraphResourceHandle, LastResourceUse>{};

                std::ranges::for_each(pass.resourceUses, [&](const PassResourceUseDesc& use) {
                    auto ownership = use.ownershipDomain != ResourceOwnershipDomain::Undefined
                                         ? use.ownershipDomain
                                         : ownershipDomainFromQueue(pass.queue);

                    auto inPassPreviousIt = passFinalUse.find(use.resource);
                    auto graphPreviousIt = lastUse.find(use.resource);

                    auto previousUse = std::optional<std::reference_wrapper<const LastResourceUse>>{};
                    auto previousFromSamePass = false;
                    if (inPassPreviousIt != passFinalUse.end())
                    {
                        previousUse = std::cref(inPassPreviousIt->second);
                        previousFromSamePass = true;
                    }
                    else if (graphPreviousIt != lastUse.end())
                    {
                        previousUse = std::cref(graphPreviousIt->second);
                    }

                    auto resolvedLayout = resolveLayout(
                        use.resource,
                        use.imageLayout,
                        previousUse.has_value() ? std::addressof(previousUse->get()) : nullptr);

                    auto currentScope = resolveUseAccessScope(use, pass.queue);

                    if (previousUse.has_value() && !previousFromSamePass)
                    {
                        auto const &previous = previousUse->get();
                        auto strength = DependencyStrength::InOrder;
                        if (previous.queue != pass.queue)
                        {
                            strength = DependencyStrength::ReleaseAcquireRequired;
                        }
                        else if (previous.batchIndex != pass.submitBatchIndex)
                        {
                            strength = DependencyStrength::BarrierRequired;
                        }
                        else if (previous.layout.has_value() &&
                                 resolvedLayout.has_value() &&
                                 previous.layout != resolvedLayout)
                        {
                            strength = DependencyStrength::BarrierRequired;
                        }

                        if (strength != DependencyStrength::InOrder)
                        {
                            auto transition = ResourceStateTransition{
                                .resource = use.resource,
                                .srcQueue = previous.queue,
                                .dstQueue = pass.queue,
                                .oldLayout = previous.layout.value_or(ImageLayoutIntent::Undefined),
                                .newLayout = resolvedLayout.value_or(previous.layout.value_or(ImageLayoutIntent::Undefined)),
                                .strength = strength,
                                .srcScope = previous.scope,
                                .dstScope = currentScope,
                            };
                            pass.preBarriers.push_back(transition);
                            if (strength == DependencyStrength::ReleaseAcquireRequired)
                            {
                                compiled.ownershipTransitions.push_back(transition);
                            }
                        }
                    }
                    else if (!previousUse.has_value())
                    {
                        auto resourceIt = resourceByHandle.find(use.resource);
                        if (resourceIt != resourceByHandle.end() && resourceIt->second.get().isImage)
                        {
                            auto oldLayout = resourceIt->second.get().initialLayout;
                            auto newLayout = resolvedLayout.value_or(oldLayout);
                            if (oldLayout != newLayout)
                            {
                                pass.preBarriers.push_back(ResourceStateTransition{
                                    .resource = use.resource,
                                    .srcQueue = pass.queue,
                                    .dstQueue = pass.queue,
                                    .oldLayout = oldLayout,
                                    .newLayout = newLayout,
                                    .strength = DependencyStrength::BarrierRequired,
                                    .srcScope = AccessScope{},
                                    .dstScope = currentScope,
                                });
                            }
                        }
                    }

                    passFinalUse.insert_or_assign(use.resource, LastResourceUse{
                        .queue = pass.queue,
                        .layout = resolvedLayout,
                        .batchIndex = pass.submitBatchIndex,
                        .ownership = ownership,
                        .scope = currentScope,
                    });
                });

                std::ranges::for_each(passFinalUse, [&](const auto& pair) {
                    lastUse.insert_or_assign(pair.first, pair.second);
                });
            });
        });

        std::ranges::for_each(lastUse, [&](const auto& pair) {
            auto indexIt = resourceIndexByHandle.find(pair.first);
            if (indexIt == resourceIndexByHandle.end())
            {
                return;
            }
            auto& resource = compiled.resources[indexIt->second];
            resource.finalOwnership = pair.second.ownership;
            if (pair.second.layout.has_value())
            {
                resource.finalLayout = *pair.second.layout;
            }
        });

        std::ranges::for_each(compiled.resources, [](CompiledResourceDesc& resource) {
            if (!resource.isSwapchain)
            {
                return;
            }

            resource.finalLayout = ImageLayoutIntent::PresentSrc;
        });
    }

    [[nodiscard]] static std::string_view queueName(QueueDomain queue)
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

    [[nodiscard]] static std::string makeDebugView(const CompiledGraphFrame& compiled)
    {
        auto text = std::string{};

        std::ranges::for_each(compiled.resources, [&](const CompiledResourceDesc& resource) {
            auto typeName = std::string_view{"Unknown"};
            if (resource.isSwapchain)
            {
                typeName = "SwapchainImage";
            }
            else if (resource.isImage)
            {
                typeName = "Image";
            }
            else if (resource.isBuffer)
            {
                typeName = "Buffer";
            }

            text += std::format("resource[{}] {} type={} lifetime={} residency={} usage(buffer={}, image={}) layout={}=>{} ownership={}=>{}\n",
                                resource.handle.value,
                                resource.debugName,
                                typeName,
                                static_cast<std::uint32_t>(resource.lifetime),
                                static_cast<std::uint32_t>(resource.residency),
                                static_cast<std::uint32_t>(resource.resolvedBufferUsage),
                                static_cast<std::uint32_t>(resource.resolvedImageUsage),
                                static_cast<std::uint32_t>(resource.initialLayout),
                                static_cast<std::uint32_t>(resource.finalLayout),
                                static_cast<std::uint32_t>(resource.initialOwnership),
                                static_cast<std::uint32_t>(resource.finalOwnership));
        });

        std::ranges::for_each(compiled.submitBatches, [&](const CompiledSubmitBatch& batch) {
            auto openedBy = batch.openedBySubmitNode.has_value()
                                ? std::to_string(batch.openedBySubmitNode->value)
                                : std::string{"none"};

            text += std::format("submitBatch[{}] queue={} passCount={} openedBySubmit={}\n",
                                batch.batchIndex,
                                queueName(batch.queue),
                                batch.passes.size(),
                                openedBy);

            std::ranges::for_each(batch.passes, [&](const CompiledPass& pass) {
                text += std::format("  pass[{}] {} isCopyPass={} queue={} uses={} resolvedUses={} preBarriers={}\n",
                                    pass.handle.value,
                                    pass.debugName,
                                    pass.isCopyPass ? 1 : 0,
                                    queueName(pass.queue),
                                    pass.resourceUses.size(),
                                    pass.resolvedResourceIndices.size(),
                                    pass.preBarriers.size());
            });
        });

        std::ranges::for_each(compiled.ownershipTransitions, [&](const ResourceStateTransition& transition) {
            text += std::format("ownershipTransition resource={} {}->{} layout={}=>{}\n",
                                transition.resource.value,
                                queueName(transition.srcQueue),
                                queueName(transition.dstQueue),
                                static_cast<std::uint32_t>(transition.oldLayout),
                                static_cast<std::uint32_t>(transition.newLayout));
        });

        return text;
    }
};
} // namespace nr::renderer
