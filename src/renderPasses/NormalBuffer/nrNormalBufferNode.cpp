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

    vk::Format colorFormat = vk::Format::eUndefined;
    vk::Format depthFormat = vk::Format::eUndefined;

    std::array<nr::rhi::Image, nr::maxFrameInFlight> normalBuffers{};
    std::array<nr::rhi::Image, nr::maxFrameInFlight> depthBuffers{};
};

inline constexpr float kModelLinearSingularityTolerance = 32.0f * std::numeric_limits<float>::epsilon();

void validateAffineModelTransform(const DirectX::XMFLOAT4X4 &model) noexcept
{
    nr::nrAssert(std::isfinite(model._11) && std::isfinite(model._12) && std::isfinite(model._13) &&
                     std::isfinite(model._14) && std::isfinite(model._21) && std::isfinite(model._22) &&
                     std::isfinite(model._23) && std::isfinite(model._24) && std::isfinite(model._31) &&
                     std::isfinite(model._32) && std::isfinite(model._33) && std::isfinite(model._34) &&
                     std::isfinite(model._41) && std::isfinite(model._42) && std::isfinite(model._43) &&
                     std::isfinite(model._44) && std::abs(model._14) <= kModelLinearSingularityTolerance &&
                     std::abs(model._24) <= kModelLinearSingularityTolerance &&
                     std::abs(model._34) <= kModelLinearSingularityTolerance &&
                     std::abs(model._44 - 1.0f) <= kModelLinearSingularityTolerance,
                 "NormalBuffer draw requires a finite affine row-vector model transform.");
}

[[nodiscard]] float validatedModelLinearDeterminant(const DirectX::XMFLOAT4X4 &model) noexcept
{
    auto const determinant = model._11 * (model._22 * model._33 - model._23 * model._32) -
                             model._12 * (model._21 * model._33 - model._23 * model._31) +
                             model._13 * (model._21 * model._32 - model._22 * model._31);
    auto const row0Length = std::sqrt(model._11 * model._11 + model._12 * model._12 + model._13 * model._13);
    auto const row1Length = std::sqrt(model._21 * model._21 + model._22 * model._22 + model._23 * model._23);
    auto const row2Length = std::sqrt(model._31 * model._31 + model._32 * model._32 + model._33 * model._33);
    auto const determinantScale = row0Length * row1Length * row2Length;
    nr::nrAssert(std::isfinite(determinant) && std::isfinite(determinantScale) && determinantScale > 0.0f &&
                     std::abs(determinant) > kModelLinearSingularityTolerance * determinantScale,
                 "NormalBuffer draw requires a finite, non-singular model linear transform.");
    return determinant;
}

[[nodiscard]] vk::FrontFace frontFaceForModelParity(vk::FrontFace objectSpaceFrontFace,
                                                     float modelLinearDeterminant) noexcept
{
    if (modelLinearDeterminant >= 0.0f)
    {
        return objectSpaceFrontFace;
    }

    return objectSpaceFrontFace == vk::FrontFace::eClockwise ? vk::FrontFace::eCounterClockwise
                                                              : vk::FrontFace::eClockwise;
}

struct NormalBufferPushConstants
{
    DirectX::XMFLOAT4X3 model{};
    DirectX::XMFLOAT4 normalUvLinear{};
    DirectX::XMFLOAT4 normalUvOffsetScale{};
    DirectX::XMUINT4 normalTextureMeta{};
};

static_assert(std::is_standard_layout_v<NormalBufferPushConstants>);
static_assert(sizeof(NormalBufferPushConstants) == 96u,
              "NormalBuffer push constants must match the reflected shader layout.");
static_assert(sizeof(DirectX::XMFLOAT4X3) == 48u);
static_assert(std::is_standard_layout_v<DirectX::XMFLOAT4X3>);
static_assert(std::is_trivially_copyable_v<DirectX::XMFLOAT4X3>);
static_assert(nr::memberOffset<&NormalBufferPushConstants::model>() == 0u);
static_assert(nr::memberOffset<&NormalBufferPushConstants::normalUvLinear>() == 48u);
static_assert(nr::memberOffset<&NormalBufferPushConstants::normalUvOffsetScale>() == 64u);
static_assert(nr::memberOffset<&NormalBufferPushConstants::normalTextureMeta>() == 80u);
static_assert(sizeof(NormalBufferPushConstants) <= nr::rhi::kMaxPushConstantBytes, "Push constants exceed 128 bytes.");

inline constexpr std::uint32_t kVertexStride = static_cast<std::uint32_t>(sizeof(nr::resource::Vertex));
inline constexpr std::uint32_t kOffsetPosition =
    static_cast<std::uint32_t>(nr::memberOffset<&nr::resource::Vertex::position>());
inline constexpr std::uint32_t kOffsetNormal =
    static_cast<std::uint32_t>(nr::memberOffset<&nr::resource::Vertex::normal>());
inline constexpr std::uint32_t kOffsetTangent =
    static_cast<std::uint32_t>(nr::memberOffset<&nr::resource::Vertex::tangent>());
inline constexpr std::uint32_t kOffsetTexCoord0 =
    static_cast<std::uint32_t>(nr::memberOffset<&nr::resource::Vertex::texCoord0>());
inline constexpr std::uint32_t kOffsetTexCoord1 =
    static_cast<std::uint32_t>(nr::memberOffset<&nr::resource::Vertex::texCoord1>());

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
    const DirectX::XMFLOAT4X4 &model, const nr::scene::SceneMaterialNormalTextureBinding &normalTexture) noexcept
{
    validateAffineModelTransform(model);
    return NormalBufferPushConstants{
        .model =
            DirectX::XMFLOAT4X3{
                model._11,
                model._12,
                model._13,
                model._21,
                model._22,
                model._23,
                model._31,
                model._32,
                model._33,
                model._41,
                model._42,
                model._43,
            },
        .normalUvLinear = normalTexture.uvLinear,
        .normalUvOffsetScale =
            DirectX::XMFLOAT4{
                normalTexture.uvOffset.x,
                normalTexture.uvOffset.y,
                normalTexture.normalScale,
                0.0f,
            },
        .normalTextureMeta =
            DirectX::XMUINT4{
                normalTexture.textureId,
                normalTexture.uvSet,
                0u,
                0u,
            },
    };
}

[[nodiscard]] std::shared_ptr<NormalBufferRuntimeCache> ensureNormalBufferRuntime(
    nr::rhi::Device &device, std::span<const nr::rhi::SlangProgram> programs, vk::Format colorFormat,
    vk::Format depthFormat, std::string debugName)
{
    auto runtime = std::make_shared<NormalBufferRuntimeCache>();
    runtime->colorFormat = colorFormat;
    runtime->depthFormat = depthFormat;

    auto pipelineDesc = nr::rhi::GraphicsPipelineDesc{};
    pipelineDesc.colorAttachmentFormats = {runtime->colorFormat};
    pipelineDesc.depthAttachmentFormat = runtime->depthFormat;
    pipelineDesc.depthTestEnable = true;
    pipelineDesc.depthWriteEnable = true;
    pipelineDesc.vertexBindings = makeVertexBindings();
    pipelineDesc.vertexAttributes = makeVertexAttributes();
    pipelineDesc.descriptorBindingPolicy.defaultRuntimeDescriptorCount = nr::renderer::kSceneTextureDescriptorCapacity;
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>>();
    auto sceneTextureImmutableSamplers = std::array{sceneTextureTableImmutableSamplerBinding()};
    runtime->pipeline->initializeDeferred(device.pipeline().createGraphicsPipeline(
        programs, pipelineDesc, 64u, sceneTextureImmutableSamplers, std::move(debugName)));

    return runtime;
}

void ensureNormalBufferImages(nr::rhi::Device &device, NormalBufferRuntimeCache &runtime, vk::Extent2D extent)
{
    auto imageNeedsRealloc = [](const nr::rhi::Image &image, vk::Extent2D requestedExtent, vk::Format requestedFormat) {
        if (!image.valid())
        {
            return true;
        }

        auto const currentExtent = image.extent();
        return currentExtent.width < requestedExtent.width || currentExtent.height < requestedExtent.height ||
               image.format() != requestedFormat;
    };

    const auto needsRealloc = std::ranges::any_of(runtime.normalBuffers,
                                                  [&](const nr::rhi::Image &image) {
                                                      return imageNeedsRealloc(image, extent, runtime.colorFormat);
                                                  }) ||
                              std::ranges::any_of(runtime.depthBuffers, [&](const nr::rhi::Image &image) {
                                  return imageNeedsRealloc(image, extent, runtime.depthFormat);
                              });

    if (!needsRealloc)
    {
        return;
    }

    auto newExtent = vk::Extent2D{
        std::max(1u, extent.width),
        std::max(1u, extent.height),
    };
    auto growToExistingExtent = [&newExtent](const nr::rhi::Image &image) {
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
            auto imageInfo = nr::rhi::makeImageCreateInfo(runtime.colorFormat, newExtent,
                                                          vk::ImageUsageFlagBits::eColorAttachment |
                                                              vk::ImageUsageFlagBits::eTransferSrc |
                                                              vk::ImageUsageFlagBits::eSampled);

            runtime.normalBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo, nr::rhi::MemoryUsage::GpuOnly, std::format("NormalBuffer.Color{}", frameSlotSuffix));
            nr::nrAssert(runtime.normalBuffers[frameSlot].valid(),
                         "NormalBuffer failed to create normal buffer image.");
        }

        {
            auto imageInfo = nr::rhi::makeImageCreateInfo(runtime.depthFormat, newExtent,
                                                          vk::ImageUsageFlagBits::eDepthStencilAttachment);

            runtime.depthBuffers[frameSlot] = device.resourceFactory.createImage(
                imageInfo, nr::rhi::MemoryUsage::GpuOnly, std::format("NormalBuffer.Depth{}", frameSlotSuffix));
            nr::nrAssert(runtime.depthBuffers[frameSlot].valid(), "NormalBuffer failed to create depth buffer image.");
        }
    });
}

} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
NormalBufferNode::~NormalBufferNode() = default;

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> NormalBufferNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/normalBuffer/vertex"},
        },
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/normalBuffer/fragment"},
        },
    };
}

void NormalBufferNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 2u && context.shaderPrograms[0].entryPoint() != nullptr &&
                     context.shaderPrograms[0].entryPoint()->stage == SLANG_STAGE_VERTEX &&
                     context.shaderPrograms[1].entryPoint() != nullptr &&
                     context.shaderPrograms[1].entryPoint()->stage == SLANG_STAGE_FRAGMENT,
                 "NormalBuffer initialization requires ordered vertex and fragment shaders.");
    device_ = context.device;
    auto colorFormat = input.colorFormat;
    if (colorFormat == vk::Format::eUndefined)
    {
        colorFormat = context.device.get().presentationContext.swapchainFormat();
    }
    nr::nrAssert(colorFormat != vk::Format::eUndefined,
                 "NormalBuffer initialization requires a resolved color attachment format.");
    nr::nrAssert(input.depthFormat != vk::Format::eUndefined,
                 "NormalBuffer initialization requires a resolved depth attachment format.");
    runtime_ = detail::ensureNormalBufferRuntime(context.device.get(), context.shaderPrograms, colorFormat,
                                                 input.depthFormat, context.runtimeName + ".Pipeline");

    auto const initialExtent = context.device.get().presentationContext.swapchainExtent();
    detail::ensureNormalBufferImages(context.device.get(), *runtime_, initialExtent);
}

void NormalBufferNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->pipeline && runtime_->pipeline->valid(),
                 "NormalBuffer async graphics PSO construction failed.");
}

void NormalBufferNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "NormalBuffer build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "NormalBuffer build stage requires initialize() device reference.");

    auto viewportExtent = frameParameters.swapchainExtent;

    auto requestedColorFormat = input.colorFormat;
    if (requestedColorFormat == vk::Format::eUndefined)
    {
        requestedColorFormat = frameParameters.swapchainFormat;
    }
    nr::nrAssert(requestedColorFormat == runtime_->colorFormat, "NormalBuffer color format changed after pipeline initialization: initialized={}, "
                           "requested={}. Reinstall the graph to rebuild the pipeline.",
                           vk::to_string(runtime_->colorFormat), vk::to_string(requestedColorFormat));
    nr::nrAssert(input.depthFormat == runtime_->depthFormat, "NormalBuffer depth format changed after pipeline initialization: initialized={}, "
                           "requested={}. Reinstall the graph to rebuild the pipeline.",
                           vk::to_string(runtime_->depthFormat), vk::to_string(input.depthFormat));

    detail::ensureNormalBufferImages(device_->get(), *runtime_, viewportExtent);

    auto const frameSlot = static_cast<std::size_t>(frameParameters.frameIndex % nr::maxFrameInFlight);
    auto normalBuffer =
        context.importColor(runtime_->normalBuffers[frameSlot], std::format("NormalBuffer.Color[{}]", frameSlot),
                            viewportExtent, runtime_->colorFormat, nr::renderer::ResourceLifetime::FrameLocal);
    auto depthBuffer =
        context.importDepth(runtime_->depthBuffers[frameSlot], std::format("NormalBuffer.Depth[{}]", frameSlot),
                            viewportExtent, runtime_->depthFormat, nr::renderer::ResourceLifetime::FrameLocal);

    auto sceneBridgeFrameHandle = frameParameters.sceneBridgeFrameHandle;
    auto sceneTextureTableBinding = detail::makeSceneTextureTableBindingInput(context.globalResources.get());
    auto &bindlessImageTableCache = context.globalResources.get().bindlessImageTableCache.get();

    auto const normalBufferRasterState = nr::rhi::MeshRasterState{
        .depthTestEnable = vk::True,
        .depthWriteEnable = vk::True,
    };
    auto const modelPushConstants =
        nr::renderer::resolvePushConstantLocation(runtime_->pipeline->state().descriptorLayout, "gPushConstants");

    auto rasterPass = nr::renderer::RasterPassBuilder{context, "NormalBuffer.Raster", runtime_->pipeline};
    if (sceneBridgeFrameHandle.has_value())
    {
        rasterPass.frameData(*sceneBridgeFrameHandle);
    }
    rasterPass.viewport(viewportExtent)
        .viewportYMode(nr::renderer::RasterViewportYMode::ClipSpaceYUp)
        .colorAttachment(normalBuffer,
                         vk::ClearValue{vk::ClearColorValue{std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}}})
        .depthAttachment(depthBuffer)
        .uniform("gFrame", context.globalResources.get().frameUniform, "Renderer.GlobalFrameUniforms",
                 nr::renderer::ShaderStageIntent::Vertex)
        .rasterState(normalBufferRasterState)
        .prepare([runtime = runtime_, sceneTextureTableBinding,
                  cache = std::ref(bindlessImageTableCache)](
                     const nr::renderer::PassPrepareContext &prepareContext,
                     nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding) {
            detail::prepareSceneTextureTableBindingForFrame(*runtime->pipeline, passBinding, cache.get(),
                                                            prepareContext.frameIndex, sceneTextureTableBinding);
        })
        .dynamicBindingSnapshot(
            [runtime = runtime_, sceneTextureTableBinding,
             cache = std::ref(bindlessImageTableCache)](
                const nr::renderer::PassPrepareContext &prepareContext,
                nr::renderer::PipelineRuntime<nr::rhi::GraphicsPipeline>::PassBindingHandle passBinding) {
                return detail::makeSceneTextureTableBindingSnapshot(
                    *runtime->pipeline, passBinding, cache.get(), prepareContext.frameIndex, sceneTextureTableBinding);
            })
        .recordParallel(
            [sceneBridgeFrameHandle](const nr::renderer::PassRecordContext &recordContext) -> std::size_t {
                if (!sceneBridgeFrameHandle.has_value())
                {
                    return 0;
                }

                auto const &sceneBridgeFrame =
                    recordContext.frameData<nr::scene::SceneBridgeFrame>(*sceneBridgeFrameHandle);
                return sceneBridgeFrame.rasterDraws.size();
            },
            [sceneBridgeFrameHandle, normalBufferRasterState,
             modelPushConstants](const nr::renderer::RasterPassRangeRecordContext &rasterContext) {
                if (!sceneBridgeFrameHandle.has_value())
                {
                    return;
                }

                auto const &sceneBridgeFrame =
                    rasterContext.pass.frameData<nr::scene::SceneBridgeFrame>(*sceneBridgeFrameHandle);

                auto &commandBuffer = rasterContext.commandBuffer;

                auto currentCullMode = normalBufferRasterState.cullMode;
                auto currentFrontFace = normalBufferRasterState.frontFace;
                auto drawIndices = std::views::iota(rasterContext.range.begin, rasterContext.range.end);
                auto const &geometryBuffers = sceneBridgeFrame.geometryBuffers;
                nr::nrAssert(geometryBuffers.valid(),
                             "NormalBuffer raster draws require a resolved frame vertex atlas binding.");

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
                    auto const &draw = sceneBridgeFrame.rasterDraws[drawIndex];
                    nr::nrAssert(draw.geometry.valid(),
                                 "NormalBuffer received an unresolved scene raster draw geometry range.");

                    auto const drawCullMode = draw.materialRaster.cullMode;
                    if (currentCullMode != drawCullMode)
                    {
                        commandBuffer.setCullMode(drawCullMode);
                        currentCullMode = drawCullMode;
                    }

                    auto const modelLinearDeterminant = detail::validatedModelLinearDeterminant(draw.world);
                    auto const drawFrontFace =
                        detail::frontFaceForModelParity(draw.geometry.frontFace, modelLinearDeterminant);

                    rasterContext.pushConstants(
                        modelPushConstants, detail::packDrawPushConstants(draw.world, draw.materialTextures.normal));

                    if (currentFrontFace != drawFrontFace)
                    {
                        commandBuffer.setFrontFace(drawFrontFace);
                        currentFrontFace = drawFrontFace;
                    }

                    if (draw.geometry.indexed())
                    {
                        nr::nrAssert(geometryBuffers.hasIndexBuffer(),
                                     "NormalBuffer indexed draw requires a frame index atlas binding.");

                        commandBuffer.drawIndexed(draw.geometry.indexCount, 1, draw.geometry.firstIndex,
                                                  draw.geometry.vertexOffset, 0);
                        return;
                    }

                    commandBuffer.draw(draw.geometry.vertexCount, 1, draw.geometry.firstVertex, 0);
                });
            });

    [[maybe_unused]] auto rasterPassHandle = rasterPass.build();

    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, normalBuffer);
    context.publishFrameResource(nr::renderer::frameResource::normalDepth, depthBuffer);
}

void NormalBufferNode::shutdown(NodeShutdownContext &)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
