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
    std::uint32_t hitShaderBindingTableRecordCount = 1;
    // Set to 0 to disable periodic AS build CPU profiling logs.
    std::uint32_t cpuProfileLogIntervalFrames = 120;
};

class AccelerationStructureBuildNode final : public Node
{
  public:
    AccelerationStructureBuildNode() = default;
    ~AccelerationStructureBuildNode() override;

    AccelerationStructureBuildNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::AccelerationStructureBuildRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
