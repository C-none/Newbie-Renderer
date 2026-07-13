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
inline constexpr std::uint32_t kAccumulateDefaultMaxHistorySampleCount = 1024u;
inline constexpr std::uint32_t kAccumulateMaxHistorySampleCount = nr::renderer::kRendererAccumulationMaxSampleCount;

struct AccumulateNodeInput
{
    vk::Format historyFormat = vk::Format::eR16G16B16A16Sfloat;
    std::uint32_t maxHistorySampleCount = kAccumulateDefaultMaxHistorySampleCount;
};

class AccumulateNode final : public Node
{
  public:
    AccumulateNode() = default;
    ~AccumulateNode() override;

    AccumulateNodeInput input{};

    void initialize(NodeInitContext& context) override;
    void collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::AccumulateRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
    std::uint32_t maxHistorySampleCountDraft_ = kAccumulateDefaultMaxHistorySampleCount;
    std::uint32_t pendingMaxHistorySampleCount_ = kAccumulateDefaultMaxHistorySampleCount;
    bool pendingMaxHistorySampleCountValid_ = false;
};
} // namespace nr::renderPasses
