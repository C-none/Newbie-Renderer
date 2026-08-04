export module nr.options:model;

import nr.utils;
import std;

export namespace nr::options
{
namespace detail
{
[[nodiscard]] constexpr bool isOptionSegmentHead(char value) noexcept
{
    return value >= 'a' && value <= 'z';
}

[[nodiscard]] constexpr bool isOptionSegmentTail(char value) noexcept
{
    return isOptionSegmentHead(value) || (value >= '0' && value <= '9') || value == '_';
}

[[nodiscard]] constexpr bool validOptionIdText(std::string_view text) noexcept
{
    if (text.empty() || text.front() == '.' || text.back() == '.')
    {
        return false;
    }

    auto segmentHead = true;
    auto const valid = std::ranges::all_of(text, [&](char value) {
        if (value == '.')
        {
            if (segmentHead)
            {
                return false;
            }
            segmentHead = true;
            return true;
        }

        if ((segmentHead && !isOptionSegmentHead(value)) || (!segmentHead && !isOptionSegmentTail(value)))
        {
            return false;
        }
        segmentHead = false;
        return true;
    });
    return valid && !segmentHead;
}
} // namespace detail

class OptionId
{
  public:
    OptionId() = default;

    [[nodiscard]] static std::optional<OptionId> parse(std::string_view text);

    [[nodiscard]] bool valid() const noexcept
    {
        return detail::validOptionIdText(value_);
    }

    [[nodiscard]] std::string_view value() const noexcept
    {
        return value_;
    }

    [[nodiscard]] friend auto operator<=>(const OptionId &, const OptionId &) noexcept = default;

  private:
    explicit OptionId(std::string value) : value_(std::move(value))
    {
    }

    std::string value_{};
};

struct OptionWireValue
{
    using Array = std::vector<OptionWireValue>;
    using Object = std::map<std::string, OptionWireValue, std::less<>>;
    using Storage = std::variant<bool, std::int64_t, std::uint64_t, double, std::string, Array, Object>;

    OptionWireValue() = default;
    OptionWireValue(bool value) : storage(value)
    {
    }
    OptionWireValue(std::int64_t value) : storage(value)
    {
    }
    OptionWireValue(std::uint64_t value) : storage(value)
    {
    }
    OptionWireValue(double value) : storage(value)
    {
    }
    OptionWireValue(std::string value) : storage(std::move(value))
    {
    }
    OptionWireValue(std::string_view value) : storage(std::string{value})
    {
    }
    OptionWireValue(const char *value) : storage(std::string{value})
    {
    }
    OptionWireValue(Array value) : storage(std::move(value))
    {
    }
    OptionWireValue(Object value) : storage(std::move(value))
    {
    }

    Storage storage = false;

    [[nodiscard]] friend bool operator==(const OptionWireValue &, const OptionWireValue &) = default;
};

template <typename T>
concept WireValueAlternative =
    std::same_as<T, bool> || std::same_as<T, std::int64_t> || std::same_as<T, std::uint64_t> ||
    std::same_as<T, double> || std::same_as<T, std::string> || std::same_as<T, OptionWireValue::Array> ||
    std::same_as<T, OptionWireValue::Object>;

template <WireValueAlternative T> class OptionKey
{
  public:
    template <std::size_t Size> consteval explicit OptionKey(const char (&id)[Size]) : id_(id, Size - 1u)
    {
        if (!detail::validOptionIdText(id_))
        {
            throw "OptionKey requires a lower-case ASCII dotted option ID";
        }
    }

    [[nodiscard]] constexpr std::string_view id() const noexcept
    {
        return id_;
    }

    [[nodiscard]] friend constexpr bool operator==(OptionKey, OptionKey) noexcept = default;

  private:
    std::string_view id_;
};

enum class OptionValueType : std::uint8_t
{
    boolean,
    signedInteger,
    unsignedInteger,
    number,
    string,
    array,
    object,
};

struct SchemaValidation
{
    bool valid = true;
    std::string path{};
    std::string detail{};

    [[nodiscard]] static SchemaValidation success() noexcept
    {
        return {};
    }

    [[nodiscard]] static SchemaValidation failure(std::string path, std::string detail)
    {
        return SchemaValidation{
            .valid = false,
            .path = std::move(path),
            .detail = std::move(detail),
        };
    }
};

struct OptionSchema;

struct OptionObjectField
{
    std::shared_ptr<const OptionSchema> schema{};
    bool required = true;
};

struct OptionSchema
{
    using ObjectValidator = std::function<std::optional<std::string>(const OptionWireValue::Object &)>;

    OptionValueType type = OptionValueType::boolean;
    std::optional<std::int64_t> signedMinimum{};
    std::optional<std::int64_t> signedMaximum{};
    std::optional<std::uint64_t> unsignedMinimum{};
    std::optional<std::uint64_t> unsignedMaximum{};
    std::optional<double> numberMinimum{};
    std::optional<double> numberMaximum{};
    std::size_t minimumSize = 0u;
    std::size_t maximumSize = std::numeric_limits<std::size_t>::max();
    std::vector<std::string> allowedStrings{};
    std::shared_ptr<const OptionSchema> elementSchema{};
    std::map<std::string, OptionObjectField, std::less<>> objectFields{};
    bool closedObject = true;
    ObjectValidator objectValidator{};

    [[nodiscard]] static OptionSchema boolean() noexcept;
    [[nodiscard]] static OptionSchema signedInteger(std::int64_t minimum, std::int64_t maximum) noexcept;
    [[nodiscard]] static OptionSchema unsignedInteger(std::uint64_t minimum, std::uint64_t maximum) noexcept;
    [[nodiscard]] static OptionSchema number(double minimum, double maximum) noexcept;
    [[nodiscard]] static OptionSchema string(std::size_t maximumBytes, std::vector<std::string> allowed = {});
    [[nodiscard]] static OptionSchema array(OptionSchema element, std::size_t minimumItems, std::size_t maximumItems);
    [[nodiscard]] static OptionSchema object(std::map<std::string, OptionObjectField, std::less<>> fields,
                                             ObjectValidator validator = {});
    [[nodiscard]] static OptionSchema emptyObject();

    [[nodiscard]] SchemaValidation validate(const OptionWireValue &value, std::size_t maximumDepth = 16u) const;
};

enum class OptionScope : std::uint8_t
{
    session,
    graph,
};

enum class OptionValueLifetime : std::uint8_t
{
    canonical,
    frameEffect,
};

enum class OptionUiControl : std::uint8_t
{
    automatic,
    checkbox,
    combo,
    slider,
    input,
    button,
    hidden,
};

struct OptionPresentation
{
    std::string group{};
    std::string label{};
    OptionUiControl control = OptionUiControl::automatic;
    std::int32_t order = 0;
};

using OptionValueMap = std::map<OptionId, OptionWireValue>;
using OptionAdmissionValidator =
    std::function<std::optional<std::string>(const OptionWireValue &, const OptionValueMap &)>;

struct OptionDefinition
{
    OptionId id{};
    OptionSchema schema{};
    OptionWireValue defaultValue{};
    OptionScope scope = OptionScope::session;
    OptionValueLifetime lifetime = OptionValueLifetime::canonical;
    OptionPresentation presentation{};
    OptionAdmissionValidator admissionValidator{};
    bool resetsTemporalHistory = false;
};

class OptionCatalog
{
  public:
    using DefinitionMap = std::map<OptionId, OptionDefinition>;

    [[nodiscard]] const DefinitionMap &definitions() const noexcept
    {
        return definitions_;
    }

    [[nodiscard]] const OptionDefinition *find(const OptionId &id) const noexcept;
    [[nodiscard]] const OptionDefinition *find(std::string_view id) const noexcept;
    [[nodiscard]] std::size_t estimatedSnapshotBytes() const noexcept
    {
        return estimatedSnapshotBytes_;
    }

  private:
    friend class OptionCatalogBuilder;

    DefinitionMap definitions_{};
    std::size_t estimatedSnapshotBytes_ = 0u;
};

struct OptionAvailability
{
    bool available = false;
    std::string reason = "unavailable";

    [[nodiscard]] friend bool operator==(const OptionAvailability &, const OptionAvailability &) = default;
};

inline constexpr std::size_t maximumAvailabilityReasonBytes = 256u;

using OptionAvailabilityMap = std::map<OptionId, OptionAvailability>;

enum class MutationOrigin : std::uint8_t
{
    imgui,
    camera,
    websocket,
    lua,
};

enum class AuthorityMode : std::uint8_t
{
    human,
    agent,
    offlineLua,
};

struct BindingProof
{
    std::optional<std::uint64_t> bindingEpoch{};
    std::optional<std::string> snapshotToken{};
};

struct OptionMutationRequest
{
    OptionId id{};
    OptionWireValue value{};
    BindingProof binding{};
    MutationOrigin origin = MutationOrigin::imgui;
    std::optional<std::string> requestId{};
};

enum class ScheduleRejectReason : std::uint8_t
{
    none,
    invalidParams,
    unknownOption,
    invalidValue,
    unavailable,
    busy,
    admissionClosed,
    staleBinding,
    staleSnapshot,
    bindingProofMismatch,
    unauthorizedOrigin,
    shutdown,
};

[[nodiscard]] std::string_view wireName(ScheduleRejectReason reason) noexcept;

struct ScheduleResult
{
    bool started = false;
    ScheduleRejectReason reason = ScheduleRejectReason::none;
    std::uint64_t sequence = 0u;

    [[nodiscard]] static ScheduleResult accepted(std::uint64_t sequence) noexcept
    {
        return ScheduleResult{.started = true, .sequence = sequence};
    }

    [[nodiscard]] static ScheduleResult rejected(ScheduleRejectReason reason) noexcept
    {
        return ScheduleResult{.reason = reason};
    }
};

struct FrameEffect
{
    std::uint64_t sequence = 0u;
    OptionId id{};
    OptionWireValue input{};
    MutationOrigin origin = MutationOrigin::imgui;
    std::optional<std::string> requestId{};
};

struct OptionFrameSnapshot
{
    std::shared_ptr<const OptionCatalog> catalog{};
    OptionValueMap values{};
    OptionAvailabilityMap availability{};
    std::uint64_t frameIndex = 0u;
    std::uint64_t revision = 0u;
    std::uint64_t graphGeneration = 0u;
    std::uint64_t bindingEpoch = 0u;
    std::string snapshotToken{};
    std::optional<FrameEffect> effect{};

    [[nodiscard]] const OptionWireValue *findValue(const OptionId &id) const noexcept;
    [[nodiscard]] const OptionWireValue *findValue(std::string_view id) const noexcept;
    [[nodiscard]] const OptionAvailability *findAvailability(const OptionId &id) const noexcept;

    template <WireValueAlternative T> [[nodiscard]] const T *find(OptionKey<T> key) const noexcept
    {
        auto const *wire = findValue(key.id());
        return wire != nullptr ? std::get_if<T>(&wire->storage) : nullptr;
    }
};

struct LiveBinding
{
    std::uint64_t graphGeneration = 0u;
    std::uint64_t bindingEpoch = 0u;
    std::string snapshotToken{};
};

enum class OptionLogPhase : std::uint8_t
{
    dispatchStarted,
    terminal,
};

enum class OptionLogStatus : std::uint8_t
{
    started,
    succeeded,
    failed,
    abandoned,
};

struct OptionMachineRecord
{
    std::uint64_t sequence = 0u;
    OptionId id{};
    OptionLogPhase phase = OptionLogPhase::terminal;
    OptionLogStatus status = OptionLogStatus::succeeded;
    std::uint64_t frameIndex = 0u;
    MutationOrigin origin = MutationOrigin::imgui;
    std::optional<std::string> requestId{};
    std::optional<std::string> reason{};
};

[[nodiscard]] std::string serializeMachineRecord(const OptionMachineRecord &record);

template <nr::LogLevel Level = nr::LogLevel::info> void emitMachineRecord(const OptionMachineRecord &record)
{
    nr::nrCompactRecord<Level>("NR_OPTION_V1", serializeMachineRecord(record));
}
} // namespace nr::options
