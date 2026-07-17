module nr.pipeline;

import nr.load;
import nr.renderer;
import nr.utils;
import std;

namespace nr::pipeline
{
[[nodiscard]] std::filesystem::path defaultEnvironmentMapPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} /
           "assets" /
           "envMap" /
           "studio_small_09_8k.exr";
}
} // namespace nr::pipeline

namespace nr::pipeline::detail
{
[[nodiscard]] std::expected<void, std::string> loadEnvironmentMap(
    nr::renderer::Renderer& renderer,
    const std::filesystem::path& sourcePath)
{
    auto result = nr::load::loadExrEnvironmentMap(nr::load::ExrEnvironmentLoadRequest{
        .sourcePath = sourcePath,
    });
    if (!result)
    {
        return std::unexpected(std::format(
            "Failed to load environment map '{}': {}",
            sourcePath.generic_string(),
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
            sourcePath.generic_string(),
            width,
            height,
            decodeScale));
    return {};
}
} // namespace nr::pipeline::detail
