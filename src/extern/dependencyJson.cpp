module;

#include <boost/json.hpp>

module dependency.json;

import std;

namespace dependency::json
{
namespace
{
namespace boostJson = boost::json;
using ErrorCode = boost::system::error_code;

struct DuplicateKeyHandler
{
    static constexpr std::size_t max_object_size = boostJson::object::max_size();
    static constexpr std::size_t max_array_size = boostJson::array::max_size();
    static constexpr std::size_t max_key_size = boostJson::string::max_size();
    static constexpr std::size_t max_string_size = boostJson::string::max_size();

    [[nodiscard]] bool on_document_begin(ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_document_end(ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_array_begin(ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_array_end(std::size_t, ErrorCode &) noexcept { return true; }

    [[nodiscard]] bool on_object_begin(ErrorCode &) noexcept
    {
        objectKeys.emplace_back();
        return true;
    }

    [[nodiscard]] bool on_object_end(std::size_t, ErrorCode &) noexcept
    {
        objectKeys.pop_back();
        return true;
    }

    [[nodiscard]] bool on_key_part(boostJson::string_view text, std::size_t total, ErrorCode &) noexcept
    {
        if (total == text.size())
        {
            pendingKey.clear();
        }
        pendingKey.append(text.data(), text.size());
        return true;
    }

    [[nodiscard]] bool on_key(boostJson::string_view text, std::size_t total, ErrorCode &) noexcept
    {
        if (total == text.size())
        {
            pendingKey.clear();
        }
        pendingKey.append(text.data(), text.size());
        return !objectKeys.empty() && objectKeys.back().insert(pendingKey).second;
    }

    [[nodiscard]] bool on_string_part(boostJson::string_view, std::size_t, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_string(boostJson::string_view, std::size_t, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_number_part(boostJson::string_view, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_int64(std::int64_t, boostJson::string_view, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_uint64(std::uint64_t, boostJson::string_view, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_double(double, boostJson::string_view, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_bool(bool, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_null(ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_comment_part(boostJson::string_view, ErrorCode &) noexcept { return true; }
    [[nodiscard]] bool on_comment(boostJson::string_view, ErrorCode &) noexcept { return true; }

    std::vector<std::set<std::string, std::less<>>> objectKeys{};
    std::string pendingKey{};
};

[[nodiscard]] boostJson::parse_options strictJsonOptions(std::size_t maximumDepth) noexcept
{
    auto options = boostJson::parse_options{};
    options.max_depth = maximumDepth;
    options.allow_comments = false;
    options.allow_trailing_commas = false;
    options.allow_infinity_and_nan = false;
    options.allow_invalid_utf8 = false;
    options.allow_invalid_utf16 = false;
    return options;
}

[[nodiscard]] JsonValue fromBoostJson(const boostJson::value &value)
{
    switch (value.kind())
    {
    case boostJson::kind::null:
        return JsonValue{nullptr};
    case boostJson::kind::bool_:
        return JsonValue{value.as_bool()};
    case boostJson::kind::int64: {
        auto const integer = value.as_int64();
        return integer >= 0 ? JsonValue{static_cast<std::uint64_t>(integer)} : JsonValue{integer};
    }
    case boostJson::kind::uint64:
        return JsonValue{value.as_uint64()};
    case boostJson::kind::double_:
        return JsonValue{value.as_double()};
    case boostJson::kind::string: {
        auto const &text = value.as_string();
        return JsonValue{std::string{text.data(), text.size()}};
    }
    case boostJson::kind::array: {
        auto result = JsonValue::Array{};
        result.reserve(value.as_array().size());
        std::ranges::transform(value.as_array(), std::back_inserter(result),
                               [](auto const &element) { return fromBoostJson(element); });
        return JsonValue{std::move(result)};
    }
    case boostJson::kind::object: {
        auto result = JsonValue::Object{};
        std::ranges::for_each(value.as_object(), [&](auto const &entry) {
            result.emplace(std::string{entry.key().data(), entry.key().size()}, fromBoostJson(entry.value()));
        });
        return JsonValue{std::move(result)};
    }
    }
    std::unreachable();
}

[[nodiscard]] boostJson::value toBoostJson(const JsonValue &value)
{
    return std::visit(
        [](auto const &stored) -> boostJson::value {
            using Stored = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::same_as<Stored, std::nullptr_t>)
            {
                return nullptr;
            }
            else if constexpr (std::same_as<Stored, bool> || std::same_as<Stored, std::int64_t> ||
                               std::same_as<Stored, std::uint64_t> || std::same_as<Stored, double>)
            {
                return stored;
            }
            else if constexpr (std::same_as<Stored, std::string>)
            {
                return boostJson::string{stored};
            }
            else if constexpr (std::same_as<Stored, JsonValue::Array>)
            {
                auto result = boostJson::array{};
                result.reserve(stored.size());
                std::ranges::transform(stored, std::back_inserter(result),
                                       [](auto const &element) { return toBoostJson(element); });
                return result;
            }
            else
            {
                auto result = boostJson::object{};
                result.reserve(stored.size());
                std::ranges::for_each(
                    stored, [&](auto const &entry) { result.emplace(entry.first, toBoostJson(entry.second)); });
                return result;
            }
        },
        value.storage);
}
} // namespace

JsonParseResult parseJson(std::string_view text, std::size_t maximumDepth)
{
    auto error = ErrorCode{};
    auto parser = boostJson::parser{boostJson::storage_ptr{}, strictJsonOptions(maximumDepth)};
    auto const consumed = parser.write(text.data(), text.size(), error);
    if (error || consumed != text.size())
    {
        return JsonParseResult{
            .error = error == boostJson::error::too_deep ? JsonError::maximumDepth : JsonError::invalidSyntax,
        };
    }
    return JsonParseResult{.value = fromBoostJson(parser.release())};
}

JsonParseResult parseJsonRejectingDuplicateKeys(std::string_view text, std::size_t maximumDepth)
{
    auto error = ErrorCode{};
    auto duplicateKeyParser = boostJson::basic_parser<DuplicateKeyHandler>{strictJsonOptions(maximumDepth)};
    auto const consumed = duplicateKeyParser.write_some(false, text.data(), text.size(), error);
    if (error || consumed != text.size() || !duplicateKeyParser.done())
    {
        return JsonParseResult{
            .error = error == boostJson::error::too_deep ? JsonError::maximumDepth : JsonError::invalidSyntax,
        };
    }
    return parseJson(text, maximumDepth);
}

JsonError serializeJson(const JsonValue &value, std::string &output, std::size_t maximumBytes)
{
    auto document = toBoostJson(value);
    auto serializer = boostJson::serializer{};
    serializer.reset(std::addressof(document));
    output.clear();
    auto buffer = std::array<char, 4096u>{};
    while (!serializer.done())
    {
        auto chunk = serializer.read(buffer.data(), buffer.size());
        if (output.size() + chunk.size() > maximumBytes)
        {
            output.clear();
            return JsonError::responseTooLarge;
        }
        output.append(chunk.data(), chunk.size());
    }

    auto validationError = ErrorCode{};
    auto validator =
        boostJson::parser{boostJson::storage_ptr{}, strictJsonOptions(std::numeric_limits<std::size_t>::max())};
    auto const consumed = validator.write(output.data(), output.size(), validationError);
    if (validationError || consumed != output.size())
    {
        output.clear();
        return validationError == boostJson::error::too_deep ? JsonError::maximumDepth : JsonError::invalidUtf8;
    }
    return JsonError::none;
}
} // namespace dependency::json
