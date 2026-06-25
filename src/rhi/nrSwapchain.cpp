module nr.rhi;
import dependency.window;
import dependency.math;
import dependency.vulkan;

import :swapchain;
import :surface;
import :queue;
import nr.utils;
import std;

namespace nr::rhi::detail
{
template <vk::Result Expected>
[[nodiscard]] bool isVulkanResult(const vk::SystemError& error) noexcept
{
    return error.code().value() == static_cast<int>(Expected);
}
} // namespace nr::rhi::detail

namespace nr::rhi
{
SwapChain SwapChain::create(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    const vk::raii::SurfaceKHR& surface,
    vk::Extent2D surfaceExtent,
    const SwapChainConfig& config)
{
    return createImpl(physicalDevice, device, surface, surfaceExtent, config, vk::SwapchainKHR{});
}

SwapChain SwapChain::recreate(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    const vk::raii::SurfaceKHR& surface,
    vk::Extent2D surfaceExtent,
    vk::SwapchainKHR oldSwapchain,
    const SwapChainConfig& config)
{
    return createImpl(physicalDevice, device, surface, surfaceExtent, config, oldSwapchain);
}

AcquireResult SwapChain::acquireNextImage(const vk::raii::Semaphore& imageAvailable, std::uint64_t timeout) const
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
        nrAssert(detail::isVulkanResult<vk::Result::eErrorOutOfDateKHR>(error), std::format("SwapChain::acquireNextImage failed: {}", error.what()));
        nrInfo<LogLevel::warning>(std::format("SwapChain::acquireNextImage returned eErrorOutOfDateKHR: {}", error.what()));
        return AcquireResult{
            .result = vk::Result::eErrorOutOfDateKHR,
        };
    }
}

PresentResult SwapChain::present(
    const vk::raii::Queue& presentQueue,
    std::uint32_t imageIndex,
    const vk::raii::Semaphore& waitSemaphore,
    std::optional<std::uint64_t> frameBoundaryFrameID) const
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
        nrAssert(detail::isVulkanResult<vk::Result::eErrorOutOfDateKHR>(error), std::format("SwapChain::present failed: {}", error.what()));
        nrInfo<LogLevel::warning>(std::format("SwapChain::present returned eErrorOutOfDateKHR: {}", error.what()));
        return PresentResult{.result = vk::Result::eErrorOutOfDateKHR};
    }
}

SwapChain SwapChain::createImpl(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    const vk::raii::SurfaceKHR& surface,
    vk::Extent2D surfaceExtent,
    const SwapChainConfig& config,
    vk::SwapchainKHR oldSwapchain)
{
    auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
    nrAssert(!formats.empty(), "SwapChain::create requires at least one supported surface format.");

    auto selectedFormat = [&formats]() -> vk::SurfaceFormatKHR {
        auto srgbBgra = std::ranges::find_if(formats, [](const auto& f) { return f.format == vk::Format::eB8G8R8A8Srgb; });
        if (srgbBgra != formats.end())
        {
            return *srgbBgra;
        }

        auto srgbRgba = std::ranges::find_if(formats, [](const auto& f) { return f.format == vk::Format::eR8G8B8A8Srgb; });
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

void AcquireSemaphorePool::initialize(const vk::raii::Device& device, std::uint32_t capacity)
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

std::uint32_t AcquireSemaphorePool::borrow()
{
    nrAssert(!freeSlots_.empty(), "AcquireSemaphorePool::borrow exhausted all slots.");
    auto slot = freeSlots_.back();
    freeSlots_.pop_back();
    return slot;
}

void AcquireSemaphorePool::returnSlot(std::uint32_t slot)
{
    nrAssert(slot < semaphores_.size(), "AcquireSemaphorePool::returnSlot slot index out of range.");
    freeSlots_.push_back(slot);
}

const vk::raii::Semaphore& AcquireSemaphorePool::semaphore(std::uint32_t slot) const
{
    nrAssert(slot < semaphores_.size(), "AcquireSemaphorePool::semaphore slot index out of range.");
    return semaphores_[slot];
}

bool AcquireSemaphorePool::empty() const noexcept
{
    return semaphores_.empty();
}

void PresentationContext::initialize(
    const vk::raii::Instance& instance,
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    std::string_view appName,
    const SwapChainConfig& config,
    std::uint32_t presentQueueFamily)
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

void PresentationContext::issueFirstAcquire(std::uint64_t timeout)
{
    nrAssert(!pendingAcquire_.has_value(), "PresentationContext::issueFirstAcquire called while a pending acquire already exists.");
    issuePendingAcquireImpl(timeout);
}

void PresentationContext::issueNextAcquire(std::uint64_t timeout)
{
    nrAssert(!pendingAcquire_.has_value(), "PresentationContext::issueNextAcquire called while a pending acquire already exists.");
    issuePendingAcquireImpl(timeout);
}

AcquireResult PresentationContext::consumePendingAcquire(std::uint32_t frameSlot)
{
    nrAssert(pendingAcquire_.has_value(), "PresentationContext::consumePendingAcquire requires a pending acquire.");
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::consumePendingAcquire frameSlot out of range.");
    nrAssert(!borrowedAcquireSlotByFrame_[frameSlot].has_value(), "PresentationContext::consumePendingAcquire frame slot still holds an un-returned acquire semaphore.");
    auto pending = *pendingAcquire_;
    pendingAcquire_.reset();
    borrowedAcquireSlotByFrame_[frameSlot] = pending.semaphoreSlot;
    return AcquireResult{.imageIndex = pending.imageIndex, .result = vk::Result::eSuccess};
}

void PresentationContext::returnAcquireSemaphore(std::uint32_t frameSlot)
{
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::returnAcquireSemaphore frameSlot out of range.");
    if (borrowedAcquireSlotByFrame_[frameSlot].has_value())
    {
        acquirePool_.returnSlot(*borrowedAcquireSlotByFrame_[frameSlot]);
        borrowedAcquireSlotByFrame_[frameSlot].reset();
    }
}

const vk::raii::Semaphore& PresentationContext::borrowedAcquireSemaphore(std::uint32_t frameSlot) const
{
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(), "PresentationContext::borrowedAcquireSemaphore frameSlot out of range.");
    nrAssert(borrowedAcquireSlotByFrame_[frameSlot].has_value(), "PresentationContext::borrowedAcquireSemaphore requires an active borrowed slot for the frame slot.");
    return acquirePool_.semaphore(*borrowedAcquireSlotByFrame_[frameSlot]);
}

bool PresentationContext::hasPendingAcquire() const noexcept
{
    return pendingAcquire_.has_value();
}

PresentResult PresentationContext::present(
    const QueueManager& queueManager,
    const vk::raii::Semaphore& waitSemaphore,
    std::optional<std::uint64_t> frameBoundaryFrameID) const
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

void PresentationContext::rebuildAcquirePool()
{
    nrAssert(device_.has_value(), "PresentationContext::rebuildAcquirePool requires device reference from initialize().");
    pendingAcquire_.reset();
    std::ranges::for_each(borrowedAcquireSlotByFrame_, [](auto& slot) { slot.reset(); });
    auto poolCapacity = swapChain_.swapChainImages.size() + 1u;
    acquirePool_.initialize(device_->get(), static_cast<std::uint32_t>(poolCapacity));
}

vk::Extent2D PresentationContext::swapchainExtent() const noexcept
{
    return swapChain_.extent;
}

vk::Format PresentationContext::swapchainFormat() const noexcept
{
    return swapChain_.format;
}

std::uint32_t PresentationContext::swapchainImageCount() const noexcept
{
    return static_cast<std::uint32_t>(swapChain_.swapChainImages.size());
}

vk::Image PresentationContext::swapchainImage(std::uint32_t imageIndex) const
{
    nrAssert(imageIndex < swapChain_.swapChainImages.size(), std::format("PresentationContext::swapchainImage index out of range: {}", imageIndex));
    return swapChain_.swapChainImages[imageIndex];
}

vk::ImageView PresentationContext::swapchainImageView(std::uint32_t imageIndex) const
{
    nrAssert(imageIndex < swapChain_.imageViews.size(), std::format("PresentationContext::swapchainImageView index out of range: {}", imageIndex));
    return *swapChain_.imageViews[imageIndex];
}

void PresentationContext::pollEvents() const
{
    glfwPollEvents();
}

bool PresentationContext::keyDown(int glfwKeyCode) const
{
    if (surface_.handle == nullptr)
    {
        return false;
    }

    auto state = glfwGetKey(surface_.handle.get(), glfwKeyCode);
    return state != 0;
}

bool PresentationContext::mouseButtonDown(int glfwMouseButton) const
{
    if (surface_.handle == nullptr)
    {
        return false;
    }

    return glfwGetMouseButton(surface_.handle.get(), glfwMouseButton) != 0;
}

glm::dvec2 PresentationContext::cursorPosition() const
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

bool PresentationContext::windowShouldClose() const
{
    return surface_.handle == nullptr || glfwWindowShouldClose(surface_.handle.get()) != 0;
}

void PresentationContext::setActiveSwapchainImage(std::uint32_t imageIndex)
{
    activeSwapchainImageIndex_ = imageIndex;
}

void PresentationContext::clearActiveSwapchainImage()
{
    activeSwapchainImageIndex_.reset();
}

bool PresentationContext::hasActiveSwapchainImage() const
{
    return activeSwapchainImageIndex_.has_value();
}

std::uint32_t PresentationContext::activeSwapchainImageIndex() const
{
    nrAssert(activeSwapchainImageIndex_.has_value(), "PresentationContext::activeSwapchainImageIndex requires an active acquired image.");
    return *activeSwapchainImageIndex_;
}

void PresentationContext::setFrameSubmitted(bool submitted)
{
    hasSubmittedCurrentFrame_ = submitted;
}

bool PresentationContext::hasSubmittedCurrentFrame() const
{
    return hasSubmittedCurrentFrame_;
}

bool PresentationContext::needsSwapchainRecreate(vk::Result result)
{
    return result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR;
}

void PresentationContext::recreate(
    const vk::raii::PhysicalDevice& physicalDevice,
    const vk::raii::Device& device,
    QueueManager& queueManager)
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

void PresentationContext::ensurePresentSupport(const vk::raii::PhysicalDevice& physicalDevice) const
{
    nrAssert(
        physicalDevice.getSurfaceSupportKHR(presentQueueFamily_, surface_.surface),
        std::format("Compute-present policy requires compute queue family {} to support present.", presentQueueFamily_));
}

void PresentationContext::issuePendingAcquireImpl(std::uint64_t timeout)
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
} // namespace nr::rhi
