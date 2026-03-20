module;
export module nr.resource:type;

import std;

export namespace nr::resource
{
inline constexpr std::uint32_t invalidResourceSlot = std::numeric_limits<std::uint32_t>::max();

enum class PixelFormat : std::uint16_t
{
	unknown,
	r8Unorm,
	rg8Unorm,
	rgba8Unorm,
	rgba8Srgb,
	bgra8Unorm,
	bgra8Srgb,
	rgba16Float,
	rgba32Float,
	bc1RgbaUnorm,
	bc1RgbaSrgb,
	bc3Unorm,
	bc3Srgb,
	bc5Unorm,
	bc7Unorm,
	bc7Srgb,
	d32Float,
};

enum class TextureDimension : std::uint8_t
{
	tex1D,
	tex2D,
	tex3D,
	cube,
};

enum class FilterMode : std::uint8_t
{
	nearest,
	linear,
};

enum class AddressMode : std::uint8_t
{
	repeat,
	mirroredRepeat,
	clampToEdge,
	clampToBorder,
};

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
