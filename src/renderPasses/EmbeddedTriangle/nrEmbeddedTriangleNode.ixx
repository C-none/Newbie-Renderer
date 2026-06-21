export module nr.renderPasses:embeddedTriangle;
import dependency;

import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct EmbeddedTriangleRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::GraphicsPipeline> pipeline{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, nr::maxFrameInFlight> passBindingSetsByFrame{};
    // Pre-allocated per-frame-slot uniform buffers; avoids vmaCreateBuffer/vmaDestroyBuffer every frame.
    std::array<nr::rhi::Buffer, nr::maxFrameInFlight> frameUniformBuffers{};
};

struct EmbeddedTriangleFrameUniforms
{
    glm::mat4 viewProjection{1.0f};
};

[[nodiscard]] std::shared_ptr<EmbeddedTriangleRuntimeCache> ensureEmbeddedTriangleRuntime(
    nr::rhi::Device& device,
    vk::Format colorFormat)
{
    auto& shaderService = nr::rhi::ShaderService::instance();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/embeddedTriangle"),
    });
    nr::nrAssert(program.valid(), "EmbeddedTriangle pass failed to compile shader module renderer/embeddedTriangle.");

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.entryPointNames = {"vertexMain", "fragmentMain"};
    pipelineDesc.colorAttachmentFormats = {colorFormat};
    pipelineDesc.depthTestEnable = false;
    pipelineDesc.depthWriteEnable = false;
    pipelineDesc.mode = nr::rhi::GraphicsPipelineMode::StandardGraphics;

    auto runtime = std::make_shared<EmbeddedTriangleRuntimeCache>();
    runtime->pipeline = device.pipeline().createGraphicsPipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "EmbeddedTriangle pass failed to create graphics pipeline.");

    auto frameSlots = std::views::iota(std::size_t{0}, runtime->passBindingSetsByFrame.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime->passBindingSetsByFrame[frameSlot] =
            nr::rhi::allocateBindingSetsForLayout(runtime->pipeline.layout, runtime->pipeline.bindingPool);

        auto bufferInfo = vk::BufferCreateInfo{};
        bufferInfo.size = sizeof(EmbeddedTriangleFrameUniforms);
        bufferInfo.usage = vk::BufferUsageFlagBits::eUniformBuffer;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;
        runtime->frameUniformBuffers[frameSlot] = device.resourceFactory.createBuffer(
            bufferInfo,
            nr::rhi::MemoryUsage::CpuToGpu,
            std::format("EmbeddedTriangle.FrameUniforms[{}]", frameSlot));
        nr::nrAssert(runtime->frameUniformBuffers[frameSlot].valid(),
            std::format("EmbeddedTriangle failed to create frame uniform buffer for frame slot {}.", frameSlot));
    });

    return runtime;
}
} // namespace

export namespace nr::renderPasses
{
struct EmbeddedTriangleNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;
    glm::mat4 viewProjection{1.0f};
};

struct EmbeddedTriangleNodeOutput
{
    nr::renderer::GraphResourceHandle color{};
};

class EmbeddedTriangleNode final : public Node
{
  public:
    EmbeddedTriangleNodeInput input{};
    EmbeddedTriangleNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "EmbeddedTriangle",
            .inputPorts = {},
            .outputPorts = {
                NodePort{.name = "color"},
            },
        };
    }

    void initialize(NodeInitContext& context) override
    {
        auto colorFormat = input.colorFormat;
        if (colorFormat == vk::Format::eUndefined)
        {
            colorFormat = context.device.get().presentationContext.swapchainFormat();
        }
        runtime_ = ensureEmbeddedTriangleRuntime(context.device.get(), colorFormat);
        nr::rhi::setPipelineDebugName(
            context.device.get().device,
            runtime_->pipeline.pipeline.raw(),
            describe().name + ".Pipeline");
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        nr::nrAssert(static_cast<bool>(runtime_), "EmbeddedTriangle build stage requires initialized runtime state.");

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

        output.color = context.transientColor("EmbeddedTriangle.Color", viewportExtent, colorFormat);

        auto frameUniforms = EmbeddedTriangleFrameUniforms{
            .viewProjection = input.viewProjection,
        };

        auto frameUniformBuffer = context.importFrameUniform(
            runtime_->frameUniformBuffers,
            "EmbeddedTriangle.FrameUniforms",
            frameUniforms);

        auto root = runtime_->pipeline.descriptorLayout.rootCursor();
        auto frameCursor = root["gFrame"];
        static_cast<void>(frameCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = frameUniformBuffer.value,
            .debugName = "EmbeddedTriangle.FrameUniforms",
        }));

        auto passBindingSnapshot = root.snapshot();
        root.clearSnapshot();

        auto colorHandle = output.color;
        auto runtime = runtime_;

        auto passIntents = std::array{
            nr::renderer::use::colorWrite(output.color),
            nr::renderer::use::uniformRead(frameUniformBuffer),
        };

        [[maybe_unused]] auto rasterPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "EmbeddedTriangle.Raster",
            [colorHandle, viewportExtent, runtime](
                const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "EmbeddedTriangle pass requires image resolver callback.");
                nr::nrAssert(recordContext.commandBuffer.has_value(), "EmbeddedTriangle pass requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(runtime), "EmbeddedTriangle pass record stage requires initialized runtime state.");

                auto colorImage = recordContext.resolveImage(colorHandle);
                nr::nrAssert(colorImage.has_value(), "EmbeddedTriangle pass failed to resolve color image resource.");
                nr::nrAssert(colorImage->view != vk::ImageView{}, "EmbeddedTriangle pass requires a valid color image view.");

                auto targetExtent = vk::Extent2D{
                    std::max(1u, std::min(viewportExtent.width, colorImage->extent.width)),
                    std::max(1u, std::min(viewportExtent.height, colorImage->extent.height)),
                };

                auto colorAttachment = nr::rhi::ops::RenderingAttachmentDesc{
                    .imageView = colorImage->view,
                    .loadOp = vk::AttachmentLoadOp::eClear,
                    .clearValue = vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}},
                };

                auto colorAttachments = std::array{colorAttachment};
                auto renderingScope = nr::rhi::ops::RenderingScopeDesc{
                    .renderArea = vk::Rect2D{vk::Offset2D{0, 0}, targetExtent},
                    .colorAttachments = colorAttachments,
                };

                auto& commandBuffer = recordContext.commandBuffer->get();
                {
                    auto scopedRendering = nr::rhi::ops::ScopedRendering(commandBuffer, renderingScope);
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, runtime->pipeline.pipeline.raw());

                    auto const frameSlot = static_cast<std::size_t>(recordContext.frameIndex % runtime->passBindingSetsByFrame.size());
                    auto const& passBindingSets = runtime->passBindingSetsByFrame[frameSlot];
                    nr::nrAssert(!passBindingSets.empty(), "EmbeddedTriangle pass requires preallocated descriptor sets for the active frame slot.");

                    nr::rhi::bindPreparedResourcesToCommandBuffer(
                        commandBuffer,
                        vk::PipelineBindPoint::eGraphics,
                        runtime->pipeline.layout,
                        std::span<const nr::rhi::ShaderBindingSet>{passBindingSets.data(), passBindingSets.size()});

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
                        .cullMode = vk::CullModeFlagBits::eNone,
                        .frontFace = vk::FrontFace::eCounterClockwise,
                        .depthTestEnable = vk::False,
                        .depthWriteEnable = vk::False,
                        .depthCompareOp = vk::CompareOp::eLessOrEqual,
                        .polygonMode = vk::PolygonMode::eFill,
                        .rasterizationSamples = vk::SampleCountFlagBits::e1,
                    };
                    nr::rhi::mesh::applyRasterState(commandBuffer, rasterState);
                    commandBuffer.draw(3, 1, 0, 0);
                }
            },
            [runtime, passBindingSnapshot](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(static_cast<bool>(runtime), "EmbeddedTriangle pass prepare stage requires initialized runtime state.");

                auto const frameSlot = static_cast<std::size_t>(prepareContext.frameIndex % runtime->passBindingSetsByFrame.size());
                auto const& passBindingSets = runtime->passBindingSetsByFrame[frameSlot];
                nr::nrAssert(!passBindingSets.empty(), "EmbeddedTriangle pass prepare requires preallocated descriptor sets for the active frame slot.");

                nr::rhi::updateResourcesForBindingSnapshot(
                    runtime->pipeline.bindingPool,
                    std::span<const nr::rhi::ShaderBindingSet>{passBindingSets.data(), passBindingSets.size()},
                    passBindingSnapshot,
                    nr::renderer::makeDefaultLogicalDescriptorResolver(prepareContext));
            });

        context.publishOutput("color", output.color);
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
    std::shared_ptr<EmbeddedTriangleRuntimeCache> runtime_{};
};
} // namespace nr::renderPasses
