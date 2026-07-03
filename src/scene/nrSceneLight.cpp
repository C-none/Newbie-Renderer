module nr.scene;
import :light;

import dependency.math;
import std;

namespace nr::scene
{
namespace
{
inline constexpr float kSceneLightAliasLuminanceRed = 0.2126f;
inline constexpr float kSceneLightAliasLuminanceGreen = 0.7152f;
inline constexpr float kSceneLightAliasLuminanceBlue = 0.0722f;

[[nodiscard]] float finitePositive(float value) noexcept
{
    return std::isfinite(value) && value > 0.0f ? value : 0.0f;
}

[[nodiscard]] SceneLightAliasGpuRecord makeAliasRecord(
    std::uint32_t primaryIndex,
    std::uint32_t aliasIndex,
    float acceptThreshold,
    float primaryPdf,
    float aliasPdf) noexcept
{
    return SceneLightAliasGpuRecord{
        .meta = glm::uvec4{
            primaryIndex,
            aliasIndex,
            0u,
            0u,
        },
        .probabilities = glm::vec4{
            std::clamp(acceptThreshold, 0.0f, 1.0f),
            finitePositive(primaryPdf),
            finitePositive(aliasPdf),
            0.0f,
        },
    };
}
} // namespace

[[nodiscard]] float sceneLightAliasEnergy(glm::vec3 color, float intensity) noexcept
{
    auto const positiveColor = glm::vec3{
        finitePositive(color.r),
        finitePositive(color.g),
        finitePositive(color.b),
    };
    auto const positiveIntensity = finitePositive(intensity);
    auto const luminance =
        positiveColor.r * kSceneLightAliasLuminanceRed +
        positiveColor.g * kSceneLightAliasLuminanceGreen +
        positiveColor.b * kSceneLightAliasLuminanceBlue;
    auto const energy = positiveIntensity * luminance;
    return finitePositive(energy);
}

[[nodiscard]] float sceneLightAliasEnergy(const SceneLightGpuRecord& record) noexcept
{
    return sceneLightAliasEnergy(glm::vec3{record.colorIntensity}, record.colorIntensity.w);
}

[[nodiscard]] SceneLightAliasTableBuildResult buildSceneLightAliasTable(
    std::span<const SceneLightGpuRecord> records)
{
    auto result = SceneLightAliasTableBuildResult{};
    auto const lightCount = records.size();
    if (lightCount == 0u ||
        lightCount > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()))
    {
        result.records.push_back(SceneLightAliasGpuRecord{});
        return result;
    }

    auto weights = records |
                   std::views::transform([](const SceneLightGpuRecord& record) {
                       return sceneLightAliasEnergy(record);
                   }) |
                   std::ranges::to<std::vector>();
    auto const totalEnergy = std::reduce(weights.begin(), weights.end(), 0.0f);
    result.totalEnergy = finitePositive(totalEnergy);
    if (result.totalEnergy <= 0.0f)
    {
        result.records.push_back(SceneLightAliasGpuRecord{});
        return result;
    }

    result.aliasCount = static_cast<std::uint32_t>(lightCount);
    result.records.resize(lightCount);

    auto pdfs = weights |
                std::views::transform([total = result.totalEnergy](float weight) {
                    return finitePositive(weight / total);
                }) |
                std::ranges::to<std::vector>();
    auto scaled = weights |
                  std::views::transform([count = static_cast<float>(lightCount), total = result.totalEnergy](float weight) {
                      return finitePositive(weight * count / total);
                  }) |
                  std::ranges::to<std::vector>();

    auto small = std::vector<std::uint32_t>{};
    auto large = std::vector<std::uint32_t>{};
    small.reserve(lightCount);
    large.reserve(lightCount);

    auto const indices = std::views::iota(std::uint32_t{0}, result.aliasCount);
    std::ranges::for_each(indices, [&](std::uint32_t index) {
        if (scaled[index] < 1.0f)
        {
            small.push_back(index);
        }
        else
        {
            large.push_back(index);
        }
    });

    while (!small.empty() && !large.empty())
    {
        auto const primaryIndex = small.back();
        small.pop_back();
        auto const aliasIndex = large.back();
        large.pop_back();

        result.records[primaryIndex] = makeAliasRecord(
            primaryIndex,
            aliasIndex,
            scaled[primaryIndex],
            pdfs[primaryIndex],
            pdfs[aliasIndex]);

        scaled[aliasIndex] = scaled[aliasIndex] + scaled[primaryIndex] - 1.0f;
        if (scaled[aliasIndex] < 1.0f)
        {
            small.push_back(aliasIndex);
        }
        else
        {
            large.push_back(aliasIndex);
        }
    }

    auto fillRemainder = [&](std::span<const std::uint32_t> remainder) {
        std::ranges::for_each(remainder, [&](std::uint32_t index) {
            result.records[index] = makeAliasRecord(index, index, 1.0f, pdfs[index], pdfs[index]);
        });
    };
    fillRemainder(std::span<const std::uint32_t>{small.data(), small.size()});
    fillRemainder(std::span<const std::uint32_t>{large.data(), large.size()});

    return result;
}
} // namespace nr::scene
