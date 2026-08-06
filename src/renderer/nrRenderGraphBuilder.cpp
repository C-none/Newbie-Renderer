module nr.renderer;
import :renderGraphBuilder;
import dependency.vulkan;
import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

namespace nr::renderer
{
namespace
{
[[nodiscard]] vk::PipelineStageFlags2 defaultShaderStagesForQueue(QueueDomain queue) noexcept
{
    if (queue == QueueDomain::Compute)
    {
        return vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eRayTracingShaderKHR;
    }
    if (queue == QueueDomain::Graphics)
    {
        return vk::PipelineStageFlagBits2::eAllGraphics;
    }
    return vk::PipelineStageFlagBits2::eAllCommands;
}

[[nodiscard]] std::optional<ImageAspectIntent> imageAspectFromMask(vk::ImageAspectFlags aspectMask) noexcept
{
    if (aspectMask == vk::ImageAspectFlags{})
    {
        return std::nullopt;
    }

    auto const hasDepth = (aspectMask & vk::ImageAspectFlagBits::eDepth) != vk::ImageAspectFlags{};
    auto const hasStencil = (aspectMask & vk::ImageAspectFlagBits::eStencil) != vk::ImageAspectFlags{};
    if (hasDepth && hasStencil)
    {
        return ImageAspectIntent::DepthStencil;
    }
    if (hasDepth)
    {
        return ImageAspectIntent::Depth;
    }
    if (hasStencil)
    {
        return ImageAspectIntent::Stencil;
    }
    return ImageAspectIntent::Color;
}

[[nodiscard]] PassResourceUseDesc imageCopySourceUse(GraphResourceHandle resource, ImageAspectIntent aspect) noexcept
{
    return use::copySource(resource, aspect);
}

[[nodiscard]] PassResourceUseDesc imageCopyDestinationUse(GraphResourceHandle resource,
                                                          ImageAspectIntent aspect) noexcept
{
    return use::copyDestination(resource, aspect);
}

[[nodiscard]] bool copySourceEqualsDestination(const CopyPassDesc &copy) noexcept
{
    return std::visit(
        [](const auto &desc) {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, CopyBufferToBufferPassDesc> ||
                          std::same_as<DescT, CopyImageToImagePassDesc>)
            {
                return desc.source == desc.destination;
            }
            else if constexpr (std::same_as<DescT, CopyBufferToImagePassDesc>)
            {
                return desc.sourceBuffer == desc.destinationImage;
            }
            else
            {
                return desc.sourceImage == desc.destinationBuffer;
            }
        },
        copy);
}
} // namespace

void RenderGraphBuilder::clear()
{
    // Release per-pass payload before clearing the top-level pass list.
    std::ranges::for_each(frame_.passes, [](PassExecutionDesc &pass) {
        pass.resourceUses.clear();
        pass.frameDataUses.clear();
        pass.copy.reset();
        pass.record = nullptr;
        pass.parallelRecord.reset();
        pass.prepare = nullptr;
    });

    // Clear top-level vectors without releasing their heap storage.
    frame_.resources.clear();
    frame_.frameData.clear();
    frame_.nodes.clear();
    frame_.passes.clear();
    frame_.submitBoundaries.clear();
    frame_.executionOrder.clear();
    resourceIndexByHandle_.clear();

    nextResource_ = 0;
    nextFrameData_ = 0;
    nextPass_ = 0;
    nextNode_ = 0;
    nextSubmit_ = 0;
    declarationCounts_ = {};
}

[[nodiscard]] GraphNodeHandle RenderGraphBuilder::addNode(std::string_view debugName, QueueDomain queue)
{
    ++declarationCounts_.nodes;
    auto handle = GraphNodeHandle{nextNode_++};
    frame_.nodes.push_back(GraphNodeDesc{
        .handle = handle,
        .debugName = std::string(debugName),
        .queue = queue,
    });
    return handle;
}

[[nodiscard]] GraphNodeHandle RenderGraphBuilder::addPresentNode(std::string_view debugName)
{
    ++declarationCounts_.nodes;
    auto handle = GraphNodeHandle{nextNode_++};
    frame_.nodes.push_back(GraphNodeDesc{
        .handle = handle,
        .debugName = std::string(debugName),
        .queue = QueueDomain::Compute,
    });
    return handle;
}

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addPass(std::string_view debugName, GraphNodeHandle node,
                                                          std::span<const PassResourceUseDesc> intentList,
                                                          PassRecordCallback executeLambda,
                                                          PassPrepareCallback prepareCallback, bool isCopyPass,
                                                          vk::PipelineStageFlags2 shaderStages,
                                                          std::span<const GraphFrameDataHandle> frameDataUses)
{
    validatePassCallbackContract(executeLambda, std::nullopt, isCopyPass);
    auto resourceUses = canonicalizePassResourceUses(intentList, false);
    auto canonicalFrameDataUses = canonicalizeFrameDataUses(frameDataUses);
    if (isCopyPass)
    {
        validateCopyPassResourceUses(resourceUses);
    }

    auto passHandle = addPassCore(debugName, node, isCopyPass, shaderStages);
    auto &pass = frame_.passes.back();
    nrAssert(pass.handle == passHandle, "RenderGraphBuilder::addPass pass insertion invariant failed.");

    pass.resourceUses = std::move(resourceUses);
    pass.frameDataUses = std::move(canonicalFrameDataUses);
    pass.prepare = std::move(prepareCallback);
    pass.record = std::move(executeLambda);

    return passHandle;
}

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addPass(std::string_view debugName, GraphNodeHandle node,
                                                          std::span<const PassResourceUseDesc> intentList,
                                                          PassParallelRecordDesc parallelRecord,
                                                          PassPrepareCallback prepareCallback,
                                                          vk::PipelineStageFlags2 shaderStages,
                                                          std::span<const GraphFrameDataHandle> frameDataUses)
{
    validatePassCallbackContract(PassRecordCallback{}, parallelRecord, false);
    auto resourceUses = canonicalizePassResourceUses(intentList, false);
    auto canonicalFrameDataUses = canonicalizeFrameDataUses(frameDataUses);

    auto passHandle = addPassCore(debugName, node, false, shaderStages);
    auto &pass = frame_.passes.back();
    nrAssert(pass.handle == passHandle, "RenderGraphBuilder::addPass pass insertion invariant failed.");

    pass.resourceUses = std::move(resourceUses);
    pass.frameDataUses = std::move(canonicalFrameDataUses);
    pass.prepare = std::move(prepareCallback);
    pass.parallelRecord = std::move(parallelRecord);

    return passHandle;
}

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addCopyPass(std::string_view debugName, GraphNodeHandle node,
                                                              CopyPassDesc copy)
{
    nrAssert(!copySourceEqualsDestination(copy),
             "RenderGraphBuilder::addCopyPass requires distinct source and destination resources.");
    auto resourceUses = makeCopyPassResourceUses(copy);
    validatePassCallbackContract(PassRecordCallback{}, std::nullopt, true);
    resourceUses = canonicalizePassResourceUses(resourceUses, true);
    validateCopyPassResourceUses(resourceUses);

    auto passHandle = addPassCore(debugName, node, true, vk::PipelineStageFlagBits2::eTransfer);
    auto &pass = frame_.passes.back();
    nrAssert(pass.handle == passHandle, "RenderGraphBuilder::addCopyPass pass insertion invariant failed.");

    pass.copy = std::move(copy);
    pass.resourceUses = std::move(resourceUses);

    return passHandle;
}

[[nodiscard]] GraphSubmitHandle RenderGraphBuilder::addSubmitNode(std::string_view debugName, SubmitBoundaryKind kind)
{
    ++declarationCounts_.submitNodes;
    auto handle = GraphSubmitHandle{nextSubmit_++};
    frame_.submitBoundaries.push_back(SubmitBoundaryDesc{
        .handle = handle,
        .debugName = std::string(debugName),
        .kind = kind,
    });
    frame_.executionOrder.push_back(handle);
    return handle;
}

[[nodiscard]] const RenderGraphFrameDescription &RenderGraphBuilder::frame() const noexcept
{
    return frame_;
}

[[nodiscard]] RenderGraphFrameDescription &RenderGraphBuilder::mutableFrame() noexcept
{
    return frame_;
}

[[nodiscard]] RenderGraphFrameDescription RenderGraphBuilder::build() const
{
    return frame_;
}

[[nodiscard]] RenderGraphDeclarationCounts RenderGraphBuilder::declarationCounts() const noexcept
{
    return declarationCounts_;
}

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addPassCore(std::string_view debugName, GraphNodeHandle node,
                                                              bool isCopyPass, vk::PipelineStageFlags2 shaderStages)
{
    ++declarationCounts_.passes;
    nrAssert(node.valid(), "RenderGraphBuilder::addPass requires a valid node handle.");

    auto nodeIt = findNode(node);
    nrAssert(nodeIt != frame_.nodes.end(), "RenderGraphBuilder::addPass requires a registered node handle.");

    auto handle = GraphPassHandle{nextPass_++};
    frame_.passes.push_back(PassExecutionDesc{
        .handle = handle,
        .node = node,
        .debugName = std::string(debugName),
        .isCopyPass = isCopyPass,
        .queue = nodeIt->queue,
        .shaderStages =
            shaderStages != vk::PipelineStageFlags2{} ? shaderStages : defaultShaderStagesForQueue(nodeIt->queue),
    });
    frame_.executionOrder.push_back(handle);
    return handle;
}

[[nodiscard]] std::vector<PassExecutionDesc>::iterator RenderGraphBuilder::findPass(GraphPassHandle handle)
{
    return std::ranges::find_if(frame_.passes,
                                [handle](const PassExecutionDesc &desc) { return desc.handle == handle; });
}

[[nodiscard]] const GraphResourceDesc &RenderGraphBuilder::resourceDesc(GraphResourceHandle handle) const
{
    nrAssert(handle.valid(), "RenderGraphBuilder::resourceDesc requires a valid resource handle.");
    auto resourceIt = resourceIndexByHandle_.find(handle);
    nrAssert(resourceIt != resourceIndexByHandle_.end(),
             "RenderGraphBuilder::resourceDesc resource handle validation failed.");
    nrAssert(resourceIt->second < frame_.resources.size(),
             "RenderGraphBuilder::resourceDesc resource index cache is out of range.");
    auto const &resource = frame_.resources[resourceIt->second];
    nrAssert(resource.handle == handle, "RenderGraphBuilder::resourceDesc resource index cache is stale.");
    return resource;
}

[[nodiscard]] ImageAspectIntent RenderGraphBuilder::imageAspectFor(GraphResourceHandle resource,
                                                                   std::optional<ImageAspectIntent> requestedAspect,
                                                                   vk::ImageAspectFlags regionAspect) const
{
    if (requestedAspect.has_value())
    {
        return *requestedAspect;
    }

    auto maskedAspect = imageAspectFromMask(regionAspect);
    if (maskedAspect.has_value())
    {
        return *maskedAspect;
    }

    return std::visit(
        [](const auto &desc) {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphImportedImageDesc> || std::same_as<DescT, GraphTransientImageDesc>)
            {
                return desc.aspect;
            }
            else
            {
                return ImageAspectIntent::Color;
            }
        },
        resourceDesc(resource).desc);
}

[[nodiscard]] std::vector<PassResourceUseDesc> RenderGraphBuilder::makeCopyPassResourceUses(
    const CopyPassDesc &copy) const
{
    return std::visit(
        [&](const auto &desc) -> std::vector<PassResourceUseDesc> {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, CopyBufferToBufferPassDesc>)
            {
                auto destinationUse = desc.destinationIntent == CopyBufferDestinationIntent::Readback
                                          ? use::readbackWrite(desc.destination)
                                          : use::bufferTransferDst(desc.destination);
                return {
                    use::bufferTransferSrc(desc.source),
                    destinationUse,
                };
            }
            else if constexpr (std::same_as<DescT, CopyBufferToImagePassDesc>)
            {
                auto aspect =
                    imageAspectFor(desc.destinationImage, desc.imageAspect, desc.region.imageSubresource.aspectMask);
                return {
                    use::bufferTransferSrc(desc.sourceBuffer),
                    imageCopyDestinationUse(desc.destinationImage, aspect),
                };
            }
            else if constexpr (std::same_as<DescT, CopyImageToBufferPassDesc>)
            {
                auto aspect =
                    imageAspectFor(desc.sourceImage, desc.imageAspect, desc.region.imageSubresource.aspectMask);
                auto destinationUse = desc.destinationIntent == CopyBufferDestinationIntent::Readback
                                          ? use::readbackWrite(desc.destinationBuffer)
                                          : use::bufferTransferDst(desc.destinationBuffer);
                return {
                    imageCopySourceUse(desc.sourceImage, aspect),
                    destinationUse,
                };
            }
            else
            {
                auto sourceAspect =
                    imageAspectFor(desc.source, desc.sourceAspect, desc.region.srcSubresource.aspectMask);
                auto destinationAspect =
                    imageAspectFor(desc.destination, desc.destinationAspect, desc.region.dstSubresource.aspectMask);
                auto result = std::vector<PassResourceUseDesc>{
                    imageCopySourceUse(desc.source, sourceAspect),
                    imageCopyDestinationUse(desc.destination, destinationAspect),
                };
                if (desc.presentDestination)
                {
                    result.push_back(use::presentRead(desc.destination));
                }
                return result;
            }
        },
        copy);
}

[[nodiscard]] bool RenderGraphBuilder::isBufferResourceDesc(const GraphResourceDesc &desc) noexcept
{
    return std::holds_alternative<GraphImportedBufferDesc>(desc.desc) ||
           std::holds_alternative<GraphTransientBufferDesc>(desc.desc);
}

[[nodiscard]] bool RenderGraphBuilder::isImageResourceDesc(const GraphResourceDesc &desc) noexcept
{
    return std::holds_alternative<GraphImportedImageDesc>(desc.desc) ||
           std::holds_alternative<GraphImportedSwapchainImageDesc>(desc.desc) ||
           std::holds_alternative<GraphTransientImageDesc>(desc.desc);
}

[[nodiscard]] bool RenderGraphBuilder::isAccelerationStructureResourceDesc(const GraphResourceDesc &desc) noexcept
{
    return std::holds_alternative<GraphImportedAccelerationStructureDesc>(desc.desc);
}

[[nodiscard]] bool RenderGraphBuilder::hasBufferIntentFields(const PassResourceUseDesc &use) noexcept
{
    return use.bufferUsage.has_value() || use.bufferAccess.has_value();
}

[[nodiscard]] bool RenderGraphBuilder::hasAccelerationStructureIntentFields(const PassResourceUseDesc &use) noexcept
{
    return use.accelerationStructureUsage.has_value() || use.accelerationStructureAccess.has_value();
}

[[nodiscard]] bool RenderGraphBuilder::hasImageIntentFields(const PassResourceUseDesc &use) noexcept
{
    return use.imageUsage.has_value() || use.imageAccess.has_value() || use.imageLayout.has_value() ||
           use.imageAspect.has_value();
}

[[nodiscard]] bool RenderGraphBuilder::bufferAccessUsesShaderStages(BufferAccessIntent intent) noexcept
{
    switch (intent)
    {
    case BufferAccessIntent::UniformRead:
    case BufferAccessIntent::ShaderSampleRead:
    case BufferAccessIntent::ShaderStorageRead:
    case BufferAccessIntent::ShaderStorageWrite:
    case BufferAccessIntent::ShaderStorageReadWrite:
    case BufferAccessIntent::TexelRead:
    case BufferAccessIntent::TexelWrite:
    case BufferAccessIntent::TexelReadWrite:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool RenderGraphBuilder::imageAccessUsesShaderStages(ImageAccessIntent intent) noexcept
{
    switch (intent)
    {
    case ImageAccessIntent::SampledRead:
    case ImageAccessIntent::StorageRead:
    case ImageAccessIntent::StorageWrite:
    case ImageAccessIntent::StorageReadWrite:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool RenderGraphBuilder::isCopySourceUse(const PassResourceUseDesc &use) noexcept
{
    return use.bufferUsage == BufferUsageIntent::TransferSrc || use.bufferUsage == BufferUsageIntent::HostUpload ||
           use.imageUsage == ImageUsageIntent::TransferSrc || use.imageUsage == ImageUsageIntent::CopySource;
}

[[nodiscard]] bool RenderGraphBuilder::isCopyDestinationUse(const PassResourceUseDesc &use) noexcept
{
    return use.bufferUsage == BufferUsageIntent::TransferDst || use.bufferUsage == BufferUsageIntent::Readback ||
           use.imageUsage == ImageUsageIntent::TransferDst || use.imageUsage == ImageUsageIntent::CopyDestination;
}

[[nodiscard]] bool RenderGraphBuilder::isImageCopyDestinationUse(const PassResourceUseDesc &use) noexcept
{
    return use.imageUsage == ImageUsageIntent::TransferDst || use.imageUsage == ImageUsageIntent::CopyDestination;
}

[[nodiscard]] bool RenderGraphBuilder::isPresentUse(const PassResourceUseDesc &use) noexcept
{
    return use.imageUsage == ImageUsageIntent::PresentSource;
}

[[nodiscard]] bool RenderGraphBuilder::isImplicitCopyPresentTransition(const PassResourceUseDesc &previous,
                                                                       const PassResourceUseDesc &current) noexcept
{
    return previous.resource == current.resource && previous.imageUsage == ImageUsageIntent::CopyDestination &&
           current.imageUsage == ImageUsageIntent::PresentSource;
}

void RenderGraphBuilder::validatePassCallbackContract(const PassRecordCallback &executeLambda,
                                                      const std::optional<PassParallelRecordDesc> &parallelRecord,
                                                      bool isCopyPass)
{
    auto const hasRecord = static_cast<bool>(executeLambda);
    auto const hasParallelRecord = parallelRecord.has_value();
    nrAssert(
        isCopyPass || hasRecord != hasParallelRecord,
        "RenderGraphBuilder::addPass requires exactly one record or parallel record callback for non-copy passes.");
    nrAssert(
        !isCopyPass || (!hasRecord && !hasParallelRecord),
        "RenderGraphBuilder::addPass copy passes use the implicit copy path and must not provide record callbacks.");
    if (hasParallelRecord)
    {
        nrAssert(parallelRecord->replaySemantics == ParallelRecordReplaySemantics::Unordered,
                 "RenderGraphBuilder::addPass only supports unordered parallel record replay.");
        nrAssert(static_cast<bool>(parallelRecord->itemCount),
                 "RenderGraphBuilder::addPass parallel record requires an item-count callback.");
        nrAssert(static_cast<bool>(parallelRecord->recordRange),
                 "RenderGraphBuilder::addPass parallel record requires a range-record callback.");
    }
}

void RenderGraphBuilder::validatePassResourceUse(const PassResourceUseDesc &use) const
{
    nrAssert(use.resource.valid(), "RenderGraphBuilder::addPass requires a valid resource handle.");
    auto resourceIt = resourceIndexByHandle_.find(use.resource);
    nrAssert(resourceIt != resourceIndexByHandle_.end(),
             "RenderGraphBuilder::addPass resource handle validation failed.");
    nrAssert(resourceIt->second < frame_.resources.size(),
             "RenderGraphBuilder::addPass resource index cache is out of range.");

    const auto &resource = frame_.resources[resourceIt->second];
    nrAssert(resource.handle == use.resource, "RenderGraphBuilder::addPass resource index cache is stale.");

    auto hasBufferFields = hasBufferIntentFields(use);
    auto hasAccelerationStructureFields = hasAccelerationStructureIntentFields(use);
    auto hasImageFields = hasImageIntentFields(use);
    nrAssert(
        hasBufferFields || hasAccelerationStructureFields || hasImageFields,
        "RenderGraphBuilder::addPass resource use requires buffer, acceleration-structure, or image intent fields.");
    nrAssert(
        static_cast<int>(hasBufferFields) + static_cast<int>(hasAccelerationStructureFields) +
                static_cast<int>(hasImageFields) ==
            1,
        "RenderGraphBuilder::addPass resource use cannot mix buffer, acceleration-structure, and image intent fields.");

    nrAssert(!hasBufferFields || isBufferResourceDesc(resource),
             "RenderGraphBuilder::addPass buffer intent targets a non-buffer resource.");
    nrAssert(
        !hasAccelerationStructureFields || isAccelerationStructureResourceDesc(resource),
        "RenderGraphBuilder::addPass acceleration-structure intent targets a non-acceleration-structure resource.");
    nrAssert(!hasImageFields || isImageResourceDesc(resource),
             "RenderGraphBuilder::addPass image intent targets a non-image resource.");

    if (use.shaderStages != vk::PipelineStageFlags2{})
    {
        auto const shaderStageCompatible =
            (use.bufferAccess.has_value() && bufferAccessUsesShaderStages(*use.bufferAccess)) ||
            (use.imageAccess.has_value() && imageAccessUsesShaderStages(*use.imageAccess));
        nrAssert(shaderStageCompatible,
                 "RenderGraphBuilder::addPass shader stage override requires a buffer/image shader access intent.");
    }
}

[[nodiscard]] std::vector<PassResourceUseDesc> RenderGraphBuilder::canonicalizePassResourceUses(
    std::span<const PassResourceUseDesc> intentList, bool allowImplicitCopyPresentTransition) const
{
    auto canonical = std::vector<PassResourceUseDesc>{};
    canonical.reserve(intentList.size());

    std::ranges::for_each(intentList, [&](const PassResourceUseDesc &use) {
        validatePassResourceUse(use);

        if (std::ranges::find(canonical, use) != canonical.end())
        {
            return;
        }

        auto const sameResource = std::ranges::find_if(
            canonical, [&](const PassResourceUseDesc &candidate) { return candidate.resource == use.resource; });
        if (sameResource == canonical.end())
        {
            canonical.push_back(use);
            return;
        }

        nrAssert(allowImplicitCopyPresentTransition && isImplicitCopyPresentTransition(*sameResource, use) &&
                     std::ranges::count_if(
                         canonical,
                         [&](const PassResourceUseDesc &candidate) { return candidate.resource == use.resource; }) == 1,
                 "RenderGraphBuilder::addPass resource handle has conflicting use declarations.");
        canonical.push_back(use);
    });

    return canonical;
}

[[nodiscard]] std::vector<GraphFrameDataHandle> RenderGraphBuilder::canonicalizeFrameDataUses(
    std::span<const GraphFrameDataHandle> frameDataUses) const
{
    auto canonical = std::vector<GraphFrameDataHandle>{};
    canonical.reserve(frameDataUses.size());
    std::ranges::for_each(frameDataUses, [&](GraphFrameDataHandle handle) {
        nrAssert(handle.valid(), "RenderGraphBuilder::addPass requires valid frame-data handles.");
        nrAssert(containsFrameData(handle),
                 "RenderGraphBuilder::addPass frame-data handle validation failed.");
        if (!std::ranges::contains(canonical, handle))
        {
            canonical.push_back(handle);
        }
    });
    return canonical;
}

void RenderGraphBuilder::validateCopyPassResourceUses(std::span<const PassResourceUseDesc> resourceUses)
{
    auto hasSource = std::ranges::any_of(resourceUses, isCopySourceUse);
    auto hasDestination = std::ranges::any_of(resourceUses, isCopyDestinationUse);
    nrAssert(hasSource, "RenderGraphBuilder::addPass copy pass requires a source copy intent.");
    nrAssert(hasDestination, "RenderGraphBuilder::addPass copy pass requires a destination copy intent.");

    auto presentTargetsDestination = std::ranges::all_of(resourceUses, [&](const PassResourceUseDesc &use) {
        if (!isPresentUse(use))
        {
            return true;
        }

        return std::ranges::any_of(resourceUses, [&](const PassResourceUseDesc &dstUse) {
            return isImageCopyDestinationUse(dstUse) && dstUse.resource == use.resource;
        });
    });
    nrAssert(presentTargetsDestination,
             "RenderGraphBuilder::addPass copy pass present intent must target the copy destination resource.");
}

[[nodiscard]] std::vector<GraphNodeDesc>::const_iterator RenderGraphBuilder::findNode(GraphNodeHandle handle) const
{
    return std::ranges::find_if(frame_.nodes, [handle](const GraphNodeDesc &desc) { return desc.handle == handle; });
}

[[nodiscard]] bool RenderGraphBuilder::containsResource(GraphResourceHandle handle) const
{
    return resourceIndexByHandle_.contains(handle);
}

[[nodiscard]] bool RenderGraphBuilder::containsFrameData(GraphFrameDataHandle handle) const
{
    return std::ranges::any_of(frame_.frameData,
                               [handle](const GraphFrameDataDesc &desc) { return desc.handle == handle; });
}

} // namespace nr::renderer
