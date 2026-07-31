export module nr.renderPasses:pathTracing;
import dependency.vulkan;

import nr.options;
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
inline constexpr std::uint32_t kPathTracingMinSurfaceBounces = 1u;
inline constexpr std::uint32_t kPathTracingDefaultMaxSurfaceBounces = 16u;
inline constexpr std::uint32_t kPathTracingMaxSurfaceBouncesLimit = 64u;

struct PathTracingVariantKey
{
    std::uint32_t maxSurfaceBounces = kPathTracingDefaultMaxSurfaceBounces;
    bool enableRussianRoulette = true;
    bool enableFilterAfterShading = false;

    [[nodiscard]] friend auto operator<=>(const PathTracingVariantKey &, const PathTracingVariantKey &) noexcept = default;
};

struct PathTracingNodeInput
{
    vk::Format outputFormat = vk::Format::eR16G16B16A16Sfloat;
};

class PathTracingNode final : public Node
{
  public:
    PathTracingNode() = default;
    ~PathTracingNode() override;

    PathTracingNodeInput input{};

    [[nodiscard]] std::string_view actionableSemantic() const noexcept override
    {
        return "render.path_tracing";
    }
    void declareOptions(nr::options::OptionCatalogBuilder& builder) const override;
    void collectOptionAvailability(
        const nr::options::OptionFrameSnapshot& snapshot,
        nr::options::OptionAvailabilityMap& availability) const override;
    void initialize(NodeInitContext& context) override;
    [[nodiscard]] bool supportsRenderGraphSkeleton() const noexcept override { return true; }
    [[nodiscard]] std::optional<StructuralSnapshot> structuralSnapshot(const NodeFrameParameters& frameParameters) const override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    bool materializeRenderGraphSkeleton(nr::renderer::RenderGraphSkeletonPatchContext& context, const NodeFrameParameters& frameParameters, const StructuralSnapshot& snapshot) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    void materializeCurrentFrame(NodeBuildContext& context, const NodeFrameParameters& frameParameters);
    std::shared_ptr<detail::PathTracingRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
};
} // namespace nr::renderPasses
