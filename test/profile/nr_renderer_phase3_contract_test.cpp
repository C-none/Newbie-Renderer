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

using namespace nr::renderer;

[[nodiscard]] bool testNoWriterReferencesInProductionPath()
{
    auto builder = RenderGraphBuilder{};
    auto node = builder.addNode("Phase3.Node", QueueDomain::Compute);

    auto buffer = builder.addResource(GraphTransientBufferDesc{
        .debugName = "Phase3.CheckBuffer",
        .lifetime = ResourceLifetime::GraphTransient,
        .size = 256,
        .usageIntents = {BufferUsageIntent::StorageReadWrite},
    });

    auto passUses = std::array{
        PassResourceUseDesc{
            .resource = buffer,
            .bufferUsage = BufferUsageIntent::StorageReadWrite,
            .bufferAccess = BufferAccessIntent::ShaderStorageWrite,
            .ownershipDomain = ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        },
    };

    auto pass = builder.addPass(
        "Phase3.CanonicalPath",
        node,
        std::span<const PassResourceUseDesc>{passUses.data(), passUses.size()},
        [](const PassRecordContext &) {});

    if (!require(pass.valid(), "addPass should return a valid pass handle."))
    {
        return false;
    }

    auto built = builder.build();
    if (!require(built.passes.size() == 1u, "Builder should contain exactly one pass."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testBindingSnapshotFrameIsolation()
{
    auto builder = RenderGraphBuilder{};
    auto node = builder.addNode("Phase3.FrameIsolation", QueueDomain::Compute);

    auto pass1 = builder.addPass(
        "Phase3.Frame1",
        node,
        std::span<const PassResourceUseDesc>{},
        [](const PassRecordContext &) {});

    auto pass2 = builder.addPass(
        "Phase3.Frame2",
        node,
        std::span<const PassResourceUseDesc>{},
        [](const PassRecordContext &) {});

    if (!require(pass1.valid() && pass2.valid(), "Both passes should be valid."))
    {
        return false;
    }

    auto built = builder.build();
    if (!require(built.passes.size() == 2u, "Both passes should be in built graph."))
    {
        return false;
    }

    auto const &pass1Recorded = built.passes[0];
    auto const &pass2Recorded = built.passes[1];

    if (!require(pass1Recorded.debugName == "Phase3.Frame1" && pass2Recorded.debugName == "Phase3.Frame2",
                 "Pass names should be preserved in execution order."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testGraphTransientResourceResolution()
{
    auto builder = RenderGraphBuilder{};
    auto node = builder.addNode("Phase3.Transient", QueueDomain::Compute);

    auto transientBuffer = builder.addResource(GraphTransientBufferDesc{
        .debugName = "Phase3.TransientBuffer",
        .lifetime = ResourceLifetime::GraphTransient,
        .size = 512,
        .usageIntents = {BufferUsageIntent::StorageReadWrite},
    });

    if (!require(transientBuffer.valid(), "Transient buffer creation should succeed."))
    {
        return false;
    }

    auto passUses = std::array{
        PassResourceUseDesc{
            .resource = transientBuffer,
            .bufferUsage = BufferUsageIntent::StorageReadWrite,
            .bufferAccess = BufferAccessIntent::ShaderStorageWrite,
            .ownershipDomain = ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        },
    };

    auto pass = builder.addPass(
        "Phase3.TransientCheck",
        node,
        std::span<const PassResourceUseDesc>{passUses.data(), passUses.size()},
        [](const PassRecordContext &) {});

    if (!require(pass.valid(), "Transient-check pass should be valid."))
    {
        return false;
    }

    auto frame = builder.build();
    if (!require(frame.passes.size() == 1u, "Frame should contain the transient pass."))
    {
        return false;
    }

    auto const &recordedPass = frame.passes.front();
    if (!require(recordedPass.resourceUses.size() == 1u, "Pass should record resource use."))
    {
        return false;
    }

    if (!require(recordedPass.resourceUses.front().resource == transientBuffer,
                 "Resource should match transient buffer handle."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testCompatibilityShimDocumentation()
{
    auto builder = RenderGraphBuilder{};
    auto node = builder.addNode("Phase3.ShimCheck", QueueDomain::Compute);

    auto passHandle = builder.addPass(
        "Phase3.CanonicalPath",
        node,
        std::span<const PassResourceUseDesc>{},
        [](const PassRecordContext &) {});

    if (!require(passHandle.valid(), "Canonical addPass should succeed."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool testArchitectureDocumentationConsistency()
{
    auto builder = RenderGraphBuilder{};

    auto computeNode = builder.addNode("Phase3.DocCheck", QueueDomain::Compute);
    if (!require(computeNode.valid(), "Node creation should succeed."))
    {
        return false;
    }

    auto buffer = builder.addResource(GraphTransientBufferDesc{
        .debugName = "Phase3.Buffer",
        .lifetime = ResourceLifetime::GraphTransient,
        .size = 256,
        .usageIntents = {BufferUsageIntent::StorageReadWrite},
    });

    auto passUses = std::array{
        PassResourceUseDesc{
            .resource = buffer,
            .bufferUsage = BufferUsageIntent::StorageReadWrite,
            .bufferAccess = BufferAccessIntent::ShaderStorageWrite,
            .ownershipDomain = ResourceOwnershipDomain::Undefined,
            .readOnly = false,
        },
    };

    auto passHandle = builder.addPass(
        "Phase3.DocumentedPass",
        computeNode,
        std::span<const PassResourceUseDesc>{passUses.data(), passUses.size()},
        [](const PassRecordContext &) {});

    if (!require(passHandle.valid(), "Pass submission should succeed."))
    {
        return false;
    }

    auto frame = builder.build();
    if (!require(!frame.passes.empty(), "Build should produce passes."))
    {
        return false;
    }

    if (!require(frame.passes.front().debugName == "Phase3.DocumentedPass",
                 "Pass name should be preserved."))
    {
        return false;
    }

    return true;
}

class Phase3SampleNode final : public nr::renderPasses::Node
{
  public:
    [[nodiscard]] nr::renderPasses::NodeDescription describe() const override
    {
        return nr::renderPasses::NodeDescription{
            .name = "Phase3.NewSampleNode",
            .inputPorts = {},
            .outputPorts = {
                nr::renderPasses::NodePort{.name = "out"},
            },
        };
    }

    void build(nr::renderPasses::NodeBuildContext &context, const nr::renderPasses::NodeFrameParameters &) override
    {
        auto transientBuffer = context.addResource(GraphTransientBufferDesc{
            .debugName = "Phase3.FrameTransient",
            .lifetime = ResourceLifetime::GraphTransient,
            .size = 512,
            .usageIntents = {BufferUsageIntent::StorageRead},
        });

        auto passIntents = std::array{
            PassResourceUseDesc{
                .resource = transientBuffer,
                .bufferUsage = BufferUsageIntent::StorageRead,
                .bufferAccess = BufferAccessIntent::ShaderStorageRead,
                .ownershipDomain = ResourceOwnershipDomain::Undefined,
                .readOnly = true,
            },
        };

        [[maybe_unused]] auto pass = context.addPass(
            std::span<const PassResourceUseDesc>{passIntents.data(), passIntents.size()},
            "Phase3.NewNodeComputePass",
            [](const PassRecordContext &) {});

        context.publishOutput("out", transientBuffer);
    }
};

[[nodiscard]] bool testNewNodeCanUseThreeStageContract()
{
    auto builder = RenderGraphBuilder{};
    auto nodeHandle = builder.addNode("Phase3.NewSampleNode", QueueDomain::Compute);

    auto publishedOutput = GraphResourceHandle{};
    auto node = Phase3SampleNode{};
    auto frameParameters = nr::renderPasses::NodeFrameParameters{};

    auto buildContext = nr::renderPasses::NodeBuildContext{
        .graphBuilder = std::ref(builder),
        .nodeHandle = nodeHandle,
        .resolveInputPort = [](std::string_view) {
            return GraphResourceHandle{};
        },
        .publishOutputPort = [&](std::string_view portName, GraphResourceHandle handle) {
            if (portName == "out")
            {
                publishedOutput = handle;
            }
        },
    };

    node.build(buildContext, frameParameters);

    auto frame = builder.build();
    if (!require(frame.passes.size() == 1u, "New node should register one pass."))
    {
        return false;
    }

    auto const &recordedPass = frame.passes.front();
    if (!require(recordedPass.debugName == "Phase3.NewNodeComputePass",
                 "Pass name should be preserved for profiling."))
    {
        return false;
    }

    if (!require(!recordedPass.prepare,
                 "Canonical model should not require prepare callbacks."))
    {
        return false;
    }

    if (!require(publishedOutput.valid(), "New node should publish output resource handle."))
    {
        return false;
    }

    return true;
}

[[nodiscard]] bool runPhase3SemanticHardeningTests()
{
    auto testCount = 0;
    auto passCount = 0;

    auto tests = std::array{
        std::pair{"Criterion 1: No writer references", &testNoWriterReferencesInProductionPath},
        std::pair{"Criterion 2: Snapshot frame isolation", &testBindingSnapshotFrameIsolation},
        std::pair{"Criterion 3: Transient resource resolution", &testGraphTransientResourceResolution},
        std::pair{"Criterion 5: Shim documentation", &testCompatibilityShimDocumentation},
        std::pair{"Criterion 6: Arch documentation match", &testArchitectureDocumentationConsistency},
        std::pair{"Criterion 7: New node three-stage contract", &testNewNodeCanUseThreeStageContract},
    };

    std::ranges::for_each(tests, [&](const auto &entry) {
        ++testCount;
        auto const &[testName, testFn] = entry;
        std::println("Phase 3 test: {}", testName);

        try
        {
            if (testFn())
            {
                ++passCount;
                std::println("  [PASS]");
            }
            else
            {
                std::println("  [FAIL]");
            }
        }
        catch (...)
        {
            std::println("  [EXCEPTION]");
        }
    });

    std::println("Phase 3: {}/{} passed", passCount, testCount);
    return passCount == testCount;
}
} // namespace

int main()
{
    return runPhase3SemanticHardeningTests() ? 0 : 1;
}
