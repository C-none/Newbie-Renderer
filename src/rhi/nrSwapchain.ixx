export module nr.rhi:swapchain;
import dependency.math;
import dependency.vulkan;

import :surface;
import :queue;
import nr.utils;
import std;

export namespace nr::rhi
{
struct AcquireResult
{
    std::uint32_t imageIndex = 0;
    vk::Result result = vk::Result::eSuccess;
};

struct PresentResult
{
    vk::Result result = vk::Result::eSuccess;
};

struct SwapChainConfig
{
    std::uint32_t preferredImageCount = 3;
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eImmediate;
    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    vk::SurfaceTransformFlagBitsKHR surfaceTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
};

struct SwapChain
{
    vk::raii::SwapchainKHR swapChain = {nullptr};
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::raii::ImageView> imageViews;
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent = {0, 0};

    SwapChain() = default;
    SwapChain(const SwapChain&) = delete;
    SwapChain& operator=(const SwapChain&) = delete;
    SwapChain(SwapChain&&) = default;
    SwapChain& operator=(SwapChain&&) = default;

    [[nodiscard]] static SwapChain create(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        const vk::raii::SurfaceKHR& surface,
        vk::Extent2D surfaceExtent,
        const SwapChainConfig& config = {});

    [[nodiscard]] static SwapChain recreate(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        const vk::raii::SurfaceKHR& surface,
        vk::Extent2D surfaceExtent,
        vk::SwapchainKHR oldSwapchain,
        const SwapChainConfig& config = {});

    [[nodiscard]] AcquireResult acquireNextImage(
        const vk::raii::Semaphore& imageAvailable,
        std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max()) const;

    [[nodiscard]] PresentResult present(
        const vk::raii::Queue& presentQueue,
        std::uint32_t imageIndex,
        const vk::raii::Semaphore& waitSemaphore,
        std::optional<std::uint64_t> frameBoundaryFrameID) const;

  private:
    [[nodiscard]] static SwapChain createImpl(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        const vk::raii::SurfaceKHR& surface,
        vk::Extent2D surfaceExtent,
        const SwapChainConfig& config,
        vk::SwapchainKHR oldSwapchain);
};

class AcquireSemaphorePool
{
  public:
    void initialize(const vk::raii::Device& device, std::uint32_t capacity);
    [[nodiscard]] std::uint32_t borrow();
    void returnSlot(std::uint32_t slot);
    [[nodiscard]] const vk::raii::Semaphore& semaphore(std::uint32_t slot) const;
    [[nodiscard]] bool empty() const noexcept;

  private:
    std::vector<vk::raii::Semaphore> semaphores_;
    std::vector<std::uint32_t> freeSlots_;
};

struct PendingAcquire
{
    std::uint32_t semaphoreSlot = 0;
    std::uint32_t imageIndex = 0;
};

class PresentationContext
{
  public:
    void initialize(
        const vk::raii::Instance& instance,
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        std::string_view appName,
        const SwapChainConfig& config,
        std::uint32_t presentQueueFamily);

    void issueFirstAcquire(std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max());
    void issueNextAcquire(std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max());
    [[nodiscard]] AcquireResult consumePendingAcquire(std::uint32_t frameSlot);
    void returnAcquireSemaphore(std::uint32_t frameSlot);
    [[nodiscard]] const vk::raii::Semaphore& borrowedAcquireSemaphore(std::uint32_t frameSlot) const;
    [[nodiscard]] bool hasPendingAcquire() const noexcept;

    [[nodiscard]] PresentResult present(
        const QueueManager& queueManager,
        const vk::raii::Semaphore& waitSemaphore,
        std::optional<std::uint64_t> frameBoundaryFrameID) const;

    void rebuildAcquirePool();

    [[nodiscard]] vk::Extent2D swapchainExtent() const noexcept;
    [[nodiscard]] vk::Format swapchainFormat() const noexcept;
    [[nodiscard]] std::uint32_t swapchainImageCount() const noexcept;
    [[nodiscard]] vk::Image swapchainImage(std::uint32_t imageIndex) const;
    [[nodiscard]] vk::ImageView swapchainImageView(std::uint32_t imageIndex) const;

    void pollEvents() const;
    [[nodiscard]] bool keyDown(int glfwKeyCode) const;
    [[nodiscard]] bool mouseButtonDown(int glfwMouseButton) const;
    [[nodiscard]] glm::dvec2 cursorPosition() const;
    [[nodiscard]] bool windowShouldClose() const;

    void setActiveSwapchainImage(std::uint32_t imageIndex);
    void clearActiveSwapchainImage();
    [[nodiscard]] bool hasActiveSwapchainImage() const;
    [[nodiscard]] std::uint32_t activeSwapchainImageIndex() const;

    void setFrameSubmitted(bool submitted);
    [[nodiscard]] bool hasSubmittedCurrentFrame() const;

    [[nodiscard]] static bool needsSwapchainRecreate(vk::Result result);

    void recreate(
        const vk::raii::PhysicalDevice& physicalDevice,
        const vk::raii::Device& device,
        QueueManager& queueManager);

  private:
    void ensurePresentSupport(const vk::raii::PhysicalDevice& physicalDevice) const;
    void issuePendingAcquireImpl(std::uint64_t timeout);

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    Surface surface_;
    SwapChain swapChain_;
    SwapChainConfig config_{};
    std::uint32_t presentQueueFamily_ = 0;
    std::optional<std::uint32_t> activeSwapchainImageIndex_{};
    bool hasSubmittedCurrentFrame_ = false;

    AcquireSemaphorePool acquirePool_{};
    std::optional<PendingAcquire> pendingAcquire_{};
    std::array<std::optional<std::uint32_t>, maxFrameInFlight> borrowedAcquireSlotByFrame_{};
};
} // namespace nr::rhi
