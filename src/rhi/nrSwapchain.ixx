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
    [[nodiscard]] PresentResult present(const vk::raii::Queue &presentQueue, std::uint32_t imageIndex, const vk::raii::Semaphore &waitSemaphore, std::optional<std::uint64_t> frameBoundaryFrameID) const
    {
        nrAssert(imageIndex < swapChainImages.size(), std::format("SwapChain::present image index {} is out of range for {} swapchain images.", imageIndex, swapChainImages.size()));

        vk::PresentInfoKHR presentInfo{};
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &*waitSemaphore;

        auto swapchainHandle = *swapChain;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &swapchainHandle;
        presentInfo.pImageIndices = &imageIndex;

        std::array<vk::Image, 1> frameBoundaryImages{};
        vk::FrameBoundaryEXT frameBoundary{};
        if (frameBoundaryFrameID.has_value())
        {
            frameBoundaryImages[0] = swapChainImages[imageIndex];
            frameBoundary = vk::FrameBoundaryEXT(
                vk::FrameBoundaryFlagBitsEXT::eFrameEnd,
                *frameBoundaryFrameID,
                static_cast<std::uint32_t>(frameBoundaryImages.size()),
                frameBoundaryImages.data(),
                0,
                nullptr);
            presentInfo.pNext = std::addressof(frameBoundary);
        }

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
 * @brief Reusable pool of binary semaphores for outstanding swapchain acquires.
 *
 * Vulkan restricts vkAcquireNextImageKHR to binary semaphores only. This pool
 * decouples semaphore lifetime from frame-in-flight slots so that the next frame
 * can be acquired immediately after present, while the previous frame's semaphore
 * is still live inside the GPU pipeline.
 *
 * Pool capacity = swapchainImageCount + 1 covers the maximum number of
 * outstanding acquires the spec allows (swapchainImageCount - minImageCount + 1 ≤ capacity).
 */
class AcquireSemaphorePool
{
  public:
    void initialize(const vk::raii::Device &device, std::uint32_t capacity)
    {
        semaphores_.clear();
        freeSlots_.clear();
        semaphores_.reserve(capacity);
        freeSlots_.reserve(capacity);

        auto indices = std::views::iota(std::uint32_t{0}, capacity);
        std::ranges::for_each(indices, [&](std::uint32_t i) {
            semaphores_.emplace_back(device, vk::SemaphoreCreateInfo{});
            freeSlots_.push_back(i);
        });
    }

    /**
     * @brief Borrow a free semaphore slot.
     * @return Slot index to pass back to returnSlot() after the semaphore is consumed by a submit wait.
     */
    [[nodiscard]] std::uint32_t borrow()
    {
        nrAssert(!freeSlots_.empty(), "AcquireSemaphorePool::borrow exhausted all slots.");
        auto slot = freeSlots_.back();
        freeSlots_.pop_back();
        return slot;
    }

    /**
     * @brief Return a slot whose semaphore has been fully consumed by the GPU (wait executed).
     */
    void returnSlot(std::uint32_t slot)
    {
        nrAssert(slot < semaphores_.size(), "AcquireSemaphorePool::returnSlot slot index out of range.");
        freeSlots_.push_back(slot);
    }

    [[nodiscard]] const vk::raii::Semaphore &semaphore(std::uint32_t slot) const
    {
        nrAssert(slot < semaphores_.size(), "AcquireSemaphorePool::semaphore slot index out of range.");
        return semaphores_[slot];
    }

    [[nodiscard]] bool empty() const noexcept { return semaphores_.empty(); }

  private:
    std::vector<vk::raii::Semaphore> semaphores_;
    std::vector<std::uint32_t> freeSlots_;
};

/**
 * @brief Tracks a single outstanding (not yet GPU-waited) acquire result.
 */
struct PendingAcquire
{
    std::uint32_t semaphoreSlot = 0;
    std::uint32_t imageIndex = 0;
};

/**
 * @brief Internal presentation lifecycle service.
 *
 * Lifetime contract: owns Surface + SwapChain and all present contract checks for
 * one Device instance after initialize() until Device destruction.
 *
 * Acquire lifecycle (post-present acquire):
 *   1. initialize() calls issueFirstAcquire() to prime the first frame.
 *   2. presentFrame() calls issueNextAcquire() immediately after vkQueuePresentKHR returns.
 *   3. beginFrame() calls consumePendingAcquire() to take ownership of the pre-acquired
 *      semaphore slot + image index for the coming frame.
 *   4. submitFrameBatch(signalForPresent=true) waits on the borrowed semaphore via addWait().
 *   5. After the final submit fence is signaled (next beginFrame waitForFence), the borrowed
 *      semaphore slot is returned to the pool via returnAcquireSemaphore().
 */
class PresentationContext
{
  public:
    void initialize(const vk::raii::Instance &instance, const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device, std::string_view appName, const SwapChainConfig &config, std::uint32_t presentQueueFamily)
    {
        device_ = std::cref(device);
        config_ = config;
        presentQueueFamily_ = presentQueueFamily;
        surface_ = Surface::create(instance, appName);
        ensurePresentSupport(physicalDevice);
        swapChain_ = SwapChain::create(physicalDevice, device, surface_.surface, surface_.extent, config_);
        surface_.format = swapChain_.format;

        auto poolCapacity = swapChain_.swapChainImages.size() + 1u;
        acquirePool_.initialize(device, static_cast<std::uint32_t>(poolCapacity));
    }

    /**
     * @brief Issue the very first acquire at the start of the application.
     *
     * Must be called once after initialize(), before the first beginFrame().
     */
    void issueFirstAcquire(std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max())
    {
        nrAssert(!pendingAcquire_.has_value(), "PresentationContext::issueFirstAcquire called while a pending acquire already exists.");
        issuePendingAcquireImpl(timeout);
    }

    /**
     * @brief Issue acquire for the next frame immediately after present.
     *
     * On eErrorOutOfDateKHR the pending state is cleared; the caller must recreate
     * and call issueFirstAcquire() again.
     */
    void issueNextAcquire(std::uint64_t timeout = std::numeric_limits<std::uint64_t>::max())
    {
        nrAssert(!pendingAcquire_.has_value(), "PresentationContext::issueNextAcquire called while a pending acquire already exists.");
        issuePendingAcquireImpl(timeout);
    }

    /**
     * @brief Consume the pre-acquired image for the coming frame.
     *
     * Returns the image index and records the borrowed semaphore slot against the
     * given frame slot. The caller must call returnAcquireSemaphore(frameSlot) only
     * after that frame slot's fence is signaled (the imageAvailable wait executed).
     */
    [[nodiscard]] AcquireResult consumePendingAcquire(std::uint32_t frameSlot)
    {
        nrAssert(pendingAcquire_.has_value(), "PresentationContext::consumePendingAcquire requires a pending acquire.");
        nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::consumePendingAcquire frameSlot out of range.");
        nrAssert(!borrowedAcquireSlotByFrame_[frameSlot].has_value(), "PresentationContext::consumePendingAcquire frame slot still holds an un-returned acquire semaphore.");
        auto pending = *pendingAcquire_;
        pendingAcquire_.reset();
        borrowedAcquireSlotByFrame_[frameSlot] = pending.semaphoreSlot;
        return AcquireResult{.imageIndex = pending.imageIndex, .result = vk::Result::eSuccess};
    }

    /**
     * @brief Return the acquire semaphore slot bound to a frame slot.
     *
     * Call once per frame after waitForFence(frameSlot) confirms that frame's
     * final submit has completed (meaning the imageAvailable wait was executed).
     */
    void returnAcquireSemaphore(std::uint32_t frameSlot)
    {
        nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::returnAcquireSemaphore frameSlot out of range.");
        if (borrowedAcquireSlotByFrame_[frameSlot].has_value())
        {
            acquirePool_.returnSlot(*borrowedAcquireSlotByFrame_[frameSlot]);
            borrowedAcquireSlotByFrame_[frameSlot].reset();
        }
    }

    /**
     * @brief Get the semaphore bound to a frame slot's borrowed acquire slot.
     *
     * Valid between consumePendingAcquire(frameSlot) and returnAcquireSemaphore(frameSlot).
     */
    [[nodiscard]] const vk::raii::Semaphore &borrowedAcquireSemaphore(std::uint32_t frameSlot) const
    {
        nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::borrowedAcquireSemaphore frameSlot out of range.");
        nrAssert(borrowedAcquireSlotByFrame_[frameSlot].has_value(), "PresentationContext::borrowedAcquireSemaphore requires an active borrowed slot for the frame slot.");
        return acquirePool_.semaphore(*borrowedAcquireSlotByFrame_[frameSlot]);
    }

    [[nodiscard]] bool hasPendingAcquire() const noexcept
    {
        return pendingAcquire_.has_value();
    }

    [[nodiscard]] PresentResult present(const QueueManager &queueManager, const vk::raii::Semaphore &waitSemaphore, std::optional<std::uint64_t> frameBoundaryFrameID) const
    {
        nrAssert(activeSwapchainImageIndex_.has_value(), "PresentationContext::present requires a valid acquired swapchain image.");
        nrAssert(
            queueManager.compute().queueFamilyIndex() == presentQueueFamily_,
            std::format(
                "PresentationContext::present compute-present policy expected compute queue family {}, but got {}.",
                presentQueueFamily_,
                queueManager.compute().queueFamilyIndex()));
        return swapChain_.present(queueManager.compute().handle(), *activeSwapchainImageIndex_, waitSemaphore, frameBoundaryFrameID);
    }

    /**
     * @brief Rebuild the acquire semaphore pool after swapchain recreation.
     */
    void rebuildAcquirePool()
    {
        nrAssert(device_.has_value(), "PresentationContext::rebuildAcquirePool requires device reference from initialize().");
        pendingAcquire_.reset();
        std::ranges::for_each(borrowedAcquireSlotByFrame_, [](auto& slot) { slot.reset(); });
        auto poolCapacity = swapChain_.swapChainImages.size() + 1u;
        acquirePool_.initialize(device_->get(), static_cast<std::uint32_t>(poolCapacity));
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
        rebuildAcquirePool();
    }

  private:
    void ensurePresentSupport(const vk::raii::PhysicalDevice &physicalDevice) const
    {
        nrAssert(
            physicalDevice.getSurfaceSupportKHR(presentQueueFamily_, surface_.surface),
            std::format("Compute-present policy requires compute queue family {} to support present.", presentQueueFamily_));
    }

    void issuePendingAcquireImpl(std::uint64_t timeout)
    {
        auto slot = acquirePool_.borrow();
        auto acquireResult = swapChain_.acquireNextImage(acquirePool_.semaphore(slot), timeout);

        if (acquireResult.result == vk::Result::eErrorOutOfDateKHR)
        {
            acquirePool_.returnSlot(slot);
            nrInfo<LogLevel::warning>("PresentationContext::issuePendingAcquireImpl got eErrorOutOfDateKHR; caller must recreate.");
            return;
        }

        nrAssert(
            acquireResult.result == vk::Result::eSuccess || acquireResult.result == vk::Result::eSuboptimalKHR,
            std::format("PresentationContext::issuePendingAcquireImpl unexpected acquire result: {}", vk::to_string(acquireResult.result)));

        pendingAcquire_ = PendingAcquire{
            .semaphoreSlot = slot,
            .imageIndex = acquireResult.imageIndex,
        };
    }

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
