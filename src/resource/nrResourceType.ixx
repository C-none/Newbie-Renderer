export module nr.resource:type;
import dependency.vulkan;

import std;

export namespace nr::resource
{
inline constexpr std::uint32_t invalidResourceSlot = std::numeric_limits<std::uint32_t>::max();

using PixelFormat = vk::Format;
using TextureDimension = vk::ImageType;
using FilterMode = vk::Filter;
using MipFilterMode = vk::SamplerMipmapMode;
using AddressMode = vk::SamplerAddressMode;

enum class AlphaMode : std::uint8_t
{
    opaque,
    mask,
    blend,
};

enum class CameraProjection : std::uint8_t
{
    perspective,
    orthographic,
};

enum class LightType : std::uint8_t
{
    directional,
    point,
    spot,
};
} // namespace nr::resource
