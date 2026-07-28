export module nr.options:registration;

export import :model;
import std;

export namespace nr::options
{
enum class CatalogIssueCode : std::uint8_t
{
    invalidId,
    duplicateId,
    invalidDefault,
    invalidDefinition,
    snapshotTooLarge,
};

struct CatalogIssue
{
    CatalogIssueCode code = CatalogIssueCode::invalidDefinition;
    std::optional<OptionId> id{};
    std::string detail{};
};

struct CatalogBuildResult
{
    std::shared_ptr<const OptionCatalog> catalog{};
    std::vector<CatalogIssue> issues{};

    [[nodiscard]] bool valid() const noexcept
    {
        return catalog != nullptr && issues.empty();
    }
};

class OptionCatalogBuilder
{
  public:
    [[nodiscard]] bool add(OptionDefinition definition);
    [[nodiscard]] CatalogBuildResult build(std::size_t maximumSnapshotBytes = 256u * 1024u) const;

  private:
    OptionCatalog::DefinitionMap definitions_{};
    std::vector<CatalogIssue> issues_{};
};

template <WireValueAlternative T> [[nodiscard]] OptionId optionId(OptionKey<T> key)
{
    return *OptionId::parse(key.id());
}

template <WireValueAlternative T> [[nodiscard]] OptionDefinition makeDefinition(OptionKey<T> key, OptionSchema schema, T defaultValue, OptionScope scope, OptionPresentation presentation = {}, OptionAdmissionValidator validator = {})
{
    return OptionDefinition{
        .id = optionId(key),
        .schema = std::move(schema),
        .defaultValue = OptionWireValue{std::move(defaultValue)},
        .scope = scope,
        .presentation = std::move(presentation),
        .admissionValidator = std::move(validator),
    };
}

[[nodiscard]] OptionDefinition makeBooleanDefinition(OptionKey<bool> key, bool defaultValue, OptionScope scope, OptionPresentation presentation = {});
[[nodiscard]] OptionDefinition makeUnsignedDefinition(OptionKey<std::uint64_t> key, std::uint64_t defaultValue, std::uint64_t minimum, std::uint64_t maximum, OptionScope scope, OptionPresentation presentation = {});
[[nodiscard]] OptionDefinition makeNumberDefinition(OptionKey<double> key, double defaultValue, double minimum, double maximum, OptionScope scope, OptionPresentation presentation = {});
[[nodiscard]] OptionDefinition makeEnumDefinition(OptionKey<std::string> key, std::string defaultValue, std::vector<std::string> allowed, OptionScope scope, OptionPresentation presentation = {}, OptionAdmissionValidator validator = {});
[[nodiscard]] OptionDefinition makeEmptyEffectDefinition(OptionKey<OptionWireValue::Object> key, OptionScope scope, OptionPresentation presentation = {});

namespace keys
{
inline constexpr auto viewerPipelineSelected = OptionKey<std::string>{"viewer.pipeline.selected"};
inline constexpr auto viewerModelSource = OptionKey<std::string>{"viewer.model.source"};
inline constexpr auto viewerEnvironmentSource = OptionKey<std::string>{"viewer.environment.source"};
inline constexpr auto viewerRtPostProcessingMode = OptionKey<std::string>{"viewer.rt.post_processing_mode"};
inline constexpr auto viewerWindowFullscreen = OptionKey<bool>{"viewer.window.fullscreen"};
inline constexpr auto viewerCameraPose = OptionKey<OptionWireValue::Object>{"viewer.camera.pose"};
inline constexpr auto viewerCameraVerticalFovDegrees = OptionKey<double>{"viewer.camera.vertical_fov_degrees"};
inline constexpr auto viewerCameraClipPlanes = OptionKey<OptionWireValue::Object>{"viewer.camera.clip_planes"};

inline constexpr auto pathTracingMaxSurfaceBounces = OptionKey<std::uint64_t>{"render.path_tracing.max_surface_bounces"};
inline constexpr auto pathTracingRussianRouletteEnabled = OptionKey<bool>{"render.path_tracing.russian_roulette_enabled"};
inline constexpr auto accumulateMaxHistorySamples = OptionKey<std::uint64_t>{"render.accumulate.max_history_samples"};
inline constexpr auto dlssEnabled = OptionKey<bool>{"render.dlss.enabled"};
inline constexpr auto dlssQuality = OptionKey<std::string>{"render.dlss.quality"};
inline constexpr auto dlssPresetPerformance = OptionKey<std::string>{"render.dlss.preset.performance"};
inline constexpr auto dlssPresetBalanced = OptionKey<std::string>{"render.dlss.preset.balanced"};
inline constexpr auto dlssPresetQuality = OptionKey<std::string>{"render.dlss.preset.quality"};
inline constexpr auto dlssPresetUltraPerformance = OptionKey<std::string>{"render.dlss.preset.ultra_performance"};
inline constexpr auto dlssPresetDlaa = OptionKey<std::string>{"render.dlss.preset.dlaa"};
inline constexpr auto dlssBypass = OptionKey<bool>{"render.dlss.bypass"};
inline constexpr auto dlssVisualizeMotionVectors = OptionKey<bool>{"render.dlss.visualize_motion_vectors"};
inline constexpr auto dlssResetHistory = OptionKey<OptionWireValue::Object>{"render.dlss.reset_history"};
inline constexpr auto presentToneMapping = OptionKey<std::string>{"render.present.tone_mapping"};
inline constexpr auto presentUiOpacity = OptionKey<double>{"render.present.ui_opacity"};
inline constexpr auto presentCaptureExr = OptionKey<OptionWireValue::Object>{"render.present.capture_exr"};
} // namespace keys

struct SessionDefinitionSeed
{
    std::vector<std::string> pipelineIds{"normalview", "rtobject"};
    std::string selectedPipeline = "normalview";
    std::string modelSource{};
    std::string environmentSource{};
    std::string postProcessingMode = "accumulate";
    bool fullscreen = false;
    OptionWireValue::Object cameraPose{
        {"position",
         OptionWireValue::Array{
             OptionWireValue{0.0},
             OptionWireValue{0.0},
             OptionWireValue{0.0},
         }},
        {"yaw_degrees", OptionWireValue{0.0}},
        {"pitch_degrees", OptionWireValue{0.0}},
    };
    double verticalFovDegrees = 60.0;
    OptionWireValue::Object clipPlanes{
        {"near", OptionWireValue{0.1}},
        {"far", OptionWireValue{1000.0}},
    };
};

[[nodiscard]] std::vector<OptionDefinition> makeSessionDefinitions(const SessionDefinitionSeed &seed);
[[nodiscard]] std::vector<OptionDefinition> makePathTracingDefinitions();
[[nodiscard]] std::vector<OptionDefinition> makeAccumulateDefinitions();
[[nodiscard]] std::vector<OptionDefinition> makeDlssDefinitions(std::string initialQuality = "quality");
[[nodiscard]] std::vector<OptionDefinition> makePresentDefinitions();
} // namespace nr::options
