export module nr.load:assimp;

import :type;
import nr.resource;
import std;

export namespace nr::load
{
[[nodiscard]] nr::resource::MaterialTextureSlotSemantic assimpTextureSlotSemantic(std::uint32_t textureTypeRaw,
                                                                                  std::uint32_t textureSlot) noexcept;

} // namespace nr::load

namespace nr::load::detail
{
[[nodiscard]] bool assimpSupportsExtension(std::string_view extension) noexcept;
[[nodiscard]] SceneImportResult importAssimpScene(const SceneLoadRequest &request);
} // namespace nr::load::detail
