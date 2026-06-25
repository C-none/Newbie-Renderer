module nr.rhi;
import :frameContext;
import dependency.vulkan;
import nr.utils;
import std;
import :commandPool;
import :type;

namespace nr::rhi
{
FrameContext::FrameContext(const vk::raii::Device &device, const PoolConfig &graphicsConfig, const PoolConfig &computeConfig, const PoolConfig &transferConfig)
        : device_(std::ref(device)), graphicsQueueFamily_(graphicsConfig.queueFamilyIndex), computeQueueFamily_(computeConfig.queueFamilyIndex), transferQueueFamily_(transferConfig.queueFamilyIndex)
{
        // Create fence (signaled initially so first frame doesn't wait)
        vk::FenceCreateInfo fenceInfo{vk::FenceCreateFlagBits::eSignaled};
        fence_ = vk::raii::Fence(device, fenceInfo);

        // renderFinished semaphore is owned here; imageAvailable is borrowed from
        // the PresentationContext acquire pool and injected at frame-begin.
        vk::SemaphoreCreateInfo semaphoreInfo{};
        renderFinished_ = vk::raii::Semaphore(device, semaphoreInfo);

        // Create primary pools (one per queue type, main thread only).
        // Primary command buffers are retained and individually reset every frame,
        // so pools must allow vkResetCommandBuffer.
        constexpr auto primaryPoolFlags = vk::CommandPoolCreateFlagBits::eTransient |
                                          vk::CommandPoolCreateFlagBits::eResetCommandBuffer;
        graphicsPrimary_ = CommandPool(device, graphicsConfig.queueFamilyIndex, primaryPoolFlags);

        computePrimary_ = CommandPool(device, computeConfig.queueFamilyIndex, primaryPoolFlags);

        transferPrimary_ = CommandPool(device, transferConfig.queueFamilyIndex, primaryPoolFlags);
    }

FrameContext::FrameContext(FrameContext &&other) noexcept
{
        moveFrom(std::move(other));
    }

FrameContext &FrameContext::operator=(FrameContext &&other) noexcept
{
        if (this != &other)
        {
            moveFrom(std::move(other));
        }
        return *this;
    }

void FrameContext::prepareSecondaryPools(std::uint32_t graphicsWorkerCount, std::uint32_t computeWorkerCount, std::uint32_t transferWorkerCount)
{
        graphicsPreparedSecondaryWorkers_ = std::min(graphicsWorkerCount, kMaxSecondaryWorkers);
        computePreparedSecondaryWorkers_ = std::min(computeWorkerCount, kMaxSecondaryWorkers);

        prepareQueueSecondaryPools(graphicsSecondary_, graphicsQueueFamily_, graphicsPreparedSecondaryWorkers_);
        prepareQueueSecondaryPools(computeSecondary_, computeQueueFamily_, computePreparedSecondaryWorkers_);

        transferPreparedSecondaryWorkers_ = std::min(transferWorkerCount, kMaxSecondaryWorkers);
        if (transferPreparedSecondaryWorkers_ > 0)
        {
            prepareQueueSecondaryPools(transferSecondary_, transferQueueFamily_, transferPreparedSecondaryWorkers_);
        }
    }

[[nodiscard]] const vk::raii::Fence &FrameContext::fence() const noexcept
{
        return fence_;
    }

void FrameContext::setBorrowedAcquireSemaphore(const vk::raii::Semaphore *semaphore) noexcept
{
        borrowedAcquireSemaphore_ = semaphore;
    }

[[nodiscard]] const vk::raii::Semaphore &FrameContext::imageAvailable() const noexcept
{
        nrAssert(borrowedAcquireSemaphore_ != nullptr, "FrameContext::imageAvailable requires a borrowed acquire semaphore (call setBorrowedAcquireSemaphore first).");
        return *borrowedAcquireSemaphore_;
    }

[[nodiscard]] const vk::raii::Semaphore &FrameContext::renderFinished() const noexcept
{
        return renderFinished_;
    }

[[nodiscard]] bool FrameContext::isFenceSignaled() const
{
        return fence_.getStatus() == vk::Result::eSuccess;
    }

void FrameContext::moveFrom(FrameContext &&other) noexcept
{
        device_ = std::move(other.device_);
        graphicsQueueFamily_ = other.graphicsQueueFamily_;
        computeQueueFamily_ = other.computeQueueFamily_;
        transferQueueFamily_ = other.transferQueueFamily_;

        fence_ = std::move(other.fence_);
        borrowedAcquireSemaphore_ = other.borrowedAcquireSemaphore_;
        other.borrowedAcquireSemaphore_ = nullptr;
        renderFinished_ = std::move(other.renderFinished_);

        graphicsPrimary_ = std::move(other.graphicsPrimary_);
        graphicsSecondary_ = std::move(other.graphicsSecondary_);

        computePrimary_ = std::move(other.computePrimary_);
        computeSecondary_ = std::move(other.computeSecondary_);

        transferPrimary_ = std::move(other.transferPrimary_);
        transferSecondary_ = std::move(other.transferSecondary_);

        graphicsPreparedSecondaryWorkers_ = other.graphicsPreparedSecondaryWorkers_;
        computePreparedSecondaryWorkers_ = other.computePreparedSecondaryWorkers_;
        transferPreparedSecondaryWorkers_ = other.transferPreparedSecondaryWorkers_;
    }

void FrameContext::resetPreparedSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots, std::uint32_t preparedWorkerCount)
{
        std::ranges::for_each(slots | std::views::take(preparedWorkerCount), [](std::optional<CommandPool> &pool) {
            if (pool.has_value())
                pool->reset();
        });
    }

void FrameContext::prepareQueueSecondaryPools(std::array<std::optional<CommandPool>, kMaxSecondaryWorkers> &slots, std::uint32_t queueFamilyIndex, std::uint32_t workerCount)
{
        auto slotIndices = std::views::iota(std::uint32_t{0}, workerCount);
        std::ranges::for_each(slotIndices, [&](std::uint32_t slotIndex) {
            if (!slots[slotIndex].has_value())
            {
                slots[slotIndex].emplace(
                    device_->get(),
                    queueFamilyIndex,
                    vk::CommandPoolCreateFlagBits::eTransient | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
            }
        });
    }
FrameManager::FrameManager(const vk::raii::Device &device, const FrameContext::PoolConfig &graphicsConfig, const FrameContext::PoolConfig &computeConfig, const FrameContext::PoolConfig &transferConfig)
{
        frames_.reserve(maxFrameInFlight);
        auto frameIndices = std::views::iota(std::uint32_t{0}, maxFrameInFlight);
        std::ranges::for_each(frameIndices, [&](std::uint32_t) {
            frames_.emplace_back(device, graphicsConfig, computeConfig, transferConfig);
        });
    }

void FrameManager::advanceFrame() noexcept
{
        currentIndex_ = (currentIndex_ + 1) % frames_.size();
    }

[[nodiscard]] FrameContext &FrameManager::current() noexcept
{
        return frames_[currentIndex_];
    }
} // namespace nr::rhi
