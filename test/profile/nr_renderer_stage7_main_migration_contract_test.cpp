import std;
import nr.renderer;
import nr.renderPasses;

namespace
{
[[nodiscard]] bool require(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::println("[fail] {}", message);
        return false;
    }
    return true;
}

template <typename T>
concept HasBuildGraphMember = requires(T value) {
    value.buildGraph;
};

static_assert(!HasBuildGraphMember<nr::renderer::RendererFrameInput>);

static_assert(std::is_invocable_v<
              decltype(&nr::renderer::Renderer::installGraph),
              nr::renderer::Renderer&,
              const nr::renderer::RendererGraphSpec&>);

static_assert(std::is_invocable_v<
              decltype(&nr::renderer::Renderer::resize),
              nr::renderer::Renderer&>);

[[nodiscard]] nr::renderer::RendererGraphSpec buildMainPathGraphSpec()
{
    auto normalView = std::make_shared<nr::renderPasses::NormalViewNode>();
    auto present = std::make_shared<nr::renderPasses::PresentNode>();

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = normalView,
            .config = nr::renderer::NodeConfig{
                .instanceName = "NormalView",
                .queue = nr::renderer::QueueDomain::Graphics,
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = present,
            .config = nr::renderer::NodeConfig{
                .instanceName = "Present",
                .queue = nr::renderer::QueueDomain::Compute,
            },
        },
    };

    graphSpec.connections = {
        nr::renderer::NodeConnection{
            .from = nr::renderer::NodePortRef{
                .nodeName = "NormalView",
                .portName = "color",
            },
            .to = nr::renderer::NodePortRef{
                .nodeName = "Present",
                .portName = "sourceColor",
            },
        },
    };

    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "Main.GraphicsToCompute",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 0,
        },
    };

    return graphSpec;
}

[[nodiscard]] bool checkMainPathGraphSpecMigration()
{
    auto graphSpec = buildMainPathGraphSpec();

    if (!require(graphSpec.nodes.size() == 2u, "Main graph should install two built-in nodes."))
    {
        return false;
    }

    if (!require(graphSpec.connections.size() == 1u, "Main graph should connect NormalView color -> Present sourceColor."))
    {
        return false;
    }

    if (!require(graphSpec.submitNodes.size() == 1u, "Main graph should keep one explicit graphics->compute submit boundary."))
    {
        return false;
    }

    if (!require(graphSpec.nodes[0].config.queue == nr::renderer::QueueDomain::Graphics &&
                     graphSpec.nodes[1].config.queue == nr::renderer::QueueDomain::Compute,
                 "Main graph should preserve node-level queue assignments."))
    {
        return false;
    }

    if (!require(graphSpec.connections.front().to.portName == "sourceColor",
                 "Present should consume upstream sourceColor connection in migrated main graph."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool checkResizeRecreateSmokeGuard()
{
    auto renderer = nr::renderer::Renderer{};

    renderer.resize();

    auto frameResult = renderer.renderFrame();
    if (!require(!frameResult.rendered,
                 "Renderer should not render before initialize/installGraph (smoke guard for pre-init resize/recreate path)."))
    {
        return false;
    }

    renderer.shutdown();
    renderer.resize();

    return true;
}
} // namespace

int main()
{
    if (!checkMainPathGraphSpecMigration())
    {
        std::println("[FAIL] stage7 main graph migration contract failed");
        return 1;
    }

    if (!checkResizeRecreateSmokeGuard())
    {
        std::println("[FAIL] stage7 resize/recreate smoke contract failed");
        return 2;
    }

    std::println("[OK] renderer stage7 main migration contract tests passed");
    return 0;
}
