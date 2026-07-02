export module nr.scene:rtMaterial;

import dependency.math;

import nr.resource;
import std;
import :type;

export namespace nr::scene
{
inline constexpr std::uint32_t kRtMaterialAbiVersion = 1u;
inline constexpr std::uint32_t kRtMaterialFallbackIndex = 0u;
inline constexpr std::uint32_t kRtGeometryFlagIndexed = 1u << 0u;

enum class RtMaterialFeatureFlag : std::uint32_t
{
    none = 0u,
    clearcoat = 1u << 0u,
    sheen = 1u << 1u,
    transmission = 1u << 2u,
    alphaMask = 1u << 3u,
    alphaBlend = 1u << 4u,
    doubleSided = 1u << 5u,
    emissive = 1u << 6u,
    unsupportedAnisotropy = 1u << 16u,
};

[[nodiscard]] constexpr RtMaterialFeatureFlag operator|(RtMaterialFeatureFlag lhs, RtMaterialFeatureFlag rhs) noexcept
{
    return static_cast<RtMaterialFeatureFlag>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr RtMaterialFeatureFlag operator&(RtMaterialFeatureFlag lhs, RtMaterialFeatureFlag rhs) noexcept
{
    return static_cast<RtMaterialFeatureFlag>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr RtMaterialFeatureFlag& operator|=(RtMaterialFeatureFlag& lhs, RtMaterialFeatureFlag rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool hasAnyRtMaterialFeature(RtMaterialFeatureFlag flags, RtMaterialFeatureFlag mask) noexcept
{
    return static_cast<std::uint32_t>(flags & mask) != 0u;
}

enum class RtMaterialLayerKind : std::uint32_t
{
    baseSurface = 0u,
    clearcoat = 1u,
    sheen = 2u,
    transmission = 3u,
};

struct RtMaterialHeader
{
    std::uint32_t abiVersion = kRtMaterialAbiVersion;
    std::uint32_t featureFlags = 0;
    std::uint32_t layerOffset = 0;
    std::uint32_t layerCount = 0;
    std::uint32_t textureRefOffset = 0;
    std::uint32_t textureRefCount = 0;
    std::uint32_t alphaMode = 0;
    float alphaCutoff = 0.5f;
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveAndMetallic{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 roughnessNormalOcclusionAlpha{1.0f, 1.0f, 1.0f, 0.5f};
    glm::vec4 transmissionClearcoatSheen{0.0f};
};

struct RtMaterialLayerRecord
{
    std::uint32_t kind = static_cast<std::uint32_t>(RtMaterialLayerKind::baseSurface);
    std::uint32_t textureMask = 0;
    std::uint32_t aux0 = 0;
    std::uint32_t aux1 = 0;
    glm::vec4 p0{0.0f};
    glm::vec4 p1{0.0f};
};

struct RtMaterialTextureRef
{
    std::uint32_t slot = 0;
    std::uint32_t textureId = kDefaultSceneTextureId;
    std::uint32_t uvSet = 0;
    std::uint32_t flags = 0;
};

struct RtGeometryMetadata
{
    std::uint32_t materialIndex = kRtMaterialFallbackIndex;
    std::uint32_t geometryIndex = 0;
    std::uint32_t primitiveOffset = 0;
    std::uint32_t firstVertex = 0;
    std::uint32_t primitiveCount = 0;
    std::uint32_t flags = 0;
    std::uint32_t reserved0 = 0;
    std::uint32_t reserved1 = 0;
};

struct RtInstanceMetadata
{
    std::uint32_t geometryOffset = 0;
    std::uint32_t geometryCount = 0;
    std::uint32_t stableInstanceId = 0;
    std::uint32_t flags = 0;
    std::uint32_t vertexBase = 0;
    std::uint32_t indexBase = 0;
    std::uint32_t vertexStride = sizeof(nr::resource::Vertex);
    std::uint32_t reserved0 = 0;
};

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

[[nodiscard]] std::uint32_t rtMaterialTextureMask(nr::resource::MaterialTextureSlotSemantic semantic) noexcept;

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
} // namespace nr::scene
