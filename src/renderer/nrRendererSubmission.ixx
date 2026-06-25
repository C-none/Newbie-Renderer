export module nr.renderer:rendererSubmission;
import dependency.vulkan;

import nr.rhi;
import nr.utils;
import std;

export namespace nr::renderer
{
struct RendererSubmitToken
{
    std::uint64_t value = 0;

    [[nodiscard]] bool valid() const noexcept
    {
        return value > 0;
    }
};

class RendererSubmissionTimeline
{
  public:
    void initialize(const vk::raii::Device& device, std::uint64_t initialValue = 0)
    {
        timeline_ = nr::rhi::sync::createTimelineSemaphore(device, initialValue);
        nextSignalValue_ = initialValue + 1;
    }

    [[nodiscard]] bool valid() const noexcept
    {
        return *timeline_ != nullptr;
    }

    [[nodiscard]] RendererSubmitToken acquireSignalToken()
    {
        nrAssert(valid(), "RendererSubmissionTimeline::acquireSignalToken requires initialize() first.");
        auto value = nextSignalValue_;
        ++nextSignalValue_;
        return RendererSubmitToken{.value = value};
    }

    [[nodiscard]] std::uint64_t peekNextSignalValue() const noexcept
    {
        return nextSignalValue_;
    }

    [[nodiscard]] const vk::raii::Semaphore& semaphore() const noexcept
    {
        return timeline_;
    }

  private:
    vk::raii::Semaphore timeline_ = {nullptr};
    std::uint64_t nextSignalValue_ = 1;
};
} // namespace nr::renderer
