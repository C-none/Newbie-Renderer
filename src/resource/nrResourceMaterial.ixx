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
    
    // Metallic/Roughness workflow
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    
    // Specular/Glossiness workflow
    glm::vec3 specularFactor{0.0f};
    float glossinessFactor = 0.0f;
    
    // Anisotropy workflow
    float anisotropyFactor = 0.0f;
    
    // Surface properties
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    AlphaMode alphaMode = AlphaMode::opaque;
    bool doubleSided = false;

    // Texture slots: standard PBR/Blinn-Phong channels
    MaterialTextureSlot baseColor{};
    MaterialTextureSlot normal{};
    MaterialTextureSlot metallicRoughness{};
    MaterialTextureSlot occlusion{};
    MaterialTextureSlot emissive{};

    Material() = default;
    ~Material() = default;

    [[nodiscard]] bool isOpaque() const noexcept;

    [[nodiscard]] bool isAlphaMasked() const noexcept;

    [[nodiscard]] bool isAlphaBlended() const noexcept;
    
    [[nodiscard]] bool usesMetallicRoughnessWorkflow() const noexcept;
    
    [[nodiscard]] bool usesSpecularGlossinessWorkflow() const noexcept;
    
    [[nodiscard]] bool usesAnisotropy() const noexcept;
};

} // namespace nr::resource
