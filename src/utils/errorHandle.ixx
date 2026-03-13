module;
export module nr.utils:errorHandle;
import :staticUtils;
import std;

export namespace nr
{
// LogLevel enum is now auto-generated in staticUtilsConstants.h

namespace detail
{
inline constexpr std::string_view ansiReset = "\x1b[0m";
inline constexpr std::string_view ansiRedBold = "\x1b[1;31m";
inline constexpr std::string_view ansiRed = "\x1b[31m";
inline constexpr std::string_view ansiYellow = "\x1b[33m";
inline constexpr std::string_view ansiCyan = "\x1b[36m";

constexpr std::string_view levelColor(LogLevel level)
{
    switch (level)
    {
    case LogLevel::info: return ansiCyan;
    case LogLevel::warning: return ansiYellow;
    case LogLevel::error: return ansiRed;
    default: return ansiReset;
    }
}

inline std::ostream &levelStream(LogLevel level)
{
    return level == LogLevel::error ? std::cerr : std::cout;
}

inline void emitLog(LogLevel level, std::string_view channel, std::string_view context, std::source_location loc)
{
    std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());
    std::print(levelStream(level),
               "{}[NR {}:{}]{} {}\n{}\n{}\n",
               detail::levelColor(level),
               channel,
               logLevelNames[static_cast<size_t>(level)],
               detail::ansiReset,
               locationStr,
               loc.function_name(),
               context.empty() ? "(none)" : context);
}
} // namespace detail

constexpr inline void nrAssert(bool condition, std::string_view context = "", std::source_location loc = std::source_location::current())
{
    if (!condition)
    {
        std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());

        std::print(std::cerr,
                   "{}[NR ASSERT]{} {}\n{}\n{}\n",
                   detail::ansiRedBold, detail::ansiReset, locationStr, loc.function_name(), context.empty() ? "(none)" : context);

        std::exit(1);
    }
}
template <LogLevel Level = LogLevel::info> constexpr inline void nrInfo(std::string_view context = "", std::source_location loc = std::source_location::current())
{
    // Compile-time log level filtering (type-safe enum comparison)
    if constexpr (globalLogLevel <= Level)
    {
        constexpr bool isError = Level == LogLevel::error;
        detail::emitLog(Level, "LOG", context, loc);
        if constexpr (isError)
            std::exit(1);
    }
}

constexpr inline void nrVulkan(LogLevel level, std::string_view context, std::source_location loc = std::source_location::current())
{
    if (globalLogLevel <= level)
    {
        detail::emitLog(level, "VULKAN", context, loc);
    }
}

} // namespace nr
