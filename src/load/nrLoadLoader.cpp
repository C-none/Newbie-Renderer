module nr.load;

import :assimp;
import :backend;
import :loader;
import :type;
import std;

namespace nr::load
{
SceneImportResult loadScene(const SceneLoadRequest &request)
{
    if (request.sourcePath.empty())
    {
        return SceneImportResult{
            std::unexpected(makeLoadError(LoadErrorCode::invalidArgument, "registry", request.sourcePath,
                                          "SceneLoadRequest.sourcePath must not be empty."))};
    }

    auto const extension = normalizedExtension(request.sourcePath);
    if (!detail::assimpSupportsExtension(extension))
    {
        return SceneImportResult{
            std::unexpected(makeLoadError(LoadErrorCode::unsupportedFormat, "registry", request.sourcePath,
                                          std::format("No importer backend accepts extension '{}'.", extension)))};
    }

    return detail::importAssimpScene(request);
}
} // namespace nr::load
