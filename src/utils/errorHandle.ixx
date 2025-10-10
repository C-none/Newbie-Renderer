module;
export module nr.utils:errorHandle;
import std;

export namespace nr
{
enum class LogLevel
{
    info,
    warning,
    error,
    number
};
} // namespace nr

namespace detail
{

struct Assert_t
{
    bool condition;
    std::source_location loc;

    constexpr Assert_t(bool cond, std::source_location l) : condition(cond), loc(l)
    {
    }

    template <typename... Args> inline void operator()(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        if (!condition)
        {
            auto formatted = std::format(fmt, std::forward<Args>(args)...);
            std::print(std::cerr,
                       "[nrAssert] FAILED\n"
                       "  msg  : {}\n"
                       "  file : {}\n"
                       "  line : {}\n"
                       "  func : {}\n",
                       formatted, loc.file_name(), loc.line(), loc.function_name());
            std::cerr.flush();
        }
    }
};

struct Info_t
{
    nr::LogLevel level;
    std::source_location loc;

    constexpr explicit Info_t(nr::LogLevel lv, std::source_location l) : level(lv), loc(l)
    {
    }

    template <typename... Args> inline void operator()(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        auto formatted = std::format(fmt, std::forward<Args>(args)...);
        std::array<std::string, static_cast<size_t>(nr::LogLevel::number)> levelDict{"INFO", "WARNING", "ERROR"};
        bool isError = level == nr::LogLevel::error;
        std::print(isError ? std::cerr : std::cout,
                   "[nr {}]\n"
                   "  msg  : {}\n"
                   "  file : {}\t\t"
                   "  line : {}\n"
                   "  func : {}\n",
                   levelDict[static_cast<size_t>(level)], formatted, loc.file_name(), loc.line(), loc.function_name());
        std::cout.flush();
        if (isError)
            std::exit(1);
    }
};
} // namespace detail

export namespace nr
{

inline auto nrAssert(bool condition, std::source_location loc = std::source_location::current())
{
    return detail::Assert_t{condition, loc};
}

inline auto nrInfo(LogLevel level = LogLevel::info, std::source_location loc = std::source_location::current())
{
    return detail::Info_t{level, loc};
}

} // namespace nr