export module dependency.json;

import std;

export namespace dependency::json
{
struct JsonValue
{
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, std::uint64_t, double, std::string, Array, Object>;

    JsonValue() = default;
    JsonValue(std::nullptr_t) : storage(nullptr)
    {
    }
    JsonValue(bool value) : storage(value)
    {
    }
    JsonValue(std::int64_t value) : storage(value)
    {
    }
    JsonValue(std::uint64_t value) : storage(value)
    {
    }
    JsonValue(double value) : storage(value)
    {
    }
    JsonValue(std::string value) : storage(std::move(value))
    {
    }
    JsonValue(std::string_view value) : storage(std::string{value})
    {
    }
    JsonValue(const char *value) : storage(std::string{value})
    {
    }
    JsonValue(Array value) : storage(std::move(value))
    {
    }
    JsonValue(Object value) : storage(std::move(value))
    {
    }

    Storage storage = nullptr;

    [[nodiscard]] friend bool operator==(const JsonValue &, const JsonValue &) = default;
};

enum class JsonError : std::uint8_t
{
    none,
    invalidSyntax,
    invalidUtf8,
    maximumDepth,
    responseTooLarge,
};

struct JsonParseResult
{
    std::optional<JsonValue> value{};
    JsonError error = JsonError::none;

    [[nodiscard]] bool valid() const noexcept
    {
        return value.has_value() && error == JsonError::none;
    }
};

[[nodiscard]] JsonParseResult parseJson(std::string_view text, std::size_t maximumDepth = 32u);
[[nodiscard]] JsonParseResult parseJsonRejectingDuplicateKeys(std::string_view text, std::size_t maximumDepth = 32u);
[[nodiscard]] JsonError serializeJson(const JsonValue &value, std::string &output, std::size_t maximumBytes);
} // namespace dependency::json
