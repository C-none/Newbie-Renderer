export module nr.renderPasses:normalBuffer;
import dependency;

import nr.app;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct NormalBufferRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::GraphicsPipeline> pipeline{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, nr::maxFrameInFlight> passBindingSetsByFrame{};

    // Pre-allocated per-frame-slot images to avoid runtime memory allocation.
    // These are imported into the render graph at build time.
    std::array<nr::rhi::Image, nr::maxFrameInFlight> normalBuffers{};
    std::array<nr::rhi::Image, nr::maxFrameInFlight> depthBuffers{};

    // Pre-allocated per-frame-slot uniform buffers for frame data.
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> frameUniformBuffers{};

    // Track allocated dimensions for resize detection
    vk::Extent2D allocatedExtent{0, 0};
    vk::Format allocatedColorFormat = vk::Format::eUndefined;
    vk::Format allocatedDepthFormat = vk::Format::eUndefined;
};

struct NormalBufferFrameUniforms
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
};

struct NormalBufferPushConstants
{
    glm::mat4 model{1.0f};
};

static_assert(sizeof(NormalBufferPushConstants) <= 128u, "Push constants exceed 128 bytes.");

// Compute vertex layout offsets at compile time
// nr::resource::Vertex layout: position(vec3), normal(vec3), tangent(vec4), texCoord0(vec2), texCoord1(vec2), color0(vec4), skin{joints(uvec4), weights(vec4)}
// For NormalBuffer, we only need position and normal - but we still use the full stride to match the buffer layout
inline constexpr std::uint32_t kVertexStride = 104; // sizeof(Vertex)
inline constexpr std::uint32_t kOffsetPosition = 0;
inline constexpr std::uint32_t kOffsetNormal = 12;

[[nodiscard]] std::vector<vk::VertexInputBindingDescription> makeVertexBindings()
{
    return {
        vk::VertexInputBindingDescription{
            0, // binding
            kVertexStride, // stride
            vk::VertexInputRate::eVertex,
        },
    };
}

[[nodiscard]] std::vector<vk::VertexInputAttributeDescription> makeVertexAttributes()
{
    // Only declare position and normal - we don't need other attributes for normal visualization
    return {
        // position: vec3 at offset 0
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, kOffsetPosition},
        // normal: vec3 at offset 12
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, kOffsetNormal},
    };
}

[[nodiscard]] std::shared_ptr<NormalBufferRuntimeCache> ensureNormalBufferRuntime(
    nr::rhi::Device& device,
    vk::Format colorFormat,
    vk::Format depthFormat)
{
    auto& shaderService = nr::rhi::ShaderService::instance();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/normalBuffer"),
    });
    nr::nrAssert(program.valid(), "NormalBuffer pass failed to compile shader module renderer/normalBuffer.");

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.entryPointNames = {"vertexMain", "fragmentMain"};
    pipelineDesc.colorAttachmentFormats = {colorFormat};
    pipelineDesc.depthAttachmentFormat = depthFormat;
    pipelineDesc.depthTestEnable = true;
    pipelineDesc.depthWriteEnable = true;
    pipelineDesc.depthCompareOp = vk::CompareOp::eLessOrEqual;
    pipelineDesc.mode = nr::rhi::GraphicsPipelineMode::StandardGraphics;
    pipelineDesc.cullMode = vk::CullModeFlagBits::eBack;
    pipelineDesc.frontFace = vk::FrontFace::eCounterClockwise;
    pipelineDesc.vertexBindings = makeVertexBindings();
    pipelineDesc.vertexAttributes = makeVertexAttributes();
    
    // Add dynamic states for depth and cull to enable runtime adjustments
    pipelineDesc.dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
        vk::DynamicState::eCullMode,
        vk::DynamicState::eFrontFace,
        vk::DynamicState::eDepthTestEnable,
        vk::DynamicState::eDepthWriteEnable,
        vk::DynamicState::eDepthCompareOp,
        vk::DynamicState::ePolygonModeEXT,
        vk::DynamicState::eRasterizationSamplesEXT,
        vk::DynamicState::ePrimitiveTopology,
    };

    auto runtime = std::make_shared<NormalBufferRuntimeCache>();
    runtime->pipeline = device.pipeline().createGraphicsPipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "NormalBuffer pass failed to create graphics pipeline.");

    auto frameSlots = std::views::iota(std::size_t{0}, runtime->passBindingSetsByFrame.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime->passBindingSetsByFrame[frameSlot] =
            nr::rhi::allocateBindingSetsForLayout(runtime->pipeline.layout, runtime->pipeline.bindingPool);
    });

    // Pre-allocate uniform buffers for each frame slot
    for (std::size_t frameSlot = 0; frameSlot < nr::maxFrameInFlight; ++frameSlot)
    {
        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = sizeof(NormalBufferFrameUniforms);
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        runtime->frameUniformBuffers[frameSlot] = device.resourceFactory.createBuffer(
            bufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            std::format("NormalBuffer.FrameUniforms[{}]", frameSlot));
        nr::nrAssert(runtime->frameUniformBuffers[frameSlot].valid(),
            std::format("NormalBuffer failed to create frame uniform buffer for frame slot {}.", frameSlot));
    }

    // Store formats for later resize detection
    runtime->allocatedColorFormat = colorFormat;
    runtime->allocatedDepthFormat = depthFormat;

    return runtime;
}

/// Ensure pre-allocated images have sufficient capacity for the given extent.
/// Reallocates images if the extent or format changes.
void ensureNormalBufferImages(
    nr::rhi::Device& device,
    NormalBufferRuntimeCache& runtime,
    vk::Extent2D extent,
    vk::Format colorFormat,
    vk::Format depthFormat)
{
    // Check if we need to reallocate (extent or format changed)
    bool needsRealloc = runtime.allocatedExtent.width < extent.width ||
                        runtime.allocatedExtent.height < extent.height ||
                        runtime.allocatedColorFormat != colorFormat ||
                        runtime.allocatedDepthFormat != depthFormat;

    if (!needsRealloc && runtime.normalBuffers[0].valid())
    {
        return;
    }

    // Grow to power of 2 or at least the requested size to minimize reallocations
    auto newExtent = vk::Extent2D{
        std::max(runtime.allocatedExtent.width, extent.width),
        std::max(runtime.allocatedExtent.height, extent.height),
    };

    // Allocate images for each frame slot
    for (std::size_t frameSlot = 0; frameSlot < nr::maxFrameInFlight; ++frameSlot)
    {
        // Normal buffer (color attachment)
        {
            auto imageInfo = vk::ImageCreateInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.format = colorFormat;
            imageInfo.extent = vk::Extent3D{newExtent.width, newExtent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eColorAttachment |
                              vk::ImageUsageFlagBits::eTransferSrc |
                              vk::ImageUsageFlagBits::eSampled;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;

            runtime.normalBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo,
                nr::rhi::MemoryUsage::GpuOnly,
                std::format("NormalBuffer.Color[{}]", frameSlot));
            nr::nrAssert(runtime.normalBuffers[frameSlot].valid(),
                std::format("NormalBuffer failed to create normal buffer image for frame slot {}.", frameSlot));
        }

        // Depth buffer
        {
            auto imageInfo = vk::ImageCreateInfo{};
            imageInfo.imageType = vk::ImageType::e2D;
            imageInfo.format = depthFormat;
            imageInfo.extent = vk::Extent3D{newExtent.width, newExtent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = vk::SampleCountFlagBits::e1;
            imageInfo.tiling = vk::ImageTiling::eOptimal;
            imageInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
            imageInfo.sharingMode = vk::SharingMode::eExclusive;
            imageInfo.initialLayout = vk::ImageLayout::eUndefined;

            runtime.depthBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo,
                nr::rhi::MemoryUsage::GpuOnly,
                std::format("NormalBuffer.Depth[{}]", frameSlot));
            nr::nrAssert(runtime.depthBuffers[frameSlot].valid(),
                std::format("NormalBuffer failed to create depth buffer image for frame slot {}.", frameSlot));
        }
    }

    runtime.allocatedExtent = newExtent;
    runtime.allocatedColorFormat = colorFormat;
    runtime.allocatedDepthFormat = depthFormat;
}

[[nodiscard]] std::optional<std::reference_wrapper<nr::app::UiSystem>> tryGetUiSystem(
    const nr::renderer::NodeFrameParameters& frameParameters)
{
    if (!frameParameters.frameServices.has_value())
    {
        return std::nullopt;
    }

    return frameParameters.frameServices->get().tryGet<nr::app::UiSystem>();
}
} // namespace

export namespace nr::renderPasses
{
struct NormalBufferNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = vk::Format::eD32Sfloat;
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    bool displayBackFaces = false;
};

struct NormalBufferNodeOutput
{
    nr::renderer::GraphResourceHandle normalBuffer{};
    nr::renderer::GraphResourceHandle depthBuffer{};
};

class NormalBufferNode final : public Node
{
  public:
    NormalBufferNodeInput input{};
    NormalBufferNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "NormalBuffer",
            .inputPorts = {},
            .outputPorts = {
                NodePort{.name = "color"},
                NodePort{.name = "depth"},
            },
        };
    }

    void initialize(NodeInitContext& context) override
    {
        device_ = context.device;
        auto colorFormat = input.colorFormat;
        if (colorFormat == vk::Format::eUndefined)
        {
            colorFormat = context.device.get().presentationContext.swapchainFormat();
        }
        runtime_ = ensureNormalBufferRuntime(context.device.get(), colorFormat, input.depthFormat);
        
        // Pre-allocate images with initial viewport extent
        // These will be resized in build() if the viewport changes
        auto initialExtent = input.viewportExtent;
        if (initialExtent.width <= 1 || initialExtent.height <= 1)
        {
            // Use a reasonable default size that will likely be resized in first build()
            initialExtent = vk::Extent2D{1920, 1080};
        }
        ensureNormalBufferImages(context.device.get(), *runtime_, initialExtent, colorFormat, input.depthFormat);
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        nr::nrAssert(static_cast<bool>(runtime_), "NormalBuffer build stage requires initialized runtime state.");
        nr::nrAssert(device_.has_value(), "NormalBuffer build stage requires initialize() device reference.");

        if (auto uiSystem = tryGetUiSystem(frameParameters); uiSystem.has_value())
        {
            auto window = uiSystem->get().window("Normal Buffer");
            if (window)
            {
                [[maybe_unused]] auto checkboxChanged =
                    uiSystem->get().checkbox("Display Back Faces", input.displayBackFaces);
            }
        }

        auto viewportExtent = input.viewportExtent;
        if (viewportExtent.width == 1 && viewportExtent.height == 1)
        {
            viewportExtent = frameParameters.swapchainExtent;
        }

        auto colorFormat = input.colorFormat;
        if (colorFormat == vk::Format::eUndefined)
        {
            colorFormat = frameParameters.swapchainFormat;
        }

        // Ensure pre-allocated images have sufficient capacity (may reallocate on resize)
        ensureNormalBufferImages(device_->get(), *runtime_, viewportExtent, colorFormat, input.depthFormat);

        // Get the current frame slot's pre-allocated images
        auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
        auto const& normalBufferImage = runtime_->normalBuffers[frameSlot];
        auto const& depthBufferImage = runtime_->depthBuffers[frameSlot];

        nr::nrAssert(normalBufferImage.valid(),
            std::format("NormalBuffer build: pre-allocated normal image for frame slot {} is invalid.", frameSlot));
        nr::nrAssert(depthBufferImage.valid(),
            std::format("NormalBuffer build: pre-allocated depth image for frame slot {} is invalid.", frameSlot));

        // Import pre-allocated normal buffer image into the render graph
        output.normalBuffer = context.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("NormalBuffer.Color[{}]", frameSlot),
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .residency = nr::renderer::ResourceResidency::Imported,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Undefined,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = colorFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::ColorAttachment,
                nr::renderer::ImageUsageIntent::TransferSrc,
                nr::renderer::ImageUsageIntent::Sampled,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::Undefined,
            .aspect = nr::renderer::ImageAspectIntent::Color,
            .importedResource = std::cref(normalBufferImage),
        });

        // Import pre-allocated depth buffer image into the render graph
        output.depthBuffer = context.addResource(nr::renderer::GraphImportedImageDesc{
            .debugName = std::format("NormalBuffer.Depth[{}]", frameSlot),
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .residency = nr::renderer::ResourceResidency::Imported,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Undefined,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = input.depthFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::DepthStencilAttachment,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::Undefined,
            .aspect = nr::renderer::ImageAspectIntent::Depth,
            .importedResource = std::cref(depthBufferImage),
        });

        // Get the current frame slot's pre-allocated uniform buffer
        auto& frameUniformBufferRef = runtime_->frameUniformBuffers[frameSlot];
        nr::nrAssert(frameUniformBufferRef.valid(),
            std::format("NormalBuffer build: pre-allocated frame uniform buffer for frame slot {} is invalid.", frameSlot));

        // Update uniform buffer contents
        auto frameUniforms = NormalBufferFrameUniforms{
            .view = input.view,
            .projection = input.projection,
            .viewProjection = input.viewProjection,
        };

        frameUniformBufferRef.writeMappedAndFlush(frameUniforms);

        // Import pre-allocated uniform buffer into the render graph
        auto frameUniformBuffer = context.addResource(nr::renderer::GraphImportedBufferDesc{
            .debugName = std::format("NormalBuffer.FrameUniforms[{}]", frameSlot),
            .lifetime = nr::renderer::ResourceLifetime::FrameLocal,
            .residency = nr::renderer::ResourceResidency::Imported,
            .initialOwnership = nr::renderer::ResourceOwnershipDomain::Undefined,
            .size = static_cast<vk::DeviceSize>(sizeof(NormalBufferFrameUniforms)),
            .usageIntents = {
                nr::renderer::BufferUsageIntent::Uniform,
            },
            .importedResource = std::ref(frameUniformBufferRef),
        });

        auto root = runtime_->pipeline.descriptorLayout.rootCursor();
        nr::nrAssert(root.valid(), "NormalBuffer build stage requires a valid root shader cursor.");

        auto frameCursor = root["gFrame"];
        nr::nrAssert(frameCursor.valid(), "NormalBuffer build stage requires gFrame uniform cursor.");

        auto frameBindOk = frameCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = frameUniformBuffer.value,
            .debugName = "NormalBuffer.FrameUniforms",
        });
        nr::nrAssert(frameBindOk, "NormalBuffer build stage failed to bind logical gFrame uniform resource.");

        auto passBindingSnapshot = root.snapshot();
        root.clearSnapshot();

        auto colorHandle = output.normalBuffer;
        auto depthHandle = output.depthBuffer;
        auto frameUniformHandle = frameUniformBuffer;
        auto runtime = runtime_;
        auto displayBackFaces = input.displayBackFaces;

        // Capture the scene bridge frame if available
        std::optional<nr::scene::SceneBridgeFrame> capturedBridgeFrame{};
        if (frameParameters.sceneBridgeFrame.has_value())
        {
            capturedBridgeFrame = frameParameters.sceneBridgeFrame->get();
        }

        auto passIntents = std::array{
            nr::renderer::PassResourceUseDesc{
                .resource = output.normalBuffer,
                .imageUsage = nr::renderer::ImageUsageIntent::ColorAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::ColorAttachmentWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = output.depthBuffer,
                .imageUsage = nr::renderer::ImageUsageIntent::DepthStencilAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::DepthStencilWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::DepthStencilAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Depth,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = frameUniformHandle,
                .bufferUsage = nr::renderer::BufferUsageIntent::Uniform,
                .bufferAccess = nr::renderer::BufferAccessIntent::UniformRead,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = true,
            },
        };

        [[maybe_unused]] auto rasterPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "NormalBuffer.Raster",
            [colorHandle, depthHandle, viewportExtent, runtime, capturedBridgeFrame, displayBackFaces,
             passBindingSnapshot = std::move(passBindingSnapshot)](
                const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "NormalBuffer pass requires image resolver callback.");
                nr::nrAssert(recordContext.commandBuffer.has_value(), "NormalBuffer pass requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(runtime), "NormalBuffer pass record stage requires initialized runtime state.");

                auto colorImage = recordContext.resolveImage(colorHandle);
                nr::nrAssert(colorImage.has_value(), "NormalBuffer pass failed to resolve color image resource.");
                nr::nrAssert(colorImage->view != vk::ImageView{}, "NormalBuffer pass requires a valid color image view.");

                auto depthImage = recordContext.resolveImage(depthHandle);
                nr::nrAssert(depthImage.has_value(), "NormalBuffer pass failed to resolve depth image resource.");
                nr::nrAssert(depthImage->view != vk::ImageView{}, "NormalBuffer pass requires a valid depth image view.");

                auto targetExtent = vk::Extent2D{
                    std::max(1u, std::min(viewportExtent.width, colorImage->extent.width)),
                    std::max(1u, std::min(viewportExtent.height, colorImage->extent.height)),
                };

                auto colorAttachment = nr::rhi::ops::RenderingAttachmentDesc{
                    .imageView = colorImage->view,
                    .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
                    .resolveMode = vk::ResolveModeFlagBits::eNone,
                    .resolveImageView = {},
                    .resolveImageLayout = vk::ImageLayout::eUndefined,
                    .loadOp = vk::AttachmentLoadOp::eClear,
                    .storeOp = vk::AttachmentStoreOp::eStore,
                    .clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}}},
                };

                auto depthAttachment = nr::rhi::ops::RenderingDepthStencilAttachmentDesc{
                    .imageView = depthImage->view,
                    .imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal,
                    .resolveMode = vk::ResolveModeFlagBits::eNone,
                    .resolveImageView = {},
                    .resolveImageLayout = vk::ImageLayout::eUndefined,
                    .depthLoadOp = vk::AttachmentLoadOp::eClear,
                    .depthStoreOp = vk::AttachmentStoreOp::eStore,
                    .stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
                    .stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
                    .clearValue = vk::ClearDepthStencilValue{1.0f, 0u},
                };

                auto colorAttachments = std::array{colorAttachment};
                auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                    .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent},
                    .layerCount = 1,
                    .viewMask = 0,
                    .flags = {},
                    .colorAttachments = colorAttachments,
                    .depthAttachment = depthAttachment,
                    .stencilAttachment = std::nullopt,
                };

                auto& commandBuffer = recordContext.commandBuffer->get();
                {
                    auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, runtime->pipeline.pipeline.raw());

                    auto const frameSlot = static_cast<std::size_t>(recordContext.frameIndex % runtime->passBindingSetsByFrame.size());
                    auto const& passBindingSets = runtime->passBindingSetsByFrame[frameSlot];
                    nr::nrAssert(!passBindingSets.empty(), "NormalBuffer pass requires preallocated descriptor sets for the active frame slot.");

                    nr::rhi::bindResourcesToCommandBuffer(
                        commandBuffer,
                        vk::PipelineBindPoint::eGraphics,
                        runtime->pipeline.layout,
                        runtime->pipeline.bindingPool,
                        std::span<const nr::rhi::ShaderBindingSet>{passBindingSets.data(), passBindingSets.size()},
                        passBindingSnapshot,
                        nr::renderer::makeDefaultLogicalDescriptorResolver(recordContext));

                    auto viewport = vk::Viewport{
                        0.0f,
                        0.0f,
                        static_cast<float>(targetExtent.width),
                        static_cast<float>(targetExtent.height), // Positive height for Y-flip
                        0.0f,
                        1.0f,
                    };
                    commandBuffer.setViewport(0, {viewport});

                    auto scissor = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent};
                    commandBuffer.setScissor(0, {scissor});
                    commandBuffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);

                    auto rasterState = nr::rhi::MeshRasterState{
                        .cullMode = displayBackFaces ? vk::CullModeFlagBits::eFront : vk::CullModeFlagBits::eBack,
                        .frontFace = vk::FrontFace::eCounterClockwise,
                        .depthTestEnable = vk::True,
                        .depthWriteEnable = vk::True,
                        .depthCompareOp = vk::CompareOp::eLessOrEqual,
                        .polygonMode = vk::PolygonMode::eFill,
                        .rasterizationSamples = vk::SampleCountFlagBits::e1,
                    };
                    nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);

                    // Draw all meshes from scene bridge frame
                    if (capturedBridgeFrame.has_value())
                    {
                        auto drawRoot = runtime->pipeline.descriptorLayout.rootCursor();
                        nr::nrAssert(drawRoot.valid(), "NormalBuffer pass requires a valid root shader cursor for push constants.");

                        auto drawPushCursor = drawRoot["gPushConstants"];
                        nr::nrAssert(drawPushCursor.valid(), "NormalBuffer pass requires gPushConstants cursor.");

                        for (const auto& draw : capturedBridgeFrame->rasterDraws)
                        {
                            if (!draw.geometry.hasVertexBuffer())
                            {
                                continue;
                            }

                            // Set push constants for model matrix using shaderCursor-based binding
                            auto pushConstants = NormalBufferPushConstants{
                                .model = draw.world,
                            };
                            auto pushConstantOk = drawPushCursor.setData(pushConstants);
                            nr::nrAssert(pushConstantOk, "NormalBuffer pass failed to set gPushConstants data.");

                            auto drawBindingSnapshot = drawRoot.snapshot();
                            drawRoot.clearSnapshot();

                            nr::rhi::pushConstantsToCommandBuffer(
                                commandBuffer,
                                runtime->pipeline.layout,
                                drawBindingSnapshot);

                            // Bind vertex buffer
                            auto vertexBuffer = draw.geometry.vertexBuffer.buffer->get().handle();
                            auto vertexOffset = draw.geometry.vertexBuffer.offset;
                            auto vertexBuffers = std::array{vertexBuffer};
                            auto vertexOffsets = std::array{vertexOffset};
                            commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);

                            // Adjust front face based on mesh winding order
                            commandBuffer.setFrontFace(draw.geometry.frontFace);

                            // Draw indexed or non-indexed
                            if (draw.geometry.hasIndexBuffer())
                            {
                                auto indexBuffer = draw.geometry.indexBuffer.buffer->get().handle();
                                auto indexOffset = draw.geometry.indexBuffer.offset;
                                commandBuffer.bindIndexBuffer(indexBuffer, indexOffset, draw.geometry.indexType);
                                commandBuffer.drawIndexed(
                                    draw.geometry.indexCount,
                                    1,
                                    draw.geometry.firstIndex,
                                    draw.geometry.vertexOffset,
                                    0);
                            }
                            else
                            {
                                commandBuffer.draw(
                                    draw.geometry.vertexCount,
                                    1,
                                    draw.geometry.firstVertex,
                                    0);
                            }
                        }
                    }
                }
            },
            [frameUniformHandle, frameUniforms](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(static_cast<bool>(prepareContext.resolveBuffer), "NormalBuffer pass prepare requires buffer resolver callback.");

                auto resolvedBuffer = prepareContext.resolveBuffer(frameUniformHandle);
                nr::nrAssert(resolvedBuffer.has_value(), "NormalBuffer pass prepare failed to resolve frame-uniform buffer resource.");
                nr::nrAssert(resolvedBuffer->resource.has_value(), "NormalBuffer pass prepare requires managed frame-uniform buffer resource.");

                auto& frameBuffer = resolvedBuffer->resource->get();
                nr::nrAssert(frameBuffer.mapped() != nullptr, "NormalBuffer pass prepare requires host-visible frame-uniform buffer.");
                nr::nrAssert(frameBuffer.size() >= sizeof(NormalBufferFrameUniforms), "NormalBuffer pass prepare uniform buffer size is smaller than frame uniform payload.");

                frameBuffer.writeMappedAndFlush(frameUniforms);
            });

        context.publishOutput("color", output.normalBuffer);
        context.publishOutput("depth", output.depthBuffer);
    }

    void shutdown(NodeShutdownContext&) override
    {
        // Explicitly clear binding sets to ensure all descriptor sets are properly
        // released to the binding pool before the pool is destroyed
        if (runtime_)
        {
            for (auto& bindingSets : runtime_->passBindingSetsByFrame)
            {
                bindingSets.clear();
            }
        }
        runtime_.reset();
    }

  private:
    std::shared_ptr<NormalBufferRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
