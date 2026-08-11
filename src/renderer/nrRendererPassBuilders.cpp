module nr.renderer;
import :renderer;
import dependency.assets;
import dependency.json;
import dependency.math;
import dependency.vulkan;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :frameServices;
import :renderGraphBuilder;
import :renderGraphCompiler;
import :renderGraphExecutor;
import :rendererSubmission;

namespace nr::renderer
{
[[nodiscard]] std::optional<NodeImageResourceDesc> describeGraphImageResource(const GraphResourceDesc &resource)
{
    return std::visit(
        [](const auto &desc) -> std::optional<NodeImageResourceDesc> {
            using DescT = std::remove_cvref_t<decltype(desc)>;
            if constexpr (std::same_as<DescT, GraphImportedImageDesc> || std::same_as<DescT, GraphTransientImageDesc>)
            {
                return NodeImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                    .aspect = desc.aspect,
                };
            }
            else if constexpr (std::same_as<DescT, GraphImportedSwapchainImageDesc>)
            {
                return NodeImageResourceDesc{
                    .debugName = desc.debugName,
                    .extent = desc.extent,
                    .format = desc.format,
                };
            }
            else
            {
                return std::nullopt;
            }
        },
        resource.desc);
}

void NodeBuildContext::publishFrameResource(std::string_view key, GraphResourceHandle resource) const
{
    nrAssert(resource.valid(), "NodeBuildContext::publishFrameResource requires a valid resource for '{}'.", key);
    nrAssert(!key.empty(), "NodeBuildContext::publishFrameResource requires a non-empty key.");
    frameResources.get().insert_or_assign(std::string(key), resource);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::resolveFrameResource(std::string_view key) const
{
    auto const resourceIt = frameResources.get().find(std::string(key));
    if (resourceIt == frameResources.get().end())
    {
        return {};
    }
    return resourceIt->second;
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::requireFrameResource(std::string_view key,
                                                                         std::string_view consumerDebugName) const
{
    auto resource = resolveFrameResource(key);
    nrAssert(resource.valid(), "{} requires frame resource '{}', but it has not been published.", consumerDebugName,
             key);
    return resource;
}

void NodeBuildContext::publishFrameData(std::string_view key, GraphFrameDataHandle frameData) const
{
    nrAssert(frameData.valid(), "NodeBuildContext::publishFrameData requires valid frame data for '{}'.", key);
    nrAssert(!key.empty(), "NodeBuildContext::publishFrameData requires a non-empty key.");
    frameDataResources.get().insert_or_assign(std::string(key), frameData);
}

[[nodiscard]] GraphFrameDataHandle NodeBuildContext::resolveFrameData(std::string_view key) const
{
    auto const frameDataIt = frameDataResources.get().find(std::string(key));
    if (frameDataIt == frameDataResources.get().end())
    {
        return {};
    }
    return frameDataIt->second;
}

[[nodiscard]] GraphFrameDataHandle NodeBuildContext::requireFrameData(std::string_view key,
                                                                      std::string_view consumerDebugName) const
{
    auto frameData = resolveFrameData(key);
    nrAssert(frameData.valid(), "{} requires frame data '{}', but it has not been published.", consumerDebugName,
             key);
    return frameData;
}

[[nodiscard]] std::optional<std::reference_wrapper<const std::any>> NodeBuildContext::resolveFrameDataPayload(
    GraphFrameDataHandle handle) const
{
    if (!handle.valid())
    {
        return {};
    }

    auto const &frameData = graphBuilder.get().frame().frameData;
    auto const frameDataIt =
        std::ranges::find_if(frameData, [handle](const GraphFrameDataDesc &desc) { return desc.handle == handle; });
    if (frameDataIt == frameData.end())
    {
        return {};
    }
    return std::cref(frameDataIt->payload);
}

[[nodiscard]] std::optional<NodeImageResourceDesc> NodeBuildContext::describeImageResource(
    GraphResourceHandle resource) const
{
    if (!resource.valid())
    {
        return std::nullopt;
    }

    auto const &resources = graphBuilder.get().frame().resources;
    auto const resourceIt =
        std::ranges::find_if(resources, [resource](const GraphResourceDesc &desc) { return desc.handle == resource; });
    if (resourceIt == resources.end())
    {
        return std::nullopt;
    }

    return describeGraphImageResource(*resourceIt);
}

[[nodiscard]] std::size_t NodeBuildContext::nodeLocalPassOrdinal() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(
        graphBuilder.get().frame().passes,
        [node = nodeHandle](const PassExecutionDesc &pass) { return pass.node == node; }));
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::transientColor(std::string_view debugName, vk::Extent2D extent,
                                                                   vk::Format format)
{
    return addResource(GraphTransientImageDesc{
        .debugName = std::string(debugName),
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            },
        .initialLayout = ImageLayoutIntent::ColorAttachment,
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importColor(const nr::rhi::Image &image, std::string_view debugName,
                                                                vk::Extent2D extent, vk::Format format,
                                                                ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::ColorAttachment,
                           ImageUsageIntent::TransferSrc,
                           ImageUsageIntent::Sampled,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importStorageColor(const nr::rhi::Image &image,
                                                                       std::string_view debugName, vk::Extent2D extent,
                                                                       vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::StorageWrite,
                           ImageUsageIntent::TransferSrc,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importRetainedStorageColor(const nr::rhi::Image &image,
                                                                               RetainedImageState &state,
                                                                               std::string_view debugName,
                                                                               vk::Extent2D extent, vk::Format format,
                                                                               ResourceLifetime lifetime)
{
    nrAssert(image.valid(), "{} image is invalid.", debugName);

    return addResource(GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = state.common.initialized ? state.common.ownership : ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::StorageWrite,
                ImageUsageIntent::TransferSrc,
            },
        .initialLayout = state.common.initialized ? state.layout : ImageLayoutIntent::Undefined,
        .initialAccessScope = state.common.initialized ? state.common.access : AccessScope{},
        .importedResource = std::cref(image),
        .retainedState = std::ref(state),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledColor(const nr::rhi::Image &image,
                                                                       std::string_view debugName, vk::Extent2D extent,
                                                                       vk::Format format, ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::ColorAttachment,
                           ImageUsageIntent::Sampled,
                       });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledImage(const nr::rhi::Image &image,
                                                                       std::string_view debugName, vk::Extent3D extent,
                                                                       vk::Format format, ResourceLifetime lifetime,
                                                                       ResourceOwnershipDomain initialOwnership)
{
    nrAssert(image.valid(), "{} image is invalid.", debugName);

    return addResource(GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .extent = extent,
        .format = format,
        .usageIntents =
            {
                ImageUsageIntent::Sampled,
            },
        .initialLayout = ImageLayoutIntent::ShaderReadOnly,
        .importedResource = std::cref(image),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importDepth(const nr::rhi::Image &image, std::string_view debugName,
                                                                vk::Extent2D extent, vk::Format format,
                                                                ResourceLifetime lifetime)
{
    return importImage(image, debugName, extent, format, lifetime,
                       {
                           ImageUsageIntent::DepthStencilAttachment,
                       },
                       ImageAspectIntent::Depth);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importBuffer(const nr::rhi::Buffer &buffer,
                                                                 std::string_view debugName, ResourceLifetime lifetime,
                                                                 std::initializer_list<BufferUsageIntent> usageIntents,
                                                                 ResourceOwnershipDomain initialOwnership)
{
    nrAssert(buffer.valid(), "{} buffer is invalid.", debugName);

    return addResource(GraphImportedBufferDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .size = buffer.size(),
        .usageIntents = std::vector<BufferUsageIntent>{usageIntents},
        .importedResource = std::cref(buffer),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importAccelerationStructure(
    const nr::rhi::AccelerationStructureResource &accelerationStructure, std::string_view debugName,
    ResourceLifetime lifetime, ResourceOwnershipDomain initialOwnership)
{
    nrAssert(accelerationStructure.valid(), "{} acceleration structure is invalid.", debugName);

    return addResource(GraphImportedAccelerationStructureDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .initialOwnership = initialOwnership,
        .type = accelerationStructure.type(),
        .size = accelerationStructure.size(),
        .importedResource = std::cref(accelerationStructure),
    });
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSwapchain(std::string_view debugName,
                                                                    const NodeFrameParameters &frameParameters)
{
    return addResource(GraphImportedSwapchainImageDesc{
        .debugName = std::string(debugName),
        .extent =
            vk::Extent3D{
                frameParameters.swapchainExtent.width,
                frameParameters.swapchainExtent.height,
                1,
            },
        .format = frameParameters.swapchainFormat,
    });
}

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(std::span<const PassResourceUseDesc> intentList,
                                                         std::string_view debugName, PassRecordCallback executeLambda,
                                                         PassPrepareCallback prepareCallback, bool isCopyPass,
                                                         vk::PipelineStageFlags2 shaderStages,
                                                         std::span<const GraphFrameDataHandle> frameDataUses)
{
    return graphBuilder.get().addPass(debugName, nodeHandle, intentList, std::move(executeLambda),
                                      std::move(prepareCallback), isCopyPass, shaderStages, frameDataUses);
}

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(std::span<const PassResourceUseDesc> intentList,
                                                        std::string_view debugName,
                                                         PassParallelRecordDesc parallelRecord,
                                                         PassPrepareCallback prepareCallback,
                                                         vk::PipelineStageFlags2 shaderStages,
                                                         std::span<const GraphFrameDataHandle> frameDataUses)
{
    return graphBuilder.get().addPass(debugName, nodeHandle, intentList, std::move(parallelRecord),
                                      std::move(prepareCallback), shaderStages, frameDataUses);
}

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSubmitNode(std::string_view debugName)
{
    return graphBuilder.get().addSubmitNode(debugName);
}

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSwapchainAcquireNode(std::string_view debugName)
{
    return graphBuilder.get().addSubmitNode(debugName, SubmitBoundaryKind::SwapchainAcquire);
}

[[nodiscard]] GraphResourceHandle NodeBuildContext::importImage(const nr::rhi::Image &image, std::string_view debugName,
                                                                vk::Extent2D extent, vk::Format format,
                                                                ResourceLifetime lifetime,
                                                                std::initializer_list<ImageUsageIntent> usageIntents,
                                                                ImageAspectIntent aspect)
{
    nrAssert(image.valid(), "{} image is invalid.", debugName);

    auto desc = GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = lifetime,
        .extent = vk::Extent3D{extent.width, extent.height, 1},
        .format = format,
        .usageIntents = std::vector<ImageUsageIntent>(usageIntents),
        .importedResource = std::cref(image),
    };

    if (aspect != ImageAspectIntent::Color)
    {
        desc.aspect = aspect;
    }

    return addResource(desc);
}

[[nodiscard]] std::size_t NodeBuildContext::frameSlotIndex(std::size_t frameSlotCount) const
{
    nrAssert(frameSlotCount > 0, "NodeBuildContext frame resource helper requires at least one frame slot.");
    return static_cast<std::size_t>(frameIndex) % frameSlotCount;
}

[[nodiscard]] std::string NodeBuildContext::indexedFrameDebugName(std::string_view debugName, std::size_t frameSlot)
{
    return std::format("{}[{}]", debugName, frameSlot);
}

void FrameUniformArena::initialize(nr::rhi::Device &device, vk::DeviceSize bytesPerFrame, std::string_view debugName)
{
    nrAssert(bytesPerFrame > 0u, "FrameUniformArena::initialize requires bytesPerFrame > 0.");

    debugName_ = debugName;

    auto const limits = device.physicalDevice.getProperties().limits;
    uniformOffsetAlignment_ = std::max<vk::DeviceSize>(1u, limits.minUniformBufferOffsetAlignment);
    maxUniformBufferRange_ = limits.maxUniformBufferRange;
    frameSliceSize_ = alignUp(bytesPerFrame, uniformOffsetAlignment_);

    auto bufferInfo = vk::BufferCreateInfo{};
    bufferInfo.size = frameSliceSize_ * nr::maxFrameInFlight;
    bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
    bufferInfo.sharingMode = vk::SharingMode::eExclusive;

    buffer_ = device.resourceFactory.createBuffer(bufferInfo, nr::rhi::MemoryUsage::CpuToGpu, debugName_);
    nrAssert(buffer_.valid(), "FrameUniformArena failed to create uniform buffer '{}'.", debugName_);
}

void FrameUniformArena::beginFrame(std::uint32_t frameIndex)
{
    nrAssert(valid(), "FrameUniformArena::beginFrame requires initialized uniform buffer.");
    auto const frameSlot = static_cast<vk::DeviceSize>(frameIndex % nr::maxFrameInFlight);
    currentFrameBaseOffset_ = frameSlot * frameSliceSize_;
    currentFrameCursor_ = 0;
}

[[nodiscard]] bool FrameUniformArena::valid() const noexcept
{
    return buffer_.valid() && frameSliceSize_ > 0u;
}

[[nodiscard]] FrameUniformBinding FrameUniformArena::uploadBytes(RenderGraphBuilder &graphBuilder,
                                                                 std::string_view debugName,
                                                                 std::span<const std::byte> bytes)
{
    nrAssert(valid(), "FrameUniformArena::uploadBytes requires initialized uniform buffer.");
    nrAssert(!bytes.empty(), "FrameUniformArena::uploadBytes requires a non-empty payload.");

    auto const range = static_cast<vk::DeviceSize>(bytes.size_bytes());
    nrAssert(range <= maxUniformBufferRange_,
             "FrameUniformArena payload exceeds maxUniformBufferRange. debugName='{}' range={} max={}", debugName,
             range, maxUniformBufferRange_);
    auto const allocationSize = alignUp(range, uniformOffsetAlignment_);
    nrAssert(currentFrameCursor_ + allocationSize <= frameSliceSize_,
             "FrameUniformArena frame slice overflow. debugName='{}' cursor={} allocation={} frameSliceSize={}",
             debugName, currentFrameCursor_, allocationSize, frameSliceSize_);

    auto const offset = currentFrameBaseOffset_ + currentFrameCursor_;
    buffer_.writeMappedAndFlush(bytes, offset);
    currentFrameCursor_ += allocationSize;

    auto resource = graphBuilder.addResource(GraphImportedBufferDesc{
        .debugName = std::format("{}@{}", debugName, offset),
        .lifetime = ResourceLifetime::FrameLocal,
        .size = buffer_.size(),
        .usageIntents =
            {
                BufferUsageIntent::Uniform,
            },
        .importedResource = std::cref(buffer_),
    });

    return FrameUniformBinding{
        .resource = resource,
        .offset = offset,
        .range = range,
    };
}

[[nodiscard]] vk::DeviceSize FrameUniformArena::alignUp(vk::DeviceSize value, vk::DeviceSize alignment) noexcept
{
    if (alignment <= 1u)
    {
        return value;
    }
    auto const remainder = value % alignment;
    return remainder == 0u ? value : value + (alignment - remainder);
}

RasterPassBuilder::RasterPassBuilder(NodeBuildContext &context, std::string_view debugName,
                                     std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime)
    : Base(context, debugName, std::move(runtime), "RasterPassBuilder")
{
}

RasterPassBuilder &RasterPassBuilder::viewport(vk::Extent2D extent)
{
    viewportExtent_ = extent;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::viewportYMode(RasterViewportYMode mode)
{
    viewportYMode_ = mode;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue)
{
    nrAssert(resource.valid(), "RasterPassBuilder::colorAttachment requires a valid graph resource.");
    colorAttachments_.push_back(RasterColorAttachment{
        .resource = resource,
        .loadOp = vk::AttachmentLoadOp::eClear,
        .clearValue = clearValue,
    });
    Base::resourceUse(use::colorReadWrite(resource));
    return *this;
}

RasterPassBuilder &RasterPassBuilder::depthAttachment(GraphResourceHandle resource)
{
    nrAssert(resource.valid(), "RasterPassBuilder::depthAttachment requires a valid graph resource.");
    depthAttachment_ = RasterDepthAttachment{
        .resource = resource,
        .loadOp = vk::AttachmentLoadOp::eClear,
    };
    Base::resourceUse(use::depthReadWrite(resource));
    return *this;
}

RasterPassBuilder &RasterPassBuilder::rasterState(nr::rhi::MeshRasterState state)
{
    rasterState_ = state;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::primitiveTopology(vk::PrimitiveTopology topology)
{
    primitiveTopology_ = topology;
    return *this;
}

RasterPassBuilder &RasterPassBuilder::record(RasterPassRecordCallback callback)
{
    nrAssert(!parallelItemCountCallback_ && !parallelRangeRecordCallback_,
             "RasterPassBuilder::record conflicts with recordParallel.");
    recordCallback_ = std::move(callback);
    return *this;
}

RasterPassBuilder &RasterPassBuilder::recordParallel(RasterPassItemCountCallback itemCountCallback,
                                                     RasterPassRangeRecordCallback rangeRecordCallback)
{
    nrAssert(static_cast<bool>(itemCountCallback),
             "RasterPassBuilder::recordParallel requires an item-count callback.");
    nrAssert(static_cast<bool>(rangeRecordCallback),
             "RasterPassBuilder::recordParallel requires a range-record callback.");
    nrAssert(!recordCallback_, "RasterPassBuilder::recordParallel conflicts with record.");
    parallelItemCountCallback_ = std::move(itemCountCallback);
    parallelRangeRecordCallback_ = std::move(rangeRecordCallback);
    return *this;
}

[[nodiscard]] RasterPassBuilder::RasterPassRenderingSetup RasterPassBuilder::makeRenderingSetup(
    const PassRecordContext &recordContext, std::span<const RasterColorAttachment> colorAttachments,
    const std::optional<RasterDepthAttachment> &depthAttachment, std::optional<vk::Extent2D> viewportExtent,
    std::string_view debugName)
{
    nrAssert(static_cast<bool>(recordContext.resolveImage),
             "RasterPassBuilder record requires image resolver callback.");

    auto setup = RasterPassRenderingSetup{};
    setup.resolvedColors =
        colorAttachments | std::views::transform([&](const RasterColorAttachment &attachment) {
            auto image = recordContext.resolveImage(attachment.resource);
            nrAssert(image.has_value(), "RasterPassBuilder failed to resolve color image for pass '{}'.", debugName);
            nrAssert(image->view != vk::ImageView{}, "RasterPassBuilder pass '{}' requires a valid color image view.",
                     debugName);
            return *image;
        }) |
        std::ranges::to<std::vector>();

    if (depthAttachment.has_value())
    {
        auto depthImage = recordContext.resolveImage(depthAttachment->resource);
        nrAssert(depthImage.has_value(), "RasterPassBuilder failed to resolve depth image for pass '{}'.", debugName);
        nrAssert(depthImage->view != vk::ImageView{}, "RasterPassBuilder pass '{}' requires a valid depth image view.",
                 debugName);
        setup.resolvedDepth = *depthImage;
    }

    setup.targetExtent = resolveTargetExtent(viewportExtent, setup.resolvedColors, setup.resolvedDepth);
    setup.colorAttachments = std::views::iota(std::size_t{0}, colorAttachments.size()) |
                             std::views::transform([&](std::size_t attachmentIndex) {
                                 auto const &attachment = colorAttachments[attachmentIndex];
                                 auto const &image = setup.resolvedColors[attachmentIndex];
                                 return nr::rhi::ops::RenderingAttachmentDesc{
                                     .imageView = image.view,
                                     .loadOp = attachment.loadOp,
                                     .storeOp = attachment.storeOp,
                                     .clearValue = attachment.clearValue,
                                 };
                             }) |
                             std::ranges::to<std::vector>();

    if (depthAttachment.has_value() && setup.resolvedDepth.has_value())
    {
        setup.depthAttachment = nr::rhi::ops::RenderingDepthStencilAttachmentDesc{
            .imageView = setup.resolvedDepth->view,
            .depthLoadOp = depthAttachment->loadOp,
            .depthStoreOp = depthAttachment->storeOp,
            .stencilLoadOp = depthAttachment->stencilLoadOp,
            .stencilStoreOp = depthAttachment->stencilStoreOp,
            .clearValue = depthAttachment->clearValue,
        };
    }

    return setup;
}

[[nodiscard]] PassPrimaryRecordScope RasterPassBuilder::makeDynamicRenderingSecondaryScope(
    const RasterPassRenderingSetup &setup, const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime,
    std::string_view debugName)
{
    auto const &graphicsDesc = runtime.state().graphicsDesc;
    nrAssert(graphicsDesc.has_value(),
             "RasterPassBuilder pass '{}' requires retained graphics pipeline dynamic-rendering state.", debugName);

    auto dynamicRendering = PassDynamicRenderingSecondaryScope{
        .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
        .colorAttachments = setup.colorAttachments,
        .depthAttachment = setup.depthAttachment,
        .colorAttachmentFormats = graphicsDesc->colorAttachmentFormats,
        .depthAttachmentFormat = graphicsDesc->depthAttachmentFormat.value_or(vk::Format::eUndefined),
        .stencilAttachmentFormat = graphicsDesc->stencilAttachmentFormat.value_or(vk::Format::eUndefined),
        .rasterizationSamples = graphicsDesc->sampleCount,
    };
    if (graphicsDesc->stencilAttachmentFormat.has_value() && setup.depthAttachment.has_value())
    {
        dynamicRendering.stencilAttachment = setup.depthAttachment;
    }

    return PassPrimaryRecordScope{
        .kind = PassPrimaryRecordScopeKind::DynamicRenderingSecondaryContents,
        .dynamicRendering = std::move(dynamicRendering),
    };
}

void RasterPassBuilder::bindGraphicsSetup(const vk::raii::CommandBuffer &commandBuffer,
                                          const PipelineRuntime<nr::rhi::GraphicsPipeline> &runtime,
                                          PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding,
                                          const nr::rhi::ShaderBindingSnapshot &bindingSnapshot,
                                          std::uint32_t frameIndex, vk::Extent2D targetExtent,
                                          RasterViewportYMode viewportYMode, nr::rhi::MeshRasterState rasterState,
                                          vk::PrimitiveTopology primitiveTopology)
{
    Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, runtime, passBinding, bindingSnapshot,
                                                        frameIndex);

    auto viewport = vk::Viewport{};
    viewport.x = 0.0f;
    viewport.width = static_cast<float>(targetExtent.width);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    if (viewportYMode == RasterViewportYMode::ClipSpaceYUp)
    {
        viewport.y = static_cast<float>(targetExtent.height);
        viewport.height = -static_cast<float>(targetExtent.height);
    }
    else
    {
        viewport.y = 0.0f;
        viewport.height = static_cast<float>(targetExtent.height);
    }
    commandBuffer.setViewport(0, {viewport});
    commandBuffer.setScissor(0, {vk::Rect2D{vk::Offset2D{0, 0}, targetExtent}});
    commandBuffer.setPrimitiveTopology(primitiveTopology);
    nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);
}

[[nodiscard]] GraphPassHandle RasterPassBuilder::build()
{
    nrAssert(!colorAttachments_.empty(), "RasterPassBuilder::build requires at least one color attachment.");
    auto const hasSerialRecord = static_cast<bool>(recordCallback_);
    auto const hasParallelRecord =
        static_cast<bool>(parallelItemCountCallback_) || static_cast<bool>(parallelRangeRecordCallback_);
    nrAssert(hasSerialRecord != hasParallelRecord,
             "RasterPassBuilder::build requires exactly one serial record or parallel record callback.");
    nrAssert(!hasParallelRecord || (parallelItemCountCallback_ && parallelRangeRecordCallback_),
             "RasterPassBuilder::build parallel record requires both item-count and range-record callbacks.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto frameDataUses = std::move(common.frameDataUses);
    auto runtime = std::move(common.runtime);
    auto passBinding = common.passBinding;
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto colorAttachments = std::move(colorAttachments_);
    auto depthAttachment = depthAttachment_;
    auto viewportExtent = viewportExtent_;
    auto viewportYMode = viewportYMode_;
    auto rasterState = rasterState_;
    auto primitiveTopology = primitiveTopology_;
    auto recordCallback = std::move(recordCallback_);
    auto parallelItemCountCallback = std::move(parallelItemCountCallback_);
    auto parallelRangeRecordCallback = std::move(parallelRangeRecordCallback_);

    if (parallelItemCountCallback && parallelRangeRecordCallback)
    {
        auto parallelRecord = PassParallelRecordDesc{
            .itemCount = std::move(parallelItemCountCallback),
            .primaryScope =
                [runtime, colorAttachments, depthAttachment, viewportExtent,
                 debugName](const PassRecordContext &recordContext) {
                    nrAssert(static_cast<bool>(runtime),
                             "RasterPassBuilder primary scope requires initialized runtime state.");
                    auto setup = makeRenderingSetup(
                        recordContext,
                        std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                        depthAttachment, viewportExtent, debugName);
                    return makeDynamicRenderingSecondaryScope(setup, *runtime, debugName);
                },
            .recordRange =
                [runtime, passBinding, colorAttachments, depthAttachment, bindingSnapshot, viewportExtent, viewportYMode,
                 rasterState, primitiveTopology, debugName,
                 rangeRecordCallback =
                     std::move(parallelRangeRecordCallback)](const PassRangeRecordContext &rangeContext) {
                    nrAssert(static_cast<bool>(runtime),
                             "RasterPassBuilder range record requires initialized runtime state.");

                    auto setup = makeRenderingSetup(
                        rangeContext.pass,
                        std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                        depthAttachment, viewportExtent, debugName);
                    auto &commandBuffer = rangeContext.commandBuffer.get();
                    bindGraphicsSetup(commandBuffer, *runtime, passBinding, bindingSnapshot,
                                      rangeContext.pass.frameIndex,
                                      setup.targetExtent, viewportYMode, rasterState, primitiveTopology);

                    rangeRecordCallback(RasterPassRangeRecordContext{
                        .pass = rangeContext.pass,
                        .plan = rangeContext.plan.get(),
                        .chunkIndex = rangeContext.chunkIndex,
                        .range = rangeContext.range,
                        .commandBuffer = commandBuffer,
                        .pipelineLayout = runtime->state().layout,
                        .extent = setup.targetExtent,
                    });
                },
        };

        return context_.get().addPass(std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
                                      debugName, std::move(parallelRecord), std::move(prepareCallback),
                                      vk::PipelineStageFlagBits2::eAllGraphics,
                                      std::span<const GraphFrameDataHandle>{frameDataUses.data(),
                                                                            frameDataUses.size()});
    }

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, passBinding, colorAttachments, depthAttachment, bindingSnapshot, viewportExtent, viewportYMode,
         rasterState,
         primitiveTopology, debugName,
         recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(),
                     "RasterPassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "RasterPassBuilder record requires initialized runtime state.");

            auto setup = makeRenderingSetup(
                recordContext, std::span<const RasterColorAttachment>{colorAttachments.data(), colorAttachments.size()},
                depthAttachment, viewportExtent, debugName);

            auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, setup.targetExtent},
                .colorAttachments =
                    std::span<const nr::rhi::ops::RenderingAttachmentDesc>{setup.colorAttachments.data(),
                                                                           setup.colorAttachments.size()},
                .depthAttachment = setup.depthAttachment,
                .stencilAttachment = setup.stencilAttachment,
            };

            auto &commandBuffer = recordContext.commandBuffer->get();
            auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
            bindGraphicsSetup(commandBuffer, *runtime, passBinding, bindingSnapshot, recordContext.frameIndex,
                              setup.targetExtent, viewportYMode, rasterState, primitiveTopology);

            recordCallback(RasterPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .pipelineLayout = runtime->state().layout,
                .extent = setup.targetExtent,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eAllGraphics,
        std::span<const GraphFrameDataHandle>{frameDataUses.data(), frameDataUses.size()});
}

[[nodiscard]] vk::Extent2D RasterPassBuilder::resolveTargetExtent(std::optional<vk::Extent2D> viewportExtent,
                                                                  std::span<const PassImageResource> resolvedColors,
                                                                  const std::optional<PassImageResource> &resolvedDepth)
{
    nrAssert(!resolvedColors.empty(),
             "RasterPassBuilder::resolveTargetExtent requires at least one resolved color image.");

    auto targetExtent = viewportExtent.value_or(vk::Extent2D{
        resolvedColors.front().extent.width,
        resolvedColors.front().extent.height,
    });

    std::ranges::for_each(resolvedColors, [&](const PassImageResource &image) {
        targetExtent = vk::Extent2D{
            std::max(1u, std::min(targetExtent.width, image.extent.width)),
            std::max(1u, std::min(targetExtent.height, image.extent.height)),
        };
    });

    if (resolvedDepth.has_value())
    {
        targetExtent = vk::Extent2D{
            std::max(1u, std::min(targetExtent.width, resolvedDepth->extent.width)),
            std::max(1u, std::min(targetExtent.height, resolvedDepth->extent.height)),
        };
    }

    return targetExtent;
}

ComputePassBuilder::ComputePassBuilder(NodeBuildContext &context, std::string_view debugName,
                                       std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime)
    : Base(context, debugName, std::move(runtime), "ComputePassBuilder")
{
}

ComputePassBuilder &ComputePassBuilder::record(ComputePassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

[[nodiscard]] GraphPassHandle ComputePassBuilder::build()
{
    nrAssert(static_cast<bool>(recordCallback_), "ComputePassBuilder::build requires a record callback.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto frameDataUses = std::move(common.frameDataUses);
    auto runtime = std::move(common.runtime);
    auto passBinding = common.passBinding;
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto recordCallback = std::move(recordCallback_);

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, passBinding, bindingSnapshot,
         recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(),
                     "ComputePassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "ComputePassBuilder record requires initialized runtime state.");

            auto &commandBuffer = recordContext.commandBuffer->get();
            Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, *runtime, passBinding,
                                                                bindingSnapshot, recordContext.frameIndex);

            recordCallback(ComputePassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .pipelineLayout = runtime->state().layout,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eComputeShader,
        std::span<const GraphFrameDataHandle>{frameDataUses.data(), frameDataUses.size()});
}

RayTracingPassBuilder::RayTracingPassBuilder(NodeBuildContext &context, std::string_view debugName,
                                             std::shared_ptr<PipelineRuntime<nr::rhi::RayTracingPipeline>> runtime)
    : Base(context, debugName, std::move(runtime), "RayTracingPassBuilder")
{
}

RayTracingPassBuilder &RayTracingPassBuilder::record(RayTracingPassRecordCallback callback)
{
    recordCallback_ = std::move(callback);
    return *this;
}

[[nodiscard]] GraphPassHandle RayTracingPassBuilder::build()
{
    nrAssert(static_cast<bool>(recordCallback_), "RayTracingPassBuilder::build requires a record callback.");

    auto common = takeCommonBuildState();
    auto resourceUses = std::move(common.resourceUses);
    auto frameDataUses = std::move(common.frameDataUses);
    auto runtime = std::move(common.runtime);
    auto passBinding = common.passBinding;
    auto debugName = std::move(common.debugName);
    auto bindingSnapshot = std::move(common.bindingSnapshot);
    auto prepareCallback = std::move(common.prepareCallback);
    auto recordCallback = std::move(recordCallback_);

    return context_.get().addPass(
        std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()}, debugName,
        [runtime, passBinding, bindingSnapshot,
         recordCallback = std::move(recordCallback)](const PassRecordContext &recordContext) {
            nrAssert(recordContext.commandBuffer.has_value(),
                     "RayTracingPassBuilder record requires RAII command buffer access.");
            nrAssert(static_cast<bool>(runtime), "RayTracingPassBuilder record requires initialized runtime state.");

            auto &commandBuffer = recordContext.commandBuffer->get();
            Base::bindPipelinePreparedResourcesAndPushConstants(commandBuffer, *runtime, passBinding,
                                                                bindingSnapshot, recordContext.frameIndex);

            recordCallback(RayTracingPassRecordContext{
                .pass = recordContext,
                .commandBuffer = commandBuffer,
                .pipelineLayout = runtime->state().layout,
            });
        },
        std::move(prepareCallback), false, vk::PipelineStageFlagBits2::eRayTracingShaderKHR,
        std::span<const GraphFrameDataHandle>{frameDataUses.data(), frameDataUses.size()});
}

} // namespace nr::renderer
