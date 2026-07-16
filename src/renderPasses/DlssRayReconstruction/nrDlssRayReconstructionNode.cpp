module nr.renderPasses;

import dependency.math;
import dependency.vulkan;
import :dlssRayReconstruction;
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
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Alpha, nr::rhi::DlssRayReconstructionResourceSlot::Alpha},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::OutputAlpha, nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseAlbedo, nr::rhi::DlssRayReconstructionResourceSlot::DiffuseAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularAlbedo, nr::rhi::DlssRayReconstructionResourceSlot::SpecularAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Normals, nr::rhi::DlssRayReconstructionResourceSlot::Normals},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Roughness, nr::rhi::DlssRayReconstructionResourceSlot::Roughness},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Color, nr::rhi::DlssRayReconstructionResourceSlot::Color},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Depth, nr::rhi::DlssRayReconstructionResourceSlot::Depth},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::MotionVectors, nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Translucency, nr::rhi::DlssRayReconstructionResourceSlot::TransparencyMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::BiasCurrentColor, nr::rhi::DlssRayReconstructionResourceSlot::BiasCurrentColorMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::Output, nr::rhi::DlssRayReconstructionResourceSlot::Output},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ReflectedAlbedo, nr::rhi::DlssRayReconstructionResourceSlot::ReflectedAlbedo},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeParticles, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeParticles},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterParticles, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterParticles},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeTransparency, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeTransparency},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterTransparency, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterTransparency},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeFog, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeFog},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterFog, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterFog},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ScreenSpaceSubsurfaceScatteringGuide, nr::rhi::DlssRayReconstructionResourceSlot::ScreenSpaceSubsurfaceScatteringGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeScreenSpaceSubsurfaceScattering, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeScreenSpaceSubsurfaceScattering},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterScreenSpaceSubsurfaceScattering, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterScreenSpaceSubsurfaceScattering},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ScreenSpaceRefractionGuide, nr::rhi::DlssRayReconstructionResourceSlot::ScreenSpaceRefractionGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeScreenSpaceRefraction, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeScreenSpaceRefraction},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterScreenSpaceRefraction, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterScreenSpaceRefraction},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DepthOfFieldGuide, nr::rhi::DlssRayReconstructionResourceSlot::DepthOfFieldGuide},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorBeforeDepthOfField, nr::rhi::DlssRayReconstructionResourceSlot::ColorBeforeDepthOfField},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ColorAfterDepthOfField, nr::rhi::DlssRayReconstructionResourceSlot::ColorAfterDepthOfField},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseHitDistance, nr::rhi::DlssRayReconstructionResourceSlot::DiffuseHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularHitDistance, nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseRayDirection, nr::rhi::DlssRayReconstructionResourceSlot::DiffuseRayDirection},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularRayDirection, nr::rhi::DlssRayReconstructionResourceSlot::SpecularRayDirection},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DiffuseRayDirectionHitDistance, nr::rhi::DlssRayReconstructionResourceSlot::DiffuseRayDirectionHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::SpecularRayDirectionHitDistance, nr::rhi::DlssRayReconstructionResourceSlot::SpecularRayDirectionHitDistance},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayer, nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayer},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayerOpacity, nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayerOpacity},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::TransparencyLayerMotionVectors, nr::rhi::DlssRayReconstructionResourceSlot::TransparencyLayerMotionVectors},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::DisocclusionMask, nr::rhi::DlssRayReconstructionResourceSlot::DisocclusionMask},
    SubrectResourceMapping{nr::rhi::DlssRayReconstructionSubrectSlot::ResponsivityMask, nr::rhi::DlssRayReconstructionResourceSlot::ResponsivityMask},
};
static_assert(subrectResourceMappings.size() == nr::rhi::kDlssRayReconstructionSubrectSlotCount);

[[nodiscard]] nr::rhi::DlssDimensions requiredSubrectDimensions(nr::rhi::DlssRayReconstructionResourceSlot resource, const nr::rhi::DlssRayReconstructionCreateDesc &createDesc) noexcept
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

void validateActiveSubrectBounds(const std::array<std::optional<nr::renderer::NodeImageResourceDesc>, nr::rhi::kDlssRayReconstructionResourceSlotCount> &descriptions, const std::array<nr::rhi::DlssCoordinates, nr::rhi::kDlssRayReconstructionSubrectSlotCount> &subrectBases,
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
        auto const dimensionsFit = baseFits && required.width <= extent.width - base.x && required.height <= extent.height - base.y;
        nrAssert(dimensionsFit, std::format("DLSS RR resource '{}' subrect base ({}, {}) with required size {}x{} exceeds image extent {}x{}.", nr::rhi::dlssResourceSlotName(mapping.resource), base.x, base.y, required.width, required.height, extent.width, extent.height));
    });
}

[[nodiscard]] vk::Extent3D outputExtent(nr::rhi::DlssDimensions targetSize, nr::rhi::DlssCoordinates subrectBase, bool outputSubrectsEnabled, std::string_view label)
{
    if (!outputSubrectsEnabled)
    {
        return vk::Extent3D{targetSize.width, targetSize.height, 1u};
    }

    constexpr auto maximum = std::numeric_limits<std::uint32_t>::max();
    nrAssert(subrectBase.x <= maximum - targetSize.width, std::format("DLSS RR {} output subrect width overflows uint32.", label));
    nrAssert(subrectBase.y <= maximum - targetSize.height, std::format("DLSS RR {} output subrect height overflows uint32.", label));
    return vk::Extent3D{subrectBase.x + targetSize.width, subrectBase.y + targetSize.height, 1u};
}

[[nodiscard]] std::shared_ptr<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>> createMotionVectorDebugPipeline(
    nr::rhi::Device& device)
{
    auto& shaderService = nr::rhi::ShaderService::instance();
    auto program = shaderService.compileProgramByFile(nr::rhi::SlangProgramCompileFileRequest{
        .sourcePath = std::filesystem::path{"renderer/dlssRayReconstructionDebug"},
    });
    nrAssert(program.valid(), "DLSS RR motion-vector debug pass failed to compile renderer/dlssRayReconstructionDebug.");

    auto pipeline = std::make_shared<nr::renderer::PipelineRuntime<nr::rhi::ComputePipeline>>();
    pipeline->initialize(device.pipeline().createComputePipeline(
        program,
        nr::rhi::ComputePipelineDesc{
            .entryPointName = "visualizeMotionVectorsMain",
        }));
    nrAssert(pipeline->valid(), "DLSS RR motion-vector debug pass failed to create its compute pipeline.");
    return pipeline;
}

[[nodiscard]] std::uint32_t dlssMotionVectorDebugDivideRoundUp(
    std::uint32_t value,
    std::uint32_t divisor)
{
    nrAssert(divisor > 0u, "DLSS RR motion-vector debug dispatch requires a non-zero divisor.");
    return (value + divisor - 1u) / divisor;
}

std::array<float, 16u> toDlssRowVectorMatrix(const glm::mat4& value) noexcept
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

[[nodiscard]] nr::rhi::DlssImage makeDlssImage(const nr::renderer::PassImageResource &image, const nr::renderer::NodeImageResourceDesc &desc, bool readWrite)
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

template <typename TEnum, std::size_t Count, typename TName> [[nodiscard]] bool enumCombo(nr::renderer::NodeUiWriter &ui, std::string_view label, TEnum &value, const std::array<TEnum, Count> &values, TName &&name)
{
    auto changed = false;
    if (ui.beginCombo(label, name(value)))
    {
        std::ranges::for_each(values, [&](TEnum candidate) {
            if (ui.selectable(name(candidate), candidate == value))
            {
                value = candidate;
                changed = true;
            }
        });
        ui.endCombo();
    }
    return changed;
}
} // namespace nr::renderPasses::detail

namespace nr::renderPasses
{
DlssRayReconstructionNodeInput makeDefaultDlssRayReconstructionNodeInput()
{
    auto result = DlssRayReconstructionNodeInput{};
    std::ranges::fill(result.create.presets, nr::rhi::DlssRayReconstructionPreset::Default);

    auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        result.resourceKeys[index] = std::format("dlss.rr.input.{}", nr::rhi::dlssResourceSlotName(slot));
        result.includeResources[index] = dlssRayReconstructionResourceRequired(slot, result.create.roughnessMode, result.create.flags.alphaUpscaling);
    });
    result.includeResources[detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance)] = true;
    return result;
}

bool dlssRayReconstructionResourceRequired(nr::rhi::DlssRayReconstructionResourceSlot slot, nr::rhi::DlssRoughnessMode roughnessMode, bool alphaUpscaling) noexcept
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

DlssRayReconstructionNode::DlssRayReconstructionNode() : input(makeDefaultDlssRayReconstructionNodeInput()), uiDraft_(input)
{
}

DlssRayReconstructionNode::~DlssRayReconstructionNode() = default;

void DlssRayReconstructionNode::initialize(NodeInitContext &context)
{
    device_ = context.device;
    runtime_ = std::make_shared<detail::DlssRayReconstructionRuntime>();
    runtime_->motionVectorDebugPipeline = detail::createMotionVectorDebugPipeline(context.device.get());
    runtime_->status = nr::rhi::dlssSdkCompiled() ? "NGX bridge loaded; enable the node to initialize NGX and query capability." : "NGX bridge unavailable; execution will fail fast.";
}

void DlssRayReconstructionNode::stageUiDraft()
{
    pendingInput_ = uiDraft_;
}

void DlssRayReconstructionNode::collectUi(NodeUiBuildContext &context, const NodeFrameParameters &)
{
    if (pendingInput_.has_value())
    {
        input = std::move(*pendingInput_);
        pendingInput_.reset();
    }
    if (pendingOneShotReset_)
    {
        consumeOneShotReset_ = true;
        pendingOneShotReset_ = false;
    }
    uiDraft_ = input;

    context.addSection(
        "DLSS Ray Reconstruction",
        [this, runtime = runtime_](NodeUiWriter &ui) {
            auto changed = false;
            changed |= ui.checkbox("Enable", uiDraft_.enabled);
            changed |= ui.checkbox("Bypass Output (show PathTracing color)", uiDraft_.bypass);

            constexpr auto qualities = std::array{
                nr::rhi::DlssQuality::Performance, nr::rhi::DlssQuality::Balanced, nr::rhi::DlssQuality::Quality, nr::rhi::DlssQuality::UltraPerformance, nr::rhi::DlssQuality::Dlaa,
            };
            changed |= detail::enumCombo(ui, "Quality", uiDraft_.create.quality, qualities, nr::rhi::dlssQualityName);

            constexpr auto presets = std::array{
                nr::rhi::DlssRayReconstructionPreset::Default,
                nr::rhi::DlssRayReconstructionPreset::D,
                nr::rhi::DlssRayReconstructionPreset::E,
            };
            std::ranges::for_each(qualities, [&](nr::rhi::DlssQuality quality) {
                auto const index = static_cast<std::size_t>(quality);
                auto label = std::format("{} Preset", nr::rhi::dlssQualityName(quality));
                changed |= detail::enumCombo(ui, label, uiDraft_.create.presets[index], presets, nr::rhi::dlssPresetName);
            });
            ui.text("Preset E is required when the Depth of Field guide is included.");

            constexpr auto roughnessModes = std::array{
                nr::rhi::DlssRoughnessMode::Packed,
                nr::rhi::DlssRoughnessMode::Unpacked,
            };
            changed |= detail::enumCombo(ui, "Roughness", uiDraft_.create.roughnessMode, roughnessModes, [](nr::rhi::DlssRoughnessMode value) { return value == nr::rhi::DlssRoughnessMode::Packed ? std::string_view{"Packed in normals.w"} : std::string_view{"Unpacked texture"}; });
            constexpr auto depthTypes = std::array{
                nr::rhi::DlssDepthType::Linear,
                nr::rhi::DlssDepthType::Hardware,
            };
            changed |= detail::enumCombo(ui, "Depth Type", uiDraft_.create.depthType, depthTypes, [](nr::rhi::DlssDepthType value) { return value == nr::rhi::DlssDepthType::Linear ? std::string_view{"Linear"} : std::string_view{"Hardware"}; });

            changed |= ui.checkbox("Override Render Size", uiDraft_.overrideRenderSize);
            if (uiDraft_.overrideRenderSize)
            {
                changed |= ui.inputUInt("Render Width", uiDraft_.renderSizeOverride.width, 1u, 16384u);
                changed |= ui.inputUInt("Render Height", uiDraft_.renderSizeOverride.height, 1u, 16384u);
            }
            changed |= ui.checkbox("Override Target Size", uiDraft_.overrideTargetSize);
            if (uiDraft_.overrideTargetSize)
            {
                changed |= ui.inputUInt("Target Width", uiDraft_.targetSizeOverride.width, 1u, 16384u);
                changed |= ui.inputUInt("Target Height", uiDraft_.targetSizeOverride.height, 1u, 16384u);
            }
            changed |= ui.checkbox("Automatic Jitter", uiDraft_.evaluate.automaticJitter);
            if (!uiDraft_.evaluate.automaticJitter)
            {
                changed |= ui.inputFloat("Jitter X", uiDraft_.evaluate.manualJitter[0], -0.5f, 0.5f);
                changed |= ui.inputFloat("Jitter Y", uiDraft_.evaluate.manualJitter[1], -0.5f, 0.5f);
            }
            changed |= ui.inputFloat("MV Scale X", uiDraft_.evaluate.motionVectorScale[0], -16.0f, 16.0f);
            changed |= ui.inputFloat("MV Scale Y", uiDraft_.evaluate.motionVectorScale[1], -16.0f, 16.0f);
            changed |= ui.checkbox("Visualize Motion Vectors", uiDraft_.evaluate.visualizeMotionVectors);
            if (uiDraft_.evaluate.visualizeMotionVectors)
            {
                ui.text("MV debug: neutral=(0.5, 0.5), R=X, G=Y; pixel motion is logarithmically amplified.");
            }
            changed |= ui.inputFloat("Pre Exposure", uiDraft_.evaluate.preExposure, 0.0001f, 65536.0f);
            changed |= ui.inputFloat("Exposure Scale", uiDraft_.evaluate.exposureScale, 0.0001f, 65536.0f);
            changed |= ui.checkbox("Automatic Camera Matrices", uiDraft_.evaluate.automaticMatrices);
            if (!uiDraft_.evaluate.automaticMatrices)
            {
                ui.text("Manual row-major matrices are supplied through DlssRayReconstructionNodeInput.");
            }
            changed |= ui.checkbox("Automatic Frame Delta", uiDraft_.evaluate.automaticFrameTimeDelta);
            if (!uiDraft_.evaluate.automaticFrameTimeDelta)
            {
                changed |= ui.inputFloat("Frame Delta (ms)", uiDraft_.evaluate.manualFrameTimeDeltaMilliseconds, 0.01f, 1000.0f);
            }
            changed |= ui.checkbox("Indicator Invert X", uiDraft_.evaluate.indicatorInvertXAxis);
            changed |= ui.checkbox("Indicator Invert Y", uiDraft_.evaluate.indicatorInvertYAxis);

            constexpr auto toneMappers = std::array{
                nr::rhi::DlssToneMapper::String,
                nr::rhi::DlssToneMapper::Reinhard,
                nr::rhi::DlssToneMapper::OneOverLuma,
                nr::rhi::DlssToneMapper::Aces,
            };
            changed |= detail::enumCombo(ui, "Tone Mapper", uiDraft_.evaluate.toneMapper, toneMappers, [](nr::rhi::DlssToneMapper value) {
                constexpr auto names = std::array{
                    std::string_view{"String"},
                    std::string_view{"Reinhard"},
                    std::string_view{"One Over Luma"},
                    std::string_view{"ACES"},
                };
                return names[static_cast<std::size_t>(value)];
            });
            if (ui.button("Reset History Next Frame"))
            {
                pendingOneShotReset_ = true;
            }

            ui.separator();
            ui.text("Flags");
            changed |= ui.checkbox("HDR (required by RR)", uiDraft_.create.flags.hdr);
            changed |= ui.checkbox("Low-resolution Motion Vectors", uiDraft_.create.flags.motionVectorsLowResolution);
            changed |= ui.checkbox("Motion Vectors Include Jitter", uiDraft_.create.flags.motionVectorsJittered);
            changed |= ui.checkbox("Depth Inverted", uiDraft_.create.flags.depthInverted);
            changed |= ui.checkbox("Auto Exposure (RR ignores/unsupported)", uiDraft_.create.flags.autoExposure);
            changed |= ui.checkbox("Alpha Upscaling", uiDraft_.create.flags.alphaUpscaling);
            ui.text("Sharpening and reserved SDK flags are intentionally not exposed.");

            ui.separator();
            ui.text("Status");
            ui.text(nr::rhi::dlssSdkCompiled() ? "NGX bridge: loaded" : "NGX bridge: unavailable (execution will fail fast)");
            {
                std::scoped_lock lock(runtime->mutex);
                ui.text(runtime->status);
                if (runtime->optimalSettingsQueried && runtime->optimalSettings.status.success())
                {
                    ui.text(std::format("Optimal render {}x{}; range {}x{}..{}x{}", runtime->optimalSettings.optimalRenderSize.width, runtime->optimalSettings.optimalRenderSize.height, runtime->optimalSettings.minimumRenderSize.width, runtime->optimalSettings.minimumRenderSize.height,
                                        runtime->optimalSettings.maximumRenderSize.width, runtime->optimalSettings.maximumRenderSize.height));
                }
            }

            if (changed)
            {
                stageUiDraft();
            }
        },
        true, "dlss-rr");
}

void DlssRayReconstructionNode::build(NodeBuildContext &context, const NodeFrameParameters &frameParameters)
{
    nrAssert(static_cast<bool>(runtime_), "DLSS RR build requires initialized runtime state.");
    nrAssert(device_.has_value(), "DLSS RR build requires a device reference.");
    if (!input.enabled)
    {
        previousBuildTime_ = {};
        std::scoped_lock lock(runtime_->mutex);
        runtime_->resetNextEvaluation = true;
        return;
    }

    nrAssert(nr::rhi::dlssSdkCompiled(), "DLSS RR was enabled, but the validated NGX bridge could not be loaded.");
    nrAssert(input.create.flags.hdr, "DLSS RR requires the HDR feature flag.");
    nrAssert(!input.outputColorKey.empty(), "DLSS RR output color key must not be empty.");
    nrAssert(input.outputColorFormat != vk::Format::eUndefined, "DLSS RR output color format must not be undefined.");
    if (input.create.flags.alphaUpscaling)
    {
        nrAssert(!input.outputAlphaKey.empty(), "DLSS RR alpha output key must not be empty when alpha upscaling is enabled.");
        nrAssert(input.outputAlphaFormat != vk::Format::eUndefined, "DLSS RR output alpha format must not be undefined when alpha upscaling is enabled.");
        nrAssert(input.outputAlphaKey != input.outputColorKey, "DLSS RR alpha and color output keys must be different.");
    }

    auto handles = std::array<nr::renderer::GraphResourceHandle, nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto descriptions = std::array<std::optional<nr::renderer::NodeImageResourceDesc>, nr::rhi::kDlssRayReconstructionResourceSlotCount>{};
    auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        if (slot == nr::rhi::DlssRayReconstructionResourceSlot::Output || slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha)
        {
            return;
        }
        auto const required = dlssRayReconstructionResourceRequired(slot, input.create.roughnessMode, input.create.flags.alphaUpscaling);
        if (!required && !input.includeResources[index])
        {
            return;
        }
        nrAssert(!input.resourceKeys[index].empty(), std::format("DLSS RR resource '{}' has an empty frame-resource key.", nr::rhi::dlssResourceSlotName(slot)));
        handles[index] = context.requireFrameResource(input.resourceKeys[index], "DlssRayReconstruction");
        descriptions[index] = context.describeImageResource(handles[index]);
        nrAssert(descriptions[index].has_value(), std::format("DLSS RR resource '{}' is not an image.", input.resourceKeys[index]));
    });

    auto const colorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Color);
    nrAssert(descriptions[colorIndex].has_value(), "DLSS RR requires the noisy input color image.");
    auto createDesc = input.create;
    createDesc.renderSize = input.overrideRenderSize ? input.renderSizeOverride : nr::rhi::DlssDimensions{descriptions[colorIndex]->extent.width, descriptions[colorIndex]->extent.height};
    createDesc.targetSize = input.overrideTargetSize ? input.targetSizeOverride : nr::rhi::DlssDimensions{frameParameters.swapchainExtent.width, frameParameters.swapchainExtent.height};
    nrAssert(createDesc.renderSize.valid() && createDesc.targetSize.valid(), "DLSS RR requires non-zero render and target sizes.");
    nrAssert(createDesc.quality != nr::rhi::DlssQuality::Count, "DLSS RR quality value is invalid.");
    if (createDesc.quality == nr::rhi::DlssQuality::Dlaa)
    {
        nrAssert(createDesc.renderSize == createDesc.targetSize, "DLSS RR DLAA requires render size to equal target size.");
    }
    auto const depthOfFieldGuideIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::DepthOfFieldGuide);
    if (handles[depthOfFieldGuideIndex].valid())
    {
        auto const activePreset = createDesc.presets[static_cast<std::size_t>(createDesc.quality)];
        nrAssert(activePreset == nr::rhi::DlssRayReconstructionPreset::E, "DLSS RR requires Preset E for the active quality mode when the Depth of Field guide is included.");
    }

    auto const reflectionMvIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::ReflectionMotionVectors);
    auto const gBufferSpecularMvIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::GBufferSpecularMotionVectors);
    auto const specularHitIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::SpecularHitDistance);
    auto const hasReflectionMotionVectors = handles[reflectionMvIndex].valid() || handles[gBufferSpecularMvIndex].valid();
    auto const hasSpecularHitDistance = handles[specularHitIndex].valid();
    auto const hasAutomaticMatrices = input.evaluate.automaticMatrices;
    auto const hasManualMatrices = input.evaluate.worldToViewRowMajor.has_value() && input.evaluate.viewToClipRowMajor.has_value();
    nrAssert(hasReflectionMotionVectors || (hasSpecularHitDistance && (hasAutomaticMatrices || hasManualMatrices)), "DLSS RR requires reflection motion vectors, or specular hit distance plus world-to-view and view-to-clip matrices.");

    auto const outputColorExtent = detail::outputExtent(createDesc.targetSize, input.evaluate.subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output)], createDesc.enableOutputSubrects, "color");
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
    descriptions[detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::Output)] = nr::renderer::NodeImageResourceDesc{
        .debugName = "DLSS.RR.OutputColor",
        .extent = outputColorExtent,
        .format = input.outputColorFormat,
    };

    auto outputAlpha = nr::renderer::GraphResourceHandle{};
    if (createDesc.flags.alphaUpscaling)
    {
        auto const outputAlphaExtent = detail::outputExtent(createDesc.targetSize, input.evaluate.subrectBases[detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::OutputAlpha)], createDesc.enableOutputSubrects, "alpha");
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
    evalDesc.reset = consumeOneShotReset_ || context.globalResources.get().cameraFrameState.reset;
    consumeOneShotReset_ = false;
    auto const cameraJitter = context.globalResources.get().cameraFrameState.jitter.pixelOffset;
    evalDesc.jitterOffset = input.evaluate.automaticJitter
                                ? std::array{cameraJitter.x, cameraJitter.y}
                                : input.evaluate.manualJitter;
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
    auto measuredDelta = previousBuildTime_ == std::chrono::steady_clock::time_point{} ? 1000.0f / 60.0f : std::chrono::duration<float, std::milli>(now - previousBuildTime_).count();
    previousBuildTime_ = now;
    evalDesc.frameTimeDeltaMilliseconds = input.evaluate.automaticFrameTimeDelta ? std::clamp(measuredDelta, 0.01f, 1000.0f) : input.evaluate.manualFrameTimeDeltaMilliseconds;

    auto intents = std::vector<nr::renderer::PassResourceUseDesc>{};
    intents.reserve(nr::rhi::kDlssRayReconstructionResourceSlotCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        if (!handles[index].valid())
            return;
        auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
        if (slot == nr::rhi::DlssRayReconstructionResourceSlot::Output || slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha)
        {
            intents.push_back(nr::renderer::use::storageWrite(handles[index]));
            return;
        }
        intents.push_back(nr::renderer::use::sampledRead(handles[index], descriptions[index]->aspect));
    });

    auto prepare = [runtime = runtime_, createDesc](const nr::renderer::PassPrepareContext &prepareContext) {
        nrAssert(prepareContext.device.has_value(), "DLSS RR prepare requires the active RHI device.");
        std::scoped_lock lock(runtime->mutex);
        auto &device = prepareContext.device->get();
        if (!runtime->feature || runtime->activeCreateDesc != createDesc)
        {
            auto context = device.dlssContext();
            runtime->optimalSettings = context->optimalSettings(createDesc.targetSize, createDesc.quality);
            runtime->optimalSettingsQueried = true;
            auto replacement = device.createDlssRayReconstructionFeature(createDesc);
            runtime->feature = std::move(replacement);
            runtime->activeCreateDesc = createDesc;
            runtime->resetNextEvaluation = true;
            auto const capabilityText = runtime->optimalSettings.status.success() ? std::string{"capability/optimal-settings query succeeded"} : std::format("optimal-settings query failed: {}", runtime->optimalSettings.status.message);
            runtime->status = std::format("Ready: render {}x{}, target {}x{}, quality {}; {}.", createDesc.renderSize.width, createDesc.renderSize.height, createDesc.targetSize.width, createDesc.targetSize.height, nr::rhi::dlssQualityName(createDesc.quality), capabilityText);
        }
    };

    auto record = [runtime = runtime_, handles, descriptions, evalDesc](const nr::renderer::PassRecordContext &recordContext) mutable {
        nrAssert(recordContext.commandBuffer.has_value(), "DLSS RR record requires a command buffer.");
        nrAssert(static_cast<bool>(recordContext.resolveImage), "DLSS RR record requires the graph image resolver.");
        auto indices = std::views::iota(std::size_t{0u}, nr::rhi::kDlssRayReconstructionResourceSlotCount);
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!handles[index].valid())
                return;
            auto resolved = recordContext.resolveImage(handles[index]);
            nrAssert(resolved.has_value(), std::format("DLSS RR failed to resolve image slot {}.", index));
            nrAssert(descriptions[index].has_value(), "DLSS RR image format snapshot is missing.");
            auto const slot = static_cast<nr::rhi::DlssRayReconstructionResourceSlot>(index);
            auto const readWrite = slot == nr::rhi::DlssRayReconstructionResourceSlot::Output || slot == nr::rhi::DlssRayReconstructionResourceSlot::OutputAlpha;
            evalDesc.resources[index] = detail::makeDlssImage(*resolved, *descriptions[index], readWrite);
        });

        std::scoped_lock lock(runtime->mutex);
        nrAssert(runtime->feature && runtime->feature->valid(), "DLSS RR record requires a prepared feature.");
        auto const resetForFeatureLifecycle = std::exchange(runtime->resetNextEvaluation, false);
        evalDesc.reset = evalDesc.reset || resetForFeatureLifecycle;
        auto const status = runtime->feature->evaluate(recordContext.commandBuffer->get(), evalDesc);
        nrAssert(status.success(), std::format("DLSS RR evaluation failed: {}", status.message));
    };

    [[maybe_unused]] auto pass = context.addPass(intents, "DLSS.RayReconstruction", std::move(record), std::move(prepare), false, vk::PipelineStageFlagBits2::eComputeShader);

    if (input.evaluate.visualizeMotionVectors)
    {
        auto const motionVectorIndex = detail::slotIndex(nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors);
        auto const motionVectorSubrectIndex = detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::MotionVectors);
        auto const outputSubrectIndex = detail::subrectSlotIndex(nr::rhi::DlssRayReconstructionSubrectSlot::Output);
        auto const motionVectorSize = detail::requiredSubrectDimensions(
            nr::rhi::DlssRayReconstructionResourceSlot::MotionVectors,
            createDesc);
        auto const outputBase = createDesc.enableOutputSubrects
                                    ? input.evaluate.subrectBases[outputSubrectIndex]
                                    : nr::rhi::DlssCoordinates{};
        auto const motionVectorBase = input.evaluate.subrectBases[motionVectorSubrectIndex];
        auto const pushConstants = detail::DlssMotionVectorDebugPushConstants{
            .outputBase = {outputBase.x, outputBase.y},
            .outputSize = {createDesc.targetSize.width, createDesc.targetSize.height},
            .motionVectorBase = {motionVectorBase.x, motionVectorBase.y},
            .motionVectorSize = {motionVectorSize.width, motionVectorSize.height},
            .motionVectorScale = input.evaluate.motionVectorScale,
        };

        auto debugPass = nr::renderer::ComputePassBuilder{
            context,
            "DLSS.RayReconstruction.VisualizeMotionVectors",
            runtime_->motionVectorDebugPipeline};
        debugPass
            .sampledImage("gMotionVectors", handles[motionVectorIndex], "DLSS.RR.MotionVectors")
            .storageImage("gMotionVectorVisualization", outputColor, "DLSS.RR.MotionVectorVisualization")
            .pushConstants("gMotionVectorDebug", pushConstants)
            .record([outputColorExtent](const nr::renderer::ComputePassRecordContext& computeContext) {
                constexpr auto threadGroupSize = 8u;
                computeContext.commandBuffer.dispatch(
                    detail::dlssMotionVectorDebugDivideRoundUp(outputColorExtent.width, threadGroupSize),
                    detail::dlssMotionVectorDebugDivideRoundUp(outputColorExtent.height, threadGroupSize),
                    1u);
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
    device_.reset();
}
} // namespace nr::renderPasses
