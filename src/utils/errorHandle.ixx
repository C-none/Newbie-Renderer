export module nr.utils:errorHandle;
import :staticUtils;
import std;

export namespace nr
{
// LogLevel enum is generated in the staticUtilsConstants module partition.

inline constexpr std::string_view engineNdjsonLogFileName = "engine.ndjson";
inline constexpr std::string_view optionNdjsonLogFileName = "options.ndjson";
inline constexpr std::uintmax_t defaultNdjsonMaximumFileBytes = 32u * 1024u * 1024u;
inline constexpr std::size_t defaultNdjsonRetainedFileCount = 4u;
inline constexpr std::size_t defaultNdjsonQueueCapacity = 8192u;

[[nodiscard]] std::filesystem::path defaultNdjsonLogDirectory();
[[nodiscard]] std::filesystem::path defaultEngineNdjsonLogPath();
[[nodiscard]] std::filesystem::path defaultOptionNdjsonLogPath();
[[nodiscard]] std::filesystem::path activeOptionNdjsonLogPath();

struct RotatingNdjsonLogConfig
{
    std::filesystem::path directory = defaultNdjsonLogDirectory();
    std::string sessionId{};
    std::uintmax_t maximumFileBytes = defaultNdjsonMaximumFileBytes;
    std::size_t retainedFileCount = defaultNdjsonRetainedFileCount;
    std::size_t queueCapacity = defaultNdjsonQueueCapacity;
};

class RotatingNdjsonLogSession
{
  public:
    RotatingNdjsonLogSession() noexcept = default;
    ~RotatingNdjsonLogSession();

    RotatingNdjsonLogSession(const RotatingNdjsonLogSession &) = delete;
    RotatingNdjsonLogSession &operator=(const RotatingNdjsonLogSession &) = delete;
    RotatingNdjsonLogSession(RotatingNdjsonLogSession &&other) noexcept;
    RotatingNdjsonLogSession &operator=(RotatingNdjsonLogSession &&other) noexcept;

    [[nodiscard]] static std::expected<RotatingNdjsonLogSession, std::string> start(RotatingNdjsonLogConfig config);
    [[nodiscard]] bool active() const noexcept;

  private:
    explicit RotatingNdjsonLogSession(bool active) noexcept;

    bool active_ = false;
};

namespace detail
{
inline constexpr std::string_view ansiReset = "\x1b[0m";
inline constexpr std::string_view ansiRedBold = "\x1b[1;31m";
inline constexpr std::string_view ansiRed = "\x1b[31m";
inline constexpr std::string_view ansiYellow = "\x1b[33m";
inline constexpr std::string_view ansiPaleYellow = "\x1b[93m";
inline constexpr std::string_view ansiCyan = "\x1b[36m";

constexpr std::string_view levelColor(LogLevel level)
{
    switch (level)
    {
    case LogLevel::info:
        return ansiCyan;
    case LogLevel::warning:
        return ansiYellow;
    case LogLevel::error:
        return ansiRed;
    default:
        return ansiReset;
    }
}

std::ostream &levelStream(LogLevel level);

void emitLog(LogLevel level, std::string_view channel, std::string_view context, std::source_location loc);

void emitCompactLog(LogLevel level, std::string_view channel, std::string_view context);

void emitCompactRecord(LogLevel level, std::string_view schema, std::string_view payload);

void emitAssertion(std::string_view context, std::source_location loc);

void shutdownNdjsonLogs() noexcept;
} // namespace detail

constexpr inline void nrAssert(bool condition, std::string_view context = "",
                               std::source_location loc = std::source_location::current())
{
    if (!condition)
    {
        detail::emitAssertion(context, loc);
        std::exit(1);
    }
}

template <typename ContextFactory>
    requires std::invocable<ContextFactory &&> &&
             std::convertible_to<std::invoke_result_t<ContextFactory &&>, std::string_view>
constexpr inline void nrAssert(bool condition, ContextFactory &&contextFactory,
                               std::source_location loc = std::source_location::current())
{
    if (!condition)
    {
        nrAssert(false, std::invoke(std::forward<ContextFactory>(contextFactory)), loc);
    }
}

constexpr inline void nrLog(LogLevel level, std::string_view channel, std::string_view context,
                            std::source_location loc = std::source_location::current(), bool terminateOnError = false)
{
    if (globalLogLevel <= level)
    {
        detail::emitLog(level, channel, context, loc);
    }

    if (terminateOnError && level == LogLevel::error)
    {
        detail::shutdownNdjsonLogs();
        std::exit(1);
    }
}

constexpr inline void nrLog(LogLevel level, std::string_view context,
                            std::source_location loc = std::source_location::current(), bool terminateOnError = false)
{
    nrLog(level, "LOG", context, loc, terminateOnError);
}

template <LogLevel Level = LogLevel::info, bool TerminateOnError = (Level == LogLevel::error)>
constexpr inline void nrInfo(std::string_view context = "", std::source_location loc = std::source_location::current())
{
    // Compile-time log level filtering (type-safe enum comparison)
    if constexpr (globalLogLevel <= Level)
    {
        nrLog(Level, "LOG", context, loc, TerminateOnError);
    }
}
constexpr inline void nrVulkan(LogLevel level, std::string_view context,
                               std::source_location /*loc*/ = std::source_location::current())
{
    if (globalLogLevel <= level)
    {
        detail::emitCompactLog(level, "VULKAN", context);
    }
}

template <LogLevel Level = LogLevel::info>
constexpr inline void nrCompactRecord(std::string_view schema, std::string_view payload)
{
    detail::emitCompactRecord(Level, schema, payload);
}

} // namespace nr
