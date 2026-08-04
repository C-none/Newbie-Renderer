export module nr.renderPasses:embeddedTriangle;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct EmbeddedTriangleRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct EmbeddedTriangleNodeInput
{
    vk::Format colorFormat = vk::Format::eR8G8B8A8Unorm;
};

class EmbeddedTriangleNode final : public Node
{
  public:
    EmbeddedTriangleNode() = default;
    ~EmbeddedTriangleNode() override;

    EmbeddedTriangleNodeInput input{};

    [[nodiscard]] std::vector<nr::rhi::SlangProgramCompileFileRequest> shaderRequests() const override;
    void initialize(NodeInitContext &context) override;

    void finalizeInitialization() override;
    void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) override;
    void shutdown(NodeShutdownContext &context) override;

  private:
    std::shared_ptr<detail::EmbeddedTriangleRuntimeCache> runtime_{};
};
} // namespace nr::renderPasses
