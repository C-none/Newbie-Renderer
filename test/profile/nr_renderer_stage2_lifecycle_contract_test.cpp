import std;
import nr.renderer;

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
concept HasBuildGraphMember = requires(T input) {
    input.buildGraph;
};

class MockNode final : public nr::renderer::NodeRuntime
{
  public:
    mutable std::size_t describeCalls = 0;
    std::size_t initializeCalls = 0;
    std::size_t buildCalls = 0;
    std::size_t shutdownCalls = 0;

    nr::renderer::GraphResourceHandle lastResolvedInput{};
    nr::renderer::GraphResourceHandle lastPublishedOutput{};

    [[nodiscard]] nr::renderer::NodeDescription describe() const override
    {
        ++describeCalls;
        return nr::renderer::NodeDescription{
            .name = "MockNode",

            .inputPorts = {
                nr::renderer::NodePort{.name = "in"},
            },
            .outputPorts = {
                nr::renderer::NodePort{.name = "out"},
            },
        };
    }

    void initialize(nr::renderer::NodeInitContext&) override
    {
        ++initializeCalls;
    }

    void build(nr::renderer::NodeBuildContext& context, const nr::renderer::NodeFrameParameters&) override
    {
        ++buildCalls;
        lastResolvedInput = context.resolveInput("in");
        auto produced = nr::renderer::GraphResourceHandle{17};
        context.publishOutput("out", produced);
        lastPublishedOutput = produced;
    }

    void shutdown(nr::renderer::NodeShutdownContext&) override
    {
        ++shutdownCalls;
    }
};

[[nodiscard]] bool testCompileTimeContracts()
{
    static_assert(std::is_invocable_v<
                  decltype(&nr::renderer::Renderer::installGraph),
                  nr::renderer::Renderer&,
                  const nr::renderer::RendererGraphSpec&>);

    static_assert(!HasBuildGraphMember<nr::renderer::RendererFrameInput>);

    static_assert(requires {
        typename nr::renderer::NodeConfig;
        typename nr::renderer::NodeFrameParameters;
        typename nr::renderer::NodePort;
    });

    return true;
}

[[nodiscard]] bool testNodeBuildContextHelpers()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Mock", nr::renderer::QueueDomain::Compute);

    auto publishedPortName = std::string{};
    auto publishedResource = nr::renderer::GraphResourceHandle{};

    auto buildContext = nr::renderer::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = node,
        .resolveInputPort = [](std::string_view name) {
            return name == "in" ? nr::renderer::GraphResourceHandle{9} : nr::renderer::GraphResourceHandle{};
        },
        .publishOutputPort = [&](std::string_view name, nr::renderer::GraphResourceHandle resource) {
            publishedPortName = std::string{name};
            publishedResource = resource;
        },
    };

    auto nodeRuntime = MockNode{};
    auto frameParams = nr::renderer::NodeFrameParameters{
        .frameIndex = 1,
        .swapchainImageIndex = 0,
        .swapchainExtent = vk::Extent2D{1280, 720},
        .swapchainFormat = vk::Format::eB8G8R8A8Srgb,
        .scenePackets = std::nullopt,
        .primaryCamera = std::nullopt,
    };

    nodeRuntime.build(buildContext, frameParams);

    if (!require(nodeRuntime.buildCalls == 1, "MockNode build should be called once."))
    {
        return false;
    }
    if (!require(nodeRuntime.lastResolvedInput.valid(), "MockNode should resolve input port via context helper."))
    {
        return false;
    }
    if (!require(publishedPortName == "out", "NodeBuildContext::publishOutput should forward output port name."))
    {
        return false;
    }
    if (!require(publishedResource == nodeRuntime.lastPublishedOutput, "NodeBuildContext::publishOutput should forward output resource handle."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testGraphSpecShape()
{
    auto mockA = std::make_shared<MockNode>();
    auto mockB = std::make_shared<MockNode>();

    auto spec = nr::renderer::RendererGraphSpec{};
    spec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = mockA,
            .config = nr::renderer::NodeConfig{
                .instanceName = "A",
                .queue = nr::renderer::QueueDomain::Graphics,
            },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = mockB,
            .config = nr::renderer::NodeConfig{
                .instanceName = "B",
                .queue = nr::renderer::QueueDomain::Compute,
            },
        },
    };

    spec.connections = {
        nr::renderer::NodeConnection{
            .from = nr::renderer::NodePortRef{.nodeName = "A", .portName = "out"},
            .to = nr::renderer::NodePortRef{.nodeName = "B", .portName = "in"},
        },
    };

    spec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "A.To.B",
            .kind = nr::renderer::SubmitBoundaryKind::Explicit,
            .afterNodeIndex = 0,
        },
    };

    if (!require(spec.nodes.size() == 2, "RendererGraphSpec should keep node list."))
    {
        return false;
    }
    if (!require(spec.connections.size() == 1, "RendererGraphSpec should keep node connection list."))
    {
        return false;
    }
    if (!require(spec.submitNodes.size() == 1, "RendererGraphSpec should keep submit-node list."))
    {
        return false;
    }

    if (!require(spec.nodes[1].config.queue == nr::renderer::QueueDomain::Compute,
                 "NodeConfig queue should be preserved for node-level queue assignment."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!testCompileTimeContracts())
    {
        std::println("[FAIL] stage2 compile-time contracts failed");
        return 1;
    }

    if (!testNodeBuildContextHelpers())
    {
        std::println("[FAIL] stage2 node build context test failed");
        return 2;
    }

    if (!testGraphSpecShape())
    {
        std::println("[FAIL] stage2 graph spec shape test failed");
        return 3;
    }

    std::println("[OK] renderer stage2 lifecycle contract tests passed");
    return 0;
}
