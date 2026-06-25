export module nr.scene:utils;
import dependency.math;
import dependency.ecs;
import dependency.vulkan;

import nr.load;
import nr.resource;
import std;
import :type;

export namespace nr::scene::detail
{
[[nodiscard]] std::string sanitizeEntityName(std::string_view label);

[[nodiscard]] std::string normalizeSemantic(std::string_view semantic);

[[nodiscard]] MaterialSemanticSlot classifyMaterialSemantic(std::string_view semantic);

[[nodiscard]] bool semanticIsColor(MaterialSemanticSlot slot) noexcept;

[[nodiscard]] bool semanticIsLinear(MaterialSemanticSlot slot) noexcept;

[[nodiscard]] std::string_view slotName(MaterialSemanticSlot slot) noexcept;

[[nodiscard]] std::vector<TextureColorSpaceHint> buildTextureColorSpaceHints(const nr::load::SceneAsset &sceneAsset);

[[nodiscard]] nr::resource::PixelFormat pickTextureFormat(std::uint32_t channels, bool srgb) noexcept;

[[nodiscard]] std::optional<PreparedImageLevel> prepareDecodedImageLevel(const nr::load::Image &image);

[[nodiscard]] nr::resource::ImageLevel prepareRawImageLevel(const nr::load::EmbeddedRawTexture &raw);

[[nodiscard]] std::string makeDeterministicChildName(SiblingNameTable &namesByParent,
                                                             flecs::entity_t parent,
                                                             std::string_view sourceName);

[[nodiscard]] std::string makeTemplateNodeEntityName(SceneTemplateHandle handle,
                                                             std::uint32_t sourceNodeIndex,
                                                             std::string_view resolvedName);

[[nodiscard]] std::string makeTemplateMeshEntityName(SceneTemplateHandle handle,
                                                             std::uint32_t sourceNodeIndex,
                                                             std::uint32_t meshSlot);

[[nodiscard]] std::string makeTemplateCameraEntityName(SceneTemplateHandle handle,
                                                               std::uint32_t sourceNodeIndex,
                                                               std::uint32_t cameraSlot);

[[nodiscard]] std::string makeTemplateLightEntityName(SceneTemplateHandle handle,
                                                              std::uint32_t sourceNodeIndex,
                                                              std::uint32_t lightSlot);

[[nodiscard]] glm::vec3 toVec3(std::array<float, 3> const &value);

[[nodiscard]] glm::mat4 toGlmMat4(const std::array<float, 16> &value);

[[nodiscard]] bool finiteMat4(const glm::mat4 &value) noexcept;

[[nodiscard]] glm::vec3 transformPoint(const glm::mat4 &matrix, const glm::vec3 &point);

[[nodiscard]] nr::resource::Aabb transformAabb(const nr::resource::Aabb &bounds,
                                                       const glm::mat4 &matrix);

[[nodiscard]] std::optional<nr::resource::LightType> mapLightType(std::string_view typeName);

template <typename HandleT>
inline void appendUniqueHandle(std::vector<HandleT> &handles,
                               std::set<std::uint64_t> &seen,
                               HandleT handle)
{
    if (seen.emplace(handle.packed()).second)
    {
        handles.push_back(handle);
    }
}
} // namespace nr::scene::detail
