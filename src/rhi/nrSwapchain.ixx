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
    bool queued = false;
};

struct SwapChainConfig
{
    std::uint32_t preferredImageCount = 3;
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eImmediate;
    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    vk::SurfaceTransformFlagBitsKHR surfaceTransform = vk::SurfaceTransformFlagBitsKHR::eIdentity;
    bool hdrMetadataEnabled = false;
    bool fullScreenExclusiveEnabled = false;
    bool fullScreenExclusiveApplicationControlled = false;
    std::uintptr_t fullScreenExclusiveMonitor = 0;
};

[[nodiscard]] bool isHdr10SwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept;
[[nodiscard]] bool isScRgbSwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept;
[[nodiscard]] bool isHdrSwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept;
[[nodiscard]] vk::SurfaceFormatKHR chooseSwapchainSurfaceFormat(std::span<const vk::SurfaceFormatKHR> formats);

struct SwapChain
{
    vk::raii::SwapchainKHR swapChain = {nullptr};
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::raii::ImageView> imageViews;
    vk::SurfaceFormatKHR surfaceFormat{};
    vk::Extent2D extent = {0, 0};

    SwapChain() = default;
    SwapChain(const SwapChain &) = delete;
    SwapChain &operator=(const SwapChain &) = delete;
    SwapChain(SwapChain &&) = default;
    SwapChain &operator=(SwapChain &&) = delete;

    [[nodiscard]] static SwapChain create(const vk::raii::PhysicalDevice &physicalDevice,
                                          const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface,
                                          vk::Extent2D surfaceExtent, const SwapChainConfig &config = {});

    [[nodiscard]] static SwapChain recreate(const vk::raii::PhysicalDevice &physicalDevice,
                                            const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface,
                                            vk::Extent2D surfaceExtent, vk::SwapchainKHR oldSwapchain,
                                            const SwapChainConfig &config = {});

    [[nodiscard]] AcquireResult acquireNextImage(
        const vk::raii::Semaphore &imageAvailable,
        std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max()) const;

    [[nodiscard]] PresentResult present(const vk::raii::Queue &presentQueue, std::uint32_t imageIndex,
                                        const vk::raii::Semaphore &waitSemaphore,
                                        const vk::raii::Fence &presentCompletionFence,
                                        std::optional<std::uint64_t> frameBoundaryFrameID) const;

    [[nodiscard]] static PresentResult resolvePresentResult(vk::Result queueResult,
                                                            vk::Result swapchainResult) noexcept;

  private:
    [[nodiscard]] static SwapChain createImpl(const vk::raii::PhysicalDevice &physicalDevice,
                                              const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface,
                                              vk::Extent2D surfaceExtent, const SwapChainConfig &config,
                                              vk::SwapchainKHR oldSwapchain);
};

class AcquireSemaphorePool
{
  public:
    void initialize(const vk::raii::Device &device, std::uint32_t capacity);
    [[nodiscard]] std::uint32_t borrow();
    void returnSlot(std::uint32_t slot);
    [[nodiscard]] const vk::raii::Semaphore &semaphore(std::uint32_t slot) const;

  private:
    std::vector<vk::raii::Semaphore> semaphores_;
    std::vector<std::uint32_t> freeSlots_;
};

class PresentationContext
{
  public:
    using AcquireOutOfDateTestHook = std::function<bool()>;

    ~PresentationContext();

    void createSurface(const vk::raii::Instance &instance, std::string_view appName);
    void initializeSwapchain(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device,
                             const SwapChainConfig &config, std::uint32_t presentQueueFamily);

    [[nodiscard]] const vk::raii::SurfaceKHR &surfaceHandle() const;

    [[nodiscard]] AcquireResult acquireNextImage(std::uint32_t frameSlot,
                                                 std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max());
    void setAcquireOutOfDateTestHook(AcquireOutOfDateTestHook hook);
    void clearAcquireOutOfDateTestHook() noexcept;
    void returnAcquireSemaphore(std::uint32_t frameSlot);
    [[nodiscard]] const vk::raii::Semaphore &borrowedAcquireSemaphore(std::uint32_t frameSlot) const;
    [[nodiscard]] const vk::raii::Semaphore &activePresentSemaphore() const;

    [[nodiscard]] PresentResult present(const QueueManager &queueManager,
                                        std::optional<std::uint64_t> frameBoundaryFrameID);

    void rebuildAcquirePool();

    [[nodiscard]] vk::Extent2D swapchainExtent() const noexcept;
    [[nodiscard]] vk::Format swapchainFormat() const noexcept;
    [[nodiscard]] vk::ColorSpaceKHR swapchainColorSpace() const noexcept;
    [[nodiscard]] std::uint32_t swapchainImageCount() const noexcept;
    [[nodiscard]] vk::Image swapchainImage(std::uint32_t imageIndex) const;
    [[nodiscard]] vk::ImageView swapchainImageView(std::uint32_t imageIndex) const;

    void pollEvents() const;
    [[nodiscard]] bool keyDown(int glfwKeyCode) const;
    [[nodiscard]] bool mouseButtonDown(int glfwMouseButton) const;
    [[nodiscard]] glm::dvec2 cursorPosition() const;
    [[nodiscard]] double consumeVerticalScrollOffset() const noexcept;
    [[nodiscard]] std::vector<std::uint32_t> consumeTextInputCodepoints() const;
    [[nodiscard]] bool windowShouldClose() const;
    [[nodiscard]] bool framebufferAvailable() const noexcept;
    [[nodiscard]] bool fullscreenEnabled() const noexcept;
    void setFullscreen(bool enabled);
    [[nodiscard]] bool consumeSwapchainRecreateRequest() noexcept;

    void setActiveSwapchainImage(std::uint32_t imageIndex);
    void clearActiveSwapchainImage();
    [[nodiscard]] bool hasActiveSwapchainImage() const;
    [[nodiscard]] std::uint32_t activeSwapchainImageIndex() const;

    void setFrameSubmitted(bool submitted);
    [[nodiscard]] bool hasSubmittedCurrentFrame() const;

    [[nodiscard]] static bool needsSwapchainRecreate(vk::Result result);

    void recreate(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device,
                  QueueManager &queueManager);

  private:
    struct PresentationGeneration
    {
        SwapChain swapChain;
        std::vector<vk::raii::Semaphore> renderFinishedSemaphores;
        std::vector<vk::raii::Fence> presentCompletionFences;
        std::vector<bool> presentPending;

        PresentationGeneration(SwapChain &&newSwapChain, const vk::raii::Device &device);
        ~PresentationGeneration();

        PresentationGeneration(const PresentationGeneration &) = delete;
        PresentationGeneration &operator=(const PresentationGeneration &) = delete;
        PresentationGeneration(PresentationGeneration &&) = delete;
        PresentationGeneration &operator=(PresentationGeneration &&) = delete;
    };

    [[nodiscard]] PresentationGeneration &generation();
    [[nodiscard]] const PresentationGeneration &generation() const;
    void waitForPendingPresent(std::uint32_t imageIndex);
    void waitForPendingPresents();
    void ensureFullScreenExclusiveSupport(const vk::raii::PhysicalDevice &physicalDevice) const;
    void refreshFullScreenExclusiveMonitor() noexcept;
    void acquireFullScreenExclusiveIfNeeded();
    void releaseFullScreenExclusiveIfNeeded() noexcept;

    std::optional<std::reference_wrapper<const vk::raii::Device>> device_{};
    Surface surface_;
    std::optional<PresentationGeneration> generation_{};
    SwapChainConfig config_{};
    std::uint32_t presentQueueFamily_ = 0;
    std::optional<std::uint32_t> activeSwapchainImageIndex_{};
    bool hasSubmittedCurrentFrame_ = false;
    bool fullScreenExclusiveAcquired_ = false;

    AcquireSemaphorePool acquirePool_{};
    std::array<std::optional<std::uint32_t>, maxFrameInFlight> borrowedAcquireSlotByFrame_{};
    AcquireOutOfDateTestHook acquireOutOfDateTestHook_{};
    mutable double verticalScrollOffset_ = 0.0;
    mutable std::vector<std::uint32_t> textInputCodepoints_{};
};
} // namespace nr::rhi
