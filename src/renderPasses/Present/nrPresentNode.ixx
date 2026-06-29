export module nr.renderPasses:presentNode;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct PresentRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct PresentNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    bool flipY = false;
    float uiOpacity = 1.0f;
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
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::PresentRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
