import dependency.json;
import nr.utils;
import std;

namespace
{
namespace json = dependency::json;

inline constexpr std::string_view sessionId = "nr-utils-rotating-ndjson-test";
inline constexpr std::string_view infoMarker = "NR_NDJSON_INFO_MUST_NOT_REACH_CMD";
inline constexpr std::string_view warningMarker = "NR_NDJSON_WARNING_MUST_REACH_CMD";
inline constexpr std::string_view errorMarker = "NR_NDJSON_ERROR_MUST_REACH_CMD";
inline constexpr std::string_view optionMarker = "NR_NDJSON_OPTION_MUST_NOT_REACH_CMD";
inline constexpr std::string_view fatalMarker = "NR_NDJSON_FATAL_MUST_FLUSH";
inline constexpr std::string_view pruneMarker = "NR_NDJSON_RETENTION_PRUNE";

[[nodiscard]] std::string serializePayload(json::JsonValue::Object object)
{
    auto output = std::string{};
    auto const error = json::serializeJson(json::JsonValue{std::move(object)}, output, 4u * 1024u);
    nr::nrAssert(error == json::JsonError::none, "Boost.JSON failed to serialize an NDJSON probe payload.");
    return output;
}

[[nodiscard]] std::expected<std::string, std::string> readFile(const std::filesystem::path &path)
{
    auto stream = std::ifstream{path, std::ios::binary};
    if (!stream.is_open())
    {
        return std::unexpected{std::format("Failed to open '{}'.", path.generic_string())};
    }
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}
} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3)
    {
        std::println(std::cerr, "Expected an NDJSON output directory and optional --fatal argument.");
        return 2;
    }

    auto const mode = argc == 3 ? std::string_view{argv[2]} : std::string_view{};
    auto session = nr::RotatingNdjsonLogSession::start({
        .directory = std::filesystem::path{argv[1]},
        .sessionId = std::string{sessionId},
        .maximumFileBytes = 1024u,
        .retainedFileCount = mode == "--prune" ? 4u : 16u,
        .queueCapacity = 64u,
    });
    if (!session)
    {
        std::println(std::cerr, "Failed to start rotating NDJSON test session: {}", session.error());
        return 3;
    }
    auto canonicalError = std::error_code{};
    auto canonicalDirectory = std::filesystem::canonical(std::filesystem::path{argv[1]}, canonicalError);
    if (canonicalError)
    {
        std::println(std::cerr, "Failed to canonicalize the rotating NDJSON test directory: {}",
                     canonicalError.message());
        return 4;
    }
    auto const expectedOptionPath = canonicalDirectory / "options.ndjson";
    if (nr::activeOptionNdjsonLogPath() != expectedOptionPath)
    {
        std::println(std::cerr, "Active option NDJSON path did not match the configured directory.");
        return 4;
    }

    if (mode == "--prune")
    {
        nr::nrCompactRecord<nr::LogLevel::info>("NR_OPTION_V1", serializePayload(json::JsonValue::Object{
                                                                    {"marker", json::JsonValue{pruneMarker}},
                                                                }));
        return 0;
    }
    if (mode == "--conflict-parent")
    {
        auto const enginePath = canonicalDirectory / "engine.ndjson";
        auto engineBefore = readFile(enginePath);
        auto optionsBefore = readFile(expectedOptionPath);
        if (!engineBefore || !optionsBefore)
        {
            std::println(std::cerr, "Failed to snapshot active logs before the conflict probe: {}{}",
                         engineBefore ? "" : engineBefore.error(), optionsBefore ? "" : optionsBefore.error());
            return 7;
        }

        auto const childCommand = std::format("\"\"{}\" \"{}\" --expect-conflict\"", argv[0], argv[1]);
        auto const childResult = std::system(childCommand.c_str());
        if (childResult != 3)
        {
            std::println(std::cerr, "Concurrent rotating NDJSON probe exited with '{}' instead of 3.", childResult);
            return 8;
        }

        auto engineAfter = readFile(enginePath);
        auto optionsAfter = readFile(expectedOptionPath);
        if (!engineAfter || !optionsAfter || *engineAfter != *engineBefore || *optionsAfter != *optionsBefore)
        {
            std::println(std::cerr, "Concurrent rotating NDJSON probe touched the active log files.");
            return 9;
        }
        return 0;
    }
    if (!mode.empty())
    {
        if (mode != "--fatal")
        {
            std::println(std::cerr, "Unknown rotating NDJSON probe mode.");
            return 5;
        }
        nr::nrInfo<nr::LogLevel::error>(fatalMarker);
        return 6;
    }

    auto const enginePadding = std::string(384u, 'e');
    nr::nrInfo<nr::LogLevel::info>(std::format("{} {}", infoMarker, enginePadding));
    nr::nrInfo<nr::LogLevel::warning>(std::format("{} {}", warningMarker, enginePadding));
    nr::nrInfo<nr::LogLevel::error, false>(std::format("{} {}", errorMarker, enginePadding));

    auto const optionPadding = std::string(256u, 'o');
    auto const recordIndices = std::views::iota(std::uint32_t{0u}, std::uint32_t{12u});
    std::ranges::for_each(recordIndices, [&](std::uint32_t recordIndex) {
        nr::nrCompactRecord<nr::LogLevel::info>(
            "NR_OPTION_V1", serializePayload(json::JsonValue::Object{
                                {"marker", json::JsonValue{optionMarker}},
                                {"padding", json::JsonValue{optionPadding}},
                                {"record_index", json::JsonValue{static_cast<std::uint64_t>(recordIndex)}},
                            }));
    });

    return 0;
}
