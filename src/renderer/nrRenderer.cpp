module nr.renderer;
import :renderer;
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
namespace
{
struct RendererGlobalFrameUniforms
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
};

[[nodiscard]] RendererGlobalFrameUniforms makeGlobalFrameUniforms(
    const nr::scene::SceneBridgeFrameConstants& frameConstants) noexcept
{
    return RendererGlobalFrameUniforms{
        .view = frameConstants.view,
        .projection = frameConstants.projection,
        .viewProjection = frameConstants.viewProjection,
    };
}

[[nodiscard]] double elapsedMilliseconds(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) noexcept
{
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }

void accumulateCpuTimings(RendererCpuFrameTimings& target, const RendererCpuFrameTimings& sample) noexcept
{
        target.cpuWaitGpuMilliseconds += sample.cpuWaitGpuMilliseconds;
        target.frameSetupMilliseconds += sample.frameSetupMilliseconds;
        target.sceneMilliseconds += sample.sceneMilliseconds;
        target.buildMilliseconds += sample.buildMilliseconds;
        target.compileMilliseconds += sample.compileMilliseconds;
        target.prepareMilliseconds += sample.prepareMilliseconds;
        target.executeMilliseconds += sample.executeMilliseconds;
        target.presentMilliseconds += sample.presentMilliseconds;
        target.totalMilliseconds += sample.totalMilliseconds;
    }

[[nodiscard]] RendererCpuFrameTimings averageCpuTimings(
    const RendererCpuFrameTimings& total,
    std::uint32_t frameCount) noexcept
{
        auto const divisor = static_cast<double>(std::max(frameCount, 1u));
        return RendererCpuFrameTimings{
            .cpuWaitGpuMilliseconds = total.cpuWaitGpuMilliseconds / divisor,
            .frameSetupMilliseconds = total.frameSetupMilliseconds / divisor,
            .sceneMilliseconds = total.sceneMilliseconds / divisor,
            .buildMilliseconds = total.buildMilliseconds / divisor,
            .compileMilliseconds = total.compileMilliseconds / divisor,
            .prepareMilliseconds = total.prepareMilliseconds / divisor,
            .executeMilliseconds = total.executeMilliseconds / divisor,
            .presentMilliseconds = total.presentMilliseconds / divisor,
            .totalMilliseconds = total.totalMilliseconds / divisor,
        };
    }

[[nodiscard]] std::vector<RendererGpuPassAverage> averageGpuPassTimings(
        const std::map<std::pair<std::uint32_t, std::string>, RendererGpuPassAverage>& totals)
{
        auto averages = totals |
                        std::views::values |
                        std::views::transform([](const RendererGpuPassAverage& total) {
                            auto average = total;
                            auto const divisor = static_cast<double>(std::max(average.sampleCount, 1u));
                            average.milliseconds /= divisor;
                            return average;
                        }) |
                        std::ranges::to<std::vector>();

        std::ranges::sort(averages, [](const RendererGpuPassAverage& lhs, const RendererGpuPassAverage& rhs) {
            return std::tie(lhs.pass.value, lhs.debugName) < std::tie(rhs.pass.value, rhs.debugName);
        });
        return averages;
    }
} // namespace

[[nodiscard]] GraphResourceHandle NodeBuildContext::resolveInput(std::string_view portName) const
{
        if (!resolveInputPort)
        {
            return {};
        }
        return resolveInputPort(portName);
    }

void NodeBuildContext::publishOutput(std::string_view portName, GraphResourceHandle resource)
{
        if (!publishOutputPort)
        {
            return;
        }
        publishOutputPort(portName, resource);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::transientColor(
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format)
{
        return addResource(GraphTransientImageDesc{
            .debugName = std::string(debugName),
            .extent = vk::Extent3D{extent.width, extent.height, 1},
            .format = format,
            .usageIntents = {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            },
            .initialLayout = ImageLayoutIntent::ColorAttachment,
        });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::TransferSrc,
                ImageUsageIntent::Sampled,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importStorageColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::StorageWrite,
                ImageUsageIntent::TransferSrc,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSampledColor(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::ColorAttachment,
                ImageUsageIntent::Sampled,
            });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importDepth(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime)
{
        return importImage(
            image,
            debugName,
            extent,
            format,
            lifetime,
            {
                ImageUsageIntent::DepthStencilAttachment,
            },
            ImageAspectIntent::Depth);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importAccelerationStructure(
        const nr::rhi::AccelerationStructureResource& accelerationStructure,
        std::string_view debugName,
        ResourceLifetime lifetime,
        ResourceOwnershipDomain initialOwnership)
{
        nrAssert(accelerationStructure.valid(), std::format("{} acceleration structure is invalid.", debugName));

        return addResource(GraphImportedAccelerationStructureDesc{
            .debugName = std::string(debugName),
            .lifetime = lifetime,
            .initialOwnership = initialOwnership,
            .type = accelerationStructure.type(),
            .size = accelerationStructure.size(),
            .importedResource = std::cref(accelerationStructure),
        });
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importSwapchain(
        std::string_view debugName,
        const NodeFrameParameters& frameParameters)
{
        return addResource(GraphImportedSwapchainImageDesc{
            .debugName = std::string(debugName),
            .swapchainImageIndex = frameParameters.swapchainImageIndex,
            .extent = vk::Extent3D{
                frameParameters.swapchainExtent.width,
                frameParameters.swapchainExtent.height,
                1,
            },
            .format = frameParameters.swapchainFormat,
        });
    }

[[nodiscard]] GraphPassHandle NodeBuildContext::addPass(
        std::span<const PassResourceUseDesc> intentList,
        std::string_view debugName,
        PassRecordCallback executeLambda,
        PassPrepareCallback prepareCallback,
        bool isCopyPass)
{
        return graphBuilder.get().addPass(
            debugName,
            nodeHandle,
            intentList,
            std::move(executeLambda),
            std::move(prepareCallback),
            isCopyPass);
    }

[[nodiscard]] GraphSubmitHandle NodeBuildContext::addSubmitNode(
        std::string_view debugName)
{
        return graphBuilder.get().addSubmitNode(debugName);
    }

[[nodiscard]] GraphResourceHandle NodeBuildContext::importImage(
        const nr::rhi::Image& image,
        std::string_view debugName,
        vk::Extent2D extent,
        vk::Format format,
        ResourceLifetime lifetime,
        std::initializer_list<ImageUsageIntent> usageIntents,
        ImageAspectIntent aspect)
{
        nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

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

void FrameUniformArena::initialize(nr::rhi::Device& device, vk::DeviceSize bytesPerFrame, std::string_view debugName)
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

        buffer_ = device.resourceFactory.createBuffer(
            bufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            debugName_);
        nrAssert(buffer_.valid(), std::format("FrameUniformArena failed to create uniform buffer '{}'.", debugName_));
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

[[nodiscard]] FrameUniformBinding FrameUniformArena::uploadBytes(
        RenderGraphBuilder& graphBuilder,
        std::string_view debugName,
        std::span<const std::byte> bytes)
{
        nrAssert(valid(), "FrameUniformArena::uploadBytes requires initialized uniform buffer.");
        nrAssert(!bytes.empty(), "FrameUniformArena::uploadBytes requires a non-empty payload.");

        auto const range = static_cast<vk::DeviceSize>(bytes.size_bytes());
        nrAssert(
            range <= maxUniformBufferRange_,
            std::format(
                "FrameUniformArena payload exceeds maxUniformBufferRange. debugName='{}' range={} max={}",
                debugName,
                range,
                maxUniformBufferRange_));
        auto const allocationSize = alignUp(range, uniformOffsetAlignment_);
        nrAssert(
            currentFrameCursor_ + allocationSize <= frameSliceSize_,
            std::format(
                "FrameUniformArena frame slice overflow. debugName='{}' cursor={} allocation={} frameSliceSize={}",
                debugName,
                currentFrameCursor_,
                allocationSize,
                frameSliceSize_));

        auto const offset = currentFrameBaseOffset_ + currentFrameCursor_;
        buffer_.writeMappedAndFlush(bytes, offset);
        currentFrameCursor_ += allocationSize;

        auto resource = graphBuilder.addResource(GraphImportedBufferDesc{
            .debugName = std::format("{}@{}", debugName, offset),
            .lifetime = ResourceLifetime::FrameLocal,
            .size = buffer_.size(),
            .usageIntents = {
                BufferUsageIntent::Uniform,
            },
            .importedResource = std::ref(buffer_),
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

RasterPassBuilder::RasterPassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::GraphicsPipeline>> runtime)
        : context_(context)
        , debugName_(debugName)
        , runtime_(std::move(runtime))
{
        nrAssert(static_cast<bool>(runtime_), "RasterPassBuilder requires a valid PipelineRuntime shared pointer.");
        nrAssert(runtime_->valid(), "RasterPassBuilder requires initialized PipelineRuntime state.");
        rootCursor_ = runtime_->rootCursor();
    }

RasterPassBuilder& RasterPassBuilder::viewport(vk::Extent2D extent)
{
        viewportExtent_ = extent;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::colorAttachment(GraphResourceHandle resource, vk::ClearValue clearValue)
{
        nrAssert(resource.valid(), "RasterPassBuilder::colorAttachment requires a valid graph resource.");
        colorAttachments_.push_back(RasterColorAttachment{
            .resource = resource,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .clearValue = clearValue,
        });
        resourceUses_.push_back(use::colorReadWrite(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::depthAttachment(GraphResourceHandle resource)
{
        nrAssert(resource.valid(), "RasterPassBuilder::depthAttachment requires a valid graph resource.");
        depthAttachment_ = RasterDepthAttachment{
            .resource = resource,
            .loadOp = vk::AttachmentLoadOp::eClear,
        };
        resourceUses_.push_back(use::depthReadWrite(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::uniform(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
        nrAssert(resource.valid(), "RasterPassBuilder::uniform requires a valid graph resource.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::uniformRead(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::uniform(std::string_view shaderPath, FrameUniformBinding binding, std::string_view debugName)
{
        nrAssert(binding.resource.valid(), "RasterPassBuilder::uniform requires a valid frame uniform resource.");
        nrAssert(binding.range > 0u, "RasterPassBuilder::uniform requires a non-zero frame uniform range.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = binding.resource.value,
            .debugName = std::string(debugName),
            .offset = binding.offset,
            .range = binding.range,
        }));
        resourceUses_.push_back(use::uniformRead(binding.resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
        nrAssert(resource.valid(), "RasterPassBuilder::sampledImage requires a valid graph resource.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        resourceUses_.push_back(use::sampledRead(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
        nrAssert(resource.valid(), "RasterPassBuilder::storageImage requires a valid graph resource.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::storageWrite(resource));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::rasterState(nr::rhi::MeshRasterState state)
{
        rasterState_ = state;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::primitiveTopology(vk::PrimitiveTopology topology)
{
        primitiveTopology_ = topology;
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::record(RasterPassRecordCallback callback)
{
        recordCallback_ = std::move(callback);
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::resourceUse(PassResourceUseDesc resourceUse)
{
        nrAssert(resourceUse.resource.valid(), "RasterPassBuilder::resourceUse requires a valid graph resource.");
        resourceUses_.push_back(resourceUse);
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::prepare(RasterPassPrepareCallback callback)
{
        prepareCallbacks_.push_back(std::move(callback));
        return *this;
    }

RasterPassBuilder& RasterPassBuilder::dynamicBindingSnapshot(
        PassBindingSnapshotCallback snapshotCallback,
        nr::rhi::LogicalDescriptorResolver resolver)
{
        nrAssert(static_cast<bool>(snapshotCallback), "RasterPassBuilder::dynamicBindingSnapshot requires a snapshot callback.");
        dynamicBindingSnapshots_.push_back(DynamicBindingSnapshotDesc{
            .snapshot = std::move(snapshotCallback),
            .resolver = std::move(resolver),
        });
        return *this;
    }

[[nodiscard]] GraphPassHandle RasterPassBuilder::build()
{
        nrAssert(!colorAttachments_.empty(), "RasterPassBuilder::build requires at least one color attachment.");
        nrAssert(static_cast<bool>(recordCallback_), "RasterPassBuilder::build requires a record callback.");

        auto bindingSnapshot = rootCursor_.snapshot();
        rootCursor_.clearSnapshot();

        auto resourceUses = std::move(resourceUses_);
        auto runtime = runtime_;
        auto colorAttachments = std::move(colorAttachments_);
        auto depthAttachment = depthAttachment_;
        auto debugName = debugName_;
        auto viewportExtent = viewportExtent_;
        auto rasterState = rasterState_;
        auto primitiveTopology = primitiveTopology_;
        auto recordCallback = std::move(recordCallback_);
        auto prepareCallbacks = std::move(prepareCallbacks_);
        auto dynamicBindingSnapshots = std::move(dynamicBindingSnapshots_);

        return context_.get().addPass(
            std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
            debugName,
            [runtime,
             colorAttachments,
             depthAttachment,
             bindingSnapshot,
             viewportExtent,
             rasterState,
             primitiveTopology,
             debugName,
             recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
                nrAssert(static_cast<bool>(recordContext.resolveImage), "RasterPassBuilder record requires image resolver callback.");
                nrAssert(recordContext.commandBuffer.has_value(), "RasterPassBuilder record requires RAII command buffer access.");
                nrAssert(static_cast<bool>(runtime), "RasterPassBuilder record requires initialized runtime state.");

                auto resolvedColors = colorAttachments |
                                      std::views::transform([&](const RasterColorAttachment& attachment) {
                                          auto image = recordContext.resolveImage(attachment.resource);
                                          nrAssert(
                                              image.has_value(),
                                              std::format("RasterPassBuilder failed to resolve color image for pass '{}'.", debugName));
                                          nrAssert(
                                              image->view != vk::ImageView{},
                                              std::format("RasterPassBuilder pass '{}' requires a valid color image view.", debugName));
                                          return *image;
                                      }) |
                                      std::ranges::to<std::vector>();

                auto resolvedDepth = std::optional<PassImageResource>{};
                if (depthAttachment.has_value())
                {
                    auto depthImage = recordContext.resolveImage(depthAttachment->resource);
                    nrAssert(
                        depthImage.has_value(),
                        std::format("RasterPassBuilder failed to resolve depth image for pass '{}'.", debugName));
                    nrAssert(
                        depthImage->view != vk::ImageView{},
                        std::format("RasterPassBuilder pass '{}' requires a valid depth image view.", debugName));
                    resolvedDepth = *depthImage;
                }

                auto targetExtent = resolveTargetExtent(viewportExtent, resolvedColors, resolvedDepth);
                auto renderingAttachments = std::views::iota(std::size_t{0}, colorAttachments.size()) |
                                            std::views::transform([&](std::size_t attachmentIndex) {
                                                auto const& attachment = colorAttachments[attachmentIndex];
                                                auto const& image = resolvedColors[attachmentIndex];
                                                return nr::rhi::ops::RenderingAttachmentDesc{
                                                    .imageView = image.view,
                                                    .loadOp = attachment.loadOp,
                                                    .storeOp = attachment.storeOp,
                                                    .clearValue = attachment.clearValue,
                                                };
                                            }) |
                                            std::ranges::to<std::vector>();

                auto renderingDepthAttachment = std::optional<nr::rhi::ops::RenderingDepthStencilAttachmentDesc>{};
                if (depthAttachment.has_value() && resolvedDepth.has_value())
                {
                    renderingDepthAttachment = nr::rhi::ops::RenderingDepthStencilAttachmentDesc{
                        .imageView = resolvedDepth->view,
                        .depthLoadOp = depthAttachment->loadOp,
                        .depthStoreOp = depthAttachment->storeOp,
                        .stencilLoadOp = depthAttachment->stencilLoadOp,
                        .stencilStoreOp = depthAttachment->stencilStoreOp,
                        .clearValue = depthAttachment->clearValue,
                    };
                }

                auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                    .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent},
                    .colorAttachments = std::span<const nr::rhi::ops::RenderingAttachmentDesc>{
                        renderingAttachments.data(),
                        renderingAttachments.size()},
                    .depthAttachment = renderingDepthAttachment,
                };

                auto& commandBuffer = recordContext.commandBuffer->get();
                auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, runtime->pipeline().raw());

                nr::rhi::bindPreparedResourcesToCommandBuffer(
                    commandBuffer,
                    vk::PipelineBindPoint::eGraphics,
                    runtime->state().layout,
                    runtime->bindingSetsForFrame(recordContext.frameIndex));

                nr::rhi::pushConstantsToCommandBuffer(
                    commandBuffer,
                    runtime->state().layout,
                    bindingSnapshot);

                auto viewport = vk::Viewport{
                    0.0f,
                    0.0f,
                    static_cast<float>(targetExtent.width),
                    static_cast<float>(targetExtent.height),
                    0.0f,
                    1.0f,
                };
                commandBuffer.setViewport(0, {viewport});
                commandBuffer.setScissor(0, {vk::Rect2D{vk::Offset2D{0, 0}, targetExtent}});
                commandBuffer.setPrimitiveTopology(primitiveTopology);
                nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);

                recordCallback(RasterPassRecordContext{
                    .pass = recordContext,
                    .commandBuffer = commandBuffer,
                    .descriptorLayout = runtime->state().descriptorLayout,
                    .pipelineLayout = runtime->state().layout,
                    .extent = targetExtent,
                });
            },
            [runtime,
             bindingSnapshot,
             prepareCallbacks,
             dynamicBindingSnapshots](const PassPrepareContext& prepareContext) {
                nrAssert(static_cast<bool>(runtime), "RasterPassBuilder prepare requires initialized runtime state.");
                std::ranges::for_each(prepareCallbacks, [&](const RasterPassPrepareCallback& callback) {
                    if (callback)
                    {
                        callback(prepareContext);
                    }
                });

                nr::rhi::updateResourcesForBindingSnapshot(
                    runtime->state().bindingPool,
                    runtime->bindingSetsForFrame(prepareContext.frameIndex),
                    bindingSnapshot,
                    makeDefaultLogicalDescriptorResolver(prepareContext));

                std::ranges::for_each(dynamicBindingSnapshots, [&](const DynamicBindingSnapshotDesc& desc) {
                    auto dynamicSnapshot = desc.snapshot(prepareContext);
                    nr::rhi::updateResourcesForBindingSnapshot(
                        runtime->state().bindingPool,
                        runtime->bindingSetsForFrame(prepareContext.frameIndex),
                        dynamicSnapshot,
                        desc.resolver);
                });
            });
    }

[[nodiscard]] vk::Extent2D RasterPassBuilder::resolveTargetExtent(
        std::optional<vk::Extent2D> viewportExtent,
        std::span<const PassImageResource> resolvedColors,
        const std::optional<PassImageResource>& resolvedDepth)
{
        nrAssert(!resolvedColors.empty(), "RasterPassBuilder::resolveTargetExtent requires at least one resolved color image.");

        auto targetExtent = viewportExtent.value_or(vk::Extent2D{
            resolvedColors.front().extent.width,
            resolvedColors.front().extent.height,
        });

        std::ranges::for_each(resolvedColors, [&](const PassImageResource& image) {
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

ComputePassBuilder::ComputePassBuilder(
        NodeBuildContext& context,
        std::string_view debugName,
        std::shared_ptr<PipelineRuntime<nr::rhi::ComputePipeline>> runtime)
        : context_(context)
        , debugName_(debugName)
        , runtime_(std::move(runtime))
{
        nrAssert(static_cast<bool>(runtime_), "ComputePassBuilder requires a valid PipelineRuntime shared pointer.");
        nrAssert(runtime_->valid(), "ComputePassBuilder requires initialized PipelineRuntime state.");
        rootCursor_ = runtime_->rootCursor();
    }

ComputePassBuilder& ComputePassBuilder::sampledImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
        nrAssert(resource.valid(), "ComputePassBuilder::sampledImage requires a valid graph resource.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        resourceUses_.push_back(use::sampledRead(resource));
        return *this;
    }

ComputePassBuilder& ComputePassBuilder::storageImage(std::string_view shaderPath, GraphResourceHandle resource, std::string_view debugName)
{
        nrAssert(resource.valid(), "ComputePassBuilder::storageImage requires a valid graph resource.");
        auto cursor = rootCursor_.getPath(shaderPath);
        static_cast<void>(cursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = resource.value,
            .debugName = std::string(debugName),
        }));
        resourceUses_.push_back(use::storageWrite(resource));
        return *this;
    }

ComputePassBuilder& ComputePassBuilder::resourceUse(PassResourceUseDesc resourceUse)
{
        nrAssert(resourceUse.resource.valid(), "ComputePassBuilder::resourceUse requires a valid graph resource.");
        resourceUses_.push_back(resourceUse);
        return *this;
    }

ComputePassBuilder& ComputePassBuilder::prepare(ComputePassPrepareCallback callback)
{
        prepareCallbacks_.push_back(std::move(callback));
        return *this;
    }

ComputePassBuilder& ComputePassBuilder::dynamicBindingSnapshot(
        PassBindingSnapshotCallback snapshotCallback,
        nr::rhi::LogicalDescriptorResolver resolver)
{
        nrAssert(static_cast<bool>(snapshotCallback), "ComputePassBuilder::dynamicBindingSnapshot requires a snapshot callback.");
        dynamicBindingSnapshots_.push_back(DynamicBindingSnapshotDesc{
            .snapshot = std::move(snapshotCallback),
            .resolver = std::move(resolver),
        });
        return *this;
    }

ComputePassBuilder& ComputePassBuilder::record(ComputePassRecordCallback callback)
{
        recordCallback_ = std::move(callback);
        return *this;
    }

[[nodiscard]] GraphPassHandle ComputePassBuilder::build()
{
        nrAssert(static_cast<bool>(recordCallback_), "ComputePassBuilder::build requires a record callback.");

        auto bindingSnapshot = rootCursor_.snapshot();
        rootCursor_.clearSnapshot();

        auto resourceUses = std::move(resourceUses_);
        auto runtime = runtime_;
        auto debugName = debugName_;
        auto recordCallback = std::move(recordCallback_);
        auto prepareCallbacks = std::move(prepareCallbacks_);
        auto dynamicBindingSnapshots = std::move(dynamicBindingSnapshots_);

        return context_.get().addPass(
            std::span<const PassResourceUseDesc>{resourceUses.data(), resourceUses.size()},
            debugName,
            [runtime,
             bindingSnapshot,
             recordCallback = std::move(recordCallback)](const PassRecordContext& recordContext) {
                nrAssert(recordContext.commandBuffer.has_value(), "ComputePassBuilder record requires RAII command buffer access.");
                nrAssert(static_cast<bool>(runtime), "ComputePassBuilder record requires initialized runtime state.");

                auto& commandBuffer = recordContext.commandBuffer->get();
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, runtime->pipeline().raw());

                nr::rhi::bindPreparedResourcesToCommandBuffer(
                    commandBuffer,
                    vk::PipelineBindPoint::eCompute,
                    runtime->state().layout,
                    runtime->bindingSetsForFrame(recordContext.frameIndex));

                nr::rhi::pushConstantsToCommandBuffer(
                    commandBuffer,
                    runtime->state().layout,
                    bindingSnapshot);

                recordCallback(ComputePassRecordContext{
                    .pass = recordContext,
                    .commandBuffer = commandBuffer,
                    .descriptorLayout = runtime->state().descriptorLayout,
                    .pipelineLayout = runtime->state().layout,
                });
            },
            [runtime,
             bindingSnapshot,
             prepareCallbacks,
             dynamicBindingSnapshots](const PassPrepareContext& prepareContext) {
                nrAssert(static_cast<bool>(runtime), "ComputePassBuilder prepare requires initialized runtime state.");
                std::ranges::for_each(prepareCallbacks, [&](const ComputePassPrepareCallback& callback) {
                    if (callback)
                    {
                        callback(prepareContext);
                    }
                });

                nr::rhi::updateResourcesForBindingSnapshot(
                    runtime->state().bindingPool,
                    runtime->bindingSetsForFrame(prepareContext.frameIndex),
                    bindingSnapshot,
                    makeDefaultLogicalDescriptorResolver(prepareContext));

                std::ranges::for_each(dynamicBindingSnapshots, [&](const DynamicBindingSnapshotDesc& desc) {
                    auto dynamicSnapshot = desc.snapshot(prepareContext);
                    nr::rhi::updateResourcesForBindingSnapshot(
                        runtime->state().bindingPool,
                        runtime->bindingSetsForFrame(prepareContext.frameIndex),
                        dynamicSnapshot,
                        desc.resolver);
                });
            });
    }

void NodeRuntime::initialize(NodeInitContext&)
{
    }

void NodeRuntime::shutdown(NodeShutdownContext&)
{
    }

void Renderer::initialize(const RendererCreateInfo& info)
{
        if (device_)
        {
            return;
        }

        auto& shaderService = nr::rhi::ShaderService::instance();
        shaderService.configure();

        device_ = std::make_unique<nr::rhi::Device>();
        device_->initialize(info.appName, info.engineName);
        frameUniformArena_.initialize(*device_, info.frameUniformBytesPerFrame, "Renderer.FrameUniformArena");
        submissionTimeline_.initialize(device_->device, 0);
    }

void Renderer::installGraph(const RendererGraphSpec& spec)
{
        nrAssert(static_cast<bool>(device_), "Renderer::installGraph requires initialize() before graph installation.");
        teardownInstalledGraph();

        auto installed = std::vector<InstalledNode>{};
        installed.reserve(spec.nodes.size());

        auto knownNames = std::set<std::string>{};
        auto initContext = NodeInitContext{
            .device = std::ref(*device_),
        };

        std::ranges::for_each(spec.nodes, [&](const NodeCreateInfo& createInfo) {
            nrAssert(static_cast<bool>(createInfo.runtime), "Renderer::installGraph requires a valid node runtime in NodeCreateInfo.");

            auto description = createInfo.runtime->describe();
            auto runtimeName = createInfo.config.instanceName.empty()
                                   ? description.name
                                   : createInfo.config.instanceName;

            nrAssert(!runtimeName.empty(), "Renderer::installGraph requires each node to have a non-empty runtime name.");
            auto [_, inserted] = knownNames.insert(runtimeName);
            nrAssert(inserted, "Renderer::installGraph found duplicate node names in RendererGraphSpec.");

            createInfo.runtime->initialize(initContext);

            installed.push_back(InstalledNode{
                .runtime = createInfo.runtime,
                .description = std::move(description),
                .config = createInfo.config,
                .runtimeName = std::move(runtimeName),
            });
        });

        auto nodeIndexByName = std::map<std::string, std::size_t>{};
        auto nodeOrdinals = std::views::iota(std::size_t{0}, installed.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            nodeIndexByName.emplace(installed[nodeIndex].runtimeName, nodeIndex);
        });

        auto connectionsByTarget = std::map<std::string, std::string>{};
        std::ranges::for_each(spec.connections, [&](const NodeConnection& connection) {
            auto fromNodeIt = nodeIndexByName.find(connection.from.nodeName);
            nrAssert(fromNodeIt != nodeIndexByName.end(), "Renderer::installGraph connection references unknown source node.");

            auto toNodeIt = nodeIndexByName.find(connection.to.nodeName);
            nrAssert(toNodeIt != nodeIndexByName.end(), "Renderer::installGraph connection references unknown target node.");

            nrAssert(
                fromNodeIt->second < toNodeIt->second,
                "Renderer::installGraph currently requires source node order before target node order.");

            auto targetKey = makePortKey(connection.to.nodeName, connection.to.portName);
            auto sourceKey = makePortKey(connection.from.nodeName, connection.from.portName);

            auto [_, inserted] = connectionsByTarget.emplace(std::move(targetKey), std::move(sourceKey));
            nrAssert(inserted, "Renderer::installGraph found multiple sources bound to the same target input port.");
        });

        auto submitNodesByAfterIndex = std::multimap<std::size_t, SubmitNodeSpec>{};
        std::ranges::for_each(spec.submitNodes, [&](const SubmitNodeSpec& submitSpec) {
            nrAssert(
                submitSpec.afterNodeIndex < installed.size(),
                "Renderer::installGraph submit node index is out of range for installed nodes.");
            submitNodesByAfterIndex.emplace(submitSpec.afterNodeIndex, submitSpec);
        });

        installedNodes_ = std::move(installed);
        nodeIndexByName_ = std::move(nodeIndexByName);
        connectionsByTargetPort_ = std::move(connectionsByTarget);
        submitNodesByAfterIndex_ = std::move(submitNodesByAfterIndex);
        graphInstalled_ = true;
    }

void Renderer::shutdown()
{
        if (!device_)
        {
            return;
        }

        device_->waitIdle();
        // Release frame-local graph callbacks and retained command buffers while Device is still valid.
        builder_.clear();
        executor_.clearRetainedState();
        teardownInstalledGraph();
        submissionTimeline_ = RendererSubmissionTimeline{};
        frameUniformArena_ = FrameUniformArena{};
        activeScene_.reset();
        sceneExtractProfile_.reset();
        cpuTimingAccumulator_ = {};
        cpuStatistics_ = {};
        gpuPassTimingAccumulator_.clear();
        gpuPassStatistics_ = {};
        device_.reset();
    }

[[nodiscard]] bool Renderer::initialized() const noexcept
{
        return static_cast<bool>(device_);
    }

[[nodiscard]] bool Renderer::graphInstalled() const noexcept
{
        return graphInstalled_;
    }

void Renderer::resize()
{
        if (!device_)
        {
            return;
        }
        device_->presentationContext.recreate(device_->physicalDevice, device_->device, device_->queueManager);
    }

[[nodiscard]] RendererFrameResult Renderer::renderFrame(const RendererFrameInput& input)
{
        if (!device_ || !graphInstalled_)
        {
            return RendererFrameResult{};
        }

        auto const totalStart = std::chrono::steady_clock::now();
        auto const beginFrameStart = std::chrono::steady_clock::now();
        auto begin = device_->beginFrame(input.acquireTimeout);
        frameUniformArena_.beginFrame(begin.frameIndex);
        auto cpuTimings = RendererCpuFrameTimings{
            .cpuWaitGpuMilliseconds = begin.cpuWaitGpuMilliseconds,
            .frameSetupMilliseconds = std::max(
                0.0,
                elapsedMilliseconds(beginFrameStart, std::chrono::steady_clock::now()) -
                    begin.cpuWaitGpuMilliseconds),
        };

        auto scenePackets = std::optional<nr::scene::ScenePacketSet>{};
        auto primaryCamera = std::optional<nr::scene::SceneResolvedCamera>{};
        auto sceneBridgeFrame = std::optional<nr::scene::SceneBridgeFrame>{};
        auto sceneExtractProfileCreated = false;
        auto sceneCameraOverride = input.cameraOverride;

        auto const sceneStart = std::chrono::steady_clock::now();
        if (input.scene.has_value())
        {
            auto& scene = input.scene->get();
            scene.beginFrame(begin.frameIndex);
            scene.uploadPending();

            auto [profile, created] = ensureSceneExtractProfile(scene);
            sceneExtractProfileCreated = created;

            auto extractInput = input.sceneExtractInput.value_or(nr::scene::SceneExtractInput{});
            if (!extractInput.viewportExtent.has_value())
            {
                auto extent = device_->presentationContext.swapchainExtent();
                extractInput.viewportExtent = glm::uvec2{extent.width, extent.height};
            }

            if (sceneCameraOverride.has_value())
            {
                extractInput.visibility = nr::scene::SceneVisibilityMode::customFrustum;
                extractInput.customFrustum = sceneCameraOverride->frustum;
            }

            scenePackets = scene.extractPackets(profile, extractInput);
            if (!sceneCameraOverride.has_value())
            {
                primaryCamera = scene.tryGetPrimaryCamera(extractInput.viewportExtent);
            }

            auto bridgeBuildInput = nr::scene::SceneRenderBridgeBuildInput{
                .packetSet = std::cref(*scenePackets),
            };

            if (sceneCameraOverride.has_value())
            {
                bridgeBuildInput.frameConstantsOverride = sceneCameraOverride->frameConstants;
            }

            bridgeBuildInput.resolveRasterDrawGeometry =
                [&](nr::resource::MeshHandle meshHandle, std::uint32_t submeshIndex)
                -> std::optional<nr::scene::SceneBridgeDrawGeometry> {
                auto meshRecordRef = scene.tryGetMeshAsset(meshHandle);
                if (!meshRecordRef.has_value())
                {
                    return std::nullopt;
                }

                auto const &meshRecord = meshRecordRef->get();
                if (!meshRecord.cpuReady || !meshRecord.gpu.has_value())
                {
                    return std::nullopt;
                }

                if (!meshRecord.gpu->vertexBuffer.valid())
                {
                    return std::nullopt;
                }

                if (submeshIndex >= meshRecord.cpu.submeshes.size())
                {
                    return std::nullopt;
                }

                auto const &submesh = meshRecord.cpu.submeshes[submeshIndex];
                auto geometry = nr::scene::SceneBridgeDrawGeometry{};
                geometry.vertexBuffer = nr::scene::SceneBridgeBufferBinding{
                    .buffer = std::cref(meshRecord.gpu->vertexBuffer),
                };
                geometry.frontFace = meshRecord.cpu.clockwiseFrontFace
                                         ? vk::FrontFace::eClockwise
                                         : vk::FrontFace::eCounterClockwise;

                auto const indexedGeometry = meshRecord.gpu->indexBuffer.valid() && !meshRecord.cpu.indices.empty();
                if (indexedGeometry)
                {
                    geometry.indexBuffer = nr::scene::SceneBridgeBufferBinding{
                        .buffer = std::cref(meshRecord.gpu->indexBuffer),
                    };
                    geometry.firstIndex = submesh.firstIndex;
                    geometry.indexCount = submesh.indexCount > 0
                                              ? submesh.indexCount
                                              : meshRecord.gpu->indexCount;
                    geometry.vertexOffset = submesh.vertexOffset <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                                                ? static_cast<std::int32_t>(submesh.vertexOffset)
                                                : std::numeric_limits<std::int32_t>::max();
                    return geometry;
                }

                geometry.firstVertex = submesh.vertexOffset;
                geometry.vertexCount = submesh.indexCount > 0
                                           ? submesh.indexCount
                                           : meshRecord.gpu->vertexCount;
                return geometry;
            };

            if (primaryCamera.has_value())
            {
                bridgeBuildInput.primaryCamera = std::cref(*primaryCamera);
            }

            sceneBridgeFrame = nr::scene::SceneRenderBridge::buildFrame(bridgeBuildInput);
        }
        cpuTimings.sceneMilliseconds = elapsedMilliseconds(
            sceneStart,
            std::chrono::steady_clock::now());

        auto frameParameters = NodeFrameParameters{};
        frameParameters.frameIndex = begin.frameIndex;
        frameParameters.swapchainImageIndex = begin.swapchainImageIndex;
        frameParameters.swapchainExtent = device_->presentationContext.swapchainExtent();
        frameParameters.swapchainFormat = device_->presentationContext.swapchainFormat();
        if (input.frameServices.has_value())
        {
            frameParameters.frameServices = input.frameServices;
        }

        if (scenePackets.has_value())
        {
            frameParameters.scenePackets = std::cref(*scenePackets);
        }

        if (primaryCamera.has_value())
        {
            frameParameters.primaryCamera = std::cref(*primaryCamera);
        }

        auto sceneBridgeFrameRef = std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>>{};
        if (sceneBridgeFrame.has_value())
        {
            sceneBridgeFrameRef = std::cref(*sceneBridgeFrame);
        }

        auto globalFrameConstants = nr::scene::SceneBridgeFrameConstants{};
        if (sceneBridgeFrame.has_value())
        {
            globalFrameConstants = sceneBridgeFrame->frameConstants;
        }
        else if (sceneCameraOverride.has_value())
        {
            globalFrameConstants = sceneCameraOverride->frameConstants;
        }

        auto const buildStart = std::chrono::steady_clock::now();
        buildInstalledGraph(frameParameters, globalFrameConstants, sceneBridgeFrameRef);
        cpuTimings.buildMilliseconds = elapsedMilliseconds(
            buildStart,
            std::chrono::steady_clock::now());

        auto const compileStart = std::chrono::steady_clock::now();
        auto compiled = compiler_.compileConsuming(builder_.mutableFrame());
        cpuTimings.compileMilliseconds = elapsedMilliseconds(
            compileStart,
            std::chrono::steady_clock::now());

        auto executeContext = RenderGraphExecutor::ExecuteContext{
            .device = *device_,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .submissionTimeline = submissionTimeline_.valid()
                                    ? std::optional<std::reference_wrapper<RendererSubmissionTimeline>>(std::ref(submissionTimeline_))
                                    : std::nullopt,
        };

        auto const prepareStart = std::chrono::steady_clock::now();
        auto prepared = executor_.prepareFrame(std::move(compiled), executeContext);
        cpuTimings.prepareMilliseconds = elapsedMilliseconds(
            prepareStart,
            std::chrono::steady_clock::now());

        auto const executeStart = std::chrono::steady_clock::now();
        auto executeReport = executor_.executePrepared(prepared, executeContext);
        cpuTimings.executeMilliseconds = elapsedMilliseconds(
            executeStart,
            std::chrono::steady_clock::now());
        if (executeReport.completedGpuPassTimingFrame.has_value())
        {
            recordGpuPassTimingSample(*executeReport.completedGpuPassTimingFrame);
        }

        auto const presentStart = std::chrono::steady_clock::now();
        auto present = device_->presentFrame();
        cpuTimings.presentMilliseconds = elapsedMilliseconds(
            presentStart,
            std::chrono::steady_clock::now());
        cpuTimings.totalMilliseconds = elapsedMilliseconds(
            totalStart,
            std::chrono::steady_clock::now());
        recordCpuTimingSample(cpuTimings);

        return RendererFrameResult{
            .rendered = true,
            .frameIndex = begin.frameIndex,
            .swapchainImageIndex = begin.swapchainImageIndex,
            .presentResult = present.result,
            .compiledSubmitBatchCount = prepared.compiled.submitBatches.size(),
            .submittedBatchCount = executeReport.submittedBatchCount,
            .invokedPassPrepareCount = executeReport.invokedPassPrepareCount,
            .invokedPassRecordCount = executeReport.invokedPassRecordCount,
            .appliedInPassBarrierCount = executeReport.appliedInPassBarrierCount,
            .appliedAcquireBarrierCount = executeReport.appliedAcquireBarrierCount,
            .appliedReleaseBarrierCount = executeReport.appliedReleaseBarrierCount,
            .syntheticPresentBatchUsed = executeReport.plan.requiresSyntheticPresentBatch,
            .usedScenePath = input.scene.has_value(),
            .usedCameraOverride = sceneCameraOverride.has_value(),
            .sceneExtractProfileCreated = sceneExtractProfileCreated,
            .sceneBridgeDrawCount = sceneBridgeFrame.has_value() ? sceneBridgeFrame->rasterDraws.size() : 0,
            .sceneRasterPacketCount = scenePackets.has_value() ? scenePackets->rasterDraws.size() : 0,
            .sceneRtPacketCount = scenePackets.has_value() ? scenePackets->rtInstances.size() : 0,
            .sceneTlasPacketCount = scenePackets.has_value() ? scenePackets->tlasBuildInputs.size() : 0,
            .cpuStatistics = cpuStatistics_,
            .gpuPassStatistics = gpuPassStatistics_,
        };
    }

[[nodiscard]] nr::rhi::Device& Renderer::device()
{
        return *device_;
    }

[[nodiscard]] const nr::rhi::Device& Renderer::device() const
{
        return *device_;
    }

[[nodiscard]] RenderGraphExecutor& Renderer::graphExecutor() noexcept
{
        return executor_;
    }

[[nodiscard]] const RenderGraphExecutor& Renderer::graphExecutor() const noexcept
{
        return executor_;
    }

[[nodiscard]] const RendererCpuStatistics& Renderer::cpuStatistics() const noexcept
{
        return cpuStatistics_;
    }

[[nodiscard]] const RendererGpuPassStatistics& Renderer::gpuPassStatistics() const noexcept
{
        return gpuPassStatistics_;
    }

[[nodiscard]] std::string Renderer::makePortKey(std::string_view nodeName, std::string_view portName)
{
        return std::format("{}::{}", nodeName, portName);
    }

void Renderer::buildInstalledGraph(
        const NodeFrameParameters& frameParameters,
        const nr::scene::SceneBridgeFrameConstants& frameConstants,
        std::optional<std::reference_wrapper<const nr::scene::SceneBridgeFrame>> sceneBridgeFrame)
{
        nrAssert(graphInstalled_, "Renderer::buildInstalledGraph requires installGraph() before rendering.");

        builder_.clear();
        auto const globalFrameUniforms = makeGlobalFrameUniforms(frameConstants);
        auto const globalResources = FrameGlobalResources{
            .frameUniform = frameUniformArena_.upload(builder_, "Renderer.GlobalFrameUniforms", globalFrameUniforms),
        };

        auto nodeFrameParameters = frameParameters;
        if (sceneBridgeFrame.has_value())
        {
            nodeFrameParameters.sceneBridgeFrameHandle =
                builder_.addFrameData("SceneBridgeFrame", sceneBridgeFrame->get());
        }

        auto publishedOutputs = std::map<std::string, GraphResourceHandle>{};

        auto nodeOrdinals = std::views::iota(std::size_t{0}, installedNodes_.size());
        std::ranges::for_each(nodeOrdinals, [&](std::size_t nodeIndex) {
            auto& installedNode = installedNodes_[nodeIndex];

            auto nodeHandle = builder_.addNode(
                installedNode.runtimeName,
                installedNode.config.queue);

            auto resolveInputPort = [&](std::string_view inputPortName) -> GraphResourceHandle {
                auto targetKey = makePortKey(installedNode.runtimeName, inputPortName);
                auto connectionIt = connectionsByTargetPort_.find(targetKey);
                if (connectionIt == connectionsByTargetPort_.end())
                {
                    return {};
                }

                auto sourceIt = publishedOutputs.find(connectionIt->second);
                if (sourceIt == publishedOutputs.end())
                {
                    return {};
                }

                return sourceIt->second;
            };

            auto publishOutputPort = [&](std::string_view outputPortName, GraphResourceHandle resource) {
                nrAssert(resource.valid(), "Renderer::buildInstalledGraph output port publish requires a valid resource handle.");
                auto outputKey = makePortKey(installedNode.runtimeName, outputPortName);
                publishedOutputs.insert_or_assign(outputKey, resource);
            };

            auto buildContext = NodeBuildContext{
                .graphBuilder = std::ref(builder_),
                .nodeHandle = nodeHandle,
                .frameIndex = frameParameters.frameIndex,
                .globalResources = std::cref(globalResources),
                .resolveInputPort = resolveInputPort,
                .publishOutputPort = publishOutputPort,
            };

            installedNode.runtime->build(buildContext, nodeFrameParameters);

            auto boundaries = submitNodesByAfterIndex_.equal_range(nodeIndex);
            std::ranges::for_each(std::ranges::subrange(boundaries.first, boundaries.second), [&](const auto& entry) {
                auto debugName = entry.second.debugName.empty()
                                     ? std::format("Submit.After.{}", installedNode.runtimeName)
                                     : entry.second.debugName;
                auto submitHandle = builder_.addSubmitNode(debugName);
                nrAssert(submitHandle.valid(), "Renderer::buildInstalledGraph failed to add a valid submit node.");
            });
        });

    }

void Renderer::teardownInstalledGraph()
{
        if (device_)
        {
            auto shutdownContext = NodeShutdownContext{
                .device = std::ref(*device_),
            };
            std::ranges::for_each(installedNodes_, [&](InstalledNode& installedNode) {
                if (installedNode.runtime)
                {
                    installedNode.runtime->shutdown(shutdownContext);
                }
            });
        }

        installedNodes_.clear();
        nodeIndexByName_.clear();
        connectionsByTargetPort_.clear();
        submitNodesByAfterIndex_.clear();
        graphInstalled_ = false;
    }

[[nodiscard]] std::pair<nr::scene::SceneExtractProfileHandle, bool> Renderer::ensureSceneExtractProfile(nr::scene::Scene& scene)
{
        auto sameScene = activeScene_.has_value() && std::addressof(activeScene_->get()) == std::addressof(scene);
        auto needsCreate = !sameScene || !sceneExtractProfile_.has_value() || !sceneExtractProfile_->valid();

        if (needsCreate)
        {
            activeScene_ = std::ref(scene);
            sceneExtractProfile_ = scene.registerExtractProfile(nr::scene::SceneExtractProfileCreateInfo{
                .debugName = "Renderer.DefaultRasterExtract",
            });
            return {*sceneExtractProfile_, true};
        }

        return {*sceneExtractProfile_, false};
    }

void Renderer::recordCpuTimingSample(const RendererCpuFrameTimings& timings) noexcept
{
        accumulateCpuTimings(cpuTimingAccumulator_, timings);
        ++cpuStatistics_.pendingSampleFrameCount;

        if (cpuStatistics_.pendingSampleFrameCount < nr::statisticsSampleFrameCount)
        {
            return;
        }

        cpuStatistics_.average = averageCpuTimings(
            cpuTimingAccumulator_,
            cpuStatistics_.pendingSampleFrameCount);
        cpuStatistics_.averagedFrameCount = cpuStatistics_.pendingSampleFrameCount;
        cpuStatistics_.pendingSampleFrameCount = 0u;
        cpuStatistics_.valid = true;
        cpuTimingAccumulator_ = {};
    }

void Renderer::recordGpuPassTimingSample(const GpuPassTimingFrame& timings)
{
        std::ranges::for_each(timings.passes, [&](const GpuPassTimingSample& sample) {
            auto key = std::pair{sample.pass.value, sample.debugName};
            auto [entryIt, inserted] = gpuPassTimingAccumulator_.try_emplace(key);
            if (inserted)
            {
                entryIt->second.pass = sample.pass;
                entryIt->second.debugName = sample.debugName;
                entryIt->second.queue = sample.queue;
                entryIt->second.isCopyPass = sample.isCopyPass;
            }

            entryIt->second.milliseconds += sample.milliseconds;
            ++entryIt->second.sampleCount;
        });

        ++gpuPassStatistics_.pendingSampleFrameCount;
        if (gpuPassStatistics_.pendingSampleFrameCount < nr::statisticsSampleFrameCount)
        {
            return;
        }

        gpuPassStatistics_.averages = averageGpuPassTimings(gpuPassTimingAccumulator_);
        gpuPassStatistics_.averagedFrameCount = gpuPassStatistics_.pendingSampleFrameCount;
        gpuPassStatistics_.pendingSampleFrameCount = 0u;
        gpuPassStatistics_.valid = true;
        gpuPassTimingAccumulator_.clear();
    }
} // namespace nr::renderer
