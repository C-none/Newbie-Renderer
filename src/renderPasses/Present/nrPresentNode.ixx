export module nr.renderPasses:presentNode;
import dependency.vulkan;

import nr.options;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PresentRuntimeState;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct PresentScreenshotConfig
{
    std::filesystem::path outputDirectory{std::filesystem::path{std::string{nr::projectRoot}} / "screenshots"};
    std::string sessionId{"session"};
};

struct PresentReadbackTarget
{
    std::reference_wrapper<const nr::rhi::Buffer> buffer;
    vk::DeviceSize offset = 0;
};

struct PresentNodeInput
{
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    std::optional<PresentReadbackTarget> readback{};
    PresentScreenshotConfig screenshot{};
};

class PresentNode final : public Node
{
  public:
    PresentNode();
    ~PresentNode() override;

    PresentNodeInput input{};

    [[nodiscard]] std::string_view actionableSemantic() const noexcept override
    {
        return "render.present";
    }
    void declareOptions(nr::options::OptionCatalogBuilder &builder) const override;
    void collectOptionAvailability(const nr::options::OptionFrameSnapshot &snapshot,
                                   nr::options::OptionAvailabilityMap &availability) const override;
    [[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> shaderRequests() const override;
    void initialize(NodeInitContext &context) override;

    void finalizeInitialization() override;
    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override
    {
        return true;
    }
    [[nodiscard]] std::optional<StructuralSnapshot> structuralSnapshot(
        const NodeFrameParameters &frameParameters) const override;
    void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) override;
    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext &context,
                                        const NodeFrameParameters &frameParameters,
                                        const StructuralSnapshot &snapshot) override;
    void advanceContinuations(std::uint32_t frameSlot) override;
    void flushContinuations() override;
    [[nodiscard]] nr::renderer::FrameEffectFinalizeDisposition finalizeFrameEffect(
        const nr::options::FrameEffect &effect, bool targetBatchSubmitted, std::uint32_t frameSlot) override;
    void shutdown(NodeShutdownContext &context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters);
    void processCompletedScreenshot(std::uint32_t frameSlot);
    void savePendingScreenshot();

    std::unique_ptr<detail::PresentRuntimeState> runtime_{};
};
} // namespace nr::renderPasses
