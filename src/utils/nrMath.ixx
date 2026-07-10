export module nr.utils:math;
import std;
export namespace nr
{

namespace hash
{
inline constexpr std::uint64_t fnv1a64OffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t fnv1a64Prime = 1099511628211ull;
inline constexpr std::uint64_t hashCombineMagic = 0x9e3779b97f4a7c15ull;

template <typename T>
concept TriviallyByteHashable = std::is_trivially_copyable_v<T>;

template <typename T>
concept StringViewLike = requires(const T &value) {
    { std::string_view(value) } -> std::same_as<std::string_view>;
};

[[nodiscard]] constexpr std::uint64_t fnv1a64(std::span<const std::byte> bytes) noexcept
{
    return std::ranges::fold_left(
        bytes,
        fnv1a64OffsetBasis,
        [](std::uint64_t state, std::byte byteValue) constexpr noexcept {
            auto next = state ^ static_cast<std::uint64_t>(std::to_integer<std::uint8_t>(byteValue));
            return next * fnv1a64Prime;
        });
}

[[nodiscard]] constexpr std::uint64_t combineHash(std::uint64_t state, std::uint64_t mixed) noexcept
{
    return state ^ (mixed + hashCombineMagic + (state << 6) + (state >> 2));
}

template <TriviallyByteHashable T>
constexpr void hashAppend(std::uint64_t &state, const T &value) noexcept
{
    auto rawBytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(value);
    state = combineHash(state, fnv1a64(std::span<const std::byte>{rawBytes.data(), rawBytes.size()}));
}

template <StringViewLike TString>
constexpr void hashAppendString(std::uint64_t &state, const TString &value) noexcept
{
    auto text = std::string_view(value);
    auto mixed = std::ranges::fold_left(
        text,
        fnv1a64OffsetBasis,
        [](std::uint64_t current, char ch) constexpr noexcept {
            auto byteValue = static_cast<std::uint8_t>(ch);
            auto next = current ^ static_cast<std::uint64_t>(byteValue);
            return next * fnv1a64Prime;
        });
    state = combineHash(state, mixed);
}

[[nodiscard]] constexpr std::array<char, 16> toHexChars(std::uint64_t value) noexcept
{
    constexpr auto hexDigits = std::string_view{"0123456789abcdef"};
    std::array<char, 16> output{};
    auto indices = std::views::iota(std::size_t{0}, output.size());
    std::ranges::for_each(indices, [&](std::size_t index) constexpr noexcept {
        auto shift = static_cast<unsigned>((output.size() - 1 - index) * 4);
        auto nibble = static_cast<std::uint8_t>((value >> shift) & 0x0full);
        output[index] = hexDigits[nibble];
    });
    return output;
}

[[nodiscard]] constexpr std::string_view toHexView(const std::array<char, 16> &value) noexcept
{
    return std::string_view{value.data(), value.size()};
}

[[nodiscard]] constexpr std::string toHexString(const std::array<char, 16> &value)
{
    return std::string(toHexView(value));
}

[[nodiscard]] constexpr std::string toHexString(std::uint64_t value)
{
    return toHexString(toHexChars(value));
}

template <typename T>
[[nodiscard]] consteval std::uint64_t hashValue(const T &value) noexcept
{
    std::uint64_t state = fnv1a64OffsetBasis;
    hashAppend(state, value);
    return state;
}
} // namespace hash

} // namespace nr
