export module nr.renderPasses:accumulateNode;
import dependency.vulkan;

import nr.options;
import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct AccumulateRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
inline constexpr std::uint32_t kAccumulateDefaultMaxHistorySampleCount = 1024u;
inline constexpr std::uint32_t kAccumulateMaxHistorySampleCount = 4096u;

struct AccumulateNodeInput
{
    vk::Format historyFormat = vk::Format::eR16G16B16A16Sfloat;
};

class AccumulateNode final : public Node
{
  public:
    AccumulateNode() = default;
    ~AccumulateNode() override;

    AccumulateNodeInput input{};

    [[nodiscard]] std::string_view actionableSemantic() const noexcept override
    {
        return "render.accumulate";
    }
    void declareOptions(nr::options::OptionCatalogBuilder& builder) const override;
    void collectOptionAvailability(
        const nr::options::OptionFrameSnapshot& snapshot,
        nr::options::OptionAvailabilityMap& availability) const override;
    [[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> shaderRequests() const override;
    void initialize(NodeInitContext& context) override;
    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override { return true; }
    [[nodiscard]] std::optional<StructuralSnapshot> structuralSnapshot(const NodeFrameParameters& frameParameters) const override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext& context, const NodeFrameParameters& frameParameters, const StructuralSnapshot& snapshot) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext& context, const NodeFrameParameters& frameParameters);
    std::shared_ptr<detail::AccumulateRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
