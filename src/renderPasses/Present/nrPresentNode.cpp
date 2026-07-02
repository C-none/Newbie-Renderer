module nr.renderPasses;
import dependency.vulkan;

import :presentNode;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PresentConvertPushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t swizzleBgr = 0u;
    std::uint32_t outputSrgb = 0u;
    std::uint32_t flipY = 0u;
    float uiOpacity = 1.0f;
};

static_assert(sizeof(PresentConvertPushConstants) <= nr::rhi::kMaxPushConstantBytes);

struct PresentFormatConversion
{
    bool swizzleBgr = false;
    bool outputSrgb = false;
};

struct PresentRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};

    nr::rhi::Image convertedColorImage{};
    vk::Extent2D allocatedExtent{0, 0};
};

[[nodiscard]] std::optional<PresentFormatConversion> resolvePresentFormatConversion(vk::Format format)
{
    switch (format)
    {
    case vk::Format::eB8G8R8A8Srgb:
        return PresentFormatConversion{
            .swizzleBgr = true,
            .outputSrgb = true,
        };
    case vk::Format::eB8G8R8A8Unorm:
        return PresentFormatConversion{
            .swizzleBgr = true,
        };
    case vk::Format::eR8G8B8A8Srgb:
        return PresentFormatConversion{
            .outputSrgb = true,
        };
    case vk::Format::eR8G8B8A8Unorm:
        return PresentFormatConversion{};
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::shared_ptr<PresentRuntimeCache> ensurePresentRuntime(nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/presentConvert"),
    });
    nr::nrAssert(program.valid(), "Present pass failed to compile shader module renderer/presentConvert.");

    auto pipelineDesc = nr::rhi::ComputePipelineDesc{
        .entryPointName = "presentConvertMain",
    };

    auto runtime = std::make_shared<PresentRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "Present pass failed to create compute pipeline.");

    return runtime;
}

[[nodiscard]] std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "divideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] vk::DeviceSize presentReadbackByteSize(vk::Extent2D extent) noexcept
{
    return static_cast<vk::DeviceSize>(extent.width) *
           static_cast<vk::DeviceSize>(extent.height) *
           vk::DeviceSize{4u};
}

void ensureConvertedColorImage(
    nr::rhi::Device& device,
    PresentRuntimeCache& runtime,
    vk::Extent2D extent)
{
    if (runtime.allocatedExtent == extent && runtime.convertedColorImage.valid())
    {
        return;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(
        vk::Format::eR8G8B8A8Unorm,
        extent,
        vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc);

    runtime.convertedColorImage = device.resourceFactory.createImage(
        imageInfo,
        nr::rhi::MemoryUsage::GpuOnly,
        "Present.ConvertedColor");
    nr::nrAssert(runtime.convertedColorImage.valid(), "Present failed to allocate convertedColor image.");

    runtime.allocatedExtent = extent;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
PresentNode::~PresentNode() = default;

NodeDescription PresentNode::describe() const
{
    return NodeDescription{
        .name = "Present",
    };
}

void PresentNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    runtime_ = detail::ensurePresentRuntime(context.device.get());
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        describe().name + ".Pipeline");
}

void PresentNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Present build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "Present build stage requires device reference from initialize stage.");

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Present");
    auto uiBuffer = context.requireFrameResource(nr::renderer::frameResource::uiColor, "Present");

    auto viewportExtent = input.viewportExtent;
    if (viewportExtent.width == 1 && viewportExtent.height == 1)
    {
        viewportExtent = frameParameters.swapchainExtent;
    }

    auto swapchainFormat = frameParameters.swapchainFormat == vk::Format::eUndefined
                               ? input.format
                               : frameParameters.swapchainFormat;
    auto formatConversion = detail::resolvePresentFormatConversion(swapchainFormat);
    nr::nrAssert(
        formatConversion.has_value(),
        std::format("Present node only supports RGBA8/BGRA8 swapchain formats for compute conversion. got={}", vk::to_string(swapchainFormat)));

    detail::ensureConvertedColorImage(device_->get(), *runtime_, viewportExtent);

    auto convertedColor = context.importStorageColor(
        runtime_->convertedColorImage,
        "Present.ConvertedColor",
        viewportExtent,
        vk::Format::eR8G8B8A8Unorm);

    auto swapchainFrameParameters = frameParameters;
    swapchainFrameParameters.swapchainExtent = viewportExtent;
    swapchainFrameParameters.swapchainFormat = swapchainFormat;
    auto swapchainImage = context.importSwapchain("Swapchain.Image", swapchainFrameParameters);

    auto conversionExtent = vk::Extent2D{
        std::max(1u, viewportExtent.width),
        std::max(1u, viewportExtent.height),
    };

    auto pushConstants = detail::PresentConvertPushConstants{
        .width = conversionExtent.width,
        .height = conversionExtent.height,
        .swizzleBgr = formatConversion->swizzleBgr ? 1u : 0u,
        .outputSrgb = formatConversion->outputSrgb ? 1u : 0u,
        .flipY = input.flipY ? 1u : 0u,
        .uiOpacity = std::clamp(input.uiOpacity, 0.0f, 1.0f),
    };

    auto convertPass = nr::renderer::ComputePassBuilder{
        context,
        "Present.Convert",
        runtime_->pipeline};
    convertPass
        .sampledImage("gSourceColor", sourceColor, "Present.SourceColor")
        .sampledImage("gUiColor", uiBuffer, "Present.UiBuffer")
        .storageImage("gConvertedColor", convertedColor, "Present.ConvertedColor")
        .pushConstants("gPresentConvert", pushConstants)
        .record([conversionExtent](const nr::renderer::ComputePassRecordContext& computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::divideRoundUp(conversionExtent.width, kThreadGroupSize),
                detail::divideRoundUp(conversionExtent.height, kThreadGroupSize),
                1u);
        });

    [[maybe_unused]] auto convertPassHandle = convertPass.build();

    if (input.readback.has_value())
    {
        nr::nrAssert(
            context.queue == nr::renderer::QueueDomain::Compute,
            "Present readback copy must be recorded by a Present node running on the compute queue.");

        auto const readbackTarget = *input.readback;
        auto const requiredReadbackBytes = detail::presentReadbackByteSize(conversionExtent);
        auto const& readbackBuffer = readbackTarget.buffer.get();
        nr::nrAssert(readbackBuffer.valid(), "Present readback target requires a valid buffer.");
        nr::nrAssert(
            (readbackBuffer.usage() & vk::BufferUsageFlagBits::eTransferDst) != vk::BufferUsageFlags{},
            "Present readback target buffer must include eTransferDst usage.");
        nr::nrAssert(
            readbackBuffer.mapped() != nullptr,
            "Present readback target buffer must be host visible and persistently mapped.");
        nr::nrAssert(
            readbackTarget.offset <= readbackBuffer.size() &&
                requiredReadbackBytes <= readbackBuffer.size() - readbackTarget.offset,
            std::format(
                "Present readback target range [{}..{}) exceeds buffer size {}.",
                readbackTarget.offset,
                readbackTarget.offset + requiredReadbackBytes,
                readbackBuffer.size()));

        auto readbackResource = context.importBuffer(
            readbackBuffer,
            "Present.ReadbackBuffer",
            nr::renderer::ResourceLifetime::RendererPersistent,
            {
                nr::renderer::BufferUsageIntent::Readback,
            },
            nr::renderer::ResourceOwnershipDomain::Compute);

        auto readbackPassIntents = std::array{
            nr::renderer::use::imageTransferSrc(convertedColor),
            nr::renderer::use::readbackWrite(readbackResource),
        };

        [[maybe_unused]] auto readbackPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{readbackPassIntents.data(), readbackPassIntents.size()},
            "Present.CopyToReadback",
            [convertedColor, readbackResource, readbackTarget, conversionExtent, requiredReadbackBytes](
                const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(
                    recordContext.commandBuffer.has_value(),
                    "Present readback copy requires RAII command buffer access.");
                nr::nrAssert(
                    static_cast<bool>(recordContext.resolveImage),
                    "Present readback copy requires image resolver.");
                nr::nrAssert(
                    static_cast<bool>(recordContext.resolveBuffer),
                    "Present readback copy requires buffer resolver.");

                auto resolvedImage = recordContext.resolveImage(convertedColor);
                nr::nrAssert(
                    resolvedImage.has_value() && resolvedImage->resource.has_value(),
                    "Present readback copy failed to resolve converted color image resource.");
                auto resolvedBuffer = recordContext.resolveBuffer(readbackResource);
                nr::nrAssert(
                    resolvedBuffer.has_value() && resolvedBuffer->resource.has_value(),
                    "Present readback copy failed to resolve readback buffer resource.");

                auto copyRegion = vk::BufferImageCopy{};
                copyRegion.bufferOffset = readbackTarget.offset;
                copyRegion.imageSubresource = vk::ImageSubresourceLayers{
                    vk::ImageAspectFlagBits::eColor,
                    0u,
                    0u,
                    1u,
                };
                copyRegion.imageExtent = vk::Extent3D{
                    conversionExtent.width,
                    conversionExtent.height,
                    1u,
                };

                nr::rhi::ops::copyImageToBuffer(
                    recordContext.commandBuffer->get(),
                    resolvedImage->resource->get(),
                    resolvedBuffer->resource->get(),
                    vk::ImageLayout::eTransferSrcOptimal,
                    copyRegion);

                auto hostReadBarrier = nr::rhi::ops::BarrierBatch{};
                hostReadBarrier.add(nr::rhi::ops::makeBufferBarrier(
                    resolvedBuffer->resource->get(),
                    vk::BufferMemoryBarrier2{
                        vk::PipelineStageFlagBits2::eTransfer,
                        vk::AccessFlagBits2::eTransferWrite,
                        vk::PipelineStageFlagBits2::eHost,
                        vk::AccessFlagBits2::eHostRead,
                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                        nr::rhi::ops::kIgnoredQueueFamilyIndex,
                        vk::Buffer{},
                        readbackTarget.offset,
                        requiredReadbackBytes,
                        nullptr,
                    }));
                nr::rhi::ops::pipelineBarrier(recordContext.commandBuffer->get(), hostReadBarrier);
            });
    }

    auto copyPassIntents = std::array{
        nr::renderer::use::imageTransferSrc(convertedColor),
        nr::renderer::use::imageTransferDst(swapchainImage),
        nr::renderer::use::presentRead(swapchainImage),
    };

    [[maybe_unused]] auto copyPassHandle = context.addPass(
        std::span<const nr::renderer::PassResourceUseDesc>{copyPassIntents.data(), copyPassIntents.size()},
        "Present.CopyToSwapchain",
        nullptr,
        nullptr,
        true);

    context.publishFrameResource(nr::renderer::frameResource::swapchainImage, swapchainImage);
}

void PresentNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
