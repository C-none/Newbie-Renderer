export module nr.renderPasses:lightPrepare;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct LightPrepareRuntimeCache;
}

export namespace nr::renderPasses
{
struct LightPrepareNodeInput
{
    std::uint32_t initialLightCapacity = 64u;
};

class LightPrepareNode final : public Node
{
  public:
    LightPrepareNodeInput input{};

    ~LightPrepareNode() override;

    [[nodiscard]] NodeDescription describe() const override;

    void initialize(NodeInitContext& context) override;

    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;

    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::LightPrepareRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
