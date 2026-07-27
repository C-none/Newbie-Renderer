import std;
import nr.test;
import nr.utils;

namespace
{
const nr::test::CaseRegistrar lazyAssertSuccessCase{
    "utils error handle skips lazy assertion context on success",
    [] {
        auto invocationCount = std::uint32_t{0};
        nr::nrAssert(true, [&] {
            ++invocationCount;
            return std::format("unexpected lazy assertion invocation {}", invocationCount);
        });

        nr::test::requireEqual(
            invocationCount,
            std::uint32_t{0},
            "successful lazy assertions should not invoke their context factory");

        nr::nrAssert(true, "literal assertion context");
        auto context = std::string_view{"string_view assertion context"};
        nr::nrAssert(true, context);
    }};
} // namespace
