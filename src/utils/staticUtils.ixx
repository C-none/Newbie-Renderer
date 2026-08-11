export module nr.utils:staticUtils;
import std;

namespace nr::detail
{
template <auto>
struct MemberOffset;

template <typename Owner, typename Member, Member Owner::*MemberPointer>
struct MemberOffset<MemberPointer>
{
    [[nodiscard]] static consteval std::size_t value() noexcept
    {
        static_assert(std::is_standard_layout_v<Owner>, "memberOffset requires a standard-layout owner type");
        static_assert(std::is_default_constructible_v<Owner>, "memberOffset requires a default-constructible owner");

        // Standard C++ cannot convert a generic member pointer to a byte offset in constant evaluation.
        // Mode zero reports the bytes remaining in the complete object without relying on member-pointer ABI layout.
        constexpr auto owner = Owner{};
        constexpr auto remainingBytes = __builtin_object_size(std::addressof(owner.*MemberPointer), 0);
        static_assert(remainingBytes <= sizeof(Owner),
                      "memberOffset requires compiler-known complete-object bounds");
        return sizeof(Owner) - remainingBytes;
    }
};
} // namespace nr::detail

export namespace nr
{
template <auto MemberPointer>
    requires std::is_member_object_pointer_v<decltype(MemberPointer)>
[[nodiscard]] consteval std::size_t memberOffset() noexcept
{
    return detail::MemberOffset<MemberPointer>::value();
}
} // namespace nr
