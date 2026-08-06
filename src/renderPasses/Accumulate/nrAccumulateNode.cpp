module nr.renderPasses;
import dependency.math;
import dependency.vulkan;

import :accumulateNode;
import nr.options;
import nr.renderer;
import nr.rhi;
import nr.scene;
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

struct AccumulateTemporalIdentity
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 cameraWorld{0.0f};
    nr::scene::SceneRevisionSnapshot sceneRevisions{};
};

[[nodiscard]] bool accumulateMatricesEquivalent(const glm::mat4 &left, const glm::mat4 &right) noexcept
{
    auto elements = std::views::iota(std::size_t{0}, std::size_t{16});
    return std::ranges::all_of(elements, [&](std::size_t element) {
        auto const column = element / 4u;
        auto const row = element % 4u;
        return left[column][row] == right[column][row];
    });
}

[[nodiscard]] bool accumulateVectorsEquivalent(const glm::vec3 &left, const glm::vec3 &right) noexcept
{
    auto components = std::views::iota(std::size_t{0}, std::size_t{3});
    return std::ranges::all_of(components, [&](std::size_t component) { return left[component] == right[component]; });
}

[[nodiscard]] bool accumulateTemporalIdentitiesEquivalent(const AccumulateTemporalIdentity &left,
                                                           const AccumulateTemporalIdentity &right) noexcept
{
    return accumulateMatricesEquivalent(left.view, right.view) &&
           accumulateMatricesEquivalent(left.projection, right.projection) &&
           accumulateVectorsEquivalent(left.cameraWorld, right.cameraWorld) &&
           left.sceneRevisions == right.sceneRevisions;
}

struct AccumulateRuntimeCache
{
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> pipeline{};
    std::array<nr::rhi::Image, 2u> historyImages{};
    std::array<nr::renderer::RetainedImageState, 2u> historyStates{};
    vk::Extent2D allocatedExtent{0u, 0u};
    vk::Format allocatedFormat = vk::Format::eUndefined;
    std::optional<AccumulateTemporalIdentity> previousTemporalIdentity{};
    std::uint32_t historySampleCount = 0u;
    std::uint32_t lastWrittenSlot = 1u;
};

struct AccumulateFramePlan
{
    vk::Extent2D extent{};
    vk::Format historyFormat = vk::Format::eUndefined;
    AccumulateTemporalIdentity temporalIdentity{};
    std::uint32_t previousSlot = 0u;
    std::uint32_t currentSlot = 0u;
    std::uint32_t maximumHistorySampleCount = kAccumulateDefaultMaxHistorySampleCount;
    bool resetHistory = true;
    AccumulatePushConstants pushConstants{};
};

[[nodiscard]] std::uint32_t accumulateDivideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nr::nrAssert(divisor > 0u, "accumulateDivideRoundUp requires divisor > 0.");
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] std::shared_ptr<AccumulateRuntimeCache> ensureAccumulateRuntime(nr::rhi::Device &device,
                                                                              const nr::rhi::SlangProgram &program,
                                                                              std::string debugName)
{
    auto runtime = std::make_shared<AccumulateRuntimeCache>();
    runtime->pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    runtime->pipeline->initialize(device.pipeline().createComputePipeline(program, {}, 64u, {}, std::move(debugName)));

    return runtime;
}

[[nodiscard]] bool ensureHistoryImages(nr::rhi::Device &device, AccumulateRuntimeCache &runtime, vk::Extent2D extent,
                                       vk::Format format)
{
    if (runtime.allocatedExtent == extent && runtime.allocatedFormat == format &&
        std::ranges::all_of(runtime.historyImages, [](const nr::rhi::Image &image) { return image.valid(); }))
    {
        return false;
    }

    auto imageInfo = nr::rhi::makeImageCreateInfo(format, extent,
                                                  vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eStorage |
                                                      vk::ImageUsageFlagBits::eTransferSrc);

    auto slots = std::views::iota(std::size_t{0}, runtime.historyImages.size());
    std::ranges::for_each(slots, [&](std::size_t slot) {
        runtime.historyImages[slot] = device.resourceFactory.createImage(imageInfo, nr::rhi::MemoryUsage::GpuOnly,
                                                                         std::format("Accumulate.History[{}]", slot));
        nr::nrAssert(runtime.historyImages[slot].valid(), "Accumulate failed to allocate history image.");
        runtime.historyStates[slot].reset();
    });

    runtime.allocatedExtent = extent;
    runtime.allocatedFormat = format;
    runtime.previousTemporalIdentity.reset();
    runtime.historySampleCount = 0u;
    runtime.lastWrittenSlot = 1u;
    return true;
}

[[nodiscard]] vk::Format accumulateHistoryFormat(vk::Format requestedFormat) noexcept
{
    return requestedFormat == vk::Format::eUndefined ? vk::Format::eR16G16B16A16Sfloat : requestedFormat;
}

[[nodiscard]] AccumulateFramePlan prepareAccumulateFramePlan(nr::rhi::Device &device,
                                                              AccumulateRuntimeCache &runtime,
                                                              const nr::renderer::NodeFrameParameters &frameParameters,
                                                              vk::Format historyFormat,
                                                              std::uint32_t maximumHistorySampleCount)
{
    auto const extent = frameParameters.swapchainExtent;
    nr::nrAssert(extent.width > 0u && extent.height > 0u && frameParameters.resolutionPlan.displayExtent == extent &&
                     frameParameters.resolutionPlan.renderExtent == extent,
                 "Accumulate requires identical non-zero render, display, and swapchain extents because it is not an "
                 "upscaler.");

    auto const reallocated = ensureHistoryImages(device, runtime, extent, historyFormat);
    auto const temporalIdentity = AccumulateTemporalIdentity{
        .view = frameParameters.renderCameraConstants.view,
        .projection = frameParameters.renderCameraConstants.projection,
        .cameraWorld = frameParameters.renderCameraConstants.cameraWorld,
        .sceneRevisions = frameParameters.sceneRevisions,
    };
    auto const historyAvailable = runtime.previousTemporalIdentity.has_value();
    auto const identityChanged =
        !historyAvailable ||
        !accumulateTemporalIdentitiesEquivalent(*runtime.previousTemporalIdentity, temporalIdentity);
    auto const currentSlot = historyAvailable ? 1u - runtime.lastWrittenSlot : 0u;
    auto const previousSlot = historyAvailable ? runtime.lastWrittenSlot : 1u - currentSlot;
    auto const resetHistory = frameParameters.resolutionPlan.resetHistory || reallocated || identityChanged;

    return AccumulateFramePlan{
        .extent = extent,
        .historyFormat = historyFormat,
        .temporalIdentity = temporalIdentity,
        .previousSlot = previousSlot,
        .currentSlot = currentSlot,
        .maximumHistorySampleCount = maximumHistorySampleCount,
        .resetHistory = resetHistory,
        .pushConstants =
            AccumulatePushConstants{
                .width = extent.width,
                .height = extent.height,
                .resetHistory = resetHistory ? 1u : 0u,
                .historySampleCount = resetHistory ? 0u : runtime.historySampleCount,
                .maxHistorySampleCount = maximumHistorySampleCount,
            },
    };
}

void commitAccumulateFramePlan(AccumulateRuntimeCache &runtime, const AccumulateFramePlan &framePlan) noexcept
{
    runtime.previousTemporalIdentity = framePlan.temporalIdentity;
    runtime.historySampleCount =
        framePlan.resetHistory
            ? 1u
            : std::min(runtime.historySampleCount + 1u, framePlan.maximumHistorySampleCount);
    runtime.lastWrittenSlot = framePlan.currentSlot;
}

[[nodiscard]] nr::renderer::GraphResourceHandle importHistoryImage(
    nr::renderer::NodeBuildContext &context, const nr::rhi::Image &image, nr::renderer::RetainedImageState &state,
    std::string_view debugName, vk::Extent2D extent, vk::Format format,
    std::initializer_list<nr::renderer::ImageUsageIntent> usageIntents)
{
    nr::nrAssert(image.valid(), std::format("{} image is invalid.", debugName));

    return context.addResource(nr::renderer::GraphImportedImageDesc{
        .debugName = std::string(debugName),
        .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
        .initialOwnership =
            state.common.initialized ? state.common.ownership : nr::renderer::ResourceOwnershipDomain::Undefined,
        .extent = vk::Extent3D{extent.width, extent.height, 1u},
        .format = format,
        .usageIntents = std::vector<nr::renderer::ImageUsageIntent>(usageIntents),
        .initialLayout = state.common.initialized ? state.layout : nr::renderer::ImageLayoutIntent::Undefined,
        .initialAccessScope = state.common.initialized ? state.common.access : nr::renderer::AccessScope{},
        .importedResource = std::cref(image),
        .retainedState = std::ref(state),
    });
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
namespace
{
[[nodiscard]] std::uint32_t maxHistorySampleCount(const nr::options::OptionFrameSnapshot &snapshot)
{
    auto const *value = snapshot.find(nr::options::keys::accumulateMaxHistorySamples);
    nr::nrAssert(value != nullptr, "Accumulate requires its max-history option in the frame snapshot.");
    return static_cast<std::uint32_t>(*value);
}
} // namespace

AccumulateNode::~AccumulateNode() = default;

void AccumulateNode::declareOptions(nr::options::OptionCatalogBuilder &builder) const
{
    std::ranges::for_each(nr::options::makeAccumulateDefinitions(), [&](nr::options::OptionDefinition definition) {
        static_cast<void>(builder.add(std::move(definition)));
    });
}

void AccumulateNode::collectOptionAvailability(const nr::options::OptionFrameSnapshot &,
                                               nr::options::OptionAvailabilityMap &availability) const
{
    availability.insert_or_assign(nr::options::optionId(nr::options::keys::accumulateMaxHistorySamples),
                                  nr::options::OptionAvailability{.available = true, .reason = {}});
}

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> AccumulateNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/accumulate"},
        },
    };
}

void AccumulateNode::initialize(NodeInitContext &context)
{
    nr::nrAssert(context.shaderPrograms.size() == 1u && context.shaderPrograms.front().entryPoint() != nullptr &&
                     context.shaderPrograms.front().entryPoint()->stage == SLANG_STAGE_COMPUTE,
                 "Accumulate initialization requires one compiled compute shader.");
    device_ = context.device;
    runtime_ = detail::ensureAccumulateRuntime(context.device.get(), context.shaderPrograms.front(),
                                               context.runtimeName + ".Pipeline");
}

void AccumulateNode::finalizeInitialization()
{
    nr::nrAssert(runtime_ && runtime_->pipeline && runtime_->pipeline->valid(),
                 "Accumulate async compute PSO construction failed.");
}

void AccumulateNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}

[[nodiscard]] std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> AccumulateNode::structuralSnapshot(
    const NodeFrameParameters &frameParameters) const
{
    auto const historyFormat = detail::accumulateHistoryFormat(input.historyFormat);
    return StructuralSnapshot{
        .configurationRevision = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(historyFormat)) << 32u) |
                                 maxHistorySampleCount(frameParameters.optionSnapshot.get()),
        .branchKey = "compute",
    };
}

bool AccumulateNode::materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext &context,
                                                    const NodeFrameParameters &frameParameters,
                                                    const StructuralSnapshot &)
{
    nr::nrAssert(static_cast<bool>(runtime_) && device_.has_value(),
                 "Accumulate Skeleton patch requires initialized state.");
    auto const maximumHistorySamples = maxHistorySampleCount(frameParameters.optionSnapshot.get());
    auto const framePlan = detail::prepareAccumulateFramePlan(
        device_->get(), *runtime_, frameParameters, detail::accumulateHistoryFormat(input.historyFormat),
        maximumHistorySamples);

    auto patchHistory = [&](std::size_t resourceSlot, std::uint32_t imageSlot, std::string debugName,
                             std::vector<nr::renderer::ImageUsageIntent> usages) {
        auto &state = runtime_->historyStates[imageSlot];
        context.patchResource(
            resourceSlot,
            nr::renderer::GraphImportedImageDesc{
                .debugName = std::move(debugName),
                .lifetime = nr::renderer::ResourceLifetime::RendererPersistent,
                .initialOwnership = state.common.initialized ? state.common.ownership
                                                             : nr::renderer::ResourceOwnershipDomain::Undefined,
                .extent = vk::Extent3D{framePlan.extent.width, framePlan.extent.height, 1u},
                .format = framePlan.historyFormat,
                .usageIntents = std::move(usages),
                .initialLayout = state.common.initialized ? state.layout : nr::renderer::ImageLayoutIntent::Undefined,
                .initialAccessScope = state.common.initialized ? state.common.access : nr::renderer::AccessScope{},
                .importedResource = std::cref(runtime_->historyImages[imageSlot]),
                .retainedState = std::ref(state),
            });
    };
    patchHistory(0u, framePlan.previousSlot, std::format("Accumulate.HistoryRead[{}]", framePlan.previousSlot),
                  {nr::renderer::ImageUsageIntent::Sampled});
    patchHistory(1u, framePlan.currentSlot, std::format("Accumulate.HistoryWrite[{}]", framePlan.currentSlot),
                  {
                      nr::renderer::ImageUsageIntent::StorageWrite,
                      nr::renderer::ImageUsageIntent::Sampled,
                      nr::renderer::ImageUsageIntent::TransferSrc,
                  });

    auto patch = nr::renderer::ComputePassPatchBuilder{context, 0u, "Accumulate.Compute", runtime_->pipeline};
    patch.sampledImage("gCurrentColor", context.passResource(0u, 0u), "Accumulate.CurrentColor")
        .sampledImage("gHistoryColor", context.resource(0u), "Accumulate.HistoryRead")
        .storageImage("gAccumulatedColor", context.resource(1u), "Accumulate.HistoryWrite")
        .pushConstants("gAccumulate", framePlan.pushConstants)
        .record([extent = framePlan.extent](const nr::renderer::ComputePassRecordContext &computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::accumulateDivideRoundUp(extent.width, kThreadGroupSize),
                detail::accumulateDivideRoundUp(extent.height, kThreadGroupSize), 1u);
        });
    patch.patch();

    detail::commitAccumulateFramePlan(*runtime_, framePlan);
    return true;
}

void AccumulateNode::materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nr::nrAssert(static_cast<bool>(runtime_), "Accumulate build stage requires initialized runtime state.");
    nr::nrAssert(device_.has_value(), "Accumulate build stage requires device reference from initialize stage.");

    auto sourceColor = context.requireFrameResource(nr::renderer::frameResource::presentSourceColor, "Accumulate");
    auto const maximumHistorySamples = maxHistorySampleCount(frameParameters.optionSnapshot.get());
    auto const framePlan = detail::prepareAccumulateFramePlan(
        device_->get(), *runtime_, frameParameters, detail::accumulateHistoryFormat(input.historyFormat),
        maximumHistorySamples);

    auto previousHistory = detail::importHistoryImage(
        context, runtime_->historyImages[framePlan.previousSlot], runtime_->historyStates[framePlan.previousSlot],
        std::format("Accumulate.HistoryRead[{}]", framePlan.previousSlot), framePlan.extent, framePlan.historyFormat,
        {
            nr::renderer::ImageUsageIntent::Sampled,
        });
    auto outputHistory = detail::importHistoryImage(
        context, runtime_->historyImages[framePlan.currentSlot], runtime_->historyStates[framePlan.currentSlot],
        std::format("Accumulate.HistoryWrite[{}]", framePlan.currentSlot), framePlan.extent, framePlan.historyFormat,
        {
            nr::renderer::ImageUsageIntent::StorageWrite,
            nr::renderer::ImageUsageIntent::Sampled,
            nr::renderer::ImageUsageIntent::TransferSrc,
        });

    auto accumulatePass = nr::renderer::ComputePassBuilder{context, "Accumulate.Compute", runtime_->pipeline};
    accumulatePass.sampledImage("gCurrentColor", sourceColor, "Accumulate.CurrentColor")
        .sampledImage("gHistoryColor", previousHistory, "Accumulate.HistoryRead")
        .storageImage("gAccumulatedColor", outputHistory, "Accumulate.HistoryWrite")
        .pushConstants("gAccumulate", framePlan.pushConstants)
        .record([extent = framePlan.extent](const nr::renderer::ComputePassRecordContext &computeContext) {
            constexpr auto kThreadGroupSize = 16u;
            computeContext.commandBuffer.dispatch(
                detail::accumulateDivideRoundUp(extent.width, kThreadGroupSize),
                detail::accumulateDivideRoundUp(extent.height, kThreadGroupSize), 1u);
        });

    [[maybe_unused]] auto accumulatePassHandle = accumulatePass.build();

    detail::commitAccumulateFramePlan(*runtime_, framePlan);
    context.publishFrameResource(nr::renderer::frameResource::presentSourceColor, outputHistory);
}

void AccumulateNode::shutdown(NodeShutdownContext &)
{
    if (runtime_ && runtime_->pipeline)
    {
        runtime_->pipeline->clearBindingSets();
    }
    runtime_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
