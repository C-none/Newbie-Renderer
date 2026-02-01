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
 
 inline void nrAssert(bool condition, 
                                  std::string_view context = "",
                                  std::source_location loc = std::source_location::current())
 {
        if (!condition)
        {
            std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());
            
            std::println(std::cerr,
                         "[nrAssert] FAILED\n"
                         "  location : {}\n"
                         "  function : {}\n"
                         "  message  : {}\n"                         ,
                         locationStr, loc.function_name(),
                         context.empty() ? "(none)" : context);
        }
 }

 inline void nrInfo(LogLevel level = LogLevel::info, 
                             std::string_view context = "",
                             std::source_location loc = std::source_location::current())
 {
    std::array<std::string_view, static_cast<size_t>(nr::LogLevel::number)> levelDict{"INFO", "WARNING", "ERROR"};
    bool isError = level == nr::LogLevel::error;
    std::string locationStr = std::format("{}:{}", loc.file_name(), loc.line());
    std::println(isError ? std::cerr : std::cout,
                     "[nr::{}]\n"
                     "  location : {}\n"
                     "  function : {}\n"
                     "  message  : {}\n",
                     levelDict[static_cast<size_t>(level)], locationStr, 
                     loc.function_name(), context.empty() ? "(none)" : context);
    if (isError)
        std::exit(1);
}

} // namespace nr