export module nr.resource:environment;

import dependency.vulkan;
import std;
import :material;

export namespace nr::resource
{
enum class EnvironmentMapProjection : std::uint8_t
{
    latitudeLongitude,
};

enum class EnvironmentMapColorSpace : std::uint8_t
{
    linearSrgb,
};

struct EnvironmentMap
{
    Texture radiance{};
    EnvironmentMapProjection projection = EnvironmentMapProjection::latitudeLongitude;
    EnvironmentMapColorSpace colorSpace = EnvironmentMapColorSpace::linearSrgb;
    float radianceDecodeScale = 1.0f;
    float intensity = 1.0f;
    float yawRadians = 0.0f;

    [[nodiscard]] bool valid() const noexcept
    {
        if (!radiance.valid() ||
            radiance.dimension != vk::ImageType::e2D ||
            radiance.format != vk::Format::eR16G16B16A16Sfloat ||
            radiance.srgb ||
            radiance.mipCount != 1u ||
            radiance.levels.size() != 1u)
        {
            return false;
        }

        auto const texelCount =
            static_cast<std::uint64_t>(radiance.width) * static_cast<std::uint64_t>(radiance.height);
        auto const expectedByteCount = texelCount * 4u * sizeof(std::uint16_t);
        if (expectedByteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
            radiance.levels.front().bytes.size() != static_cast<std::size_t>(expectedByteCount))
        {
            return false;
        }

        return std::isfinite(radianceDecodeScale) && radianceDecodeScale > 0.0f &&
               std::isfinite(intensity) && intensity >= 0.0f &&
               std::isfinite(yawRadians);
    }
};
} // namespace nr::resource
