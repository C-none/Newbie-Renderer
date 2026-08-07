import nr.utils;
import std;

namespace
{
struct Counter
{
    static inline int formatterInvocations = 0;
};

struct Probe
{
    int value = 0;
};
} // namespace

template <> struct std::formatter<Probe> : std::formatter<int>
{
    auto format(const Probe &probe, std::format_context &context) const
    {
        ++Counter::formatterInvocations;
        return std::formatter<int>::format(probe.value, context);
    }
};

int main()
{
    nr::nrAssert(true);
    nr::nrAssert(true, "plain literal message");
    nr::nrAssert(true, "formatted {} and {}", 1, "two");

    nr::nrAssert(true, "should not format {}", Probe{7});
    if (Counter::formatterInvocations != 0)
    {
        std::println("FAIL: successful assertion formatted its arguments");
        return 1;
    }

    nr::nrLog<nr::LogLevel::info>("spike log without channel {}", 1);
    nr::nrLog<nr::LogLevel::warning, "SPIKE">("spike log with channel {}", 2);
    nr::nrVulkan<nr::LogLevel::warning>("spike vulkan {}", 3);

    std::println("SPIKE OK");
    return 0;
}
