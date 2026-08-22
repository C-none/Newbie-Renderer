module nr.rhi;
import dependency.window;
import dependency.math;
import dependency.vulkan;

import :swapchain;
import :surface;
import :queue;
import :type;
import nr.utils;
import std;

namespace nr::rhi::detail
{
struct SurfaceFormatPreference
{
    vk::Format format = vk::Format::eUndefined;
    vk::ColorSpaceKHR colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
};

template <vk::Result Expected> [[nodiscard]] bool isVulkanResult(const vk::SystemError &error) noexcept
{
    return error.code().value() == static_cast<int>(Expected);
}

[[nodiscard]] std::optional<vk::Result> swapchainRecreateResultFrom(const vk::SystemError &error) noexcept
{
    if (isVulkanResult<vk::Result::eErrorOutOfDateKHR>(error))
    {
        return vk::Result::eErrorOutOfDateKHR;
    }
    if (isVulkanResult<vk::Result::eErrorFullScreenExclusiveModeLostEXT>(error))
    {
        return vk::Result::eErrorFullScreenExclusiveModeLostEXT;
    }
    return std::nullopt;
}

template <typename Result>
[[nodiscard]] Result swapchainFailureResult(const vk::SystemError &error, std::string_view operation)
{
    auto recreateResult = swapchainRecreateResultFrom(error);
    if (!recreateResult.has_value())
    {
        nrAssert(false, "SwapChain::{} failed: {}", operation, error.what());
        return Result{.result = vk::Result::eErrorOutOfDateKHR};
    }
    nrLog<LogLevel::warning>("SwapChain::{} returned {}: {}", operation, vk::to_string(*recreateResult),
                             error.what());
    return Result{.result = *recreateResult};
}

[[nodiscard]] bool matchesSurfaceFormat(const vk::SurfaceFormatKHR &candidate,
                                        SurfaceFormatPreference preference) noexcept
{
    return candidate.format == preference.format && candidate.colorSpace == preference.colorSpace;
}

[[nodiscard]] std::optional<vk::SurfaceFormatKHR> findPreferredSurfaceFormat(
    std::span<const vk::SurfaceFormatKHR> formats, std::span<const SurfaceFormatPreference> preferences)
{
    auto preference = std::ranges::find_if(preferences, [&](SurfaceFormatPreference item) {
        return std::ranges::any_of(
            formats, [&](const vk::SurfaceFormatKHR &format) { return matchesSurfaceFormat(format, item); });
    });
    if (preference == preferences.end())
    {
        return std::nullopt;
    }

    auto format = std::ranges::find_if(
        formats, [&](const vk::SurfaceFormatKHR &item) { return matchesSurfaceFormat(item, *preference); });
    nrAssert(format != formats.end(), "findPreferredSurfaceFormat resolved a preference without a matching format.");
    return *format;
}

[[nodiscard]] std::string formatSurfaceFormatList(std::span<const vk::SurfaceFormatKHR> formats)
{
    auto indices = std::views::iota(std::size_t{0}, formats.size());
    auto lines = indices | std::views::transform([&](std::size_t index) {
                     const auto &format = formats[index];
                     return std::format("[{}] format={} colorSpace={}", index, vk::to_string(format.format),
                                        vk::to_string(format.colorSpace));
                 }) |
                 std::ranges::to<std::vector>();
    return lines.empty() ? std::string{"<none>"} : std::views::join_with(lines, '\n') | std::ranges::to<std::string>();
}

[[nodiscard]] bool hasHdrSurfaceFormat(std::span<const vk::SurfaceFormatKHR> formats) noexcept
{
    return std::ranges::any_of(formats, [](const vk::SurfaceFormatKHR &format) {
        return isHdr10SwapchainColorSpace(format.colorSpace) || isScRgbSwapchainColorSpace(format.colorSpace);
    });
}

[[nodiscard]] vk::HdrMetadataEXT makeHdr10Metadata() noexcept
{
    return vk::HdrMetadataEXT{vk::XYColorEXT{0.708f, 0.292f},
                              vk::XYColorEXT{0.170f, 0.797f},
                              vk::XYColorEXT{0.131f, 0.046f},
                              vk::XYColorEXT{0.3127f, 0.3290f},
                              1000.0f,
                              0.0001f,
                              1000.0f,
                              203.0f};
}

void applyHdrMetadataIfNeeded(const vk::raii::Device &device, vk::SwapchainKHR swapchain,
                              vk::SurfaceFormatKHR surfaceFormat, const SwapChainConfig &config)
{
    if (!config.hdrMetadataEnabled || !isHdr10SwapchainColorSpace(surfaceFormat.colorSpace))
    {
        return;
    }

    auto swapchains = std::array{swapchain};
    auto metadata = std::array{makeHdr10Metadata()};
    try
    {
        device.setHdrMetadataEXT(std::span<const vk::SwapchainKHR>{swapchains.data(), swapchains.size()},
                                 std::span<const vk::HdrMetadataEXT>{metadata.data(), metadata.size()});
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("SwapChain::create failed to set HDR10 metadata: {}", error.what());
        nrAssert(false, "SwapChain::create failed to set HDR10 metadata.");
    }
}
} // namespace nr::rhi::detail

namespace nr::rhi
{
[[nodiscard]] bool isHdr10SwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept
{
    return colorSpace == vk::ColorSpaceKHR::eHdr10St2084EXT;
}

[[nodiscard]] bool isScRgbSwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept
{
    return colorSpace == vk::ColorSpaceKHR::eExtendedSrgbLinearEXT;
}

[[nodiscard]] bool isHdrSwapchainColorSpace(vk::ColorSpaceKHR colorSpace) noexcept
{
    return isHdr10SwapchainColorSpace(colorSpace) || isScRgbSwapchainColorSpace(colorSpace);
}

[[nodiscard]] vk::SurfaceFormatKHR chooseSwapchainSurfaceFormat(std::span<const vk::SurfaceFormatKHR> formats)
{
    nrAssert(!formats.empty(), "chooseSwapchainSurfaceFormat requires at least one supported surface format.");

    // The project only outputs to three color spaces: scRGB extended-linear, HDR10 ST2084, and SDR sRGB.
    // Preference order keeps HDR paths ahead of SDR; each entry is matched by exact format+color-space pair
    // so an unsupported gamut can never be selected. Fail fast if none of the supported pairs is available.
    constexpr auto preferences = std::array{
        detail::SurfaceFormatPreference{
            .format = vk::Format::eR16G16B16A16Sfloat,
            .colorSpace = vk::ColorSpaceKHR::eExtendedSrgbLinearEXT,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eA2B10G10R10UnormPack32,
            .colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eA2R10G10B10UnormPack32,
            .colorSpace = vk::ColorSpaceKHR::eHdr10St2084EXT,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eB8G8R8A8Srgb,
            .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eR8G8B8A8Srgb,
            .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eB8G8R8A8Unorm,
            .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        },
        detail::SurfaceFormatPreference{
            .format = vk::Format::eR8G8B8A8Unorm,
            .colorSpace = vk::ColorSpaceKHR::eSrgbNonlinear,
        },
    };
    if (auto selectedFormat = detail::findPreferredSurfaceFormat(formats, std::span{preferences});
        selectedFormat.has_value())
    {
        return *selectedFormat;
    }

    nrAssert(false,
             "chooseSwapchainSurfaceFormat found none of the supported scRGB / HDR10 ST2084 / SDR "
             "sRGB format-color-space pairs. Available surface formats:\n{}",
             detail::formatSurfaceFormatList(formats));
    return formats.front();
}

AcquireResult SwapChain::acquireNextImage(const vk::raii::Semaphore &imageAvailable, std::uint64_t timeout) const
{
    try
    {
        auto [result, imageIndex] = swapChain.acquireNextImage(timeout, *imageAvailable, vk::Fence{});
        return AcquireResult{
            .imageIndex = imageIndex,
            .result = result,
        };
    }
    catch (const vk::SystemError &error)
    {
        return detail::swapchainFailureResult<AcquireResult>(error, "acquireNextImage");
    }
}

PresentResult SwapChain::present(const vk::raii::Queue &presentQueue, std::uint32_t imageIndex,
                                 const vk::raii::Semaphore &waitSemaphore,
                                 const vk::raii::Fence &presentCompletionFence,
                                 std::optional<std::uint64_t> frameBoundaryFrameID) const
{
    nrAssert(imageIndex < swapChainImages.size(),
             "SwapChain::present image index {} is out of range for {} swapchain images.", imageIndex,
             swapChainImages.size());

    vk::PresentInfoKHR presentInfo{};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &*waitSemaphore;

    auto swapchainHandle = *swapChain;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;
    auto swapchainResult = vk::Result::eSuccess;
    presentInfo.pResults = std::addressof(swapchainResult);

    auto presentCompletionFenceHandle = *presentCompletionFence;
    auto presentFenceInfo = vk::SwapchainPresentFenceInfoEXT{};
    presentFenceInfo.swapchainCount = 1;
    presentFenceInfo.pFences = std::addressof(presentCompletionFenceHandle);
    presentInfo.pNext = std::addressof(presentFenceInfo);

    std::array<vk::Image, 1> frameBoundaryImages{};
    vk::FrameBoundaryEXT frameBoundary{};
    if (frameBoundaryFrameID.has_value())
    {
        frameBoundaryImages[0] = swapChainImages[imageIndex];
        frameBoundary = vk::FrameBoundaryEXT(vk::FrameBoundaryFlagBitsEXT::eFrameEnd, *frameBoundaryFrameID,
                                             static_cast<std::uint32_t>(frameBoundaryImages.size()),
                                             frameBoundaryImages.data(), 0, nullptr);
        presentFenceInfo.pNext = std::addressof(frameBoundary);
    }

    try
    {
        auto result = presentQueue.presentKHR(presentInfo);
        return resolvePresentResult(result, swapchainResult);
    }
    catch (const vk::SystemError &error)
    {
        return detail::swapchainFailureResult<PresentResult>(error, "present");
    }
}

PresentResult SwapChain::resolvePresentResult(vk::Result queueResult, vk::Result swapchainResult) noexcept
{
    auto const wasQueued = [](vk::Result item) {
        return item == vk::Result::eSuccess || item == vk::Result::eSuboptimalKHR;
    };
    return PresentResult{
        .result = wasQueued(queueResult) && swapchainResult != vk::Result::eSuccess ? swapchainResult : queueResult,
        .queued = wasQueued(queueResult) && wasQueued(swapchainResult),
    };
}

SwapChain SwapChain::create(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device,
                            const vk::raii::SurfaceKHR &surface, vk::Extent2D surfaceExtent,
                            const SwapChainConfig &config, vk::SwapchainKHR oldSwapchain)
{
    auto formats = physicalDevice.getSurfaceFormatsKHR(surface);
    nrAssert(!formats.empty(), "SwapChain::create requires at least one supported surface format.");
    auto surfaceFormats = std::span<const vk::SurfaceFormatKHR>{formats.data(), formats.size()};

    if (!detail::hasHdrSurfaceFormat(surfaceFormats))
    {
        nrLog<LogLevel::warning>("SwapChain::create did not receive any HDR10/scRGB surface format/color-space pairs "
                                  "from Vulkan WSI; falling back to SDR.");
    }

    auto selectedFormat = chooseSwapchainSurfaceFormat(surfaceFormats);

    auto capabilities = physicalDevice.getSurfaceCapabilitiesKHR(surface);
    nrAssert((config.imageUsage & capabilities.supportedUsageFlags) == config.imageUsage,
             "SwapChain::create image usage is unsupported. requested={} supported={}.",
             vk::to_string(config.imageUsage), vk::to_string(capabilities.supportedUsageFlags));
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
        extent.height =
            std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    auto choosePresentMode = [&]() {
        auto requested = std::ranges::find(presentModes, config.presentMode);
        nrAssert(requested != presentModes.end(),
                 "SwapChain::create requires present mode '{}'; refusing to enable a v-sync fallback.",
                 vk::to_string(config.presentMode));
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

    vk::SurfaceFullScreenExclusiveWin32InfoEXT fullScreenExclusiveWin32Info{};
    vk::SurfaceFullScreenExclusiveInfoEXT fullScreenExclusiveInfo{};
    auto fullScreenExclusivePolicy = std::string_view{"Default"};
    if (config.fullScreenExclusiveApplicationControlled)
    {
        nrAssert(config.fullScreenExclusiveMonitor != 0,
                 "VK_EXT_full_screen_exclusive requires a valid Win32 monitor handle.");
        fullScreenExclusiveWin32Info.hmonitor =
            reinterpret_cast<decltype(fullScreenExclusiveWin32Info.hmonitor)>(config.fullScreenExclusiveMonitor);
        fullScreenExclusiveInfo.fullScreenExclusive = vk::FullScreenExclusiveEXT::eApplicationControlled;
        fullScreenExclusiveInfo.pNext = std::addressof(fullScreenExclusiveWin32Info);
        fullScreenExclusivePolicy = "ApplicationControlled";
    }

    vk::SwapchainCreateInfoKHR createInfo(vk::SwapchainCreateFlagsKHR{}, surface, imageCount, selectedFormat.format,
                                          selectedFormat.colorSpace, extent, 1, config.imageUsage,
                                          vk::SharingMode::eExclusive, {}, chooseSurfaceTransform(),
                                          chooseCompositeAlpha(), selectedPresentMode, vk::True, oldSwapchain);
    if (config.fullScreenExclusiveApplicationControlled)
    {
        createInfo.pNext = std::addressof(fullScreenExclusiveInfo);
    }

    SwapChain result;
    result.swapChain = vk::raii::SwapchainKHR(device, createInfo);
    result.swapChainImages = result.swapChain.getImages();
    result.surfaceFormat = selectedFormat;
    result.extent = extent;
    detail::applyHdrMetadataIfNeeded(device, *result.swapChain, selectedFormat, config);

    nrLog<LogLevel::info>("Swapchain created: requestedPresentMode={}, selectedPresentMode={}, format={}, colorSpace={}, "
            "hdrOutput={}, fullScreenExclusivePolicy={}, imageCount={}.",
            vk::to_string(config.presentMode), vk::to_string(selectedPresentMode), vk::to_string(selectedFormat.format),
            vk::to_string(selectedFormat.colorSpace), isHdrSwapchainColorSpace(selectedFormat.colorSpace) ? "true" : "false",
            fullScreenExclusivePolicy, result.swapChainImages.size());

    vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, selectedFormat.format, {},
                                                {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
    result.imageViews = result.swapChainImages | std::views::transform([&](vk::Image image) {
                            imageViewCreateInfo.image = image;
                            return vk::raii::ImageView(device, imageViewCreateInfo);
                        }) |
                        std::ranges::to<std::vector>();

    if constexpr (gpuDebugNamesEnabled)
    {
        auto imageIndices = std::views::iota(std::size_t{0}, result.swapChainImages.size());
        std::ranges::for_each(imageIndices, [&](std::size_t index) {
            auto imageName = std::format("Swapchain.Image[{}]", index);
            auto viewName = std::format("Swapchain.View[{}]", index);
            try
            {
                nr::rhi::setDebugObjectName<vk::ObjectType::eImage>(device, result.swapChainImages[index], imageName);
                nr::rhi::setDebugObjectName<vk::ObjectType::eImageView>(device, result.imageViews[index], viewName);
            }
            catch (const vk::SystemError &error)
            {
                nrLog<LogLevel::warning>("SwapChain::create failed to set debug names for swapchain image {}: {}", index,
                                        error.what());
                nrAssert(false, "SwapChain::create failed to set Vulkan debug object names.");
            }
        });
    }

    return result;
}

void AcquireSemaphorePool::initialize(const vk::raii::Device &device, std::uint32_t capacity)
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

const vk::raii::Semaphore &AcquireSemaphorePool::semaphore(std::uint32_t slot) const
{
    nrAssert(slot < semaphores_.size(), "AcquireSemaphorePool::semaphore slot index out of range.");
    return semaphores_[slot];
}

PresentationContext::PresentationGeneration::PresentationGeneration(SwapChain &&newSwapChain,
                                                                     const vk::raii::Device &device)
    : swapChain(std::move(newSwapChain))
{
    auto const imageCount = static_cast<std::uint32_t>(swapChain.swapChainImages.size());
    renderFinishedSemaphores.reserve(imageCount);
    presentCompletionFences.reserve(imageCount);
    presentPending.resize(imageCount);

    auto imageIndices = std::views::iota(std::uint32_t{0}, imageCount);
    std::ranges::for_each(imageIndices, [&](std::uint32_t) {
        renderFinishedSemaphores.emplace_back(device, vk::SemaphoreCreateInfo{});
        presentCompletionFences.emplace_back(device, vk::FenceCreateInfo{});
    });
}

PresentationContext::PresentationGeneration::~PresentationGeneration()
{
    // Keep the old swapchain alive until every object derived from or submitted with this generation is gone.
    swapChain.imageViews.clear();
    presentCompletionFences.clear();
    renderFinishedSemaphores.clear();
}

PresentationContext::PresentationGeneration &PresentationContext::generation()
{
    nrAssert(generation_.has_value(), "PresentationContext requires an initialized presentation generation.");
    return *generation_;
}

const PresentationContext::PresentationGeneration &PresentationContext::generation() const
{
    nrAssert(generation_.has_value(), "PresentationContext requires an initialized presentation generation.");
    return *generation_;
}

void PresentationContext::waitForPendingPresent(std::uint32_t imageIndex)
{
    auto &currentGeneration = generation();
    nrAssert(imageIndex < currentGeneration.presentPending.size(),
             "PresentationContext::waitForPendingPresent image index out of range.");
    if (!currentGeneration.presentPending[imageIndex])
    {
        return;
    }

    nrAssert(device_.has_value(),
             "PresentationContext::waitForPendingPresent requires initializeSwapchain() first.");
    auto &presentFence = currentGeneration.presentCompletionFences[imageIndex];
    auto const waitResult =
        device_->get().waitForFences(*presentFence, vk::True, std::numeric_limits<std::uint64_t>::max());
    nrAssert(waitResult == vk::Result::eSuccess,
             "PresentationContext failed waiting for present completion fence for image {}: {}", imageIndex,
             vk::to_string(waitResult));
    device_->get().resetFences(*presentFence);
    currentGeneration.presentPending[imageIndex] = false;
}

void PresentationContext::waitForPendingPresents()
{
    if (!generation_.has_value())
    {
        return;
    }

    auto imageIndices = std::views::iota(std::uint32_t{0},
                                         static_cast<std::uint32_t>(generation_->presentPending.size()));
    std::ranges::for_each(imageIndices, [&](std::uint32_t imageIndex) { waitForPendingPresent(imageIndex); });
}

PresentationContext::PresentationContext(Surface surface) : surface_(std::move(surface))
{
}

PresentationContext::~PresentationContext()
{
    waitForPendingPresents();
    releaseFullScreenExclusiveIfNeeded();
    generation_.reset();
}

void PresentationContext::initializeSwapchain(const vk::raii::PhysicalDevice &physicalDevice,
                                              const vk::raii::Device &device, const SwapChainConfig &config,
                                              std::uint32_t presentQueueFamily)
{
    nrAssert(!generation_.has_value(),
             "PresentationContext::initializeSwapchain can only initialize the first swapchain generation once.");
    device_ = std::cref(device);
    config_ = config;
    presentQueueFamily_ = presentQueueFamily;
    refreshFullScreenExclusiveMonitor();
    ensureFullScreenExclusiveSupport(physicalDevice);
    auto swapChain = SwapChain::create(physicalDevice, device, surface_.surface, surface_.extent, config_);
    generation_.emplace(std::move(swapChain), device);
    acquireFullScreenExclusiveIfNeeded();
    rebuildAcquirePool();
}

const vk::raii::SurfaceKHR &PresentationContext::surfaceHandle() const
{
    return surface_.surface;
}

AcquireResult PresentationContext::acquireNextImage(std::uint32_t frameSlot, std::uint64_t timeout)
{
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(),
             "PresentationContext::acquireNextImage frameSlot out of range.");
    nrAssert(!borrowedAcquireSlotByFrame_[frameSlot].has_value(),
             "PresentationContext::acquireNextImage frame slot still holds an un-returned acquire semaphore.");

    auto slot = acquirePool_.borrow();
    auto acquireResult = [&]() {
        if constexpr (nr::isDebugMode)
        {
            if (acquireOutOfDateTestHook_ && acquireOutOfDateTestHook_())
            {
                return AcquireResult{
                    .result = vk::Result::eErrorOutOfDateKHR,
                };
            }
        }
        return generation().swapChain.acquireNextImage(acquirePool_.semaphore(slot), timeout);
    }();
    if (needsSwapchainRecreate(acquireResult.result) && acquireResult.result != vk::Result::eSuboptimalKHR)
    {
        acquirePool_.returnSlot(slot);
        nrLog<LogLevel::warning>("PresentationContext::acquireNextImage got {}; caller must recreate.",
                                 vk::to_string(acquireResult.result));
        return acquireResult;
    }

    nrAssert(acquireResult.result == vk::Result::eSuccess || acquireResult.result == vk::Result::eSuboptimalKHR,
             "PresentationContext::acquireNextImage unexpected acquire result: {}",
             vk::to_string(acquireResult.result));
    waitForPendingPresent(acquireResult.imageIndex);
    borrowedAcquireSlotByFrame_[frameSlot] = slot;
    return acquireResult;
}

void PresentationContext::setAcquireOutOfDateTestHook(AcquireOutOfDateTestHook hook)
{
    nrAssert(static_cast<bool>(hook), "PresentationContext::setAcquireOutOfDateTestHook requires a non-empty hook.");
    if constexpr (nr::isDebugMode)
    {
        acquireOutOfDateTestHook_ = std::move(hook);
    }
    else
    {
        nrAssert(false, "PresentationContext acquire out-of-date test hooks are available only in Debug builds.");
    }
}

void PresentationContext::clearAcquireOutOfDateTestHook() noexcept
{
    if constexpr (nr::isDebugMode)
    {
        acquireOutOfDateTestHook_ = {};
    }
}

void PresentationContext::returnAcquireSemaphore(std::uint32_t frameSlot)
{
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(),
             "PresentationContext::returnAcquireSemaphore frameSlot out of range.");
    if (borrowedAcquireSlotByFrame_[frameSlot].has_value())
    {
        acquirePool_.returnSlot(*borrowedAcquireSlotByFrame_[frameSlot]);
        borrowedAcquireSlotByFrame_[frameSlot].reset();
    }
}

const vk::raii::Semaphore &PresentationContext::borrowedAcquireSemaphore(std::uint32_t frameSlot) const
{
    nrAssert(frameSlot < borrowedAcquireSlotByFrame_.size(),
             "PresentationContext::borrowedAcquireSemaphore frameSlot out of range.");
    nrAssert(borrowedAcquireSlotByFrame_[frameSlot].has_value(),
             "PresentationContext::borrowedAcquireSemaphore requires an active borrowed slot for the frame slot.");
    return acquirePool_.semaphore(*borrowedAcquireSlotByFrame_[frameSlot]);
}

const vk::raii::Semaphore &PresentationContext::activePresentSemaphore() const
{
    auto const imageIndex = activeSwapchainImageIndex();
    auto const &currentGeneration = generation();
    nrAssert(imageIndex < currentGeneration.renderFinishedSemaphores.size(),
             "PresentationContext::activePresentSemaphore image index out of range.");
    return currentGeneration.renderFinishedSemaphores[imageIndex];
}

PresentResult PresentationContext::present(const QueueManager &queueManager,
                                           std::optional<std::uint64_t> frameBoundaryFrameID)
{
    nrAssert(activeSwapchainImageIndex_.has_value(),
             "PresentationContext::present requires a valid acquired swapchain image.");
    nrAssert(queueManager.compute().queueFamilyIndex() == presentQueueFamily_,
             "PresentationContext::present compute-present policy expected compute queue family {}, but got {}.",
             presentQueueFamily_, queueManager.compute().queueFamilyIndex());
    auto const imageIndex = *activeSwapchainImageIndex_;
    auto &currentGeneration = generation();
    nrAssert(!currentGeneration.presentPending[imageIndex],
             "PresentationContext::present cannot reuse an image whose previous present is still pending.");
    auto result = currentGeneration.swapChain.present(
        queueManager.compute().handle(), imageIndex, currentGeneration.renderFinishedSemaphores[imageIndex],
        currentGeneration.presentCompletionFences[imageIndex], frameBoundaryFrameID);
    if (result.queued && (result.result == vk::Result::eSuccess || result.result == vk::Result::eSuboptimalKHR))
    {
        currentGeneration.presentPending[imageIndex] = true;
    }
    return result;
}

void PresentationContext::rebuildAcquirePool()
{
    nrAssert(device_.has_value(),
             "PresentationContext::rebuildAcquirePool requires initializeSwapchain() first.");
    std::ranges::for_each(borrowedAcquireSlotByFrame_, [](auto &slot) { slot.reset(); });
    auto const poolCapacity = std::max(swapchainImageCount() + 1u, maxFrameInFlight);
    acquirePool_.initialize(device_->get(), poolCapacity);
}

vk::Extent2D PresentationContext::swapchainExtent() const noexcept
{
    return generation().swapChain.extent;
}

vk::Format PresentationContext::swapchainFormat() const noexcept
{
    return generation().swapChain.surfaceFormat.format;
}

vk::ColorSpaceKHR PresentationContext::swapchainColorSpace() const noexcept
{
    return generation().swapChain.surfaceFormat.colorSpace;
}

std::uint32_t PresentationContext::swapchainImageCount() const noexcept
{
    return static_cast<std::uint32_t>(generation().swapChain.swapChainImages.size());
}

vk::Image PresentationContext::swapchainImage(std::uint32_t imageIndex) const
{
    auto const &swapChain = generation().swapChain;
    nrAssert(imageIndex < swapChain.swapChainImages.size(),
             "PresentationContext::swapchainImage index out of range: {}", imageIndex);
    return swapChain.swapChainImages[imageIndex];
}

vk::ImageView PresentationContext::swapchainImageView(std::uint32_t imageIndex) const
{
    auto const &swapChain = generation().swapChain;
    nrAssert(imageIndex < swapChain.imageViews.size(),
             "PresentationContext::swapchainImageView index out of range: {}", imageIndex);
    return *swapChain.imageViews[imageIndex];
}

WindowInput &PresentationContext::windowInput()
{
    return *surface_.input;
}

const WindowInput &PresentationContext::windowInput() const
{
    return *surface_.input;
}

bool PresentationContext::windowShouldClose() const
{
    return surface_.handle == nullptr || glfwWindowShouldClose(surface_.handle.get()) != 0;
}

bool PresentationContext::framebufferAvailable() const noexcept
{
    return surface_.framebufferAvailable();
}

bool PresentationContext::fullscreenEnabled() const noexcept
{
    return surface_.fullscreenEnabled();
}

void PresentationContext::setFullscreen(bool enabled)
{
    surface_.setFullscreen(enabled);
    refreshFullScreenExclusiveMonitor();
}

bool PresentationContext::consumeSwapchainRecreateRequest() noexcept
{
    return surface_.consumeSwapchainRecreateRequest();
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
    nrAssert(activeSwapchainImageIndex_.has_value(),
             "PresentationContext::activeSwapchainImageIndex requires an active acquired image.");
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
    return result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR ||
           result == vk::Result::eErrorFullScreenExclusiveModeLostEXT;
}

void PresentationContext::recreate(const vk::raii::PhysicalDevice &physicalDevice, const vk::raii::Device &device,
                                   QueueManager &queueManager)
{
    queueManager.waitAllIdle();
    waitForPendingPresents();
    releaseFullScreenExclusiveIfNeeded();
    static_cast<void>(surface_.consumeSwapchainRecreateRequest());
    surface_.refreshExtentFromFramebuffer();
    refreshFullScreenExclusiveMonitor();
    ensureFullScreenExclusiveSupport(physicalDevice);
    auto oldSwapchain = *generation().swapChain.swapChain;
    auto rebuilt = SwapChain::create(physicalDevice, device, surface_.surface, surface_.extent, config_, oldSwapchain);
    generation_.reset();
    generation_.emplace(std::move(rebuilt), device);
    rebuildAcquirePool();
    acquireFullScreenExclusiveIfNeeded();
}

void PresentationContext::ensureFullScreenExclusiveSupport(const vk::raii::PhysicalDevice &physicalDevice) const
{
    if (!config_.fullScreenExclusiveApplicationControlled)
    {
        return;
    }

    nrAssert(config_.fullScreenExclusiveMonitor != 0,
             "VK_EXT_full_screen_exclusive requires a valid Win32 monitor handle before surface capability query.");
    auto win32Info = vk::SurfaceFullScreenExclusiveWin32InfoEXT{};
    win32Info.hmonitor = reinterpret_cast<decltype(win32Info.hmonitor)>(config_.fullScreenExclusiveMonitor);

    auto exclusiveInfo = vk::SurfaceFullScreenExclusiveInfoEXT{};
    exclusiveInfo.fullScreenExclusive = vk::FullScreenExclusiveEXT::eApplicationControlled;
    exclusiveInfo.pNext = std::addressof(win32Info);

    auto surfaceInfo = vk::PhysicalDeviceSurfaceInfo2KHR{};
    surfaceInfo.surface = *surface_.surface;
    surfaceInfo.pNext = std::addressof(exclusiveInfo);

    auto capabilities =
        physicalDevice
            .getSurfaceCapabilities2KHR<vk::SurfaceCapabilities2KHR, vk::SurfaceCapabilitiesFullScreenExclusiveEXT>(
                surfaceInfo);
    auto const &exclusiveCapabilities = capabilities.get<vk::SurfaceCapabilitiesFullScreenExclusiveEXT>();
    nrAssert(exclusiveCapabilities.fullScreenExclusiveSupported == vk::True,
             "VK_EXT_full_screen_exclusive reports that application-controlled exclusive mode is not supported for the "
             "current surface/monitor.");
}

void PresentationContext::refreshFullScreenExclusiveMonitor() noexcept
{
    config_.fullScreenExclusiveMonitor = surface_.fullscreenExclusiveMonitor();
    config_.fullScreenExclusiveApplicationControlled =
        config_.fullScreenExclusiveEnabled && surface_.fullscreenEnabled();
}

void PresentationContext::acquireFullScreenExclusiveIfNeeded()
{
    if (!config_.fullScreenExclusiveApplicationControlled || fullScreenExclusiveAcquired_)
    {
        return;
    }

    try
    {
        generation().swapChain.swapChain.acquireFullScreenExclusiveModeEXT();
        fullScreenExclusiveAcquired_ = true;
        nrLog<LogLevel::info>("VK_EXT_full_screen_exclusive acquired application-controlled exclusive mode.");
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::warning>("PresentationContext failed to acquire full-screen exclusive mode: {}", error.what());
        nrAssert(false, "PresentationContext failed to acquire VK_EXT_full_screen_exclusive mode.");
    }
}

void PresentationContext::releaseFullScreenExclusiveIfNeeded() noexcept
{
    if (!config_.fullScreenExclusiveEnabled || !fullScreenExclusiveAcquired_)
    {
        return;
    }

    fullScreenExclusiveAcquired_ = false;
    try
    {
        generation().swapChain.swapChain.releaseFullScreenExclusiveModeEXT();
    }
    catch (const vk::SystemError &error)
    {
        if (detail::isVulkanResult<vk::Result::eErrorFullScreenExclusiveModeLostEXT>(error))
        {
            nrLog<LogLevel::warning>("Full-screen exclusive mode was already lost before release: {}", error.what());
            return;
        }

        nrLog<LogLevel::warning>("PresentationContext failed to release full-screen exclusive mode: {}", error.what());
        nrAssert(false, "PresentationContext failed to release VK_EXT_full_screen_exclusive mode.");
    }
}

} // namespace nr::rhi
