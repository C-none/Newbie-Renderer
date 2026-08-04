import nr.options;
import nr.renderer;
import nr.test;
import nr.utils;
import std;

namespace
{
struct EffectTestNode final : nr::renderer::NodeRuntime
{
    void build(nr::renderer::NodeBuildContext &, const nr::renderer::NodeFrameParameters &) override
    {
    }
};

[[nodiscard]] std::string readProjectFile(std::filesystem::path relativePath)
{
    auto const path = std::filesystem::path{std::string{nr::projectRoot}} / relativePath;
    auto file = std::ifstream{path};
    nr::test::require(file.good(), std::format("failed to open {}", path.generic_string()));
    auto contents = std::ostringstream{};
    contents << file.rdbuf();
    return contents.str();
}

void requirePresent(std::string_view contents, std::string_view token, std::string_view message)
{
    nr::test::require(contents.contains(token), std::string{message});
}

void requireOrdered(std::string_view contents, std::string_view first, std::string_view second,
                    std::string_view message)
{
    auto const firstPosition = contents.find(first);
    auto const secondPosition = firstPosition == std::string_view::npos
                                    ? std::string_view::npos
                                    : contents.find(second, firstPosition + first.size());
    nr::test::require(firstPosition != std::string_view::npos && secondPosition != std::string_view::npos &&
                          firstPosition < secondPosition,
                      std::string{message});
}

const nr::test::CaseRegistrar frameEffectSinkCase{
    "frame effect sink permits one exact valid pass claim", [] {
        auto firstNode = EffectTestNode{};
        auto secondNode = EffectTestNode{};
        auto empty = nr::renderer::FrameEffectSink{};
        nr::test::require(!empty.claim(firstNode, nr::renderer::GraphPassHandle{3u}),
                          "a sink without an effect cannot be claimed");

        auto effectId = nr::options::OptionId::parse("render.present.capture_exr");
        nr::test::require(effectId.has_value());
        auto sink = nr::renderer::FrameEffectSink{nr::options::FrameEffect{
            .sequence = 17u,
            .id = *effectId,
            .input = nr::options::OptionWireValue::Object{},
        }};
        nr::test::require(!sink.claim(firstNode, nr::renderer::GraphPassHandle{}),
                          "an invalid pass cannot claim an effect");
        nr::test::require(sink.claim(firstNode, nr::renderer::GraphPassHandle{7u}));
        nr::test::require(sink.claimed());
        nr::test::requireEqual(sink.targetPass(), nr::renderer::GraphPassHandle{7u});
        nr::test::require(sink.claimedRuntime().has_value() &&
                          std::addressof(sink.claimedRuntime()->get()) == std::addressof(firstNode));
        nr::test::require(!sink.claim(secondNode, nr::renderer::GraphPassHandle{9u}),
                          "a second node or pass cannot replace the first claim");
        nr::test::requireEqual(sink.targetPass(), nr::renderer::GraphPassHandle{7u});
    }};

const nr::test::CaseRegistrar rendererOptionAndEffectStaticCase{
    "renderer preflight and frame effects preserve snapshot and exact-batch boundaries", [] {
        auto const interface = readProjectFile("src/renderer/nrRenderer.ixx");
        auto const renderer = readProjectFile("src/renderer/nrRenderer.cpp");
        auto const pipeline = readProjectFile("src/pipeline/nrPipeline.cpp");
        auto const executorInterface = readProjectFile("src/renderer/nrRenderGraphExecutor.ixx");
        auto const executor = readProjectFile("src/renderer/nrRenderGraphExecutor.cpp");

        requirePresent(interface, "std::reference_wrapper<const nr::options::OptionFrameSnapshot> optionSnapshot;",
                       "renderer and node frame contracts must require an immutable option snapshot");
        requirePresent(renderer, "knownSemantics.emplace(semantic).second",
                       "graph preflight must reject duplicate actionable semantic nodes");
        requirePresent(renderer, "createInfo.runtime->declareOptions(optionBuilder)",
                       "graph preflight must collect pure node option definitions");
        requirePresent(renderer, "Frame resolution resolver requires undeclared graph option",
                       "graph preflight must prove every resolver option key exists");
        requireOrdered(renderer, "auto const preflight = preflightGraph(spec);", "device_->waitIdle();",
                       "graph preflight must complete before the destructive install barrier");

        requirePresent(executorInterface, "std::vector<std::uint32_t> submittedCompiledBatchIndices{};",
                       "executor reports must identify the exact compiled batches submitted");
        requireOrdered(executor, "context.device.submitFrameBatch(",
                       "report.submittedCompiledBatchIndices.push_back(compiledBatch.batchIndex);",
                       "a compiled batch may be reported submitted only after queue submission is accepted");
        requirePresent(renderer, "pass.handle == targetPass",
                       "renderer must map a claimed effect pass to its exact compiled batch");
        requirePresent(renderer, "executeReport.submittedCompiledBatchIndices",
                       "effect success must inspect exact submitted compiled batch indices");
        requirePresent(renderer, "std::string{\"failed_before_submission\"}",
                       "the frame-scope effect finalizer must default to failure");
        requireOrdered(renderer, "auto frameEffectFinalizer = ScopeExit", "if (!device_ || !graphInstalled_)",
                       "the frame-effect finalizer must cover renderer or graph unavailability");
        requireOrdered(renderer, "auto begin = device_->beginFrame();", "frameEffectFrameSlot = begin.frameIndex;",
                       "effect finalization must use the RHI frame slot reclaimed by beginFrame");
        requirePresent(renderer, "disposition == FrameEffectFinalizeDisposition::continuationArmed",
                       "renderer must distinguish terminal effects from armed continuations");
        requirePresent(renderer, ".status = nr::options::OptionLogStatus::started",
                       "capture dispatch must be logged as started only at the exact submission boundary");
        requireOrdered(renderer, "if (frameEffectTargetSubmitted &&",
                       ".phase = nr::options::OptionLogPhase::dispatchStarted",
                       "capture dispatch logging must remain guarded by exact target-batch submission");
        requirePresent(renderer, "effect.id == nr::options::optionId(nr::options::keys::presentCaptureExr)",
                       "unrelated submitted frame effects must never emit a capture dispatch record");
        nr::test::require(!pipeline.contains("OptionLogPhase::dispatchStarted"),
                          "the pipeline must not log capture dispatch before renderer submission");
    }};
} // namespace
