export module nr.test;

import std;

namespace nr::test::detail {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

struct TestResult {
    std::string name;
    bool passed;
    int assertions;
    std::string detail;
};

inline auto registryStorage() -> std::vector<TestCase>&
{
    static std::vector<TestCase> storage;
    return storage;
}

inline auto assertionCounterPtr() -> int*&
{
    static thread_local int* counter = nullptr;
    return counter;
}

inline void noteAssertion()
{
    auto* counter = assertionCounterPtr();
    if (counter != nullptr) {
        ++(*counter);
    }
}

[[noreturn]] inline void throwFailure(
    std::string_view message,
    const std::source_location& location = std::source_location::current())
{
    std::ostringstream stream;
    stream << location.file_name() << ':' << location.line() << "\n"
           << "  detail    : " << message;
    throw std::runtime_error(stream.str());
}

inline auto runOne(const TestCase& testCase) -> TestResult
{
    int localAssertions = 0;
    assertionCounterPtr() = &localAssertions;

    try {
        testCase.body();
        assertionCounterPtr() = nullptr;
        return TestResult{.name = testCase.name, .passed = true, .assertions = localAssertions, .detail = {}};
    } catch (const std::exception& exception) {
        assertionCounterPtr() = nullptr;
        return TestResult{.name = testCase.name, .passed = false, .assertions = localAssertions, .detail = exception.what()};
    } catch (...) {
        assertionCounterPtr() = nullptr;
        return TestResult{.name = testCase.name, .passed = false, .assertions = localAssertions, .detail = "unknown exception"};
    }
}

} // namespace nr::test::detail

export namespace nr::test {

inline constexpr std::uint32_t kOutputLevelConcise = 0u;
inline constexpr std::uint32_t kOutputLevelNormal = 1u;
inline constexpr std::uint32_t kOutputLevelDetailed = 2u;
inline constexpr std::uint32_t kOutputLevelFlag = NR_TEST_OUTPUT_LEVEL_FLAG;

class CaseRegistrar {
public:
    CaseRegistrar(std::string name, std::function<void()> body)
    {
        detail::registryStorage().push_back(detail::TestCase{.name = std::move(name), .body = std::move(body)});
    }
};

inline void require(bool condition,
                    std::string_view message = "require failed",
                    const std::source_location& location = std::source_location::current())
{
    detail::noteAssertion();
    if (!condition) {
        detail::throwFailure(message, location);
    }
}

template <typename L, typename R>
    requires requires(const L& lhs, const R& rhs) {
        { lhs == rhs } -> std::convertible_to<bool>;
    }
inline void requireEqual(const L& lhs,
                         const R& rhs,
                         std::string_view message = "expected lhs == rhs",
                         const std::source_location& location = std::source_location::current())
{
    require(lhs == rhs, message, location);
}

inline int runAll(std::ostream& output = std::cout)
{
    const auto& testCases = detail::registryStorage();

    output << "[nr_test] running tests with output level " << kOutputLevelFlag << "\n";

    if (testCases.empty()) {
        output << "[nr_test] no tests registered\n";
        return 0;
    }

    std::vector<detail::TestResult> results;
    results.reserve(testCases.size());

    std::ranges::for_each(testCases, [&](const detail::TestCase& testCase) {
        if (kOutputLevelFlag >= kOutputLevelDetailed) {
            output << "[RUN ] " << testCase.name << "\n";
        }
        results.push_back(detail::runOne(testCase));
    });

    if (kOutputLevelFlag >= kOutputLevelDetailed) {
        output << "[nr_test] running " << testCases.size() << " test cases\n";
    }

    std::ranges::for_each(results, [&](const detail::TestResult& result) {
        if (kOutputLevelFlag == kOutputLevelConcise && result.passed) {
            return;
        }

        if (kOutputLevelFlag >= kOutputLevelDetailed) {
            output << "[CASE] " << result.name << "\n";
            output << "  assertions: " << result.assertions << "\n";
            output << "  result    : " << (result.passed ? "PASS" : "FAIL") << "\n";
            if (!result.passed) {
                output << result.detail << "\n";
            }
            return;
        }

        if (result.passed) {
            output << "[PASS] " << result.name << " (" << result.assertions << " assertions)\n";
            return;
        }

        output << "[FAIL] " << result.name << "\n"
               << result.detail << "\n";
    });

    const auto passedCount = std::ranges::count_if(results, [](const detail::TestResult& result) {
        return result.passed;
    });
    const auto failedCount = static_cast<int>(results.size()) - static_cast<int>(passedCount);
    const auto assertionCount = std::transform_reduce(
        results.begin(),
        results.end(),
        0,
        std::plus{},
        [](const detail::TestResult& result) { return result.assertions; });

    output << "[nr_test] passed=" << passedCount
           << ", failed=" << failedCount
           << ", assertions=" << assertionCount << '\n';

    return failedCount == 0 ? 0 : 1;
}

} // namespace nr::test
