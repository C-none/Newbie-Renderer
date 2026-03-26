import std;
import nr.renderer;
import nr.rhi;

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

static_assert(requires(nr::renderer::RenderGraphBuilder &builder,
                       nr::renderer::GraphNodeHandle node,
                       std::span<const nr::renderer::PassResourceUseDesc> intents,
                       nr::renderer::PassRecordCallback recordCallback,
                       nr::renderer::PassPrepareCallback prepareCallback) {
    {
        builder.addPass("Stage5.PreparePass", node, intents, recordCallback, prepareCallback)
    } -> std::same_as<nr::renderer::GraphPassHandle>;
});

static_assert(requires(nr::renderer::RenderGraphExecutor &executor,
                       const nr::renderer::CompiledGraphFrame &compiled,
                       const nr::renderer::RenderGraphExecutor::ExecuteContext &context,
                       const nr::renderer::PreparedGraphFrame &prepared) {
    {
        executor.prepareFrame(compiled, context)
    } -> std::same_as<nr::renderer::PreparedGraphFrame>;

    {
        executor.executePrepared(prepared, context)
    } -> std::same_as<nr::renderer::ExecuteReport>;
});

[[nodiscard]] bool checkPrepareCallbackInvokedDuringPrepareFrame()
{
    auto builder = nr::renderer::RenderGraphBuilder{};
    auto node = builder.addNode("Stage5.ComputeNode", nr::renderer::QueueDomain::Compute);

    auto intents = std::array<nr::renderer::PassResourceUseDesc, 0>{};

    auto prepareInvokedCount = std::size_t{0};
    auto recordInvokedCount = std::size_t{0};
    auto prepareLookupValid = true;

    auto pass = builder.addPass(
        "Stage5.PreparePass",
        node,
        std::span<const nr::renderer::PassResourceUseDesc>{intents.data(), intents.size()},
        [&](const nr::renderer::PassRecordContext &) {
            ++recordInvokedCount;
        },
        [&](const nr::renderer::PassPrepareContext &prepareContext) {
            ++prepareInvokedCount;

            if (prepareContext.resolveBuffer)
            {
                auto unresolved = prepareContext.resolveBuffer(nr::renderer::GraphResourceHandle{999u});
                if (unresolved.has_value())
                {
                    prepareLookupValid = false;
                }
            }

            if (prepareContext.resolveImage)
            {
                auto unresolved = prepareContext.resolveImage(nr::renderer::GraphResourceHandle{999u});
                if (unresolved.has_value())
                {
                    prepareLookupValid = false;
                }
            }
        });

    if (!require(pass.valid(), "addPass should return a valid pass handle for prepare callback contract test."))
    {
        return false;
    }

    auto frame = builder.build();
    auto compiler = nr::renderer::RenderGraphCompiler{};
    auto compiled = compiler.compile(frame);

    auto device = nr::rhi::Device{};
    auto executor = nr::renderer::RenderGraphExecutor{};

    auto executeContext = nr::renderer::RenderGraphExecutor::ExecuteContext{
        .device = device,
        .frameIndex = 0,
        .swapchainImageIndex = std::nullopt,
        .submissionTimeline = std::nullopt,
        .importedBuffers = {},
        .importedImages = {},
    };

    auto prepared = executor.prepareFrame(compiled, executeContext);

    if (!require(prepareInvokedCount == 1u, "prepare callback should be invoked exactly once during prepareFrame."))
    {
        return false;
    }

    if (!require(prepared.invokedPassPrepareCount == 1u, "PreparedGraphFrame should report one invoked prepare callback."))
    {
        return false;
    }

    if (!require(recordInvokedCount == 0u, "record callback should not run during prepareFrame."))
    {
        return false;
    }

    if (!require(prepareLookupValid, "prepare context should not resolve unknown handles."))
    {
        return false;
    }

    if (!require(prepared.compiled.submitBatches.size() == 1u, "prepareFrame should preserve compiled submit batch shape."))
    {
        return false;
    }

    if (!require(prepared.plan.batches.size() == 1u, "prepareFrame should build executor plan with one batch."))
    {
        return false;
    }

    if (!require(prepared.runtimeBindings.empty(), "prepareFrame should keep runtimeBindings empty when graph has no resources."))
    {
        return false;
    }

    return true;
}
} // namespace

int main()
{
    if (!checkPrepareCallbackInvokedDuringPrepareFrame())
    {
        std::println("[FAIL] stage5 runtime prepare/execute contract failed");
        return 1;
    }

    std::println("[OK] renderer stage5 runtime contract tests passed");
    return 0;
}
