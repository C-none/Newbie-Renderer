module nr.renderPasses;
import dependency.vulkan;

import :embeddedTriangle;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct EmbeddedTriangleRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>> pipeline{};
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

    auto runtime = std::make_shared<EmbeddedTriangleRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    runtime->pipeline->initialize(device.pipeline().createGraphicsPipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "EmbeddedTriangle pass failed to create graphics pipeline.");

    return runtime;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
EmbeddedTriangleNode::~EmbeddedTriangleNode() = default;

NodeDescription EmbeddedTriangleNode::describe() const
{
    return NodeDescription{
        .name = "EmbeddedTriangle",
    };
}

void EmbeddedTriangleNode::initialize(NodeInitContext& context)
{
    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = context.device.get().presentationContext.swapchainFormat();
    }
    runtime_ = detail::ensureEmbeddedTriangleRuntime(context.device.get(), colorFormat);
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        describe().name + ".Pipeline");
}

void EmbeddedTriangleNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
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

    auto color = context.transientColor("EmbeddedTriangle.Color", viewportExtent, colorFormat);

    auto rasterPass = nr::renderer::RasterPassBuilder{
        context,
        "EmbeddedTriangle.Raster",
        runtime_->pipeline};
    rasterPass
        .viewport(viewportExtent)
        .viewportYMode(nr::renderer::RasterViewportYMode::ClipSpaceYUp)
        .colorAttachment(
            color,
            vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}})
        .uniform(
            "gFrame",
            context.globalResources.get().frameUniform,
            "Renderer.GlobalFrameUniforms",
            nr::renderer::ShaderStageIntent::Vertex)
        .rasterState(nr::rhi::MeshRasterState{
            .cullMode = vk::CullModeFlagBits::eNone,
        })
        .record([](const nr::renderer::RasterPassRecordContext& rasterContext) {
            rasterContext.commandBuffer.draw(3, 1, 0, 0);
        });

    [[maybe_unused]] auto rasterPassHandle = rasterPass.build();

    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, color);
}

void EmbeddedTriangleNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
}
} // namespace nr::renderPasses
