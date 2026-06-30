export module nr.utils:threading;
import :errorHandle;
import :staticUtils;
import std;

export namespace nr::threading
{
inline constexpr std::uint32_t invalidWorkerIndex = std::numeric_limits<std::uint32_t>::max();

struct WorkRange
{
    std::size_t begin = 0;
    std::size_t end = 0;

    [[nodiscard]] std::size_t size() const noexcept
    {
        return end - begin;
    }
};

struct WorkRangePlan
{
    std::size_t itemCount = 0;
    std::uint32_t assignedWorkerCount = 0;
    std::vector<WorkRange> ranges{};
};

[[nodiscard]] WorkRangePlan planContiguousRanges(
    std::size_t itemCount,
    std::uint32_t availableWorkers);

[[nodiscard]] std::uint32_t resolveWorkerCount(
    std::uint32_t requestedWorkers,
    std::size_t taskCount) noexcept;

[[nodiscard]] std::optional<std::uint32_t> currentWorkerIndex() noexcept;
} // namespace nr::threading

namespace nr::threading::detail
{
#if defined(__cpp_lib_move_only_function) && __cpp_lib_move_only_function >= 202110L
using TaskFunction = std::move_only_function<void()>;
#else
using TaskFunction = std::packaged_task<void()>;
#endif
} // namespace nr::threading::detail

export namespace nr::threading
{
class StaticThreadPool
{
  private:
    using Task = detail::TaskFunction;

    struct WorkerQueue
    {
        std::queue<Task> tasks{};
    };

  public:
    StaticThreadPool();

    StaticThreadPool(const StaticThreadPool&) = delete;
    StaticThreadPool& operator=(const StaticThreadPool&) = delete;

    ~StaticThreadPool();

    void ensureWorkerCount(std::uint32_t workerCount);

    [[nodiscard]] std::uint32_t workerCount() const noexcept;

    template <typename Fn>
    requires std::invocable<std::decay_t<Fn>&>
    [[nodiscard]] auto submit(Fn&& fn) -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>
    {
        using Function = std::decay_t<Fn>;
        using Result = std::invoke_result_t<Function&>;

        auto task = std::packaged_task<Result()>{std::forward<Fn>(fn)};
        auto future = task.get_future();
        enqueueShared(Task{[task = std::move(task)]() mutable {
            task();
        }});
        return future;
    }

    template <typename Fn>
    requires std::invocable<std::decay_t<Fn>&>
    [[nodiscard]] auto submitTo(
        std::uint32_t workerId,
        Fn&& fn) -> std::future<std::invoke_result_t<std::decay_t<Fn>&>>
    {
        using Function = std::decay_t<Fn>;
        using Result = std::invoke_result_t<Function&>;

        auto task = std::packaged_task<Result()>{std::forward<Fn>(fn)};
        auto future = task.get_future();
        enqueueTo(workerId, Task{[task = std::move(task)]() mutable {
            task();
        }});
        return future;
    }

    void waitIdle();

    void stop();

  private:
    void enqueueShared(Task task);

    void enqueueTo(std::uint32_t workerId, Task task);

    [[nodiscard]] bool hasPendingWorkFor(std::uint32_t workerId) const;

    [[nodiscard]] Task popTaskFor(std::uint32_t workerId);

    void workerLoop(std::uint32_t workerId, const std::stop_token& stopToken);

    std::array<WorkerQueue, nr::maxThreads> workerQueues_{};
    std::queue<Task> sharedTasks_{};
    std::vector<std::jthread> workers_{};
    mutable std::mutex mutex_{};
    std::condition_variable taskAvailable_{};
    std::condition_variable idle_{};
    std::atomic_uint32_t workerCount_{0};
    std::size_t queuedTaskCount_ = 0;
    std::size_t runningTaskCount_ = 0;
    bool acceptingTasks_ = true;
    bool stopping_ = false;
};
} // namespace nr::threading
