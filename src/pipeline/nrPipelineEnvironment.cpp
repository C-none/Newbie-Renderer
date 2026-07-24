module nr.pipeline;

import nr.load;
import nr.renderer;
import nr.utils;
import std;

namespace nr::pipeline
{
namespace
{
[[nodiscard]] bool isSupportedEnvironmentMapAsset(const std::filesystem::path& sourcePath)
{
    auto extension = sourcePath.extension().string();
    std::ranges::transform(extension, extension.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return extension == ".exr";
}

[[nodiscard]] std::expected<EnvironmentMapAsset, std::string> resolveEnvironmentMapAsset(
    const std::filesystem::path& sourcePath)
{
    if (sourcePath.empty())
    {
        return std::unexpected("Environment map path is empty.");
    }
    if (!isSupportedEnvironmentMapAsset(sourcePath))
    {
        return std::unexpected(std::format(
            "Environment map '{}' is not a supported OpenEXR asset.",
            sourcePath.generic_string()));
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

    auto const requestedSourcePath = sourcePath.is_absolute()
                                         ? sourcePath
                                         : std::filesystem::path{std::string{nr::projectRoot}} / sourcePath;
    auto const resolvedSourcePath = std::filesystem::canonical(requestedSourcePath, pathError);
    if (pathError)
    {
        return std::unexpected(std::format(
            "Failed to resolve environment map asset '{}': {}",
            sourcePath.generic_string(),
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
            sourcePath.generic_string()));
    }

    return EnvironmentMapAsset{
        .sourcePath = resolvedSourcePath,
        .displayName = resolvedSourcePath.stem().string(),
    };
}
} // namespace

[[nodiscard]] std::filesystem::path environmentMapAssetDirectoryPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           "assets" /
           "envMap";
}

[[nodiscard]] std::filesystem::path defaultEnvironmentMapPath()
{
    return environmentMapAssetDirectoryPath() /
           "kloofendal_48d_partly_cloudy_puresky_8k.exr";
}

[[nodiscard]] std::expected<std::vector<EnvironmentMapAsset>, std::string> discoverEnvironmentMapAssets()
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

    auto assets = std::vector<EnvironmentMapAsset>{};
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
            auto asset = resolveEnvironmentMapAsset(iterator->path());
            if (!asset)
            {
                return std::unexpected(std::move(asset.error()));
            }
            assets.push_back(std::move(*asset));
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

    std::ranges::sort(assets, {}, &EnvironmentMapAsset::displayName);
    return assets;
}
} // namespace nr::pipeline

namespace nr::pipeline::detail
{
[[nodiscard]] std::expected<EnvironmentMapAsset, std::string> loadEnvironmentMap(
    nr::renderer::Renderer& renderer,
    const std::filesystem::path& sourcePath)
{
    auto asset = resolveEnvironmentMapAsset(sourcePath);
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
            "Loaded environment map: {} ({}x{}, RGBA16F, decode scale {})",
            asset->sourcePath.generic_string(),
            width,
            height,
            decodeScale));
    return asset;
}
} // namespace nr::pipeline::detail
