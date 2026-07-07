export module nr.renderer:variantRegistry;

import nr.rhi;
import nr.utils;
import std;

export namespace nr::renderer
{
enum class VariantWriteSource : std::uint8_t
{
    Default,
    Auto,
    Node,
    Ui,
};

enum class VariantItemEffect : std::uint8_t
{
    RuntimeOnly,
    SlangLinkTime,
    PipelineState,
};

struct VariantItemDesc
{
    nr::rhi::ShaderVariantItemDesc shader{};
    VariantItemEffect effect = VariantItemEffect::RuntimeOnly;
    bool uiVisible = true;
};

struct VariantItemSnapshot
{
    std::string runtimeName{};
    VariantItemDesc desc{};
    nr::rhi::ShaderVariantValue value = false;
};

struct VariantPatch
{
    std::string runtimeName{};
    std::string itemId{};
    nr::rhi::ShaderVariantValue value = false;
    VariantWriteSource source = VariantWriteSource::Node;
    std::uint64_t sequence = 0u;
};

class VariantStateRegistry
{
  public:
    void clear();

    void clearRuntime(std::string_view runtimeName);

    void registerItems(
        std::string_view runtimeName,
        std::span<const VariantItemDesc> items);

    void registerItem(
        std::string_view runtimeName,
        VariantItemDesc item);

    bool submitPatch(
        std::string_view runtimeName,
        std::string_view itemId,
        nr::rhi::ShaderVariantValue value,
        VariantWriteSource source = VariantWriteSource::Node);

    template <typename TValue>
    requires(
        std::same_as<std::remove_cvref_t<TValue>, bool> ||
        std::same_as<std::remove_cvref_t<TValue>, std::int32_t> ||
        std::same_as<std::remove_cvref_t<TValue>, std::uint32_t> ||
        std::same_as<std::remove_cvref_t<TValue>, float> ||
        std::same_as<std::remove_cvref_t<TValue>, std::string>)
    bool submitTypedPatch(
        std::string_view runtimeName,
        std::string_view itemId,
        TValue&& value,
        VariantWriteSource source = VariantWriteSource::Node)
    {
        return submitPatch(
            runtimeName,
            itemId,
            nr::rhi::ShaderVariantValue{std::forward<TValue>(value)},
            source);
    }

    void commitFramePatches();

    [[nodiscard]] std::optional<nr::rhi::ShaderVariantValue> value(
        std::string_view runtimeName,
        std::string_view itemId) const;

    template <typename TValue>
    [[nodiscard]] TValue valueOr(
        std::string_view runtimeName,
        std::string_view itemId,
        TValue fallback) const
    {
        auto current = value(runtimeName, itemId);
        if (!current.has_value())
        {
            return fallback;
        }

        auto const* typed = std::get_if<TValue>(std::addressof(*current));
        return typed != nullptr ? *typed : fallback;
    }

    [[nodiscard]] std::vector<VariantItemSnapshot> snapshot(std::string_view runtimeName) const;

    [[nodiscard]] std::vector<std::string> runtimeNames() const;

    [[nodiscard]] nr::rhi::ShaderVariantValueSet valueSet(
        std::string_view runtimeName,
        bool includeRuntimeOnly = true) const;

    [[nodiscard]] std::uint64_t stateHash(
        std::string_view runtimeName,
        bool includeRuntimeOnly = true) const;

    [[nodiscard]] nr::rhi::SlangProgramVariantDesc makeSlangProgramVariantDesc(
        std::string_view runtimeName,
        std::string_view debugName) const;

  private:
    struct RuntimeState
    {
        std::map<std::string, VariantItemDesc> itemsById{};
        std::map<std::string, nr::rhi::ShaderVariantValue> valuesById{};
    };

    [[nodiscard]] const RuntimeState* runtimeStateOrNull(std::string_view runtimeName) const noexcept;

    [[nodiscard]] RuntimeState& runtimeState(std::string_view runtimeName);

    [[nodiscard]] const VariantItemDesc* itemOrNull(
        const RuntimeState& runtime,
        std::string_view itemId) const noexcept;

    [[nodiscard]] std::optional<nr::rhi::ShaderVariantValue> normalizePatchValue(
        std::string_view runtimeName,
        const VariantItemDesc& item,
        const nr::rhi::ShaderVariantValue& value) const;

    std::map<std::string, RuntimeState> runtimes_{};
    std::vector<VariantPatch> stagedPatches_{};
    std::uint64_t nextSequence_ = 1u;
};
} // namespace nr::renderer
