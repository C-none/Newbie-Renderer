export module nr.renderPasses:displayNode;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct DisplayRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct DisplayNodeInput
{
    vk::Extent2D viewportExtent{1, 1};
    vk::Format format = vk::Format::eR8G8B8A8Unorm;
    bool flipY = false;
};

class DisplayNode final : public Node
{
  public:
    DisplayNode() = default;
    ~DisplayNode() override;

    DisplayNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::DisplayRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
