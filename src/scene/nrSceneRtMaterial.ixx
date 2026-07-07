export module nr.scene:rtMaterial;

import dependency.math;
import dependency.shaderShare;

import nr.resource;
import std;
import :type;

export namespace nr::scene
{
using nr::shader::share::AlphaMode;
using nr::shader::share::MaterialTextureSlot;
using nr::shader::share::MaterialTextureSlotFlag;
using nr::shader::share::RtGeometryFlag;
using nr::shader::share::RtGeometryMetadata;
using nr::shader::share::RtInstanceFlag;
using nr::shader::share::RtInstanceMetadata;
using nr::shader::share::RtMaterialFeatureFlag;
using nr::shader::share::RtMaterialHeader;
using nr::shader::share::RtMaterialLayerKind;
using nr::shader::share::RtMaterialLayerRecord;
using nr::shader::share::RtMaterialTextureRef;

inline constexpr auto kRtMaterialAbiVersion = nr::shader::share::kRtMaterialAbiVersion;
inline constexpr auto kRtMaterialFallbackIndex = nr::shader::share::kRtMaterialFallbackIndex;
inline constexpr auto kRtGeometryFlagIndexed = RtGeometryFlag::indexed;

[[nodiscard]] constexpr bool hasAnyRtMaterialFeature(RtMaterialFeatureFlag flags, RtMaterialFeatureFlag mask) noexcept
{
    return static_cast<std::uint32_t>(flags & mask) != 0u;
}

struct RtCompiledMaterial
{
    RtMaterialHeader header{};
    std::vector<RtMaterialLayerRecord> layers{};
    std::vector<RtMaterialTextureRef> textureRefs{};
};

struct RtMaterialTable
{
    std::vector<RtMaterialHeader> headers{};
    std::vector<RtMaterialLayerRecord> layers{};
    std::vector<RtMaterialTextureRef> textureRefs{};
};

[[nodiscard]] MaterialTextureSlotFlag rtMaterialTextureMask(nr::resource::MaterialTextureSlotSemantic semantic) noexcept;

[[nodiscard]] RtCompiledMaterial compileRtMaterial(
    const nr::resource::Material& material,
    const SceneMaterialTextureIds& textureIds = {});

[[nodiscard]] RtMaterialTable makeRtMaterialTable(std::span<const std::reference_wrapper<const RtCompiledMaterial>> materials);

[[nodiscard]] RtCompiledMaterial makeFallbackRtMaterial();

static_assert(sizeof(RtMaterialHeader) == 96u);
static_assert(sizeof(RtMaterialLayerRecord) == 48u);
static_assert(sizeof(RtMaterialTextureRef) == 16u);
static_assert(sizeof(RtGeometryMetadata) == 32u);
static_assert(sizeof(RtInstanceMetadata) == 32u);
static_assert(static_cast<std::uint32_t>(AlphaMode::opaque) == static_cast<std::uint32_t>(nr::resource::AlphaMode::opaque));
static_assert(static_cast<std::uint32_t>(AlphaMode::mask) == static_cast<std::uint32_t>(nr::resource::AlphaMode::mask));
static_assert(static_cast<std::uint32_t>(AlphaMode::blend) == static_cast<std::uint32_t>(nr::resource::AlphaMode::blend));
} // namespace nr::scene
