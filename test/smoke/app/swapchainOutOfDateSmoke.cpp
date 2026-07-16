import std;
import dependency;
import nr.app;
import nr.renderer;
import nr.renderPasses;

namespace
{
struct AcquireObservations
{
    std::uint32_t attempts = 0;
    std::uint32_t forced = 0;
    std::uint32_t nativePassThrough = 0;
};

struct ResolverObservations
{
    std::uint32_t calls = 0;
    std::uint32_t acquireAttemptsAtCall = 0;
    vk::Extent2D displayExtent{};
    bool matchedPresentationExtent = false;
};

[[nodiscard]] nr::renderer::RendererGraphSpec buildGraphSpec(
    const std::shared_ptr<nr::renderPasses::EmbeddedTriangleNode>& embeddedTriangle,
    AcquireObservations& acquire,
    ResolverObservations& resolver)
{
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = embeddedTriangle,
            .config = nr::renderer::NodeConfig{
                .instanceName = "EmbeddedTriangle",
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
    graphSpec.submitNodes = {
        nr::renderer::SubmitNodeSpec{
            .debugName = "SwapchainOutOfDate.GraphicsToCompute",
            .afterNodeIndex = 0,
        },
    };
    graphSpec.frameResolutionResolver = [&acquire, &resolver](nr::rhi::Device& device, vk::Extent2D displayExtent) {
        ++resolver.calls;
        resolver.acquireAttemptsAtCall = acquire.attempts;
        resolver.displayExtent = displayExtent;
        resolver.matchedPresentationExtent = displayExtent == device.presentationContext.swapchainExtent();
        return nr::renderer::FrameResolutionPlan{
            .displayExtent = displayExtent,
            .renderExtent = displayExtent,
        };
    };
    return graphSpec;
}

[[nodiscard]] bool validateObservations(
    const AcquireObservations& acquire,
    const ResolverObservations& resolver,
    const nr::renderer::RendererFrameResult& frameResult)
{
    auto valid = true;
    auto require = [&](bool condition, std::string_view message) {
        if (!condition)
        {
            std::println("[error] {}", message);
            valid = false;
        }
    };

    require(acquire.attempts == 2u, "expected exactly two acquire hook attempts");
    require(acquire.forced == 1u, "expected exactly one forced out-of-date acquire");
    require(acquire.nativePassThrough == 1u, "expected exactly one native acquire pass-through");
    require(resolver.calls == 1u, "expected the frame resolution resolver to run exactly once");
    require(resolver.acquireAttemptsAtCall == 2u, "expected the resolver to run after the recreated swapchain acquire");
    require(resolver.displayExtent.width > 0u && resolver.displayExtent.height > 0u, "expected a non-zero resolver display extent");
    require(resolver.matchedPresentationExtent, "expected resolver display extent to match the current presentation extent");
    require(frameResult.rendered, "expected the frame to render");
    require(frameResult.presentResult == vk::Result::eSuccess, "expected presentation to succeed");
    require(frameResult.compiledSubmitBatchCount >= 2u, "expected at least two compiled submit batches");
    require(frameResult.submittedBatchCount == frameResult.compiledSubmitBatchCount, "expected every compiled submit batch to be submitted");
    require(frameResult.invokedPassPrepareCount >= 3u, "expected at least three prepared passes");
    require(frameResult.invokedPassRecordCount == frameResult.invokedPassPrepareCount, "expected every prepared pass to be recorded");
    return valid;
}

[[nodiscard]] bool runSmokeTest()
{
    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = "SwapchainOutOfDateSmoke",
        .engineName = "NewbieRenderer",
    });

    auto& renderer = app.renderer();
    auto& presentation = renderer.device().presentationContext;
    auto acquire = AcquireObservations{};
    auto resolver = ResolverObservations{};

    presentation.setAcquireOutOfDateTestHook([&acquire]() {
        ++acquire.attempts;
        if (acquire.attempts == 1u)
        {
            ++acquire.forced;
            return true;
        }
        ++acquire.nativePassThrough;
        return false;
    });
    auto hookCleanup = std::scope_exit([&presentation]() {
        presentation.clearAcquireOutOfDateTestHook();
    });

    auto embeddedTriangle = std::make_shared<nr::renderPasses::EmbeddedTriangleNode>();
    renderer.installGraph(buildGraphSpec(embeddedTriangle, acquire, resolver));
    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
    });

    presentation.clearAcquireOutOfDateTestHook();
    hookCleanup.release();
    auto const valid = validateObservations(acquire, resolver, frameResult);
    auto const attemptsAfterRender = acquire.attempts;
    if (attemptsAfterRender != 2u)
    {
        std::println("[error] acquire hook attempts changed after renderFrame: {}", attemptsAfterRender);
    }

    app.shutdown();
    return valid && attemptsAfterRender == 2u;
}
} // namespace

int main()
{
    return runSmokeTest() ? 0 : 1;
}
