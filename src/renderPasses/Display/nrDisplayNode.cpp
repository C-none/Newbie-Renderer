module nr.renderPasses;
import dependency.vulkan;

import :displayNode;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct DisplayConvertPushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t swizzleBgr = 0u;
    std::uint32_t outputSrgb = 0u;
    std::uint32_t flipY = 0u;
};

static_assert(sizeof(DisplayConvertPushConstants) <= nr::rhi::kMaxPushConstantBytes);

struct DisplayFormatConversion
{
    bool swizzleBgr = false;
    bool outputSrgb = false;
};

struct DisplayRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};

    nr::rhi::Image convertedColorImage{};
    vk::Extent2D allocatedExtent{0, 0};
};

[[nodiscard]] std::optional<DisplayFormatConversion> resolveDisplayFormatConversion(vk::Format format)
{
    switch (format)
    {
    case vk::Format::eB8G8R8A8Srgb:
        return DisplayFormatConversion{
            .swizzleBgr = true,
            .outputSrgb = true,
        };
    case vk::Format::eB8G8R8A8Unorm:
        return DisplayFormatConversion{
            .swizzleBgr = true,
        };
    case vk::Format::eR8G8B8A8Srgb:
        return DisplayFormatConversion{
            .outputSrgb = true,
        };
    case vk::Format::eR8G8B8A8Unorm:
        return DisplayFormatConversion{};
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::shared_ptr<DisplayRuntimeCache> ensureDisplayRuntime(nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/displayConvert"),
    });
    nr::nrAssert(program.valid(), "Display pass failed to compile shader module renderer/displayConvert.");

    auto pipelineDesc = nr::rhi::ComputePipelineDesc{
        .entryPointName = "displayConvertMain",
    };

    auto runtime = std::make_shared<DisplayRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "Display pass failed to create compute pipeline.");

    return runtime;
}

[[nodiscard]] std::uint32_t displayDivideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "displayDivideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

void ensureConvertedColorImage(
    nr::rhi::Device& device,
    DisplayRuntimeCache& runtime,
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
        "Display.ConvertedColor");
    nr::nrAssert(runtime.convertedColorImage.valid(), "Display failed to allocate convertedColor image.");

    runtime.allocatedExtent = extent;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
DisplayNode::~DisplayNode() = default;

NodeDescription DisplayNode::describe() const
{
    return NodeDescription{
        .name = "Display",
    };
}

void DisplayNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    runtime_ = detail::ensureDisplayRuntime(context.device.get());
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        describe().name + ".Pipeline");
}

void DisplayNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Display build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "Display build stage requires device reference from initialize stage.");

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Display");

    auto viewportExtent = input.viewportExtent;
    if (viewportExtent.width == 1 && viewportExtent.height == 1)
    {
        viewportExtent = frameParameters.swapchainExtent;
    }

    auto swapchainFormat = frameParameters.swapchainFormat == vk::Format::eUndefined
                               ? input.format
                               : frameParameters.swapchainFormat;
    auto formatConversion = detail::resolveDisplayFormatConversion(swapchainFormat);
    nr::nrAssert(
        formatConversion.has_value(),
        std::format("Display node only supports RGBA8/BGRA8 swapchain formats for compute conversion. got={}", vk::to_string(swapchainFormat)));

    detail::ensureConvertedColorImage(device_->get(), *runtime_, viewportExtent);

    auto convertedColor = context.importStorageColor(
        runtime_->convertedColorImage,
        "Display.ConvertedColor",
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

    auto pushConstants = detail::DisplayConvertPushConstants{
        .width = conversionExtent.width,
        .height = conversionExtent.height,
        .swizzleBgr = formatConversion->swizzleBgr ? 1u : 0u,
        .outputSrgb = formatConversion->outputSrgb ? 1u : 0u,
        .flipY = input.flipY ? 1u : 0u,
    };

    auto convertPass = nr::renderer::ComputePassBuilder{
        context,
        "Display.Convert",
        runtime_->pipeline};
    convertPass
        .sampledImage("gSourceColor", sourceColor, "Display.SourceColor")
        .storageImage("gConvertedColor", convertedColor, "Display.ConvertedColor")
        .pushConstants("gDisplayConvert", pushConstants)
        .record([conversionExtent](const nr::renderer::ComputePassRecordContext& computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::displayDivideRoundUp(conversionExtent.width, kThreadGroupSize),
                detail::displayDivideRoundUp(conversionExtent.height, kThreadGroupSize),
                1u);
        });

    [[maybe_unused]] auto convertPassHandle = convertPass.build();

    auto copyPassIntents = std::array{
        nr::renderer::use::transferSrc(convertedColor),
        nr::renderer::use::transferDst(swapchainImage),
        nr::renderer::use::presentRead(swapchainImage),
    };

    [[maybe_unused]] auto copyPassHandle = context.addPass(
        std::span<const nr::renderer::PassResourceUseDesc>{copyPassIntents.data(), copyPassIntents.size()},
        "Display.CopyToSwapchain",
        nullptr,
        nullptr,
        true);

    context.publishFrameResource(nr::renderer::frameResource::swapchainImage, swapchainImage);
}

void DisplayNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
