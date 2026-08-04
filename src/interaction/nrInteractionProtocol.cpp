module nr.interaction;

import :protocol;
import dependency.json;
import dependency.network;
import nr.options;
import nr.utils;
import std;

namespace nr::interaction
{
namespace
{
namespace json = ::dependency::json;
namespace network = dependency::network;
namespace options = nr::options;
using Json = json::JsonValue;
using JsonObject = Json::Object;
using JsonArray = Json::Array;

inline constexpr std::size_t maximumRequestIdBytes = 128u;
inline constexpr std::size_t maximumOptionIdBytes = 128u;
inline constexpr std::size_t maximumOrdinaryStringBytes = 4u * 1024u;
inline constexpr std::size_t maximumWireDepth = 16u;

[[nodiscard]] const JsonObject *object(const Json &value) noexcept
{
    return std::get_if<JsonObject>(&value.storage);
}

[[nodiscard]] const std::string *string(const Json &value) noexcept
{
    return std::get_if<std::string>(&value.storage);
}

[[nodiscard]] const Json *field(const JsonObject &value, std::string_view name) noexcept
{
    auto const found = value.find(name);
    return found != value.end() ? std::addressof(found->second) : nullptr;
}

[[nodiscard]] bool hasExactFields(const JsonObject &value, std::initializer_list<std::string_view> expected) noexcept
{
    return value.size() == expected.size() &&
           std::ranges::all_of(expected, [&](std::string_view name) { return value.contains(name); });
}

[[nodiscard]] bool validRequestId(const Json &value) noexcept
{
    if (auto const *text = std::get_if<std::string>(&value.storage))
    {
        return text->size() <= maximumRequestIdBytes;
    }
    return std::holds_alternative<std::int64_t>(value.storage) || std::holds_alternative<std::uint64_t>(value.storage);
}

[[nodiscard]] std::string normalizedRequestId(const Json &value)
{
    if (auto const *text = std::get_if<std::string>(&value.storage))
    {
        return *text;
    }
    if (auto const *signedValue = std::get_if<std::int64_t>(&value.storage))
    {
        return std::to_string(*signedValue);
    }
    return std::to_string(std::get<std::uint64_t>(value.storage));
}

[[nodiscard]] bool writeJson(const Json &value, std::string &slot, std::size_t maximumBytes)
{
    return json::serializeJson(value, slot, maximumBytes) == json::JsonError::none;
}

[[nodiscard]] Json rpcResult(const Json &id, Json result)
{
    return Json{JsonObject{
        {"id", id},
        {"jsonrpc", Json{"2.0"}},
        {"result", std::move(result)},
    }};
}

[[nodiscard]] Json rpcError(const Json &id, std::int64_t code, std::string message, std::string reason)
{
    return Json{JsonObject{
        {"error", Json{JsonObject{
                      {"code", Json{code}},
                      {"data", Json{JsonObject{{"reason", Json{std::move(reason)}}}}},
                      {"message", Json{std::move(message)}},
                  }}},
        {"id", id},
        {"jsonrpc", Json{"2.0"}},
    }};
}

[[nodiscard]] network::TextMessageResult writeError(const Json &id, std::int64_t code, std::string message,
                                                    std::string reason, std::string &slot, std::size_t maximumBytes)
{
    auto const written = writeJson(rpcError(id, code, std::move(message), std::move(reason)), slot, maximumBytes);
    return network::TextMessageResult{.responseReady = written};
}

[[nodiscard]] network::TextMessageResult invalidRequest(std::string &slot, std::size_t maximumBytes,
                                                        const Json &id = Json{nullptr})
{
    return writeError(id, -32600, "Invalid Request.", "invalid_request", slot, maximumBytes);
}

[[nodiscard]] network::TextMessageResult invalidParams(const Json &id, std::string reason, std::string &slot,
                                                       std::size_t maximumBytes)
{
    return writeError(id, -32602, "Invalid method parameters.", std::move(reason), slot, maximumBytes);
}

[[nodiscard]] bool snapshotReady(const std::shared_ptr<const options::OptionFrameSnapshot> &snapshot) noexcept
{
    return snapshot && snapshot->catalog && !snapshot->catalog->definitions().empty();
}

[[nodiscard]] std::string_view schemaTypeName(options::OptionValueType type) noexcept
{
    switch (type)
    {
    case options::OptionValueType::boolean:
        return "boolean";
    case options::OptionValueType::signedInteger:
    case options::OptionValueType::unsignedInteger:
        return "integer";
    case options::OptionValueType::number:
        return "number";
    case options::OptionValueType::string:
        return "string";
    case options::OptionValueType::array:
        return "array";
    case options::OptionValueType::object:
        return "object";
    }
    std::unreachable();
}

[[nodiscard]] std::string_view controlName(options::OptionUiControl control) noexcept
{
    switch (control)
    {
    case options::OptionUiControl::automatic:
        return "automatic";
    case options::OptionUiControl::checkbox:
        return "checkbox";
    case options::OptionUiControl::combo:
        return "combo";
    case options::OptionUiControl::slider:
        return "slider";
    case options::OptionUiControl::input:
        return "input";
    case options::OptionUiControl::button:
        return "button";
    case options::OptionUiControl::hidden:
        return "hidden";
    }
    std::unreachable();
}

[[nodiscard]] Json schemaToJson(const options::OptionSchema &schema)
{
    auto result = JsonObject{{"type", Json{schemaTypeName(schema.type)}}};
    if (schema.signedMinimum)
    {
        result.emplace("minimum", Json{*schema.signedMinimum});
    }
    if (schema.signedMaximum)
    {
        result.emplace("maximum", Json{*schema.signedMaximum});
    }
    if (schema.unsignedMinimum)
    {
        result.emplace("minimum", Json{*schema.unsignedMinimum});
    }
    if (schema.unsignedMaximum)
    {
        result.emplace("maximum", Json{*schema.unsignedMaximum});
    }
    if (schema.numberMinimum)
    {
        result.emplace("minimum", Json{*schema.numberMinimum});
    }
    if (schema.numberMaximum)
    {
        result.emplace("maximum", Json{*schema.numberMaximum});
    }
    if (schema.type == options::OptionValueType::string)
    {
        result.emplace("max_bytes", Json{static_cast<std::uint64_t>(schema.maximumSize)});
        if (!schema.allowedStrings.empty())
        {
            auto values = JsonArray{};
            values.reserve(schema.allowedStrings.size());
            std::ranges::transform(schema.allowedStrings, std::back_inserter(values),
                                   [](auto const &entry) { return Json{entry}; });
            result.emplace("enum", Json{std::move(values)});
        }
    }
    if (schema.type == options::OptionValueType::array)
    {
        result.emplace("min_items", Json{static_cast<std::uint64_t>(schema.minimumSize)});
        result.emplace("max_items", Json{static_cast<std::uint64_t>(schema.maximumSize)});
        if (schema.elementSchema)
        {
            result.emplace("items", schemaToJson(*schema.elementSchema));
        }
    }
    if (schema.type == options::OptionValueType::object)
    {
        auto properties = JsonObject{};
        auto required = JsonArray{};
        std::ranges::for_each(schema.objectFields, [&](auto const &entry) {
            if (entry.second.schema)
            {
                properties.emplace(entry.first, schemaToJson(*entry.second.schema));
            }
            if (entry.second.required)
            {
                required.emplace_back(entry.first);
            }
        });
        result.emplace("properties", Json{std::move(properties)});
        result.emplace("required", Json{std::move(required)});
        result.emplace("additional_properties", Json{!schema.closedObject});
    }
    return Json{std::move(result)};
}

[[nodiscard]] Json wireToJson(const options::OptionWireValue &value)
{
    return std::visit(
        [](auto const &stored) -> Json {
            using Stored = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::same_as<Stored, bool> || std::same_as<Stored, std::int64_t> ||
                          std::same_as<Stored, std::uint64_t> || std::same_as<Stored, double> ||
                          std::same_as<Stored, std::string>)
            {
                return Json{stored};
            }
            else if constexpr (std::same_as<Stored, options::OptionWireValue::Array>)
            {
                auto result = JsonArray{};
                result.reserve(stored.size());
                std::ranges::transform(stored, std::back_inserter(result),
                                       [](auto const &entry) { return wireToJson(entry); });
                return Json{std::move(result)};
            }
            else
            {
                auto result = JsonObject{};
                std::ranges::for_each(
                    stored, [&](auto const &entry) { result.emplace(entry.first, wireToJson(entry.second)); });
                return Json{std::move(result)};
            }
        },
        value.storage);
}

[[nodiscard]] Json optionRecord(const options::OptionDefinition &definition,
                                const options::OptionFrameSnapshot &snapshot)
{
    auto const *value = snapshot.findValue(definition.id);
    auto const *availability = snapshot.findAvailability(definition.id);
    auto const available = availability != nullptr && availability->available;
    return Json{JsonObject{
        {"available", Json{available}},
        {"control", Json{controlName(definition.presentation.control)}},
        {"group", Json{definition.presentation.group}},
        {"id", Json{definition.id.value()}},
        {"input_schema", schemaToJson(definition.schema)},
        {"order", Json{static_cast<std::int64_t>(definition.presentation.order)}},
        {"title", Json{definition.presentation.label.empty() ? std::string{definition.id.value()}
                                                             : definition.presentation.label}},
        {"unavailable_reason", available || availability == nullptr ? Json{nullptr} : Json{availability->reason}},
        {"value", value != nullptr ? wireToJson(*value) : Json{nullptr}},
    }};
}

[[nodiscard]] Json snapshotMetadata(const options::OptionFrameSnapshot &snapshot)
{
    return Json{JsonObject{
        {"binding_epoch", Json{snapshot.bindingEpoch}},
        {"frame_index", Json{snapshot.frameIndex}},
        {"graph_generation", Json{snapshot.graphGeneration}},
        {"schema_version", Json{std::uint64_t{1u}}},
        {"snapshot_revision", Json{snapshot.revision}},
        {"snapshot_token", Json{snapshot.snapshotToken}},
    }};
}

[[nodiscard]] Json completeSnapshot(const options::OptionFrameSnapshot &snapshot)
{
    auto records = JsonArray{};
    records.reserve(snapshot.catalog->definitions().size());
    std::ranges::transform(snapshot.catalog->definitions(), std::back_inserter(records),
                           [&](auto const &entry) { return optionRecord(entry.second, snapshot); });
    auto result = std::get<JsonObject>(snapshotMetadata(snapshot).storage);
    result.emplace("options", Json{std::move(records)});
    return Json{std::move(result)};
}

[[nodiscard]] std::optional<options::OptionWireValue> jsonToWire(const Json &value, const options::OptionSchema &schema,
                                                                 std::size_t depth = 0u)
{
    if (depth > maximumWireDepth)
    {
        return {};
    }
    switch (schema.type)
    {
    case options::OptionValueType::boolean:
        if (auto const *stored = std::get_if<bool>(&value.storage))
        {
            return options::OptionWireValue{*stored};
        }
        return {};
    case options::OptionValueType::signedInteger:
        if (auto const *stored = std::get_if<std::int64_t>(&value.storage))
        {
            return options::OptionWireValue{*stored};
        }
        if (auto const *stored = std::get_if<std::uint64_t>(&value.storage);
            stored && *stored <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        {
            return options::OptionWireValue{static_cast<std::int64_t>(*stored)};
        }
        return {};
    case options::OptionValueType::unsignedInteger:
        if (auto const *stored = std::get_if<std::uint64_t>(&value.storage))
        {
            return options::OptionWireValue{*stored};
        }
        if (auto const *stored = std::get_if<std::int64_t>(&value.storage); stored && *stored >= 0)
        {
            return options::OptionWireValue{static_cast<std::uint64_t>(*stored)};
        }
        return {};
    case options::OptionValueType::number:
        if (auto const *stored = std::get_if<double>(&value.storage); stored && std::isfinite(*stored))
        {
            return options::OptionWireValue{*stored};
        }
        if (auto const *stored = std::get_if<std::int64_t>(&value.storage))
        {
            return options::OptionWireValue{static_cast<double>(*stored)};
        }
        if (auto const *stored = std::get_if<std::uint64_t>(&value.storage))
        {
            return options::OptionWireValue{static_cast<double>(*stored)};
        }
        return {};
    case options::OptionValueType::string:
        if (auto const *stored = std::get_if<std::string>(&value.storage);
            stored && stored->size() <= maximumOrdinaryStringBytes)
        {
            return options::OptionWireValue{*stored};
        }
        return {};
    case options::OptionValueType::array: {
        auto const *stored = std::get_if<JsonArray>(&value.storage);
        if (stored == nullptr || !schema.elementSchema)
        {
            return {};
        }
        auto result = options::OptionWireValue::Array{};
        result.reserve(stored->size());
        auto valid = true;
        std::ranges::for_each(*stored, [&](auto const &entry) {
            if (!valid)
            {
                return;
            }
            auto converted = jsonToWire(entry, *schema.elementSchema, depth + 1u);
            if (!converted)
            {
                valid = false;
                return;
            }
            result.push_back(std::move(*converted));
        });
        return valid ? std::optional{options::OptionWireValue{std::move(result)}} : std::nullopt;
    }
    case options::OptionValueType::object: {
        auto const *stored = std::get_if<JsonObject>(&value.storage);
        if (stored == nullptr)
        {
            return {};
        }
        auto result = options::OptionWireValue::Object{};
        auto valid = true;
        std::ranges::for_each(*stored, [&](auto const &entry) {
            if (!valid)
            {
                return;
            }
            auto const fieldSchema = schema.objectFields.find(entry.first);
            if (fieldSchema == schema.objectFields.end() || !fieldSchema->second.schema)
            {
                valid = false;
                return;
            }
            auto converted = jsonToWire(entry.second, *fieldSchema->second.schema, depth + 1u);
            if (!converted)
            {
                valid = false;
                return;
            }
            result.emplace(entry.first, std::move(*converted));
        });
        return valid ? std::optional{options::OptionWireValue{std::move(result)}} : std::nullopt;
    }
    }
    std::unreachable();
}

[[nodiscard]] std::optional<std::uint64_t> unsignedInteger(const Json &value) noexcept
{
    if (auto const *stored = std::get_if<std::uint64_t>(&value.storage))
    {
        return *stored;
    }
    if (auto const *stored = std::get_if<std::int64_t>(&value.storage); stored && *stored >= 0)
    {
        return static_cast<std::uint64_t>(*stored);
    }
    return {};
}

struct Rejection
{
    std::int64_t code;
    std::string_view reason;
};

[[nodiscard]] Rejection rejectionFor(options::ScheduleRejectReason reason) noexcept
{
    switch (reason)
    {
    case options::ScheduleRejectReason::invalidParams:
    case options::ScheduleRejectReason::invalidValue:
        return {-32602, "invalid_params"};
    case options::ScheduleRejectReason::unknownOption:
        return {-32602, "unknown_option"};
    case options::ScheduleRejectReason::unauthorizedOrigin:
        return {-32010, "controller_unavailable"};
    case options::ScheduleRejectReason::busy:
        return {-32011, "operation_busy"};
    case options::ScheduleRejectReason::admissionClosed:
        return {-32012, "admission_closed"};
    case options::ScheduleRejectReason::staleBinding:
    case options::ScheduleRejectReason::staleSnapshot:
    case options::ScheduleRejectReason::bindingProofMismatch:
        return {-32013, "stale_binding"};
    case options::ScheduleRejectReason::unavailable:
        return {-32014, "option_unavailable"};
    case options::ScheduleRejectReason::shutdown:
        return {-32016, "server_stopping"};
    case options::ScheduleRejectReason::none:
        break;
    }
    return {-32603, "internal_error"};
}

[[nodiscard]] std::string_view authorityName(options::AuthorityMode mode) noexcept
{
    switch (mode)
    {
    case options::AuthorityMode::human:
        return "human";
    case options::AuthorityMode::agent:
        return "agent";
    case options::AuthorityMode::offlineLua:
        return "offline-lua";
    }
    std::unreachable();
}
} // namespace

OptionRpcProtocol::OptionRpcProtocol(options::OptionSystem &optionSystem, std::size_t maximumResponseBytes) noexcept
    : optionSystem_(optionSystem), maximumResponseBytes_(maximumResponseBytes)
{
}

network::TextMessageResult OptionRpcProtocol::handleText(std::string_view payload,
                                                         const network::MessageContext &context,
                                                         std::string &responseSlot) const
{
    if (responseSlot.capacity() < maximumResponseBytes_)
    {
        responseSlot.reserve(maximumResponseBytes_);
    }

    auto parsed = json::parseJson(payload, 32u);
    if (!parsed.valid())
    {
        return writeError(Json{nullptr}, -32700, "Parse error.", "parse_error", responseSlot, maximumResponseBytes_);
    }
    auto const *request = object(*parsed.value);
    if (request != nullptr && request->contains("jsonrpc") && request->contains("method") &&
        request->contains("params") && !request->contains("id"))
    {
        return {};
    }
    if (request == nullptr || !hasExactFields(*request, {"jsonrpc", "id", "method", "params"}))
    {
        return invalidRequest(responseSlot, maximumResponseBytes_);
    }

    auto const *versionValue = field(*request, "jsonrpc");
    auto const *id = field(*request, "id");
    auto const *methodValue = field(*request, "method");
    auto const *paramsValue = field(*request, "params");
    auto const *version = versionValue != nullptr ? string(*versionValue) : nullptr;
    auto const *method = methodValue != nullptr ? string(*methodValue) : nullptr;
    auto const *params = paramsValue != nullptr ? object(*paramsValue) : nullptr;
    if (version == nullptr || *version != "2.0" || id == nullptr || !validRequestId(*id) || method == nullptr ||
        params == nullptr)
    {
        return invalidRequest(responseSlot, maximumResponseBytes_);
    }
    if (context.rateLimited)
    {
        return writeError(*id, -32017, "Request rate limit exceeded.", "rate_limited", responseSlot,
                          maximumResponseBytes_);
    }

    auto &system = optionSystem_.get();
    if (*method == "session.describe")
    {
        if (!params->empty())
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        auto const mode = system.authorityMode();
        auto result = Json{JsonObject{
            {"authority_mode", Json{authorityName(mode)}},
            {"batch_mutation", Json{false}},
            {"capture_format", Json{"exr"}},
            {"final_result_channel", Json{"rotating-ndjson-file"}},
            {"final_result_path", Json{nr::activeOptionNdjsonLogPath().generic_string()}},
            {"final_result_schema", Json{"NR_OPTION_V1"}},
            {"mutation_model", Json{"single-slot-next-renderable-frame"}},
            {"protocol", Json{"newbie-renderer-options"}},
            {"tasks", Json{false}},
            {"version", Json{std::uint64_t{1u}}},
            {"writable", Json{mode == options::AuthorityMode::agent}},
        }};
        return network::TextMessageResult{
            .responseReady = writeJson(rpcResult(*id, std::move(result)), responseSlot, maximumResponseBytes_),
        };
    }

    auto snapshot = system.snapshot();
    if (!snapshotReady(snapshot))
    {
        return writeError(*id, -32015, "Initial option snapshot is not ready.", "snapshot_not_ready", responseSlot,
                          maximumResponseBytes_);
    }

    if (*method == "option.snapshot")
    {
        if (!params->empty())
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        if (writeJson(rpcResult(*id, completeSnapshot(*snapshot)), responseSlot, maximumResponseBytes_))
        {
            return network::TextMessageResult{.responseReady = true};
        }
        return writeError(*id, -32603, "Snapshot response exceeded its bound.", "internal_error", responseSlot,
                          maximumResponseBytes_);
    }

    if (*method == "option.get")
    {
        if (!hasExactFields(*params, {"id"}))
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        auto const *optionIdValue = field(*params, "id");
        auto const *optionIdText = optionIdValue != nullptr ? string(*optionIdValue) : nullptr;
        if (optionIdText == nullptr || optionIdText->size() > maximumOptionIdBytes)
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        auto parsedId = options::OptionId::parse(*optionIdText);
        auto const *definition = parsedId ? snapshot->catalog->find(*parsedId) : nullptr;
        if (definition == nullptr)
        {
            return invalidParams(*id, "unknown_option", responseSlot, maximumResponseBytes_);
        }
        auto result = std::get<JsonObject>(snapshotMetadata(*snapshot).storage);
        result.emplace("option", optionRecord(*definition, *snapshot));
        return network::TextMessageResult{
            .responseReady = writeJson(rpcResult(*id, Json{std::move(result)}), responseSlot, maximumResponseBytes_),
        };
    }

    if (*method == "option.apply")
    {
        auto const validShape = params->size() >= 3u && params->size() <= 4u && params->contains("id") &&
                                params->contains("value") &&
                                (params->contains("binding_epoch") || params->contains("snapshot_token")) &&
                                std::ranges::all_of(*params, [](auto const &entry) {
                                    return entry.first == "id" || entry.first == "value" ||
                                           entry.first == "binding_epoch" || entry.first == "snapshot_token";
                                });
        if (!validShape)
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        auto const *optionIdValue = field(*params, "id");
        auto const *optionIdText = optionIdValue != nullptr ? string(*optionIdValue) : nullptr;
        if (optionIdText == nullptr || optionIdText->size() > maximumOptionIdBytes)
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }
        auto parsedId = options::OptionId::parse(*optionIdText);
        auto const *definition = parsedId ? snapshot->catalog->find(*parsedId) : nullptr;
        if (definition == nullptr)
        {
            return invalidParams(*id, "unknown_option", responseSlot, maximumResponseBytes_);
        }

        auto const *input = field(*params, "value");
        auto converted = input != nullptr ? jsonToWire(*input, definition->schema) : std::nullopt;
        if (!converted)
        {
            return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
        }

        auto proof = options::BindingProof{};
        if (auto const *epochValue = field(*params, "binding_epoch"))
        {
            proof.bindingEpoch = unsignedInteger(*epochValue);
            if (!proof.bindingEpoch)
            {
                return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
            }
        }
        if (auto const *tokenValue = field(*params, "snapshot_token"))
        {
            auto const *token = string(*tokenValue);
            if (token == nullptr || token->empty() || token->size() > maximumOrdinaryStringBytes)
            {
                return invalidParams(*id, "invalid_params", responseSlot, maximumResponseBytes_);
            }
            proof.snapshotToken = *token;
        }

        auto const startedResponse = rpcResult(*id, Json{JsonObject{{"status", Json{"started"}}}});
        if (!writeJson(startedResponse, responseSlot, maximumResponseBytes_))
        {
            return writeError(*id, -32603, "Response preparation failed.", "internal_error", responseSlot,
                              maximumResponseBytes_);
        }

        auto schedule = system.trySchedule(options::OptionMutationRequest{
            .id = *parsedId,
            .value = std::move(*converted),
            .binding = std::move(proof),
            .origin = options::MutationOrigin::websocket,
            .requestId = normalizedRequestId(*id),
        });
        if (schedule.started)
        {
            return network::TextMessageResult{
                .responseReady = true,
                .mutationStarted = true,
            };
        }

        auto const rejection = rejectionFor(schedule.reason);
        return writeError(*id, rejection.code, "Operation was not started.", std::string{rejection.reason},
                          responseSlot, maximumResponseBytes_);
    }

    return writeError(*id, -32601, "Method not found.", "method_not_found", responseSlot, maximumResponseBytes_);
}
} // namespace nr::interaction
