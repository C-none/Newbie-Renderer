export module nr.renderPasses:dlssRayReconstruction;

import dependency.math;
import dependency.vulkan;
import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct DlssRayReconstructionRuntime;
struct DlssRayReconstructionResolutionControllerImpl;
}

export namespace nr::renderPasses::detail
{
[[nodiscard]] std::array<float, 16u> toDlssRowVectorMatrix(const glm::mat4& value) noexcept;
}

export namespace nr::renderPasses
{
inline constexpr std::string_view kDlssRayReconstructionOutputColorKey = "dlss.rr.output.color";
inline constexpr std::string_view kDlssRayReconstructionOutputAlphaKey = "dlss.rr.output.alpha";

struct DlssRayReconstructionEvalConfig
{
    bool automaticJitter = true;
    std::array<float, 2u> manualJitter{};
    std::array<float, 2u> motionVectorScale{1.0f, 1.0f};
    bool visualizeMotionVectors = false;
    float preExposure = 1.0f;
    float exposureScale = 1.0f;
    bool indicatorInvertXAxis = false;
    bool indicatorInvertYAxis = false;
    bool automaticMatrices = true;
    std::optional<std::array<float, 16u>> worldToViewRowMajor{};
    std::optional<std::array<float, 16u>> viewToClipRowMajor{};
    nr::rhi::DlssToneMapper toneMapper = nr::rhi::DlssToneMapper::Aces;
    bool automaticFrameTimeDelta = true;
    float manualFrameTimeDeltaMilliseconds = 1000.0f / 60.0f;
    std::array<nr::rhi::DlssCoordinates, nr::rhi::kDlssRayReconstructionSubrectSlotCount> subrectBases{};
};

struct DlssRayReconstructionNodeInput
{
    bool enabled = false;
    bool bypass = false;
    bool overrideRenderSize = false;
    nr::rhi::DlssDimensions renderSizeOverride{};
    bool overrideTargetSize = false;
    nr::rhi::DlssDimensions targetSizeOverride{};
    nr::rhi::DlssRayReconstructionCreateDesc create{};
    DlssRayReconstructionEvalConfig evaluate{};
    std::array<bool, nr::rhi::kDlssRayReconstructionResourceSlotCount> includeResources{};
    std::array<std::string, nr::rhi::kDlssRayReconstructionResourceSlotCount> resourceKeys{};
    vk::Format outputColorFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format outputAlphaFormat = vk::Format::eR16Sfloat;
    std::string outputColorKey{std::string{kDlssRayReconstructionOutputColorKey}};
    std::string outputAlphaKey{std::string{kDlssRayReconstructionOutputAlphaKey}};
};

struct DlssRayReconstructionResolutionRequest
{
    bool enabled = false;
    nr::rhi::DlssQuality quality = nr::rhi::DlssQuality::Quality;
    bool bypass = false;

    [[nodiscard]] friend bool operator==(const DlssRayReconstructionResolutionRequest&, const DlssRayReconstructionResolutionRequest&) noexcept = default;
};

struct DlssRayReconstructionResolutionSnapshot
{
    DlssRayReconstructionResolutionRequest request{};
    nr::renderer::FrameResolutionPlan plan{};
    std::optional<nr::rhi::DlssOptimalSettings> optimalSettings{};
};

class DlssRayReconstructionResolutionController final
{
  public:
    using OptimalSettingsQuery = std::function<nr::rhi::DlssOptimalSettings(nr::rhi::DlssDimensions, nr::rhi::DlssQuality)>;

    DlssRayReconstructionResolutionController();
    ~DlssRayReconstructionResolutionController();

    DlssRayReconstructionResolutionController(const DlssRayReconstructionResolutionController&) = delete;
    DlssRayReconstructionResolutionController& operator=(const DlssRayReconstructionResolutionController&) = delete;
    DlssRayReconstructionResolutionController(DlssRayReconstructionResolutionController&&) noexcept;
    DlssRayReconstructionResolutionController& operator=(DlssRayReconstructionResolutionController&&) noexcept;

    [[nodiscard]] nr::renderer::FrameResolutionPlan resolve(
        DlssRayReconstructionResolutionRequest request,
        vk::Extent2D displayExtent,
        const OptimalSettingsQuery& optimalSettingsQuery);

    [[nodiscard]] std::optional<DlssRayReconstructionResolutionSnapshot> snapshot() const;

  private:
    std::unique_ptr<detail::DlssRayReconstructionResolutionControllerImpl> impl_{};
};

[[nodiscard]] DlssRayReconstructionNodeInput makeDefaultDlssRayReconstructionNodeInput();

[[nodiscard]] bool dlssRayReconstructionResourceRequired(
    nr::rhi::DlssRayReconstructionResourceSlot slot,
    nr::rhi::DlssRoughnessMode roughnessMode,
    bool alphaUpscaling) noexcept;

class DlssRayReconstructionNode final : public Node
{
  public:
    DlssRayReconstructionNode();
    ~DlssRayReconstructionNode() override;

    DlssRayReconstructionNodeInput input{};

    void setResolutionController(const std::shared_ptr<DlssRayReconstructionResolutionController>& controller) noexcept;
    [[nodiscard]] DlssRayReconstructionResolutionRequest effectiveResolutionRequest() const noexcept;

    void initialize(NodeInitContext& context) override;
    void collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void stageUiDraft();

    std::shared_ptr<detail::DlssRayReconstructionRuntime> runtime_{};
    std::weak_ptr<DlssRayReconstructionResolutionController> resolutionController_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
    DlssRayReconstructionNodeInput uiDraft_{};
    std::optional<DlssRayReconstructionNodeInput> pendingInput_{};
    bool pendingOneShotReset_ = false;
    bool consumeOneShotReset_ = false;
    std::chrono::steady_clock::time_point previousBuildTime_{};
};
} // namespace nr::renderPasses
