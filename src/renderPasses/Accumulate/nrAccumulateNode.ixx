export module nr.renderPasses:accumulateNode;
import dependency.vulkan;

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
struct AccumulateNodeInput
{
    vk::Extent2D viewportExtent{1u, 1u};
    vk::Format historyFormat = vk::Format::eR16G16B16A16Sfloat;
    std::uint32_t maxHistorySampleCount = nr::renderer::kRendererAccumulationMaxSampleCount;
};

class AccumulateNode final : public Node
{
  public:
    AccumulateNode() = default;
    ~AccumulateNode() override;

    AccumulateNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::AccumulateRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
