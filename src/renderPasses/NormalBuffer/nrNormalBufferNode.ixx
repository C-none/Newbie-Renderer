export module nr.renderPasses:normalBuffer;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct NormalBufferRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct NormalBufferNodeInput
{
    vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;
    vk::Format depthFormat = vk::Format::eD32Sfloat;
};

class NormalBufferNode final : public Node
{
  public:
    NormalBufferNode() = default;
    ~NormalBufferNode() override;

    NormalBufferNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::NormalBufferRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
