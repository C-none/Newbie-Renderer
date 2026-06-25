export module nr.load:assimp;

import :type;
import :backend;
import std;

export namespace nr::load
{
struct AssimpSceneImporter : SceneImporterBackendBase<AssimpSceneImporter>
{
    static constexpr std::string_view kBackendName = "assimp";
    static constexpr std::array<std::string_view, 4> kSupportedExtensions = {
        ".gltf",
        ".glb",
        ".fbx",
        ".obj",
    };

    [[nodiscard]] static bool supportsExtension(std::string_view extension);
    [[nodiscard]] static SceneImportResult importScene(const SceneLoadRequest& request);
};
} // namespace nr::load
