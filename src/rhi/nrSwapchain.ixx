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
    [[nodiscard]] AcquireResult acquireNextImage(const vk::raii::Semaphore &imageAvailable, std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max()) const
    {
        try
        {
            auto [result, imageIndex] = swapChain.acquireNextImage(timeout, *imageAvailable, vk::Fence{});
            return AcquireResult{
                .imageIndex = imageIndex,
                .result = result,
            };
        }
        catch (const vk::SystemError& error)
        {
            nrAssert(isVulkanResult<vk::Result::eErrorOutOfDateKHR>(error), std::format("SwapChain::acquireNextImage failed: {}", error.what()));
            nrInfo<LogLevel::warning>(std::format("SwapChain::acquireNextImage returned eErrorOutOfDateKHR: {}", error.what()));
            return AcquireResult{
                .imageIndex = 0,
                .result = vk::Result::eErrorOutOfDateKHR,
            };
        }
    }

    /**
     * @brief Present one swapchain image on the given queue.
     *
     * The wait semaphore should be signaled by the render submission for the same frame.
     */
    [[nodiscard]] PresentResult present(const vk::raii::Queue &presentQueue, std::uint32_t imageIndex, const vk::raii::Semaphore &waitSemaphore) const
    {
        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &*waitSemaphore;

        auto swapchainHandle = *swapChain;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &imageIndex;

        try
        {
            auto result = presentQueue.presentKHR(presentInfo);
            return PresentResult{.result = result};
        }
        catch (const vk::SystemError& error)
        {
            nrAssert(isVulkanResult<vk::Result::eErrorOutOfDateKHR>(error), std::format("SwapChain::present failed: {}", error.what()));
            nrInfo<LogLevel::warning>(std::format("SwapChain::present returned eErrorOutOfDateKHR: {}", error.what()));
            return PresentResult{.result = vk::Result::eErrorOutOfDateKHR};
        }
    }

  private:
    template<vk::Result Expected>
    [[nodiscard]] static bool isVulkanResult(const vk::SystemError& error) noexcept
    {
        return error.code().value() == static_cast<int>(Expected);
    }

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
        if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max())
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
            nrAssert(
                requested != presentModes.end(),
                std::format(
                    "SwapChain::create requires present mode '{}'; refusing to enable a v-sync fallback.",
                    vk::to_string(config.presentMode)));
            return config.presentMode;
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

        auto selectedPresentMode = choosePresentMode();

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
            selectedPresentMode,
            vk::True,
            oldSwapchain);

        SwapChain result;
        result.swapChain = vk::raii::SwapchainKHR(device, createInfo);
        result.swapChainImages = result.swapChain.getImages();
        result.format = selectedFormat.format;
        result.extent = extent;

        nrInfo(std::format(
            "Swapchain created: requestedPresentMode={}, selectedPresentMode={}, imageCount={}.",
            vk::to_string(config.presentMode),
            vk::to_string(selectedPresentMode),
            result.swapChainImages.size()));

        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, selectedFormat.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        result.imageViews = result.swapChainImages | std::views::transform([&](vk::Image image) {
                                imageViewCreateInfo.image = image;
                                return vk::raii::ImageView(device, imageViewCreateInfo);
                            }) |
                            std::ranges::to<std::vector>();

        if constexpr (isDebugMode)
        {
            auto imageIndices = std::views::iota(std::size_t{0}, result.swapChainImages.size());
            std::ranges::for_each(imageIndices, [&](std::size_t index) {
                auto imageName = std::format("Swapchain.Image[{}]", index);
                auto viewName = std::format("Swapchain.View[{}]", index);

                auto imageRaw = static_cast<VkImage>(result.swapChainImages[index]);
                auto viewRaw = static_cast<VkImageView>(*result.imageViews[index]);

                auto imageNameInfo = vk::DebugUtilsObjectNameInfoEXT{};
                imageNameInfo.objectType = vk::ObjectType::eImage;
                imageNameInfo.objectHandle = std::bit_cast<std::uint64_t>(imageRaw);
                imageNameInfo.pObjectName = imageName.c_str();

                auto viewNameInfo = vk::DebugUtilsObjectNameInfoEXT{};
                viewNameInfo.objectType = vk::ObjectType::eImageView;
                viewNameInfo.objectHandle = std::bit_cast<std::uint64_t>(viewRaw);
                viewNameInfo.pObjectName = viewName.c_str();

                try
                {
                    device.setDebugUtilsObjectNameEXT(imageNameInfo);
                    device.setDebugUtilsObjectNameEXT(viewNameInfo);
                }
                catch (const vk::SystemError& error)
                {
                    nrInfo<LogLevel::error>(std::format(
                        "SwapChain::create failed to set debug names for swapchain image {}: {}",
                        index,
                        error.what()));
                    nrAssert(false, "SwapChain::create failed to set Vulkan debug object names.");
                }
            });
        }

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
    void initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, std::string_view appName, const SwapChainConfig &config, std::uint32_t presentQueueFamily)
    {
        config_ = config;
        presentQueueFamily_ = presentQueueFamily;
        surface_ = Surface::create(instance, appName);
        ensurePresentSupport(physicalDevice);
        swapChain_ = SwapChain::create(physicalDevice, device, surface_.surface, surface_.extent, config_);
        surface_.format = swapChain_.format;
    }

    [[nodiscard]] AcquireResult acquireNextImage(const vk::raii::Semaphore &imageAvailable, std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max()) const
    {
        return swapChain_.acquireNextImage(imageAvailable, timeout);
    }

    [[nodiscard]] PresentResult present(const QueueManager &queueManager, const vk::raii::Semaphore &waitSemaphore) const
    {
        nrAssert(activeSwapchainImageIndex_.has_value(), "PresentationContext::present requires a valid acquired swapchain image.");
        nrAssert(
            queueManager.compute().queueFamilyIndex() == presentQueueFamily_,
            std::format(
                "PresentationContext::present compute-present policy expected compute queue family {}, but got {}.",
                presentQueueFamily_,
                queueManager.compute().queueFamilyIndex()));
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

    [[nodiscard]] std::uint32_t swapchainImageCount() const noexcept
    {
        return static_cast<std::uint32_t>(swapChain_.swapChainImages.size());
    }

    [[nodiscard]] vk::Image swapchainImage(std::uint32_t imageIndex) const
    {
        nrAssert(imageIndex < swapChain_.swapChainImages.size(), std::format("PresentationContext::swapchainImage index out of range: {}", imageIndex));
        return swapChain_.swapChainImages[imageIndex];
    }

    [[nodiscard]] vk::ImageView swapchainImageView(std::uint32_t imageIndex) const
    {
        nrAssert(imageIndex < swapChain_.imageViews.size(), std::format("PresentationContext::swapchainImageView index out of range: {}", imageIndex));
        return *swapChain_.imageViews[imageIndex];
    }

    void pollEvents() const
    {
        glfwPollEvents();
    }

    [[nodiscard]] bool keyDown(int glfwKeyCode) const
    {
        if (surface_.handle == nullptr)
        {
            return false;
        }

        auto state = glfwGetKey(surface_.handle.get(), glfwKeyCode);
        return state != 0;
    }

    [[nodiscard]] bool mouseButtonDown(int glfwMouseButton) const
    {
        if (surface_.handle == nullptr)
        {
            return false;
        }

        return glfwGetMouseButton(surface_.handle.get(), glfwMouseButton) != 0;
    }

    [[nodiscard]] glm::dvec2 cursorPosition() const
    {
        if (surface_.handle == nullptr)
        {
            return glm::dvec2{0.0, 0.0};
        }

        auto x = 0.0;
        auto y = 0.0;
        glfwGetCursorPos(surface_.handle.get(), &x, &y);
        return glm::dvec2{x, y};
    }

    [[nodiscard]] bool windowShouldClose() const
    {
        return surface_.handle == nullptr || glfwWindowShouldClose(surface_.handle.get()) != 0;
    }

    void setActiveSwapchainImage(std::uint32_t imageIndex)
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

    [[nodiscard]] std::uint32_t activeSwapchainImageIndex() const
    {
        nrAssert(activeSwapchainImageIndex_.has_value(), "PresentationContext::activeSwapchainImageIndex requires an active acquired image.");
        return *activeSwapchainImageIndex_;
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
        nrAssert(
            physicalDevice.getSurfaceSupportKHR(presentQueueFamily_, surface_.surface),
            std::format("Compute-present policy requires compute queue family {} to support present.", presentQueueFamily_));
    }

    Surface surface_;
    SwapChain swapChain_;
    SwapChainConfig config_{};
    std::uint32_t presentQueueFamily_ = 0;
    std::optional<std::uint32_t> activeSwapchainImageIndex_;
    bool hasSubmittedCurrentFrame_ = false;
};
} // namespace nr::rhi
