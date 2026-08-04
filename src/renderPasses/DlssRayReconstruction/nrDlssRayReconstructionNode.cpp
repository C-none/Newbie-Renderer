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
    nr::rhi::DlssOptimalSettings optimalSettings{};
    bool optimalSettingsQueried = false;
    bool resetNextEvaluation = false;
    std::string status{"Disabled; NGX has not been initialized."};
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
    nr::nrAssert(settings.status.success(),
                 std::format("DLSS RR optimal-settings query failed: {}", settings.status.message));
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
                 std::format(
                     "DLSS RR resource '{}' subrect base ({}, {}) with required size {}x{} exceeds image extent {}x{}.",
                     nr::rhi::dlssResourceSlotName(mapping.resource), base.x, base.y, required.width, required.height,
                     extent.width, extent.height));
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
    nrAssert(subrectBase.x <= maximum - targetSize.width,
             std::format("DLSS RR {} output subrect width overflows uint32.", label));
    nrAssert(subrectBase.y <= maximum - targetSize.height,
             std::format("DLSS RR {} output subrect height overflows uint32.", label));
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
    nrAssert(value != nullptr, std::format("DLSS requires option '{}' in the frame snapshot.", key.id()));
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

DlssRayReconstructionResolutionRequest DlssRayReconstructionNode::effectiveResolutionRequest(
    const nr::options::OptionFrameSnapshot &snapshot) const
{
    return dlssResolutionRequestFromSnapshot(snapshot);
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
    device_ = context.device;
    runtime_ = std::make_shared<detail::DlssRayReconstructionRuntime>();
    runtime_->motionVectorDebugPipeline = detail::createMotionVectorDebugPipeline(
        context.device.get(), context.shaderPrograms.front(), context.runtimeName + ".MotionVectorDebug.Pipeline");
    runtime_->status = nr::rhi::dlssSdkCompiled()
                           ? "NGX bridge loaded; enable the node to initialize NGX and query capability."
                           : "NGX bridge unavailable; execution will fail fast.";
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

[[nodiscard]] std::optional<nr::renderer::NodeRuntime::StructuralSnapshot> DlssRayReconstructionNode::
    structuralSnapshot(const NodeFrameParameters &frameParameters) const
{
    auto const input = detail::resolveDlssInput(this->input, frameParameters.optionSnapshot.get());
    auto branch = std::format(
        "{};bypass={};alpha={};hdr={};debug={};quality={};roughness={};outSubrects={};overrideRender={}:{}x{};"
        "overrideTarget={}:{}x{};render={}x{};display={}x{};colorFmt={};alphaFmt={};colorKey={};alphaKey={}",
        input.enabled ? "enabled" : "disabled", input.bypass ? 1u : 0u, input.create.flags.alphaUpscaling ? 1u : 0u,
        input.create.flags.hdr ? 1u : 0u, input.evaluate.visualizeMotionVectors ? 1u : 0u,
        static_cast<std::uint32_t>(input.create.quality), static_cast<std::uint32_t>(input.create.roughnessMode),
        input.create.enableOutputSubrects ? 1u : 0u, input.overrideRenderSize ? 1u : 0u, input.renderSizeOverride.width,
        input.renderSizeOverride.height, input.overrideTargetSize ? 1u : 0u, input.targetSizeOverride.width,
        input.targetSizeOverride.height, frameParameters.resolutionPlan.renderExtent.width,
        frameParameters.resolutionPlan.renderExtent.height, frameParameters.resolutionPlan.displayExtent.width,
        frameParameters.resolutionPlan.displayExtent.height, static_cast<std::uint32_t>(input.outputColorFormat),
        static_cast<std::uint32_t>(input.outputAlphaFormat), input.outputColorKey, input.outputAlphaKey);
    auto const slots = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(slots, [&](std::size_t index) {
        branch += std::format(";{}:{}:{}", index, input.includeResources[index] ? 1u : 0u, input.resourceKeys[index]);
    });
    std::ranges::for_each(input.create.presets, [&](auto preset) {
        branch += std::format(";preset={}", static_cast<std::uint32_t>(preset));
    });
    std::ranges::for_each(input.evaluate.subrectBases,
                          [&](auto base) { branch += std::format(";subrect={},{}", base.x, base.y); });
    return StructuralSnapshot{
        .configurationRevision = std::max<std::uint64_t>(1u, std::hash<std::string>{}(branch)),
        .branchKey = std::move(branch),
    };
}

bool DlssRayReconstructionNode::materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext &context,
                                                               const NodeFrameParameters &frameParameters,
                                                               const StructuralSnapshot &snapshot)
{
    auto const input = detail::resolveDlssInput(this->input, frameParameters.optionSnapshot.get());
    nrAssert(static_cast<bool>(runtime_) && device_.has_value(), "DLSS RR Skeleton patch requires initialized state.");
    if (!input.enabled)
    {
        if (!snapshot.branchKey.starts_with("disabled;"))
        {
            return false;
        }
        previousBuildTime_ = {};
        {
            std::scoped_lock lock(runtime_->mutex);
            runtime_->resetNextEvaluation = true;
        }
        return true;
    }
    if (!snapshot.branchKey.starts_with("enabled;"))
    {
        return false;
    }
    nrAssert(nr::rhi::dlssSdkCompiled(), "DLSS RR was enabled, but the validated NGX bridge could not be loaded.");
    nrAssert(!input.bypass || input.create.quality == nr::rhi::DlssQuality::Dlaa,
             "DLSS RR bypass is available only in DLAA mode.");
    nrAssert(input.create.flags.hdr, "DLSS RR requires the HDR feature flag.");
    nrAssert(!input.outputColorKey.empty(), "DLSS RR output color key must not be empty.");
    nrAssert(input.outputColorFormat != vk::Format::eUndefined, "DLSS RR output color format must not be undefined.");
    if (input.create.flags.alphaUpscaling)
    {
        nrAssert(!input.outputAlphaKey.empty(), "DLSS RR alpha output key must not be empty.");
        nrAssert(input.outputAlphaFormat != vk::Format::eUndefined, "DLSS RR alpha output format must be defined.");
        nrAssert(input.outputAlphaKey != input.outputColorKey, "DLSS RR alpha and color output keys must differ.");
    }

    auto const resolutionController = resolutionController_.lock();
    auto resolutionSnapshot = resolutionController ? resolutionController->snapshot()
                                                   : std::optional<DlssRayReconstructionResolutionSnapshot>{};
    if (resolutionController)
    {
        nrAssert(resolutionSnapshot.has_value(), "Coordinated DLSS RR Skeleton patch requires a resolution snapshot.");
        auto const request = DlssRayReconstructionResolutionRequest{
            .enabled = input.enabled,
            .quality = input.create.quality,
            .bypass = input.bypass,
        };
        nrAssert(resolutionSnapshot->request == request, "DLSS RR Skeleton resolution request is stale.");
        nrAssert(resolutionSnapshot->displayExtent == frameParameters.resolutionPlan.displayExtent &&
                     resolutionSnapshot->renderExtent == frameParameters.resolutionPlan.renderExtent,
                 "DLSS RR Skeleton resolution snapshot does not match the frame plan.");
    }
    auto handles = std::array<nr::renderer::GraphResourceHandle, nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto descriptions = std::array<std::optional<nr::renderer::NodeImageResourceDesc>,
                                   nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto const indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        if (slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
            slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha)
        {
            return;
        }
        auto const required =
            dlssRayReconstructionResourceRequired(slot, input.create.roughnessMode, input.create.flags.alphaUpscaling);
        if (!required && !input.includeResources[index])
        {
            return;
        }
        if (!context.hasNamedResource(input.resourceKeys[index]))
        {
            return;
        }
        handles[index] = context.namedResource(input.resourceKeys[index]);
        auto image = context.describeImageResource(handles[index]);
        nrAssert(image.has_value(), "DLSS RR Skeleton input must remain an image.");
        descriptions[index] = nr::renderer::NodeImageResourceDesc{
            .debugName = image->debugName,
            .extent = image->extent,
            .format = image->format,
            .aspect = image->aspect,
        };
    });
    if (std::ranges::any_of(indices, [&](std::size_t index) {
            auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
            return dlssRayReconstructionResourceRequired(slot, input.create.roughnessMode,
                                                         input.create.flags.alphaUpscaling) &&
                   slot != nr::rhi::DlssRayReconstructionResourceSlot::Output &&
                   slot != nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha && !handles[index].valid();
        }))
    {
        return false;
    }

    auto const colorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Color);
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
    }
    else
    {
        createDesc.renderSize = input.overrideRenderSize ? input.renderSizeOverride
                                                         : nr::rhi::DlssDimensions{
                                                               descriptions[colorIndex]->extent.width,
                                                               descriptions[colorIndex]->extent.height,
                                                           };
        createDesc.targetSize = input.overrideTargetSize ? input.targetSizeOverride
                                                         : nr::rhi::DlssDimensions{
                                                               frameParameters.swapchainExtent.width,
                                                               frameParameters.swapchainExtent.height,
                                                           };
    }
    nrAssert(createDesc.renderSize.valid() && createDesc.targetSize.valid(),
             "DLSS RR requires non-zero render and target sizes.");
    nrAssert(createDesc.quality != nr::rhi::DlssQuality::Count, "DLSS RR quality value is invalid.");
    if (createDesc.quality == nr::rhi::DlssQuality::Dlaa)
    {
        nrAssert(createDesc.renderSize == createDesc.targetSize,
                 "DLSS RR DLAA requires equal render and target sizes.");
    }
    auto const outputExtent = detail::outputExtent(
        createDesc.targetSize,
        input.evaluate.subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output)],
        createDesc.enableOutputSubrects, "color");
    auto const outputIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Output);
    auto const outputColor = context.resource(0u);
    context.patchResource(0u, nr::renderer::GraphTransientImageDesc{
                                  .debugName = "DLSS.RR.OutputColor",
                                  .extent = outputExtent,
                                  .format = input.outputColorFormat,
                                  .usageIntents =
                                      {
                                          nr::renderer::ImageUsageIntent::StorageWrite,
                                          nr::renderer::ImageUsageIntent::Sampled,
                                          nr::renderer::ImageUsageIntent::TransferSrc,
                                      },
                              });
    handles[outputIndex] = outputColor;
    descriptions[outputIndex] = nr::renderer::NodeImageResourceDesc{
        .debugName = "DLSS.RR.OutputColor",
        .extent = outputExtent,
        .format = input.outputColorFormat,
    };
    if (createDesc.flags.alphaUpscaling)
    {
        auto const alphaExtent = detail::outputExtent(
            createDesc.targetSize,
            input.evaluate
                .subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::OutputAlpha)],
            createDesc.enableOutputSubrects, "alpha");
        auto const alphaIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha);
        context.patchResource(1u, nr::renderer::GraphTransientImageDesc{
                                      .debugName = "DLSS.RR.OutputAlpha",
                                      .extent = alphaExtent,
                                      .format = input.outputAlphaFormat,
                                      .usageIntents =
                                          {
                                              nr::renderer::ImageUsageIntent::StorageWrite,
                                              nr::renderer::ImageUsageIntent::Sampled,
                                              nr::renderer::ImageUsageIntent::TransferSrc,
                                          },
                                  });
        handles[alphaIndex] = context.resource(1u);
        descriptions[alphaIndex] = nr::renderer::NodeImageResourceDesc{
            .debugName = "DLSS.RR.OutputAlpha",
            .extent = alphaExtent,
            .format = input.outputAlphaFormat,
        };
    }
    detail::validateActiveSubrectBounds(descriptions, input.evaluate.subrectBases, createDesc);

    auto const reflectionMvIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::ReflectionMotionVectors);
    auto const gBufferSpecularMvIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::GBufferSpecularMotionVectors);
    auto const specularHitIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance);
    auto const hasReflectionMotionVectors =
        handles[reflectionMvIndex].valid() || handles[gBufferSpecularMvIndex].valid();
    auto const hasManualMatrices =
        input.evaluate.worldToViewRowMajor.has_value() && input.evaluate.viewToClipRowMajor.has_value();
    nrAssert(hasReflectionMotionVectors ||
                 (handles[specularHitIndex].valid() && (input.evaluate.automaticMatrices || hasManualMatrices)),
             "DLSS RR requires reflection motion vectors, or hit distance plus matrices.");

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
    auto const cameraJitter = context.globalResources().cameraFrameState.jitter.pixelOffset;
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
    auto const measuredDelta = previousBuildTime_ == std::chrono::steady_clock::time_point{}
                                   ? 1000.0f / 60.0f
                                   : std::chrono::duration<float, std::milli>(now - previousBuildTime_).count();
    previousBuildTime_ = now;
    evalDesc.frameTimeDeltaMilliseconds = input.evaluate.automaticFrameTimeDelta
                                              ? std::clamp(measuredDelta, 0.01f, 1000.0f)
                                              : input.evaluate.manualFrameTimeDeltaMilliseconds;

    auto const coordinatedOptimalSettings =
        resolutionSnapshot.has_value() ? resolutionSnapshot->optimalSettings : std::nullopt;
    auto prepare = [runtime = runtime_, createDesc,
                    coordinatedOptimalSettings](const nr::renderer::PassPrepareContext &prepareContext) {
        nrAssert(prepareContext.device.has_value(), "DLSS RR prepare requires the active RHI device.");
        std::scoped_lock lock(runtime->mutex);
        auto &device = prepareContext.device->get();
        if (!runtime->feature || runtime->activeCreateDesc != createDesc)
        {
            runtime->optimalSettings =
                coordinatedOptimalSettings.has_value()
                    ? *coordinatedOptimalSettings
                    : device.dlssContext()->optimalSettings(createDesc.targetSize, createDesc.quality);
            detail::validateDlssOptimalSettings(runtime->optimalSettings, createDesc.targetSize, createDesc.quality);
            runtime->optimalSettingsQueried = true;
            runtime->feature = device.createDlssRayReconstructionFeature(createDesc);
            runtime->activeCreateDesc = createDesc;
            runtime->resetNextEvaluation = true;
            runtime->status = std::format("Ready: render {}x{}, target {}x{}, quality {}.", createDesc.renderSize.width,
                                          createDesc.renderSize.height, createDesc.targetSize.width,
                                          createDesc.targetSize.height, nr::rhi::dlssQualityName(createDesc.quality));
        }
    };
    auto record = [runtime = runtime_, handles, descriptions,
                   evalDesc](const nr::renderer::PassRecordContext &recordContext) mutable {
        nrAssert(recordContext.commandBuffer.has_value(), "DLSS RR Skeleton record requires a command buffer.");
        nrAssert(static_cast<bool>(recordContext.resolveImage),
                 "DLSS RR Skeleton record requires the graph image resolver.");
        auto const indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!handles[index].valid())
            {
                return;
            }
            auto resolved = recordContext.resolveImage(handles[index]);
            nrAssert(resolved.has_value(), std::format("DLSS RR Skeleton failed to resolve image slot {}.", index));
            nrAssert(descriptions[index].has_value(), "DLSS RR Skeleton image format snapshot is missing.");
            auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
            auto const readWrite = slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
                                   slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha;
            evalDesc.resources[index] = detail::makeDlssImage(*resolved, *descriptions[index], readWrite);
        });

        std::scoped_lock lock(runtime->mutex);
        nrAssert(runtime->feature && runtime->feature->valid(), "DLSS RR Skeleton record requires a prepared feature.");
        auto const resetForFeatureLifecycle = std::exchange(runtime->resetNextEvaluation, false);
        evalDesc.reset = evalDesc.reset || resetForFeatureLifecycle;
        auto const status = runtime->feature->evaluate(recordContext.commandBuffer->get(), evalDesc);
        nrAssert(status.success(), std::format("DLSS RR Skeleton evaluation failed: {}", status.message));
    };
    context.patchPass(0u, "DLSS.RayReconstruction", std::move(prepare), std::move(record));
    if (detail::hasFrameEffect(frameParameters.optionSnapshot.get(), nr::options::keys::dlssResetHistory))
    {
        nrAssert(frameParameters.frameEffectSink.has_value() &&
                     frameParameters.frameEffectSink->get().claim(*this, context.passHandle(0u)),
                 "DLSS reset-history effect must claim its evaluation pass exactly once.");
    }
    if (input.evaluate.visualizeMotionVectors)
    {
        auto const motionVectorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors);
        auto const motionVectorSize =
            detail::requiredSubrectDimensions(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors, createDesc);
        auto const outputBase =
            createDesc.enableOutputSubrects
                ? input.evaluate
                      .subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output)]
                : nr::rhi::DlssCoordinates{};
        auto const motionVectorBase =
            input.evaluate
                .subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::MotionVectors)];
        auto const constants = detail::DlssMotionVectorDebugPushConstants{
            .outputBase = {outputBase.x, outputBase.y},
            .outputSize = {createDesc.targetSize.width, createDesc.targetSize.height},
            .motionVectorBase = {motionVectorBase.x, motionVectorBase.y},
            .motionVectorSize = {motionVectorSize.width, motionVectorSize.height},
            .motionVectorScale = input.evaluate.motionVectorScale,
        };
        auto patch = nr::renderer::ComputePassPatchBuilder{context, 1u, "DLSS.RayReconstruction.VisualizeMotionVectors",
                                                           runtime_->motionVectorDebugPipeline};
        patch.sampledImage("gMotionVectors", handles[motionVectorIndex], "DLSS.RR.MotionVectors")
            .storageImage("gMotionVectorVisualization", outputColor, "DLSS.RR.MotionVectorVisualization")
            .pushConstants("gMotionVectorDebug", constants)
            .record([outputExtent](const nr::renderer::ComputePassRecordContext &computeContext) {
                constexpr auto threadGroupSize = 8u;
                computeContext.commandBuffer.dispatch(
                    detail::dlssMotionVectorDebugDivideRoundUp(outputExtent.width, threadGroupSize),
                    detail::dlssMotionVectorDebugDivideRoundUp(outputExtent.height, threadGroupSize), 1u);
            });
        patch.patch();
    }
    return true;
}

void DlssRayReconstructionNode::materializeCurrentFrame(NodeBuildContext &context,
                                                        const NodeFrameParameters &frameParameters)
{
    auto const input = detail::resolveDlssInput(this->input, frameParameters.optionSnapshot.get());
    nrAssert(static_cast<bool>(runtime_), "DLSS RR build requires initialized runtime state.");
    nrAssert(device_.has_value(), "DLSS RR build requires a device reference.");
    auto const resolutionController = resolutionController_.lock();
    auto resolutionSnapshot = std::optional<DlssRayReconstructionResolutionSnapshot>{};
    if (resolutionController)
    {
        nrAssert(!input.overrideRenderSize && !input.overrideTargetSize,
                 "Coordinated DLSS RR resolution forbids node-local render and target size overrides.");
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
        if (slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
            slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha)
        {
            return;
        }
        auto const required =
            dlssRayReconstructionResourceRequired(slot, input.create.roughnessMode, input.create.flags.alphaUpscaling);
        if (!required && !input.includeResources[index])
        {
            return;
        }
        nrAssert(
            !input.resourceKeys[index].empty(),
            std::format("DLSS RR resource '{}' has an empty frame-resource key.", nr::rhi::dlssResourceSlotName(slot)));
        handles[index] = context.requireFrameResource(input.resourceKeys[index], "DlssRayReconstruction");
        descriptions[index] = context.describeImageResource(handles[index]);
        nrAssert(descriptions[index].has_value(),
                 std::format("DLSS RR resource '{}' is not an image.", input.resourceKeys[index]));
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
        nrAssert(descriptions[colorIndex]->extent.width == createDesc.renderSize.width &&
                     descriptions[colorIndex]->extent.height == createDesc.renderSize.height,
                 "Coordinated DLSS RR input color extent does not match the renderer render extent.");
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
    nrAssert(createDesc.renderSize.valid() && createDesc.targetSize.valid(),
             "DLSS RR requires non-zero render and target sizes.");
    nrAssert(createDesc.quality != nr::rhi::DlssQuality::Count, "DLSS RR quality value is invalid.");
    if (createDesc.quality == nr::rhi::DlssQuality::Dlaa)
    {
        nrAssert(createDesc.renderSize == createDesc.targetSize,
                 "DLSS RR DLAA requires render size to equal target size.");
    }
    auto const depthOfFieldGuideIndex =
        detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::DepthOfFieldGuide);
    if (handles[depthOfFieldGuideIndex].valid())
    {
        auto const activePreset = createDesc.presets[static_cast<std::size_t>(createDesc.quality)];
        nrAssert(activePreset == nr::rhi::DlssRayReconstructionPreset::E,
                 "DLSS RR requires Preset E for the active quality mode when the Depth of Field guide is included.");
    }

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
    auto prepare = [runtime = runtime_, createDesc,
                    coordinatedOptimalSettings](const nr::renderer::PassPrepareContext &prepareContext) {
        nrAssert(prepareContext.device.has_value(), "DLSS RR prepare requires the active RHI device.");
        std::scoped_lock lock(runtime->mutex);
        auto &device = prepareContext.device->get();
        if (!runtime->feature || runtime->activeCreateDesc != createDesc)
        {
            if (coordinatedOptimalSettings.has_value())
            {
                runtime->optimalSettings = *coordinatedOptimalSettings;
            }
            else
            {
                auto dlssContext = device.dlssContext();
                runtime->optimalSettings = dlssContext->optimalSettings(createDesc.targetSize, createDesc.quality);
            }
            detail::validateDlssOptimalSettings(runtime->optimalSettings, createDesc.targetSize, createDesc.quality);
            runtime->optimalSettingsQueried = true;
            auto replacement = device.createDlssRayReconstructionFeature(createDesc);
            runtime->feature = std::move(replacement);
            runtime->activeCreateDesc = createDesc;
            runtime->resetNextEvaluation = true;
            auto const capabilityText =
                runtime->optimalSettings.status.success()
                    ? std::string{"capability/optimal-settings query succeeded"}
                    : std::format("optimal-settings query failed: {}", runtime->optimalSettings.status.message);
            runtime->status =
                std::format("Ready: render {}x{}, target {}x{}, quality {}; {}.", createDesc.renderSize.width,
                            createDesc.renderSize.height, createDesc.targetSize.width, createDesc.targetSize.height,
                            nr::rhi::dlssQualityName(createDesc.quality), capabilityText);
        }
    };

    auto record = [runtime = runtime_, handles, descriptions,
                   evalDesc](const nr::renderer::PassRecordContext &recordContext) mutable {
        nrAssert(recordContext.commandBuffer.has_value(), "DLSS RR record requires a command buffer.");
        nrAssert(static_cast<bool>(recordContext.resolveImage), "DLSS RR record requires the graph image resolver.");
        auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!handles[index].valid())
            {
                return;
            }
            auto resolved = recordContext.resolveImage(handles[index]);
            nrAssert(resolved.has_value(), std::format("DLSS RR failed to resolve image slot {}.", index));
            nrAssert(descriptions[index].has_value(), "DLSS RR image format snapshot is missing.");
            auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
            auto const readWrite = slot == nr::rhi::DlssRayReconstructionResourceSlot::Output ||
                                   slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha;
            evalDesc.resources[index] = detail::makeDlssImage(*resolved, *descriptions[index], readWrite);
        });

        std::scoped_lock lock(runtime->mutex);
        nrAssert(runtime->feature && runtime->feature->valid(), "DLSS RR record requires a prepared feature.");
        auto const resetForFeatureLifecycle = std::exchange(runtime->resetNextEvaluation, false);
        evalDesc.reset = evalDesc.reset || resetForFeatureLifecycle;
        auto const status = runtime->feature->evaluate(recordContext.commandBuffer->get(), evalDesc);
        nrAssert(status.success(), std::format("DLSS RR evaluation failed: {}", status.message));
    };
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

    context.publishFrameResource(input.outputColorKey, input.bypass ? handles[colorIndex] : outputColor);
    if (outputAlpha.valid())
    {
        context.publishFrameResource(input.outputAlphaKey, outputAlpha);
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
        runtime_->status = "Shut down.";
    }
    runtime_.reset();
    resolutionController_.reset();
    device_.reset();
}
} // namespace nr::renderPasses
