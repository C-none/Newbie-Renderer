module nr.utils;

import :errorHandle;
import :staticUtils;
import dependency.json;
import dependency.processLease;
import std;

namespace nr
{
namespace detail
{
namespace
{
namespace json = dependency::json;
using Json = json::JsonValue;
using JsonObject = Json::Object;

inline constexpr std::size_t maximumSerializedLogRecordBytes = 16u * 1024u * 1024u;
inline constexpr std::uintmax_t minimumNdjsonFileBytes = 1024u;
inline constexpr std::size_t maximumRetainedFileCount = 99u;
inline constexpr std::size_t maximumQueueCapacity = 1024u * 1024u;

enum class NdjsonTarget : std::uint8_t
{
    engine,
    options,
};

struct PendingRecord
{
    NdjsonTarget target = NdjsonTarget::engine;
    std::string line{};
};

std::mutex &logWriteMutex() noexcept
{
    static auto *mutex = new std::mutex{};
    return *mutex;
}

[[nodiscard]] std::string singleLine(std::string_view text)
{
    auto result = std::string{text};
    std::ranges::replace_if(result, [](char value) { return value == '\r' || value == '\n'; }, ' ');
    return result;
}

[[nodiscard]] std::int64_t unixTimestampNanoseconds() noexcept
{
    return static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count());
}

[[nodiscard]] std::string makeGeneratedSessionId()
{
    return std::format("session_{}", unixTimestampNanoseconds());
}

[[nodiscard]] std::expected<std::string, std::string> serializeRecord(JsonObject record)
{
    auto output = std::string{};
    auto const error = json::serializeJson(Json{std::move(record)}, output, maximumSerializedLogRecordBytes);
    if (error != json::JsonError::none)
    {
        return std::unexpected{"NDJSON record exceeded the serialization limit."};
    }
    return output;
}

[[nodiscard]] std::expected<std::string, std::string> makeSessionRecord(const RotatingNdjsonLogConfig &config,
                                                                        std::string_view stream,
                                                                        const std::filesystem::path &activePath)
{
    return serializeRecord(JsonObject{
        {"active_path", Json{activePath.generic_string()}},
        {"maximum_file_bytes", Json{static_cast<std::uint64_t>(config.maximumFileBytes)}},
        {"retained_file_count", Json{static_cast<std::uint64_t>(config.retainedFileCount)}},
        {"schema", Json{"NR_LOG_SESSION_V1"}},
        {"session_id", Json{config.sessionId}},
        {"stream", Json{stream}},
        {"timestamp_unix_ns", Json{unixTimestampNanoseconds()}},
    });
}

[[nodiscard]] std::expected<std::string, std::string> makeEngineRecord(LogLevel level, std::string_view channel,
                                                                       std::string_view context,
                                                                       std::optional<std::source_location> location)
{
    auto record = JsonObject{
        {"channel", Json{channel}},
        {"level", Json{logLevelNames[static_cast<std::size_t>(level)]}},
        {"message", Json{context.empty() ? std::string_view{"(none)"} : context}},
        {"schema", Json{"NR_LOG_V1"}},
        {"timestamp_unix_ns", Json{unixTimestampNanoseconds()}},
    };
    if (location.has_value())
    {
        record.emplace("source", Json{JsonObject{
                                     {"file", Json{location->file_name()}},
                                     {"function", Json{location->function_name()}},
                                     {"line", Json{static_cast<std::uint64_t>(location->line())}},
                                 }});
    }
    return serializeRecord(std::move(record));
}

[[nodiscard]] std::expected<std::string, std::string> makeCompactRecord(LogLevel level, std::string_view schema,
                                                                        std::string_view payload)
{
    auto parsed = json::parseJson(payload);
    auto record = JsonObject{};
    if (parsed.valid() && std::holds_alternative<JsonObject>(parsed.value->storage))
    {
        record = std::get<JsonObject>(std::move(parsed.value->storage));
    }
    else
    {
        record.emplace("payload_error", Json{"payload_must_be_a_json_object"});
        record.emplace("raw_payload", Json{payload});
    }
    record.insert_or_assign("level", Json{logLevelNames[static_cast<std::size_t>(level)]});
    record.insert_or_assign("schema", Json{schema});
    record.insert_or_assign("timestamp_unix_ns", Json{unixTimestampNanoseconds()});
    return serializeRecord(std::move(record));
}

[[nodiscard]] std::string filesystemFailure(std::string_view operation, const std::filesystem::path &path,
                                            const std::error_code &error)
{
    return std::format("{} '{}' failed: {}", operation, path.generic_string(), error.message());
}

class ActiveLogDirectoryLease
{
  public:
    ActiveLogDirectoryLease() noexcept = default;
    ~ActiveLogDirectoryLease()
    {
        static_cast<void>(release());
    }

    ActiveLogDirectoryLease(const ActiveLogDirectoryLease &) = delete;
    ActiveLogDirectoryLease &operator=(const ActiveLogDirectoryLease &) = delete;
    ActiveLogDirectoryLease(ActiveLogDirectoryLease &&other) noexcept
        : path_(std::move(other.path_)), kernelLease_(std::move(other.kernelLease_)),
          acquired_(std::exchange(other.acquired_, false))
    {
    }
    ActiveLogDirectoryLease &operator=(ActiveLogDirectoryLease &&other) noexcept
    {
        if (this != std::addressof(other))
        {
            static_cast<void>(release());
            path_ = std::move(other.path_);
            kernelLease_ = std::move(other.kernelLease_);
            acquired_ = std::exchange(other.acquired_, false);
        }
        return *this;
    }

    [[nodiscard]] std::expected<void, std::string> acquire(const std::filesystem::path &directory)
    {
        auto kernelAcquired = kernelLease_.acquire(directory);
        if (!kernelAcquired)
        {
            if (kernelAcquired.error().error == dependency::process::ExclusiveDirectoryLeaseError::alreadyOwned)
            {
                return std::unexpected{std::format("NDJSON log directory '{}' is already owned by another viewer.",
                                                   directory.generic_string())};
            }
            return std::unexpected{std::format("Acquiring the NDJSON log-directory kernel lease failed: {}",
                                               kernelAcquired.error().detail)};
        }

        path_ = directory / ".active-viewer";
        auto error = std::error_code{};
        auto const status = std::filesystem::symlink_status(path_, error);
        if (error && error != std::errc::no_such_file_or_directory)
        {
            return std::unexpected{filesystemFailure("Inspecting NDJSON viewer marker", path_, error)};
        }
        error.clear();
        if (std::filesystem::exists(status))
        {
            if (!std::filesystem::is_directory(status))
            {
                return std::unexpected{
                    std::format("NDJSON viewer marker '{}' is not a directory.", path_.generic_string())};
            }
            auto const markerEmpty = std::filesystem::is_empty(path_, error);
            if (error)
            {
                return std::unexpected{filesystemFailure("Inspecting stale NDJSON viewer marker", path_, error)};
            }
            if (!markerEmpty)
            {
                return std::unexpected{std::format(
                    "NDJSON viewer marker '{}' is not empty; refusing automatic recovery.", path_.generic_string())};
            }
            auto const removed = std::filesystem::remove(path_, error);
            if (error)
            {
                return std::unexpected{filesystemFailure("Removing stale NDJSON viewer marker", path_, error)};
            }
            if (!removed)
            {
                return std::unexpected{std::format(
                    "Removing stale NDJSON viewer marker '{}' did not remove a directory.", path_.generic_string())};
            }
        }

        auto const created = std::filesystem::create_directory(path_, error);
        if (error)
        {
            return std::unexpected{filesystemFailure("Creating NDJSON viewer marker", path_, error)};
        }
        if (!created)
        {
            return std::unexpected{
                std::format("Creating NDJSON viewer marker '{}' did not create a directory.", path_.generic_string())};
        }

        acquired_ = true;
        return {};
    }

    [[nodiscard]] std::error_code release() noexcept
    {
        if (!acquired_)
        {
            return {};
        }
        auto error = std::error_code{};
        static_cast<void>(std::filesystem::remove(path_, error));
        kernelLease_.release();
        acquired_ = false;
        path_.clear();
        return error;
    }

  private:
    std::filesystem::path path_{};
    dependency::process::ExclusiveDirectoryLease kernelLease_{};
    bool acquired_ = false;
};

class RotatingNdjsonFile
{
  public:
    [[nodiscard]] std::expected<void, std::string> start(std::filesystem::path activePath,
                                                         std::uintmax_t maximumFileBytes, std::size_t retainedFileCount,
                                                         std::string sessionRecord)
    {
        static_cast<void>(closeChecked());
        activePath_ = std::move(activePath);
        maximumFileBytes_ = maximumFileBytes;
        retainedFileCount_ = retainedFileCount;
        sessionRecord_ = std::move(sessionRecord);

        auto error = std::error_code{};
        auto const activeExists = std::filesystem::exists(activePath_, error);
        if (error)
        {
            return std::unexpected{filesystemFailure("Inspecting active NDJSON log", activePath_, error)};
        }
        if (activeExists)
        {
            auto const activeBytes = std::filesystem::file_size(activePath_, error);
            if (error)
            {
                return std::unexpected{filesystemFailure("Reading active NDJSON log size", activePath_, error)};
            }
            if (activeBytes > 0u)
            {
                auto rotated = rotateHistory();
                if (!rotated)
                {
                    return rotated;
                }
            }
        }
        auto pruned = pruneExcessHistory();
        if (!pruned)
        {
            return pruned;
        }
        return openTruncated();
    }

    [[nodiscard]] std::expected<void, std::string> append(std::string_view line)
    {
        auto const lineBytes = static_cast<std::uintmax_t>(line.size());
        auto const requiredBytes = lineBytes == std::numeric_limits<std::uintmax_t>::max() ? lineBytes : lineBytes + 1u;
        auto const wouldExceedLimit = currentBytes_ > maximumFileBytes_ ||
                                      requiredBytes > maximumFileBytes_ - std::min(currentBytes_, maximumFileBytes_);
        if (currentBytes_ > sessionRecordBytes_ && wouldExceedLimit)
        {
            auto rotated = rotateCurrent();
            if (!rotated)
            {
                return rotated;
            }
        }

        stream_.write(line.data(), static_cast<std::streamsize>(line.size()));
        stream_.put('\n');
        if (!stream_)
        {
            return std::unexpected{std::format("Writing active NDJSON log '{}' failed.", activePath_.generic_string())};
        }
        currentBytes_ += requiredBytes;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> flush()
    {
        stream_.flush();
        if (!stream_)
        {
            return std::unexpected{
                std::format("Flushing active NDJSON log '{}' failed.", activePath_.generic_string())};
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> closeChecked()
    {
        auto failure = std::optional<std::string>{};
        if (stream_.is_open())
        {
            stream_.flush();
            if (!stream_)
            {
                failure =
                    std::format("Flushing active NDJSON log '{}' before close failed.", activePath_.generic_string());
            }
            stream_.close();
            if (stream_.fail() && !failure.has_value())
            {
                failure = std::format("Closing active NDJSON log '{}' failed.", activePath_.generic_string());
            }
        }
        currentBytes_ = 0u;
        sessionRecordBytes_ = 0u;
        if (failure.has_value())
        {
            return std::unexpected{std::move(*failure)};
        }
        return {};
    }

  private:
    [[nodiscard]] std::filesystem::path rotatedPath(std::size_t index) const
    {
        return activePath_.parent_path() /
               std::format("{}.{}{}", activePath_.stem().string(), index, activePath_.extension().string());
    }

    [[nodiscard]] std::expected<void, std::string> rotateHistory()
    {
        auto failure = std::optional<std::string>{};
        auto const indices = std::views::iota(std::size_t{1u}, retainedFileCount_ + 1u) | std::views::reverse;
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (failure.has_value())
            {
                return;
            }

            auto const source = index == 1u ? activePath_ : rotatedPath(index - 1u);
            auto const destination = rotatedPath(index);
            auto error = std::error_code{};
            auto const sourceExists = std::filesystem::exists(source, error);
            if (error)
            {
                failure = filesystemFailure("Inspecting NDJSON rotation source", source, error);
                return;
            }
            if (sourceExists)
            {
                std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing,
                                           error);
                if (error)
                {
                    failure = filesystemFailure("Copying NDJSON rotation file", destination, error);
                }
                return;
            }

            static_cast<void>(std::filesystem::remove(destination, error));
            if (error)
            {
                failure = filesystemFailure("Removing stale NDJSON rotation file", destination, error);
            }
        });
        if (failure.has_value())
        {
            return std::unexpected{std::move(*failure)};
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> pruneExcessHistory()
    {
        auto failure = std::optional<std::string>{};
        auto const indices = std::views::iota(retainedFileCount_ + 1u, maximumRetainedFileCount + 1u);
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (failure.has_value())
            {
                return;
            }
            auto const path = rotatedPath(index);
            auto error = std::error_code{};
            static_cast<void>(std::filesystem::remove(path, error));
            if (error)
            {
                failure = filesystemFailure("Removing excess NDJSON rotation file", path, error);
            }
        });
        if (failure.has_value())
        {
            return std::unexpected{std::move(*failure)};
        }
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> openTruncated()
    {
        stream_.clear();
        stream_.open(activePath_, std::ios::binary | std::ios::trunc);
        if (!stream_.is_open())
        {
            return std::unexpected{std::format("Opening active NDJSON log '{}' failed.", activePath_.generic_string())};
        }

        stream_.write(sessionRecord_.data(), static_cast<std::streamsize>(sessionRecord_.size()));
        stream_.put('\n');
        stream_.flush();
        if (!stream_)
        {
            static_cast<void>(closeChecked());
            return std::unexpected{
                std::format("Writing NDJSON session record to '{}' failed.", activePath_.generic_string())};
        }
        sessionRecordBytes_ = static_cast<std::uintmax_t>(sessionRecord_.size()) + 1u;
        currentBytes_ = sessionRecordBytes_;
        return {};
    }

    [[nodiscard]] std::expected<void, std::string> rotateCurrent()
    {
        auto closed = closeChecked();
        if (!closed)
        {
            return closed;
        }
        auto rotated = rotateHistory();
        if (!rotated)
        {
            return rotated;
        }
        return openTruncated();
    }

    std::filesystem::path activePath_{};
    std::uintmax_t maximumFileBytes_ = 0u;
    std::size_t retainedFileCount_ = 0u;
    std::string sessionRecord_{};
    std::ofstream stream_{};
    std::uintmax_t currentBytes_ = 0u;
    std::uintmax_t sessionRecordBytes_ = 0u;
};

class NdjsonSink
{
  public:
    ~NdjsonSink()
    {
        stop();
    }

    [[nodiscard]] std::expected<void, std::string> start(RotatingNdjsonLogConfig config)
    {
        if (config.directory.empty())
        {
            return std::unexpected{"The NDJSON log directory must not be empty."};
        }
        if (config.maximumFileBytes < minimumNdjsonFileBytes)
        {
            return std::unexpected{
                std::format("The NDJSON maximum file size must be at least {} bytes.", minimumNdjsonFileBytes)};
        }
        if (config.retainedFileCount > maximumRetainedFileCount)
        {
            return std::unexpected{
                std::format("The NDJSON retained file count must not exceed {}.", maximumRetainedFileCount)};
        }
        if (config.queueCapacity == 0u || config.queueCapacity > maximumQueueCapacity)
        {
            return std::unexpected{std::format("The NDJSON queue capacity must be in [1, {}].", maximumQueueCapacity)};
        }
        if (config.sessionId.empty())
        {
            config.sessionId = makeGeneratedSessionId();
        }

        auto lock = std::unique_lock{mutex_};
        if (configured_)
        {
            return std::unexpected{"A rotating NDJSON log session is already active."};
        }

        auto error = std::error_code{};
        static_cast<void>(std::filesystem::create_directories(config.directory, error));
        if (error)
        {
            return std::unexpected{filesystemFailure("Creating NDJSON log directory", config.directory, error)};
        }
        auto canonicalDirectory = std::filesystem::canonical(config.directory, error);
        if (error)
        {
            return std::unexpected{filesystemFailure("Canonicalizing NDJSON log directory", config.directory, error)};
        }
        config.directory = std::move(canonicalDirectory);

        auto directoryLease = ActiveLogDirectoryLease{};
        auto leaseAcquired = directoryLease.acquire(config.directory);
        if (!leaseAcquired)
        {
            return leaseAcquired;
        }

        auto const enginePath = config.directory / std::filesystem::path{std::string{engineNdjsonLogFileName}};
        auto const optionPath = config.directory / std::filesystem::path{std::string{optionNdjsonLogFileName}};
        auto engineSessionRecord = makeSessionRecord(config, "engine", enginePath);
        if (!engineSessionRecord)
        {
            return std::unexpected{std::move(engineSessionRecord.error())};
        }
        auto optionSessionRecord = makeSessionRecord(config, "options", optionPath);
        if (!optionSessionRecord)
        {
            return std::unexpected{std::move(optionSessionRecord.error())};
        }

        auto engineStarted = engine_.start(enginePath, config.maximumFileBytes, config.retainedFileCount,
                                           std::move(*engineSessionRecord));
        if (!engineStarted)
        {
            return engineStarted;
        }
        auto optionStarted = options_.start(optionPath, config.maximumFileBytes, config.retainedFileCount,
                                            std::move(*optionSessionRecord));
        if (!optionStarted)
        {
            static_cast<void>(engine_.closeChecked());
            return optionStarted;
        }

        directoryLease_ = std::move(directoryLease);
        optionPath_ = optionPath;
        queue_.clear();
        queueCapacity_ = config.queueCapacity;
        configured_ = true;
        accepting_ = true;
        stopping_ = false;
        writer_ = std::jthread{[this] { writerLoop(); }};
        return {};
    }

    [[nodiscard]] bool enqueue(NdjsonTarget target, std::string line)
    {
        auto lock = std::unique_lock{mutex_};
        spaceAvailable_.wait(lock, [&] { return !accepting_ || queue_.size() < queueCapacity_; });
        if (!accepting_)
        {
            return false;
        }

        queue_.push_back(PendingRecord{
            .target = target,
            .line = std::move(line),
        });
        recordsAvailable_.notify_one();
        return true;
    }

    void stop() noexcept
    {
        {
            auto lock = std::unique_lock{mutex_};
            if (!configured_)
            {
                return;
            }
            if (stopping_)
            {
                stopCompleted_.wait(lock, [&] { return !configured_; });
                return;
            }
            stopping_ = true;
            accepting_ = false;
            recordsAvailable_.notify_all();
            spaceAvailable_.notify_all();
        }

        if (writer_.joinable())
        {
            writer_.join();
        }
        auto engineClosed = engine_.closeChecked();
        auto optionsClosed = options_.closeChecked();
        auto const leaseError = directoryLease_.release();
        if (!engineClosed || !optionsClosed || leaseError)
        {
            auto consoleLock = std::scoped_lock{logWriteMutex()};
            if (!engineClosed)
            {
                std::print(std::cerr, "{}[NR LOG:ERROR]{} {}\n", ansiRed, ansiReset, engineClosed.error());
            }
            if (!optionsClosed)
            {
                std::print(std::cerr, "{}[NR LOG:ERROR]{} {}\n", ansiRed, ansiReset, optionsClosed.error());
            }
            if (leaseError)
            {
                std::print(std::cerr, "{}[NR LOG:ERROR]{} Releasing the NDJSON viewer lease failed: {}\n", ansiRed,
                           ansiReset, leaseError.message());
            }
            std::cerr.flush();
        }

        {
            auto lock = std::scoped_lock{mutex_};
            queue_.clear();
            writer_ = {};
            configured_ = false;
            stopping_ = false;
            optionPath_.clear();
        }
        stopCompleted_.notify_all();
    }

    [[nodiscard]] std::filesystem::path activeOptionPath()
    {
        auto lock = std::scoped_lock{mutex_};
        return optionPath_;
    }

  private:
    void writerLoop() noexcept
    {
        while (true)
        {
            auto batch = std::vector<PendingRecord>{};
            {
                auto lock = std::unique_lock{mutex_};
                recordsAvailable_.wait(lock, [&] { return !queue_.empty() || !accepting_; });
                if (queue_.empty() && !accepting_)
                {
                    break;
                }

                batch.reserve(queue_.size());
                std::ranges::move(queue_, std::back_inserter(batch));
                queue_.clear();
                spaceAvailable_.notify_all();
            }

            auto failure = std::optional<std::string>{};
            std::ranges::for_each(batch, [&](const PendingRecord &record) {
                if (failure.has_value())
                {
                    return;
                }
                auto appended =
                    record.target == NdjsonTarget::engine ? engine_.append(record.line) : options_.append(record.line);
                if (!appended)
                {
                    failure = std::move(appended.error());
                }
            });
            if (!failure.has_value())
            {
                auto engineFlushed = engine_.flush();
                if (!engineFlushed)
                {
                    failure = std::move(engineFlushed.error());
                }
            }
            if (!failure.has_value())
            {
                auto optionsFlushed = options_.flush();
                if (!optionsFlushed)
                {
                    failure = std::move(optionsFlushed.error());
                }
            }

            auto fallbackRecords = std::vector<PendingRecord>{};
            {
                auto lock = std::scoped_lock{mutex_};
                if (failure.has_value())
                {
                    accepting_ = false;
                    fallbackRecords.reserve(batch.size() + queue_.size());
                    std::ranges::move(batch, std::back_inserter(fallbackRecords));
                    std::ranges::move(queue_, std::back_inserter(fallbackRecords));
                    queue_.clear();
                }
                spaceAvailable_.notify_all();
            }

            if (failure.has_value())
            {
                auto consoleLock = std::scoped_lock{logWriteMutex()};
                std::print(std::cerr, "{}[NR LOG:ERROR]{} Rotating NDJSON sink disabled: {}\n", ansiRed, ansiReset,
                           *failure);
                std::ranges::for_each(fallbackRecords,
                                      [](const PendingRecord &record) { std::println(std::cerr, "{}", record.line); });
                std::cerr.flush();
                return;
            }
        }

        auto failure = std::optional<std::string>{};
        auto engineFlushed = engine_.flush();
        if (!engineFlushed)
        {
            failure = std::move(engineFlushed.error());
        }
        auto optionsFlushed = options_.flush();
        if (!optionsFlushed && !failure.has_value())
        {
            failure = std::move(optionsFlushed.error());
        }
        if (failure.has_value())
        {
            auto consoleLock = std::scoped_lock{logWriteMutex()};
            std::print(std::cerr, "{}[NR LOG:ERROR]{} Rotating NDJSON final flush failed: {}\n", ansiRed, ansiReset,
                       *failure);
            std::cerr.flush();
        }
    }

    std::mutex mutex_{};
    std::condition_variable recordsAvailable_{};
    std::condition_variable spaceAvailable_{};
    std::condition_variable stopCompleted_{};
    std::deque<PendingRecord> queue_{};
    std::size_t queueCapacity_ = 0u;
    bool configured_ = false;
    bool accepting_ = false;
    bool stopping_ = false;
    ActiveLogDirectoryLease directoryLease_{};
    std::filesystem::path optionPath_{};
    RotatingNdjsonFile engine_{};
    RotatingNdjsonFile options_{};
    std::jthread writer_{};
};

NdjsonSink &ndjsonSink() noexcept
{
    // Process-lifetime storage keeps late static destructors from calling into a destroyed sink.
    static auto *sink = new NdjsonSink{};
    return *sink;
}

void writeConsoleLog(LogLevel level, std::string_view channel, std::string_view context, std::source_location loc)
{
    auto const location = std::format("{}:{}", loc.file_name(), loc.line());
    auto lock = std::scoped_lock{logWriteMutex()};
    auto &stream = levelStream(level);
    std::print(stream, "{}[NR {}:{}]{} {}\n{}{}{}\n{}\n", levelColor(level), channel,
               logLevelNames[static_cast<std::size_t>(level)], ansiReset, location, ansiPaleYellow, loc.function_name(),
               ansiReset, context.empty() ? "(none)" : context);
    stream.flush();
}

void writeCompactConsoleLog(LogLevel level, std::string_view channel, std::string_view context)
{
    auto lock = std::scoped_lock{logWriteMutex()};
    auto &stream = levelStream(level);
    std::print(stream, "{}[NR {}:{}]{} {}\n", levelColor(level), channel,
               logLevelNames[static_cast<std::size_t>(level)], ansiReset, context.empty() ? "(none)" : context);
    stream.flush();
}
} // namespace

std::ostream &levelStream(LogLevel level)
{
    return level == LogLevel::error ? std::cerr : std::cout;
}

void emitLog(LogLevel level, std::string_view channel, std::string_view context, std::source_location loc)
{
    auto record = makeEngineRecord(level, channel, context, loc);
    auto const persisted = record.has_value() && ndjsonSink().enqueue(NdjsonTarget::engine, std::move(*record));
    if (level != LogLevel::info || !persisted)
    {
        writeConsoleLog(level, channel, context, loc);
    }
}

void emitCompactLog(LogLevel level, std::string_view channel, std::string_view context)
{
    auto record = makeEngineRecord(level, channel, context, std::nullopt);
    auto const persisted = record.has_value() && ndjsonSink().enqueue(NdjsonTarget::engine, std::move(*record));
    if (level != LogLevel::info || !persisted)
    {
        writeCompactConsoleLog(level, channel, context);
    }
}

void emitCompactRecord(LogLevel level, std::string_view schema, std::string_view payload)
{
    auto record = makeCompactRecord(level, schema, payload);
    auto const persisted = record.has_value() && ndjsonSink().enqueue(NdjsonTarget::options, std::move(*record));
    if (!persisted)
    {
        auto const safeSchema = singleLine(schema);
        auto const safePayload = singleLine(payload);
        auto lock = std::scoped_lock{logWriteMutex()};
        auto &stream = levelStream(level);
        std::print(stream, "{} {}\n", safeSchema, safePayload);
        stream.flush();
    }
}

void emitAssertion(std::string_view context, std::source_location loc)
{
    auto record = makeEngineRecord(LogLevel::error, "ASSERT", context, loc);
    if (record.has_value())
    {
        static_cast<void>(ndjsonSink().enqueue(NdjsonTarget::engine, std::move(*record)));
    }

    auto const location = std::format("{}:{}", loc.file_name(), loc.line());
    {
        auto lock = std::scoped_lock{logWriteMutex()};
        std::print(std::cerr, "{}[NR ASSERT]{} {}\n{}{}{}\n{}\n", ansiRedBold, ansiReset, location, ansiPaleYellow,
                   loc.function_name(), ansiReset, context.empty() ? "(none)" : context);
        std::cerr.flush();
    }
    shutdownNdjsonLogs();
}

void shutdownNdjsonLogs() noexcept
{
    ndjsonSink().stop();
}
} // namespace detail

std::filesystem::path defaultNdjsonLogDirectory()
{
    return std::filesystem::path{std::string{projectRoot}} / "build" / "app" / "logs";
}

std::filesystem::path defaultEngineNdjsonLogPath()
{
    return defaultNdjsonLogDirectory() / std::filesystem::path{std::string{engineNdjsonLogFileName}};
}

std::filesystem::path defaultOptionNdjsonLogPath()
{
    return defaultNdjsonLogDirectory() / std::filesystem::path{std::string{optionNdjsonLogFileName}};
}

std::filesystem::path activeOptionNdjsonLogPath()
{
    auto path = detail::ndjsonSink().activeOptionPath();
    return path.empty() ? defaultOptionNdjsonLogPath() : path;
}

RotatingNdjsonLogSession::RotatingNdjsonLogSession(bool active) noexcept : active_(active)
{
}

RotatingNdjsonLogSession::~RotatingNdjsonLogSession()
{
    if (active_)
    {
        detail::ndjsonSink().stop();
    }
}

RotatingNdjsonLogSession::RotatingNdjsonLogSession(RotatingNdjsonLogSession &&other) noexcept
    : active_(std::exchange(other.active_, false))
{
}

RotatingNdjsonLogSession &RotatingNdjsonLogSession::operator=(RotatingNdjsonLogSession &&other) noexcept
{
    if (this != std::addressof(other))
    {
        if (active_)
        {
            detail::ndjsonSink().stop();
        }
        active_ = std::exchange(other.active_, false);
    }
    return *this;
}

std::expected<RotatingNdjsonLogSession, std::string> RotatingNdjsonLogSession::start(RotatingNdjsonLogConfig config)
{
    auto started = detail::ndjsonSink().start(std::move(config));
    if (!started)
    {
        return std::unexpected{std::move(started.error())};
    }
    return RotatingNdjsonLogSession{true};
}

bool RotatingNdjsonLogSession::active() const noexcept
{
    return active_;
}
} // namespace nr
