export module nr.load:loader;

import :type;

export namespace nr::load
{
[[nodiscard]] SceneImportResult loadScene(const SceneLoadRequest &request);
} // namespace nr::load
