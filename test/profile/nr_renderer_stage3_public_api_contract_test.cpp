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

template <typename TOwnership>
concept HasPresentOwnershipEnumerator = requires {
    TOwnership::Present;
};

template <typename T>
concept HasUseLevelQueueField = requires(T use) {
    use.queue;
};

[[nodiscard]] bool testCompileTimePublicPruning()
{
    static_assert(!HasPresentOwnershipEnumerator<nr::renderer::ResourceOwnershipDomain>, "Stage3 requires removing public ResourceOwnershipDomain::Present.");
    static_assert(!HasUseLevelQueueField<nr::renderer::PassResourceUseDesc>, "Stage3 requires removing use-level queue declaration.");

    // Phase 1: RenderGraphNodeContext is no longer publicly exported
    // Verify NodeBuildContext has the new node-scoped authoring methods
    static_assert(requires(nr::renderer::NodeBuildContext &ctx,
                          const nr::renderer::GraphTransientBufferDesc &bufDesc,
                          const nr::renderer::GraphTransientImageDesc &imgDesc,
                          std::span<const nr::renderer::PassResourceUseDesc> intents) {
        { ctx.addResource(bufDesc) } -> std::same_as<nr::renderer::GraphResourceHandle>;
        { ctx.addResource(imgDesc) } -> std::same_as<nr::renderer::GraphResourceHandle>;
        { ctx.addPass(intents, "name", nullptr) } -> std::same_as<nr::renderer::GraphPassHandle>;
    });

    return true;
}

[[nodiscard]] bool testNodeQueueDrivesPassQueue()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("ComputeNode", nr::renderer::QueueDomain::Compute);

    auto transient = builder.addResource(nr::renderer::GraphTransientBufferDesc{
        .debugName = "Stage3.Buffer",
        .lifetime = nr::renderer::ResourceLifetime::GraphTransient,
        .size = 256,
        .usageIntents = {
            nr::renderer::BufferUsageIntent::StorageReadWrite,
        },
    });

    auto intentList = std::array{
        nr::renderer::PassResourceUseDesc{
            .resource = transient,
            .bufferUsage = nr::renderer::BufferUsageIntent::StorageReadWrite,
            .bufferAccess = nr::renderer::BufferAccessIntent::ShaderStorageWrite,
            .imageUsage = std::nullopt,
            .imageAccess = std::nullopt,
            .imageLayout = std::nullopt,
            .imageAspect = std::nullopt,
            .ownershipDomain = nr::renderer::ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        }
    };

    auto pass = builder.addPass("Stage3.Compute", node, intentList, nullptr, nullptr, false);

    auto frame = builder.build();
    if (!require(frame.nodes.size() == 1, "Builder should contain one node."))
    {
        return false;
    }
    if (!require(frame.passes.size() == 1, "Builder should contain one pass."))
    {
        return false;
    }

    if (!require(frame.nodes.front().queue == nr::renderer::QueueDomain::Compute,
                 "Node queue should be explicitly preserved in graph node descriptor."))
    {
        return false;
    }

    if (!require(frame.passes.front().queue == nr::renderer::QueueDomain::Compute,
                 "Pass queue should be derived from owning node queue."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testSwapchainOwnershipDefaults()
{
    auto desc = nr::renderer::GraphImportedSwapchainImageDesc{};
    if (!require(desc.initialOwnership == nr::renderer::ResourceOwnershipDomain::Compute,
                 "Swapchain imported image default ownership should be Compute after Stage3."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!testCompileTimePublicPruning())
    {
        std::println("[FAIL] stage3 compile-time public API pruning failed");
        return 1;
    }

    if (!testNodeQueueDrivesPassQueue())
    {
        std::println("[FAIL] stage3 node-queue contract failed");
        return 2;
    }

    if (!testSwapchainOwnershipDefaults())
    {
        std::println("[FAIL] stage3 swapchain ownership default test failed");
        return 3;
    }

    std::println("[OK] renderer stage3 public API contract tests passed");
    return 0;
}
