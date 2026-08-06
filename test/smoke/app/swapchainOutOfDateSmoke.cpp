import std;
import dependency.vulkan;
import nr.app;
import nr.options;
import nr.renderer;
import nr.renderPasses;
import nr.rhi;
import nr.test.options;

namespace
{
// TODO: Replace this compatibility guard with std::scope_exit as soon as libc++ supports C++23 <scope>.
class AcquireOutOfDateHookCleanupGuard
{
  public:
    explicit AcquireOutOfDateHookCleanupGuard(nr::rhi::PresentationContext &presentation) noexcept
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

    AcquireOutOfDateHookCleanupGuard(const AcquireOutOfDateHookCleanupGuard &) = delete;
    AcquireOutOfDateHookCleanupGuard &operator=(const AcquireOutOfDateHookCleanupGuard &) = delete;
    AcquireOutOfDateHookCleanupGuard(AcquireOutOfDateHookCleanupGuard &&) = delete;
    AcquireOutOfDateHookCleanupGuard &operator=(AcquireOutOfDateHookCleanupGuard &&) = delete;

    void release() noexcept
    {
        active_ = false;
    }

  private:
    nr::rhi::PresentationContext &presentation_;
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
    std::vector<std::uint32_t> acquireAttemptsAtCalls;
    std::vector<vk::Extent2D> displayExtents;
    bool matchedPresentationExtents = true;
};

[[nodiscard]] nr::renderer::RendererGraphSpec buildGraphSpec(
    const std::shared_ptr<nr::renderPasses::EmbeddedTriangleNode> &embeddedTriangle, AcquireObservations &acquire,
    ResolverObservations &resolver)
{
    auto present = std::make_shared<nr::renderPasses::PresentNode>();
    auto graphSpec = nr::renderer::RendererGraphSpec{};
    graphSpec.nodes = {
        nr::renderer::NodeCreateInfo{
            .runtime = embeddedTriangle,
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "EmbeddedTriangle",
                },
        },
        nr::renderer::NodeCreateInfo{
            .runtime = present,
            .config =
                nr::renderer::NodeConfig{
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
    graphSpec.frameResolutionResolver = [&acquire, &resolver](nr::rhi::Device &device, vk::Extent2D displayExtent,
                                                              const nr::options::OptionFrameSnapshot &) {
        ++resolver.calls;
        resolver.acquireAttemptsAtCalls.push_back(acquire.attempts);
        resolver.displayExtents.push_back(displayExtent);
        resolver.matchedPresentationExtents =
            resolver.matchedPresentationExtents && displayExtent == device.presentationContext.swapchainExtent();
        return nr::renderer::FrameResolutionPlan{
            .displayExtent = displayExtent,
            .renderExtent = displayExtent,
        };
    };
    return graphSpec;
}

[[nodiscard]] bool validateObservations(const AcquireObservations &acquire, const ResolverObservations &resolver,
                                         const nr::renderer::RendererFrameResult &normalFrameResult,
                                         const nr::renderer::RendererFrameResult &recreatedFrameResult,
                                         std::uint64_t recreationGenerationInitial,
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
    require(resolver.calls == 2u, "expected the frame resolution resolver to run for both presentation generations");
    require(resolver.acquireAttemptsAtCalls == std::vector<std::uint32_t>{0u, 2u},
            "expected one normal resolver call before the hook and one after the recreated swapchain acquire");
    require(std::ranges::all_of(resolver.displayExtents, [](vk::Extent2D extent) {
                return extent.width > 0u && extent.height > 0u;
            }),
            "expected non-zero resolver display extents for both presentation generations");
    require(resolver.matchedPresentationExtents,
            "expected every resolver display extent to match its current presentation extent");

    auto validateFrame = [&](const nr::renderer::RendererFrameResult &frameResult, std::string_view phase) {
        require(frameResult.rendered, std::format("expected the {} frame to render", phase));
        require(frameResult.presentResult == vk::Result::eSuccess,
                std::format("expected the {} presentation to succeed", phase));
        require(frameResult.compiledSubmitBatchCount >= 2u,
                std::format("expected the {} frame to compile at least two submit batches", phase));
        require(frameResult.submittedBatchCount == frameResult.compiledSubmitBatchCount,
                std::format("expected every {} frame submit batch to be submitted", phase));
        require(frameResult.invokedPassPrepareCount >= 2u,
                std::format("expected at least two {} frame pass prepare callbacks", phase));
        require(frameResult.invokedPassRecordCount >= 2u,
                std::format("expected at least two {} frame pass record callbacks", phase));
        require(frameResult.replayedSecondaryCommandBufferCount == 3u,
                std::format("expected all three fixed graph pass command buffers to replay in the {} frame", phase));
    };
    validateFrame(normalFrameResult, "old-generation");
    validateFrame(recreatedFrameResult, "new-generation");
    require(recreationGenerationBefore == recreationGenerationInitial,
            "expected the initial normal present to keep the original swapchain generation");
    require(recreationGenerationAfter == recreationGenerationBefore + 1u,
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

    auto &renderer = app.renderer();
    auto &presentation = renderer.device().presentationContext;
    auto acquire = AcquireObservations{};
    auto resolver = ResolverObservations{};

    auto embeddedTriangle = std::make_shared<nr::renderPasses::EmbeddedTriangleNode>();
    auto graphSpec = buildGraphSpec(embeddedTriangle, acquire, resolver);
    auto const preflight = renderer.preflightGraph(graphSpec);
    if (!preflight || !renderer.installGraph(graphSpec))
    {
        app.shutdown();
        return false;
    }
    auto const optionSnapshot = nr::test::options::makeDefaultSnapshot(preflight.optionCatalog, "smoke-snapshot");
    auto const recreationGenerationInitial = renderer.device().swapchainRecreationGeneration();
    auto normalFrameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .optionSnapshot = std::cref(optionSnapshot),
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
    });

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

    auto const recreationGenerationBefore = renderer.device().swapchainRecreationGeneration();
    auto recreatedFrameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
        .optionSnapshot = std::cref(optionSnapshot),
        .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
    });
    auto const recreationGenerationAfter = renderer.device().swapchainRecreationGeneration();

    presentation.clearAcquireOutOfDateTestHook();
    hookCleanup.release();
    auto const valid = validateObservations(acquire, resolver, normalFrameResult, recreatedFrameResult,
                                            recreationGenerationInitial, recreationGenerationBefore,
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
