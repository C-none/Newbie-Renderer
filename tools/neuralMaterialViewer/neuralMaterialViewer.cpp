import std;
import dependency.vulkan;
import nr.app;
import nr.options;
import nr.renderer;
import nr.renderPasses;
import nr.rhi;
import nr.utils;

namespace
{
inline constexpr auto kCheckpointFrameInterval = std::uint64_t{64u};

void printUsage()
{
    std::println("Usage:");
    std::println("  neuralMaterialViewer");
    std::println("  neuralMaterialViewer --train-and-save <artifact.nart> [--checkpoint <checkpoint.bin>]");
    std::println("  neuralMaterialViewer --resume <checkpoint.bin> --train-and-save <artifact.nart>");
    std::println("  neuralMaterialViewer --help");
    std::println("Modes:");
    std::println("  no arguments                      Run the interactive viewer.");
    std::println("  --train-and-save <path>           Train and save the final artifact.");
    std::println("  --checkpoint <path>               Override the resumable checkpoint path.");
    std::println("                                    Default: <artifact-path>.checkpoint.");
    std::println("  --resume <path>                   Require and resume the specified checkpoint.");
    std::println("  --seed <n>                        Training seed for initialization and sample order.");
    std::println("View: left native, middle neural, right absolute error.");
}

struct TrainingCommandLineOptions
{
    std::filesystem::path artifactPath{};
    std::filesystem::path checkpointPath{};
    bool checkpointMustExist = false;
};

struct CommandLineOptions
{
    std::optional<TrainingCommandLineOptions> training{};
    bool showHelp = false;
    std::uint32_t trainingSeed = 0u;
};

// `--seed <n>` is extracted before positional parsing so it can appear anywhere
// without disturbing the existing argument order.
[[nodiscard]] std::optional<std::pair<std::uint32_t, std::vector<char *>>> extractSeedOption(std::span<char *> args)
{
    auto seed = std::uint32_t{0u};
    auto remaining = std::vector<char *>{};
    remaining.reserve(args.size());
    for (auto index = std::size_t{0u}; index < args.size(); ++index)
    {
        auto const value = args[index] != nullptr ? std::string_view{args[index]} : std::string_view{};
        if (value != "--seed")
        {
            remaining.push_back(args[index]);
            continue;
        }
        if (index + 1u >= args.size() || args[index + 1u] == nullptr)
        {
            nr::nrLog<nr::LogLevel::warning>("--seed requires an unsigned 32-bit value.");
            return std::nullopt;
        }
        auto const seedText = std::string_view{args[index + 1u]};
        auto parsed = std::uint32_t{};
        auto const result = std::from_chars(seedText.data(), seedText.data() + seedText.size(), parsed);
        if (result.ec != std::errc{} || result.ptr != seedText.data() + seedText.size())
        {
            nr::nrLog<nr::LogLevel::warning>("--seed value '{}' is not an unsigned 32-bit integer.", seedText);
            return std::nullopt;
        }
        seed = parsed;
        ++index;
    }
    return std::pair{seed, std::move(remaining)};
}

[[nodiscard]] std::optional<std::filesystem::path> parsePathArgument(std::span<char *> args, std::size_t index,
                                                                     std::string_view optionName)
{
    if (index >= args.size() || args[index] == nullptr)
    {
        nr::nrLog<nr::LogLevel::warning>("{} requires a non-empty path.", optionName);
        return std::nullopt;
    }

    auto const value = std::string_view{args[index]};
    if (value.empty() || value.starts_with('-'))
    {
        nr::nrLog<nr::LogLevel::warning>("{} requires a non-empty path.", optionName);
        return std::nullopt;
    }
    return std::filesystem::path{value};
}

[[nodiscard]] std::optional<std::filesystem::path::string_type> comparablePathKey(const std::filesystem::path &path)
{
    auto error = std::error_code{};
    auto const normalizedPath = std::filesystem::weakly_canonical(path, error);
    if (error)
    {
        nr::nrLog<nr::LogLevel::warning>("Failed to resolve path '{}': {}.", path.string(), error.message());
        return std::nullopt;
    }

    auto key = normalizedPath.native();
    std::ranges::transform(key, key.begin(), [](std::filesystem::path::value_type character) {
        return static_cast<std::filesystem::path::value_type>(std::towlower(character));
    });
    return key;
}

[[nodiscard]] std::optional<CommandLineOptions> makeTrainingCommandLineOptions(std::filesystem::path artifactPath,
                                                                               std::filesystem::path checkpointPath,
                                                                               bool checkpointMustExist)
{
    if (artifactPath.extension() != ".nart")
    {
        nr::nrLog<nr::LogLevel::warning>(
            "Neural material production artifacts must use the .nart extension (received '{}').", artifactPath.string());
        return std::nullopt;
    }
    auto checkpointSlot0Path = checkpointPath;
    checkpointSlot0Path += ".0";
    auto checkpointSlot1Path = checkpointPath;
    checkpointSlot1Path += ".1";

    auto const artifactKey = comparablePathKey(artifactPath);
    auto const checkpointKey = comparablePathKey(checkpointPath);
    auto const checkpointSlot0Key = comparablePathKey(checkpointSlot0Path);
    auto const checkpointSlot1Key = comparablePathKey(checkpointSlot1Path);
    if (!artifactKey || !checkpointKey || !checkpointSlot0Key || !checkpointSlot1Key)
    {
        return std::nullopt;
    }

    auto conflictingCheckpointPath = std::optional<std::reference_wrapper<const std::filesystem::path>>{};
    if (*artifactKey == *checkpointKey)
    {
        conflictingCheckpointPath = std::cref(checkpointPath);
    }
    else if (*artifactKey == *checkpointSlot0Key)
    {
        conflictingCheckpointPath = std::cref(checkpointSlot0Path);
    }
    else if (*artifactKey == *checkpointSlot1Key)
    {
        conflictingCheckpointPath = std::cref(checkpointSlot1Path);
    }

    if (conflictingCheckpointPath)
    {
        nr::nrLog<nr::LogLevel::warning>(
            "Artifact path '{}' conflicts with checkpoint storage path '{}'; they must be different files.",
            artifactPath.string(), conflictingCheckpointPath->get().string());
        return std::nullopt;
    }

    auto training = TrainingCommandLineOptions{
        .artifactPath = std::move(artifactPath),
        .checkpointPath = std::move(checkpointPath),
        .checkpointMustExist = checkpointMustExist,
    };
    return CommandLineOptions{.training = std::move(training)};
}

[[nodiscard]] std::optional<CommandLineOptions> parseCommandLine(std::span<char *> args)
{
    if (args.empty())
    {
        return CommandLineOptions{};
    }

    auto const first = args.front() != nullptr ? std::string_view{args.front()} : std::string_view{};
    if (first == "--help" || first == "-h")
    {
        if (args.size() != 1u)
        {
            nr::nrLog<nr::LogLevel::warning>("The help option does not accept additional arguments.");
            return std::nullopt;
        }
        return CommandLineOptions{.showHelp = true};
    }

    if (first == "--train-and-save")
    {
        auto artifactPath = parsePathArgument(args, 1u, "--train-and-save");
        if (!artifactPath)
        {
            return std::nullopt;
        }

        auto checkpointPath = *artifactPath;
        checkpointPath += ".checkpoint";
        if (args.size() == 2u)
        {
            return makeTrainingCommandLineOptions(std::move(*artifactPath), std::move(checkpointPath), false);
        }

        auto const checkpointOption =
            args.size() > 2u && args[2] != nullptr ? std::string_view{args[2]} : std::string_view{};
        if (checkpointOption != "--checkpoint")
        {
            nr::nrLog<nr::LogLevel::warning>("Unexpected neuralMaterialViewer argument '{}'.", checkpointOption);
            return std::nullopt;
        }

        auto explicitCheckpointPath = parsePathArgument(args, 3u, "--checkpoint");
        if (!explicitCheckpointPath)
        {
            return std::nullopt;
        }
        if (args.size() != 4u)
        {
            auto const unexpected = args[4] != nullptr ? std::string_view{args[4]} : std::string_view{};
            nr::nrLog<nr::LogLevel::warning>("Unexpected neuralMaterialViewer argument '{}'.", unexpected);
            return std::nullopt;
        }
        return makeTrainingCommandLineOptions(std::move(*artifactPath), std::move(*explicitCheckpointPath), false);
    }

    if (first == "--resume")
    {
        auto checkpointPath = parsePathArgument(args, 1u, "--resume");
        if (!checkpointPath)
        {
            return std::nullopt;
        }

        auto const trainingOption =
            args.size() > 2u && args[2] != nullptr ? std::string_view{args[2]} : std::string_view{};
        if (trainingOption != "--train-and-save")
        {
            nr::nrLog<nr::LogLevel::warning>("--resume must be followed by --train-and-save <artifact-path>.");
            return std::nullopt;
        }

        auto artifactPath = parsePathArgument(args, 3u, "--train-and-save");
        if (!artifactPath)
        {
            return std::nullopt;
        }
        if (args.size() != 4u)
        {
            auto const unexpected = args[4] != nullptr ? std::string_view{args[4]} : std::string_view{};
            nr::nrLog<nr::LogLevel::warning>("Unexpected neuralMaterialViewer argument '{}'.", unexpected);
            return std::nullopt;
        }
        return makeTrainingCommandLineOptions(std::move(*artifactPath), std::move(*checkpointPath), true);
    }

    nr::nrLog<nr::LogLevel::warning>("Unknown neuralMaterialViewer argument '{}'.", first);
    return std::nullopt;
}

[[nodiscard]] nr::options::OptionFrameSnapshot makeDefaultSnapshot(
    std::shared_ptr<const nr::options::OptionCatalog> catalog)
{
    nr::nrAssert(static_cast<bool>(catalog), "Neural material viewer requires a valid option catalog.");

    auto values = nr::options::OptionValueMap{};
    auto availability = nr::options::OptionAvailabilityMap{};
    std::ranges::for_each(catalog->definitions(), [&](const auto &entry) {
        values.emplace(entry.first, entry.second.defaultValue);
        availability.emplace(entry.first, nr::options::OptionAvailability{.available = true, .reason = {}});
    });

    return nr::options::OptionFrameSnapshot{
        .catalog = std::move(catalog),
        .values = std::move(values),
        .availability = std::move(availability),
        .frameIndex = 1u,
        .revision = 1u,
        .graphGeneration = 1u,
        .bindingEpoch = 1u,
        .snapshotToken = "neural-material-viewer-snapshot",
    };
}

[[nodiscard]] nr::renderer::RendererGraphSpec buildViewerGraphSpec(
    const std::shared_ptr<nr::renderPasses::NeuralAppearanceNode> &neuralAppearanceNode, bool interactive)
{
    nr::nrAssert(static_cast<bool>(neuralAppearanceNode), "Neural material viewer requires a valid neural node.");

    auto graphSpec = nr::renderer::RendererGraphSpec{};
    if (interactive)
    {
        graphSpec.nodes.push_back(nr::renderer::NodeCreateInfo{
            .runtime = std::make_shared<nr::renderPasses::UiNode>(),
            .config =
                nr::renderer::NodeConfig{
                    .instanceName = "Ui",
                },
        });
        graphSpec.submitNodes.push_back(nr::renderer::SubmitNodeSpec{
            .debugName = "NeuralMaterialViewer.GraphicsToCompute",
            .afterNodeIndex = 0u,
        });
    }
    graphSpec.nodes.push_back(nr::renderer::NodeCreateInfo{
        .runtime = neuralAppearanceNode,
        .config =
            nr::renderer::NodeConfig{
                .instanceName = "NeuralAppearance",
                .queue = nr::renderer::QueueDomain::Compute,
            },
    });
    graphSpec.nodes.push_back(nr::renderer::NodeCreateInfo{
        .runtime = std::make_shared<nr::renderPasses::PresentNode>(),
        .config =
            nr::renderer::NodeConfig{
                .instanceName = "Present",
                .queue = nr::renderer::QueueDomain::Compute,
            },
    });
    return graphSpec;
}

[[nodiscard]] int runViewer(const std::optional<TrainingCommandLineOptions> &trainingOptions,
                            std::uint32_t trainingSeed)
{
    auto app = nr::app::AppSession{};
    app.initialize(nr::renderer::RendererCreateInfo{
        .appName = "Neural Material Viewer",
        .engineName = "NewbieRenderer",
        .debugShaderInstrumentationEnabled = false,
    });
    app.resetCameraFromSceneOrDefault();
    auto const interactive = !trainingOptions;
    if (!interactive)
    {
        std::println("Training the complete neural appearance budget; artifact='{}', checkpoint='{}'.",
                     trainingOptions->artifactPath.string(), trainingOptions->checkpointPath.string());
    }
    else
    {
        std::println("View: left native, middle neural, right absolute error.");
    }

    auto &renderer = app.renderer();
    auto &presentation = renderer.device().presentationContext;
    auto neuralAppearanceNode =
        std::make_shared<nr::renderPasses::NeuralAppearanceNode>(interactive, trainingSeed);
    auto graphSpec = buildViewerGraphSpec(neuralAppearanceNode, interactive);
    auto const preflight = renderer.preflightGraph(graphSpec);
    nr::nrAssert(static_cast<bool>(preflight), "Neural material viewer graph preflight failed.");
    nr::nrAssert(renderer.installGraph(graphSpec), "Neural material viewer graph installation failed.");

    if (trainingOptions)
    {
        auto const checkpointExists =
            nr::renderPasses::NeuralAppearanceNode::trainingCheckpointExists(trainingOptions->checkpointPath);
        if (trainingOptions->checkpointMustExist && !checkpointExists)
        {
            nr::nrLog<nr::LogLevel::warning>("Required checkpoint '{}' does not exist.",
                                             trainingOptions->checkpointPath.string());
            app.shutdown();
            return 1;
        }
        if (checkpointExists)
        {
            if (!neuralAppearanceNode->loadTrainingCheckpoint(renderer.device(), trainingOptions->checkpointPath))
            {
                nr::nrLog<nr::LogLevel::warning>("Failed to load neural appearance checkpoint '{}'.",
                                                 trainingOptions->checkpointPath.string());
                app.shutdown();
                return 1;
            }
            std::println("Resumed checkpoint '{}': GPU-completed step={}.", trainingOptions->checkpointPath.string(),
                         neuralAppearanceNode->lastScheduledTrainingStep());
        }
        else
        {
            std::println("No checkpoint found at '{}'; starting fresh.", trainingOptions->checkpointPath.string());
        }
    }

    auto optionSnapshot = makeDefaultSnapshot(preflight.optionCatalog);
    auto frameServices = app.makeFrameServices();
    auto previousTick = std::chrono::steady_clock::now();
    auto framebufferWasUnavailable = false;
    auto const trainingStart = std::chrono::steady_clock::now();
    auto renderedFrameCount = std::uint64_t{0u};

    while (!presentation.windowShouldClose())
    {
        presentation.windowInput().pollEvents();

        auto const now = std::chrono::steady_clock::now();
        auto const deltaSeconds = std::chrono::duration<float>(now - previousTick).count();
        previousTick = now;
        if (!presentation.framebufferAvailable())
        {
            framebufferWasUnavailable = true;
            std::this_thread::sleep_for(std::chrono::milliseconds{16});
            continue;
        }

        if (framebufferWasUnavailable)
        {
            renderer.resize();
            framebufferWasUnavailable = false;
        }

        if (interactive)
        {
            app.ui().beginFrame(presentation, deltaSeconds);
            app.ui().setCameraFrame(app.camera().frame());
            static_cast<void>(app.ui().finalizeFrame());
        }

        auto const frameResult = renderer.renderFrame(nr::renderer::RendererFrameInput{
            .optionSnapshot = std::cref(optionSnapshot),
            .acquireTimeout = std::numeric_limits<std::uint64_t>::max(),
            .cameraOverride = app.camera().buildRendererCameraOverride(),
            .frameServices = std::ref(frameServices),
        });
        if (!frameResult.rendered)
        {
            nr::nrLog<nr::LogLevel::warning>("Neural material viewer renderer returned rendered=false.");
            app.shutdown();
            return 1;
        }
        ++renderedFrameCount;
        nr::nrAssert(optionSnapshot.frameIndex != std::numeric_limits<std::uint64_t>::max(),
                     "Neural material viewer frame index exhausted.");
        ++optionSnapshot.frameIndex;

        if (nr::rhi::PresentationContext::needsSwapchainRecreate(frameResult.presentResult))
        {
            renderer.resize();
        }
        else if (frameResult.presentResult != vk::Result::eSuccess)
        {
            nr::nrLog<nr::LogLevel::warning>("Neural material viewer received unexpected present result {}.",
                                             vk::to_string(frameResult.presentResult));
            app.shutdown();
            return 1;
        }

        if (trainingOptions && neuralAppearanceNode->trainingComplete())
        {
            auto &device = renderer.device();
            auto const completedStep =
                neuralAppearanceNode->saveTrainingCheckpoint(device, trainingOptions->checkpointPath);
            if (!completedStep || *completedStep != nr::renderPasses::NeuralAppearanceNode::totalTrainingStepCount())
            {
                nr::nrLog<nr::LogLevel::warning>(
                    "Failed to save a complete neural appearance checkpoint '{}' at GPU step {}.",
                    trainingOptions->checkpointPath.string(), nr::renderPasses::NeuralAppearanceNode::totalTrainingStepCount());
                app.shutdown();
                return 1;
            }

            auto const checkpointElapsedSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - trainingStart).count();
            std::println(
                "Final checkpoint saved: GPU-completed step={}, rendered frames={}, elapsed={:.3f}s, path='{}'.",
                *completedStep, renderedFrameCount, checkpointElapsedSeconds, trainingOptions->checkpointPath.string());

            if (!neuralAppearanceNode->saveTrainingArtifact(device, trainingOptions->artifactPath))
            {
                nr::nrLog<nr::LogLevel::warning>("Failed to save the neural appearance artifact to '{}'.",
                                                 trainingOptions->artifactPath.string());
                app.shutdown();
                return 1;
            }

            auto const elapsedSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - trainingStart).count();
            std::println("Training complete: rendered frames={}, elapsed={:.3f}s, final step={}, artifact='{}'.",
                         renderedFrameCount, elapsedSeconds, *completedStep, trainingOptions->artifactPath.string());

            if (!nr::renderPasses::NeuralAppearanceNode::removeTrainingCheckpoint(trainingOptions->checkpointPath,
                                                                                  trainingOptions->artifactPath))
            {
                nr::nrLog<nr::LogLevel::warning>(
                    "Artifact saved, but checkpoint files for '{}' may remain because cleanup did not complete.",
                    trainingOptions->checkpointPath.string());
            }
            else
            {
                std::println("Removed completed-training checkpoint '{}'.", trainingOptions->checkpointPath.string());
            }
            app.shutdown();
            return 0;
        }

        if (trainingOptions && renderedFrameCount % kCheckpointFrameInterval == 0u)
        {
            auto const completedStep =
                neuralAppearanceNode->saveTrainingCheckpoint(renderer.device(), trainingOptions->checkpointPath);
            if (!completedStep)
            {
                nr::nrLog<nr::LogLevel::warning>(
                    "Failed to save neural appearance checkpoint '{}' after {} rendered frames.",
                    trainingOptions->checkpointPath.string(), renderedFrameCount);
                app.shutdown();
                return 1;
            }

            auto const elapsedSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - trainingStart).count();
            std::println("Checkpoint saved: GPU-completed step={}, rendered frames={}, elapsed={:.3f}s, path='{}'.",
                         *completedStep, renderedFrameCount, elapsedSeconds, trainingOptions->checkpointPath.string());
        }
    }

    if (trainingOptions)
    {
        nr::nrLog<nr::LogLevel::warning>("Neural material training was cancelled before completion.");
        app.shutdown();
        return 1;
    }

    app.shutdown();
    return 0;
}
} // namespace

int main(int argc, char **argv)
{
    auto args = std::span<char *>{};
    if (argc > 1)
    {
        args = std::span<char *>{argv + 1, static_cast<std::size_t>(argc - 1)};
    }

    auto seedExtraction = extractSeedOption(args);
    if (!seedExtraction)
    {
        printUsage();
        return 2;
    }

    auto const options = parseCommandLine(std::span<char *>{seedExtraction->second});
    if (!options)
    {
        printUsage();
        return 2;
    }

    if (options->showHelp)
    {
        printUsage();
        return 0;
    }

    return runViewer(options->training, seedExtraction->first);
}
