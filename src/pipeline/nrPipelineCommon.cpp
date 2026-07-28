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
    return modelAssetRootPath() / "glTF-Sample-Assets" / "Models" / "Sponza" / "glTF" / "Sponza.gltf";
}

[[nodiscard]] std::filesystem::path modelAssetRootPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "assets";
}

[[nodiscard]] std::filesystem::path modelHistoryFilePath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "build" / "app" / "model-history.txt";
}

[[nodiscard]] std::expected<std::filesystem::path, std::string> resolveModelAssetPath(
    const std::filesystem::path& path)
{
    if (path.empty())
    {
        return std::unexpected("Model path is empty.");
    }

    auto const rawPath = path.generic_string();
    auto const hasForbiddenPrefix =
        rawPath.starts_with("//") ||
        rawPath.starts_with(R"(\\)") ||
        rawPath.starts_with(R"(\\?\)") ||
        rawPath.starts_with(R"(\\.\)") ||
        rawPath.contains("://");
    auto const hasShellSyntax = rawPath.find_first_of("|&;<>\n\r\t\"'`$*?") != std::string::npos;
    if (hasForbiddenPrefix || hasShellSyntax)
    {
        return std::unexpected(std::format(
            "Model path contains a forbidden path or shell form: '{}'.",
            rawPath));
    }

    auto ec = std::error_code{};
    auto const assetRoot = std::filesystem::canonical(modelAssetRootPath(), ec);
    if (ec)
    {
        return std::unexpected(std::format(
            "Failed to resolve model asset root '{}': {}",
            modelAssetRootPath().generic_string(),
            ec.message()));
    }

    auto requested = path;
    if (!requested.is_absolute())
    {
        auto firstComponent = requested.begin();
        auto firstText = firstComponent == requested.end()
                             ? std::string{}
                             : firstComponent->string();
        std::ranges::transform(
            firstText,
            firstText.begin(),
            [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
        requested = firstText == "assets"
                        ? std::filesystem::path{std::string{nr::projectRoot}} / requested
                        : assetRoot / requested;
    }

    auto const resolved = std::filesystem::canonical(requested, ec);
    if (ec)
    {
        return std::unexpected(std::format(
            "Failed to resolve model asset '{}': {}",
            path.generic_string(),
            ec.message()));
    }
    if (!std::filesystem::is_regular_file(resolved, ec) || ec)
    {
        return std::unexpected(std::format(
            "Model asset is not a regular file: '{}'.",
            resolved.generic_string()));
    }

    auto const relative = resolved.lexically_relative(assetRoot);
    if (relative.empty() ||
        relative.is_absolute() ||
        *relative.begin() == "..")
    {
        return std::unexpected(std::format(
            "Model assets must remain under '{}': '{}'.",
            assetRoot.generic_string(),
            path.generic_string()));
    }

    return resolved;
}

[[nodiscard]] std::filesystem::path normalizeModelPathForStorage(const std::filesystem::path &path)
{
    auto resolved = resolveModelAssetPath(path);
    if (!resolved)
    {
        return {};
    }

    auto ec = std::error_code{};
    auto const assetRoot = std::filesystem::canonical(modelAssetRootPath(), ec);
    if (ec)
    {
        return {};
    }
    return resolved->lexically_relative(assetRoot).lexically_normal();
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
