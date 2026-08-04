export module nr.load:loader;

import :type;
import :backend;
import :assimp;
import std;

export namespace nr::load
{
using DefaultSceneImporterRegistry = std::tuple<AssimpSceneImporter>;
using DefaultSceneLoader = SceneImporterRegistry<DefaultSceneImporterRegistry>;

[[nodiscard]] inline SceneImportResult loadScene(const SceneLoadRequest &request)
{
    return DefaultSceneLoader::import(request);
}

template <typename RegistryTuple> [[nodiscard]] inline SceneImportResult loadSceneWith(const SceneLoadRequest &request)
{
    return SceneImporterRegistry<RegistryTuple>::import(request);
}

} // namespace nr::load
