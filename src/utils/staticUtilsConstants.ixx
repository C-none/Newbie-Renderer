export module nr.utils:staticUtilsConstants;
import std;

#if __has_include("staticUtilsConstantsConfig.inl")
#include "staticUtilsConstantsConfig.inl"
#endif

#ifndef NR_IS_DEBUG_MODE
#define NR_IS_DEBUG_MODE false
#endif

#ifndef NR_MAX_THREADS
#define NR_MAX_THREADS 4u
#endif

#ifndef NR_SHADER_CACHE_ROOT
#define NR_SHADER_CACHE_ROOT ""
#endif

#ifndef NR_SHADER_ROOT
#define NR_SHADER_ROOT ""
#endif

#ifndef NR_PROJECT_ROOT
#define NR_PROJECT_ROOT ""
#endif

export namespace nr
{
inline constexpr bool isDebugMode = NR_IS_DEBUG_MODE;
inline constexpr std::uint32_t maxThreads = NR_MAX_THREADS;

enum class LogLevel : std::uint32_t
{
    info = 0,
    warning = 1,
    error = 2,
    number = 3
};

#ifndef NR_GLOBAL_LOG_LEVEL
#define NR_GLOBAL_LOG_LEVEL LogLevel::info
#endif

inline constexpr LogLevel globalLogLevel = NR_GLOBAL_LOG_LEVEL;

inline constexpr std::array<std::string_view, static_cast<std::size_t>(LogLevel::number)> logLevelNames = {"INFO", "WARNING", "ERROR"};

inline constexpr std::string_view projectRoot = NR_PROJECT_ROOT;
inline constexpr std::string_view shaderCacheRoot = NR_SHADER_CACHE_ROOT;
inline constexpr std::string_view shaderRoot = NR_SHADER_ROOT;
} // namespace nr
