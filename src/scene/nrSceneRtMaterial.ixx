module;
#include <cstddef>

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
using nr::shader::share::RtGeometryFlag;
using nr::shader::share::RtGeometryMetadata;
using nr::shader::share::RtInstanceFlag;
using nr::shader::share::RtInstanceMetadata;
using nr::shader::share::RtBaseLobeVariant;
using nr::shader::share::RtMaterialFeatureFlag;
using nr::shader::share::RtMaterialHeader;
using nr::shader::share::RtMaterialLayerFlag;
using nr::shader::share::RtMaterialLayerRecord;
using nr::shader::share::RtMaterialTextureRef;
using nr::shader::share::RtTransmissionMode;

inline constexpr auto kRtMaterialFallbackIndex = nr::shader::share::kRtMaterialFallbackIndex;
inline constexpr auto kRtGeometryFlagIndexed = RtGeometryFlag::indexed;

[[nodiscard]] constexpr bool hasAnyRtMaterialFeature(RtMaterialFeatureFlag flags, RtMaterialFeatureFlag mask) noexcept
{
    return static_cast<std::uint32_t>(flags & mask) != 0u;
}

[[nodiscard]] constexpr bool hasRtMaterialLayer(RtMaterialLayerFlag flags, RtMaterialLayerFlag layer) noexcept
{
    return (static_cast<std::uint32_t>(flags) & static_cast<std::uint32_t>(layer)) != 0u;
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

[[nodiscard]] RtCompiledMaterial compileRtMaterial(
    const nr::resource::Material& material,
    const SceneMaterialTextureIds& textureIds = {});

[[nodiscard]] RtMaterialTable makeRtMaterialTable(std::span<const std::reference_wrapper<const RtCompiledMaterial>> materials);

[[nodiscard]] RtCompiledMaterial makeFallbackRtMaterial();

static_assert(sizeof(RtMaterialHeader) == 112u);
static_assert(offsetof(RtMaterialHeader, anisotropy) == 96u);
static_assert(sizeof(RtMaterialLayerRecord) == 44u);
static_assert(sizeof(RtMaterialTextureRef) == 32u);
static_assert(offsetof(RtMaterialTextureRef, uvLinear) == 0u);
static_assert(offsetof(RtMaterialTextureRef, uvOffset) == 16u);
static_assert(offsetof(RtMaterialTextureRef, textureId) == 24u);
static_assert(offsetof(RtMaterialTextureRef, uvSet) == 28u);
static_assert(sizeof(RtGeometryMetadata) == 32u);
static_assert(sizeof(RtInstanceMetadata) == 32u);
static_assert(static_cast<std::uint32_t>(RtTransmissionMode::thin) == 0u);
static_assert(static_cast<std::uint32_t>(RtTransmissionMode::volume) == 1u);
static_assert(static_cast<std::uint32_t>(AlphaMode::opaque) == static_cast<std::uint32_t>(nr::resource::AlphaMode::opaque));
static_assert(static_cast<std::uint32_t>(AlphaMode::mask) == static_cast<std::uint32_t>(nr::resource::AlphaMode::mask));
static_assert(static_cast<std::uint32_t>(AlphaMode::blend) == static_cast<std::uint32_t>(nr::resource::AlphaMode::blend));
} // namespace nr::scene
