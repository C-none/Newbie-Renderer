module nr.utils;
import :threading;
import :errorHandle;
import :staticUtilsConstants;
import std;

namespace nr::threading::detail
{
struct WorkerIdentity
{
    const StaticThreadPool *pool = nullptr;
    std::uint32_t workerIndex = invalidWorkerIndex;
};

[[nodiscard]] WorkerIdentity &currentWorkerIdentity() noexcept
{
    static thread_local WorkerIdentity identity{};
    return identity;
}

class ScopedWorkerIdentity
{
  public:
    ScopedWorkerIdentity(const StaticThreadPool &pool, std::uint32_t workerIndex) noexcept
        : previous_{currentWorkerIdentity()}
    {
        currentWorkerIdentity() = WorkerIdentity{
            .pool = std::addressof(pool),
            .workerIndex = workerIndex,
        };
    }

    ScopedWorkerIdentity(const ScopedWorkerIdentity &) = delete;
    ScopedWorkerIdentity &operator=(const ScopedWorkerIdentity &) = delete;

    ~ScopedWorkerIdentity()
    {
        currentWorkerIdentity() = previous_;
    }

  private:
    WorkerIdentity previous_{};
};
} // namespace nr::threading::detail

namespace nr::threading
{
[[nodiscard]] WorkRangePlan planContiguousRanges(std::size_t itemCount, std::uint32_t availableWorkers)
{
    auto plan = WorkRangePlan{
        .itemCount = itemCount,
    };
    if (itemCount == 0 || availableWorkers == 0)
    {
        return plan;
    }

    auto const assignedWorkerCount = std::min<std::size_t>(availableWorkers, itemCount);
    plan.assignedWorkerCount = static_cast<std::uint32_t>(assignedWorkerCount);

    auto const baseRangeSize = itemCount / assignedWorkerCount;
    auto const remainder = itemCount % assignedWorkerCount;
    auto rangeIndices = std::views::iota(std::size_t{0}, assignedWorkerCount);
    plan.ranges = rangeIndices | std::views::transform([&](std::size_t rangeIndex) {
                      auto const begin = rangeIndex * baseRangeSize + std::min(rangeIndex, remainder);
                      auto const rangeSize = baseRangeSize + (rangeIndex < remainder ? 1u : 0u);
                      return WorkRange{
                          .begin = begin,
                          .end = begin + rangeSize,
                      };
                  }) |
                  std::ranges::to<std::vector>();
    return plan;
}

[[nodiscard]] std::uint32_t resolveWorkerCount(std::uint32_t requestedWorkers, std::size_t taskCount) noexcept
{
    if (taskCount == 0)
    {
        return 0;
    }

    auto const hardwareWorkers = std::max(1u, std::thread::hardware_concurrency());
    auto const normalizedRequested = requestedWorkers == 0 ? hardwareWorkers : requestedWorkers;
    auto const cappedWorkers = std::min<std::uint32_t>(std::max(normalizedRequested, 1u), nr::maxThreads);
    auto const clampedToTaskCount = std::min<std::uint64_t>(cappedWorkers, taskCount);
    return static_cast<std::uint32_t>(std::max<std::uint64_t>(1, clampedToTaskCount));
}

[[nodiscard]] std::optional<std::uint32_t> currentWorkerIndex() noexcept
{
    auto const identity = detail::currentWorkerIdentity();
    if (identity.pool == nullptr)
    {
        return std::nullopt;
    }

    return identity.workerIndex;
}

StaticThreadPool::StaticThreadPool()
{
    workers_.reserve(nr::maxThreads);
}

StaticThreadPool::~StaticThreadPool()
{
    stop();
}

void StaticThreadPool::ensureWorkerCount(std::uint32_t workerCount)
{
    auto const targetWorkerCount = std::min<std::uint32_t>(std::max(workerCount, 1u), nr::maxThreads);

    std::scoped_lock lock(mutex_);
    nrAssert(acceptingTasks_ && !stopping_, "StaticThreadPool::ensureWorkerCount cannot grow a stopped pool.");

    auto const currentCount = static_cast<std::uint32_t>(workers_.size());
    if (targetWorkerCount <= currentCount)
    {
        return;
    }
    auto workerIds = std::views::iota(currentCount, targetWorkerCount);
    std::ranges::for_each(workerIds, [&](std::uint32_t workerId) {
        workers_.emplace_back([this, workerId](std::stop_token stopToken) { workerLoop(workerId, stopToken); });
        workerCount_.store(static_cast<std::uint32_t>(workers_.size()));
    });
}

[[nodiscard]] std::uint32_t StaticThreadPool::workerCount() const noexcept
{
    return workerCount_.load();
}

void StaticThreadPool::waitIdle()
{
    auto const identity = detail::currentWorkerIdentity();
    nrAssert(identity.pool != this,
             "StaticThreadPool::waitIdle cannot be called from one of the same pool's worker threads.");

    std::unique_lock lock(mutex_);
    idle_.wait(lock, [&]() { return queuedTaskCount_ == 0 && runningTaskCount_ == 0; });
}

void StaticThreadPool::stop()
{
    auto const identity = detail::currentWorkerIdentity();
    nrAssert(identity.pool != this,
             "StaticThreadPool::stop cannot be called from one of the same pool's worker threads.");

    {
        std::scoped_lock lock(mutex_);
        if (stopping_)
        {
            return;
        }

        acceptingTasks_ = false;
        stopping_ = true;
    }

    taskAvailable_.notify_all();
    workers_.clear();
    workerCount_.store(0);
}

void StaticThreadPool::enqueueShared(Task task)
{
    {
        std::scoped_lock lock(mutex_);
        nrAssert(workerCount() > 0, "StaticThreadPool::submit requires at least one worker.");
        nrAssert(acceptingTasks_ && !stopping_, "StaticThreadPool::submit cannot accept tasks after stop.");

        sharedTasks_.push(std::move(task));
        ++queuedTaskCount_;
    }

    taskAvailable_.notify_one();
}

void StaticThreadPool::enqueueTo(std::uint32_t workerId, Task task)
{
    {
        std::scoped_lock lock(mutex_);
        nrAssert(workerId < workerCount(), "StaticThreadPool::submitTo workerId is out of range.");
        nrAssert(acceptingTasks_ && !stopping_, "StaticThreadPool::submitTo cannot accept tasks after stop.");

        workerQueues_[workerId].tasks.push(std::move(task));
        ++queuedTaskCount_;
    }

    taskAvailable_.notify_all();
}

[[nodiscard]] bool StaticThreadPool::hasPendingWorkFor(std::uint32_t workerId) const
{
    return !workerQueues_[workerId].tasks.empty() || !sharedTasks_.empty();
}

[[nodiscard]] StaticThreadPool::Task StaticThreadPool::popTaskFor(std::uint32_t workerId)
{
    auto &localQueue = workerQueues_[workerId].tasks;
    auto &queue = localQueue.empty() ? sharedTasks_ : localQueue;

    auto task = std::move(queue.front());
    queue.pop();
    --queuedTaskCount_;
    ++runningTaskCount_;
    return task;
}

void StaticThreadPool::workerLoop(std::uint32_t workerId, const std::stop_token &stopToken)
{
    while (true)
    {
        auto task = Task{};
        {
            std::unique_lock lock(mutex_);
            taskAvailable_.wait(
                lock, [&]() { return stopping_ || stopToken.stop_requested() || hasPendingWorkFor(workerId); });

            if ((stopping_ || stopToken.stop_requested()) && !hasPendingWorkFor(workerId))
            {
                return;
            }

            task = popTaskFor(workerId);
        }

        {
            auto identityScope = detail::ScopedWorkerIdentity{*this, workerId};
            task();
        }

        {
            std::scoped_lock lock(mutex_);
            nrAssert(runningTaskCount_ > 0, "StaticThreadPool worker task accounting underflow.");
            --runningTaskCount_;
            if (queuedTaskCount_ == 0 && runningTaskCount_ == 0)
            {
                idle_.notify_all();
            }
        }
    }
}
} // namespace nr::threading
