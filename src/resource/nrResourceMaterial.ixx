export module nr.resource:material;
import dependency.math;
import dependency.vulkan;

import std;
import :type;
import :handle;

export namespace nr::resource
{
struct ImageLevel
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::vector<std::byte> bytes{};

    ImageLevel() = default;
    ~ImageLevel() = default;

    [[nodiscard]] std::size_t byteSize() const noexcept;
};

struct Texture
{
    std::string name{};
    std::filesystem::path sourcePath{};
    TextureDimension dimension = vk::ImageType::e2D;
    PixelFormat format = vk::Format::eUndefined;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipCount = 1;
    bool srgb = true;
    bool compressed = false;
    std::vector<ImageLevel> levels{};

    Texture() = default;
    ~Texture() = default;

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] bool hasCpuPixels() const noexcept;

    [[nodiscard]] std::size_t byteSize() const noexcept;

    [[nodiscard]] glm::uvec3 mipExtent(std::uint32_t mip) const noexcept;
};

struct SamplerDesc
{
    FilterMode minFilter = vk::Filter::eLinear;
    FilterMode magFilter = vk::Filter::eLinear;
    MipFilterMode mipFilter = vk::SamplerMipmapMode::eLinear;
    AddressMode addressU = vk::SamplerAddressMode::eRepeat;
    AddressMode addressV = vk::SamplerAddressMode::eRepeat;
    AddressMode addressW = vk::SamplerAddressMode::eRepeat;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = std::numeric_limits<float>::max();
    float maxAnisotropy = 1.0f;

    SamplerDesc() = default;
    ~SamplerDesc() = default;
};

struct MaterialTextureTransform
{
    glm::vec4 linear{1.0f, 0.0f, 0.0f, 1.0f};
    glm::vec2 offset{};
};

struct MaterialTextureSlot
{
    TextureHandle texture{};
    SamplerDesc sampler{};
    std::uint32_t uvSet = 0;
    MaterialTextureTransform transform{};
    float scale = 1.0f;
    float strength = 1.0f;

    MaterialTextureSlot() = default;
    ~MaterialTextureSlot() = default;
};

enum class MaterialTextureSlotSemantic : std::uint8_t
{
    baseColor,
    normal,
    metallicRoughness,
    occlusion,
    emissive,
    clearcoat,
    clearcoatRoughness,
    clearcoatNormal,
    sheenColor,
    sheenRoughness,
    transmission,
    anisotropy,
    unsupported,
};

inline constexpr std::size_t materialTextureSlotCount = static_cast<std::size_t>(MaterialTextureSlotSemantic::unsupported);

[[nodiscard]] constexpr bool materialTextureSlotSemanticValid(MaterialTextureSlotSemantic semantic) noexcept
{
    return static_cast<std::size_t>(semantic) < materialTextureSlotCount;
}

[[nodiscard]] constexpr std::size_t materialTextureSlotIndex(MaterialTextureSlotSemantic semantic) noexcept
{
    return static_cast<std::size_t>(semantic);
}

[[nodiscard]] std::string_view materialTextureSlotSemanticName(MaterialTextureSlotSemantic semantic) noexcept;

enum class MaterialFeatureFlag : std::uint32_t
{
    none = 0u,
    clearcoat = 1u << 0u,
    sheen = 1u << 1u,
    transmission = 1u << 2u,
    anisotropy = 1u << 3u,
    doubleSided = 1u << 4u,
    alphaMask = 1u << 5u,
    alphaBlend = 1u << 6u,
};

[[nodiscard]] constexpr MaterialFeatureFlag operator|(MaterialFeatureFlag lhs, MaterialFeatureFlag rhs) noexcept
{
    return static_cast<MaterialFeatureFlag>(static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
}

[[nodiscard]] constexpr MaterialFeatureFlag operator&(MaterialFeatureFlag lhs, MaterialFeatureFlag rhs) noexcept
{
    return static_cast<MaterialFeatureFlag>(static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
}

constexpr MaterialFeatureFlag &operator|=(MaterialFeatureFlag &lhs, MaterialFeatureFlag rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool hasAnyFeature(MaterialFeatureFlag flags, MaterialFeatureFlag mask) noexcept
{
    return static_cast<std::uint32_t>(flags & mask) != 0u;
}

struct MaterialCorePbr
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::opaque;
    bool doubleSided = false;

    MaterialCorePbr() = default;
    ~MaterialCorePbr() = default;
};

struct MaterialClearcoatExtension
{
    float factor = 0.0f;
    float roughnessFactor = 0.0f;

    MaterialClearcoatExtension() = default;
    ~MaterialClearcoatExtension() = default;
};

struct MaterialSheenExtension
{
    glm::vec3 colorFactor{0.0f};
    float roughnessFactor = 0.0f;

    MaterialSheenExtension() = default;
    ~MaterialSheenExtension() = default;
};

struct MaterialTransmissionExtension
{
    float factor = 0.0f;

    MaterialTransmissionExtension() = default;
    ~MaterialTransmissionExtension() = default;
};

struct MaterialIorExtension
{
    float ior = 1.5f;

    MaterialIorExtension() = default;
    ~MaterialIorExtension() = default;
};

struct MaterialVolumeBoundaryExtension
{
    float thicknessFactor = 0.0f;

    MaterialVolumeBoundaryExtension() = default;
    ~MaterialVolumeBoundaryExtension() = default;
};

struct MaterialAnisotropyExtension
{
    float factor = 0.0f;
    float rotation = 0.0f;

    MaterialAnisotropyExtension() = default;
    ~MaterialAnisotropyExtension() = default;
};

struct Material
{
    std::string name{};
    MaterialCorePbr core{};
    std::optional<MaterialClearcoatExtension> clearcoat{};
    std::optional<MaterialSheenExtension> sheen{};
    std::optional<MaterialTransmissionExtension> transmission{};
    std::optional<MaterialIorExtension> ior{};
    std::optional<MaterialVolumeBoundaryExtension> volumeBoundary{};
    std::optional<MaterialAnisotropyExtension> anisotropy{};
    bool unlit = false;
    std::array<MaterialTextureSlot, materialTextureSlotCount> textureSlots{};

    Material() = default;
    ~Material() = default;

    [[nodiscard]] MaterialTextureSlot &slot(MaterialTextureSlotSemantic semantic) noexcept;

    [[nodiscard]] const MaterialTextureSlot &slot(MaterialTextureSlotSemantic semantic) const noexcept;

    [[nodiscard]] MaterialFeatureFlag featureFlags() const noexcept;

    [[nodiscard]] bool hasVolumeTransmissionBoundary() const noexcept;

    [[nodiscard]] bool isOpaque() const noexcept;

    [[nodiscard]] bool isAlphaMasked() const noexcept;

    [[nodiscard]] bool isAlphaBlended() const noexcept;
};

} // namespace nr::resource
