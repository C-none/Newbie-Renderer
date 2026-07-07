export module nr.rhi:shaderVariant;

import :slang;
import nr.utils;
import std;

export namespace nr::rhi
{
enum class ShaderVariantValueKind : std::uint8_t
{
    Bool,
    Int32,
    UInt32,
    Float32,
    String,
};

using ShaderVariantValue = std::variant<bool, std::int32_t, std::uint32_t, float, std::string>;

struct ShaderVariantNumericRange
{
    double minValue = 0.0;
    double maxValue = 0.0;
    double step = 1.0;
    bool bounded = false;
};

struct ShaderVariantStringChoice
{
    std::string value{};
    std::string label{};
};

enum class ShaderVariantSlangBindingKind : std::uint8_t
{
    None,
    Constant,
    TypeAlias,
};

struct ShaderVariantSlangConstantBinding
{
    std::string name{};
    SlangVariantConstantType type = SlangVariantConstantType::Bool;
};

struct ShaderVariantSlangTypeAliasBinding
{
    std::string exportedTypeName{};
    std::string interfaceName{};
    std::map<std::string, std::string> concreteTypeNameByChoice{};
};

struct ShaderVariantSlangBinding
{
    ShaderVariantSlangBindingKind kind = ShaderVariantSlangBindingKind::None;
    ShaderVariantSlangConstantBinding constant{};
    ShaderVariantSlangTypeAliasBinding typeAlias{};
};

struct ShaderVariantItemDesc
{
    std::string id{};
    std::string label{};
    ShaderVariantValueKind kind = ShaderVariantValueKind::Bool;
    ShaderVariantValue defaultValue = false;
    ShaderVariantNumericRange numericRange{};
    std::vector<ShaderVariantStringChoice> stringChoices{};
    ShaderVariantSlangBinding slangBinding{};
};

struct ShaderVariantValueSet
{
    std::map<std::string, ShaderVariantValue> values{};

    [[nodiscard]] bool empty() const noexcept
    {
        return values.empty();
    }

    [[nodiscard]] std::uint64_t hashValue() const noexcept;

    [[nodiscard]] std::string hashHex() const;
};

[[nodiscard]] ShaderVariantValueKind shaderVariantValueKind(const ShaderVariantValue& value) noexcept;

[[nodiscard]] bool shaderVariantValueMatchesKind(
    const ShaderVariantValue& value,
    ShaderVariantValueKind kind) noexcept;

[[nodiscard]] std::optional<ShaderVariantValue> normalizeShaderVariantValue(
    const ShaderVariantItemDesc& desc,
    const ShaderVariantValue& value);

[[nodiscard]] std::uint64_t hashShaderVariantValue(const ShaderVariantValue& value) noexcept;

[[nodiscard]] std::string shaderVariantValueToString(const ShaderVariantValue& value);

[[nodiscard]] std::optional<SlangVariantConstant> shaderVariantValueToSlangConstant(
    const ShaderVariantValue& value,
    SlangVariantConstantType type) noexcept;

[[nodiscard]] SlangProgramVariantDesc makeSlangProgramVariantDesc(
    std::string_view debugName,
    std::span<const ShaderVariantItemDesc> items,
    const ShaderVariantValueSet& values);

[[nodiscard]] inline std::optional<std::reference_wrapper<const ShaderVariantItemDesc>> findShaderVariantItem(
    std::span<const ShaderVariantItemDesc> items,
    std::string_view id) noexcept
{
    auto it = std::ranges::find_if(items, [id](const ShaderVariantItemDesc& item) {
        return item.id == id;
    });
    if (it == std::ranges::end(items))
    {
        return {};
    }
    return std::cref(*it);
}
} // namespace nr::rhi
