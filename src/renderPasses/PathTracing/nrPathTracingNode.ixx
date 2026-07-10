export module nr.renderPasses:pathTracing;
import dependency.vulkan;

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

    [[nodiscard]] friend auto operator<=>(const PathTracingVariantKey &, const PathTracingVariantKey &) noexcept = default;
};

struct PathTracingNodeInput
{
    vk::Format outputFormat = vk::Format::eR16G16B16A16Sfloat;
    PathTracingVariantKey variant{};
};

class PathTracingNode final : public Node
{
  public:
    PathTracingNode() = default;
    ~PathTracingNode() override;

    PathTracingNodeInput input{};

    [[nodiscard]] NodeDescription describe() const override;
    void initialize(NodeInitContext& context) override;
    void collectUi(NodeUiBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void build(NodeBuildContext& context, const NodeFrameParameters& frameParameters) override;
    void shutdown(NodeShutdownContext& context) override;

  private:
    std::shared_ptr<detail::PathTracingRuntimeCache> runtime_{};
    std::optional<std::reference_wrapper<nr::rhi::Device>> device_{};
    PathTracingVariantKey variantUiDraft_{};
    std::optional<PathTracingVariantKey> pendingVariant_{};
};
} // namespace nr::renderPasses
