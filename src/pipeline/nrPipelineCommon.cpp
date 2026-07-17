module nr.pipeline;

import nr.utils;
import std;

namespace nr::pipeline
{
[[nodiscard]] bool RenderPipelineRegistry::registerPipeline(RenderPipelineDesc desc)
{
    if (desc.id.empty() || !desc.buildGraph || indexById_.contains(desc.id))
    {
        return false;
    }

    if (desc.displayName.empty())
    {
        desc.displayName = desc.id;
    }

    auto const index = pipelines_.size();
    indexById_.emplace(desc.id, index);
    pipelines_.push_back(std::move(desc));
    return true;
}

[[nodiscard]] std::optional<std::reference_wrapper<const RenderPipelineDesc>> RenderPipelineRegistry::find(std::string_view id) const noexcept
{
    auto const it = indexById_.find(std::string{id});
    if (it == indexById_.end())
    {
        return std::nullopt;
    }

    nrAssert(it->second < pipelines_.size(), "RenderPipelineRegistry index is out of range.");
    return std::cref(pipelines_[it->second]);
}

[[nodiscard]] std::span<const RenderPipelineDesc> RenderPipelineRegistry::pipelines() const noexcept
{
    return std::span<const RenderPipelineDesc>{pipelines_.data(), pipelines_.size()};
}

[[nodiscard]] bool RenderPipelineRegistry::empty() const noexcept
{
    return pipelines_.empty();
}

[[nodiscard]] bool RenderPipelineRegistry::contains(std::string_view id) const noexcept
{
    return find(id).has_value();
}

void registerDefaultPipelines(RenderPipelineRegistry &registry)
{
    detail::registerNormalViewPipeline(registry);
    detail::registerRtObjectPipeline(registry);
}

[[nodiscard]] RenderPipelineRegistry makeDefaultPipelineRegistry()
{
    auto registry = RenderPipelineRegistry{};
    registerDefaultPipelines(registry);
    return registry;
}

[[nodiscard]] std::filesystem::path defaultModelPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "assets" / "glTF-Sample-Assets" / "Models" / "Sponza" / "glTF" / "Sponza.gltf";
}

[[nodiscard]] std::filesystem::path modelHistoryFilePath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "app" / "model-history.txt";
}

[[nodiscard]] std::filesystem::path normalizeModelPathForStorage(const std::filesystem::path &path)
{
    if (path.empty())
    {
        return {};
    }

    auto ec = std::error_code{};
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec)
    {
        return path.lexically_normal();
    }
    return absolute.lexically_normal();
}

[[nodiscard]] std::string displayPathLeafFirst(const std::filesystem::path &path)
{
    auto normalized = path.lexically_normal();
    auto parts = std::vector<std::string>{};
    std::ranges::for_each(normalized.relative_path(), [&](const std::filesystem::path &part) {
        auto text = part.string();
        if (!text.empty() && text != ".")
        {
            parts.push_back(std::move(text));
        }
    });

    if (parts.empty())
    {
        return path.string();
    }

    std::ranges::reverse(parts);
    auto root = normalized.root_path().string();
    if (!root.empty())
    {
        parts.push_back(std::move(root));
    }

    auto output = std::string{};
    std::ranges::for_each(parts, [&](const std::string &part) {
        if (!output.empty())
        {
            output += " / ";
        }
        output += part;
    });
    return output;
}
} // namespace nr::pipeline

namespace nr::pipeline::detail
{
[[nodiscard]] std::string normalizedModelPathKey(const std::filesystem::path &path)
{
    auto key = normalizeModelPathForStorage(path).string();
    std::ranges::transform(key, key.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return key;
}
} // namespace nr::pipeline::detail
