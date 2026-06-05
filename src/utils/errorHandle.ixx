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
    auto& stream = levelStream(level);
    std::print(stream,
               "{}[NR {}:{}]{} {}\n{}\n{}\n",
               detail::levelColor(level),
               channel,
               logLevelNames[static_cast<size_t>(level)],
               detail::ansiReset,
               locationStr,
               loc.function_name(),
               context.empty() ? "(none)" : context);
    stream.flush();
}

inline void emitCompactLog(LogLevel level, std::string_view channel, std::string_view context)
{
    auto& stream = levelStream(level);
    std::print(stream,
               "{}[NR {}:{}]{} {}\n",
               detail::levelColor(level),
               channel,
               logLevelNames[static_cast<size_t>(level)],
               detail::ansiReset,
               context.empty() ? "(none)" : context);
    stream.flush();
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

constexpr inline void nrLog(LogLevel level,
                            std::string_view channel,
                            std::string_view context,
                            std::source_location loc = std::source_location::current(),
                            bool terminateOnError = false)
{
    if (globalLogLevel <= level)
    {
        detail::emitLog(level, channel, context, loc);
    }

    if (terminateOnError && level == LogLevel::error)
    {
        std::exit(1);
    }
}

constexpr inline void nrLog(LogLevel level,
                            std::string_view context,
                            std::source_location loc = std::source_location::current(),
                            bool terminateOnError = false)
{
    nrLog(level, "LOG", context, loc, terminateOnError);
}

template <LogLevel Level = LogLevel::info> constexpr inline void nrInfo(std::string_view context = "", std::source_location loc = std::source_location::current())
{
    // Compile-time log level filtering (type-safe enum comparison)
    if constexpr (globalLogLevel <= Level)
    {
        constexpr bool isError = Level == LogLevel::error;
        nrLog(Level, "LOG", context, loc, isError);
    }
}

constexpr inline void nrVulkan(LogLevel level, std::string_view context, std::source_location /*loc*/ = std::source_location::current())
{
    if (globalLogLevel <= level)
    {
        detail::emitCompactLog(level, "VULKAN", context);
    }
}

} // namespace nr
