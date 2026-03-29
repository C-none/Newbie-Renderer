module;
export module nr.renderPasses:normalBuffer;

import dependency;
import nr.renderer;
import nr.rhi;
import nr.resource;
import nr.scene;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct NormalBufferRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::GraphicsPipeline> pipeline{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, nr::maxFrameInFlight> passBindingSetsByFrame{};
};

struct NormalBufferFrameUniforms
{
    glm::mat4 viewProjection{1.0f};
};

struct NormalBufferDrawPushConstants
{
    glm::mat4 model{1.0f};
    std::uint32_t objectId = 0u;
    glm::uvec3 padding{0u};
};

static_assert(sizeof(NormalBufferDrawPushConstants) <= 128u);

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
    pipelineDesc.mode = nr::rhi::GraphicsPipelineMode::StandardGraphics;

    auto const vertexProbe = nr::resource::Vertex{};
    auto const vertexBase = reinterpret_cast<std::uintptr_t>(std::addressof(vertexProbe));
    auto const positionOffset = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(std::addressof(vertexProbe.position)) - vertexBase);
    auto const normalOffset = static_cast<std::uint32_t>(
        reinterpret_cast<std::uintptr_t>(std::addressof(vertexProbe.normal)) - vertexBase);

    pipelineDesc.vertexBindings = {
        vk::VertexInputBindingDescription{0u, static_cast<std::uint32_t>(sizeof(nr::resource::Vertex)), vk::VertexInputRate::eVertex},
    };
    pipelineDesc.vertexAttributes = {
        vk::VertexInputAttributeDescription{0u, 0u, vk::Format::eR32G32B32Sfloat, positionOffset},
        vk::VertexInputAttributeDescription{1u, 0u, vk::Format::eR32G32B32Sfloat, normalOffset},
    };

    auto runtime = std::make_shared<NormalBufferRuntimeCache>();
    runtime->pipeline = device.pipeline().createGraphicsPipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "NormalBuffer pass failed to create graphics pipeline.");

    auto frameSlots = std::views::iota(std::size_t{0}, runtime->passBindingSetsByFrame.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime->passBindingSetsByFrame[frameSlot] =
            nr::rhi::allocateBindingSetsForLayout(runtime->pipeline.layout, runtime->pipeline.bindingPool);
    });

    return runtime;
}
} // namespace

export namespace nr::renderPasses
{
struct NormalBufferNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format depthFormat = vk::Format::eD32Sfloat;
};

struct NormalBufferNodeOutput
{
    nr::renderer::GraphResourceHandle normalBuffer{};
    nr::renderer::GraphResourceHandle depth{};
    std::size_t plannedDrawCount = 0;
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
                NodePort{.name = "normalBuffer"},
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

        runtime_ = ensureNormalBufferRuntime(
            context.device.get(),
            colorFormat,
            input.depthFormat);
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        nr::nrAssert(static_cast<bool>(runtime_), "NormalBuffer build stage requires initialized runtime state.");
        nr::nrAssert(device_.has_value(), "NormalBuffer build stage requires device reference from initialize stage.");

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

        output.normalBuffer = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "NormalBuffer.Color",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = colorFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::ColorAttachment,
                nr::renderer::ImageUsageIntent::TransferSrc,
                nr::renderer::ImageUsageIntent::Sampled,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
            .aspect = nr::renderer::ImageAspectIntent::Color,
        });

        output.depth = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "NormalBuffer.Depth",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = input.depthFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::DepthStencilAttachment,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::DepthStencilAttachment,
            .aspect = nr::renderer::ImageAspectIntent::Depth,
        });

        auto sceneDraws = std::vector<nr::scene::SceneBridgeDrawPacket>{};
        auto sceneFrameConstants = nr::scene::SceneBridgeFrameConstants{};
        if (frameParameters.sceneBridgeFrame.has_value())
        {
            auto const& bridgeFrame = frameParameters.sceneBridgeFrame->get();
            sceneDraws = bridgeFrame.rasterDraws;
            sceneFrameConstants = bridgeFrame.frameConstants;
        }
        output.plannedDrawCount = sceneDraws.size();

        auto frameUniforms = NormalBufferFrameUniforms{
            .viewProjection = sceneFrameConstants.viewProjection,
        };

        auto frameUniformBuffer = context.addResource(nr::renderer::GraphTransientBufferDesc{
            .debugName = "NormalBuffer.FrameUniforms",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .size = static_cast<vk::DeviceSize>(sizeof(NormalBufferFrameUniforms)),
            .usageIntents = {
                nr::renderer::BufferUsageIntent::Uniform,
            },
            .memoryUsage = nr::rhi::MemoryUsage::CpuToGpu,
        });

        auto passRoot = runtime_->pipeline.descriptorLayout.rootCursor();
        nr::nrAssert(passRoot.valid(), "NormalBuffer build stage requires a valid root shader cursor.");

        auto frameCursor = passRoot["gFrame"];
        nr::nrAssert(frameCursor.valid(), "NormalBuffer build stage requires gFrame uniform cursor.");

        auto frameBindOk = frameCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = frameUniformBuffer.value,
            .debugName = "NormalBuffer.FrameUniforms",
        });
        nr::nrAssert(frameBindOk, "NormalBuffer build stage failed to bind logical gFrame uniform resource.");

        auto passBindingSnapshot = passRoot.snapshot();
        passRoot.clearSnapshot();

        auto colorHandle = output.normalBuffer;
        auto depthHandle = output.depth;
        auto runtime = runtime_;

        auto passIntents = std::array{
            nr::renderer::PassResourceUseDesc{
                .resource = colorHandle,
                .imageUsage = nr::renderer::ImageUsageIntent::ColorAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::ColorAttachmentWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = depthHandle,
                .imageUsage = nr::renderer::ImageUsageIntent::DepthStencilAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::DepthStencilWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::DepthStencilAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Depth,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = frameUniformBuffer,
                .bufferUsage = nr::renderer::BufferUsageIntent::Uniform,
                .bufferAccess = nr::renderer::BufferAccessIntent::UniformRead,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = true,
            },
        };

        [[maybe_unused]] auto rasterPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "NormalBuffer.Raster",
            [colorHandle,
             depthHandle,
             viewportExtent,
             runtime,
             sceneDraws = std::move(sceneDraws),
             passBindingSnapshot = std::move(passBindingSnapshot)](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "NormalBuffer pass requires image resolver callback.");
                nr::nrAssert(recordContext.commandBuffer.has_value(), "NormalBuffer pass requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(runtime), "NormalBuffer pass record stage requires initialized runtime state.");

                auto colorImage = recordContext.resolveImage(colorHandle);
                auto depthImage = recordContext.resolveImage(depthHandle);
                nr::nrAssert(colorImage.has_value(), "NormalBuffer pass failed to resolve color image resource.");
                nr::nrAssert(depthImage.has_value(), "NormalBuffer pass failed to resolve depth image resource.");
                nr::nrAssert(colorImage->view != vk::ImageView{}, "NormalBuffer pass requires a valid color image view.");
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

                    auto root = runtime->pipeline.descriptorLayout.rootCursor();
                    nr::nrAssert(root.valid(), "NormalBuffer pass record stage requires a valid root shader cursor.");

                    auto drawCursor = root["gDraw"];
                    nr::nrAssert(drawCursor.valid(), "NormalBuffer pass record stage requires gDraw push-constant cursor.");

                    auto viewport = vk::Viewport{
                        0.0f,
                        0.0f,
                        static_cast<float>(targetExtent.width),
                        static_cast<float>(targetExtent.height),
                        0.0f,
                        1.0f,
                    };
                    commandBuffer.setViewport(0, {viewport});

                    auto scissor = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent};
                    commandBuffer.setScissor(0, {scissor});
                    commandBuffer.setPrimitiveTopology(vk::PrimitiveTopology::eTriangleList);

                    auto rasterState = nr::rhi::MeshRasterState{
                        .cullMode = vk::CullModeFlagBits::eBack,
                        .frontFace = vk::FrontFace::eCounterClockwise,
                        .depthTestEnable = vk::True,
                        .depthWriteEnable = vk::True,
                        .depthCompareOp = vk::CompareOp::eLessOrEqual,
                        .polygonMode = vk::PolygonMode::eFill,
                        .rasterizationSamples = vk::SampleCountFlagBits::e1,
                    };
                    nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);

                    auto drawIndices = std::views::iota(std::size_t{0}, sceneDraws.size());
                    std::ranges::for_each(drawIndices, [&](std::size_t drawIndex) {
                        auto const& drawPacket = sceneDraws[drawIndex];
                        auto const& geometry = drawPacket.geometry;
                        if (!geometry.hasVertexBuffer())
                        {
                            return;
                        }

                        rasterState.frontFace = geometry.frontFace;
                        nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);

                        auto vertexBufferHandle = geometry.vertexBuffer.buffer->get().handle();
                        auto vertexBuffers = std::array<vk::Buffer, 1>{vertexBufferHandle};
                        auto vertexOffsets = std::array<vk::DeviceSize, 1>{geometry.vertexBuffer.offset};
                        commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);

                        auto drawPushConstants = NormalBufferDrawPushConstants{
                            .model = drawPacket.world,
                            .objectId = static_cast<std::uint32_t>(drawPacket.renderable.id()),
                            .padding = glm::uvec3{0u},
                        };

                        auto pushConstantsOk = drawCursor.setData(drawPushConstants);
                        nr::nrAssert(pushConstantsOk, "NormalBuffer pass record stage failed to set gDraw push constants.");

                        auto drawSnapshot = root.snapshot();
                        nr::rhi::pushConstantsToCommandBuffer(commandBuffer, runtime->pipeline.layout, drawSnapshot);
                        root.clearSnapshot();

                        if (geometry.hasIndexBuffer())
                        {
                            auto indexBufferHandle = geometry.indexBuffer.buffer->get().handle();
                            commandBuffer.bindIndexBuffer(indexBufferHandle, geometry.indexBuffer.offset, geometry.indexType);
                            commandBuffer.drawIndexed(geometry.indexCount, 1, geometry.firstIndex, geometry.vertexOffset, 0);
                            return;
                        }

                        if (geometry.vertexCount > 0)
                        {
                            commandBuffer.draw(geometry.vertexCount, 1, geometry.firstVertex, 0);
                        }
                    });
                }
            },
            [frameUniformBuffer, frameUniforms](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(static_cast<bool>(prepareContext.resolveBuffer), "NormalBuffer pass prepare requires buffer resolver callback.");

                auto resolvedBuffer = prepareContext.resolveBuffer(frameUniformBuffer);
                nr::nrAssert(resolvedBuffer.has_value(), "NormalBuffer pass prepare failed to resolve frame-uniform buffer resource.");
                nr::nrAssert(resolvedBuffer->resource.has_value(), "NormalBuffer pass prepare requires managed frame-uniform buffer resource.");

                auto& frameBuffer = resolvedBuffer->resource->get();
                nr::nrAssert(frameBuffer.mapped() != nullptr, "NormalBuffer pass prepare requires host-visible frame-uniform buffer.");
                nr::nrAssert(frameBuffer.size() >= sizeof(NormalBufferFrameUniforms), "NormalBuffer pass prepare uniform buffer size is smaller than frame uniform payload.");

                frameBuffer.write(frameUniforms);
                frameBuffer.flush(0, static_cast<vk::DeviceSize>(sizeof(NormalBufferFrameUniforms)));
            });

        context.publishOutput("normalBuffer", output.normalBuffer);
        context.publishOutput("depth", output.depth);
    }

    void shutdown(NodeShutdownContext&) override
    {
        device_.reset();
        runtime_.reset();
    }

  private:
    std::shared_ptr<NormalBufferRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
