export module nr.renderer:rendererSubmission;
import dependency.vulkan;

import nr.rhi;
import nr.utils;
import std;
import :rendererType;

export namespace nr::renderer
{
struct RendererSubmitToken
{
    QueueDomain queue = QueueDomain::Graphics;
    std::uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return value > 0;
    }

    [[nodiscard]] bool operator==(const RendererSubmitToken &) const = default;
};

class RendererSubmissionTimelines
{
  public:
    void initialize(const vk::raii::Device &device, std::uint64_t initialValue = 0)
    {
        std::ranges::for_each(timelines_, [&](QueueTimeline &timeline) {
            timeline.semaphore = nr::rhi::sync::createTimelineSemaphore(device, initialValue);
            timeline.nextSignalValue = initialValue + 1;
        });
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return std::ranges::all_of(timelines_,
                                   [](const QueueTimeline &timeline) { return *timeline.semaphore != nullptr; });
    }

    [[nodiscard]] bool valid(QueueDomain queue) const noexcept
    {
        auto index = queueIndex(queue);
        return index < timelines_.size() && *timelines_[index].semaphore != nullptr;
    }

    [[nodiscard]] RendererSubmitToken acquireSignalToken(QueueDomain queue)
    {
        nrAssert(valid(queue),
                 "RendererSubmissionTimelines::acquireSignalToken requires an initialized queue timeline.");
        auto &timeline = timelineFor(queue);
        auto value = timeline.nextSignalValue;
        ++timeline.nextSignalValue;
        return RendererSubmitToken{
            .queue = queue,
            .value = value,
        };
    }

    [[nodiscard]] std::uint64_t peekNextSignalValue(QueueDomain queue) const
    {
        return timelineFor(queue).nextSignalValue;
    }

    [[nodiscard]] const vk::raii::Semaphore &semaphore(QueueDomain queue) const
    {
        nrAssert(valid(queue), "RendererSubmissionTimelines::semaphore requires an initialized queue timeline.");
        return timelineFor(queue).semaphore;
    }

  private:
    struct QueueTimeline
    {
        vk::raii::Semaphore semaphore = {nullptr};
        std::uint64_t nextSignalValue = 1;
    };

    static constexpr std::size_t timelineCount = static_cast<std::size_t>(QueueDomain::Transfer) + 1u;

    [[nodiscard]] static constexpr std::size_t queueIndex(QueueDomain queue) noexcept
    {
        return static_cast<std::size_t>(queue);
    }

    [[nodiscard]] QueueTimeline &timelineFor(QueueDomain queue)
    {
        auto index = queueIndex(queue);
        nrAssert(index < timelines_.size(), "RendererSubmissionTimelines received an unknown queue domain.");
        return timelines_[index];
    }

    [[nodiscard]] const QueueTimeline &timelineFor(QueueDomain queue) const
    {
        auto index = queueIndex(queue);
        nrAssert(index < timelines_.size(), "RendererSubmissionTimelines received an unknown queue domain.");
        return timelines_[index];
    }

    std::array<QueueTimeline, timelineCount> timelines_{};
};
} // namespace nr::renderer
