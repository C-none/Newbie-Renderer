export module nr.utils:statistics;
import std;

export namespace nr::statistics
{
// Runtime statistics refresh cadence is always reported as a multiple of this many frames.
inline constexpr std::uint32_t sampleFrameQuantum = 30u;

// Smallest sampleFrameQuantum * n frame span whose duration reaches at least one second
// at the supplied fps: quantum * n / fps >= 1  =>  n = ceil(fps / quantum), clamped to
// n >= 1. A non-positive fps falls back to a single quantum.
[[nodiscard]] std::uint32_t sampleFrameCountForFps(float framesPerSecond) noexcept;

// Current global statistics sample period in frames, shared across modules so the UI, CPU,
// and GPU statistics collectors observe the same cadence. Bootstraps to
// nr::statisticsSampleFrameCount and then tracks the most recently published fps.
[[nodiscard]] std::uint32_t sampleFrameCount() noexcept;

// Recomputes and publishes the global sample period from the latest measured fps.
void refreshSampleFrameCount(float framesPerSecond) noexcept;
} // namespace nr::statistics
