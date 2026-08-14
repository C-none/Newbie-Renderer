export module nr.renderPasses:accelerationStructureBuild;
import dependency.vulkan;

import nr.renderer;
import nr.rhi;
import nr.resource;
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

// This is the host-side half of the neural P0 contract. The shader repeats
// the runtime material-state check before evaluating the artifact.
[[nodiscard]] bool neuralMaterialP0Eligible(const nr::resource::Material &material) noexcept;

class AccelerationStructureBuildNode final : public Node
{
  public:
    AccelerationStructureBuildNode() = default;
    ~AccelerationStructureBuildNode() override;

    AccelerationStructureBuildNodeInput input{};

    void initialize(NodeInitContext &context) override;
    void build(NodeBuildContext &context, const NodeFrameParameters &frameParameters) override;
    void shutdown(NodeShutdownContext &context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext &context, const NodeFrameParameters &frameParameters);
    std::shared_ptr<detail::AccelerationStructureBuildRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
