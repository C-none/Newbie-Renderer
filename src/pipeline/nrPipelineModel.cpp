module nr.pipeline;

import nr.app;
import nr.load;
import nr.scene;
import nr.utils;
import std;

namespace nr::pipeline
{
ModelHistory::ModelHistory(std::filesystem::path storagePath, std::size_t maxEntries)
    : storagePath_(std::move(storagePath)), maxEntries_(std::max<std::size_t>(1u, maxEntries))
{
}

void ModelHistory::load()
{
    entries_.clear();

    auto input = std::ifstream{storagePath_};
    if (!input)
    {
        return;
    }

    auto line = std::string{};
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        if (line.empty())
        {
            continue;
        }
        auto normalized = normalizeModelPathForStorage(std::filesystem::path{line});
        if (normalized.empty())
        {
            continue;
        }
        auto duplicate = std::ranges::any_of(
            entries_, [&](const std::filesystem::path &entry) { return sameStoredPath(entry, normalized); });
        if (!duplicate)
        {
            entries_.push_back(std::move(normalized));
        }
        trimToLimit();
    }
}

void ModelHistory::save() const
{
    auto ec = std::error_code{};
    std::filesystem::create_directories(storagePath_.parent_path(), ec);
    if (ec)
    {
        nr::nrLog<nr::LogLevel::warning, "PIPELINE">(
            "Failed to create model history directory '{}': {}", storagePath_.parent_path().string(), ec.message());
        return;
    }

    auto output = std::ofstream{storagePath_, std::ios::trunc};
    if (!output)
    {
        nr::nrLog<nr::LogLevel::warning, "PIPELINE">("Failed to write model history '{}'.", storagePath_.string());
        return;
    }

    std::ranges::for_each(entries_, [&](const std::filesystem::path &entry) { output << entry.string() << '\n'; });
}

void ModelHistory::noteLoaded(const std::filesystem::path &path)
{
    auto normalized = normalizeModelPathForStorage(path);
    if (normalized.empty())
    {
        return;
    }

    std::erase_if(entries_, [&](const std::filesystem::path &entry) { return sameStoredPath(entry, normalized); });
    entries_.insert(entries_.begin(), std::move(normalized));
    trimToLimit();
}

[[nodiscard]] std::span<const std::filesystem::path> ModelHistory::entries() const noexcept
{
    return std::span<const std::filesystem::path>{entries_.data(), entries_.size()};
}

[[nodiscard]] const std::filesystem::path &ModelHistory::storagePath() const noexcept
{
    return storagePath_;
}

[[nodiscard]] bool ModelHistory::sameStoredPath(const std::filesystem::path &lhs,
                                                const std::filesystem::path &rhs) const
{
    return detail::normalizedModelPathKey(lhs) == detail::normalizedModelPathKey(rhs);
}

void ModelHistory::trimToLimit()
{
    if (entries_.size() > maxEntries_)
    {
        entries_.resize(maxEntries_);
    }
}

[[nodiscard]] SceneModelController::ModelCpuLoadResult SceneModelController::loadModelCpu(
    const std::filesystem::path &modelPath)
{
    auto normalizedPath = normalizeModelPathForStorage(modelPath);
    if (normalizedPath.empty())
    {
        return std::unexpected(ModelLoadReport{
            .message = std::format("Model path is invalid or outside the assets root: {}", modelPath.generic_string()),
        });
    }

    auto resolvedPath = resolveModelAssetPath(normalizedPath);
    if (!resolvedPath)
    {
        return std::unexpected(ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::move(resolvedPath.error()),
        });
    }

    nr::nrLog<nr::LogLevel::info, "PIPELINE">("Loading model: {}", resolvedPath->string());
    auto sceneLoad = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = *resolvedPath,
    });
    if (!sceneLoad.has_value())
    {
        return std::unexpected(ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Failed to load model: {}", sceneLoad.error().message),
        });
    }

    auto neuralMaterialBinding = nr::neuralAppearance::loadBindingRequest(modelAssetRootPath(), normalizedPath);
    if (!neuralMaterialBinding)
    {
        return std::unexpected(ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Failed to load neural material binding: {}", neuralMaterialBinding.error()),
        });
    }
    if (neuralMaterialBinding->has_value())
    {
        auto bindingValidation = nr::neuralAppearance::validateBindingForScene(**neuralMaterialBinding, *sceneLoad);
        if (!bindingValidation)
        {
            return std::unexpected(ModelLoadReport{
                .modelPath = normalizedPath,
                .message = std::format("Neural material binding rejected: {}", bindingValidation.error()),
            });
        }
    }

    return ModelCpuLoad{std::move(normalizedPath), std::move(*sceneLoad), std::move(*neuralMaterialBinding)};
}

[[nodiscard]] ModelLoadReport SceneModelController::commitModel(
    nr::app::AppSession &app, SceneModelController::ModelCpuLoad &&loadedModel,
    std::optional<std::reference_wrapper<ModelHistory>> history)
{
    auto normalizedModelPath = std::move(loadedModel.normalizedModelPath_);
    auto sceneAsset = std::move(loadedModel.sceneAsset_);
    auto neuralMaterialBinding = std::move(loadedModel.neuralMaterialBinding_);
    auto candidate = app.makeSceneCandidate();
    auto templateHandle = candidate->registerTemplate(
        sceneAsset, nr::scene::SceneTemplateCreateInfo{
                        .neuralMaterialBinding = neuralMaterialBinding.transform([](nr::neuralAppearance::BindingRequest request) {
                            return nr::scene::NeuralAppearanceMaterialBinding{
                                .sourceMaterialIndex = request.sourceMaterialIndex,
                                .artifact = std::move(request.artifact),
                            };
                        }),
                    });
    if (!templateHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedModelPath,
            .message = "Failed to register scene template.",
        };
    }

    auto instanceHandle = candidate->instantiate(templateHandle);
    if (!instanceHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedModelPath,
            .message = "Failed to instantiate scene.",
        };
    }

    app.commitScene(std::move(candidate));
    app.resetCameraFromSceneOrDefault();
    currentModelPath_ = normalizedModelPath;

    if (history.has_value())
    {
        history->get().noteLoaded(normalizedModelPath);
        history->get().save();
    }

    auto message = std::format("Loaded: {} meshes, {} vertices, {} indices, {} lights", sceneAsset.stats.meshCount,
                               sceneAsset.stats.vertexCount, sceneAsset.stats.indexCount, sceneAsset.stats.lightCount);
    nr::nrLog<nr::LogLevel::info, "PIPELINE">("{}", message);
    return ModelLoadReport{
        .loaded = true,
        .modelPath = std::move(normalizedModelPath),
        .message = std::move(message),
    };
}

[[nodiscard]] ModelLoadReport SceneModelController::loadModel(
    nr::app::AppSession &app, const std::filesystem::path &modelPath,
    std::optional<std::reference_wrapper<ModelHistory>> history)
{
    auto loadedModel = loadModelCpu(modelPath);
    if (!loadedModel)
    {
        return std::move(loadedModel.error());
    }

    return commitModel(app, std::move(*loadedModel), history);
}

[[nodiscard]] const std::optional<std::filesystem::path> &SceneModelController::currentModelPath() const noexcept
{
    return currentModelPath_;
}
} // namespace nr::pipeline
