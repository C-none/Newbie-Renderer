export module nr.load:backend;

import :type;
import std;

export namespace nr::load
{
[[nodiscard]] std::string normalizedExtension(const std::filesystem::path &path);

[[nodiscard]] LoadError makeLoadError(LoadErrorCode code, std::string_view backend,
                                      const std::filesystem::path &sourcePath, std::string message);

template <typename T>
concept SceneImporterBackend = requires(const SceneLoadRequest &request) {
    { T::backendName() } -> std::convertible_to<std::string_view>;
    { T::supports(request) } -> std::same_as<bool>;
    { T::importScene(request) } -> std::same_as<SceneImportResult>;
};

template <typename Derived> struct SceneImporterBackendBase
{
    [[nodiscard]] static std::string_view backendName()
    {
        return Derived::kBackendName;
    }

    [[nodiscard]] static bool supports(const SceneLoadRequest &request)
    {
        return Derived::supportsExtension(normalizedExtension(request.sourcePath));
    }

    [[nodiscard]] static LoadError makeError(LoadErrorCode code, const std::filesystem::path &sourcePath,
                                             std::string message)
    {
        return makeLoadError(code, backendName(), sourcePath, std::move(message));
    }
};

namespace detail
{
template <typename RegistryTuple, std::size_t Index = 0>
[[nodiscard]] SceneImportResult dispatchSceneImport(const SceneLoadRequest &request)
{
    if constexpr (Index >= std::tuple_size_v<RegistryTuple>)
    {
        return SceneImportResult{std::unexpected(makeLoadError(
            LoadErrorCode::unsupportedFormat, "registry", request.sourcePath,
            std::format("No importer backend accepts extension '{}'.", normalizedExtension(request.sourcePath))))};
    }
    else
    {
        using Backend = std::tuple_element_t<Index, RegistryTuple>;
        static_assert(SceneImporterBackend<Backend>, "Registry backend must satisfy SceneImporterBackend concept.");

        if (Backend::supports(request))
        {
            return Backend::importScene(request);
        }

        return dispatchSceneImport<RegistryTuple, Index + 1>(request);
    }
}
} // namespace detail

template <typename RegistryTuple> struct SceneImporterRegistry
{
    [[nodiscard]] static SceneImportResult import(const SceneLoadRequest &request)
    {
        if (request.sourcePath.empty())
        {
            return SceneImportResult{
                std::unexpected(makeLoadError(LoadErrorCode::invalidArgument, "registry", request.sourcePath,
                                              "SceneLoadRequest.sourcePath must not be empty."))};
        }

        return detail::dispatchSceneImport<RegistryTuple>(request);
    }
};

template <typename RegistryTuple> [[nodiscard]] inline SceneImportResult importScene(const SceneLoadRequest &request)
{
    return SceneImporterRegistry<RegistryTuple>::import(request);
}

} // namespace nr::load
