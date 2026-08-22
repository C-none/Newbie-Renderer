export module nr.renderPasses:dlssRayReconstruction;

import dependency.dlss;
import dependency.math;
import dependency.vulkan;
import nr.options;
import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct DlssRayReconstructionRuntime;
struct DlssRayReconstructionResolutionControllerImpl;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses::detail
{
[[nodiscard]] std::array<float, 16u> toDlssRowVectorMatrix(const DirectX::XMFLOAT4X4 &value) noexcept;
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
    nr::dependency::dlss::ToneMapper toneMapper = nr::dependency::dlss::ToneMapper::Aces;
    bool automaticFrameTimeDelta = true;
    float manualFrameTimeDeltaMilliseconds = 1000.0f / 60.0f;
    std::array<nr::dependency::dlss::Coordinates, nr::dependency::dlss::rayReconstructionSubrectSlotCount> subrectBases{};
};

struct DlssRayReconstructionNodeInput
{
    bool enabled = false;
    bool bypass = false;
    bool overrideRenderSize = false;
    nr::dependency::dlss::Dimensions renderSizeOverride{};
    bool overrideTargetSize = false;
    nr::dependency::dlss::Dimensions targetSizeOverride{};
    nr::dependency::dlss::RayReconstructionCreateDesc create{};
    DlssRayReconstructionEvalConfig evaluate{};
    std::array<bool, nr::dependency::dlss::rayReconstructionResourceSlotCount> includeResources{};
    std::array<std::string, nr::dependency::dlss::rayReconstructionResourceSlotCount> resourceKeys{};
    vk::Format outputColorFormat = vk::Format::eR16G16B16A16Sfloat;
    vk::Format outputAlphaFormat = vk::Format::eR16Sfloat;
    std::string outputColorKey{std::string{kDlssRayReconstructionOutputColorKey}};
    std::string outputAlphaKey{std::string{kDlssRayReconstructionOutputAlphaKey}};
};

struct DlssRayReconstructionResolutionRequest
{
    bool enabled = false;
    nr::dependency::dlss::Quality quality = nr::dependency::dlss::Quality::Quality;
    bool bypass = false;

    [[nodiscard]] friend bool operator==(const DlssRayReconstructionResolutionRequest &,
                                         const DlssRayReconstructionResolutionRequest &) noexcept = default;
};

struct DlssRayReconstructionResolutionSnapshot
{
    DlssRayReconstructionResolutionRequest request{};
    vk::Extent2D displayExtent{1u, 1u};
    vk::Extent2D renderExtent{1u, 1u};
    std::optional<nr::dependency::dlss::OptimalSettings> optimalSettings{};
};

class DlssRayReconstructionResolutionController final
{
  public:
    using OptimalSettingsQuery =
        std::function<nr::dependency::dlss::OptimalSettings(nr::dependency::dlss::Dimensions, nr::dependency::dlss::Quality)>;

    DlssRayReconstructionResolutionController();
    ~DlssRayReconstructionResolutionController();

    DlssRayReconstructionResolutionController(const DlssRayReconstructionResolutionController &) = delete;
    DlssRayReconstructionResolutionController &operator=(const DlssRayReconstructionResolutionController &) = delete;
    DlssRayReconstructionResolutionController(DlssRayReconstructionResolutionController &&) noexcept;
    DlssRayReconstructionResolutionController &operator=(DlssRayReconstructionResolutionController &&) noexcept;

    [[nodiscard]] nr::renderer::FrameResolutionPlan resolve(DlssRayReconstructionResolutionRequest request,
                                                            vk::Extent2D displayExtent,
                                                            const OptimalSettingsQuery &optimalSettingsQuery);

    [[nodiscard]] std::optional<DlssRayReconstructionResolutionSnapshot> snapshot() const;

  private:
    std::unique_ptr<detail::DlssRayReconstructionResolutionControllerImpl> impl_{};
};

[[nodiscard]] DlssRayReconstructionNodeInput makeDefaultDlssRayReconstructionNodeInput();

[[nodiscard]] DlssRayReconstructionResolutionRequest dlssResolutionRequestFromSnapshot(
    const nr::options::OptionFrameSnapshot &snapshot);

[[nodiscard]] bool dlssRayReconstructionResourceRequired(nr::dependency::dlss::RayReconstructionResourceSlot slot,
                                                         nr::dependency::dlss::RoughnessMode roughnessMode,
                                                         bool alphaUpscaling) noexcept;

class DlssRayReconstructionNode final : public Node
{
  public:
    DlssRayReconstructionNode();
    ~DlssRayReconstructionNode() override;

    DlssRayReconstructionNodeInput input{};

    [[nodiscard]] std::string_view actionableSemantic() const noexcept override
    {
        return "render.dlss";
    }
    void declareOptions(nr::options::OptionCatalogBuilder &builder) const override;
    void collectOptionAvailability(const nr::options::OptionFrameSnapshot &snapshot,
                                   nr::options::OptionAvailabilityMap &availability) const override;
    void setResolutionController(const std::shared_ptr<DlssRayReconstructionResolutionController> &controller) noexcept;

    [[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> shaderRequests() const override;
    void initialize(NodeInitContext &context) override;

    void finalizeInitialization() override;
    void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) override;
    void shutdown(NodeShutdownContext &context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters);

    std::shared_ptr<detail::DlssRayReconstructionRuntime> runtime_{};
    std::weak_ptr<DlssRayReconstructionResolutionController> resolutionController_{};
    std::chrono::steady_clock::time_point previousBuildTime_{};
};
} // namespace nr::renderPasses
