import std;
import dependency;
import nr.app;
import nr.options;
import nr.renderer;
import nr.renderPasses;
import nr.rhi;

namespace
{
// TODO: Replace this compatibility guard with std::scope_exit as soon as libc++ supports C++23 <scope>.
class AcquireOutOfDateHookCleanupGuard
{
  public:
    explicit AcquireOutOfDateHookCleanupGuard(nr::rhi::PresentationContext& presentation) noexcept
        : presentation_(presentation)
    {
    }

    ~AcquireOutOfDateHookCleanupGuard() noexcept
    {
        if (active_)
        {
            presentation_.clearAcquireOutOfDateTestHook();
        }
    }

    AcquireOutOfDateHookCleanupGuard(const AcquireOutOfDateHookCleanupGuard&) = delete;
    AcquireOutOfDateHookCleanupGuard& operator=(const AcquireOutOfDateHookCleanupGuard&) = delete;
    AcquireOutOfDateHookCleanupGuard(AcquireOutOfDateHookCleanupGuard&&) = delete;
    AcquireOutOfDateHookCleanupGuard& operator=(AcquireOutOfDateHookCleanupGuard&&) = delete;

    void release() noexcept
    {
        active_ = false;
    }

  private:
    nr::rhi::PresentationContext& presentation_;
    bool active_ = true;
};

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

[[nodiscard]] nr::options::OptionFrameSnapshot makeDefaultSnapshot(
    const nr::renderer::RendererGraphPreflightResult& preflight)
{
    auto values = nr::options::OptionValueMap{};
    auto availability = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(preflight.optionCatalog->definitions(), [&](auto const& entry) {
        values.emplace(entry.first, entry.second.defaultValue);
        availability.emplace(
            entry.first,
            nr::options::OptionAvailability{.available = true, .reason = {}});
    });
    return nr::options::OptionFrameSnapshot{
        .catalog = preflight.optionCatalog,
        .values = std::move(values),
        .availability = std::move(availability),
        .frameIndex = 1u,
        .revision = 1u,
        .graphGeneration = 1u,
        .bindingEpoch = 1u,
        .snapshotToken = "smoke-snapshot",
    };
}

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
    graphSpec.frameResolutionResolver = [&acquire, &resolver](
                                            nr::rhi::Device& device,
                                            vk::Extent2D displayExtent,
                                            const nr::options::OptionFrameSnapshot&) {
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
    const nr::renderer::RendererFrameResult& frameResult,
    std::uint64_t recreationGenerationBefore,
    std::uint64_t recreationGenerationAfter)
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
    require(frameResult.invokedPassPrepareCount >= 2u, "expected at least two pass prepare callbacks to be invoked");
    require(frameResult.invokedPassRecordCount >= 3u, "expected at least three explicit pass record callbacks to be invoked");
    require(frameResult.replayedSecondaryCommandBufferCount == 4u, "expected all four fixed graph pass secondary command buffers to be replayed");
    require(
        recreationGenerationAfter == recreationGenerationBefore + 1u,
        "expected the forced acquire out-of-date path to increment swapchain recreation generation exactly once");
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
    auto hookCleanup = AcquireOutOfDateHookCleanupGuard{presentation};

    auto embeddedTriangle = std::make_shared<nr::renderPasses::EmbeddedTriangleNode>();
    auto graphSpec = buildGraphSpec(embeddedTriangle, acquire, resolver);
    auto const preflight = renderer.preflightGraph(graphSpec);
    if (!preflight || !renderer.installGraph(graphSpec))
    {
        return 1;
    }
    auto const optionSnapshot = makeDefaultSnapshot(preflight);
    auto const recreationGenerationBefore = renderer.device().swapchainRecreationGeneration();
    auto frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .optionSnapshot = std::cref(optionSnapshot),
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
    });
    auto const recreationGenerationAfter = renderer.device().swapchainRecreationGeneration();

    presentation.clearAcquireOutOfDateTestHook();
    hookCleanup.release();
    auto const valid = validateObservations(
        acquire,
        resolver,
        frameResult,
        recreationGenerationBefore,
        recreationGenerationAfter);
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
