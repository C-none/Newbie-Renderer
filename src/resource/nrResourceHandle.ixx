export module nr.resource:handle;
import std;
import :type;

export namespace nr::resource
{
template <typename Tag> struct Handle
{
    std::uint32_t slot = invalidResourceSlot;
    std::uint32_t generation = 0;

    constexpr Handle() noexcept = default;
    constexpr Handle(std::uint32_t inSlot, std::uint32_t inGeneration) noexcept : slot(inSlot), generation(inGeneration)
    {
    }
    ~Handle() = default;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return slot != invalidResourceSlot;
    }

    [[nodiscard]] constexpr std::uint64_t packed() const noexcept
    {
        return (static_cast<std::uint64_t>(generation) << 32ull) | static_cast<std::uint64_t>(slot);
    }

    auto operator<=>(const Handle &) const = default;
};

using MeshHandle = Handle<struct MeshTag>;
using TextureHandle = Handle<struct TextureTag>;
using MaterialHandle = Handle<struct MaterialTag>;
using SkeletonHandle = Handle<struct SkeletonTag>;
using AnimationClipHandle = Handle<struct AnimationClipTag>;
using ParticleSetHandle = Handle<struct ParticleSetTag>;
using CameraAssetHandle = Handle<struct CameraAssetTag>;
using LightAssetHandle = Handle<struct LightAssetTag>;

} // namespace nr::resource
