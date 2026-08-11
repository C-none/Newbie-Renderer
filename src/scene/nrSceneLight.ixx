export module nr.scene:light;
import dependency.math;

import nr.resource;
import std;

export namespace nr::scene
{
inline constexpr std::uint32_t kSceneLightGpuAbiVersion = 3u;

struct SceneLightGpuHeader
{
    std::uint32_t abiVersion = kSceneLightGpuAbiVersion;
    std::uint32_t lightCount = 0;
    std::uint32_t aliasCount = 0;
    // Sampling weight sum only; not a physical energy unit.
    float totalEnergy = 0.0f;
};

struct SceneLightGpuRecord
{
    // meta.x: LightType, meta.y: flags, meta.z: stable/debug instance id, meta.w: reserved.
    DirectX::XMUINT4 meta{};
    // colorIntensity.rgb: glTF unitless linear RGB color multiplier.
    // colorIntensity.w: glTF intensity, in lux for directional lights and
    // candela for point/spot lights.
    DirectX::XMFLOAT4 colorIntensity{};
    // positionRange.xyz: world position for point/spot lights.
    // positionRange.w: glTF point/spot range in meters; <=0 means infinite.
    DirectX::XMFLOAT4 positionRange{};
    DirectX::XMFLOAT4 direction{};
    DirectX::XMFLOAT4 spotCone{};
};

struct SceneLightAliasGpuRecord
{
    // meta.x: primary light index, meta.y: alias light index, meta.zw: reserved.
    DirectX::XMUINT4 meta{};
    // probabilities.x: alias accept threshold, probabilities.y: primary pdf, probabilities.z: alias pdf.
    DirectX::XMFLOAT4 probabilities{};
};

struct SceneLightAliasTableBuildResult
{
    std::uint32_t aliasCount = 0;
    float totalEnergy = 0.0f;
    std::vector<SceneLightAliasGpuRecord> records{};
};

inline constexpr std::uint32_t kSceneLightGpuFlagCastShadow = 1u << 0u;

[[nodiscard]] constexpr std::uint32_t sceneLightGpuType(nr::resource::LightType type) noexcept
{
    return static_cast<std::uint32_t>(type);
}

[[nodiscard]] float sceneLightAliasEnergy(DirectX::XMFLOAT3 color, float intensity) noexcept;

[[nodiscard]] float sceneLightAliasEnergy(const SceneLightGpuRecord &record) noexcept;

[[nodiscard]] SceneLightAliasTableBuildResult buildSceneLightAliasTable(std::span<const SceneLightGpuRecord> records);

static_assert(sizeof(SceneLightGpuHeader) == 16u);
static_assert(sizeof(SceneLightGpuRecord) == 80u);
static_assert(sizeof(SceneLightAliasGpuRecord) == 32u);
static_assert(alignof(SceneLightGpuHeader) <= 16u);
static_assert(alignof(SceneLightGpuRecord) <= 16u);
static_assert(alignof(SceneLightAliasGpuRecord) <= 16u);
} // namespace nr::scene
