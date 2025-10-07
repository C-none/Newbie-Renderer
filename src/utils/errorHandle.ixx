module;
export module nr.utils:errorHandle;
import std;

namespace detail
{

struct nrAssert_t
{
    bool condition;
    std::source_location loc;

    constexpr nrAssert_t(bool cond, std::source_location l = std::source_location::current()) : condition(cond), loc(l)
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

struct nrInfo_t
{
    std::source_location loc;

    constexpr explicit nrInfo_t(std::source_location l = std::source_location::current()) : loc(l)
    {
    }

    template <typename... Args> inline void operator()(std::format_string<Args...> fmt, Args &&...args) const noexcept
    {
        auto formatted = std::format(fmt, std::forward<Args>(args)...);
        std::print(std::cout,
                   "[nrInfo]\n"
                   "  msg  : {}\n"
                   "  file : {}\n"
                   "  line : {}\n"
                   "  func : {}\n",
                   formatted, loc.file_name(), loc.line(), loc.function_name());
        std::cout.flush();
    }
};
} // namespace detail

export namespace nr
{

inline auto nrAssert(bool condition, std::source_location loc = std::source_location::current())
{
    return detail::nrAssert_t{condition, loc};
}

inline auto nrInfo(std::source_location loc = std::source_location::current())
{
    return detail::nrInfo_t{loc};
}

} // namespace nr