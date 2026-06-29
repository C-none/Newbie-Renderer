export module nr.renderPasses:uiNode;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct UiRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct UiNodeInput
{
    vk::Format bufferFormat = vk::Format::eR8G8B8A8Unorm;
};

class UiNode final : public Node
{
  public:
    UiNodeInput input{};

    UiNode() = default;
    ~UiNode() override;

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::UiRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
