module nr.pipeline;

import nr.load;
import nr.renderer;
import nr.utils;
import std;

namespace nr::pipeline
{
namespace
{
inline constexpr auto environmentMapAssetPrefix = std::string_view{"assets/envMap"};
inline constexpr auto defaultEnvironmentName =
    std::string_view{"kloofendal_48d_partly_cloudy_puresky_8k"};

struct EnvironmentMapAsset
{
    std::filesystem::path sourcePath{};
    std::string name{};
};

[[nodiscard]] std::filesystem::path environmentMapAssetDirectoryPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           std::filesystem::path{std::string{environmentMapAssetPrefix}};
}

[[nodiscard]] bool isSupportedEnvironmentMapAsset(const std::filesystem::path& sourcePath)
{
    auto extension = sourcePath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return extension == ".exr";
}

[[nodiscard]] bool isEnvironmentMapName(std::string_view name)
{
    if (name.empty() || name.contains('\0'))
    {
        return false;
    }

    auto const path = std::filesystem::path{std::string{name}};
    return !path.has_root_name() &&
           !path.has_root_directory() &&
           path == path.filename() &&
           path != "." &&
           path != "..";
}

[[nodiscard]] std::expected<EnvironmentMapAsset, std::string> resolveEnvironmentMapAsset(
    std::string_view name)
{
    if (!isEnvironmentMapName(name))
    {
        return std::unexpected(
            std::format("Invalid environment map name: '{}'.", name));
    }

    auto pathError = std::error_code{};
    auto const assetDirectory = std::filesystem::canonical(
        environmentMapAssetDirectoryPath(),
        pathError);
    if (pathError)
    {
        return std::unexpected(std::format(
            "Failed to resolve environment map asset directory '{}': {}",
            environmentMapAssetDirectoryPath().generic_string(),
            pathError.message()));
    }

    auto const requestedSourcePath =
        assetDirectory / std::filesystem::path{std::format("{}.exr", name)};
    auto const resolvedSourcePath = std::filesystem::canonical(requestedSourcePath, pathError);
    if (pathError)
    {
        return std::unexpected(std::format(
            "Failed to resolve environment map '{}': {}",
            name,
            pathError.message()));
    }

    auto const sourceIsRegularFile = std::filesystem::is_regular_file(resolvedSourcePath, pathError);
    if (pathError || !sourceIsRegularFile)
    {
        return std::unexpected(std::format(
            "Environment map asset is not a regular file: '{}'.",
            resolvedSourcePath.generic_string()));
    }

    auto const sourceParentMatchesAssetDirectory = std::filesystem::equivalent(
        resolvedSourcePath.parent_path(),
        assetDirectory,
        pathError);
    if (pathError || !sourceParentMatchesAssetDirectory)
    {
        return std::unexpected(std::format(
            "Environment maps must be direct files under '{}': '{}'.",
            assetDirectory.generic_string(),
            name));
    }

    return EnvironmentMapAsset{
        .sourcePath = resolvedSourcePath,
        .name = std::string{name},
    };
}
} // namespace

[[nodiscard]] std::string_view defaultEnvironmentMapName() noexcept
{
    return defaultEnvironmentName;
}

[[nodiscard]] std::expected<std::vector<std::string>, std::string> discoverEnvironmentMapNames()
{
    auto const assetDirectory = environmentMapAssetDirectoryPath();
    auto iterationError = std::error_code{};
    auto iterator = std::filesystem::directory_iterator{
        assetDirectory,
        std::filesystem::directory_options::skip_permission_denied,
        iterationError};
    if (iterationError)
    {
        return std::unexpected(std::format(
            "Failed to enumerate environment map assets under '{}': {}",
            assetDirectory.generic_string(),
            iterationError.message()));
    }

    auto names = std::vector<std::string>{};
    auto const end = std::filesystem::directory_iterator{};
    while (iterator != end)
    {
        auto statusError = std::error_code{};
        auto const status = iterator->symlink_status(statusError);
        if (statusError)
        {
            return std::unexpected(std::format(
                "Failed to inspect environment map asset '{}': {}",
                iterator->path().generic_string(),
                statusError.message()));
        }

        if (std::filesystem::is_regular_file(status) &&
            isSupportedEnvironmentMapAsset(iterator->path()))
        {
            auto const name = iterator->path().stem().string();
            if (!isEnvironmentMapName(name))
            {
                return std::unexpected(std::format(
                    "Environment map asset has an invalid selectable name: '{}'.",
                    iterator->path().generic_string()));
            }
            names.push_back(name);
        }

        iterator.increment(iterationError);
        if (iterationError)
        {
            return std::unexpected(std::format(
                "Failed while enumerating environment map assets under '{}': {}",
                assetDirectory.generic_string(),
                iterationError.message()));
        }
    }

    std::ranges::sort(names);
    auto const duplicate = std::ranges::adjacent_find(names);
    if (duplicate != names.end())
    {
        return std::unexpected(std::format(
            "Environment map asset names must be unique after removing '.exr': '{}'.",
            *duplicate));
    }
    return names;
}
} // namespace nr::pipeline

namespace nr::pipeline::detail
{
[[nodiscard]] std::expected<void, std::string> loadEnvironmentMap(
    nr::renderer::Renderer& renderer,
    std::string_view environmentMapName)
{
    auto asset = resolveEnvironmentMapAsset(environmentMapName);
    if (!asset)
    {
        return std::unexpected(std::move(asset.error()));
    }

    auto result = nr::load::loadExrEnvironmentMap(nr::load::ExrEnvironmentLoadRequest{
        .sourcePath = asset->sourcePath,
    });
    if (!result)
    {
        return std::unexpected(std::format(
            "Failed to load environment map '{}': {}",
            asset->sourcePath.generic_string(),
            result.error().message));
    }

    auto const width = result->radiance.width;
    auto const height = result->radiance.height;
    auto const decodeScale = result->radianceDecodeScale;
    renderer.setEnvironmentMap(std::move(*result));
    nr::nrLog(
        nr::LogLevel::info,
        "PIPELINE",
        std::format(
            "Loaded environment map '{}': {} ({}x{}, RGBA16F, decode scale {})",
            asset->name,
            asset->sourcePath.generic_string(),
            width,
            height,
            decodeScale));
    return {};
}
} // namespace nr::pipeline::detail
