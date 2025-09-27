module;
export module nr.utils:errorHandle;
import std;
export namespace nr
{

inline void nrAssertReport(std::string_view formatted, std::source_location loc = std::source_location::current())
{
    std::print(std::cerr,
               "[nrAssert] FAILED\n"
               "  msg  : {}\n"
               "  file : {}\n"
               "  line : {}\n"
               "  func : {}\n",
               formatted, loc.file_name(), loc.line(), loc.function_name());
    std::cerr.flush();
    // std::abort();
}

template <typename... Args> inline void nrAssert(bool condition, const std::format_string<Args...> _Fmt, Args &&..._Args)
{
    if (!condition)
    {
        auto formatted = std::format(_Fmt, std::forward<Args>(_Args)...);
        auto loc = std::source_location::current();
        nrAssertReport(formatted, loc);
    }
}

template <typename... Args> inline void nrInfo(const std::format_string<Args...> _Fmt, Args &&..._Args)
{
    auto formatted = std::format(_Fmt, std::forward<Args>(_Args)...);
    auto loc = std::source_location::current();
    std::print(std::cout,
               "[nrInfo]\n"
               "  msg  : {}\n"
               "  file : {}\n"
               "  line : {}\n"
               "  func : {}\n",
               formatted, loc.file_name(), loc.line(), loc.function_name());
    std::cout.flush();
}

} // namespace nr