export module nr.renderPasses:pathTracing;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PathTracingRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct PathTracingNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format outputFormat = vk::Format::eR16G16B16A16Sfloat;
};

class PathTracingNode final : public Node
{
  public:
    PathTracingNode() = default;
    ~PathTracingNode() override;

    PathTracingNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::PathTracingRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
