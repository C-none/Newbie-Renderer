module;
export module nr.renderPasses:normalView;

import dependency;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct NormalViewRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::GraphicsPipeline> pipeline{};
};

struct NormalViewPushConstants
{
    glm::mat4 viewProjection{1.0f};
    glm::vec4 cameraWorldAndDrawCount{0.0f};
    glm::uvec4 drawMeta{0u};
    glm::vec4 worldTranslationAndPadding{0.0f};
};

[[nodiscard]] std::shared_ptr<NormalViewRuntimeCache> ensureNormalViewRuntime(
    nr::rhi::Device& device,
    vk::Format colorFormat,
    vk::Format depthFormat)
{
    auto& shaderService = nr::rhi::ShaderService::instance();

    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/normalView"),
    });
    nr::nrAssert(program.valid(), "NormalView pass failed to compile shader module renderer/normalView.");

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.entryPointNames = {"vertexMain", "fragmentMain"};
    pipelineDesc.colorAttachmentFormats = {colorFormat};
    pipelineDesc.depthAttachmentFormat = depthFormat;
    pipelineDesc.depthTestEnable = true;
    pipelineDesc.depthWriteEnable = true;
    pipelineDesc.mode = nr::rhi::GraphicsPipelineMode::StandardGraphics;

    auto runtime = std::make_shared<NormalViewRuntimeCache>();
    runtime->pipeline = device.pipeline().createGraphicsPipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "NormalView pass failed to create graphics pipeline.");
    return runtime;
}
} // namespace

export namespace nr::renderPasses
{
struct NormalViewNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format colorFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format depthFormat = vk::Format::eD32Sfloat;
};

struct NormalViewNodeOutput
{
    nr::renderer::GraphResourceHandle normalColor{};
    nr::renderer::GraphResourceHandle depth{};
    std::size_t plannedDrawCount = 0;
};

class NormalViewNode final : public Node
{
  public:
    NormalViewNodeInput input{};
    NormalViewNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "NormalView",
            .inputPorts = {},
            .outputPorts = {
                NodePort{.name = "color"},
                NodePort{.name = "depth"},
            },
        };
    }

    void initialize(NodeInitContext& context) override
    {
        runtime_ = ensureNormalViewRuntime(
            context.device.get(),
            input.colorFormat,
            input.depthFormat);
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        auto viewportExtent = input.viewportExtent;
        if (viewportExtent.width == 1 && viewportExtent.height == 1)
        {
            viewportExtent = frameParameters.swapchainExtent;
        }

        auto colorImage = nr::renderer::GraphTransientImageDesc{
            .debugName = "NormalView.Color",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = input.colorFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::ColorAttachment,
                nr::renderer::ImageUsageIntent::TransferSrc,
                nr::renderer::ImageUsageIntent::Sampled,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
            .aspect = nr::renderer::ImageAspectIntent::Color,
        };

        auto depthImage = nr::renderer::GraphTransientImageDesc{
            .debugName = "NormalView.Depth",
            .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
            .extent = vk::Extent3D{viewportExtent.width, viewportExtent.height, 1},
            .format = input.depthFormat,
            .usageIntents = {
                nr::renderer::ImageUsageIntent::DepthStencilAttachment,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
            .initialLayout = nr::renderer::ImageLayoutIntent::DepthStencilAttachment,
            .aspect = nr::renderer::ImageAspectIntent::Depth,
        };

        output.normalColor = context.addResource(colorImage);
        output.depth = context.addResource(depthImage);

        auto colorHandle = output.normalColor;
        auto depthHandle = output.depth;
        auto runtime = runtime_;

        auto sceneDraws = std::vector<nr::scene::SceneBridgeDrawPacket>{};
        auto sceneFrameConstants = nr::scene::SceneBridgeFrameConstants{};
        if (frameParameters.sceneBridgeFrame.has_value())
        {
            auto const &bridgeFrame = frameParameters.sceneBridgeFrame->get();
            sceneDraws = bridgeFrame.rasterDraws;
            sceneFrameConstants = bridgeFrame.frameConstants;
        }

        output.plannedDrawCount = sceneDraws.size();

        auto passIntents = std::array{
            nr::renderer::PassResourceUseDesc{
                .resource = output.normalColor,
                .bufferUsage = std::nullopt,
                .bufferAccess = std::nullopt,
                .imageUsage = nr::renderer::ImageUsageIntent::ColorAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::ColorAttachmentWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::ColorAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Color,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
            nr::renderer::PassResourceUseDesc{
                .resource = output.depth,
                .bufferUsage = std::nullopt,
                .bufferAccess = std::nullopt,
                .imageUsage = nr::renderer::ImageUsageIntent::DepthStencilAttachment,
                .imageAccess = nr::renderer::ImageAccessIntent::DepthStencilWrite,
                .imageLayout = nr::renderer::ImageLayoutIntent::DepthStencilAttachment,
                .imageAspect = nr::renderer::ImageAspectIntent::Depth,
                .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
                .readOnly = false,
            },
        };

        [[maybe_unused]] auto rasterPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "NormalView.Raster",
            [colorHandle, depthHandle, viewportExtent, runtime, sceneDraws = std::move(sceneDraws), sceneFrameConstants](const nr::renderer::PassRecordContext& recordContext) {
            nr::nrAssert(static_cast<bool>(recordContext.resolveImage), "NormalView pass requires image resolver callback.");

            auto colorImage = recordContext.resolveImage(colorHandle);
            auto depthImage = recordContext.resolveImage(depthHandle);
            nr::nrAssert(colorImage.has_value(), "NormalView pass failed to resolve color image resource.");
            nr::nrAssert(depthImage.has_value(), "NormalView pass failed to resolve depth image resource.");
            nr::nrAssert(colorImage->view != vk::ImageView{}, "NormalView pass requires a valid color image view.");
            nr::nrAssert(depthImage->view != vk::ImageView{}, "NormalView pass requires a valid depth image view.");
            nr::nrAssert(static_cast<bool>(runtime), "NormalView pass record stage requires initialized runtime state.");

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

            nr::nrAssert(recordContext.commandBuffer.has_value(), "NormalView pass requires RAII command buffer access.");
            auto& commandBuffer = recordContext.commandBuffer->get();
            {
                auto scopedRendering = nr::rhi::ops::ScopedRendering(*commandBuffer, renderingScope);
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, runtime->pipeline.pipeline.raw());

                auto root = runtime->pipeline.descriptorLayout.rootCursor();
                nr::nrAssert(root.valid(), "NormalView pass record stage requires a valid root shader cursor.");

                auto pushCursor = root["gFrame"];
                nr::nrAssert(pushCursor.valid(), "NormalView pass record stage requires gFrame push-constant cursor.");

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
                    auto worldTranslation = glm::vec3{drawPacket.world[3]};

                    auto pushConstants = NormalViewPushConstants{
                        .viewProjection = sceneFrameConstants.viewProjection,
                        .cameraWorldAndDrawCount = glm::vec4{
                            sceneFrameConstants.cameraWorld,
                            static_cast<float>(sceneDraws.size()),
                        },
                        .drawMeta = glm::uvec4{
                            static_cast<std::uint32_t>(drawIndex),
                            drawPacket.meshBindless,
                            drawPacket.materialBindless,
                            drawPacket.submeshIndex,
                        },
                        .worldTranslationAndPadding = glm::vec4{worldTranslation, 1.0f},
                    };

                    auto pushConstantsOk = pushCursor.setData(pushConstants);
                    nr::nrAssert(pushConstantsOk, "NormalView pass record stage failed to set gFrame push constants.");

                    auto drawSnapshot = root.snapshot();
                    nr::rhi::pushConstantsToCommandBuffer(
                        *commandBuffer,
                        runtime->pipeline.layout,
                        drawSnapshot);
                    root.clearSnapshot();
                    commandBuffer.draw(3, 1, 0, 0);
                });
            }
        });

        context.publishOutput("color", output.normalColor);
        context.publishOutput("depth", output.depth);
    }

    void shutdown(NodeShutdownContext&) override
    {
        runtime_.reset();
    }

  private:
        std::shared_ptr<NormalViewRuntimeCache> runtime_{};
};
} // namespace nr::renderPasses