module nr.utils;
import :errorHandle;
import :staticUtils;
import std;

namespace nr
{
namespace detail
{
std::ostream &levelStream(LogLevel level)
{
    return level == LogLevel::error ? std::cerr : std::cout;
}

void emitLog(LogLevel level, std::string_view channel, std::string_view context, std::source_location loc)
{
    std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());
    auto& stream = levelStream(level);
    std::print(stream,
               "{}[NR {}:{}]{} {}\n{}{}{}\n{}\n",
               detail::levelColor(level),
               channel,
               logLevelNames[static_cast<std::size_t>(level)],
               detail::ansiReset,
               locationStr,
               detail::ansiPaleYellow,
               loc.function_name(),
               detail::ansiReset,
               context.empty() ? "(none)" : context);
    stream.flush();
}

void emitCompactLog(LogLevel level, std::string_view channel, std::string_view context)
{
    auto& stream = levelStream(level);
    std::print(stream,
               "{}[NR {}:{}]{} {}\n",
               detail::levelColor(level),
               channel,
               logLevelNames[static_cast<std::size_t>(level)],
               detail::ansiReset,
               context.empty() ? "(none)" : context);
    stream.flush();
}
} // namespace detail
} // namespace nr
