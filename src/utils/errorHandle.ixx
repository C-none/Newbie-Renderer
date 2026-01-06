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
template <typename Derived> struct LogBase_t
{
    std::source_location loc;

    constexpr explicit LogBase_t(std::source_location l) : loc(l)
    {
    }

    template <typename... Args> inline void operator()(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        static_cast<const Derived *>(this)->log(fmt, std::forward<Args>(args)...);
    }
};
struct Assert_t : LogBase_t<Assert_t>
{
    bool condition;

    constexpr Assert_t(bool cond, std::source_location l) : LogBase_t<Assert_t>(l), condition(cond)
    {
    }

    template <typename... Args> inline void log(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        if (!condition)
        {
            std::string formatted = std::format(fmt, std::forward<Args>(args)...);
            std::println(std::cerr,
                         "[nrAssert] FAILED\n"
                         "  msg  : {}\n"
                         "  file : {}\n"
                         "  line : {}\n"
                         "  func : {}",
                         formatted, loc.file_name(), loc.line(), loc.function_name());
        }
    }
};

struct Info_t : LogBase_t<Info_t>
{
    nr::LogLevel level;

    constexpr Info_t(nr::LogLevel lv, std::source_location l) : LogBase_t<Info_t>(l), level(lv)
    {
    }

    template <typename... Args> inline void log(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        std::string formatted = std::format(fmt, std::forward<Args>(args)...);
        std::array<std::string, static_cast<size_t>(nr::LogLevel::number)> levelDict{"INFO", "WARNING", "ERROR"};
        bool isError = level == nr::LogLevel::error;
        std::println(isError ? std::cerr : std::cout,
                     "[nr {}]\n"
                     "  msg  : {}\n"
                     "  file : {}\t\t"
                     "  line : {}\n"
                     "  func : {}",
                     levelDict[static_cast<size_t>(level)], formatted, loc.file_name(), loc.line(), loc.function_name());
        if (isError)
            std::exit(1);
    }
};

 } // namespace detail

export namespace nr
{

 inline detail::Assert_t nrAssert(bool condition, std::source_location loc = std::source_location::current())
 {
     return {condition, loc};
 }

 inline detail::Info_t nrInfo(LogLevel level = LogLevel::info, std::source_location loc = std::source_location::current())
 {
     return {level, loc};
 }

} // namespace nr