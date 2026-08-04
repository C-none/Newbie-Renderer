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
    nr::rhi::Device &device, std::span<const nr::rhi::SlangProgram> programs, vk::Format colorFormat,
    std::string debugName)
{
    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.colorAttachmentFormats = {colorFormat};

    auto runtime = std::make_shared<EmbeddedTriangleRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    runtime->pipeline->initialize(
        device.pipeline().createGraphicsPipeline(programs, pipelineDesc, 64u, {}, std::move(debugName)));

    return runtime;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
EmbeddedTriangleNode::~EmbeddedTriangleNode() = default;

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> EmbeddedTriangleNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path("renderer/embeddedTriangle/vertex"),
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path("renderer/embeddedTriangle/fragment"),
        },
    };
}

void EmbeddedTriangleNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 2u && context.shaderPrograms[0].entryPoint() != nullptr &&
                     context.shaderPrograms[0].entryPoint()->stage == SLANG_STAGE_VERTEX &&
                     context.shaderPrograms[1].entryPoint() != nullptr &&
                     context.shaderPrograms[1].entryPoint()->stage == SLANG_STAGE_FRAGMENT,
                 "EmbeddedTriangle initialization requires ordered vertex and fragment shaders.");
    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = context.device.get().presentationContext.swapchainFormat();
    }
    runtime_ = detail::ensureEmbeddedTriangleRuntime(context.device.get(), context.shaderPrograms, colorFormat,
                                                     context.runtimeName + ".Pipeline");
}

void EmbeddedTriangleNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->pipeline && runtime_->pipeline->valid(),
                 "EmbeddedTriangle async graphics PSO construction failed.");
}

void EmbeddedTriangleNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "EmbeddedTriangle build stage requires initialized runtime state.");

    auto viewportExtent = frameParameters.swapchainExtent;

    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = frameParameters.swapchainFormat;
    }

    auto color = context.transientColor("EmbeddedTriangle.Color", viewportExtent, colorFormat);

    auto rasterPass = nr::renderer::RasterPassBuilder{context, "EmbeddedTriangle.Raster", runtime_->pipeline};
    rasterPass.viewport(viewportExtent)
        .viewportYMode(nr::renderer::RasterViewportYMode::ClipSpaceYUp)
        .colorAttachment(color, vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}}})
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms",
                 nr::renderer::ShaderStageIntent::Vertex)
        .rasterState(nr::rhi::MeshRasterState{
            .cullMode = vk::CullModeFlagBits::eNone,
        })
        .record([](const nr::renderer::RasterPassRecordContext &rasterContext) {
            rasterContext.commandBuffer.draw(3, 1, 0, 0);
        });

    [[maybe_unused]] auto rasterPassHandle = rasterPass.build();

    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, color);
}

void EmbeddedTriangleNode::shutdown(NodeShutdownContext &)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
}
} // namespace nr::renderPasses
