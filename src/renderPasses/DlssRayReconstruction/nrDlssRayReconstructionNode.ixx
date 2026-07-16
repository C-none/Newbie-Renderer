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

    void initialize(NodeInitContext& context) override;
    void collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void stageUiDraft();

    std::shared_ptr<detail::DlssRayReconstructionRuntime> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
    DlssRayReconstructionNodeInput uiDraft_{};
    std::optional<DlssRayReconstructionNodeInput> pendingInput_{};
    bool pendingOneShotReset_ = false;
    bool consumeOneShotReset_ = false;
    std::chrono::steady_clock::time_point previousBuildTime_{};
};
} // namespace nr::renderPasses
