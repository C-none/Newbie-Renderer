module nr.renderPasses;

import dependency.math;
import dependency.vulkan;
import :dlssRayReconstruction;
import nr.options;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct DlssMotionVectorDebugPushConstants
{
    std::array<std::uint32_t, 2u> outputBase{};
    std::array<std::uint32_t, 2u> outputSize{};
    std::array<std::uint32_t, 2u> motionVectorBase{};
    std::array<std::uint32_t, 2u> motionVectorSize{};
    std::array<float, 2u> motionVectorScale{1.0f, 1.0f};
};
static_assert(sizeof(DlssMotionVectorDebugPushConstants) == 40u);
static_assert(sizeof(DlssMotionVectorDebugPushConstants) <= nr::rhi::kMaxPushConstantBytes);

struct DlssRayReconstructionRuntime
{
    std::mutex mutex{};
    std::unique_ptr<nr::rhi::DlssRayReconstructionFeature> feature{};
    std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> motionVectorDebugPipeline{};
    std::optional<nr::rhi::DlssRayReconstructionCreateDesc> activeCreateDesc{};
    bool resetNextEvaluation = false;
};

struct DlssRayReconstructionResolutionControllerImpl
{
    static constexpr auto qualityCount = static_cast<std::size_t>(nr::rhi::DlssQuality::Count);

    std::array<std::optional<nr::rhi::DlssOptimalSettings>, qualityCount> optimalSettingsByQuality{};
    std::optional<vk::Extent2D> cacheDisplayExtent{};
    std::optional<DlssRayReconstructionResolutionSnapshot> snapshot{};
};

void validateDlssOptimalSettings(const nr::rhi::DlssOptimalSettings &settings, nr::rhi::DlssDimensions targetSize,
                                 nr::rhi::DlssQuality quality)
{
    nr::nrAssert(settings.status.success(), "DLSS RR optimal-settings query failed: {}", settings.status.message);
    nr::nrAssert(settings.optimalRenderSize.valid() && settings.minimumRenderSize.valid() &&
                     settings.maximumRenderSize.valid(),
                 "DLSS RR optimal-settings query returned zero dimensions.");
    nr::nrAssert(settings.minimumRenderSize.width <= settings.optimalRenderSize.width &&
                     settings.minimumRenderSize.height <= settings.optimalRenderSize.height &&
                     settings.optimalRenderSize.width <= settings.maximumRenderSize.width &&
                     settings.optimalRenderSize.height <= settings.maximumRenderSize.height &&
                     settings.maximumRenderSize.width <= targetSize.width &&
                     settings.maximumRenderSize.height <= targetSize.height,
                 "DLSS RR optimal-settings query returned inconsistent dimensions or bounds outside the target size.");
    if (quality == nr::rhi::DlssQuality::Dlaa)
    {
        nr::nrAssert(settings.optimalRenderSize == targetSize,
                     "DLSS RR DLAA optimal render size must equal the target size.");
    }
}

[[nodiscard]] constexpr std::size_t slotIndex(nr::rhi::DlssRayReconstructionResourceSlot slot) noexcept
{
    return static_cast<std::size_t>(slot);
}

[[nodiscard]] constexpr std::size_t subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot slot) noexcept
{
    return static_cast<std::size_t>(slot);
}

[[nodiscard]] bool dlssInputResourceActive(const DlssRayReconstructionNodeInput &input,
                                           nr::rhi::DlssRayReconstructionResourceSlot slot) noexcept
{
    using Resource = nr::rhi::DlssRayReconstructionResourceSlot;
    if (slot == Resource::Output || slot == Resource::OutputAlpha)
    {
        return false;
    }
    return dlssRayReconstructionResourceRequired(slot, input.create.roughnessMode,
                                                 input.create.flags.alphaUpscaling) ||
           input.includeResources[slotIndex(slot)];
}

void validateCoordinatedResolutionOverrides(const DlssRayReconstructionNodeInput &input, bool coordinated)
{
    if (!coordinated)
    {
        return;
    }
    nrAssert(!input.overrideRenderSize && !input.overrideTargetSize,
             "Coordinated DLSS RR resolution forbids node-local render and target size overrides.");
}

void validateFiniteValues(std::span<const float> values, std::string_view label)
{
    nrAssert(std::ranges::all_of(values, [](float value) { return std::isfinite(value); }),
             "DLSS RR {} must contain only finite values.", label);
}

void validateDlssEvaluationConfiguration(const DlssRayReconstructionEvalConfig &evaluate)
{
    auto const hasWorldToView = evaluate.worldToViewRowMajor.has_value();
    auto const hasViewToClip = evaluate.viewToClipRowMajor.has_value();
    nrAssert(hasWorldToView == hasViewToClip,
             "DLSS RR manual world-to-view and view-to-clip matrices must be configured as a pair.");
    if (!evaluate.automaticMatrices)
    {
        nrAssert(hasWorldToView,
                 "DLSS RR manual matrix mode requires world-to-view and view-to-clip matrices.");
        validateFiniteValues(*evaluate.worldToViewRowMajor, "manual world-to-view matrix");
        validateFiniteValues(*evaluate.viewToClipRowMajor, "manual view-to-clip matrix");
    }
    if (!evaluate.automaticJitter)
    {
        validateFiniteValues(evaluate.manualJitter, "manual jitter");
    }
    if (!evaluate.automaticFrameTimeDelta)
    {
        nrAssert(std::isfinite(evaluate.manualFrameTimeDeltaMilliseconds),
                 "DLSS RR manual frame-time delta must be finite.");
    }
    validateFiniteValues(evaluate.motionVectorScale, "motion-vector scale");
    nrAssert(std::isfinite(evaluate.preExposure), "DLSS RR pre-exposure must be finite.");
    nrAssert(std::isfinite(evaluate.exposureScale), "DLSS RR exposure scale must be finite.");
}

void validateDlssResolvedConfiguration(const nr::rhi::DlssRayReconstructionCreateDesc &createDesc,
                                       bool depthOfFieldGuideActive)
{
    nrAssert(createDesc.renderSize.valid() && createDesc.targetSize.valid(),
             "DLSS RR requires non-zero render and target sizes.");
    nrAssert(createDesc.quality != nr::rhi::DlssQuality::Count, "DLSS RR quality value is invalid.");
    if (createDesc.quality == nr::rhi::DlssQuality::Dlaa)
    {
        nrAssert(createDesc.renderSize == createDesc.targetSize,
                 "DLSS RR DLAA requires render size to equal target size.");
    }
    if (depthOfFieldGuideActive)
    {
        auto const activePreset = createDesc.presets[static_cast<std::size_t>(createDesc.quality)];
        nrAssert(activePreset == nr::rhi::DlssRayReconstructionPreset::E,
                 "DLSS RR requires Preset E for the active quality mode when the Depth of Field guide is included.");
    }
}

struct SubrectResourceMapping
{
    nr::rhi::DlssRayReconstructionSubrectSlot subrect;
    nr::rhi::DlssRayReconstructionResourceSlot resource;
};

inline constexpr auto subrectResourceMappings = std::array{
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Alpha,
                           nr::rhi::DlssRayReconstructionResourceSlot::Alpha},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::OutputAlpha,
                           nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseAlbedo,
                           nr::rhi::DlssRayReconstructionResourceSlot::DiffuseAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularAlbedo,
                           nr::rhi::DlssRayReconstructionResourceSlot::SpecularAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Normals,
                           nr::rhi::DlssRayReconstructionResourceSlot::Normals},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Roughness,
                           nr::rhi::DlssRayReconstructionResourceSlot::Roughness},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Color,
                           nr::rhi::DlssRayReconstructionResourceSlot::Color},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Depth,
                           nr::rhi::DlssRayReconstructionResourceSlot::Depth},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::MotionVectors,
                           nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Translucency,
                           nr::rhi::DlssRayReconstructionResourceSlot::TransparencyMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::BiasCurrentColor,
                           nr::rhi::DlssRayReconstructionResourceSlot::BiasCurrentColorMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Output,
                           nr::rhi::DlssRayReconstructionResourceSlot::Output},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ReflectedAlbedo,
                           nr::rhi::DlssRayReconstructionResourceSlot::ReflectedAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeParticles,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeParticles},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterParticles,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterParticles},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeTransparency,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeTransparency},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterTransparency,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterTransparency},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeFog,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeFog},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterFog,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterFog},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ScreenSpaceSubsurfaceScatteringGuide,
                           nr::rhi::DlssRayReconstructionResourceSlot::ScreenSpaceSubsurfaceScatteringGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeScreenSpaceSubsurfaceScattering,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeScreenSpaceSubsurfaceScattering},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterScreenSpaceSubsurfaceScattering,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterScreenSpaceSubsurfaceScattering},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ScreenSpaceRefractionGuide,
                           nr::rhi::DlssRayReconstructionResourceSlot::ScreenSpaceRefractionGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeScreenSpaceRefraction,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeScreenSpaceRefraction},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterScreenSpaceRefraction,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterScreenSpaceRefraction},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DepthOfFieldGuide,
                           nr::rhi::DlssRayReconstructionResourceSlot::DepthOfFieldGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeDepthOfField,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeDepthOfField},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterDepthOfField,
                           nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterDepthOfField},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseHitDistance,
                           nr::rhi::DlssRayReconstructionResourceSlot::DiffuseHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularHitDistance,
                           nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseRayDirection,
                           nr::rhi::DlssRayReconstructionResourceSlot::DiffuseRayDirection},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularRayDirection,
                           nr::rhi::DlssRayReconstructionResourceSlot::SpecularRayDirection},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseRayDirectionHitDistance,
                           nr::rhi::DlssRayReconstructionResourceSlot::DiffuseRayDirectionHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularRayDirectionHitDistance,
                           nr::rhi::DlssRayReconstructionResourceSlot::SpecularRayDirectionHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayer,
                           nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayer},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayerOpacity,
                           nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayerOpacity},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayerMotionVectors,
                           nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayerMotionVectors},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DisocclusionMask,
                           nr::rhi::DlssRayReconstructionResourceSlot::DisocclusionMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ResponsivityMask,
                           nr::rhi::DlssRayReconstructionResourceSlot::ResponsivityMask},
};
static_assert(subrectResourceMappings.size() == nr::rhi::kDlssRayReconstructionSubrectSlotCount);

[[nodiscard]] nr::rhi::DlssDimensions requiredSubrectDimensions(
    nr::rhi::DlssRayReconstructionResourceSlot resource,
    const nr::rhi::DlssRayReconstructionCreateDesc &createDesc) noexcept
{
    using Resource = nr::rhi::DlssRayReconstructionResourceSlot;
    if (resource == Resource::Output || resource == Resource::OutputAlpha)
    {
        return createDesc.targetSize;
    }
    if (resource == Resource::MotionVectors)
    {
        return createDesc.flags.motionVectorsLowResolution ? createDesc.renderSize : createDesc.targetSize;
    }
    return createDesc.renderSize;
}

void validateActiveSubrectBounds(
    const std::array<std::optional<nr::renderer::NodeImageResourceDesc>,
                     nr::rhi::kDlssRayReconstructionResourceSlotCount> &descriptions,
    const std::array<nr::rhi::DlssCoordinates, nr::rhi::kDlssRayReconstructionSubrectSlotCount> &subrectBases,
    const nr::rhi::DlssRayReconstructionCreateDesc &createDesc)
{
    std::ranges::for_each(subrectResourceMappings, [&](const SubrectResourceMapping &mapping) {
        auto const &description = descriptions[slotIndex(mapping.resource)];
        if (!description.has_value())
        {
            return;
        }

        auto const base = subrectBases[subrectSlotIndex(mapping.subrect)];
        auto const required = requiredSubrectDimensions(mapping.resource, createDesc);
        auto const extent = description->extent;
        auto const baseFits = base.x <= extent.width && base.y <= extent.height;
        auto const dimensionsFit =
            baseFits && required.width <= extent.width - base.x && required.height <= extent.height - base.y;
        nrAssert(dimensionsFit, 
                     "DLSS RR resource '{}' subrect base ({}, {}) with required size {}x{} exceeds image extent {}x{}.",
                     nr::rhi::dlssResourceSlotName(mapping.resource), base.x, base.y, required.width, required.height,
                     extent.width, extent.height);
    });
}

[[nodiscard]] vk::Extent3D outputExtent(nr::rhi::DlssDimensions targetSize, nr::rhi::DlssCoordinates subrectBase,
                                        bool outputSubrectsEnabled, std::string_view label)
{
    if (!outputSubrectsEnabled)
    {
        return vk::Extent3D{targetSize.width, targetSize.height, 1u};
    }

    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    nrAssert(subrectBase.x <= maximum - targetSize.width, "DLSS RR {} output subrect width overflows uint32.", label);
    nrAssert(subrectBase.y <= maximum - targetSize.height, "DLSS RR {} output subrect height overflows uint32.", label);
    return vk::Extent3D{subrectBase.x + targetSize.width, subrectBase.y + targetSize.height, 1u};
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> createMotionVectorDebugPipeline(
    nr::rhi::Device &device, const nr::rhi::SlangProgram &program, std::string debugName)
{
    auto pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    pipeline->initialize(device.pipeline().createComputePipeline(program, {}, 64u, {}, std::move(debugName)));
    return pipeline;
}

[[nodiscard]] std::uint32_t dlssMotionVectorDebugDivideRoundUp(std::uint32_t value, std::uint32_t divisor)
{
    nrAssert(divisor > 0u, "DLSS RR motion-vector debug dispatch requires a non-zero divisor.");
    return (value + divisor - 1u) / divisor;
}

[[nodiscard]] nr::rhi::DlssQuality dlssQualityFromOption(std::string_view value)
{
    if (value == "performance")
    {
        return nr::rhi::DlssQuality::Performance;
    }
    if (value == "balanced")
    {
        return nr::rhi::DlssQuality::Balanced;
    }
    if (value == "quality")
    {
        return nr::rhi::DlssQuality::Quality;
    }
    if (value == "ultra_performance")
    {
        return nr::rhi::DlssQuality::UltraPerformance;
    }
    nrAssert(value == "dlaa", "DLSS option snapshot contains an invalid quality.");
    return nr::rhi::DlssQuality::Dlaa;
}

[[nodiscard]] std::string dlssQualityOptionValue(nr::rhi::DlssQuality value)
{
    switch (value)
    {
    case nr::rhi::DlssQuality::Performance:
        return "performance";
    case nr::rhi::DlssQuality::Balanced:
        return "balanced";
    case nr::rhi::DlssQuality::Quality:
        return "quality";
    case nr::rhi::DlssQuality::UltraPerformance:
        return "ultra_performance";
    case nr::rhi::DlssQuality::Dlaa:
        return "dlaa";
    case nr::rhi::DlssQuality::Count:
        break;
    }
    nrAssert(false, "DLSS graph registration received an invalid quality.");
    return "quality";
}

template <typename T>
[[nodiscard]] const T &requiredOption(const nr::options::OptionFrameSnapshot &snapshot, nr::options::OptionKey<T> key)
{
    auto const *value = snapshot.find(key);
    nrAssert(value != nullptr, "DLSS requires option '{}' in the frame snapshot.", key.id());
    return *value;
}

[[nodiscard]] bool hasFrameEffect(const nr::options::OptionFrameSnapshot &snapshot,
                                  nr::options::OptionKey<nr::options::OptionWireValue::Object> key)
{
    return snapshot.effect.has_value() && snapshot.effect->id == nr::options::optionId(key);
}

[[nodiscard]] DlssRayReconstructionNodeInput resolveDlssInput(const DlssRayReconstructionNodeInput &configured,
                                                              const nr::options::OptionFrameSnapshot &snapshot)
{
    auto resolved = configured;
    resolved.enabled = requiredOption(snapshot, nr::options::keys::dlssEnabled);
    resolved.create.quality = dlssQualityFromOption(requiredOption(snapshot, nr::options::keys::dlssQuality));
    resolved.bypass = requiredOption(snapshot, nr::options::keys::dlssBypass);
    resolved.evaluate.visualizeMotionVectors = requiredOption(snapshot, nr::options::keys::dlssVisualizeMotionVectors);
    return resolved;
}

std::array<float, 16u> toDlssRowVectorMatrix(const glm::mat4 &value) noexcept
{
    // Preserve the transform while converting GLM column-vector math to NGX row-vector math.
    auto result = std::array<float, 16u>{};
    auto indices = std::views::iota(std::size_t{0u}, result.size());
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const row = index / 4u;
        auto const column = index % 4u;
        result[index] = value[static_cast<glm::length_t>(row)][static_cast<glm::length_t>(column)];
    });
    return result;
}

[[nodiscard]] nr::rhi::DlssImage makeDlssImage(const nr::renderer::PassImageResource &image,
                                               const nr::renderer::NodeImageResourceDesc &desc, bool readWrite)
{
    return nr::rhi::DlssImage{
        .image = image.image,
        .view = image.view,
        .subresourceRange = image.subresourceRange,
        .format = desc.format,
        .extent = nr::rhi::DlssDimensions{image.extent.width, image.extent.height},
        .readWrite = readWrite,
    };
}

[[nodiscard]] nr::renderer::PassPrepareCallback makeDlssPrepareCallback(
    std::shared_ptr<DlssRayReconstructionRuntime> runtime,
    nr::rhi::DlssRayReconstructionCreateDesc createDesc,
    std::optional<nr::rhi::DlssOptimalSettings> coordinatedOptimalSettings)
{
    return [runtime = std::move(runtime), createDesc,
            coordinatedOptimalSettings = std::move(coordinatedOptimalSettings)](
               const nr::renderer::PassPrepareContext &prepareContext) {
        nrAssert(prepareContext.device.has_value(), "DLSS RR prepare requires the active RHI device.");
        std::scoped_lock lock(runtime->mutex);
        auto &device = prepareContext.device->get();
        if (!runtime->feature || runtime->activeCreateDesc != createDesc)
        {
            auto optimalSettings = coordinatedOptimalSettings.has_value()
                                       ? *coordinatedOptimalSettings
                                       : device.dlssContext()->optimalSettings(createDesc.targetSize,
                                                                               createDesc.quality);
            validateDlssOptimalSettings(optimalSettings, createDesc.targetSize, createDesc.quality);
            auto replacement = device.createDlssRayReconstructionFeature(createDesc);
            runtime->feature = std::move(replacement);
            runtime->activeCreateDesc = createDesc;
            runtime->resetNextEvaluation = true;
        }
    };
}

[[nodiscard]] nr::renderer::PassRecordCallback makeDlssRecordCallback(
    std::shared_ptr<DlssRayReconstructionRuntime> runtime,
    std::array<nr::renderer::GraphResourceHandle, nr::rhi::kDlssRayReconstructionResourceSlotCount> handles,
    std::array<std::optional<nr::renderer::NodeImageResourceDesc>,
               nr::rhi::kDlssRayReconstructionResourceSlotCount> descriptions,
    nr::rhi::DlssRayReconstructionEvalDesc evalDesc)
{
    return [runtime = std::move(runtime), handles = std::move(handles), descriptions = std::move(descriptions),
            evalDesc = std::move(evalDesc)](const nr::renderer::PassRecordContext &recordContext) mutable {
        nrAssert(recordContext.commandBuffer.has_value(), "DLSS RR record requires a command buffer.");
        nrAssert(static_cast<bool>(recordContext.resolveImage),
                 "DLSS RR record requires the graph image resolver.");
        auto const indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!handles[index].valid())
            {
                return;
            }
            auto resolved = recordContext.resolveImage(handles[index]);
            nrAssert(resolved.has_value(), "DLSS RR failed to resolve image slot {}.", index);
            nrAssert(descriptions[index].has_value(), "DLSS RR image format snapshot is missing.");
            auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
            auto const readWrite = slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
                                   slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha;
            evalDesc.resources[index] = makeDlssImage(*resolved, *descriptions[index], readWrite);
        });

        std::scoped_lock lock(runtime->mutex);
        nrAssert(runtime->feature && runtime->feature->valid(), "DLSS RR record requires a prepared feature.");
        auto const resetForFeatureLifecycle = std::exchange(runtime->resetNextEvaluation, false);
        evalDesc.reset = evalDesc.reset || resetForFeatureLifecycle;
        auto const status = runtime->feature->evaluate(recordContext.commandBuffer->get(), evalDesc);
        nrAssert(status.success(), "DLSS RR evaluation failed: {}", status.message);
    };
}

} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
DlssRayReconstructionResolutionController::DlssRayReconstructionResolutionController()
    : impl_(std::make_unique<detail::DlssRayReconstructionResolutionControllerImpl>())
{
}

DlssRayReconstructionResolutionController::~DlssRayReconstructionResolutionController() = default;
DlssRayReconstructionResolutionController::DlssRayReconstructionResolutionController(
    DlssRayReconstructionResolutionController &&) noexcept = default;
DlssRayReconstructionResolutionController &DlssRayReconstructionResolutionController::operator=(
    DlssRayReconstructionResolutionController &&) noexcept = default;

nr::renderer::FrameResolutionPlan DlssRayReconstructionResolutionController::resolve(
    DlssRayReconstructionResolutionRequest request, vk::Extent2D displayExtent,
    const OptimalSettingsQuery &optimalSettingsQuery)
{
    nrAssert(static_cast<bool>(impl_), "DLSS RR resolution controller requires valid implementation state.");
    nrAssert(displayExtent.width > 0u && displayExtent.height > 0u,
             "DLSS RR resolution controller requires a non-zero display extent.");

    auto const qualityIndex = static_cast<std::size_t>(request.quality);
    nrAssert(qualityIndex < detail::DlssRayReconstructionResolutionControllerImpl::qualityCount,
             "DLSS RR resolution controller received an invalid quality mode.");
    if (request.enabled)
    {
        nrAssert(!request.bypass || request.quality == nr::rhi::DlssQuality::Dlaa,
                 "DLSS RR bypass is available only in DLAA mode.");
    }

    if (!impl_->cacheDisplayExtent.has_value() || *impl_->cacheDisplayExtent != displayExtent)
    {
        std::ranges::fill(impl_->optimalSettingsByQuality, std::nullopt);
        impl_->cacheDisplayExtent = displayExtent;
    }

    auto plan = nr::renderer::FrameResolutionPlan{
        .displayExtent = displayExtent,
        .renderExtent = displayExtent,
    };
    auto optimalSettings = std::optional<nr::rhi::DlssOptimalSettings>{};
    if (request.enabled)
    {
        nrAssert(static_cast<bool>(optimalSettingsQuery),
                 "DLSS RR enabled resolution requires an optimal-settings query.");
        auto &cachedSettings = impl_->optimalSettingsByQuality[qualityIndex];
        if (!cachedSettings.has_value())
        {
            cachedSettings = optimalSettingsQuery(nr::rhi::DlssDimensions{displayExtent.width, displayExtent.height},
                                                  request.quality);
        }

        auto const &settings = *cachedSettings;
        detail::validateDlssOptimalSettings(
            settings, nr::rhi::DlssDimensions{displayExtent.width, displayExtent.height}, request.quality);

        plan.renderExtent = vk::Extent2D{settings.optimalRenderSize.width, settings.optimalRenderSize.height};
        optimalSettings = settings;
    }

    if (impl_->snapshot.has_value())
    {
        auto const &previous = *impl_->snapshot;
        plan.resetHistory = previous.request.enabled != request.enabled ||
                            previous.request.quality != request.quality ||
                            previous.displayExtent != plan.displayExtent || previous.renderExtent != plan.renderExtent;
    }
    else
    {
        plan.resetHistory = request.enabled;
    }

    impl_->snapshot = DlssRayReconstructionResolutionSnapshot{
        .request = request,
        .displayExtent = plan.displayExtent,
        .renderExtent = plan.renderExtent,
        .optimalSettings = std::move(optimalSettings),
    };
    return plan;
}

std::optional<DlssRayReconstructionResolutionSnapshot> DlssRayReconstructionResolutionController::snapshot() const
{
    nrAssert(static_cast<bool>(impl_), "DLSS RR resolution controller requires valid implementation state.");
    return impl_->snapshot;
}

DlssRayReconstructionNodeInput makeDefaultDlssRayReconstructionNodeInput()
{
    auto result = DlssRayReconstructionNodeInput{};
    std::ranges::fill(result.create.presets, nr::rhi::DlssRayReconstructionPreset::Default);

    auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        result.resourceKeys[index] = std::format("dlss.rr.input.{}", nr::rhi::dlssResourceSlotName(slot));
        result.includeResources[index] = dlssRayReconstructionResourceRequired(slot, result.create.roughnessMode,
                                                                               result.create.flags.alphaUpscaling);
    });
    result.includeResources[detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance)] = true;
    return result;
}

bool dlssRayReconstructionResourceRequired(nr::rhi::DlssRayReconstructionResourceSlot slot,
                                           nr::rhi::DlssRoughnessMode roughnessMode, bool alphaUpscaling) noexcept
{
    using Slot = nr::rhi::DlssRayReconstructionResourceSlot;
    switch (slot)
    {
    case Slot::DiffuseAlbedo:
    case Slot::SpecularAlbedo:
    case Slot::Normals:
    case Slot::Color:
    case Slot::Depth:
    case Slot::MotionVectors:
        return true;
    case Slot::Roughness:
        return roughnessMode == nr::rhi::DlssRoughnessMode::Unpacked;
    case Slot::Alpha:
        return alphaUpscaling;
    default:
        return false;
    }
}

DlssRayReconstructionNode::DlssRayReconstructionNode() : input(makeDefaultDlssRayReconstructionNodeInput())
{
}

DlssRayReconstructionNode::~DlssRayReconstructionNode() = default;

void DlssRayReconstructionNode::declareOptions(nr::options::OptionCatalogBuilder &builder) const
{
    std::ranges::for_each(
        nr::options::makeDlssDefinitions(detail::dlssQualityOptionValue(input.create.quality)),
        [&](nr::options::OptionDefinition definition) { static_cast<void>(builder.add(std::move(definition))); });
}

void DlssRayReconstructionNode::collectOptionAvailability(const nr::options::OptionFrameSnapshot &snapshot,
                                                          nr::options::OptionAvailabilityMap &availability) const
{
    auto markAvailable = [&](const nr::options::OptionId &id) {
        availability.insert_or_assign(id, nr::options::OptionAvailability{.available = true, .reason = {}});
    };
    auto definitions = nr::options::makeDlssDefinitions();
    std::ranges::for_each(definitions, [&](const nr::options::OptionDefinition &definition) {
        if (definition.id != nr::options::optionId(nr::options::keys::dlssResetHistory))
        {
            markAvailable(definition.id);
        }
    });

    auto const *enabled = snapshot.find(nr::options::keys::dlssEnabled);
    auto const resetAvailable = runtime_ && enabled != nullptr && *enabled;
    availability.insert_or_assign(nr::options::optionId(nr::options::keys::dlssResetHistory),
                                  resetAvailable
                                      ? nr::options::OptionAvailability{.available = true, .reason = {}}
                                      : nr::options::OptionAvailability{.available = false, .reason = "dlss_disabled"});
}

void DlssRayReconstructionNode::setResolutionController(
    const std::shared_ptr<DlssRayReconstructionResolutionController> &controller) noexcept
{
    resolutionController_ = controller;
}

DlssRayReconstructionResolutionRequest dlssResolutionRequestFromSnapshot(
    const nr::options::OptionFrameSnapshot &snapshot)
{
    return DlssRayReconstructionResolutionRequest{
        .enabled = detail::requiredOption(snapshot, nr::options::keys::dlssEnabled),
        .quality = detail::dlssQualityFromOption(detail::requiredOption(snapshot, nr::options::keys::dlssQuality)),
        .bypass = detail::requiredOption(snapshot, nr::options::keys::dlssBypass),
    };
}

[[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> DlssRayReconstructionNode::shaderRequests() const
{
    return {
        nr::rhi::SlangProgramCompileFileRequest{
            .sourcePath = std::filesystem::path{"renderer/dlssRayReconstructionDebug"},
        },
    };
}

void DlssRayReconstructionNode::initialize(NodeInitContext &context)
{
    nrAssert(context.shaderPrograms.size() == 1u && context.shaderPrograms.front().entryPoint() != nullptr &&
                 context.shaderPrograms.front().entryPoint()->stage == SLANG_STAGE_COMPUTE,
             "DLSS RR initialization requires one compiled debug compute shader.");
    runtime_ = std::make_shared<detail::DlssRayReconstructionRuntime>();
    runtime_->motionVectorDebugPipeline = detail::createMotionVectorDebugPipeline(
        context.device.get(), context.shaderPrograms.front(), context.runtimeName + ".MotionVectorDebug.Pipeline");
}

void DlssRayReconstructionNode::finalizeInitialization()
{
    nrAssert(runtime_ && runtime_->motionVectorDebugPipeline && runtime_->motionVectorDebugPipeline->valid(),
             "DLSS RR async motion-vector debug PSO construction failed.");
}

void DlssRayReconstructionNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    materializeCurrentFrame(context, frameParameters);
}


void DlssRayReconstructionNode::materializeCurrentFrame(NodeBuildContext &context,
                                                        const NodeFrameParameters &frameParameters)
{
    auto const input = detail::resolveDlssInput(this->input, frameParameters.optionSnapshot.get());
    nrAssert(static_cast<bool>(runtime_), "DLSS RR build requires initialized runtime state.");
    auto const resolutionController = resolutionController_.lock();
    auto resolutionSnapshot = std::optional<DlssRayReconstructionResolutionSnapshot>{};
    detail::validateCoordinatedResolutionOverrides(input, static_cast<bool>(resolutionController));
    if (resolutionController)
    {
        resolutionSnapshot = resolutionController->snapshot();
        nrAssert(resolutionSnapshot.has_value(), "Coordinated DLSS RR build requires an early resolution snapshot.");
        auto const activeRequest = DlssRayReconstructionResolutionRequest{
            .enabled = input.enabled,
            .quality = input.create.quality,
            .bypass = input.bypass,
        };
        nrAssert(resolutionSnapshot->request == activeRequest,
                 "Coordinated DLSS RR request does not match the node's active staged configuration.");
        nrAssert(resolutionSnapshot->displayExtent == frameParameters.resolutionPlan.displayExtent,
                 "Coordinated DLSS RR snapshot display extent does not match the renderer frame resolution plan.");
        nrAssert(resolutionSnapshot->renderExtent == frameParameters.resolutionPlan.renderExtent,
                 "Coordinated DLSS RR snapshot render extent does not match the renderer frame resolution plan.");
        nrAssert(frameParameters.resolutionPlan.displayExtent == frameParameters.swapchainExtent,
                 "Coordinated DLSS RR display extent does not match the renderer swapchain extent.");
    }
    if (!input.enabled)
    {
        previousBuildTime_ = {};
        {
            std::scoped_lock lock(runtime_->mutex);
            runtime_->resetNextEvaluation = true;
        }
        return;
    }

    nrAssert(nr::rhi::dlssSdkCompiled(), "DLSS RR was enabled, but the validated NGX bridge could not be loaded.");
    nrAssert(!input.bypass || input.create.quality == nr::rhi::DlssQuality::Dlaa,
             "DLSS RR bypass is available only in DLAA mode.");
    nrAssert(input.create.flags.hdr, "DLSS RR requires the HDR feature flag.");
    detail::validateDlssEvaluationConfiguration(input.evaluate);
    nrAssert(!input.outputColorKey.empty(), "DLSS RR output color key must not be empty.");
    nrAssert(input.outputColorFormat != vk::Format::eUndefined, "DLSS RR output color format must not be undefined.");
    if (input.create.flags.alphaUpscaling)
    {
        nrAssert(!input.outputAlphaKey.empty(),
                 "DLSS RR alpha output key must not be empty when alpha upscaling is enabled.");
        nrAssert(input.outputAlphaFormat != vk::Format::eUndefined,
                 "DLSS RR output alpha format must not be undefined when alpha upscaling is enabled.");
        nrAssert(input.outputAlphaKey != input.outputColorKey,
                 "DLSS RR alpha and color output keys must be different.");
    }

    auto handles = std::array<nr::renderer::GraphResourceHandle, nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto descriptions = std::array<std::optional<nr::renderer::NodeImageResourceDesc>,
                                   nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        if (!detail::dlssInputResourceActive(input, slot))
        {
            return;
        }
        nrAssert(
            !input.resourceKeys[index].empty(), "DLSS RR resource '{}' has an empty frame-resource key.", nr::rhi::dlssResourceSlotName(slot));
        handles[index] = context.requireFrameResource(input.resourceKeys[index], "DlssRayReconstruction");
        descriptions[index] = context.describeImageResource(handles[index]);
        nrAssert(descriptions[index].has_value(), "DLSS RR resource '{}' is not an image.", input.resourceKeys[index]);
    });

    auto const colorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Color);
    nrAssert(descriptions[colorIndex].has_value(), "DLSS RR requires the noisy input color image.");
    auto createDesc = input.create;
    if (resolutionSnapshot.has_value())
    {
        createDesc.renderSize = nr::rhi::DlssDimensions{
            frameParameters.resolutionPlan.renderExtent.width,
            frameParameters.resolutionPlan.renderExtent.height,
        };
        createDesc.targetSize = nr::rhi::DlssDimensions{
            frameParameters.resolutionPlan.displayExtent.width,
            frameParameters.resolutionPlan.displayExtent.height,
        };
        nrAssert(resolutionSnapshot->optimalSettings.has_value(),
                 "Coordinated enabled DLSS RR build requires cached optimal settings.");
    }
    else
    {
        createDesc.renderSize = input.overrideRenderSize
                                    ? input.renderSizeOverride
                                    : nr::rhi::DlssDimensions{descriptions[colorIndex]->extent.width,
                                                              descriptions[colorIndex]->extent.height};
        createDesc.targetSize = input.overrideTargetSize
                                    ? input.targetSizeOverride
                                    : nr::rhi::DlssDimensions{frameParameters.swapchainExtent.width,
                                                              frameParameters.swapchainExtent.height};
    }
    auto const depthOfFieldGuideIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::DepthOfFieldGuide);
    detail::validateDlssResolvedConfiguration(createDesc, handles[depthOfFieldGuideIndex].valid());

    auto const reflectionMvIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::ReflectionMotionVectors);
    auto const gBufferSpecularMvIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::GBufferSpecularMotionVectors);
    auto const specularHitIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance);
    auto const hasReflectionMotionVectors =
        handles[reflectionMvIndex].valid() || handles[gBufferSpecularMvIndex].valid();
    auto const hasSpecularHitDistance = handles[specularHitIndex].valid();
    auto const hasAutomaticMatrices = input.evaluate.automaticMatrices;
    auto const hasManualMatrices =
        input.evaluate.worldToViewRowMajor.has_value() && input.evaluate.viewToClipRowMajor.has_value();
    nrAssert(hasReflectionMotionVectors || (hasSpecularHitDistance && (hasAutomaticMatrices || hasManualMatrices)),
             "DLSS RR requires reflection motion vectors, or specular hit distance plus world-to-view and view-to-clip "
             "matrices.");

    auto const outputColorExtent = detail::outputExtent(
        createDesc.targetSize,
        input.evaluate.subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output)],
        createDesc.enableOutputSubrects, "color");
    auto outputColor = context.addResource(nr::renderer::GraphTransientImageDesc{
        .debugName = "DLSS.RR.OutputColor",
        .extent = outputColorExtent,
        .format = input.outputColorFormat,
        .usageIntents =
            {
                nr::renderer::ImageUsageIntent::StorageWrite,
                nr::renderer::ImageUsageIntent::Sampled,
                nr::renderer::ImageUsageIntent::TransferSrc,
            },
    });
    handles[detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Output)] = outputColor;
    descriptions[detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Output)] =
        nr::renderer::NodeImageResourceDesc{
            .debugName = "DLSS.RR.OutputColor",
            .extent = outputColorExtent,
            .format = input.outputColorFormat,
        };

    auto outputAlpha = nr::renderer::GraphResourceHandle{};
    if (createDesc.flags.alphaUpscaling)
    {
        auto const outputAlphaExtent = detail::outputExtent(
            createDesc.targetSize,
            input.evaluate
                .subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::OutputAlpha)],
            createDesc.enableOutputSubrects, "alpha");
        outputAlpha = context.addResource(nr::renderer::GraphTransientImageDesc{
            .debugName = "DLSS.RR.OutputAlpha",
            .extent = outputAlphaExtent,
            .format = input.outputAlphaFormat,
            .usageIntents =
                {
                    nr::renderer::ImageUsageIntent::StorageWrite,
                    nr::renderer::ImageUsageIntent::Sampled,
                    nr::renderer::ImageUsageIntent::TransferSrc,
                },
        });
        auto const outputAlphaIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha);
        handles[outputAlphaIndex] = outputAlpha;
        descriptions[outputAlphaIndex] = nr::renderer::NodeImageResourceDesc{
            .debugName = "DLSS.RR.OutputAlpha",
            .extent = outputAlphaExtent,
            .format = input.outputAlphaFormat,
        };
    }

    detail::validateActiveSubrectBounds(descriptions, input.evaluate.subrectBases, createDesc);

    auto evalDesc = nr::rhi::DlssRayReconstructionEvalDesc{};
    evalDesc.subrectBases = input.evaluate.subrectBases;
    evalDesc.renderSubrectDimensions = createDesc.renderSize;
    evalDesc.motionVectorScale = input.evaluate.motionVectorScale;
    evalDesc.preExposure = input.evaluate.preExposure;
    evalDesc.exposureScale = input.evaluate.exposureScale;
    evalDesc.indicatorInvertXAxis = input.evaluate.indicatorInvertXAxis;
    evalDesc.indicatorInvertYAxis = input.evaluate.indicatorInvertYAxis;
    evalDesc.toneMapper = input.evaluate.toneMapper;
    evalDesc.reset =
        detail::hasFrameEffect(frameParameters.optionSnapshot.get(), nr::options::keys::dlssResetHistory) ||
        frameParameters.resolutionPlan.resetHistory;
    auto const cameraJitter = context.globalResources.get().cameraFrameState.jitter.pixelOffset;
    evalDesc.jitterOffset =
        input.evaluate.automaticJitter ? std::array{cameraJitter.x, cameraJitter.y} : input.evaluate.manualJitter;
    if (input.evaluate.automaticMatrices)
    {
        evalDesc.worldToViewRowMajor = detail::toDlssRowVectorMatrix(frameParameters.renderCameraConstants.view);
        evalDesc.viewToClipRowMajor = detail::toDlssRowVectorMatrix(frameParameters.renderCameraConstants.projection);
    }
    else
    {
        evalDesc.worldToViewRowMajor = input.evaluate.worldToViewRowMajor;
        evalDesc.viewToClipRowMajor = input.evaluate.viewToClipRowMajor;
    }
    auto const now = std::chrono::steady_clock::now();
    auto measuredDelta = previousBuildTime_ == std::chrono::steady_clock::time_point{}
                             ? 1000.0f / 60.0f
                             : std::chrono::duration<float, std::milli>(now - previousBuildTime_).count();
    previousBuildTime_ = now;
    evalDesc.frameTimeDeltaMilliseconds = input.evaluate.automaticFrameTimeDelta
                                              ? std::clamp(measuredDelta, 0.01f, 1000.0f)
                                              : input.evaluate.manualFrameTimeDeltaMilliseconds;

    auto intents = std::vector<nr::renderer::PassResourceUseDesc>{};
    intents.reserve(nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (!handles[index].valid())
            return;
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        if (slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
            slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha)
        {
            intents.push_back(nr::renderer::use::storageWrite(handles[index]));
            return;
        }
        intents.push_back(nr::renderer::use::sampledRead(handles[index], descriptions[index]->aspect));
    });

    auto coordinatedOptimalSettings =
        resolutionSnapshot.has_value() ? resolutionSnapshot->optimalSettings : std::nullopt;
    auto prepare = detail::makeDlssPrepareCallback(runtime_, createDesc, std::move(coordinatedOptimalSettings));
    auto record = detail::makeDlssRecordCallback(runtime_, handles, std::move(descriptions), std::move(evalDesc));
    auto pass = context.addPass(intents, "DLSS.RayReconstruction", std::move(record), std::move(prepare), false,
                                vk::PipelineStageFlagBits2::eComputeShader);
    if (detail::hasFrameEffect(frameParameters.optionSnapshot.get(), nr::options::keys::dlssResetHistory))
    {
        nrAssert(frameParameters.frameEffectSink.has_value() &&
                     frameParameters.frameEffectSink->get().claim(*this, pass),
                 "DLSS reset-history effect must claim its evaluation pass exactly once.");
    }

    if (input.evaluate.visualizeMotionVectors)
    {
        auto const motionVectorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors);
        auto const motionVectorSubrectIndex =
            detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::MotionVectors);
        auto const outputSubrectIndex = detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output);
        auto const motionVectorSize =
            detail::requiredSubrectDimensions(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors, createDesc);
        auto const outputBase = createDesc.enableOutputSubrects ? input.evaluate.subrectBases[outputSubrectIndex]
                                                                : nr::rhi::DlssCoordinates{};
        auto const motionVectorBase = input.evaluate.subrectBases[motionVectorSubrectIndex];
        auto const pushConstants = detail::DlssMotionVectorDebugPushConstants{
            .outputBase = {outputBase.x, outputBase.y},
            .outputSize = {createDesc.targetSize.width, createDesc.targetSize.height},
            .motionVectorBase = {motionVectorBase.x, motionVectorBase.y},
            .motionVectorSize = {motionVectorSize.width, motionVectorSize.height},
            .motionVectorScale = input.evaluate.motionVectorScale,
        };

        auto debugPass = nr::renderer::ComputePassBuilder{context, "DLSS.RayReconstruction.VisualizeMotionVectors",
                                                          runtime_->motionVectorDebugPipeline};
        debugPass.sampledImage("gMotionVectors", handles[motionVectorIndex], "DLSS.RR.MotionVectors")
            .storageImage("gMotionVectorVisualization", outputColor, "DLSS.RR.MotionVectorVisualization")
            .pushConstants("gMotionVectorDebug", pushConstants)
            .record([outputColorExtent](const nr::renderer::ComputePassRecordContext &computeContext) {
                constexpr auto threadGroupSize = 8u;
                computeContext.commandBuffer.dispatch(
                    detail::dlssMotionVectorDebugDivideRoundUp(outputColorExtent.width, threadGroupSize),
                    detail::dlssMotionVectorDebugDivideRoundUp(outputColorExtent.height, threadGroupSize), 1u);
            });
        [[maybe_unused]] auto debugPassHandle = debugPass.build();
    }

    auto publishedColor = outputColor;
    if (input.bypass && !input.evaluate.visualizeMotionVectors)
    {
        publishedColor = handles[colorIndex];
    }
    context.publishFrameResource(input.outputColorKey, publishedColor);
    if (outputAlpha.valid())
    {
        auto publishedAlpha = outputAlpha;
        if (input.bypass)
        {
            auto const alphaIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Alpha);
            nrAssert(handles[alphaIndex].valid(), "DLSS RR alpha bypass requires the input alpha resource.");
            publishedAlpha = handles[alphaIndex];
        }
        context.publishFrameResource(input.outputAlphaKey, publishedAlpha);
    }
}

void DlssRayReconstructionNode::shutdown(NodeShutdownContext &context)
{
    context.device.get().waitIdle();
    if (runtime_)
    {
        if (runtime_->motionVectorDebugPipeline)
        {
            runtime_->motionVectorDebugPipeline->clearBindingSets();
        }
        std::scoped_lock lock(runtime_->mutex);
        runtime_->feature.reset();
        runtime_->activeCreateDesc.reset();
    }
    runtime_.reset();
    resolutionController_.reset();
}
} // namespace nr::renderPasses
