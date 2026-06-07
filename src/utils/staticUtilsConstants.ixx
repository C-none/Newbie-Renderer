export module nr.utils:staticUtilsConstants;
import std;

export namespace nr
{
inline constexpr bool isDebugMode = true;
inline constexpr std::uint32_t maxThreads = 32u;

enum class LogLevel : std::uint32_t
{
    info = 0,
    warning = 1,
    error = 2,
    number = 3
};

inline constexpr LogLevel globalLogLevel = LogLevel::info;

inline constexpr std::array<std::string_view, static_cast<std::size_t>(LogLevel::number)> logLevelNames = {"INFO", "WARNING", "ERROR"};

inline constexpr std::string_view shaderCacheRoot = "D:/file/prog/Newbie-Renderer/build/llvm-debug-codex/shader_cache";
inline constexpr std::string_view shaderRoot = "D:/file/prog/Newbie-Renderer/shader";
} // namespace nr
