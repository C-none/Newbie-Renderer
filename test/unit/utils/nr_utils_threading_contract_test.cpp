import std;
import nr.test;
import nr.utils;

namespace
{
void requireContiguousCoverage(const nr::threading::WorkRangePlan &plan, std::size_t itemCount)
{
    auto expectedBegin = std::size_t{0};
    std::ranges::for_each(plan.ranges, [&](const nr::threading::WorkRange &range) {
        nr::test::requireEqual(range.begin, expectedBegin, "work ranges should be contiguous");
        nr::test::require(range.end >= range.begin, "work ranges should be non-inverted");
        expectedBegin = range.end;
    });
    nr::test::requireEqual(expectedBegin, itemCount, "work ranges should cover the item domain exactly");
}

void requireBalanced(const nr::threading::WorkRangePlan &plan)
{
    if (plan.ranges.empty())
    {
        return;
    }

    auto sizes = plan.ranges |
                 std::views::transform([](const nr::threading::WorkRange &range) { return range.size(); }) |
                 std::ranges::to<std::vector>();
    auto const [minIt, maxIt] = std::ranges::minmax_element(sizes);
    nr::test::require(*maxIt - *minIt <= 1u, "work range sizes should differ by at most one item");
}

const nr::test::CaseRegistrar rangePlannerCase{
    "utils threading planner splits contiguous balanced ranges", [] {
        auto zero = nr::threading::planContiguousRanges(0, 4);
        nr::test::requireEqual(zero.itemCount, std::size_t{0});
        nr::test::requireEqual(zero.assignedWorkerCount, std::uint32_t{0});
        nr::test::require(zero.ranges.empty(), "zero-item plan should not create ranges");

        auto one = nr::threading::planContiguousRanges(1, 4);
        nr::test::requireEqual(one.assignedWorkerCount, std::uint32_t{1});
        requireContiguousCoverage(one, 1);
        requireBalanced(one);

        auto sixtyThree = nr::threading::planContiguousRanges(63, 4);
        nr::test::requireEqual(sixtyThree.assignedWorkerCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyThree, 63);
        requireBalanced(sixtyThree);

        auto sixtyFour = nr::threading::planContiguousRanges(64, 4);
        nr::test::requireEqual(sixtyFour.assignedWorkerCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyFour, 64);
        requireBalanced(sixtyFour);

        auto sixtyFive = nr::threading::planContiguousRanges(65, 4);
        nr::test::requireEqual(sixtyFive.assignedWorkerCount, std::uint32_t{4});
        requireContiguousCoverage(sixtyFive, 65);
        requireBalanced(sixtyFive);

        auto twenty = nr::threading::planContiguousRanges(20, 10);
        nr::test::requireEqual(twenty.assignedWorkerCount, std::uint32_t{10});
        nr::test::require(std::ranges::all_of(
            twenty.ranges, [](const nr::threading::WorkRange &range) { return range.size() == 2u; }));
        requireContiguousCoverage(twenty, 20);
        requireBalanced(twenty);

        auto large = nr::threading::planContiguousRanges(1000, 4);
        nr::test::requireEqual(large.assignedWorkerCount, std::uint32_t{4});
        requireContiguousCoverage(large, 1000);
        requireBalanced(large);
    }};

const nr::test::CaseRegistrar workerCountResolverCase{
    "utils worker count resolver enforces task and project limits", [] {
        nr::test::requireEqual(nr::threading::resolveWorkerCount(6u, 0u), 0u);
        nr::test::requireEqual(nr::threading::resolveWorkerCount(8u, 3u), 3u);
        nr::test::requireEqual(nr::threading::resolveWorkerCount(std::numeric_limits<std::uint32_t>::max(),
                                                                 std::numeric_limits<std::uint32_t>::max()),
                               nr::maxThreads);

        auto const automaticWorkers = nr::threading::resolveWorkerCount(0u, std::numeric_limits<std::uint32_t>::max());
        nr::test::require(automaticWorkers >= 1u);
        nr::test::require(automaticWorkers <= nr::maxThreads);
    }};

const nr::test::CaseRegistrar submitCase{
    "utils static thread pool executes shared queue tasks", [] {
        auto pool = nr::threading::StaticThreadPool{};
        pool.ensureWorkerCount(2);

        auto value = pool.submit([] { return 42; });
        nr::test::requireEqual(value.get(), 42);

        auto counter = std::atomic_uint32_t{0};
        auto taskIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{16});
        std::ranges::for_each(taskIndices, [&](std::uint32_t) {
            static_cast<void>(pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
        });

        pool.waitIdle();
        nr::test::requireEqual(counter.load(std::memory_order_relaxed), std::uint32_t{16});
    }};

const nr::test::CaseRegistrar concurrentGrowthCase{
    "utils static thread pool serializes concurrent worker growth", [] {
        auto pool = nr::threading::StaticThreadPool{};
        auto const smallerTarget = std::min(2u, nr::maxThreads);
        auto const largerTarget = std::min(4u, nr::maxThreads);

        auto smallerGrowth = std::async(std::launch::async, [&] { pool.ensureWorkerCount(smallerTarget); });
        auto largerGrowth = std::async(std::launch::async, [&] { pool.ensureWorkerCount(largerTarget); });
        smallerGrowth.get();
        largerGrowth.get();

        nr::test::requireEqual(pool.workerCount(), largerTarget);
    }};

const nr::test::CaseRegistrar submitToCase{
    "utils static thread pool executes directed tasks on requested workers", [] {
        auto pool = nr::threading::StaticThreadPool{};
        pool.ensureWorkerCount(3);

        auto workerIds = std::views::iota(std::uint32_t{0}, std::uint32_t{3});
        auto futures = std::vector<std::future<std::uint32_t>>{};
        std::ranges::for_each(workerIds, [&](std::uint32_t workerId) {
            futures.push_back(pool.submitTo(workerId, [] {
                auto workerIndex = nr::threading::currentWorkerIndex();
                nr::test::require(workerIndex.has_value(), "directed task should run on a pool worker");
                return *workerIndex;
            }));
        });

        auto expectedWorkerId = std::uint32_t{0};
        std::ranges::for_each(futures, [&](std::future<std::uint32_t> &future) {
            nr::test::requireEqual(future.get(), expectedWorkerId, "directed task should run on requested worker");
            ++expectedWorkerId;
        });
    }};

const nr::test::CaseRegistrar destructorDrainCase{
    "utils static thread pool drains queued tasks before destruction", [] {
        auto counter = std::atomic_uint32_t{0};
        {
            auto pool = nr::threading::StaticThreadPool{};
            pool.ensureWorkerCount(2);

            auto taskIndices = std::views::iota(std::uint32_t{0}, std::uint32_t{12});
            std::ranges::for_each(taskIndices, [&](std::uint32_t) {
                static_cast<void>(pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); }));
            });
        }

        nr::test::requireEqual(counter.load(std::memory_order_relaxed), std::uint32_t{12});
    }};
} // namespace
