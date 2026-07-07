module nr.renderer;
import :variantRegistry;

import nr.utils;
import std;

namespace nr::renderer
{
namespace
{
[[nodiscard]] std::uint8_t variantWriteSourcePriority(VariantWriteSource source) noexcept
{
    return static_cast<std::uint8_t>(source);
}

[[nodiscard]] bool patchWins(const VariantPatch& candidate, const VariantPatch& current) noexcept
{
    auto const candidatePriority = variantWriteSourcePriority(candidate.source);
    auto const currentPriority = variantWriteSourcePriority(current.source);
    if (candidatePriority != currentPriority)
    {
        return candidatePriority > currentPriority;
    }
    return candidate.sequence > current.sequence;
}

[[nodiscard]] std::string variantRuntimeItemKey(std::string_view runtimeName, std::string_view itemId)
{
    return std::format("{}::{}", runtimeName, itemId);
}
} // namespace

void VariantStateRegistry::clear()
{
    runtimes_.clear();
    stagedPatches_.clear();
    nextSequence_ = 1u;
}

void VariantStateRegistry::clearRuntime(std::string_view runtimeName)
{
    runtimes_.erase(std::string{runtimeName});
    std::erase_if(stagedPatches_, [&](const VariantPatch& patch) {
        return patch.runtimeName == runtimeName;
    });
}

void VariantStateRegistry::registerItems(
    std::string_view runtimeName,
    std::span<const VariantItemDesc> items)
{
    std::ranges::for_each(items, [&](const VariantItemDesc& item) {
        registerItem(runtimeName, item);
    });
}

void VariantStateRegistry::registerItem(
    std::string_view runtimeName,
    VariantItemDesc item)
{
    nrAssert(!runtimeName.empty(), "VariantStateRegistry::registerItem requires a non-empty runtime name.");
    nrAssert(!item.shader.id.empty(), "VariantStateRegistry::registerItem requires a non-empty item id.");

    auto normalizedDefault = nr::rhi::normalizeShaderVariantValue(item.shader, item.shader.defaultValue);
    nrAssert(
        normalizedDefault.has_value(),
        std::format("VariantStateRegistry item '{}' has an invalid default value.", item.shader.id));

    item.shader.defaultValue = *normalizedDefault;

    auto& runtime = runtimeState(runtimeName);
    auto [itemIt, inserted] = runtime.itemsById.insert_or_assign(item.shader.id, std::move(item));
    if (inserted || !runtime.valuesById.contains(itemIt->first))
    {
        runtime.valuesById.insert_or_assign(itemIt->first, itemIt->second.shader.defaultValue);
        return;
    }

    auto normalizedCurrent = nr::rhi::normalizeShaderVariantValue(
        itemIt->second.shader,
        runtime.valuesById.at(itemIt->first));
    if (!normalizedCurrent.has_value())
    {
        runtime.valuesById.insert_or_assign(itemIt->first, itemIt->second.shader.defaultValue);
        return;
    }

    runtime.valuesById.insert_or_assign(itemIt->first, *normalizedCurrent);
}

bool VariantStateRegistry::submitPatch(
    std::string_view runtimeName,
    std::string_view itemId,
    nr::rhi::ShaderVariantValue value,
    VariantWriteSource source)
{
    if (runtimeName.empty() || itemId.empty())
    {
        nrInfo<LogLevel::warning>("VariantStateRegistry rejected patch with empty runtime name or item id.");
        return false;
    }

    stagedPatches_.push_back(VariantPatch{
        .runtimeName = std::string{runtimeName},
        .itemId = std::string{itemId},
        .value = std::move(value),
        .source = source,
        .sequence = nextSequence_++,
    });
    return true;
}

void VariantStateRegistry::commitFramePatches()
{
    auto winningPatchesByItem = std::map<std::string, VariantPatch>{};
    std::ranges::for_each(stagedPatches_, [&](const VariantPatch& patch) {
        auto const key = variantRuntimeItemKey(patch.runtimeName, patch.itemId);
        auto [it, inserted] = winningPatchesByItem.try_emplace(key, patch);
        if (!inserted && patchWins(patch, it->second))
        {
            it->second = patch;
        }
    });
    stagedPatches_.clear();

    std::ranges::for_each(winningPatchesByItem | std::views::values, [&](const VariantPatch& patch) {
        auto runtimeIt = runtimes_.find(patch.runtimeName);
        if (runtimeIt == runtimes_.end())
        {
            nrInfo<LogLevel::warning>(std::format(
                "VariantStateRegistry rejected patch for unknown runtime '{}'.",
                patch.runtimeName));
            return;
        }

        auto& runtime = runtimeIt->second;
        auto item = itemOrNull(runtime, patch.itemId);
        if (item == nullptr)
        {
            nrInfo<LogLevel::warning>(std::format(
                "VariantStateRegistry rejected patch for unknown item '{}.{}'.",
                patch.runtimeName,
                patch.itemId));
            return;
        }

        auto normalized = normalizePatchValue(patch.runtimeName, *item, patch.value);
        if (!normalized.has_value())
        {
            return;
        }

        runtime.valuesById.insert_or_assign(patch.itemId, std::move(*normalized));
    });
}

[[nodiscard]] std::optional<nr::rhi::ShaderVariantValue> VariantStateRegistry::value(
    std::string_view runtimeName,
    std::string_view itemId) const
{
    auto const* runtime = runtimeStateOrNull(runtimeName);
    if (runtime == nullptr)
    {
        return {};
    }

    auto valueIt = runtime->valuesById.find(std::string{itemId});
    if (valueIt == runtime->valuesById.end())
    {
        return {};
    }
    return valueIt->second;
}

[[nodiscard]] std::vector<VariantItemSnapshot> VariantStateRegistry::snapshot(std::string_view runtimeName) const
{
    auto const* runtime = runtimeStateOrNull(runtimeName);
    if (runtime == nullptr)
    {
        return {};
    }

    auto snapshots = std::vector<VariantItemSnapshot>{};
    snapshots.reserve(runtime->itemsById.size());
    std::ranges::for_each(runtime->itemsById, [&](const auto& entry) {
        auto valueIt = runtime->valuesById.find(entry.first);
        snapshots.push_back(VariantItemSnapshot{
            .runtimeName = std::string{runtimeName},
            .desc = entry.second,
            .value = valueIt != runtime->valuesById.end()
                         ? valueIt->second
                         : entry.second.shader.defaultValue,
        });
    });
    return snapshots;
}

[[nodiscard]] std::vector<std::string> VariantStateRegistry::runtimeNames() const
{
    return runtimes_ |
           std::views::keys |
           std::ranges::to<std::vector>();
}

[[nodiscard]] nr::rhi::ShaderVariantValueSet VariantStateRegistry::valueSet(
    std::string_view runtimeName,
    bool includeRuntimeOnly) const
{
    auto valueSet = nr::rhi::ShaderVariantValueSet{};
    auto const* runtime = runtimeStateOrNull(runtimeName);
    if (runtime == nullptr)
    {
        return valueSet;
    }

    std::ranges::for_each(runtime->itemsById, [&](const auto& entry) {
        auto const& item = entry.second;
        if (!includeRuntimeOnly && item.effect == VariantItemEffect::RuntimeOnly)
        {
            return;
        }

        auto valueIt = runtime->valuesById.find(entry.first);
        valueSet.values.try_emplace(
            entry.first,
            valueIt != runtime->valuesById.end()
                ? valueIt->second
                : item.shader.defaultValue);
    });
    return valueSet;
}

[[nodiscard]] std::uint64_t VariantStateRegistry::stateHash(
    std::string_view runtimeName,
    bool includeRuntimeOnly) const
{
    return valueSet(runtimeName, includeRuntimeOnly).hashValue();
}

[[nodiscard]] nr::rhi::SlangProgramVariantDesc VariantStateRegistry::makeSlangProgramVariantDesc(
    std::string_view runtimeName,
    std::string_view debugName) const
{
    auto const* runtime = runtimeStateOrNull(runtimeName);
    if (runtime == nullptr)
    {
        auto desc = nr::rhi::SlangProgramVariantDesc{};
        desc.debugName = std::string{debugName};
        return desc;
    }

    auto slangItems = runtime->itemsById |
                      std::views::values |
                      std::views::filter([](const VariantItemDesc& item) {
                          return item.effect != VariantItemEffect::RuntimeOnly &&
                                 item.shader.slangBinding.kind != nr::rhi::ShaderVariantSlangBindingKind::None;
                      }) |
                      std::views::transform([](const VariantItemDesc& item) {
                          return item.shader;
                      }) |
                      std::ranges::to<std::vector>();
    auto compileValues = valueSet(runtimeName, false);
    return nr::rhi::makeSlangProgramVariantDesc(
        debugName,
        std::span<const nr::rhi::ShaderVariantItemDesc>{slangItems.data(), slangItems.size()},
        compileValues);
}

[[nodiscard]] const VariantStateRegistry::RuntimeState* VariantStateRegistry::runtimeStateOrNull(
    std::string_view runtimeName) const noexcept
{
    auto runtimeIt = runtimes_.find(std::string{runtimeName});
    return runtimeIt != runtimes_.end() ? std::addressof(runtimeIt->second) : nullptr;
}

[[nodiscard]] VariantStateRegistry::RuntimeState& VariantStateRegistry::runtimeState(std::string_view runtimeName)
{
    return runtimes_[std::string{runtimeName}];
}

[[nodiscard]] const VariantItemDesc* VariantStateRegistry::itemOrNull(
    const RuntimeState& runtime,
    std::string_view itemId) const noexcept
{
    auto itemIt = runtime.itemsById.find(std::string{itemId});
    return itemIt != runtime.itemsById.end() ? std::addressof(itemIt->second) : nullptr;
}

[[nodiscard]] std::optional<nr::rhi::ShaderVariantValue> VariantStateRegistry::normalizePatchValue(
    std::string_view runtimeName,
    const VariantItemDesc& item,
    const nr::rhi::ShaderVariantValue& value) const
{
    auto normalized = nr::rhi::normalizeShaderVariantValue(item.shader, value);
    if (!normalized.has_value())
    {
        nrInfo<LogLevel::warning>(std::format(
            "VariantStateRegistry rejected invalid value '{}' for item '{}.{}'.",
            nr::rhi::shaderVariantValueToString(value),
            runtimeName,
            item.shader.id));
        return {};
    }

    return normalized;
}
} // namespace nr::renderer
