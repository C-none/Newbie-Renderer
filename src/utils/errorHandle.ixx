module;
export module nr.utils:errorHandle;
import :staticUtils;
import std;

export namespace nr
{
// LogLevel enum is now auto-generated in staticUtilsConstants.h

constexpr inline void nrAssert(bool condition, std::string_view context = "", std::source_location loc = std::source_location::current())
{
    if (!condition)
    {
        std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());

        std::print(std::cerr,
                     "[nrAssert] FAILED\n"
                     "  location : {}\n"
                     "  function : {}\n"
                     "  message  : {}\n",
                     locationStr, loc.function_name(), context.empty() ? "(none)" : context);

        std::exit(1);
    }
}
template <LogLevel Level = LogLevel::info> constexpr inline void nrInfo(std::string_view context = "", std::source_location loc = std::source_location::current())
{
    // Compile-time log level filtering (type-safe enum comparison)
    if constexpr (globalLogLevel <= Level)
    {
        constexpr bool isError = Level == LogLevel::error;
        std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());
        std::print(isError ? std::cerr : std::cout,
                     "[nr::{}]\n"
                     "  location : {}\n"
                     "  function : {}\n"
                     "  message  : {}\n",
                     logLevelNames[static_cast<size_t>(Level)], locationStr, loc.function_name(), context.empty() ? "(none)" : context);
        if constexpr (isError)
            std::exit(1);
    }
}

} // namespace nr