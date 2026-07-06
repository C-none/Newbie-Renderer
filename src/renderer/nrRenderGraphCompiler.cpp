module nr.renderer;
import :renderGraphCompiler;
import dependency.vulkan;
import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

namespace nr::renderer
{
[[nodiscard]] bool RenderGraphCompiler::hasExplicitSubmitBoundariesForQueueTransitions(const RenderGraphFrameDescription& frame)
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

[[nodiscard]] vk::BufferUsageFlags RenderGraphCompiler::mapBufferUsageIntent(BufferUsageIntent intent)
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

[[nodiscard]] vk::ImageUsageFlags RenderGraphCompiler::mapImageUsageIntent(ImageUsageIntent intent)
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

[[nodiscard]] vk::ImageLayout RenderGraphCompiler::mapImageLayoutIntent(ImageLayoutIntent intent)
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

[[nodiscard]] vk::ImageAspectFlags RenderGraphCompiler::mapImageAspectIntent(ImageAspectIntent intent)
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

[[nodiscard]] vk::PipelineStageFlags2 RenderGraphCompiler::shaderStagesForQueue(QueueDomain queue)
{
        if (queue == QueueDomain::Compute)
        {
            return vk::PipelineStageFlagBits2::eComputeShader |
                   vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
        }
        if (queue == QueueDomain::Graphics)
        {
            return vk::PipelineStageFlagBits2::eAllGraphics;
        }
        return vk::PipelineStageFlagBits2::eAllCommands;
    }

[[nodiscard]] AccessScope RenderGraphCompiler::mapBufferAccessIntent(
        BufferAccessIntent intent,
        vk::PipelineStageFlags2 shaderStages)
{
        auto const effectiveShaderStages = shaderStages != vk::PipelineStageFlags2{}
                                              ? shaderStages
                                              : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        switch (intent)
        {
        case BufferAccessIntent::None:
            return AccessScope{};
        case BufferAccessIntent::TransferRead:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};
        case BufferAccessIntent::TransferWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};
        case BufferAccessIntent::UniformRead:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eUniformRead};
        case BufferAccessIntent::ShaderSampleRead:
        case BufferAccessIntent::TexelRead:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderSampledRead};
        case BufferAccessIntent::ShaderStorageRead:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderStorageRead};
        case BufferAccessIntent::ShaderStorageWrite:
        case BufferAccessIntent::TexelWrite:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderStorageWrite};
        case BufferAccessIntent::ShaderStorageReadWrite:
        case BufferAccessIntent::TexelReadWrite:
            return AccessScope{
                effectiveShaderStages,
                vk::AccessFlagBits2::eShaderStorageRead |
                    vk::AccessFlagBits2::eShaderStorageWrite};
        case BufferAccessIntent::VertexRead:
            return AccessScope{vk::PipelineStageFlagBits2::eVertexAttributeInput, vk::AccessFlagBits2::eVertexAttributeRead};
        case BufferAccessIntent::IndexRead:
            return AccessScope{vk::PipelineStageFlagBits2::eIndexInput, vk::AccessFlagBits2::eIndexRead};
        case BufferAccessIntent::IndirectRead:
            return AccessScope{vk::PipelineStageFlagBits2::eDrawIndirect, vk::AccessFlagBits2::eIndirectCommandRead};
        case BufferAccessIntent::AccelerationStructureRead:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR |
                    vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        case BufferAccessIntent::AccelerationStructureWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
        case BufferAccessIntent::AccelerationStructureBuildInputRead:
            return AccessScope{vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR, vk::AccessFlagBits2::eShaderRead};
        case BufferAccessIntent::AccelerationStructureScratchReadWrite:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR |
                    vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
        case BufferAccessIntent::ShaderBindingTableRead:
            return AccessScope{vk::PipelineStageFlagBits2::eRayTracingShaderKHR, vk::AccessFlagBits2::eShaderBindingTableReadKHR};
        case BufferAccessIntent::HostRead:
            return AccessScope{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostRead};
        case BufferAccessIntent::HostWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eHost, vk::AccessFlagBits2::eHostWrite};
        }
        return AccessScope{};
    }

[[nodiscard]] AccessScope RenderGraphCompiler::mapBufferAccessIntent(BufferAccessIntent intent, QueueDomain queue)
{
        return mapBufferAccessIntent(intent, shaderStagesForQueue(queue));
    }

[[nodiscard]] AccessScope RenderGraphCompiler::mapImageAccessIntent(
        ImageAccessIntent intent,
        vk::PipelineStageFlags2 shaderStages)
{
        auto const effectiveShaderStages = shaderStages != vk::PipelineStageFlags2{}
                                              ? shaderStages
                                              : vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands};
        switch (intent)
        {
        case ImageAccessIntent::None:
            return AccessScope{};
        case ImageAccessIntent::TransferRead:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferRead};
        case ImageAccessIntent::TransferWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eTransfer, vk::AccessFlagBits2::eTransferWrite};
        case ImageAccessIntent::SampledRead:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderSampledRead};
        case ImageAccessIntent::StorageRead:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderStorageRead};
        case ImageAccessIntent::StorageWrite:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderStorageWrite};
        case ImageAccessIntent::StorageReadWrite:
            return AccessScope{effectiveShaderStages, vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite};
        case ImageAccessIntent::ColorAttachmentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentRead};
        case ImageAccessIntent::ColorAttachmentWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::AccessFlagBits2::eColorAttachmentWrite};
        case ImageAccessIntent::ColorAttachmentReadWrite:
            return AccessScope{
                vk::PipelineStageFlagBits2::eColorAttachmentOutput,
                vk::AccessFlagBits2::eColorAttachmentRead |
                    vk::AccessFlagBits2::eColorAttachmentWrite};
        case ImageAccessIntent::DepthStencilRead:
            return AccessScope{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentRead};
        case ImageAccessIntent::DepthStencilWrite:
            return AccessScope{vk::PipelineStageFlagBits2::eEarlyFragmentTests | vk::PipelineStageFlagBits2::eLateFragmentTests, vk::AccessFlagBits2::eDepthStencilAttachmentWrite};
        case ImageAccessIntent::DepthStencilReadWrite:
            return AccessScope{
                vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                    vk::PipelineStageFlagBits2::eLateFragmentTests,
                vk::AccessFlagBits2::eDepthStencilAttachmentRead |
                    vk::AccessFlagBits2::eDepthStencilAttachmentWrite};
        case ImageAccessIntent::InputAttachmentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eFragmentShader, vk::AccessFlagBits2::eInputAttachmentRead};
        case ImageAccessIntent::PresentRead:
            return AccessScope{vk::PipelineStageFlagBits2::eBottomOfPipe, vk::AccessFlags2{}};
        }
        return AccessScope{};
    }

[[nodiscard]] AccessScope RenderGraphCompiler::mapImageAccessIntent(ImageAccessIntent intent, QueueDomain queue)
{
        return mapImageAccessIntent(intent, shaderStagesForQueue(queue));
    }

[[nodiscard]] AccessScope RenderGraphCompiler::mapAccelerationStructureAccessIntent(AccelerationStructureAccessIntent intent)
{
        switch (intent)
        {
        case AccelerationStructureAccessIntent::None:
            return AccessScope{};
        case AccelerationStructureAccessIntent::BuildRead:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        case AccelerationStructureAccessIntent::BuildWrite:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureBuildKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
        case AccelerationStructureAccessIntent::TraceRead:
            return AccessScope{
                vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        case AccelerationStructureAccessIntent::CopyRead:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR,
                vk::AccessFlagBits2::eAccelerationStructureReadKHR};
        case AccelerationStructureAccessIntent::CopyWrite:
            return AccessScope{
                vk::PipelineStageFlagBits2::eAccelerationStructureCopyKHR,
                vk::AccessFlagBits2::eAccelerationStructureWriteKHR};
        }
        return AccessScope{};
    }

[[nodiscard]] AccessScope RenderGraphCompiler::resolveUseAccessScope(
        const PassResourceUseDesc& use,
        QueueDomain queue,
        vk::PipelineStageFlags2 passShaderStages)
{
        auto const shaderStages = use.shaderStages != vk::PipelineStageFlags2{}
                                      ? use.shaderStages
                                      : passShaderStages != vk::PipelineStageFlags2{}
                                            ? passShaderStages
                                            : shaderStagesForQueue(queue);
        if (use.bufferAccess.has_value())
        {
            return mapBufferAccessIntent(*use.bufferAccess, shaderStages);
        }
        if (use.imageAccess.has_value())
        {
            return mapImageAccessIntent(*use.imageAccess, shaderStages);
        }
        if (use.accelerationStructureAccess.has_value())
        {
            return mapAccelerationStructureAccessIntent(*use.accelerationStructureAccess);
        }
        return AccessScope{};
    }

[[nodiscard]] CompiledGraphFrame RenderGraphCompiler::compile(const RenderGraphFrameDescription& frame) const
{
        return compileImpl<false>(frame);
    }

[[nodiscard]] CompiledGraphFrame RenderGraphCompiler::compileConsuming(RenderGraphFrameDescription& frame) const
{
        return compileImpl<true>(frame);
    }

[[nodiscard]] std::vector<std::size_t> RenderGraphCompiler::resolvePassResourceIndices(
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

void RenderGraphCompiler::annotateResourceTransitions(CompiledGraphFrame& compiled)
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

                    auto currentScope = resolveUseAccessScope(use, pass.queue, pass.shaderStages);

                    if (previousUse.has_value() && !previousFromSamePass)
                    {
                        const auto& previous = previousUse->get();
                        auto strength = DependencyStrength::InOrder;
                        if (previous.queue != pass.queue)
                        {
                            strength = DependencyStrength::ReleaseAcquireRequired;
                        }
                        else if (previous.batchIndex != pass.submitBatchIndex)
                        {
                            strength = DependencyStrength::BarrierRequired;
                        }
                        else if (use.requiresPreviousUseBarrier)
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
                                    .srcScope = resourceIt->second.get().initialAccessScope,
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
            resource.finalAccessScope = pair.second.scope;
        });

        std::ranges::for_each(compiled.resources, [](CompiledResourceDesc& resource) {
            if (!resource.isSwapchain)
            {
                return;
            }

            resource.finalLayout = ImageLayoutIntent::PresentSrc;
        });
    }

[[nodiscard]] std::string_view RenderGraphCompiler::queueName(QueueDomain queue)
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

[[nodiscard]] std::string RenderGraphCompiler::makeDebugView(const CompiledGraphFrame& compiled)
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
            else if (resource.isAccelerationStructure)
            {
                typeName = "AccelerationStructure";
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
            auto openedBy = std::string{"none"};
            if (batch.openedBySubmitNode.has_value())
            {
                openedBy = batch.openedBySubmitNodeDebugName.empty()
                               ? std::to_string(batch.openedBySubmitNode->value)
                               : std::format("{} ({})", batch.openedBySubmitNodeDebugName, batch.openedBySubmitNode->value);
            }

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
} // namespace nr::renderer
