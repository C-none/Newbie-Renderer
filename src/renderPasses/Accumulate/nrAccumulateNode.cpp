module nr.renderPasses;
import dependency.math;
import dependency.vulkan;

import :accumulateNode;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct AccumulatePushConstants
{
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::uint32_t resetHistory = 1u;
    std::uint32_t historySampleCount = 0u;
    std::uint32_t maxHistorySampleCount = kAccumulateDefaultMaxHistorySampleCount;
};

static_assert(sizeof(AccumulatePushConstants) <= nr::rhi::kMaxPushConstantBytes);

struct AccumulateCameraTransform
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
};

[[nodiscard]] bool accumulateMatricesEquivalent(
    const glm::mat4& left,
    const glm::mat4& right) noexcept
{
    auto elements = std::views::iota(std::size_t{0}, std::size_t{16});
    return std::ranges::all_of(elements, [&](std::size_t element) {
        auto const column = element / 4u;
        auto const row = element % 4u;
        return left[column][row] == right[column][row];
    });
}

[[nodiscard]] bool accumulateCameraTransformsEquivalent(
    const AccumulateCameraTransform& left,
    const AccumulateCameraTransform& right) noexcept
{
    return accumulateMatricesEquivalent(left.view, right.view) &&
           accumulateMatricesEquivalent(left.projection, right.projection);
}

struct AccumulateRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};
    std::array<nr::rhi::Image, 2u> historyImages{};
    std::array<nr::renderer::RetainedImageState, 2u> historyStates{};
    vk::Extent2D allocatedExtent{0u, 0u};
    vk::Format allocatedFormat = vk::Format::eUndefined;
    std::optional<AccumulateCameraTransform> previousCameraTransform{};
    std::uint32_t historySampleCount = 0u;
    bool historyValid = false;
    std::uint32_t lastWrittenSlot = 1u;
};

[[nodiscard]] std::uint32_t accumulateDivideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "accumulateDivideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] std::shared_ptr<AccumulateRuntimeCache> ensureAccumulateRuntime(nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path("renderer/accumulate"),
    });
    nr::nrAssert(program.valid(), "Accumulate pass failed to compile shader module renderer/accumulate.");

    auto pipelineDesc = nr::rhi::ComputePipelineDesc{
        .entryPointName = "accumulateMain",
    };

    auto runtime = std::make_shared<AccumulateRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, pipelineDesc));
    nr::nrAssert(runtime->pipeline->valid(), "Accumulate pass failed to create compute pipeline.");

    return runtime;
}

[[nodiscard]] bool ensureHistoryImages(
    nr::rhi::Device& device,
    AccumulateRuntimeCache& runtime,
    vk::Extent2D extent,
    vk::Format format)
{
    if (runtime.allocatedExtent == extent &&
        runtime.allocatedFormat == format &&
        std::ranges::all_of(runtime.historyImages, [](const nr::rhi::Image& image) { return image.valid(); }))
    {
        return false;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(
        format,
        extent,
        vk::ImageUsageFlagBits::eSampled |
            vk::ImageUsageFlagBits::eStorage |
            vk::ImageUsageFlagBits::eTransferSrc);

    auto slots = std::views::iota(std::size_t{0}, runtime.historyImages.size());
    std::ranges::for_each(slots, [&](std::size_t slot) {
        runtime.historyImages[slot] = device.resourceFactory.createImage(
            imageInfo,
            nr::rhi::MemoryUsage::GpuOnly,
            std::format("Accumulate.History[{}]", slot));
        nr::nrAssert(runtime.historyImages[slot].valid(), "Accumulate failed to allocate history image.");
        runtime.historyStates[slot].reset();
    });

    runtime.allocatedExtent = extent;
    runtime.allocatedFormat = format;
    runtime.historySampleCount = 0u;
    runtime.historyValid = false;
    runtime.lastWrittenSlot = 1u;
    return true;
}

[[nodiscard]] nr::renderer::GraphResourceHandle importHistoryImage(
    nr::renderer::NodeBuildContext& context,
    const nr::rhi::Image& image,
    nr::renderer::RetainedImageState& state,
    std::string_view debugName,
    vk::Extent2D extent,
    vk::Format format,
    std::initializer_list<nr::renderer::ImageUsageIntent> usageIntents)
{
    nr::nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

    return context.addResource(nr::renderer::GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
        .initialOwnership = state.initialized
                                ? state.ownership
                                : nr::renderer::ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{extent.width, extent.height, 1u},
        .format = format,
        .usageIntents = std::vector<nr::renderer::ImageUsageIntent>(usageIntents),
        .initialLayout = state.initialized
                             ? state.layout
                             : nr::renderer::ImageLayoutIntent::Undefined,
        .initialAccessScope = state.initialized ? state.access : nr::renderer::AccessScope{},
        .importedResource = std::cref(image),
        .retainedState = std::ref(state),
    });
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
AccumulateNode::~AccumulateNode() = default;

void AccumulateNode::initialize(NodeInitContext& context)
{
    device_ = context.device;
    input.maxHistorySampleCount = std::clamp(
        input.maxHistorySampleCount,
        1u,
        kAccumulateMaxHistorySampleCount);
    runtime_ = detail::ensureAccumulateRuntime(context.device.get());
    nr::rhi::setPipelineDebugName(
        context.device.get().device,
        runtime_->pipeline->pipeline().raw(),
        context.runtimeName + ".Pipeline");
}

void AccumulateNode::collectUi(NodeUiBuildContext& context, const NodeFrameParameters&)
{
    if (pendingMaxHistorySampleCountValid_)
    {
        input.maxHistorySampleCount = std::clamp(
            pendingMaxHistorySampleCount_,
            1u,
            kAccumulateMaxHistorySampleCount);
        pendingMaxHistorySampleCountValid_ = false;
    }

    maxHistorySampleCountDraft_ = std::clamp(
        input.maxHistorySampleCount,
        1u,
        kAccumulateMaxHistorySampleCount);

    context.addSection(
        context.runtimeName(),
        [this](NodeUiWriter& ui) {
            auto value = maxHistorySampleCountDraft_;
            if (ui.inputUInt("History Samples", value, 1u, kAccumulateMaxHistorySampleCount))
            {
                maxHistorySampleCountDraft_ = std::clamp(
                    value,
                    1u,
                    kAccumulateMaxHistorySampleCount);
                pendingMaxHistorySampleCount_ = maxHistorySampleCountDraft_;
                pendingMaxHistorySampleCountValid_ = true;
            }
        },
        true,
        "controls");
}

void AccumulateNode::build(NodeBuildContext& context, const NodeFrameParameters& frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Accumulate build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "Accumulate build stage requires device reference from initialize stage.");
    input.maxHistorySampleCount = std::clamp(
        input.maxHistorySampleCount,
        1u,
        kAccumulateMaxHistorySampleCount);

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Accumulate");

    auto viewportExtent = frameParameters.swapchainExtent;
    viewportExtent.width = std::max(1u, viewportExtent.width);
    viewportExtent.height = std::max(1u, viewportExtent.height);

    auto const historyFormat = input.historyFormat == vk::Format::eUndefined
                                   ? vk::Format::eR16G16B16A16Sfloat
                                   : input.historyFormat;
    auto const reallocated = detail::ensureHistoryImages(device_->get(), *runtime_, viewportExtent, historyFormat);
    auto const currentCameraTransform = detail::AccumulateCameraTransform{
        .view = frameParameters.renderCameraConstants.view,
        .projection = frameParameters.renderCameraConstants.projection,
    };
    auto const cameraReset =
        !runtime_->previousCameraTransform.has_value() ||
        !detail::accumulateCameraTransformsEquivalent(*runtime_->previousCameraTransform, currentCameraTransform);

    auto const currentSlot = runtime_->historyValid
                                 ? 1u - runtime_->lastWrittenSlot
                                 : 0u;
    auto const previousSlot = runtime_->historyValid
                                  ? runtime_->lastWrittenSlot
                                  : 1u - currentSlot;
    auto const resetHistory = cameraReset || reallocated || !runtime_->historyValid;

    auto previousHistory = detail::importHistoryImage(
        context,
        runtime_->historyImages[previousSlot],
        runtime_->historyStates[previousSlot],
        std::format("Accumulate.HistoryRead[{}]", previousSlot),
        viewportExtent,
        historyFormat,
        {
            nr::renderer::ImageUsageIntent::Sampled,
        });
    auto outputHistory = detail::importHistoryImage(
        context,
        runtime_->historyImages[currentSlot],
        runtime_->historyStates[currentSlot],
        std::format("Accumulate.HistoryWrite[{}]", currentSlot),
        viewportExtent,
        historyFormat,
        {
            nr::renderer::ImageUsageIntent::StorageWrite,
            nr::renderer::ImageUsageIntent::Sampled,
            nr::renderer::ImageUsageIntent::TransferSrc,
        });

    auto const maxHistorySampleCount = std::clamp(input.maxHistorySampleCount, 1u, kAccumulateMaxHistorySampleCount);
    auto pushConstants = detail::AccumulatePushConstants{
        .width = viewportExtent.width,
        .height = viewportExtent.height,
        .resetHistory = resetHistory ? 1u : 0u,
        .historySampleCount = resetHistory ? 0u : runtime_->historySampleCount,
        .maxHistorySampleCount = maxHistorySampleCount,
    };

    auto accumulatePass = nr::renderer::ComputePassBuilder{
        context,
        "Accumulate.Compute",
        runtime_->pipeline};
    accumulatePass
        .sampledImage("gCurrentColor", sourceColor, "Accumulate.CurrentColor")
        .sampledImage("gHistoryColor", previousHistory, "Accumulate.HistoryRead")
        .storageImage("gAccumulatedColor", outputHistory, "Accumulate.HistoryWrite")
        .pushConstants("gAccumulate", pushConstants)
        .record([viewportExtent](const nr::renderer::ComputePassRecordContext& computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::accumulateDivideRoundUp(viewportExtent.width, kThreadGroupSize),
                detail::accumulateDivideRoundUp(viewportExtent.height, kThreadGroupSize),
                1u);
        });

    [[maybe_unused]] auto accumulatePassHandle = accumulatePass.build();

    runtime_->previousCameraTransform = currentCameraTransform;
    if (resetHistory)
    {
        runtime_->historySampleCount = 1u;
    }
    else if (runtime_->historySampleCount < kAccumulateMaxHistorySampleCount)
    {
        ++runtime_->historySampleCount;
    }
    runtime_->lastWrittenSlot = currentSlot;
    runtime_->historyValid = true;
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, outputHistory);
}

void AccumulateNode::shutdown(NodeShutdownContext&)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
