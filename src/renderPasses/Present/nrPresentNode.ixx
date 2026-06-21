export module nr.renderPasses:presentNode;
import dependency;

import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace
{
struct PresentConvertPushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t swizzleBgr = 0u;
    std::uint32_t outputSrgb = 0u;
    std::uint32_t flipY = 1u;
    float uiOpacity = 1.0f;
};

static_assert(sizeof(PresentConvertPushConstants) <= 128u);

struct PresentFormatConversion
{
    bool swizzleBgr = false;
    bool outputSrgb = false;
};

struct PresentRuntimeCache
{
    nr::rhi::PipelineState<nr::rhi::ComputePipeline> pipeline{};
    std::array<std::vector<nr::rhi::ShaderBindingSet>, nr::maxFrameInFlight> convertBindingSetsByFrame{};

    // Pre-allocated per-frame-slot images; avoids vmaCreateImage/vmaDestroyImage every frame.
    std::array<nr::rhi::Image, nr::maxFrameInFlight> convertedColorImages{};
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
            .outputSrgb = false,
        };
    case vk::Format::eR8G8B8A8Srgb:
        return PresentFormatConversion{
            .swizzleBgr = false,
            .outputSrgb = true,
        };
    case vk::Format::eR8G8B8A8Unorm:
        return PresentFormatConversion{
            .swizzleBgr = false,
            .outputSrgb = false,
        };
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
    runtime->pipeline = device.pipeline().createComputePipeline(program, pipelineDesc);
    nr::nrAssert(runtime->pipeline.pipeline.valid(), "Present pass failed to create compute pipeline.");

    auto frameSlots = std::views::iota(std::size_t{0}, runtime->convertBindingSetsByFrame.size());
    std::ranges::for_each(frameSlots, [&](std::size_t frameSlot) {
        runtime->convertBindingSetsByFrame[frameSlot] =
            nr::rhi::allocateBindingSetsForLayout(runtime->pipeline.layout, runtime->pipeline.bindingPool);
    });

    return runtime;
}

[[nodiscard]] std::uint32_t divideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "divideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

void ensureConvertedColorImages(
    nr::rhi::Device& device,
    PresentRuntimeCache& runtime,
    vk::Extent2D extent)
{
    if (runtime.allocatedExtent == extent && runtime.convertedColorImages[0].valid())
    {
        return;
    }

    auto imageInfo = vk::ImageCreateInfo{};
    imageInfo.imageType = vk::ImageType::e2D;
    imageInfo.format = vk::Format::eR8G8B8A8Unorm;
    imageInfo.extent = vk::Extent3D{extent.width, extent.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = vk::SampleCountFlagBits::e1;
    imageInfo.tiling = vk::ImageTiling::eOptimal;
    imageInfo.usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eTransferSrc;
    imageInfo.sharingMode = vk::SharingMode::eExclusive;
    imageInfo.initialLayout = vk::ImageLayout::eUndefined;

    auto frameSlots = std::views::iota(std::size_t{0}, runtime.convertedColorImages.size());
    std::ranges::for_each(frameSlots, [&](std::size_t slot) {
        runtime.convertedColorImages[slot] = device.resourceFactory.createImage(
            imageInfo, nr::rhi::MemoryUsage::GpuOnly,
            std::format("Present.ConvertedColor[{}]", slot));
        nr::nrAssert(runtime.convertedColorImages[slot].valid(),
            std::format("Present failed to allocate convertedColor image for frame slot {}.", slot));
    });

    runtime.allocatedExtent = extent;
}
} // namespace

export namespace nr::renderPasses
{
struct PresentNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    bool flipY = true;
    float uiOpacity = 1.0f;
};

struct PresentNodeOutput
{
    nr::renderer::GraphResourceHandle swapchainImage{};
};

class PresentNode final : public Node
{
  public:
    PresentNodeInput input{};
    PresentNodeOutput output{};

    [[nodiscard]] NodeDescription describe() const override
    {
        return NodeDescription{
            .name = "Present",
            .inputPorts = {
                NodePort{.name = "sourceColor"},
                NodePort{.name = "uiBuffer"},
            },
            .outputPorts = {
                NodePort{.name = "swapchain"},
            },
        };
    }

    void initialize(NodeInitContext& context) override
    {
        device_ = context.device;
        runtime_ = ensurePresentRuntime(context.device.get());
        nr::rhi::setPipelineDebugName(
            context.device.get().device,
            runtime_->pipeline.pipeline.raw(),
            describe().name + ".Pipeline");
    }

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override
    {
        nr::nrAssert(static_cast<bool>(runtime_), "Present build stage requires initialized runtime state.");
        nr::nrAssert(device_.has_value(), "Present build stage requires device reference from initialize stage.");

        auto sourceColor = context.resolveInput("sourceColor");
        nr::nrAssert(sourceColor.valid(), "Present node requires a valid sourceColor input from upstream graph connection.");
        auto uiBuffer = context.resolveInput("uiBuffer");
        nr::nrAssert(uiBuffer.valid(), "Present node requires a valid uiBuffer input from upstream graph connection.");

        auto viewportExtent = input.viewportExtent;
        if (viewportExtent.width == 1 && viewportExtent.height == 1)
        {
            viewportExtent = frameParameters.swapchainExtent;
        }

        auto swapchainFormat = frameParameters.swapchainFormat == vk::Format::eUndefined
                                   ? input.format
                                   : frameParameters.swapchainFormat;
        auto formatConversion = resolvePresentFormatConversion(swapchainFormat);
        nr::nrAssert(
            formatConversion.has_value(),
            std::format("Present node only supports RGBA8/BGRA8 swapchain formats for compute conversion. got={}", vk::to_string(swapchainFormat)));

        ensureConvertedColorImages(device_->get(), *runtime_, viewportExtent);

        auto convertedColor = context.importFrameStorageColor(
            runtime_->convertedColorImages,
            "Present.ConvertedColor",
            viewportExtent,
            vk::Format::eR8G8B8A8Unorm);

        auto swapchainFrameParameters = frameParameters;
        swapchainFrameParameters.swapchainExtent = viewportExtent;
        swapchainFrameParameters.swapchainFormat = swapchainFormat;
        auto swapchainImage = context.importSwapchain("Swapchain.Image", swapchainFrameParameters);

        output.swapchainImage = swapchainImage;

        auto runtime = runtime_;
        auto conversionExtent = vk::Extent2D{
            std::max(1u, viewportExtent.width),
            std::max(1u, viewportExtent.height),
        };

        auto pushConstants = PresentConvertPushConstants{
            .width = conversionExtent.width,
            .height = conversionExtent.height,
            .swizzleBgr = formatConversion->swizzleBgr ? 1u : 0u,
            .outputSrgb = formatConversion->outputSrgb ? 1u : 0u,
            .flipY = input.flipY ? 1u : 0u,
            .uiOpacity = std::clamp(input.uiOpacity, 0.0f, 1.0f),
        };

        auto convertRoot = runtime->pipeline.descriptorLayout.rootCursor();
        auto sourceCursor = convertRoot["gSourceColor"];
        auto uiCursor = convertRoot["gUiColor"];
        auto convertedCursor = convertRoot["gConvertedColor"];
        auto pushCursor = convertRoot["gPresentConvert"];

        static_cast<void>(sourceCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = sourceColor.value,
            .debugName = "Present.SourceColor",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        static_cast<void>(uiCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = uiBuffer.value,
            .debugName = "Present.UiBuffer",
            .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        }));
        static_cast<void>(convertedCursor.setObject(nr::rhi::LogicalResourceDescriptorWrite{
            .logicalResourceId = convertedColor.value,
            .debugName = "Present.ConvertedColor",
            .imageLayout = vk::ImageLayout::eGeneral,
        }));
        static_cast<void>(pushCursor.setData(pushConstants));

        auto convertBindingSnapshot = convertRoot.snapshot();
        convertRoot.clearSnapshot();

        auto convertPassIntents = std::array{
            nr::renderer::use::sampledRead(sourceColor),
            nr::renderer::use::sampledRead(uiBuffer),
            nr::renderer::use::storageWrite(convertedColor),
        };

        [[maybe_unused]] auto convertPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{convertPassIntents.data(), convertPassIntents.size()},
            "Present.Convert",
            [runtime,
              conversionExtent,
              convertBindingSnapshot](const nr::renderer::PassRecordContext& recordContext) {
                nr::nrAssert(recordContext.commandBuffer.has_value(), "Present convert pass requires RAII command buffer access.");
                nr::nrAssert(static_cast<bool>(runtime), "Present convert pass record stage requires initialized runtime state.");

                auto& commandBuffer = recordContext.commandBuffer->get();
                commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, runtime->pipeline.pipeline.raw());

                auto frameSlot = static_cast<std::size_t>(recordContext.frameIndex % runtime->convertBindingSetsByFrame.size());
                auto const& frameBindingSets = runtime->convertBindingSetsByFrame[frameSlot];
                nr::nrAssert(!frameBindingSets.empty(), "Present convert pass requires preallocated descriptor sets for the active frame slot.");

                nr::rhi::bindPreparedResourcesToCommandBuffer(
                    commandBuffer,
                    vk::PipelineBindPoint::eCompute,
                    runtime->pipeline.layout,
                    std::span<const nr::rhi::ShaderBindingSet>{frameBindingSets.data(), frameBindingSets.size()});

                nr::rhi::pushConstantsToCommandBuffer(
                    commandBuffer,
                    runtime->pipeline.layout,
                    convertBindingSnapshot);

                constexpr auto kThreadGroupSize = 16u;
                commandBuffer.dispatch(
                    divideRoundUp(conversionExtent.width, kThreadGroupSize),
                    divideRoundUp(conversionExtent.height, kThreadGroupSize),
                    1u);
            },
            [runtime, convertBindingSnapshot](const nr::renderer::PassPrepareContext& prepareContext) {
                nr::nrAssert(static_cast<bool>(runtime), "Present convert pass prepare stage requires initialized runtime state.");

                auto frameSlot = static_cast<std::size_t>(prepareContext.frameIndex % runtime->convertBindingSetsByFrame.size());
                auto const& frameBindingSets = runtime->convertBindingSetsByFrame[frameSlot];
                nr::nrAssert(!frameBindingSets.empty(), "Present convert pass prepare requires preallocated descriptor sets for the active frame slot.");

                nr::rhi::updateResourcesForBindingSnapshot(
                    runtime->pipeline.bindingPool,
                    std::span<const nr::rhi::ShaderBindingSet>{frameBindingSets.data(), frameBindingSets.size()},
                    convertBindingSnapshot,
                    nr::renderer::makeDefaultLogicalDescriptorResolver(prepareContext));
            });

        auto copyPassIntents = std::array{
            nr::renderer::use::transferSrc(convertedColor),
            nr::renderer::use::transferDst(swapchainImage),
            nr::renderer::use::presentRead(swapchainImage),
        };

        [[maybe_unused]] auto copyPassHandle = context.addPass(
            std::span<const nr::renderer::PassResourceUseDesc>{copyPassIntents.data(), copyPassIntents.size()},
            "Present.CopyToSwapchain",
            nullptr,
            nullptr,
            true);

        context.publishOutput("swapchain", swapchainImage);
    }

    void shutdown(NodeShutdownContext&) override
    {
        // Explicitly clear binding sets to ensure all descriptor sets are properly
        // released to the binding pool before the pool is destroyed
        if (runtime_)
        {
            for (auto& bindingSets : runtime_->convertBindingSetsByFrame)
            {
                bindingSets.clear();
            }
        }
        runtime_.reset();
        device_.reset();
    }

  private:
    std::shared_ptr<PresentRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
