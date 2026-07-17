module nr.pipeline;

import nr.app;
import nr.load;
import nr.scene;
import nr.utils;
import std;

namespace nr::pipeline
{
ModelHistory::ModelHistory(std::filesystem::path storagePath, std::size_t maxEntries) : storagePath_(std::move(storagePath)), maxEntries_(std::max<std::size_t>(1u, maxEntries))
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
        auto duplicate = std::ranges::any_of(entries_, [&](const std::filesystem::path &entry) { return sameStoredPath(entry, normalized); });
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
        nr::nrLog(nr::LogLevel::warning, "PIPELINE", std::format("Failed to create model history directory '{}': {}", storagePath_.parent_path().string(), ec.message()));
        return;
    }

    auto output = std::ofstream{storagePath_, std::ios::trunc};
    if (!output)
    {
        nr::nrLog(nr::LogLevel::warning, "PIPELINE", std::format("Failed to write model history '{}'.", storagePath_.string()));
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

[[nodiscard]] bool ModelHistory::sameStoredPath(const std::filesystem::path &lhs, const std::filesystem::path &rhs) const
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

[[nodiscard]] ModelLoadReport SceneModelController::loadModel(nr::app::AppSession &app, const std::filesystem::path &modelPath, std::optional<std::reference_wrapper<ModelHistory>> history)
{
    auto normalizedPath = normalizeModelPathForStorage(modelPath);
    if (normalizedPath.empty())
    {
        return ModelLoadReport{
            .message = "Model path is empty.",
        };
    }

    auto ec = std::error_code{};
    if (!std::filesystem::exists(normalizedPath, ec) || ec)
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Model file not found: {}", normalizedPath.string()),
        };
    }

    nr::nrLog(nr::LogLevel::info, "PIPELINE", std::format("Loading model: {}", normalizedPath.string()));
    auto loadResult = nr::load::loadScene(nr::load::SceneLoadRequest{
        .sourcePath = normalizedPath,
    });
    if (!loadResult.has_value())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = std::format("Failed to load model: {}", loadResult.error().message),
        };
    }

    auto &sceneAsset = loadResult.value();
    auto &scene = app.createScene();
    auto templateHandle = scene.registerTemplate(sceneAsset);
    if (!templateHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = "Failed to register scene template.",
        };
    }

    auto instanceHandle = scene.instantiate(templateHandle);
    if (!instanceHandle.valid())
    {
        return ModelLoadReport{
            .modelPath = normalizedPath,
            .message = "Failed to instantiate scene.",
        };
    }

    app.resetCameraFromSceneOrDefault();
    currentModelPath_ = normalizedPath;

    if (history.has_value())
    {
        history->get().noteLoaded(normalizedPath);
        history->get().save();
    }

    auto message = std::format("Loaded: {} meshes, {} vertices, {} indices, {} lights", sceneAsset.stats.meshCount, sceneAsset.stats.vertexCount, sceneAsset.stats.indexCount, sceneAsset.stats.lightCount);
    nr::nrLog(nr::LogLevel::info, "PIPELINE", message);
    return ModelLoadReport{
        .loaded = true,
        .modelPath = normalizedPath,
        .message = std::move(message),
    };
}

[[nodiscard]] const std::optional<std::filesystem::path> &SceneModelController::currentModelPath() const noexcept
{
    return currentModelPath_;
}
} // namespace nr::pipeline
