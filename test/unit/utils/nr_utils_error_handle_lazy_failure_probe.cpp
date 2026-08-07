import std;
import nr.utils;

namespace
{
struct DeferredFormatProbe
{
    static inline std::uint32_t formatInvocations = 0u;
};
} // namespace

template <> struct std::formatter<DeferredFormatProbe> : std::formatter<std::uint32_t>
{
    auto format(const DeferredFormatProbe &, std::format_context &context) const
    {
        ++DeferredFormatProbe::formatInvocations;
        return std::formatter<std::uint32_t>::format(DeferredFormatProbe::formatInvocations, context);
    }
};

int main()
{
    nr::nrAssert(false, "deferred assertion failure probe invocation={}", DeferredFormatProbe{});
    return 0;
}
