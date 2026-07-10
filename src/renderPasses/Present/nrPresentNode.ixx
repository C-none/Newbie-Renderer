export module nr.renderPasses:presentNode;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PresentRuntimeCache;

struct PresentScreenshotPendingSave
{
    vk::Extent2D extent{1u, 1u};
    vk::Format format = vk::Format::eUndefined;
    vk::DeviceSize byteSize = 0;
    std::filesystem::path path{};
    std::uint32_t frameSlot = 0;
    bool flipY = false;
};
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct PresentScreenshotConfig
{
    std::filesystem::path outputDirectory{"screenshots"};
    std::string filePrefix{"screenshot"};
};

struct PresentReadbackTarget
{
    std::reference_wrapper<const nr::rhi::Buffer> buffer;
    vk::DeviceSize offset = 0;
};

struct PresentNodeInput
{
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    bool flipY = false;
    float uiOpacity = 1.0f;
    std::optional<PresentReadbackTarget> readback{};
    PresentScreenshotConfig screenshot{};
};

class PresentNode final : public Node
{
  public:
    PresentNode() = default;
    ~PresentNode() override;

    PresentNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void processCompletedScreenshot(std::uint32_t frameSlot);
    void savePendingScreenshot();

    std::shared_ptr<detail::PresentRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
    nr::rhi::Buffer screenshotReadbackBuffer_{};
    std::optional<detail::PresentScreenshotPendingSave> screenshotPendingSave_{};
    std::uint32_t pendingScreenshotRequestCount_ = 0;
    std::uint32_t screenshotRequestCount_ = 0;
    std::uint64_t screenshotSequence_ = 0;
    float uiOpacityDraft_ = 1.0f;
    float pendingUiOpacity_ = 1.0f;
    bool pendingUiOpacityValid_ = false;
    std::uint32_t toneMappingSelection_ = 0u;
    std::uint32_t pendingToneMappingSelection_ = 0u;
    bool pendingToneMappingSelectionValid_ = false;
    std::string screenshotStatus_{};
};
} // namespace nr::renderPasses
