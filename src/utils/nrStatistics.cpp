module nr.utils;
import :statistics;
import :staticUtilsConstants;
import std;

namespace nr::statistics
{
namespace detail
{
[[nodiscard]] std::atomic<std::uint32_t>& sampleFrameCountState() noexcept
{
    static std::atomic<std::uint32_t> state{nr::statisticsSampleFrameCount};
    return state;
}
} // namespace detail

std::uint32_t sampleFrameCountForFps(float framesPerSecond) noexcept
{
    auto const naturalMultiplier = framesPerSecond > 0.0f
        ? static_cast<std::uint32_t>(std::ceil(framesPerSecond / static_cast<float>(sampleFrameQuantum)))
        : 1u;
    return sampleFrameQuantum * std::max(1u, naturalMultiplier);
}

std::uint32_t sampleFrameCount() noexcept
{
    return detail::sampleFrameCountState().load(std::memory_order_relaxed);
}

void refreshSampleFrameCount(float framesPerSecond) noexcept
{
    detail::sampleFrameCountState().store(
        sampleFrameCountForFps(framesPerSecond),
        std::memory_order_relaxed);
}
} // namespace nr::statistics
