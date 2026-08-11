import std;
import nr.test;
import nr.utils;

namespace
{
struct MemberOffsetProbe
{
    std::uint8_t first = 0u;
    std::uint32_t second = 0u;
    std::array<std::uint16_t, 3u> third{};
};

static_assert(std::is_standard_layout_v<MemberOffsetProbe>);
static_assert(nr::memberOffset<&MemberOffsetProbe::first>() == 0u);
static_assert(nr::memberOffset<&MemberOffsetProbe::second>() == 4u);
static_assert(nr::memberOffset<&MemberOffsetProbe::third>() == 8u);

const nr::test::CaseRegistrar memberOffsetCase{
    "utils member offset is a typed compile-time operation", [] {
        nr::test::requireEqual(nr::memberOffset<&MemberOffsetProbe::first>(), std::size_t{0u});
        nr::test::requireEqual(nr::memberOffset<&MemberOffsetProbe::second>(), std::size_t{4u});
        nr::test::requireEqual(nr::memberOffset<&MemberOffsetProbe::third>(), std::size_t{8u});
    }};
} // namespace
