export module nr.load:backend;

import :type;
import std;

namespace nr::load
{
[[nodiscard]] std::string normalizedExtension(const std::filesystem::path &path);

[[nodiscard]] LoadError makeLoadError(LoadErrorCode code, std::string_view backend,
                                      const std::filesystem::path &sourcePath, std::string message);
} // namespace nr::load
