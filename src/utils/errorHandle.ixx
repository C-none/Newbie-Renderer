export module nr.utils:errorHandle;
import :staticUtilsConstants;
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

inline constexpr std::size_t maximumLogChannelBytes = 32u;

// Structural carrier so a log channel can be supplied as a non-type template parameter.
struct LogChannel
{
    std::array<char, maximumLogChannelBytes> storage{};
    std::size_t length = 0u;

    template <std::size_t Extent>
    consteval LogChannel(const char (&text)[Extent]) : length(Extent - 1u)
    {
        static_assert(Extent <= maximumLogChannelBytes, "Log channel exceeds maximumLogChannelBytes.");
        std::ranges::copy(std::string_view{text, Extent - 1u}, storage.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const noexcept
    {
        return std::string_view{storage.data(), length};
    }
};

// Captures the caller location alongside a compile-time checked format string, so the
// formatting work stays inside the reporting entry point instead of the call site.
template <typename... Args> struct FormatWithLocation
{
    std::format_string<Args...> format;
    std::source_location location;

    template <typename Text>
        requires std::convertible_to<const Text &, std::string_view>
    consteval FormatWithLocation(const Text &text, std::source_location loc = std::source_location::current())
        : format(text), location(loc)
    {
    }
};

template <typename... Args> using FormatContext = FormatWithLocation<std::type_identity_t<Args>...>;
} // namespace detail

constexpr inline void nrAssert(bool condition, std::source_location loc = std::source_location::current())
{
    if (!condition)
    {
        detail::emitAssertion({}, loc);
        std::exit(1);
    }
}

template <typename... Args>
constexpr inline void nrAssert(bool condition, detail::FormatContext<Args...> message, Args &&...args)
{
    if (!condition)
    {
        detail::emitAssertion(std::format(message.format, std::forward<Args>(args)...), message.location);
        std::exit(1);
    }
}

// Error-level logs are always fatal: they emit regardless of globalLogLevel and terminate the process.
// A recoverable condition must be reported at warning level or lower.
template <LogLevel Level, detail::LogChannel Channel = detail::LogChannel{"LOG"}, typename... Args>
constexpr inline void nrLog(detail::FormatContext<Args...> message, Args &&...args)
{
    if constexpr (globalLogLevel <= Level || Level == LogLevel::error)
    {
        detail::emitLog(Level, Channel.view(), std::format(message.format, std::forward<Args>(args)...),
                        message.location);
    }

    if constexpr (Level == LogLevel::error)
    {
        detail::shutdownNdjsonLogs();
        std::exit(1);
    }
}

template <LogLevel Level, typename... Args>
constexpr inline void nrVulkan(detail::FormatContext<Args...> message, Args &&...args)
{
    if constexpr (globalLogLevel <= Level)
    {
        detail::emitCompactLog(Level, "VULKAN", std::format(message.format, std::forward<Args>(args)...));
    }
}

template <LogLevel Level = LogLevel::info>
constexpr inline void nrCompactRecord(std::string_view schema, std::string_view payload)
{
    detail::emitCompactRecord(Level, schema, payload);
}

} // namespace nr
