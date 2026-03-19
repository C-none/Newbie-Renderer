module;
export module nr.rhi:swapchain;
import dependency;
import :surface;
import :queue;
import nr.utils;
import std;

export namespace nr::rhi
{

struct AcquireResult
{
    uint32_t imageIndex = 0;
    vk::Result result = vk::Result::eSuccess;
};

struct PresentResult
{
    vk::Result result = vk::Result::eSuccess;
};

struct SwapChainConfig
{
    uint32_t preferredImageCount = 3;
    vk::ImageUsageFlags imageUsage = vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment;
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eMailbox;
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
    SwapChain(const SwapChain &) = delete;
    SwapChain &operator=(const SwapChain &) = delete;
    SwapChain(SwapChain &&) = default;
    SwapChain &operator=(SwapChain &&) = default;

    /**
     * @brief Create swapchain and image views for a target surface.
     */
    [[nodiscard]] static SwapChain create(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface, vk::Extent2D surfaceExtent, const SwapChainConfig &config = {})
    {
        return createImpl(physicalDevice, device, surface, surfaceExtent, config, vk::SwapchainKHR{});
    }

    /**
     * @brief Recreate swapchain while preserving old handle for Vulkan migration.
     */
    [[nodiscard]] static SwapChain recreate(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface, vk::Extent2D surfaceExtent, vk::SwapchainKHR oldSwapchain, const SwapChainConfig &config = {})
    {
        return createImpl(physicalDevice, device, surface, surfaceExtent, config, oldSwapchain);
    }

    /**
     * @brief Acquire the next presentable image from the swapchain.
     *
     * The provided semaphore is signaled when the image is ready for queue submission.
     * Returns status that the caller can use to trigger swapchain recreation.
     */
    [[nodiscard]] AcquireResult acquireNextImage(const vk::raii::Semaphore &imageAvailable, uint64_t timeout = std::numeric_limits<uint64_t>::max()) const
    {
        auto [result, imageIndex] = swapChain.acquireNextImage(timeout, *imageAvailable, vk::Fence{});
        return AcquireResult{
            .imageIndex = imageIndex,
            .result = result,
        };
    }

    /**
     * @brief Present one swapchain image on the given queue.
     *
     * The wait semaphore should be signaled by the render submission for the same frame.
     */
    [[nodiscard]] PresentResult present(const vk::raii::Queue &presentQueue, uint32_t imageIndex, const vk::raii::Semaphore &waitSemaphore) const
    {
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &*waitSemaphore;

        auto swapchainHandle = *swapChain;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &imageIndex;

        auto result = presentQueue.presentKHR(presentInfo);
        return PresentResult{.result = result};
    }

  private:
    [[nodiscard]] static SwapChain createImpl(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, const vk::raii::SurfaceKHR &surface, vk::Extent2D surfaceExtent, const SwapChainConfig &config, vk::SwapchainKHR oldSwapchain)
    {
        auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
        nrAssert(!formats.empty(), "SwapChain::create requires at least one supported surface format.");

        auto selectedFormat = [&formats]() -> vk::SurfaceFormatKHR {
            auto srgbBgra = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eB8G8R8A8Srgb; });
            if (srgbBgra != formats.end())
            {
                return *srgbBgra;
            }

            auto srgbRgba = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eR8G8B8A8Srgb; });
            if (srgbRgba != formats.end())
            {
                return *srgbRgba;
            }
            return formats.front();
        }();

        auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
        auto presentModes = physicalDevice.getSurfacePresentModesKHR(surface);

        auto maxImageCount = capabilities.maxImageCount == 0 ? config.preferredImageCount : capabilities.maxImageCount;
        auto imageCount = std::clamp(config.preferredImageCount, capabilities.minImageCount, maxImageCount);

        auto extent = surfaceExtent;
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            extent = capabilities.currentExtent;
        }
        else
        {
            extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
        }

        auto choosePresentMode = [&]() {
            auto requested = std::ranges::find(presentModes, config.presentMode);
            if (requested != presentModes.end())
            {
                return config.presentMode;
            }
            auto mailbox = std::ranges::find(presentModes, vk::PresentModeKHR::eMailbox);
            if (mailbox != presentModes.end())
            {
                return vk::PresentModeKHR::eMailbox;
            }
            return vk::PresentModeKHR::eFifo;
        };

        auto chooseCompositeAlpha = [&]() {
            if ((capabilities.supportedCompositeAlpha & config.compositeAlpha) == config.compositeAlpha)
            {
                return config.compositeAlpha;
            }
            constexpr std::array preferred{
                vk::CompositeAlphaFlagBitsKHR::eOpaque,
                vk::CompositeAlphaFlagBitsKHR::ePreMultiplied,
                vk::CompositeAlphaFlagBitsKHR::ePostMultiplied,
                vk::CompositeAlphaFlagBitsKHR::eInherit,
            };
            auto it = std::ranges::find_if(preferred, [&](vk::CompositeAlphaFlagBitsKHR candidate) {
                return (capabilities.supportedCompositeAlpha & candidate) == candidate;
            });
            nrAssert(it != preferred.end(), "SwapChain::create found no supported composite alpha mode.");
            return *it;
        };

        auto chooseSurfaceTransform = [&]() {
            if ((capabilities.supportedTransforms & config.surfaceTransform) == config.surfaceTransform)
            {
                return config.surfaceTransform;
            }
            return capabilities.currentTransform;
        };

        vk::SwapchainCreateInfoKHR createInfo(
            vk::SwapchainCreateFlagsKHR{},
            surface,
            imageCount,
            selectedFormat.format,
            selectedFormat.colorSpace,
            extent,
            1,
            config.imageUsage,
            vk::SharingMode::eExclusive,
            {},
            chooseSurfaceTransform(),
            chooseCompositeAlpha(),
            choosePresentMode(),
            vk::True,
            oldSwapchain);

        SwapChain result;
        result.swapChain = vk::raii::SwapchainKHR(device, createInfo);
        result.swapChainImages = result.swapChain.getImages();
        result.format = selectedFormat.format;
        result.extent = extent;

        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, selectedFormat.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        result.imageViews = result.swapChainImages | std::views::transform([&](vk::Image image) {
                                imageViewCreateInfo.image = image;
                                return vk::raii::ImageView(device, imageViewCreateInfo);
                            }) |
                            std::ranges::to<std::vector>();
        return result;
    }
};

/**
 * @brief Internal presentation lifecycle service.
 *
 * Lifetime contract: owns Surface + SwapChain and all present contract checks for
 * one Device instance after initialize() until Device destruction.
 */
class PresentationContext
{
  public:
    void initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, std::string_view appName, const SwapChainConfig &config, uint32_t presentQueueFamily)
    {
        config_ = config;
        presentQueueFamily_ = presentQueueFamily;
        surface_ = Surface::create(instance, appName);
        ensurePresentSupport(physicalDevice);
        swapChain_ = SwapChain::create(physicalDevice, device, surface_.surface, surface_.extent, config_);
        surface_.format = swapChain_.format;
    }

    [[nodiscard]] AcquireResult acquireNextImage(const vk::raii::Semaphore &imageAvailable, uint64_t timeout = std::numeric_limits<uint64_t>::max()) const
    {
        return swapChain_.acquireNextImage(imageAvailable, timeout);
    }

    [[nodiscard]] PresentResult present(const QueueManager &queueManager, const vk::raii::Semaphore &waitSemaphore) const
    {
        nrAssert(activeSwapchainImageIndex_.has_value(), "PresentationContext::present requires a valid acquired swapchain image.");
        return swapChain_.present(queueManager.compute().handle(), *activeSwapchainImageIndex_, waitSemaphore);
    }

    [[nodiscard]] vk::Extent2D swapchainExtent() const noexcept
    {
        return swapChain_.extent;
    }

    [[nodiscard]] vk::Format swapchainFormat() const noexcept
    {
        return swapChain_.format;
    }

    [[nodiscard]] uint32_t swapchainImageCount() const noexcept
    {
        return static_cast<uint32_t>(swapChain_.swapChainImages.size());
    }

    [[nodiscard]] vk::Image swapchainImage(uint32_t imageIndex) const
    {
        nrAssert(imageIndex < swapChain_.swapChainImages.size(), std::format("PresentationContext::swapchainImage index out of range: {}", imageIndex));
        return swapChain_.swapChainImages[imageIndex];
    }

    [[nodiscard]] vk::ImageView swapchainImageView(uint32_t imageIndex) const
    {
        nrAssert(imageIndex < swapChain_.imageViews.size(), std::format("PresentationContext::swapchainImageView index out of range: {}", imageIndex));
        return *swapChain_.imageViews[imageIndex];
    }

    void pollEvents() const
    {
        glfwPollEvents();
    }

    [[nodiscard]] bool windowShouldClose() const
    {
        return surface_.handle == nullptr || glfwWindowShouldClose(surface_.handle.get()) != 0;
    }

    void setActiveSwapchainImage(uint32_t imageIndex)
    {
        activeSwapchainImageIndex_ = imageIndex;
    }

    void clearActiveSwapchainImage()
    {
        activeSwapchainImageIndex_.reset();
    }

    [[nodiscard]] bool hasActiveSwapchainImage() const
    {
        return activeSwapchainImageIndex_.has_value();
    }

    void setFrameSubmitted(bool submitted)
    {
        hasSubmittedCurrentFrame_ = submitted;
    }

    [[nodiscard]] bool hasSubmittedCurrentFrame() const
    {
        return hasSubmittedCurrentFrame_;
    }

    [[nodiscard]] static bool needsSwapchainRecreate(vk::Result result)
    {
        return result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR;
    }

    void recreate(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, QueueManager &queueManager)
    {
        queueManager.waitAllIdle();
        surface_.refreshExtentFromFramebuffer();
        ensurePresentSupport(physicalDevice);
        auto oldSwapchain = *swapChain_.swapChain;
        auto rebuilt = SwapChain::recreate(physicalDevice, device, surface_.surface, surface_.extent, oldSwapchain, config_);
        surface_.format = rebuilt.format;
        swapChain_ = std::move(rebuilt);
    }

  private:
    void ensurePresentSupport(const vk::raii::PhysicalDevice &physicalDevice) const
    {
        nrAssert(physicalDevice.getSurfaceSupportKHR(presentQueueFamily_, surface_.surface), std::format("Compute queue family {} does not support present.", presentQueueFamily_));
    }

    Surface surface_;
    SwapChain swapChain_;
    SwapChainConfig config_{};
    uint32_t presentQueueFamily_ = 0;
    std::optional<uint32_t> activeSwapchainImageIndex_;
    bool hasSubmittedCurrentFrame_ = false;
};
} // namespace nr::rhi