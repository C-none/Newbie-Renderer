export module nr.renderPasses:rayTraceInstanceHash;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct RayTraceInstanceHashRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct RayTraceInstanceHashNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format outputFormat = vk::Format::eR16G16B16A16Sfloat;
};

class RayTraceInstanceHashNode final : public Node
{
  public:
    RayTraceInstanceHashNode() = default;
    ~RayTraceInstanceHashNode() override;

    RayTraceInstanceHashNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::RayTraceInstanceHashRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
