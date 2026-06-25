module nr.renderer;
import :renderGraphBuilder;
import nr.utils;
import std;
import :renderGraphType;
import :rendererType;

namespace nr::renderer
{
RenderGraphNodeContext::RenderGraphNodeContext(RenderGraphBuilder& builder, GraphNodeHandle node) noexcept
        : builder_(builder)
        , node(node)
{
    }

[[nodiscard]] GraphNodeHandle RenderGraphNodeContext::nodeHandle() const noexcept
{
        return node;
    }

[[nodiscard]] RenderGraphBuilder& RenderGraphNodeContext::builder() noexcept
{
        return builder_.get();
    }

void RenderGraphBuilder::clear()
{
        // Release per-pass payload before clearing the top-level pass list.
        std::ranges::for_each(frame_.passes, [](PassExecutionDesc& pass) {
            pass.resourceUses.clear();
            pass.record = nullptr;
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
    }

[[nodiscard]] GraphNodeHandle RenderGraphBuilder::addNode(
        std::string_view debugName,
        QueueDomain queue)
{
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
        auto handle = GraphNodeHandle{nextNode_++};
        frame_.nodes.push_back(GraphNodeDesc{
            .handle = handle,
            .debugName = std::string(debugName),
            .queue = QueueDomain::Compute,
        });
        return handle;
    }

[[nodiscard]] RenderGraphNodeContext RenderGraphBuilder::makeNodeContext(GraphNodeHandle node)
{
        nrAssert(node.valid(), "RenderGraphBuilder::makeNodeContext requires a valid node handle.");
        nrAssert(containsNode(node), "RenderGraphBuilder::makeNodeContext requires an existing node handle.");
        return RenderGraphNodeContext{*this, node};
    }

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addPass(
        std::string_view debugName,
        GraphNodeHandle node,
        std::span<const PassResourceUseDesc> intentList,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback,
        bool isCopyPass)
{
        validatePassCallbackContract(executeLambda, isCopyPass);
        validatePassResourceUses(intentList, isCopyPass);

        auto passHandle = addPassCore(debugName, node, isCopyPass);
        auto& pass = frame_.passes.back();
        nrAssert(pass.handle == passHandle, "RenderGraphBuilder::addPass pass insertion invariant failed.");

        pass.resourceUses.reserve(intentList.size());
        std::ranges::copy(intentList, std::back_inserter(pass.resourceUses));
        pass.prepare = std::move(prepareCallback);
        pass.record = std::move(executeLambda);

        return passHandle;
    }

[[nodiscard]] GraphSubmitHandle RenderGraphBuilder::addSubmitNode(
        std::string_view debugName)
{
        auto handle = GraphSubmitHandle{nextSubmit_++};
        frame_.submitBoundaries.push_back(SubmitBoundaryDesc{
            .handle = handle,
            .debugName = std::string(debugName),
        });
        frame_.executionOrder.push_back(handle);
        return handle;
    }

[[nodiscard]] const RenderGraphFrameDescription& RenderGraphBuilder::frame() const noexcept
{
        return frame_;
    }

[[nodiscard]] RenderGraphFrameDescription& RenderGraphBuilder::mutableFrame() noexcept
{
        return frame_;
    }

[[nodiscard]] RenderGraphFrameDescription RenderGraphBuilder::build() const
{
        return frame_;
    }

[[nodiscard]] GraphPassHandle RenderGraphBuilder::addPassCore(
        std::string_view debugName,
        GraphNodeHandle node,
        bool isCopyPass)
{
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
        });
        frame_.executionOrder.push_back(handle);
        return handle;
    }

[[nodiscard]] std::vector<PassExecutionDesc>::iterator RenderGraphBuilder::findPass(GraphPassHandle handle)
{
        return std::ranges::find_if(frame_.passes, [handle](const PassExecutionDesc& desc) {
            return desc.handle == handle;
        });
    }

[[nodiscard]] bool RenderGraphBuilder::isBufferResourceDesc(const GraphResourceDesc& desc) noexcept
{
        return std::holds_alternative<GraphImportedBufferDesc>(desc.desc) ||
               std::holds_alternative<GraphTransientBufferDesc>(desc.desc);
    }

[[nodiscard]] bool RenderGraphBuilder::isImageResourceDesc(const GraphResourceDesc& desc) noexcept
{
        return std::holds_alternative<GraphImportedImageDesc>(desc.desc) ||
               std::holds_alternative<GraphImportedSwapchainImageDesc>(desc.desc) ||
               std::holds_alternative<GraphTransientImageDesc>(desc.desc);
    }

[[nodiscard]] bool RenderGraphBuilder::isAccelerationStructureResourceDesc(const GraphResourceDesc& desc) noexcept
{
        return std::holds_alternative<GraphImportedAccelerationStructureDesc>(desc.desc);
    }

[[nodiscard]] bool RenderGraphBuilder::hasBufferIntentFields(const PassResourceUseDesc& use) noexcept
{
        return use.bufferUsage.has_value() || use.bufferAccess.has_value();
    }

[[nodiscard]] bool RenderGraphBuilder::hasAccelerationStructureIntentFields(const PassResourceUseDesc& use) noexcept
{
        return use.accelerationStructureUsage.has_value() || use.accelerationStructureAccess.has_value();
    }

[[nodiscard]] bool RenderGraphBuilder::hasImageIntentFields(const PassResourceUseDesc& use) noexcept
{
        return use.imageUsage.has_value() ||
               use.imageAccess.has_value() ||
               use.imageLayout.has_value() ||
               use.imageAspect.has_value();
    }

[[nodiscard]] bool RenderGraphBuilder::bufferAccessReads(BufferAccessIntent intent) noexcept
{
        switch (intent)
        {
        case BufferAccessIntent::TransferRead:
        case BufferAccessIntent::UniformRead:
        case BufferAccessIntent::ShaderSampleRead:
        case BufferAccessIntent::ShaderStorageRead:
        case BufferAccessIntent::ShaderStorageReadWrite:
        case BufferAccessIntent::VertexRead:
        case BufferAccessIntent::IndexRead:
        case BufferAccessIntent::IndirectRead:
        case BufferAccessIntent::TexelRead:
        case BufferAccessIntent::TexelReadWrite:
        case BufferAccessIntent::AccelerationStructureRead:
        case BufferAccessIntent::ShaderBindingTableRead:
        case BufferAccessIntent::HostRead:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::bufferAccessWrites(BufferAccessIntent intent) noexcept
{
        switch (intent)
        {
        case BufferAccessIntent::TransferWrite:
        case BufferAccessIntent::ShaderStorageWrite:
        case BufferAccessIntent::ShaderStorageReadWrite:
        case BufferAccessIntent::TexelWrite:
        case BufferAccessIntent::TexelReadWrite:
        case BufferAccessIntent::AccelerationStructureWrite:
        case BufferAccessIntent::HostWrite:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::imageAccessReads(ImageAccessIntent intent) noexcept
{
        switch (intent)
        {
        case ImageAccessIntent::TransferRead:
        case ImageAccessIntent::SampledRead:
        case ImageAccessIntent::StorageRead:
        case ImageAccessIntent::StorageReadWrite:
        case ImageAccessIntent::ColorAttachmentRead:
        case ImageAccessIntent::ColorAttachmentReadWrite:
        case ImageAccessIntent::DepthStencilRead:
        case ImageAccessIntent::DepthStencilReadWrite:
        case ImageAccessIntent::InputAttachmentRead:
        case ImageAccessIntent::PresentRead:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::imageAccessWrites(ImageAccessIntent intent) noexcept
{
        switch (intent)
        {
        case ImageAccessIntent::TransferWrite:
        case ImageAccessIntent::StorageWrite:
        case ImageAccessIntent::StorageReadWrite:
        case ImageAccessIntent::ColorAttachmentWrite:
        case ImageAccessIntent::ColorAttachmentReadWrite:
        case ImageAccessIntent::DepthStencilWrite:
        case ImageAccessIntent::DepthStencilReadWrite:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::accelerationStructureAccessReads(AccelerationStructureAccessIntent intent) noexcept
{
        switch (intent)
        {
        case AccelerationStructureAccessIntent::BuildRead:
        case AccelerationStructureAccessIntent::TraceRead:
        case AccelerationStructureAccessIntent::CopyRead:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::accelerationStructureAccessWrites(AccelerationStructureAccessIntent intent) noexcept
{
        switch (intent)
        {
        case AccelerationStructureAccessIntent::BuildWrite:
        case AccelerationStructureAccessIntent::CopyWrite:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::accelerationStructureUsageReads(AccelerationStructureUsageIntent intent) noexcept
{
        switch (intent)
        {
        case AccelerationStructureUsageIntent::BuildInput:
        case AccelerationStructureUsageIntent::TraceInput:
        case AccelerationStructureUsageIntent::CopySource:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::accelerationStructureUsageWrites(AccelerationStructureUsageIntent intent) noexcept
{
        switch (intent)
        {
        case AccelerationStructureUsageIntent::BuildOutput:
        case AccelerationStructureUsageIntent::CopyDestination:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::bufferUsageReads(BufferUsageIntent intent) noexcept
{
        switch (intent)
        {
        case BufferUsageIntent::TransferSrc:
        case BufferUsageIntent::Uniform:
        case BufferUsageIntent::StorageRead:
        case BufferUsageIntent::StorageReadWrite:
        case BufferUsageIntent::Vertex:
        case BufferUsageIntent::Index:
        case BufferUsageIntent::Indirect:
        case BufferUsageIntent::UniformTexel:
        case BufferUsageIntent::StorageTexelRead:
        case BufferUsageIntent::StorageTexelReadWrite:
        case BufferUsageIntent::AccelerationStructureBuildInput:
        case BufferUsageIntent::HostUpload:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::bufferUsageWrites(BufferUsageIntent intent) noexcept
{
        switch (intent)
        {
        case BufferUsageIntent::TransferDst:
        case BufferUsageIntent::StorageWrite:
        case BufferUsageIntent::StorageReadWrite:
        case BufferUsageIntent::StorageTexelWrite:
        case BufferUsageIntent::StorageTexelReadWrite:
        case BufferUsageIntent::AccelerationStructureStorage:
        case BufferUsageIntent::AccelerationStructureScratch:
        case BufferUsageIntent::Readback:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::imageUsageReads(ImageUsageIntent intent) noexcept
{
        switch (intent)
        {
        case ImageUsageIntent::TransferSrc:
        case ImageUsageIntent::Sampled:
        case ImageUsageIntent::StorageRead:
        case ImageUsageIntent::StorageReadWrite:
        case ImageUsageIntent::DepthStencilReadOnly:
        case ImageUsageIntent::InputAttachment:
        case ImageUsageIntent::ResolveSrc:
        case ImageUsageIntent::PresentSource:
        case ImageUsageIntent::CopySource:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::imageUsageWrites(ImageUsageIntent intent) noexcept
{
        switch (intent)
        {
        case ImageUsageIntent::TransferDst:
        case ImageUsageIntent::StorageWrite:
        case ImageUsageIntent::StorageReadWrite:
        case ImageUsageIntent::ColorAttachment:
        case ImageUsageIntent::DepthStencilAttachment:
        case ImageUsageIntent::ResolveDst:
        case ImageUsageIntent::CopyDestination:
            return true;
        default:
            return false;
        }
    }

[[nodiscard]] bool RenderGraphBuilder::isCopySourceUse(const PassResourceUseDesc& use) noexcept
{
        return use.imageUsage == ImageUsageIntent::TransferSrc ||
               use.imageUsage == ImageUsageIntent::CopySource;
    }

[[nodiscard]] bool RenderGraphBuilder::isCopyDestinationUse(const PassResourceUseDesc& use) noexcept
{
        return use.imageUsage == ImageUsageIntent::TransferDst ||
               use.imageUsage == ImageUsageIntent::CopyDestination;
    }

[[nodiscard]] bool RenderGraphBuilder::isPresentUse(const PassResourceUseDesc& use) noexcept
{
        return use.imageUsage == ImageUsageIntent::PresentSource;
    }

void RenderGraphBuilder::validatePassCallbackContract(
        const PassRecordCallback& executeLambda,
        bool isCopyPass)
{
        nrAssert(
            isCopyPass || static_cast<bool>(executeLambda),
            "RenderGraphBuilder::addPass requires a record callback for non-copy passes.");
        nrAssert(
            !isCopyPass || !static_cast<bool>(executeLambda),
            "RenderGraphBuilder::addPass copy passes use the implicit copy path and must not provide a record callback.");
    }

void RenderGraphBuilder::validatePassUseReadOnlyContract(const PassResourceUseDesc& use)
{
        auto hasExplicitAccess = false;
        auto reads = false;
        auto writes = false;

        if (use.bufferAccess.has_value() && *use.bufferAccess != BufferAccessIntent::None)
        {
            hasExplicitAccess = true;
            reads = bufferAccessReads(*use.bufferAccess);
            writes = bufferAccessWrites(*use.bufferAccess);
        }
        else if (use.imageAccess.has_value() && *use.imageAccess != ImageAccessIntent::None)
        {
            hasExplicitAccess = true;
            reads = imageAccessReads(*use.imageAccess);
            writes = imageAccessWrites(*use.imageAccess);
        }
        else if (use.accelerationStructureAccess.has_value() &&
                 *use.accelerationStructureAccess != AccelerationStructureAccessIntent::None)
        {
            hasExplicitAccess = true;
            reads = accelerationStructureAccessReads(*use.accelerationStructureAccess);
            writes = accelerationStructureAccessWrites(*use.accelerationStructureAccess);
        }

        if (!hasExplicitAccess)
        {
            if (use.bufferUsage.has_value())
            {
                reads = bufferUsageReads(*use.bufferUsage);
                writes = bufferUsageWrites(*use.bufferUsage);
            }
            else if (use.imageUsage.has_value())
            {
                reads = imageUsageReads(*use.imageUsage);
                writes = imageUsageWrites(*use.imageUsage);
            }
            else if (use.accelerationStructureUsage.has_value())
            {
                reads = accelerationStructureUsageReads(*use.accelerationStructureUsage);
                writes = accelerationStructureUsageWrites(*use.accelerationStructureUsage);
            }
        }

        if (!reads && !writes)
        {
            return;
        }

        nrAssert(
            use.readOnly == (reads && !writes),
            "RenderGraphBuilder::addPass resource use readOnly flag does not match declared access intent.");
    }

void RenderGraphBuilder::validatePassResourceUse(const PassResourceUseDesc& use) const
{
        nrAssert(use.resource.valid(), "RenderGraphBuilder::addPass requires a valid resource handle.");
        auto resourceIt = resourceIndexByHandle_.find(use.resource);
        nrAssert(resourceIt != resourceIndexByHandle_.end(), "RenderGraphBuilder::addPass resource handle validation failed.");
        nrAssert(resourceIt->second < frame_.resources.size(), "RenderGraphBuilder::addPass resource index cache is out of range.");

        const auto& resource = frame_.resources[resourceIt->second];
        nrAssert(resource.handle == use.resource, "RenderGraphBuilder::addPass resource index cache is stale.");

        auto hasBufferFields = hasBufferIntentFields(use);
        auto hasAccelerationStructureFields = hasAccelerationStructureIntentFields(use);
        auto hasImageFields = hasImageIntentFields(use);
        nrAssert(
            hasBufferFields || hasAccelerationStructureFields || hasImageFields,
            "RenderGraphBuilder::addPass resource use requires buffer, acceleration-structure, or image intent fields.");
        nrAssert(
            static_cast<int>(hasBufferFields) + static_cast<int>(hasAccelerationStructureFields) + static_cast<int>(hasImageFields) == 1,
            "RenderGraphBuilder::addPass resource use cannot mix buffer, acceleration-structure, and image intent fields.");

        nrAssert(
            !hasBufferFields || isBufferResourceDesc(resource),
            "RenderGraphBuilder::addPass buffer intent targets a non-buffer resource.");
        nrAssert(
            !hasAccelerationStructureFields || isAccelerationStructureResourceDesc(resource),
            "RenderGraphBuilder::addPass acceleration-structure intent targets a non-acceleration-structure resource.");
        nrAssert(
            !hasImageFields || isImageResourceDesc(resource),
            "RenderGraphBuilder::addPass image intent targets a non-image resource.");

        validatePassUseReadOnlyContract(use);
    }

void RenderGraphBuilder::validatePassResourceUses(
        std::span<const PassResourceUseDesc> intentList,
        bool isCopyPass) const
{
        std::ranges::for_each(intentList, [this](const PassResourceUseDesc& use) {
            validatePassResourceUse(use);
        });

        if (!isCopyPass)
        {
            return;
        }

        auto hasSource = std::ranges::any_of(intentList, isCopySourceUse);
        auto hasDestination = std::ranges::any_of(intentList, isCopyDestinationUse);
        nrAssert(hasSource, "RenderGraphBuilder::addPass copy pass requires a source image intent.");
        nrAssert(hasDestination, "RenderGraphBuilder::addPass copy pass requires a destination image intent.");

        auto presentTargetsDestination = std::ranges::all_of(intentList, [&](const PassResourceUseDesc& use) {
            if (!isPresentUse(use))
            {
                return true;
            }

            return std::ranges::any_of(intentList, [&](const PassResourceUseDesc& dstUse) {
                return isCopyDestinationUse(dstUse) && dstUse.resource == use.resource;
            });
        });
        nrAssert(
            presentTargetsDestination,
            "RenderGraphBuilder::addPass copy pass present intent must target the copy destination resource.");
    }

[[nodiscard]] std::vector<GraphNodeDesc>::const_iterator RenderGraphBuilder::findNode(GraphNodeHandle handle) const
{
        return std::ranges::find_if(frame_.nodes, [handle](const GraphNodeDesc& desc) {
            return desc.handle == handle;
        });
    }

[[nodiscard]] bool RenderGraphBuilder::containsNode(GraphNodeHandle handle) const
{
        return findNode(handle) != frame_.nodes.end();
    }

[[nodiscard]] bool RenderGraphBuilder::containsResource(GraphResourceHandle handle) const
{
        return resourceIndexByHandle_.contains(handle);
    }

GraphPassHandle RenderGraphNodeContext::addPass(
    std::span<const PassResourceUseDesc> intentList,
    std::string_view debugName,
    PassRecordCallback executeLambda,
    PassPrepareCallback prepareCallback,
    bool isCopyPass)
{
    return builder_.get().addPass(
        debugName,
        node,
        intentList,
        std::move(executeLambda),
        std::move(prepareCallback),
        isCopyPass);
}

GraphSubmitHandle RenderGraphNodeContext::addSubmitNode(
    std::string_view debugName)
{
    return builder_.get().addSubmitNode(debugName);
}
} // namespace nr::renderer
