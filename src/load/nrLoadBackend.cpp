module nr.load;
import :backend;
import :type;
import std;

namespace nr::load
{
[[nodiscard]] std::string normalizedExtension(const std::filesystem::path &path)
{
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return extension;
}

[[nodiscard]] LoadError makeLoadError(LoadErrorCode code,
                                             std::string_view backend,
                                             const std::filesystem::path &sourcePath,
                                             std::string message)
{
    return LoadError{
        .code = code,
        .backend = std::string{backend},
        .sourcePath = sourcePath,
        .message = std::move(message),
    };
}

} // namespace nr::load
