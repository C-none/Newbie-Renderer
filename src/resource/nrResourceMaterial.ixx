module;
export module nr.resource:material;

import dependency;
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

    [[nodiscard]] std::size_t byteSize() const noexcept
    {
        return bytes.size();
    }
};

struct Texture
{
    std::string name{};
    std::filesystem::path sourcePath{};
    TextureDimension dimension = TextureDimension::tex2D;
    PixelFormat format = PixelFormat::unknown;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1;
    std::uint32_t mipCount = 1;
    bool srgb = true;
    bool compressed = false;
    std::vector<ImageLevel> levels{};

    Texture() = default;
    ~Texture() = default;

    [[nodiscard]] bool valid() const noexcept
    {
        if (width == 0u || height == 0u || depth == 0u || mipCount == 0u)
        {
            return false;
        }

        if (format == PixelFormat::unknown)
        {
            return false;
        }

        if (levels.size() > static_cast<std::size_t>(mipCount))
        {
            return false;
        }

        return true;
    }

    [[nodiscard]] bool hasCpuPixels() const noexcept
    {
        return std::ranges::any_of(levels, [](const ImageLevel &level) {
            return !level.bytes.empty();
        });
    }

    [[nodiscard]] std::size_t byteSize() const noexcept
    {
        return std::ranges::fold_left(
            levels,
            std::size_t{0},
            [](std::size_t sum, const ImageLevel &level) {
                return sum + level.byteSize();
            });
    }

    [[nodiscard]] glm::uvec3 mipExtent(std::uint32_t mip) const noexcept
    {
        if (mip >= mipCount)
        {
            return glm::uvec3{0u, 0u, 0u};
        }

        auto reduce = [mip](std::uint32_t value) {
            auto shifted = value >> mip;
            return std::max(shifted, 1u);
        };

        return glm::uvec3{reduce(width), reduce(height), reduce(depth)};
    }
};

struct SamplerDesc
{
    FilterMode minFilter = FilterMode::linear;
    FilterMode magFilter = FilterMode::linear;
    FilterMode mipFilter = FilterMode::linear;
    AddressMode addressU = AddressMode::repeat;
    AddressMode addressV = AddressMode::repeat;
    AddressMode addressW = AddressMode::repeat;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = std::numeric_limits<float>::max();
    float maxAnisotropy = 1.0f;

    SamplerDesc() = default;
    ~SamplerDesc() = default;
};

struct MaterialTextureSlot
{
    TextureHandle texture{};
    SamplerDesc sampler{};
    std::uint32_t uvSet = 0;
    float scale = 1.0f;
    float strength = 1.0f;

    MaterialTextureSlot() = default;
    ~MaterialTextureSlot() = default;
};

struct Material
{
    std::string name{};
    glm::vec4 baseColorFactor{1.0f};
    glm::vec3 emissiveFactor{0.0f};
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::opaque;
    bool doubleSided = false;

    MaterialTextureSlot baseColor{};
    MaterialTextureSlot normal{};
    MaterialTextureSlot metallicRoughness{};
    MaterialTextureSlot occlusion{};
    MaterialTextureSlot emissive{};

    Material() = default;
    ~Material() = default;

    [[nodiscard]] bool isOpaque() const noexcept
    {
        return alphaMode == AlphaMode::opaque;
    }

    [[nodiscard]] bool isAlphaMasked() const noexcept
    {
        return alphaMode == AlphaMode::mask;
    }

    [[nodiscard]] bool isAlphaBlended() const noexcept
    {
        return alphaMode == AlphaMode::blend;
    }
};

} // namespace nr::resource
