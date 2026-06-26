module nr.resource;
import :material;
import dependency.math;
import dependency.vulkan;
import nr.utils;
import std;
import :type;
import :handle;

namespace nr::resource
{
[[nodiscard]] std::string_view materialTextureSlotSemanticName(MaterialTextureSlotSemantic semantic) noexcept
{
        switch (semantic)
        {
        case MaterialTextureSlotSemantic::baseColor: return "baseColor";
        case MaterialTextureSlotSemantic::normal: return "normal";
        case MaterialTextureSlotSemantic::metallicRoughness: return "metallicRoughness";
        case MaterialTextureSlotSemantic::occlusion: return "occlusion";
        case MaterialTextureSlotSemantic::emissive: return "emissive";
        case MaterialTextureSlotSemantic::clearcoat: return "clearcoat";
        case MaterialTextureSlotSemantic::clearcoatRoughness: return "clearcoatRoughness";
        case MaterialTextureSlotSemantic::clearcoatNormal: return "clearcoatNormal";
        case MaterialTextureSlotSemantic::sheenColor: return "sheenColor";
        case MaterialTextureSlotSemantic::sheenRoughness: return "sheenRoughness";
        case MaterialTextureSlotSemantic::transmission: return "transmission";
        case MaterialTextureSlotSemantic::anisotropy: return "anisotropy";
        default: return "unsupported";
        }
    }

[[nodiscard]] std::size_t ImageLevel::byteSize() const noexcept
{
        return bytes.size();
    }

[[nodiscard]] bool Texture::valid() const noexcept
{
        if (width == 0u || height == 0u || depth == 0u || mipCount == 0u)
        {
            return false;
        }

        if (format == vk::Format::eUndefined)
        {
            return false;
        }

        if (levels.size() > static_cast<std::size_t>(mipCount))
        {
            return false;
        }

        return true;
    }

[[nodiscard]] bool Texture::hasCpuPixels() const noexcept
{
        return std::ranges::any_of(levels, [](const ImageLevel &level) {
            return !level.bytes.empty();
        });
    }

[[nodiscard]] std::size_t Texture::byteSize() const noexcept
{
        return std::ranges::fold_left(
            levels,
            std::size_t{0},
            [](std::size_t sum, const ImageLevel &level) {
                return sum + level.byteSize();
            });
    }

[[nodiscard]] glm::uvec3 Texture::mipExtent(std::uint32_t mip) const noexcept
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

[[nodiscard]] MaterialTextureSlot &Material::slot(MaterialTextureSlotSemantic semantic) noexcept
{
        nrAssert(materialTextureSlotSemanticValid(semantic), "Material::slot requires a supported texture slot semantic.");
        return textureSlots[materialTextureSlotIndex(semantic)];
    }

[[nodiscard]] const MaterialTextureSlot &Material::slot(MaterialTextureSlotSemantic semantic) const noexcept
{
        nrAssert(materialTextureSlotSemanticValid(semantic), "Material::slot requires a supported texture slot semantic.");
        return textureSlots[materialTextureSlotIndex(semantic)];
    }

[[nodiscard]] MaterialFeatureFlag Material::featureFlags() const noexcept
{
        auto flags = MaterialFeatureFlag::none;

        if (clearcoat.has_value())
        {
            flags |= MaterialFeatureFlag::clearcoat;
        }

        if (sheen.has_value())
        {
            flags |= MaterialFeatureFlag::sheen;
        }

        if (transmission.has_value())
        {
            flags |= MaterialFeatureFlag::transmission;
        }

        if (anisotropy.has_value())
        {
            flags |= MaterialFeatureFlag::anisotropy;
        }

        if (core.doubleSided)
        {
            flags |= MaterialFeatureFlag::doubleSided;
        }

        if (core.alphaMode == AlphaMode::mask)
        {
            flags |= MaterialFeatureFlag::alphaMask;
        }

        if (core.alphaMode == AlphaMode::blend)
        {
            flags |= MaterialFeatureFlag::alphaBlend;
        }

        return flags;
    }

[[nodiscard]] bool Material::isOpaque() const noexcept
{
        return core.alphaMode == AlphaMode::opaque;
    }

[[nodiscard]] bool Material::isAlphaMasked() const noexcept
{
        return core.alphaMode == AlphaMode::mask;
    }

[[nodiscard]] bool Material::isAlphaBlended() const noexcept
{
        return core.alphaMode == AlphaMode::blend;
    }
} // namespace nr::resource
