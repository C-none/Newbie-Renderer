export module nr.load:exr;

import nr.resource;
import std;
import :type;

export namespace nr::load
{
inline constexpr float defaultEnvironmentHalfSafeMaximum = 60'000.0f;

struct ExrEnvironmentLoadRequest
{
    std::filesystem::path sourcePath{};
    float halfSafeMaximum = defaultEnvironmentHalfSafeMaximum;
};

using EnvironmentMapLoadResult = std::expected<nr::resource::EnvironmentMap, LoadError>;

/**
 * @brief Load a single-part, scanline OpenEXR RGB(A) latitude-longitude map.
 *
 * HALF and FLOAT source channels are decoded as linear RGB. Negative radiance
 * is clamped to zero. The full image is scaled into a safe HALF range and
 * stored as RGBA16F; radianceDecodeScale restores source radiance in shaders.
 * Alpha and unrelated extra channels are ignored, and output alpha is one.
 */
[[nodiscard]] EnvironmentMapLoadResult loadExrEnvironmentMap(const ExrEnvironmentLoadRequest &request);
} // namespace nr::load
