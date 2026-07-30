module;
#include <cstddef>

module nr.renderPasses;
import dependency.math;
import dependency.vulkan;

import :normalBuffer;
import :sceneTextureTableBinding;
import nr.renderer;
import nr.rhi;
import nr.scene;
import nr.resource;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct NormalBufferRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>> pipeline{};

    std::array<nr::rhi::Image, nr::maxFrameInFlight> normalBuffers{};
    std::array<nr::rhi::Image, nr::maxFrameInFlight> depthBuffers{};
};

struct NormalBufferPushConstants
{
    glm::vec4 modelRow0{};
    glm::vec4 modelRow1{};
    glm::vec4 modelRow2{};
    glm::vec4 normalUvLinear{};
    glm::vec4 normalUvOffsetScale{};
    glm::uvec4 normalTextureMeta{};
};

static_assert(sizeof(NormalBufferPushConstants) == 96u, "NormalBuffer push constants must match the reflected shader layout.");
static_assert(offsetof(NormalBufferPushConstants, modelRow0) == 0u);
static_assert(offsetof(NormalBufferPushConstants, modelRow1) == 16u);
static_assert(offsetof(NormalBufferPushConstants, modelRow2) == 32u);
static_assert(offsetof(NormalBufferPushConstants, normalUvLinear) == 48u);
static_assert(offsetof(NormalBufferPushConstants, normalUvOffsetScale) == 64u);
static_assert(offsetof(NormalBufferPushConstants, normalTextureMeta) == 80u);
static_assert(sizeof(NormalBufferPushConstants) <= nr::rhi::kMaxPushConstantBytes, "Push constants exceed 128 bytes.");

inline constexpr std::uint32_t kVertexStride = static_cast<std::uint32_t>(sizeof(nr::resource::Vertex));
inline constexpr std::uint32_t kOffsetPosition = static_cast<std::uint32_t>(offsetof(nr::resource::Vertex, position));
inline constexpr std::uint32_t kOffsetNormal = static_cast<std::uint32_t>(offsetof(nr::resource::Vertex, normal));
inline constexpr std::uint32_t kOffsetTangent = static_cast<std::uint32_t>(offsetof(nr::resource::Vertex, tangent));
inline constexpr std::uint32_t kOffsetTexCoord0 = static_cast<std::uint32_t>(offsetof(nr::resource::Vertex, texCoord0));
inline constexpr std::uint32_t kOffsetTexCoord1 = static_cast<std::uint32_t>(offsetof(nr::resource::Vertex, texCoord1));

[[nodiscard]] std::vector<vk::VertexInputBindingDescription> makeVertexBindings()
{
    return {
        vk::VertexInputBindingDescription{
            0,
            kVertexStride,
            vk::VertexInputRate::eVertex,
        },
    };
}

[[nodiscard]] std::vector<vk::VertexInputAttributeDescription> makeVertexAttributes()
{
    return {
        vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, kOffsetPosition},
        vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, kOffsetNormal},
        vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32A32Sfloat, kOffsetTangent},
        vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32Sfloat, kOffsetTexCoord0},
        vk::VertexInputAttributeDescription{4, 0, vk::Format::eR32G32Sfloat, kOffsetTexCoord1},
    };
}

[[nodiscard]] NormalBufferPushConstants packDrawPushConstants(
    const glm::mat4& model,
    const nr::scene::SceneMaterialNormalTextureBinding& normalTexture) noexcept
{
    return NormalBufferPushConstants{
        .modelRow0 = glm::vec4{model[0][0], model[1][0], model[2][0], model[3][0]},
        .modelRow1 = glm::vec4{model[0][1], model[1][1], model[2][1], model[3][1]},
        .modelRow2 = glm::vec4{model[0][2], model[1][2], model[2][2], model[3][2]},
        .normalUvLinear = normalTexture.uvLinear,
        .normalUvOffsetScale = glm::vec4{
            normalTexture.uvOffset,
            normalTexture.normalScale,
            0.0f,
        },
        .normalTextureMeta = glm::uvec4{
            normalTexture.textureId,
            normalTexture.uvSet,
            0u,
            0u,
        },
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
    pipelineDesc.vertexBindings = makeVertexBindings();
    pipelineDesc.vertexAttributes = makeVertexAttributes();
    auto const& descriptorCaps = device.descriptorIndexingCapabilities();
    nr::nrAssert(
        descriptorCaps.maxDescriptorSetUpdateAfterBindSampledImages >= nr::renderer::kSceneTextureDescriptorCapacity,
        std::format(
            "NormalBuffer requires at least {} update-after-bind sampled/combined image descriptors per set; device reports {}.",
            nr::renderer::kSceneTextureDescriptorCapacity,
            descriptorCaps.maxDescriptorSetUpdateAfterBindSampledImages));
    nr::nrAssert(
        descriptorCaps.maxPerStageDescriptorUpdateAfterBindSampledImages >= nr::renderer::kSceneTextureDescriptorCapacity,
        std::format(
            "NormalBuffer requires at least {} update-after-bind sampled/combined image descriptors per stage; device reports {}.",
            nr::renderer::kSceneTextureDescriptorCapacity,
            descriptorCaps.maxPerStageDescriptorUpdateAfterBindSampledImages));
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = nr::renderer::kSceneTextureDescriptorCapacity;
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
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    auto sceneTextureImmutableSamplers = std::array{sceneTextureTableImmutableSamplerBinding()};
    runtime->pipeline->initializeDeferred(device.pipeline().createGraphicsPipeline(
        program,
        pipelineDesc,
        64u,
        sceneTextureImmutableSamplers));
    nr::nrAssert(runtime->pipeline->valid(), "NormalBuffer pass failed to create graphics pipeline.");

    return runtime;
}

void ensureNormalBufferImages(
    nr::rhi::Device& device,
    NormalBufferRuntimeCache& runtime,
    vk::Extent2D extent,
    vk::Format colorFormat,
    vk::Format depthFormat)
{
    auto imageNeedsRealloc = [](const nr::rhi::Image& image, vk::Extent2D requestedExtent, vk::Format requestedFormat) {
        if (!image.valid())
        {
            return true;
        }

        auto const currentExtent = image.extent();
        return currentExtent.width < requestedExtent.width ||
               currentExtent.height < requestedExtent.height ||
               image.format() != requestedFormat;
    };

    const auto needsRealloc =
        std::ranges::any_of(runtime.normalBuffers, [&](const nr::rhi::Image& image) {
            return imageNeedsRealloc(image, extent, colorFormat);
        }) ||
        std::ranges::any_of(runtime.depthBuffers, [&](const nr::rhi::Image& image) {
            return imageNeedsRealloc(image, extent, depthFormat);
        });

    if (!needsRealloc)
    {
        return;
    }

    auto newExtent = vk::Extent2D{
        std::max(1u, extent.width),
        std::max(1u, extent.height),
    };
    auto growToExistingExtent = [&newExtent](const nr::rhi::Image& image) {
        if (!image.valid())
        {
            return;
        }

        auto const currentExtent = image.extent();
        newExtent.width = std::max(newExtent.width, currentExtent.width);
        newExtent.height = std::max(newExtent.height, currentExtent.height);
    };

    std::ranges::for_each(runtime.normalBuffers, growToExistingExtent);
    std::ranges::for_each(runtime.depthBuffers, growToExistingExtent);

    auto frameSlots = std::views::iota(std::size_t{0}, runtime.normalBuffers.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        auto const frameSlotSuffix = std::format("[{}]", frameSlot);
        {
            auto imageInfo = nr::rhi::makeImageCreateInfo(
                colorFormat,
                newExtent,
                vk::ImageUsageFlagBits::eColorAttachment |
                    vk::ImageUsageFlagBits::eTransferSrc |
                    vk::ImageUsageFlagBits::eSampled);

            runtime.normalBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo,
                nr::rhi::MemoryUsage::GpuOnly,
                std::format("NormalBuffer.Color{}", frameSlotSuffix));
            nr::nrAssert(runtime.normalBuffers[frameSlot].valid(), "NormalBuffer failed to create normal buffer image.");
        }

        {
            auto imageInfo = nr::rhi::makeImageCreateInfo(
                depthFormat,
                newExtent,
                vk::ImageUsageFlagBits::eDepthStencilAttachment);

            runtime.depthBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo,
                nr::rhi::MemoryUsage::GpuOnly,
                std::format("NormalBuffer.Depth{}", frameSlotSuffix));
            nr::nrAssert(runtime.depthBuffers[frameSlot].valid(), "NormalBuffer failed to create depth buffer image.");
        }
    });
}

} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
NormalBufferNode::~NormalBufferNode() = default;

void NormalBufferNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = context.device.get().presentationContext.swapchainFormat();
    }
    runtime_ = detail::ensureNormalBufferRuntime(context.device.get(), colorFormat, input.depthFormat);
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        context.runtimeName + ".Pipeline");

    auto const initialExtent = context.device.get().presentationContext.swapchainExtent();
    detail::ensureNormalBufferImages(context.device.get(), *runtime_, initialExtent, colorFormat, input.depthFormat);
}

void NormalBufferNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "NormalBuffer build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "NormalBuffer build stage requires initialize() device reference.");

    auto viewportExtent = frameParameters.swapchainExtent;

    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = frameParameters.swapchainFormat;
    }

    detail::ensureNormalBufferImages(device_->get(), *runtime_, viewportExtent, colorFormat, input.depthFormat);

    auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
    auto normalBuffer = context.importColor(
        runtime_->normalBuffers[frameSlot],
        std::format("NormalBuffer.Color[{}]", frameSlot),
        viewportExtent,
        colorFormat,
        nr::renderer::ResourceLifetime::FrameLocal);
    auto depthBuffer = context.importDepth(
        runtime_->depthBuffers[frameSlot],
        std::format("NormalBuffer.Depth[{}]", frameSlot),
        viewportExtent,
        input.depthFormat,
        nr::renderer::ResourceLifetime::FrameLocal);

    auto sceneBridgeFrameHandle = frameParameters.sceneBridgeFrameHandle;
    auto sceneTextureTableBinding = detail::makeSceneTextureTableBindingInput(context.globalResources.get());
    auto& bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();

    auto const normalBufferRasterState = nr::rhi::MeshRasterState{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
    };

    auto rasterPass = nr::renderer::RasterPassBuilder{
        context,
        "NormalBuffer.Raster",
        runtime_->pipeline};
    rasterPass
        .viewport(viewportExtent)
        .viewportYMode(nr::renderer::RasterViewportYMode::ClipSpaceYUp)
        .colorAttachment(
            normalBuffer,
            vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}}})
        .depthAttachment(depthBuffer)
        .uniform(
            "gFrame",
            context.globalResources.get().frameUniform,
            "Renderer.GlobalFrameUniforms",
            nr::renderer::ShaderStageIntent::Vertex)
        .rasterState(normalBufferRasterState)
        .prepare(
            [runtime = runtime_,
             sceneTextureTableBinding,
             cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                detail::prepareSceneTextureTableBindingForFrame(
                    *runtime->pipeline,
                    cache.get(),
                    prepareContext.frameIndex,
                    sceneTextureTableBinding);
            })
        .dynamicBindingSnapshot(
            [runtime = runtime_,
             sceneTextureTableBinding,
             cache = std::ref(bindlessImageTableCache)](const nr::renderer::PassPrepareContext& prepareContext) {
                return detail::makeSceneTextureTableBindingSnapshot(
                    *runtime->pipeline,
                    cache.get(),
                    prepareContext.frameIndex,
                    sceneTextureTableBinding);
            })
        .recordParallel(
        [sceneBridgeFrameHandle](const nr::renderer::PassRecordContext& recordContext) -> std::size_t {
            if (!sceneBridgeFrameHandle.has_value())
            {
                return 0;
            }

            auto const& sceneBridgeFrame =
                recordContext.frameData<nr::scene::SceneBridgeFrame>(*sceneBridgeFrameHandle);
            return sceneBridgeFrame.rasterDraws.size();
        },
        [sceneBridgeFrameHandle, normalBufferRasterState](const nr::renderer::RasterPassRangeRecordContext& rasterContext) {
            if (!sceneBridgeFrameHandle.has_value())
            {
                return;
            }

            auto const& sceneBridgeFrame =
                rasterContext.pass.frameData<nr::scene::SceneBridgeFrame>(*sceneBridgeFrameHandle);

            auto& commandBuffer = rasterContext.commandBuffer;
            auto const modelPushConstants = rasterContext.pushConstantLocation("gPushConstants");

            auto currentCullMode = normalBufferRasterState.cullMode;
            auto currentFrontFace = normalBufferRasterState.frontFace;
            auto drawIndices = std::views::iota(rasterContext.range.begin, rasterContext.range.end);
            auto const& geometryBuffers = sceneBridgeFrame.geometryBuffers;
            if (!geometryBuffers.hasVertexBuffer())
            {
                return;
            }

            auto vertexBuffer = geometryBuffers.vertexBuffer.buffer->get().handle();
            auto vertexOffset = geometryBuffers.vertexBuffer.offset;
            auto vertexBuffers = std::array{vertexBuffer};
            auto vertexOffsets = std::array{vertexOffset};
            commandBuffer.bindVertexBuffers(0, vertexBuffers, vertexOffsets);

            if (geometryBuffers.hasIndexBuffer())
            {
                auto indexBuffer = geometryBuffers.indexBuffer.buffer->get().handle();
                auto indexOffset = geometryBuffers.indexBuffer.offset;
                commandBuffer.bindIndexBuffer(indexBuffer, indexOffset, geometryBuffers.indexType);
            }

            std::ranges::for_each(drawIndices, [&](std::size_t drawIndex) {
                auto const& draw = sceneBridgeFrame.rasterDraws[drawIndex];
                if (!draw.geometry.hasVertexBuffer())
                {
                    return;
                }

                auto const drawCullMode = draw.materialRaster.cullMode;
                if (currentCullMode != drawCullMode)
                {
                    commandBuffer.setCullMode(drawCullMode);
                    currentCullMode = drawCullMode;
                }

                rasterContext.pushConstants(
                    modelPushConstants,
                    detail::packDrawPushConstants(draw.world, draw.materialTextures.normal));

                if (currentFrontFace != draw.geometry.frontFace)
                {
                    commandBuffer.setFrontFace(draw.geometry.frontFace);
                    currentFrontFace = draw.geometry.frontFace;
                }

                if (draw.geometry.hasIndexBuffer())
                {
                    nr::nrAssert(
                        geometryBuffers.hasIndexBuffer(),
                        "NormalBuffer indexed draw requires a frame index atlas binding.");

                    commandBuffer.drawIndexed(
                        draw.geometry.indexCount,
                        1,
                        draw.geometry.firstIndex,
                        draw.geometry.vertexOffset,
                        0);
                    return;
                }

                commandBuffer.draw(
                    draw.geometry.vertexCount,
                    1,
                    draw.geometry.firstVertex,
                    0);
            });
        });

    [[maybe_unused]] auto rasterPassHandle = rasterPass.build();

    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, normalBuffer);
    context.publishFrameResource(nr::renderer::frameResource::normalDepth, depthBuffer);
}

void NormalBufferNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
}
} // namespace nr::renderPasses
