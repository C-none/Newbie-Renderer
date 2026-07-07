module nr.rhi;
import :shaderVariant;

import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
template <typename T>
[[nodiscard]] std::optional<T> shaderVariantGetAs(const ShaderVariantValue& value) noexcept
{
    if (const auto* typedValue = std::get_if<T>(std::addressof(value)); typedValue != nullptr)
    {
        return *typedValue;
    }
    return {};
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeBoolValue(const ShaderVariantValue& value)
{
    auto typedValue = shaderVariantGetAs<bool>(value);
    if (!typedValue.has_value())
    {
        return {};
    }
    return *typedValue;
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeInt32Value(
    const ShaderVariantNumericRange& range,
    const ShaderVariantValue& value)
{
    auto typedValue = shaderVariantGetAs<std::int32_t>(value);
    if (!typedValue.has_value())
    {
        return {};
    }

    if (!range.bounded)
    {
        return *typedValue;
    }

    auto const minValue = static_cast<std::int32_t>(range.minValue);
    auto const maxValue = static_cast<std::int32_t>(range.maxValue);
    return std::clamp(*typedValue, minValue, maxValue);
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeUInt32Value(
    const ShaderVariantNumericRange& range,
    const ShaderVariantValue& value)
{
    auto typedValue = shaderVariantGetAs<std::uint32_t>(value);
    if (!typedValue.has_value())
    {
        return {};
    }

    if (!range.bounded)
    {
        return *typedValue;
    }

    auto const minValue = static_cast<std::uint32_t>(std::max(0.0, range.minValue));
    auto const maxValue = static_cast<std::uint32_t>(std::max(0.0, range.maxValue));
    return std::clamp(*typedValue, minValue, maxValue);
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeFloat32Value(
    const ShaderVariantNumericRange& range,
    const ShaderVariantValue& value)
{
    auto typedValue = shaderVariantGetAs<float>(value);
    if (!typedValue.has_value())
    {
        return {};
    }

    if (!range.bounded)
    {
        return *typedValue;
    }

    return std::clamp(
        *typedValue,
        static_cast<float>(range.minValue),
        static_cast<float>(range.maxValue));
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeStringValue(
    const ShaderVariantItemDesc& desc,
    const ShaderVariantValue& value)
{
    auto typedValue = shaderVariantGetAs<std::string>(value);
    if (!typedValue.has_value())
    {
        return {};
    }

    auto choice = std::ranges::find_if(desc.stringChoices, [&](const ShaderVariantStringChoice& item) {
        return item.value == *typedValue;
    });
    if (choice == std::ranges::end(desc.stringChoices))
    {
        return {};
    }
    return *typedValue;
}

void hashAppendShaderVariantValue(std::uint64_t& state, const ShaderVariantValue& value) noexcept
{
    nr::hash::hashAppend(state, shaderVariantValueKind(value));
    std::visit(
        [&](const auto& typedValue) noexcept {
            using ValueT = std::remove_cvref_t<decltype(typedValue)>;
            if constexpr (std::same_as<ValueT, std::string>)
            {
                nr::hash::hashAppendString(state, typedValue);
            }
            else if constexpr (std::same_as<ValueT, float>)
            {
                auto const bits = std::bit_cast<std::uint32_t>(typedValue);
                nr::hash::hashAppend(state, bits);
            }
            else
            {
                nr::hash::hashAppend(state, typedValue);
            }
        },
        value);
}

[[nodiscard]] const ShaderVariantValue& valueForItem(
    const ShaderVariantItemDesc& item,
    const ShaderVariantValueSet& values)
{
    auto valueIt = values.values.find(item.id);
    return valueIt != values.values.end() ? valueIt->second : item.defaultValue;
}
} // namespace

[[nodiscard]] ShaderVariantValueKind shaderVariantValueKind(const ShaderVariantValue& value) noexcept
{
    return std::visit(
        []<typename TValue>(const TValue&) constexpr noexcept {
            using ValueT = std::remove_cvref_t<TValue>;
            if constexpr (std::same_as<ValueT, bool>)
            {
                return ShaderVariantValueKind::Bool;
            }
            else if constexpr (std::same_as<ValueT, std::int32_t>)
            {
                return ShaderVariantValueKind::Int32;
            }
            else if constexpr (std::same_as<ValueT, std::uint32_t>)
            {
                return ShaderVariantValueKind::UInt32;
            }
            else if constexpr (std::same_as<ValueT, float>)
            {
                return ShaderVariantValueKind::Float32;
            }
            else
            {
                return ShaderVariantValueKind::String;
            }
        },
        value);
}

[[nodiscard]] bool shaderVariantValueMatchesKind(
    const ShaderVariantValue& value,
    ShaderVariantValueKind kind) noexcept
{
    return shaderVariantValueKind(value) == kind;
}

[[nodiscard]] std::optional<ShaderVariantValue> normalizeShaderVariantValue(
    const ShaderVariantItemDesc& desc,
    const ShaderVariantValue& value)
{
    switch (desc.kind)
    {
    case ShaderVariantValueKind::Bool:
        return normalizeBoolValue(value);
    case ShaderVariantValueKind::Int32:
        return normalizeInt32Value(desc.numericRange, value);
    case ShaderVariantValueKind::UInt32:
        return normalizeUInt32Value(desc.numericRange, value);
    case ShaderVariantValueKind::Float32:
        return normalizeFloat32Value(desc.numericRange, value);
    case ShaderVariantValueKind::String:
        return normalizeStringValue(desc, value);
    }

    return {};
}

[[nodiscard]] std::uint64_t hashShaderVariantValue(const ShaderVariantValue& value) noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "ShaderVariantValue.v1");
    hashAppendShaderVariantValue(state, value);
    return state;
}

[[nodiscard]] std::string shaderVariantValueToString(const ShaderVariantValue& value)
{
    return std::visit(
        [](const auto& typedValue) {
            using ValueT = std::remove_cvref_t<decltype(typedValue)>;
            if constexpr (std::same_as<ValueT, bool>)
            {
                return typedValue ? std::string{"true"} : std::string{"false"};
            }
            else if constexpr (std::same_as<ValueT, std::string>)
            {
                return typedValue;
            }
            else
            {
                return std::format("{}", typedValue);
            }
        },
        value);
}

[[nodiscard]] std::optional<SlangVariantConstant> shaderVariantValueToSlangConstant(
    const ShaderVariantValue& value,
    SlangVariantConstantType type) noexcept
{
    switch (type)
    {
    case SlangVariantConstantType::Bool:
        if (auto typedValue = shaderVariantGetAs<bool>(value); typedValue.has_value())
        {
            return SlangVariantConstant::fromBool(*typedValue);
        }
        return {};
    case SlangVariantConstantType::Int32:
        if (auto typedValue = shaderVariantGetAs<std::int32_t>(value); typedValue.has_value())
        {
            return SlangVariantConstant::fromInt32(*typedValue);
        }
        return {};
    case SlangVariantConstantType::UInt32:
        if (auto typedValue = shaderVariantGetAs<std::uint32_t>(value); typedValue.has_value())
        {
            return SlangVariantConstant::fromUInt32(*typedValue);
        }
        return {};
    case SlangVariantConstantType::Float32:
        if (auto typedValue = shaderVariantGetAs<float>(value); typedValue.has_value())
        {
            return SlangVariantConstant::fromFloat32(*typedValue);
        }
        return {};
    }

    return {};
}

[[nodiscard]] std::uint64_t ShaderVariantValueSet::hashValue() const noexcept
{
    auto state = nr::hash::fnv1a64OffsetBasis;
    nr::hash::hashAppendString(state, "ShaderVariantValueSet.v1");
    nr::hash::hashAppend(state, static_cast<std::uint32_t>(values.size()));
    std::ranges::for_each(values, [&](const auto& entry) noexcept {
        nr::hash::hashAppendString(state, entry.first);
        hashAppendShaderVariantValue(state, entry.second);
    });
    return state;
}

[[nodiscard]] std::string ShaderVariantValueSet::hashHex() const
{
    auto hashChars = nr::hash::toHexChars(hashValue());
    return std::string(nr::hash::toHexView(hashChars));
}

[[nodiscard]] SlangProgramVariantDesc makeSlangProgramVariantDesc(
    std::string_view debugName,
    std::span<const ShaderVariantItemDesc> items,
    const ShaderVariantValueSet& values)
{
    auto variant = SlangProgramVariantDesc{};
    variant.debugName = std::string{debugName};

    std::ranges::for_each(items, [&](const ShaderVariantItemDesc& item) {
        auto normalized = normalizeShaderVariantValue(item, valueForItem(item, values));
        nrAssert(
            normalized.has_value(),
            std::format("Shader variant item '{}' has an invalid value for Slang variant synthesis.", item.id));

        switch (item.slangBinding.kind)
        {
        case ShaderVariantSlangBindingKind::None:
            return;
        case ShaderVariantSlangBindingKind::Constant:
        {
            auto constant = shaderVariantValueToSlangConstant(*normalized, item.slangBinding.constant.type);
            nrAssert(
                constant.has_value(),
                std::format(
                    "Shader variant item '{}' cannot convert value '{}' to requested Slang constant type.",
                    item.id,
                    shaderVariantValueToString(*normalized)));
            variant.constants.try_emplace(item.slangBinding.constant.name, *constant);
            return;
        }
        case ShaderVariantSlangBindingKind::TypeAlias:
        {
            auto choiceKey = shaderVariantValueToString(*normalized);
            auto concreteIt = item.slangBinding.typeAlias.concreteTypeNameByChoice.find(choiceKey);
            nrAssert(
                concreteIt != item.slangBinding.typeAlias.concreteTypeNameByChoice.end(),
                std::format(
                    "Shader variant item '{}' has no Slang concrete type for string choice '{}'.",
                    item.id,
                    choiceKey));
            variant.typeAliases.try_emplace(
                item.slangBinding.typeAlias.exportedTypeName,
                SlangVariantTypeAlias{
                    .typeName = item.slangBinding.typeAlias.exportedTypeName,
                    .interfaceName = item.slangBinding.typeAlias.interfaceName,
                    .concreteTypeName = concreteIt->second,
                });
            return;
        }
        }
    });

    return variant;
}
} // namespace nr::rhi
