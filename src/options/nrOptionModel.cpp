module nr.options;

import :model;
import dependency.json;
import nr.utils;
import std;

namespace nr::options
{
std::string_view wireName(OptionValueType type) noexcept
{
    using enum OptionValueType;
    switch (type)
    {
    case boolean:
        return "boolean";
    case signedInteger:
    case unsignedInteger:
        return "integer";
    case number:
        return "number";
    case string:
        return "string";
    case array:
        return "array";
    case object:
        return "object";
    }
    std::unreachable();
}

namespace
{
namespace json = dependency::json;

inline constexpr std::size_t maximumMachineRequestIdBytes = 128u;
inline constexpr std::size_t maximumMachineReasonBytes = 4u * 1024u;
inline constexpr std::size_t maximumSerializedMachineRecordBytes = 32u * 1024u;

[[nodiscard]] bool validUtf8(std::string_view text) noexcept
{
    auto const *bytes = reinterpret_cast<const unsigned char *>(text.data());
    auto index = std::size_t{0u};
    while (index < text.size())
    {
        auto const first = bytes[index];
        auto continuationCount = std::size_t{0u};
        auto minimum = std::uint32_t{0u};
        auto codePoint = std::uint32_t{0u};

        if (first <= 0x7fu)
        {
            ++index;
            continue;
        }
        if ((first & 0xe0u) == 0xc0u)
        {
            continuationCount = 1u;
            minimum = 0x80u;
            codePoint = first & 0x1fu;
        }
        else if ((first & 0xf0u) == 0xe0u)
        {
            continuationCount = 2u;
            minimum = 0x800u;
            codePoint = first & 0x0fu;
        }
        else if ((first & 0xf8u) == 0xf0u)
        {
            continuationCount = 3u;
            minimum = 0x10000u;
            codePoint = first & 0x07u;
        }
        else
        {
            return false;
        }

        if (index + continuationCount >= text.size())
        {
            return false;
        }

        auto offsets = std::views::iota(std::size_t{1u}, continuationCount + 1u);
        auto valid = true;
        std::ranges::for_each(offsets, [&](std::size_t offset) {
            auto const continuation = bytes[index + offset];
            if ((continuation & 0xc0u) != 0x80u)
            {
                valid = false;
                return;
            }
            codePoint = (codePoint << 6u) | (continuation & 0x3fu);
        });
        if (!valid || codePoint < minimum || codePoint > 0x10ffffu || (codePoint >= 0xd800u && codePoint <= 0xdfffu))
        {
            return false;
        }
        index += continuationCount + 1u;
    }
    return true;
}

[[nodiscard]] SchemaValidation wrongType(std::string_view expected)
{
    return SchemaValidation::failure("$", std::format("expected {}", expected));
}

[[nodiscard]] SchemaValidation prependPath(std::string_view prefix, SchemaValidation result)
{
    if (!result.valid)
    {
        result.path = std::format("{}{}", prefix,
                                  result.path == "$" ? std::string_view{} : std::string_view{result.path}.substr(1u));
    }
    return result;
}

[[nodiscard]] SchemaValidation validateSchema(const OptionSchema &schema, const OptionWireValue &value,
                                              std::size_t depth, std::size_t maximumDepth)
{
    if (depth > maximumDepth)
    {
        return SchemaValidation::failure("$", "maximum nesting depth exceeded");
    }

    switch (schema.type)
    {
    case OptionValueType::boolean:
        return std::holds_alternative<bool>(value.storage) ? SchemaValidation::success() : wrongType("boolean");
    case OptionValueType::signedInteger: {
        auto const *integer = std::get_if<std::int64_t>(&value.storage);
        if (integer == nullptr)
        {
            return wrongType("signed integer");
        }
        if ((schema.signedMinimum && *integer < *schema.signedMinimum) ||
            (schema.signedMaximum && *integer > *schema.signedMaximum))
        {
            return SchemaValidation::failure("$", "signed integer is outside the allowed range");
        }
        return SchemaValidation::success();
    }
    case OptionValueType::unsignedInteger: {
        auto const *integer = std::get_if<std::uint64_t>(&value.storage);
        if (integer == nullptr)
        {
            return wrongType("unsigned integer");
        }
        if ((schema.unsignedMinimum && *integer < *schema.unsignedMinimum) ||
            (schema.unsignedMaximum && *integer > *schema.unsignedMaximum))
        {
            return SchemaValidation::failure("$", "unsigned integer is outside the allowed range");
        }
        return SchemaValidation::success();
    }
    case OptionValueType::number: {
        auto const *number = std::get_if<double>(&value.storage);
        if (number == nullptr)
        {
            return wrongType("number");
        }
        if (!std::isfinite(*number) || (schema.numberMinimum && *number < *schema.numberMinimum) ||
            (schema.numberMaximum && *number > *schema.numberMaximum))
        {
            return SchemaValidation::failure("$", "number is outside the allowed finite range");
        }
        return SchemaValidation::success();
    }
    case OptionValueType::string: {
        auto const *text = std::get_if<std::string>(&value.storage);
        if (text == nullptr)
        {
            return wrongType("string");
        }
        if (!validUtf8(*text))
        {
            return SchemaValidation::failure("$", "string is not valid UTF-8");
        }
        if (text->size() < schema.minimumSize || text->size() > schema.maximumSize)
        {
            return SchemaValidation::failure("$", "string byte length is outside the allowed range");
        }
        if (!schema.allowedStrings.empty() && !std::ranges::contains(schema.allowedStrings, *text))
        {
            return SchemaValidation::failure("$", "string is not in the closed enum");
        }
        return SchemaValidation::success();
    }
    case OptionValueType::array: {
        auto const *array = std::get_if<OptionWireValue::Array>(&value.storage);
        if (array == nullptr)
        {
            return wrongType("array");
        }
        if (array->size() < schema.minimumSize || array->size() > schema.maximumSize)
        {
            return SchemaValidation::failure("$", "array length is outside the allowed range");
        }
        if (!schema.elementSchema)
        {
            return SchemaValidation::failure("$", "array schema has no element schema");
        }
        auto indices = std::views::iota(std::size_t{0u}, array->size());
        auto failed = SchemaValidation::success();
        std::ranges::for_each(indices, [&](std::size_t index) {
            if (!failed.valid)
            {
                return;
            }
            auto result = validateSchema(*schema.elementSchema, (*array)[index], depth + 1u, maximumDepth);
            if (!result.valid)
            {
                failed = prependPath(std::format("$[{}]", index), std::move(result));
            }
        });
        return failed;
    }
    case OptionValueType::object: {
        auto const *object = std::get_if<OptionWireValue::Object>(&value.storage);
        if (object == nullptr)
        {
            return wrongType("object");
        }
        if (object->size() < schema.minimumSize || object->size() > schema.maximumSize)
        {
            return SchemaValidation::failure("$", "object field count is outside the allowed range");
        }

        auto fieldFailure = SchemaValidation::success();
        std::ranges::for_each(schema.objectFields, [&](auto const &entry) {
            if (!fieldFailure.valid)
            {
                return;
            }
            auto const &[name, field] = entry;
            auto const valueIt = object->find(name);
            if (valueIt == object->end())
            {
                if (field.required)
                {
                    fieldFailure = SchemaValidation::failure(std::format("$.{}", name), "required field is missing");
                }
                return;
            }
            if (!field.schema)
            {
                fieldFailure = SchemaValidation::failure(std::format("$.{}", name), "field schema is missing");
                return;
            }
            auto result = validateSchema(*field.schema, valueIt->second, depth + 1u, maximumDepth);
            if (!result.valid)
            {
                fieldFailure = prependPath(std::format("$.{}", name), std::move(result));
            }
        });
        if (!fieldFailure.valid)
        {
            return fieldFailure;
        }

        if (schema.closedObject)
        {
            auto unknown = std::ranges::find_if(
                *object, [&](auto const &entry) { return !schema.objectFields.contains(entry.first); });
            if (unknown != object->end())
            {
                return SchemaValidation::failure(std::format("$.{}", unknown->first), "unknown field in closed object");
            }
        }

        if (schema.objectValidator)
        {
            if (auto error = schema.objectValidator(*object))
            {
                return SchemaValidation::failure("$", std::move(*error));
            }
        }
        return SchemaValidation::success();
    }
    }
    std::unreachable();
}

[[nodiscard]] std::string_view originName(MutationOrigin origin) noexcept
{
    switch (origin)
    {
    case MutationOrigin::imgui:
        return "imgui";
    case MutationOrigin::camera:
        return "camera";
    case MutationOrigin::websocket:
        return "websocket";
    case MutationOrigin::lua:
        return "lua";
    }
    std::unreachable();
}

[[nodiscard]] std::string_view phaseName(OptionLogPhase phase) noexcept
{
    switch (phase)
    {
    case OptionLogPhase::dispatchStarted:
        return "dispatch";
    case OptionLogPhase::terminal:
        return "terminal";
    }
    std::unreachable();
}

[[nodiscard]] std::string_view statusName(OptionLogStatus status) noexcept
{
    switch (status)
    {
    case OptionLogStatus::started:
        return "started";
    case OptionLogStatus::succeeded:
        return "succeeded";
    case OptionLogStatus::failed:
        return "failed";
    case OptionLogStatus::abandoned:
        return "abandoned";
    }
    std::unreachable();
}

[[nodiscard]] std::string_view boundedMachineField(std::string_view value, std::size_t maximumBytes,
                                                   std::string_view overflowValue,
                                                   std::string_view invalidUtf8Value) noexcept
{
    if (value.size() > maximumBytes)
    {
        return overflowValue;
    }
    return validUtf8(value) ? value : invalidUtf8Value;
}
} // namespace

std::optional<OptionId> OptionId::parse(std::string_view text)
{
    return detail::validOptionIdText(text) ? std::optional{OptionId{std::string{text}}} : std::nullopt;
}

OptionSchema OptionSchema::boolean() noexcept
{
    return {};
}

OptionSchema OptionSchema::signedInteger(std::int64_t minimum, std::int64_t maximum) noexcept
{
    return OptionSchema{
        .type = OptionValueType::signedInteger,
        .signedMinimum = minimum,
        .signedMaximum = maximum,
    };
}

OptionSchema OptionSchema::unsignedInteger(std::uint64_t minimum, std::uint64_t maximum) noexcept
{
    return OptionSchema{
        .type = OptionValueType::unsignedInteger,
        .unsignedMinimum = minimum,
        .unsignedMaximum = maximum,
    };
}

OptionSchema OptionSchema::number(double minimum, double maximum) noexcept
{
    return OptionSchema{
        .type = OptionValueType::number,
        .numberMinimum = minimum,
        .numberMaximum = maximum,
    };
}

OptionSchema OptionSchema::string(std::size_t maximumBytes, std::vector<std::string> allowed)
{
    return OptionSchema{
        .type = OptionValueType::string,
        .maximumSize = maximumBytes,
        .allowedStrings = std::move(allowed),
    };
}

OptionSchema OptionSchema::array(OptionSchema element, std::size_t minimumItems, std::size_t maximumItems)
{
    return OptionSchema{
        .type = OptionValueType::array,
        .minimumSize = minimumItems,
        .maximumSize = maximumItems,
        .elementSchema = std::make_shared<const OptionSchema>(std::move(element)),
    };
}

OptionSchema OptionSchema::object(std::map<std::string, OptionObjectField, std::less<>> fields,
                                  ObjectValidator validator)
{
    return OptionSchema{
        .type = OptionValueType::object,
        .minimumSize = static_cast<std::size_t>(
            std::ranges::count_if(fields, [](auto const &entry) { return entry.second.required; })),
        .maximumSize = fields.size(),
        .objectFields = std::move(fields),
        .objectValidator = std::move(validator),
    };
}

OptionSchema OptionSchema::emptyObject()
{
    return object({});
}

SchemaValidation OptionSchema::validate(const OptionWireValue &value, std::size_t maximumDepth) const
{
    return validateSchema(*this, value, 0u, maximumDepth);
}

const OptionDefinition *OptionCatalog::find(const OptionId &id) const noexcept
{
    auto const found = definitions_.find(id);
    return found != definitions_.end() ? std::addressof(found->second) : nullptr;
}

const OptionDefinition *OptionCatalog::find(std::string_view id) const noexcept
{
    auto parsed = OptionId::parse(id);
    return parsed ? find(*parsed) : nullptr;
}

const OptionWireValue *OptionFrameSnapshot::findValue(const OptionId &id) const noexcept
{
    auto const found = values.find(id);
    return found != values.end() ? std::addressof(found->second) : nullptr;
}

const OptionWireValue *OptionFrameSnapshot::findValue(std::string_view id) const noexcept
{
    auto parsed = OptionId::parse(id);
    return parsed ? findValue(*parsed) : nullptr;
}

const OptionAvailability *OptionFrameSnapshot::findAvailability(const OptionId &id) const noexcept
{
    auto const found = availability.find(id);
    return found != availability.end() ? std::addressof(found->second) : nullptr;
}

std::string_view wireName(ScheduleRejectReason reason) noexcept
{
    switch (reason)
    {
    case ScheduleRejectReason::none:
        return "none";
    case ScheduleRejectReason::invalidParams:
        return "invalid_params";
    case ScheduleRejectReason::unknownOption:
        return "unknown_option";
    case ScheduleRejectReason::invalidValue:
        return "invalid_params";
    case ScheduleRejectReason::unavailable:
        return "unavailable";
    case ScheduleRejectReason::busy:
        return "busy";
    case ScheduleRejectReason::admissionClosed:
        return "admission_closed";
    case ScheduleRejectReason::staleBinding:
        return "stale_binding_epoch";
    case ScheduleRejectReason::staleSnapshot:
        return "stale_snapshot_token";
    case ScheduleRejectReason::bindingProofMismatch:
        return "binding_proof_mismatch";
    case ScheduleRejectReason::unauthorizedOrigin:
        return "unauthorized_origin";
    case ScheduleRejectReason::shutdown:
        return "shutdown";
    }
    std::unreachable();
}

std::string serializeMachineRecord(const OptionMachineRecord &record)
{
    using Json = json::JsonValue;
    auto object = Json::Object{
        {"frame_index", Json{static_cast<std::uint64_t>(record.frameIndex)}},
        {"option_id", Json{record.id.value()}},
        {"origin", Json{originName(record.origin)}},
        {"phase", Json{phaseName(record.phase)}},
        {"sequence", Json{static_cast<std::uint64_t>(record.sequence)}},
        {"status", Json{statusName(record.status)}},
    };
    if (record.requestId)
    {
        object.emplace("request_id", Json{boundedMachineField(*record.requestId, maximumMachineRequestIdBytes,
                                                              "request_id_exceeded_limit", "request_id_invalid_utf8")});
    }
    if (record.reason)
    {
        object.emplace("reason", Json{boundedMachineField(*record.reason, maximumMachineReasonBytes,
                                                          "reason_exceeded_limit", "reason_invalid_utf8")});
    }

    auto output = std::string{};
    auto const error = json::serializeJson(Json{std::move(object)}, output, maximumSerializedMachineRecordBytes);
    nrAssert(error == json::JsonError::none, "Boost.JSON failed to serialize a bounded option machine record.");
    return output;
}
} // namespace nr::options
