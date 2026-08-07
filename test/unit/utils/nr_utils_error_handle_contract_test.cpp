import std;
import nr.test;
import nr.utils;

namespace
{
struct DeferredFormatProbe
{
    static inline std::uint32_t formatInvocations = 0u;

    std::uint32_t value = 0u;
};
} // namespace

template <> struct std::formatter<DeferredFormatProbe> : std::formatter<std::uint32_t>
{
    auto format(const DeferredFormatProbe &probe, std::format_context &context) const
    {
        ++DeferredFormatProbe::formatInvocations;
        return std::formatter<std::uint32_t>::format(probe.value, context);
    }
};

namespace
{
const nr::test::CaseRegistrar deferredAssertFormattingCase{
    "utils error handle defers assertion formatting on success", [] {
        DeferredFormatProbe::formatInvocations = 0u;

        nr::nrAssert(true, "unexpected assertion formatting {}", DeferredFormatProbe{7u});

        nr::test::requireEqual(DeferredFormatProbe::formatInvocations, std::uint32_t{0},
                               "successful assertions must not format their message arguments");

        nr::nrAssert(true);
        nr::nrAssert(true, "literal assertion context");
        nr::nrAssert(true, "assertion context with {} argument", 1);
    }};

const nr::test::CaseRegistrar deferredLogFormattingCase{
    "utils error handle defers log formatting below the active level", [] {
        DeferredFormatProbe::formatInvocations = 0u;

        // Levels below globalLogLevel are discarded at compile time, so their arguments stay unformatted.
        if constexpr (nr::globalLogLevel > nr::LogLevel::info)
        {
            nr::nrLog<nr::LogLevel::info>("filtered log {}", DeferredFormatProbe{3u});
            nr::test::requireEqual(DeferredFormatProbe::formatInvocations, std::uint32_t{0},
                                   "filtered log levels must not format their message arguments");
        }

        nr::nrLog<nr::LogLevel::warning, "UTILS-TEST">("emitted log {}", DeferredFormatProbe{5u});
        nr::test::requireEqual(DeferredFormatProbe::formatInvocations, std::uint32_t{1},
                               "emitted logs must format their message arguments exactly once");
    }};
} // namespace
