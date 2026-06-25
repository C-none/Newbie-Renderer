module nr.resource;
import :material;
import dependency.math;
import dependency.vulkan;
import std;
import :type;
import :handle;

namespace nr::resource
{
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

[[nodiscard]] bool Material::isOpaque() const noexcept
{
        return alphaMode == AlphaMode::opaque;
    }

[[nodiscard]] bool Material::isAlphaMasked() const noexcept
{
        return alphaMode == AlphaMode::mask;
    }

[[nodiscard]] bool Material::isAlphaBlended() const noexcept
{
        return alphaMode == AlphaMode::blend;
    }

[[nodiscard]] bool Material::usesMetallicRoughnessWorkflow() const noexcept
{
        return metallicFactor > 0.0f || roughnessFactor < 1.0f || metallicRoughness.texture.valid();
    }

[[nodiscard]] bool Material::usesSpecularGlossinessWorkflow() const noexcept
{
        return glm::any(glm::notEqual(specularFactor, glm::vec3{0.0f})) || glossinessFactor > 0.0f;
    }

[[nodiscard]] bool Material::usesAnisotropy() const noexcept
{
        return anisotropyFactor > 0.0f;
    }
} // namespace nr::resource
