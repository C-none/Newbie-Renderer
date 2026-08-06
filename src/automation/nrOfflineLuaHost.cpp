module nr.automation;

import dependency.lua;
import nr.options;
import nr.utils;
import std;

namespace nr::automation
{
namespace
{
inline constexpr std::size_t kMaximumSourceBytes = 256u * 1024u;
inline constexpr std::size_t kMaximumLogCallBytes = 1024u;
inline constexpr std::size_t kMaximumLogCallsPerResume = 16u;
inline constexpr std::size_t kMaximumLogBytesPerResume = 8u * 1024u;

using LuaValue = dependency::lua::Value;
using LuaObject = LuaValue::Object;
using LuaArray = LuaValue::Array;
using HostCallResult = dependency::lua::HostCallResult;

[[nodiscard]] LuaValue integerValue(std::uint64_t value)
{
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    {
        return LuaValue{static_cast<std::int64_t>(value)};
    }
    return LuaValue{static_cast<double>(value)};
}

[[nodiscard]] std::string_view scopeName(nr::options::OptionScope scope) noexcept
{
    return scope == nr::options::OptionScope::session ? "session" : "graph";
}

[[nodiscard]] std::string_view lifetimeName(nr::options::OptionValueLifetime lifetime) noexcept
{
    return lifetime == nr::options::OptionValueLifetime::canonical ? "canonical" : "frame_effect";
}

[[nodiscard]] LuaValue wireToLua(const nr::options::OptionWireValue &value)
{
    return std::visit(
        [](auto const &stored) -> LuaValue {
            using Stored = std::remove_cvref_t<decltype(stored)>;
            if constexpr (std::same_as<Stored, bool>)
            {
                return LuaValue{stored};
            }
            else if constexpr (std::same_as<Stored, std::int64_t>)
            {
                return LuaValue{stored};
            }
            else if constexpr (std::same_as<Stored, std::uint64_t>)
            {
                return integerValue(stored);
            }
            else if constexpr (std::same_as<Stored, double> || std::same_as<Stored, std::string>)
            {
                return LuaValue{stored};
            }
            else if constexpr (std::same_as<Stored, nr::options::OptionWireValue::Array>)
            {
                auto result = LuaArray{};
                result.reserve(stored.size());
                std::ranges::transform(stored, std::back_inserter(result),
                                       [](auto const &element) { return wireToLua(element); });
                return LuaValue{std::move(result)};
            }
            else
            {
                auto result = LuaObject{};
                std::ranges::for_each(stored,
                                      [&](auto const &entry) { result.emplace(entry.first, wireToLua(entry.second)); });
                return LuaValue{std::move(result)};
            }
        },
        value.storage);
}

[[nodiscard]] LuaValue schemaToLua(const nr::options::OptionSchema &schema)
{
    auto result = LuaObject{
        {"type", LuaValue{nr::options::wireName(schema.type)}},
    };
    if (schema.signedMinimum)
    {
        result.emplace("minimum", LuaValue{*schema.signedMinimum});
    }
    if (schema.signedMaximum)
    {
        result.emplace("maximum", LuaValue{*schema.signedMaximum});
    }
    if (schema.unsignedMinimum)
    {
        result.emplace("minimum", integerValue(*schema.unsignedMinimum));
    }
    if (schema.unsignedMaximum)
    {
        result.emplace("maximum", integerValue(*schema.unsignedMaximum));
    }
    if (schema.numberMinimum)
    {
        result.emplace("minimum", LuaValue{*schema.numberMinimum});
    }
    if (schema.numberMaximum)
    {
        result.emplace("maximum", LuaValue{*schema.numberMaximum});
    }
    if (!schema.allowedStrings.empty())
    {
        auto values = LuaArray{};
        values.reserve(schema.allowedStrings.size());
        std::ranges::transform(schema.allowedStrings, std::back_inserter(values),
                               [](const std::string &value) { return LuaValue{value}; });
        result.emplace("enum", LuaValue{std::move(values)});
    }
    if (schema.type == nr::options::OptionValueType::array && schema.elementSchema)
    {
        result.emplace("items", schemaToLua(*schema.elementSchema));
        result.emplace("minimum_items", integerValue(schema.minimumSize));
        result.emplace("maximum_items", integerValue(schema.maximumSize));
    }
    if (schema.type == nr::options::OptionValueType::object)
    {
        auto fields = LuaObject{};
        std::ranges::for_each(schema.objectFields, [&](auto const &entry) {
            if (entry.second.schema)
            {
                auto field = std::get<LuaObject>(schemaToLua(*entry.second.schema).storage);
                field.emplace("required", LuaValue{entry.second.required});
                fields.emplace(entry.first, LuaValue{std::move(field)});
            }
        });
        result.emplace("fields", LuaValue{std::move(fields)});
        result.emplace("closed", LuaValue{schema.closedObject});
    }
    return LuaValue{std::move(result)};
}

[[nodiscard]] LuaValue optionRecord(const nr::options::OptionFrameSnapshot &snapshot,
                                    const nr::options::OptionDefinition &definition)
{
    auto const *value = snapshot.findValue(definition.id);
    auto const *availability = snapshot.findAvailability(definition.id);
    auto record = LuaObject{
        {"id", LuaValue{definition.id.value()}},
        {"scope", LuaValue{scopeName(definition.scope)}},
        {"lifetime", LuaValue{lifetimeName(definition.lifetime)}},
        {"input_schema", schemaToLua(definition.schema)},
        {"available", LuaValue{availability != nullptr && availability->available}},
        {"group", LuaValue{definition.presentation.group}},
        {"title", LuaValue{definition.presentation.label}},
    };
    if (value != nullptr)
    {
        record.emplace("value", wireToLua(*value));
    }
    if (availability != nullptr && !availability->reason.empty())
    {
        record.emplace("unavailable_reason", LuaValue{availability->reason});
    }
    return LuaValue{std::move(record)};
}

[[nodiscard]] LuaValue snapshotRecord(const nr::options::OptionFrameSnapshot &snapshot)
{
    auto options = LuaArray{};
    options.reserve(snapshot.catalog ? snapshot.catalog->definitions().size() : 0u);
    if (snapshot.catalog)
    {
        std::ranges::transform(snapshot.catalog->definitions(), std::back_inserter(options),
                               [&](auto const &entry) { return optionRecord(snapshot, entry.second); });
    }

    return LuaValue{LuaObject{
        {"schema_version", integerValue(1u)},
        {"frame_index", integerValue(snapshot.frameIndex)},
        {"snapshot_revision", integerValue(snapshot.revision)},
        {"graph_generation", integerValue(snapshot.graphGeneration)},
        {"binding_epoch", integerValue(snapshot.bindingEpoch)},
        {"snapshot_token", LuaValue{snapshot.snapshotToken}},
        {"options", LuaValue{std::move(options)}},
    }};
}

[[nodiscard]] std::expected<nr::options::OptionWireValue, std::string> luaToWire(
    const LuaValue &value, const nr::options::OptionSchema &schema, std::size_t depth = 0u)
{
    if (depth > 16u)
    {
        return std::unexpected("Option value exceeds the conversion depth limit.");
    }

    using enum nr::options::OptionValueType;
    switch (schema.type)
    {
    case boolean: {
        auto const *stored = std::get_if<bool>(&value.storage);
        return stored != nullptr
                   ? std::expected<nr::options::OptionWireValue, std::string>{nr::options::OptionWireValue{*stored}}
                   : std::unexpected("Option value must be a boolean.");
    }
    case signedInteger: {
        auto const *stored = std::get_if<std::int64_t>(&value.storage);
        return stored != nullptr
                   ? std::expected<nr::options::OptionWireValue, std::string>{nr::options::OptionWireValue{*stored}}
                   : std::unexpected("Option value must be an integer.");
    }
    case unsignedInteger: {
        auto const *stored = std::get_if<std::int64_t>(&value.storage);
        return stored != nullptr && *stored >= 0
                   ? std::expected<nr::options::OptionWireValue, std::string>{nr::options::OptionWireValue{
                         static_cast<std::uint64_t>(*stored)}}
                   : std::unexpected("Option value must be a non-negative integer.");
    }
    case number: {
        if (auto const *stored = std::get_if<double>(&value.storage))
        {
            return nr::options::OptionWireValue{*stored};
        }
        if (auto const *stored = std::get_if<std::int64_t>(&value.storage))
        {
            return nr::options::OptionWireValue{static_cast<double>(*stored)};
        }
        return std::unexpected("Option value must be a number.");
    }
    case string: {
        auto const *stored = std::get_if<std::string>(&value.storage);
        return stored != nullptr
                   ? std::expected<nr::options::OptionWireValue, std::string>{nr::options::OptionWireValue{*stored}}
                   : std::unexpected("Option value must be a string.");
    }
    case array: {
        auto const *stored = std::get_if<LuaArray>(&value.storage);
        if (stored == nullptr || !schema.elementSchema)
        {
            return std::unexpected("Option value must be an array.");
        }
        auto result = nr::options::OptionWireValue::Array{};
        result.reserve(stored->size());
        auto error = std::string{};
        std::ranges::for_each(*stored, [&](const LuaValue &element) {
            if (!error.empty())
            {
                return;
            }
            auto converted = luaToWire(element, *schema.elementSchema, depth + 1u);
            if (!converted)
            {
                error = std::move(converted.error());
                return;
            }
            result.push_back(std::move(*converted));
        });
        return error.empty() ? std::expected<nr::options::OptionWireValue, std::string>{nr::options::OptionWireValue{
                                   std::move(result)}}
                             : std::unexpected(std::move(error));
    }
    case object: {
        auto const *stored = std::get_if<LuaObject>(&value.storage);
        if (stored == nullptr)
        {
            return std::unexpected("Option value must be an object.");
        }
        auto result = nr::options::OptionWireValue::Object{};
        auto error = std::string{};
        std::ranges::for_each(*stored, [&](auto const &entry) {
            if (!error.empty())
            {
                return;
            }
            auto const field = schema.objectFields.find(entry.first);
            if (field == schema.objectFields.end() || !field->second.schema)
            {
                error = std::format("Unknown field '{}' in closed option object.", entry.first);
                return;
            }
            auto converted = luaToWire(entry.second, *field->second.schema, depth + 1u);
            if (!converted)
            {
                error = std::move(converted.error());
                return;
            }
            result.emplace(entry.first, std::move(*converted));
        });
        if (!error.empty())
        {
            return std::unexpected(std::move(error));
        }
        auto wire = nr::options::OptionWireValue{std::move(result)};
        auto validation = schema.validate(wire);
        return validation.valid ? std::expected<nr::options::OptionWireValue, std::string>{std::move(wire)}
                                : std::unexpected(std::format("{}: {}", validation.path, validation.detail));
    }
    }
    std::unreachable();
}

[[nodiscard]] bool forbiddenPathForm(std::string_view text) noexcept
{
    return text.starts_with("//") || text.starts_with(R"(\\)") || text.starts_with(R"(\\?\)") ||
           text.starts_with(R"(\\.\)") || text.contains('\0') || text.contains("://") ||
           text.find_first_of("|&;:<>%!?^()\n\r\t\"'`$*") != std::string_view::npos;
}

[[nodiscard]] std::string_view luaRejectionReason(nr::options::ScheduleRejectReason reason) noexcept
{
    using enum nr::options::ScheduleRejectReason;
    switch (reason)
    {
    case none:
        return "none";
    case invalidParams:
    case invalidValue:
        return "invalid_params";
    case unknownOption:
        return "unknown_option";
    case unavailable:
        return "option_unavailable";
    case busy:
        return "operation_busy";
    case admissionClosed:
        return "admission_closed";
    case staleBinding:
    case staleSnapshot:
    case bindingProofMismatch:
        return "stale_binding";
    case unauthorizedOrigin:
        return "controller_unavailable";
    case shutdown:
        return "server_stopping";
    }
    std::unreachable();
}
} // namespace

std::filesystem::path automationRootPath()
{
    return std::filesystem::path{std::string{nr::projectRoot}} / "automation";
}

std::expected<std::filesystem::path, std::string> resolveAutomationScriptPath(
    const std::filesystem::path &rootRelativePath)
{
    if (rootRelativePath.empty() || rootRelativePath.is_absolute() || rootRelativePath.has_root_name() ||
        rootRelativePath.extension() != ".lua")
    {
        return std::unexpected("Automation script must be a root-relative .lua path.");
    }
    auto const raw = rootRelativePath.generic_string();
    if (forbiddenPathForm(raw))
    {
        return std::unexpected("Automation script path contains a forbidden path or shell form.");
    }

    auto error = std::error_code{};
    auto const root = std::filesystem::canonical(automationRootPath(), error);
    if (error)
    {
        return std::unexpected(std::format("Failed to resolve automation root '{}': {}",
                                           automationRootPath().generic_string(), error.message()));
    }
    auto const resolved = std::filesystem::canonical(root / rootRelativePath, error);
    if (error)
    {
        return std::unexpected(std::format("Failed to resolve automation script '{}': {}", raw, error.message()));
    }
    if (!std::filesystem::is_regular_file(resolved, error) || error)
    {
        return std::unexpected("Automation script is not a regular file.");
    }

    auto const relative = resolved.lexically_relative(root);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..")
    {
        return std::unexpected("Automation script resolves outside the automation root.");
    }
    return resolved;
}

class OfflineLuaHost::Impl
{
  public:
    [[nodiscard]] OfflineLuaStartResult start(nr::options::OptionSystem &optionSystem,
                                              const std::filesystem::path &rootRelativeScript)
    {
        if (sandbox_.started())
        {
            return OfflineLuaStartResult{.error = OfflineLuaStartError::alreadyStarted};
        }
        if (optionSystem.authorityMode() != nr::options::AuthorityMode::offlineLua)
        {
            return OfflineLuaStartResult{.error = OfflineLuaStartError::wrongAuthority};
        }

        auto resolved = resolveAutomationScriptPath(rootRelativeScript);
        if (!resolved)
        {
            return OfflineLuaStartResult{
                .error = OfflineLuaStartError::invalidPath,
                .detail = std::move(resolved.error()),
            };
        }

        auto error = std::error_code{};
        auto const sourceBytes = std::filesystem::file_size(*resolved, error);
        if (error || sourceBytes > kMaximumSourceBytes)
        {
            return OfflineLuaStartResult{
                .error = OfflineLuaStartError::sourceReadFailed,
                .detail = error ? std::format("Failed to inspect Lua source: {}", error.message())
                                : "Lua source exceeds 256 KiB.",
            };
        }

        auto input = std::ifstream{*resolved, std::ios::binary};
        if (!input)
        {
            return OfflineLuaStartResult{
                .error = OfflineLuaStartError::sourceReadFailed,
                .detail = "Failed to open the Lua source.",
            };
        }
        auto source = std::string(static_cast<std::size_t>(sourceBytes), '\0');
        input.read(source.data(), static_cast<std::streamsize>(source.size()));
        if (!input || static_cast<std::size_t>(input.gcount()) != source.size())
        {
            return OfflineLuaStartResult{
                .error = OfflineLuaStartError::sourceReadFailed,
                .detail = "Failed to read the complete Lua source.",
            };
        }

        optionSystem_ = std::ref(optionSystem);
        auto config = dependency::lua::SandboxConfig{};
        config.hostFunctions = {
            dependency::lua::HostFunctionBinding{
                .dottedName = "nr.options.snapshot",
                .function = [this](std::span<const LuaValue> arguments) { return snapshot(arguments); },
            },
            dependency::lua::HostFunctionBinding{
                .dottedName = "nr.options.get",
                .function = [this](std::span<const LuaValue> arguments) { return get(arguments); },
            },
            dependency::lua::HostFunctionBinding{
                .dottedName = "nr.options.apply",
                .function = [this](std::span<const LuaValue> arguments) { return apply(arguments); },
            },
            dependency::lua::HostFunctionBinding{
                .dottedName = "nr.frame.next",
                .function =
                    [](std::span<const LuaValue> arguments) {
                        return arguments.empty() ? HostCallResult::suspend()
                                                 : HostCallResult::failure("nr.frame.next expects no arguments.");
                    },
            },
            dependency::lua::HostFunctionBinding{
                .dottedName = "nr.log.info",
                .function = [this](std::span<const LuaValue> arguments) { return logInfo(arguments); },
            },
        };
        auto started = sandbox_.start(std::move(source), resolved->generic_string(), std::move(config));
        if (!started.started)
        {
            optionSystem_.reset();
            return OfflineLuaStartResult{
                .error = OfflineLuaStartError::sandboxStartFailed,
                .detail = std::move(started.detail),
            };
        }
        return OfflineLuaStartResult{.started = true};
    }

    [[nodiscard]] OfflineLuaFrameResult resume(std::shared_ptr<const nr::options::OptionFrameSnapshot> snapshotValue)
    {
        if (!sandbox_.started())
        {
            return OfflineLuaFrameResult{.status = OfflineLuaFrameStatus::notStarted};
        }
        if (!snapshotValue)
        {
            return OfflineLuaFrameResult{
                .status = OfflineLuaFrameStatus::failed,
                .detail = "Offline Lua resume requires a published snapshot.",
            };
        }

        currentSnapshot_ = std::move(snapshotValue);
        logCallsThisResume_ = 0u;
        logBytesThisResume_ = 0u;
        auto result = sandbox_.resume();
        currentSnapshot_.reset();

        using enum dependency::lua::ResumeStatus;
        switch (result.status)
        {
        case yielded:
            return OfflineLuaFrameResult{.status = OfflineLuaFrameStatus::running};
        case completed:
            return OfflineLuaFrameResult{.status = OfflineLuaFrameStatus::completed};
        case failed:
            return OfflineLuaFrameResult{
                .status = OfflineLuaFrameStatus::failed,
                .detail = std::move(result.detail),
            };
        case notStarted:
            return OfflineLuaFrameResult{.status = OfflineLuaFrameStatus::notStarted};
        }
        std::unreachable();
    }

    void stop() noexcept
    {
        currentSnapshot_.reset();
        optionSystem_.reset();
        sandbox_.close();
        logCallsThisResume_ = 0u;
        logBytesThisResume_ = 0u;
    }

    [[nodiscard]] bool started() const noexcept
    {
        return sandbox_.started();
    }

  private:
    [[nodiscard]] HostCallResult snapshot(std::span<const LuaValue> arguments) const
    {
        if (!arguments.empty())
        {
            return HostCallResult::failure("nr.options.snapshot expects no arguments.");
        }
        if (!currentSnapshot_)
        {
            return HostCallResult::failure("nr.options.snapshot is only available during a frame resume.");
        }
        return HostCallResult::success({snapshotRecord(*currentSnapshot_)});
    }

    [[nodiscard]] HostCallResult get(std::span<const LuaValue> arguments) const
    {
        if (arguments.size() != 1u)
        {
            return HostCallResult::failure("nr.options.get expects one option ID.");
        }
        auto const *idText = std::get_if<std::string>(&arguments.front().storage);
        if (idText == nullptr)
        {
            return HostCallResult::failure("nr.options.get requires a string option ID.");
        }
        if (!currentSnapshot_ || !currentSnapshot_->catalog)
        {
            return HostCallResult::failure("nr.options.get is only available during a frame resume.");
        }

        auto id = nr::options::OptionId::parse(*idText);
        auto const *definition = id ? currentSnapshot_->catalog->find(*id) : nullptr;
        if (definition == nullptr)
        {
            return HostCallResult::success({
                LuaValue{},
                LuaValue{"unknown_option"},
            });
        }
        return HostCallResult::success({
            LuaValue{LuaObject{
                {"frame_index", integerValue(currentSnapshot_->frameIndex)},
                {"snapshot_revision", integerValue(currentSnapshot_->revision)},
                {"graph_generation", integerValue(currentSnapshot_->graphGeneration)},
                {"binding_epoch", integerValue(currentSnapshot_->bindingEpoch)},
                {"snapshot_token", LuaValue{currentSnapshot_->snapshotToken}},
                {"option", optionRecord(*currentSnapshot_, *definition)},
            }},
        });
    }

    [[nodiscard]] HostCallResult apply(std::span<const LuaValue> arguments)
    {
        if (arguments.size() != 3u)
        {
            return HostCallResult::failure("nr.options.apply expects id, value, and binding.");
        }
        if (!currentSnapshot_ || !currentSnapshot_->catalog || !optionSystem_)
        {
            return HostCallResult::failure("nr.options.apply is only available during a frame resume.");
        }

        auto const *idText = std::get_if<std::string>(&arguments[0].storage);
        if (idText == nullptr)
        {
            return HostCallResult::failure("nr.options.apply requires a string option ID.");
        }
        auto id = nr::options::OptionId::parse(*idText);
        auto const *definition = id ? currentSnapshot_->catalog->find(*id) : nullptr;
        if (definition == nullptr)
        {
            return HostCallResult::success({
                LuaValue{false},
                LuaValue{"unknown_option"},
            });
        }

        auto converted = luaToWire(arguments[1], definition->schema);
        if (!converted)
        {
            return HostCallResult::failure(std::move(converted.error()));
        }

        auto const *binding = std::get_if<LuaObject>(&arguments[2].storage);
        if (binding == nullptr)
        {
            return HostCallResult::failure("nr.options.apply binding must be an object.");
        }
        auto proof = nr::options::BindingProof{};
        auto validBinding = true;
        auto bindingError = std::string{};
        std::ranges::for_each(*binding, [&](auto const &entry) {
            if (!validBinding)
            {
                return;
            }
            if (entry.first == "binding_epoch")
            {
                auto const *epoch = std::get_if<std::int64_t>(&entry.second.storage);
                if (epoch == nullptr || *epoch < 0)
                {
                    validBinding = false;
                    bindingError = "binding_epoch must be a non-negative integer.";
                    return;
                }
                proof.bindingEpoch = static_cast<std::uint64_t>(*epoch);
            }
            else if (entry.first == "snapshot_token")
            {
                auto const *token = std::get_if<std::string>(&entry.second.storage);
                if (token == nullptr || token->empty())
                {
                    validBinding = false;
                    bindingError = "snapshot_token must be a non-empty string.";
                    return;
                }
                proof.snapshotToken = *token;
            }
            else
            {
                validBinding = false;
                bindingError = std::format("Unknown binding field '{}'.", entry.first);
            }
        });
        if (!validBinding || (!proof.bindingEpoch && !proof.snapshotToken))
        {
            return HostCallResult::failure(bindingError.empty() ? "Binding requires binding_epoch or snapshot_token."
                                                                : std::move(bindingError));
        }

        auto scheduled = optionSystem_->get().trySchedule(nr::options::OptionMutationRequest{
            .id = *id,
            .value = std::move(*converted),
            .binding = std::move(proof),
            .origin = nr::options::MutationOrigin::lua,
        });
        return scheduled.started ? HostCallResult::success({LuaValue{true}})
                                 : HostCallResult::success({
                                       LuaValue{false},
                                       LuaValue{luaRejectionReason(scheduled.reason)},
                                   });
    }

    [[nodiscard]] HostCallResult logInfo(std::span<const LuaValue> arguments)
    {
        if (arguments.size() != 1u)
        {
            return HostCallResult::failure("nr.log.info expects one string.");
        }
        auto const *text = std::get_if<std::string>(&arguments.front().storage);
        if (text == nullptr)
        {
            return HostCallResult::failure("nr.log.info expects one string.");
        }
        if (text->size() > kMaximumLogCallBytes)
        {
            return HostCallResult::failure("nr.log.info message exceeds 1 KiB.");
        }
        if (logCallsThisResume_ >= kMaximumLogCallsPerResume ||
            logBytesThisResume_ > kMaximumLogBytesPerResume - text->size())
        {
            return HostCallResult::failure("nr.log.info exceeded the per-resume log quota.");
        }

        auto sanitized = *text;
        std::erase(sanitized, '\r');
        std::erase(sanitized, '\n');
        ++logCallsThisResume_;
        logBytesThisResume_ += text->size();
        nr::nrLog(nr::LogLevel::info, "LUA", sanitized);
        return HostCallResult::success();
    }

    dependency::lua::Sandbox sandbox_{};
    std::optional<std::reference_wrapper<nr::options::OptionSystem>> optionSystem_{};
    std::shared_ptr<const nr::options::OptionFrameSnapshot> currentSnapshot_{};
    std::size_t logCallsThisResume_ = 0u;
    std::size_t logBytesThisResume_ = 0u;
};

OfflineLuaHost::OfflineLuaHost() : impl_(std::make_unique<Impl>())
{
}

OfflineLuaHost::~OfflineLuaHost() = default;

OfflineLuaStartResult OfflineLuaHost::start(nr::options::OptionSystem &optionSystem,
                                            const std::filesystem::path &rootRelativeScript)
{
    return impl_->start(optionSystem, rootRelativeScript);
}

OfflineLuaFrameResult OfflineLuaHost::resume(std::shared_ptr<const nr::options::OptionFrameSnapshot> snapshot)
{
    return impl_->resume(std::move(snapshot));
}

void OfflineLuaHost::stop() noexcept
{
    impl_->stop();
}

bool OfflineLuaHost::started() const noexcept
{
    return impl_->started();
}
} // namespace nr::automation
