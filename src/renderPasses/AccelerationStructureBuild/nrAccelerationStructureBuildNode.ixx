export module nr.renderPasses:accelerationStructureBuild;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import std;
import :nodeType;

namespace nr::renderPasses::detail
{
struct AccelerationStructureBuildRuntimeCache;
} // namespace nr::renderPasses::detail

export namespace nr::renderPasses
{
struct AccelerationStructureBuildNodeInput
{
    std::uint64_t unusedFrameRetireLatency = 300;
};

class AccelerationStructureBuildNode final : public Node
{
  public:
    AccelerationStructureBuildNode() = default;
    ~AccelerationStructureBuildNode() override;

    AccelerationStructureBuildNodeInput input{};

    void initialize(NodeInitContext& context) override;
    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override { return true; }
    [[nodiscard]] std::optional<StructuralSnapshot> structuralSnapshot(const NodeFrameParameters& frameParameters) const override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext& context, const NodeFrameParameters& frameParameters, const StructuralSnapshot& snapshot) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext& context, const NodeFrameParameters& frameParameters);
    std::shared_ptr<detail::AccelerationStructureBuildRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
