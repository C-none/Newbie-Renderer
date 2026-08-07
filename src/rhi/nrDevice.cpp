module nr.rhi;
import :device;
import dependency.window;
import dependency.nsight;
import dependency.dlss;
import dependency.vulkan;
import :vk;
import :surface;
import :swapchain;
import :type;
import :queue;
import :frameContext;
import :command;
import :commandPool;
import :memoryAllocator;
import :nsightGraphics;
import :resourcePool;
import :pipeline;
import :resourceOps;
import :dlss;
import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
[[nodiscard]] double elapsedMilliseconds(std::chrono::steady_clock::time_point begin,
                                         std::chrono::steady_clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}
} // namespace

[[nodiscard]] const RayTracingCapabilitySnapshot &Device::rayTracingCapabilities() const noexcept
{
    return rtCapabilities_;
}

[[nodiscard]] const ops::QueueFamilyTransferPolicy &Device::queueFamilyTransferPolicy() const noexcept
{
    return queueFamilyTransferPolicy_;
}

[[nodiscard]] bool Device::frameBoundaryEnabled() const noexcept
{
    return frameBoundaryEnabled_;
}

[[nodiscard]] bool Device::hdrMetadataEnabled() const noexcept
{
    return hdrMetadataEnabled_;
}

[[nodiscard]] bool Device::nsightGraphicsEnabled() const noexcept
{
    return nsightGraphics_.enabled();
}

[[nodiscard]] bool Device::hasEnabledInstanceExtension(std::string_view extension) const
{
    return std::ranges::any_of(instanceEnabledExtensions,
                               [extension](const std::string &item) { return item == extension; });
}

[[nodiscard]] bool Device::hasEnabledDeviceExtension(std::string_view extension) const
{
    return std::ranges::any_of(enabledDeviceExtensions_,
                               [extension](const std::string &item) { return item == extension; });
}

void Device::initialize(std::string const &_appName, std::string const &_engineName)
{
    initialize(_appName, _engineName, std::filesystem::path{std::string{nr::psoCacheRoot}});
}

void Device::initialize(std::string const &_appName, std::string const &_engineName,
                        std::filesystem::path pipelineBinaryRoot)
{
    appName = _appName;
    engineName = _engineName;
    setupInitialFlags();
    nsightGraphics_.configureFromEnvironment();
    nsightGraphics_.injectIfRequested();
    instance = makeInstance();
    if constexpr (isDebugMode)
    {
        if (hasEnabledInstanceExtension(vk::EXTDebugUtilsExtensionName))
        {
            debugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(instance, makeDebugUtilsMessengerCreateInfoEXT());
        }
    }
    presentationContext.createSurface(instance, appName);
    physicalDevice =
        selectPhysicalDevice(instance, presentationContext.surfaceHandle(), requestedDeviceExtensions_);
    {
        auto gpuProps = physicalDevice.getProperties();
        nrLog<LogLevel::info>("Selected GPU: {}", gpuProps.deviceName.data());
    }
    if (nr::dependency::dlss::sdkCompiled())
    {
        auto const deviceExtensionQuery =
            nr::dependency::dlss::rayReconstructionDeviceExtensions(*instance, *physicalDevice);
        nrAssert(deviceExtensionQuery.status.success(),
                 "DLSS RR Vulkan device-extension discovery failed: {} (native code {}).",
                 deviceExtensionQuery.status.message, deviceExtensionQuery.status.nativeCode);
        auto addDeviceExtensionIfMissing = [&](std::string_view extension) {
            if (std::ranges::none_of(requestedDeviceExtensions_,
                                     [extension](const std::string &item) { return item == extension; }))
            {
                requestedDeviceExtensions_.emplace_back(extension);
            }
        };
        std::ranges::for_each(deviceExtensionQuery.names, addDeviceExtensionIfMissing);
    }
    device = makeDevice();

    memoryAllocator.initialize(instance, physicalDevice, device);
    resourceFactory.initialize(memoryAllocator, device);
    resourcePool.initialize(memoryAllocator, device);

    initializeCommandSystem();
    nsightGraphics_.initializeIfRequested(presentQueueRawForExternalTools());
    uploadReadbackContext_.emplace(device, resourceFactory, queueManager, queueFamilyTransferPolicy_);

    swapChainConfig_.hdrMetadataEnabled = hdrMetadataEnabled_;
    swapChainConfig_.fullScreenExclusiveEnabled = true;
    presentationContext.initializeSwapchain(physicalDevice, device, swapChainConfig_, presentQueueFamilyIndex());
    pipelineService.bindDevice(device, physicalDevice.getProperties().limits.maxBoundDescriptorSets, rtCapabilities_,
                               std::move(pipelineBinaryRoot));
}

[[nodiscard]] Device::FrameBeginResult Device::beginFrame()
{
    auto &frame = frameManager.current();
    const auto frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
    auto const waitGpuStart = std::chrono::steady_clock::now();
    nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");
    auto const cpuWaitGpuMilliseconds = elapsedMilliseconds(waitGpuStart, std::chrono::steady_clock::now());

    // After this frame slot's fence: its previous final submit has completed, meaning the
    // imageAvailable wait bound to THIS frame slot was executed. Return that slot to the pool.
    presentationContext.returnAcquireSemaphore(frameIndex);

    resourcePool.resetFrame(frameIndex);

    memoryAllocator.resetFramePool(frameIndex);

    frame.resetFence();

    frame.resetPools();

    constexpr auto minimumSecondaryPoolSlots = 2u;
    const auto secondaryPoolSlotCount =
        std::min<std::uint32_t>(maxThreads, std::max(minimumSecondaryPoolSlots, std::thread::hardware_concurrency()));
    frame.prepareSecondaryPools(secondaryPoolSlotCount, secondaryPoolSlotCount, secondaryPoolSlotCount);

    if (presentationContext.consumeSwapchainRecreateRequest())
    {
        recreateSwapchain();
    }

    nrAssert(!presentationContext.hasActiveSwapchainImage(),
             "Device::beginFrame requires the previous frame's active swapchain image to be cleared.");
    presentationContext.setFrameSubmitted(false);
    presentFrameBoundaryFrameID_.reset();
    frameAcquireRequiresRecreate_ = false;
    nsightGraphics_.beginFrame(frameBoundaryEnabled_);

    return FrameBeginResult{
        .frameIndex = frameIndex,
        .cpuWaitGpuMilliseconds = cpuWaitGpuMilliseconds,
    };
}

[[nodiscard]] Device::FrameAcquireResult Device::acquireFrameImage(std::uint64_t acquireTimeout)
{
    nrAssert(!presentationContext.hasActiveSwapchainImage(),
             "Device::acquireFrameImage can only acquire once per frame.");

    auto const frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
    auto acquire = presentationContext.acquireNextImage(frameIndex, acquireTimeout);
    auto recreatedSwapchain = false;
    if (PresentationContext::needsSwapchainRecreate(acquire.result) && acquire.result != vk::Result::eSuboptimalKHR)
    {
        recreateSwapchain();
        recreatedSwapchain = true;
        acquire = presentationContext.acquireNextImage(frameIndex, acquireTimeout);
    }

    nrAssert(acquire.result == vk::Result::eSuccess || acquire.result == vk::Result::eSuboptimalKHR,
             "Device::acquireFrameImage failed to acquire a valid swapchain image after recreation.");

    presentationContext.setActiveSwapchainImage(acquire.imageIndex);
    frameAcquireRequiresRecreate_ = PresentationContext::needsSwapchainRecreate(acquire.result);

    return FrameAcquireResult{
        .swapchainImageIndex = acquire.imageIndex,
        .swapchainResult = acquire.result,
        .recreatedSwapchain = recreatedSwapchain,
    };
}

void Device::submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent,
                              vk::PipelineStageFlags2 imageAvailableWaitStage)
{
    nrAssert(!presentationContext.hasSubmittedCurrentFrame(),
             "Device::submitFrameBatch cannot submit additional batches after final present-signaling submit.");

    if (signalForPresent)
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(),
                 "Device::submitFrameBatch final submission requires acquireFrameImage().");
        nrAssert(
            submitRole == presentSubmitRole(),
            "Device::submitFrameBatch compute-present policy requires the compute queue when signalForPresent=true.");
    }

    auto &frame = frameManager.current();

    // Keep pre-present work decoupled from swapchain availability.
    // Waiting on imageAvailable only at the present-signaling submit prevents vblank pacing
    // from stalling earlier GPU batches that do not touch the swapchain image.
    if (signalForPresent)
    {
        auto const frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
        batch.addWait(presentationContext.borrowedAcquireSemaphore(frameIndex), imageAvailableWaitStage);
    }

    if (signalForPresent)
    {
        batch.addSignal(presentationContext.activePresentSemaphore());
    }

    auto frameBoundaryFrameID = batch.frameBoundaryFrameID();
    auto submitToRole = [&](CommandBatch &&consumedBatch, QueueRole role,
                            std::optional<std::reference_wrapper<const vk::raii::Fence>> fence) {
        if (role == QueueRole::Graphics)
        {
            queueManager.graphics().submit(std::move(consumedBatch), fence);
            return;
        }
        if (role == QueueRole::Compute)
        {
            queueManager.compute().submit(std::move(consumedBatch), fence);
            return;
        }
        queueManager.transfer().submit(std::move(consumedBatch), fence);
    };

    auto fence = signalForPresent
                     ? std::optional<std::reference_wrapper<const vk::raii::Fence>>(std::cref(frame.fence()))
                     : std::nullopt;
    submitToRole(std::move(batch), submitRole, fence);

    if (signalForPresent)
    {
        presentationContext.setFrameSubmitted(true);
        presentFrameBoundaryFrameID_.reset();
        if (frameBoundaryEnabled_)
        {
            presentFrameBoundaryFrameID_ = frameBoundaryFrameID;
        }
    }
}

void Device::submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent)
{
    submitFrameBatch(std::move(batch), submitRole, signalForPresent,
                     vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands});
}

[[nodiscard]] PresentResult Device::presentFrame()
{
    nrAssert(presentationContext.hasActiveSwapchainImage(),
             "Device::presentFrame requires beginFrame() before present.");
    nrAssert(presentationContext.hasSubmittedCurrentFrame(),
             "Device::presentFrame compute-present policy requires a final submission that signals the active present "
             "semaphore.");

    auto const presentImage = activeSwapchainImageRawForExternalTools();
    nsightGraphics_.stopTraceBeforeBoundaryIfNeeded(presentImage);

    auto presentResult = presentationContext.present(queueManager, presentFrameBoundaryFrameID_);
    nsightGraphics_.markFrameBoundaryAfterPresent(presentResult.result, presentImage);

    auto const recreateRequested = presentationContext.consumeSwapchainRecreateRequest();
    if (frameAcquireRequiresRecreate_ || PresentationContext::needsSwapchainRecreate(presentResult.result) ||
        recreateRequested)
    {
        if (presentationContext.framebufferAvailable())
        {
            recreateSwapchain();
        }
    }

    frameManager.advanceFrame();
    presentationContext.clearActiveSwapchainImage();
    presentationContext.setFrameSubmitted(false);
    presentFrameBoundaryFrameID_.reset();
    frameAcquireRequiresRecreate_ = false;

    return presentResult;
}

vk::raii::Instance Device::makeInstance(std::uint32_t apiVersion) const
{
    const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
    std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
    std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);

    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    vk::InstanceCreateInfo instanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions);
    if constexpr (isDebugMode)
    {
        const void *debugPNext = nullptr;
        if (hasEnabledInstanceExtension(vk::EXTDebugUtilsExtensionName))
        {
            debugCreateInfo = makeDebugUtilsMessengerCreateInfoEXT();
            debugPNext = &debugCreateInfo;
        }
        DebugValidationLayerSettings validationLayerSettings;
        auto validationLayerSettingsCreateInfo = validationLayerSettings.createInfo(debugPNext);
        instanceCreateInfo.pNext = &validationLayerSettingsCreateInfo;
        nrLog<LogLevel::info>("Debug Vulkan validation layer settings enabled programmatically: "
                              "Core, Sync Validation, GPU-AV={}, DebugPrintf={}, "
                              "report_flags=verbose/error/perf/info/warn, "
                              "debug_action=none (routed through nrVulkan callback), "
                              "duplicate message limit disabled. "
                              "GPU-assisted validation and debug printf are enabled by default.",
                              validationLayerSettings.gpuAssistedValidationEnabled() ? "on" : "off",
                              validationLayerSettings.debugPrintfEnabled() ? "on" : "off");
        return vk::raii::Instance(context, instanceCreateInfo);
    }
    return vk::raii::Instance(context, instanceCreateInfo);
}

vk::raii::Device Device::makeDevice()
{
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    std::ranges::fill(queueFamilyDict, std::numeric_limits<std::size_t>::max());

    auto queueIndices = std::views::iota(std::size_t{0}, queueFamilyProperties.size());
    auto presentSupport = queueIndices | std::views::transform([&](std::size_t index) {
                              return physicalDevice.getSurfaceSupportKHR(static_cast<std::uint32_t>(index),
                                                                         presentationContext.surfaceHandle())
                                         ? vk::True
                                         : vk::False;
                          }) |
                          std::ranges::to<std::vector>();
    auto queueFamilies = selectRequiredQueueFamilies(queueFamilyProperties, presentSupport);
    nrAssert(queueFamilies.has_value(),
             "Selected GPU does not expose required graphics, present-capable compute, and dedicated physical "
             "copy/transfer queue families.");

    auto toQueueIndex = [](QueueFamilyKind kind) { return static_cast<std::size_t>(kind); };
    queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)] = queueFamilies->graphics;
    queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)] = queueFamilies->compute;
    queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)] = queueFamilies->transfer;

    auto queueFamilySummary = [&](std::uint32_t familyIndex) {
        const auto &family = queueFamilyProperties[familyIndex];
        return std::format("index={} flags={} queueCount={}", familyIndex, vk::to_string(family.queueFlags),
                           family.queueCount);
    };
    auto queueFamilySelectionMessage =
        std::format("Vulkan queue family selection: graphics{{{}}} compute{{{}}} transfer{{{}}}",
                    queueFamilySummary(queueFamilies->graphics), queueFamilySummary(queueFamilies->compute),
                    queueFamilySummary(queueFamilies->transfer));
    nrLog<LogLevel::info>("{}", queueFamilySelectionMessage);

    constexpr float queuePriority = 1.0f;
    auto uniqueFamilies =
        std::array{static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)]),
                   static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]),
                   static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)])};
    std::ranges::sort(uniqueFamilies);

    auto queueCreateInfos = uniqueFamilies | std::views::filter([last = std::uint32_t(-1)](std::uint32_t f) mutable {
                                if (f == last)
                                    return false;
                                last = f;
                                return true;
                            }) |
                            std::views::transform([&](std::uint32_t familyIndex) {
                                return vk::DeviceQueueCreateInfo({}, familyIndex, 1, &queuePriority);
                            }) |
                            std::ranges::to<std::vector>();

    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    auto isExtensionSupported = [&availableExtensions](std::string_view extensionName) {
        return std::ranges::any_of(availableExtensions, [extensionName](const vk::ExtensionProperties &property) {
            return std::string_view(property.extensionName) == extensionName;
        });
    };

    enabledDeviceExtensions_.clear();
    std::vector<char const *> enabledExtensions;
    enabledExtensions.reserve(requestedDeviceExtensions_.size() + 2u);
    std::set<std::string_view> enabledExtensionSet;

    auto enableExtension = [&](std::string_view extensionName, std::string_view reason) {
        if (!isExtensionSupported(extensionName))
        {
            nrAssert(false, "Required device extension '{}' is not supported ({})", extensionName, reason);
            return false;
        }
        if (enabledExtensionSet.insert(extensionName).second)
        {
            enabledExtensions.push_back(extensionName.data());
        }
        return true;
    };

    std::ranges::for_each(requestedDeviceExtensions_, [&](std::string_view extensionName) {
        auto reason = std::string_view{"modern pipeline backend"};
        if (extensionName == vk::KHRMaintenance8ExtensionName)
            reason = "precise queue-family ownership transfer synchronization scopes";
        if (extensionName == vk::KHRMaintenance9ExtensionName)
            reason = "maintenance9 queue-family ownership transfer rules";
        if (extensionName == vk::EXTRayTracingInvocationReorderExtensionName)
            reason = "path-tracing shader invocation reordering";
        if (extensionName == vk::EXTFullScreenExclusiveExtensionName)
            reason = "application-controlled fullscreen exclusive swapchain ownership";
        if (extensionName == vk::EXTSwapchainMaintenance1ExtensionName)
            reason = "per-present completion fences and swapchain generation retirement";
        enableExtension(extensionName, reason);
    });

    auto const frameBoundaryExtensionSupported = isExtensionSupported(vk::EXTFrameBoundaryExtensionName);
    auto const hdrMetadataExtensionSupported = isExtensionSupported(vk::EXTHdrMetadataExtensionName);
    auto frameBoundaryFeatureSupported = false;
    if (frameBoundaryExtensionSupported)
    {
        auto frameBoundaryFeatureQuery =
            physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
        auto const &frameBoundaryFeatures = frameBoundaryFeatureQuery.get<vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
        frameBoundaryFeatureSupported = frameBoundaryFeatures.frameBoundary == vk::True;
    }

    auto supportedFeatures = physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDevicePipelineBinaryFeaturesKHR,
        vk::PhysicalDeviceMaintenance8FeaturesKHR, vk::PhysicalDeviceMaintenance9FeaturesKHR,
        vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
        vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT>();

    auto requestedFeatures = vk::StructureChain<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDevicePipelineBinaryFeaturesKHR,
        vk::PhysicalDeviceMaintenance8FeaturesKHR, vk::PhysicalDeviceMaintenance9FeaturesKHR,
        vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
        vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT>{};

    auto const requireFeature = [](vk::Bool32 supported, std::string_view featureName) {
        nrAssert(supported == vk::True, "Required Vulkan device feature '{}' is not supported.", featureName);
    };

    auto const &supportedCore = supportedFeatures.get<vk::PhysicalDeviceFeatures2>().features;
    auto &requestedCore = requestedFeatures.get<vk::PhysicalDeviceFeatures2>().features;
    requireFeature(supportedCore.shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat");
    requestedCore.shaderStorageImageReadWithoutFormat = vk::True;
    requireFeature(supportedCore.shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat");
    requestedCore.shaderStorageImageWriteWithoutFormat = vk::True;

    auto const &supportedVulkan11 = supportedFeatures.get<vk::PhysicalDeviceVulkan11Features>();
    auto &requestedVulkan11 = requestedFeatures.get<vk::PhysicalDeviceVulkan11Features>();
    requireFeature(supportedVulkan11.shaderDrawParameters, "shaderDrawParameters");
    requestedVulkan11.shaderDrawParameters = vk::True;

    auto const &supportedVulkan12 = supportedFeatures.get<vk::PhysicalDeviceVulkan12Features>();
    auto &requestedVulkan12 = requestedFeatures.get<vk::PhysicalDeviceVulkan12Features>();
    requireFeature(supportedVulkan12.bufferDeviceAddress, "bufferDeviceAddress");
    requestedVulkan12.bufferDeviceAddress = vk::True;
    requireFeature(supportedVulkan12.descriptorIndexing, "descriptorIndexing");
    requestedVulkan12.descriptorIndexing = vk::True;
    requireFeature(supportedVulkan12.runtimeDescriptorArray, "runtimeDescriptorArray");
    requestedVulkan12.runtimeDescriptorArray = vk::True;
    requireFeature(supportedVulkan12.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
    requestedVulkan12.descriptorBindingPartiallyBound = vk::True;
    requireFeature(supportedVulkan12.descriptorBindingVariableDescriptorCount,
                   "descriptorBindingVariableDescriptorCount");
    requestedVulkan12.descriptorBindingVariableDescriptorCount = vk::True;
    requireFeature(supportedVulkan12.scalarBlockLayout, "scalarBlockLayout");
    requestedVulkan12.scalarBlockLayout = vk::True;
    requireFeature(supportedVulkan12.shaderSampledImageArrayNonUniformIndexing,
                   "shaderSampledImageArrayNonUniformIndexing");
    requestedVulkan12.shaderSampledImageArrayNonUniformIndexing = vk::True;
    requireFeature(supportedVulkan12.timelineSemaphore, "timelineSemaphore");
    requestedVulkan12.timelineSemaphore = vk::True;

    auto const &supportedVulkan13 = supportedFeatures.get<vk::PhysicalDeviceVulkan13Features>();
    auto &requestedVulkan13 = requestedFeatures.get<vk::PhysicalDeviceVulkan13Features>();
    requireFeature(supportedVulkan13.inlineUniformBlock, "inlineUniformBlock");
    requestedVulkan13.inlineUniformBlock = vk::True;
    requireFeature(supportedVulkan13.dynamicRendering, "dynamicRendering");
    requestedVulkan13.dynamicRendering = vk::True;
    requireFeature(supportedVulkan13.synchronization2, "synchronization2");
    requestedVulkan13.synchronization2 = vk::True;
    requireFeature(supportedVulkan13.maintenance4, "maintenance4");
    requestedVulkan13.maintenance4 = vk::True;

    auto const &supportedExtendedDynamicState =
        supportedFeatures.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    auto &requestedExtendedDynamicState =
        requestedFeatures.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
    requireFeature(supportedExtendedDynamicState.extendedDynamicState, "extendedDynamicState");
    requestedExtendedDynamicState.extendedDynamicState = vk::True;

    auto const &supportedPipelineBinary = supportedFeatures.get<vk::PhysicalDevicePipelineBinaryFeaturesKHR>();
    auto &requestedPipelineBinary = requestedFeatures.get<vk::PhysicalDevicePipelineBinaryFeaturesKHR>();
    requireFeature(supportedPipelineBinary.pipelineBinaries, "pipelineBinaries");
    requestedPipelineBinary.pipelineBinaries = vk::True;

    auto const &supportedVulkan14 = supportedFeatures.get<vk::PhysicalDeviceVulkan14Features>();
    auto &requestedVulkan14 = requestedFeatures.get<vk::PhysicalDeviceVulkan14Features>();
    requireFeature(supportedVulkan14.maintenance5, "maintenance5");
    requestedVulkan14.maintenance5 = vk::True;
    if (enabledExtensionSet.contains("VK_KHR_push_descriptor"))
    {
        requireFeature(supportedVulkan14.pushDescriptor, "pushDescriptor");
        requestedVulkan14.pushDescriptor = vk::True;
    }

    auto const &supportedMaintenance8 = supportedFeatures.get<vk::PhysicalDeviceMaintenance8FeaturesKHR>();
    auto &requestedMaintenance8 = requestedFeatures.get<vk::PhysicalDeviceMaintenance8FeaturesKHR>();
    requireFeature(supportedMaintenance8.maintenance8, "maintenance8");
    requestedMaintenance8.maintenance8 = vk::True;

    auto const &supportedMaintenance9 = supportedFeatures.get<vk::PhysicalDeviceMaintenance9FeaturesKHR>();
    auto &requestedMaintenance9 = requestedFeatures.get<vk::PhysicalDeviceMaintenance9FeaturesKHR>();
    requireFeature(supportedMaintenance9.maintenance9, "maintenance9");
    requestedMaintenance9.maintenance9 = vk::True;

    auto const &supportedInvocationReorder =
        supportedFeatures.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT>();
    auto &requestedInvocationReorder =
        requestedFeatures.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT>();
    requireFeature(supportedInvocationReorder.rayTracingInvocationReorder, "rayTracingInvocationReorder");
    requestedInvocationReorder.rayTracingInvocationReorder = vk::True;

    auto const &supportedAccelerationStructure =
        supportedFeatures.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
    auto &requestedAccelerationStructure =
        requestedFeatures.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
    requireFeature(supportedAccelerationStructure.accelerationStructure, "accelerationStructure");
    requestedAccelerationStructure.accelerationStructure = vk::True;

    auto const &supportedRayTracingPipeline =
        supportedFeatures.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
    auto &requestedRayTracingPipeline =
        requestedFeatures.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
    requireFeature(supportedRayTracingPipeline.rayTracingPipeline, "rayTracingPipeline");
    requestedRayTracingPipeline.rayTracingPipeline = vk::True;

    auto const &supportedSwapchainMaintenance1 =
        supportedFeatures.get<vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT>();
    auto &requestedSwapchainMaintenance1 =
        requestedFeatures.get<vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT>();
    requireFeature(supportedSwapchainMaintenance1.swapchainMaintenance1, "swapchainMaintenance1");
    requestedSwapchainMaintenance1.swapchainMaintenance1 = vk::True;

    auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                                     vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    auto const &physicalDeviceProperties = properties2.get<vk::PhysicalDeviceProperties2>();
    auto const &rayTracingPipelineProperties =
        properties2.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

    auto queueOwnershipPropertyChains = physicalDevice.getQueueFamilyProperties2<
        vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyOwnershipTransferPropertiesKHR>>();
    auto ownershipTransferMasks = queueOwnershipPropertyChains | std::views::transform([](const auto &chain) {
                                      return chain.template get<vk::QueueFamilyOwnershipTransferPropertiesKHR>()
                                          .optimalImageTransferToQueueFamilies;
                                  }) |
                                  std::ranges::to<std::vector>();
    nrAssert(ownershipTransferMasks.size() == queueFamilyProperties.size(),
             "VK_KHR_maintenance9 queue-family ownership transfer property query returned an unexpected family count.");
    queueFamilyTransferPolicy_ = nr::rhi::ops::QueueFamilyTransferPolicy{
        .maintenance9 = true,
        .optimalImageTransferToQueueFamilies = std::move(ownershipTransferMasks),
    };

    auto frameBoundaryCreateFeatures = vk::PhysicalDeviceFrameBoundaryFeaturesEXT{};
    frameBoundaryEnabled_ = frameBoundaryExtensionSupported && frameBoundaryFeatureSupported;
    if (frameBoundaryEnabled_)
    {
        enableExtension(vk::EXTFrameBoundaryExtensionName, "graphics debugger frame boundary metadata");
        frameBoundaryCreateFeatures.frameBoundary = vk::True;
        auto &requestedFeatureList = requestedFeatures.get<vk::PhysicalDeviceFeatures2>();
        frameBoundaryCreateFeatures.pNext = requestedFeatureList.pNext;
        requestedFeatureList.pNext = std::addressof(frameBoundaryCreateFeatures);
        nrLog<LogLevel::info>("VK_EXT_frame_boundary enabled for graphics debugger frame capture.");
    }
    else if (frameBoundaryExtensionSupported)
    {
        nrLog<LogLevel::warning>(
            "VK_EXT_frame_boundary was exposed without its frameBoundary feature; frame-boundary tagging is disabled.");
    }

    hdrMetadataEnabled_ = false;
    if (hdrMetadataExtensionSupported)
    {
        hdrMetadataEnabled_ = enableExtension(vk::EXTHdrMetadataExtensionName, "HDR10 swapchain metadata");
    }
    else
    {
        nrLog<LogLevel::info>(
            "VK_EXT_hdr_metadata is unavailable; HDR swapchain output can still run without presentation metadata.");
    }

    auto const &limits = physicalDeviceProperties.properties.limits;
    rtCapabilities_ = RayTracingCapabilitySnapshot{
        .shaderGroupHandleSize = rayTracingPipelineProperties.shaderGroupHandleSize,
        .shaderGroupHandleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment,
        .shaderGroupBaseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment,
        .maxShaderGroupStride = rayTracingPipelineProperties.maxShaderGroupStride,
        .maxRayDispatchInvocationCount = rayTracingPipelineProperties.maxRayDispatchInvocationCount,
        .maxRayRecursionDepth = rayTracingPipelineProperties.maxRayRecursionDepth,
        .maxDispatchDimensions =
            {
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[0]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[0]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[1]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[1]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[2]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[2]),
            },
    };

    auto &requestedFeatureList = requestedFeatures.get<vk::PhysicalDeviceFeatures2>();
    vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos,
                                          {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions,
                                          nullptr, &requestedFeatureList);
    auto enabledExtensionNames = enabledExtensions | std::views::transform([](const char *extensionName) {
                                     return std::string{extensionName};
                                 }) |
                                 std::ranges::to<std::vector>();
    try
    {
        auto logicalDevice = vk::raii::Device(physicalDevice, deviceCreateInfo);
        enabledDeviceExtensions_ = std::move(enabledExtensionNames);
        return logicalDevice;
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::error, "LOG">("Vulkan logical-device creation failed: {}", error.what());
        return {nullptr};
    }
}

void Device::initializeCommandSystem()
{
    std::uint32_t graphicsFamily = requiredQueueFamily(QueueFamilyKind::graphics);
    std::uint32_t computeFamily = requiredQueueFamily(QueueFamilyKind::compute);
    std::uint32_t transferFamily = requiredQueueFamily(QueueFamilyKind::transfer);

    GpuQueue graphicsQueue(device, graphicsFamily);
    GpuQueue computeQueue(device, computeFamily);
    GpuQueue transferQueue(device, transferFamily);

    queueManager = QueueManager(std::move(graphicsQueue), std::move(computeQueue), std::move(transferQueue));

    FrameContext::PoolConfig graphicsConfig{.queueFamilyIndex = graphicsFamily};
    FrameContext::PoolConfig computeConfig{.queueFamilyIndex = computeFamily};
    FrameContext::PoolConfig transferConfig{.queueFamilyIndex = transferFamily};

    frameManager = FrameManager(device, graphicsConfig, computeConfig, transferConfig);
}

void Device::waitIdle()
{
    queueManager.waitAllIdle();
    frameManager.waitAll();
}

void Device::recreateSwapchain()
{
    presentationContext.recreate(physicalDevice, device, queueManager);
    ++swapchainRecreationGeneration_;
}

[[nodiscard]] std::uint64_t Device::swapchainRecreationGeneration() const noexcept
{
    return swapchainRecreationGeneration_;
}

[[nodiscard]] PipelineService &Device::pipeline() noexcept
{
    return pipelineService;
}

[[nodiscard]] const PipelineService &Device::pipeline() const noexcept
{
    return pipelineService;
}

[[nodiscard]] ShaderService &Device::shaderCompiler() const
{
    return ShaderService::instance();
}

[[nodiscard]] ops::UploadReadbackContext &Device::uploadReadback() noexcept
{
    nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
    return *uploadReadbackContext_;
}

[[nodiscard]] const ops::UploadReadbackContext &Device::uploadReadback() const noexcept
{
    nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
    return *uploadReadbackContext_;
}

[[nodiscard]] std::shared_ptr<DlssContext> Device::dlssContext()
{
    nrAssert(dlssSdkCompiled(), "DLSS execution was requested, but the deployed NGX bridge is unavailable. Configure "
                                "with NR_ENABLE_DLSS_NGX_SDK=ON and deploy the validated bridge artifact.");
    if (!dlssContext_)
    {
        auto pathError = std::error_code{};
        auto applicationDataPath = std::filesystem::current_path(pathError);
        nrAssert(!pathError, "DLSS NGX application-data path resolution failed: {}", pathError.message());
        applicationDataPath /= "ngx";
        dlssContext_ = std::make_shared<DlssContext>(static_cast<vk::Instance>(*instance),
                                                     static_cast<vk::PhysicalDevice>(*physicalDevice),
                                                     static_cast<vk::Device>(*device), std::move(applicationDataPath));
        nrAssert(dlssContext_->valid(), "DLSS NGX context initialization failed: {}", dlssContext_->status().message);
    }
    return dlssContext_;
}

[[nodiscard]] std::unique_ptr<DlssRayReconstructionFeature> Device::createDlssRayReconstructionFeature(
    const DlssRayReconstructionCreateDesc &desc)
{
    auto sharedContext = dlssContext();
    // Prepare runs after beginFrame(), whose current fence is intentionally
    // unsignaled. Wait only for queues here; waiting every frame fence would
    // deadlock before the current frame has been submitted.
    queueManager.waitAllIdle();

    auto commandPool =
        CommandPool{device, queueManager.compute().queueFamilyIndex(), vk::CommandPoolCreateFlagBits::eTransient};
    auto commandBuffers = commandPool.allocatePrimary();
    nrAssert(!commandBuffers.empty(), "DLSS RR feature creation failed to allocate a command buffer.");

    std::unique_ptr<DlssRayReconstructionFeature> feature{};
    {
        auto recording = ScopedCommandBuffer{commandBuffers.front(), vk::CommandBufferUsageFlagBits::eOneTimeSubmit};
        feature = std::make_unique<DlssRayReconstructionFeature>(std::move(sharedContext), recording.get(), desc);
    }
    nrAssert(feature->valid(), "DLSS RR feature creation failed: {}", feature->status().message);
    queueManager.compute().submit(commandBuffers.front());
    queueManager.compute().waitIdle();
    return feature;
}

Device::~Device()
{
    if (*device != nullptr)
    {
        waitIdle();
        dlssContext_.reset();
        uploadReadbackContext_.reset();
    }
}

[[nodiscard]] VkQueue Device::presentQueueRawForExternalTools() const noexcept
{
    return static_cast<VkQueue>(*queueManager.compute().handle());
}

[[nodiscard]] VkImage Device::activeSwapchainImageRawForExternalTools() const
{
    auto const imageIndex = presentationContext.activeSwapchainImageIndex();
    return static_cast<VkImage>(presentationContext.swapchainImage(imageIndex));
}

void Device::setupInitialFlags()
{
    Surface::ensureGlfwInitialized();

    std::uint32_t glfwCount = 0;
    const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
    nrAssert(glfwExt != nullptr && glfwCount > 0, "GLFW did not report Vulkan instance extensions.");
    instanceEnabledExtensions.assign(glfwExt, glfwExt + glfwCount);

    auto addIfMissing = [](std::vector<std::string> &list, std::string_view item) {
        if (std::ranges::none_of(list, [item](const auto &s) { return s == item; }))
            list.push_back(std::string(item));
    };

    if (nr::dependency::dlss::sdkCompiled())
    {
        auto const instanceExtensionQuery = nr::dependency::dlss::rayReconstructionInstanceExtensions();
        nrAssert(instanceExtensionQuery.status.success(),
                 "DLSS RR Vulkan instance-extension discovery failed: {} (native code {}).",
                 instanceExtensionQuery.status.message, instanceExtensionQuery.status.nativeCode);
        std::ranges::for_each(instanceExtensionQuery.names,
                              [&](std::string_view extension) { addIfMissing(instanceEnabledExtensions, extension); });
    }

    if (hasInstanceExtension(vk::EXTSwapchainColorSpaceExtensionName))
    {
        addIfMissing(instanceEnabledExtensions, vk::EXTSwapchainColorSpaceExtensionName);
    }
    else
    {
        nrLog<LogLevel::info>(
            "VK_EXT_swapchain_colorspace is unavailable; swapchain format selection is limited to core color spaces.");
    }

    nrAssert(
        hasInstanceExtension(vk::KHRGetSurfaceCapabilities2ExtensionName),
        "VK_KHR_get_surface_capabilities2 is required for VK_EXT_full_screen_exclusive surface capability queries.");
    addIfMissing(instanceEnabledExtensions, vk::KHRGetSurfaceCapabilities2ExtensionName);

    nrAssert(hasInstanceExtension(vk::EXTSurfaceMaintenance1ExtensionName),
             "VK_EXT_surface_maintenance1 is required by VK_EXT_swapchain_maintenance1.");
    addIfMissing(instanceEnabledExtensions, vk::EXTSurfaceMaintenance1ExtensionName);

    if constexpr (isDebugMode || gpuDebugNamesEnabled)
    {
        if constexpr (isDebugMode)
        {
            constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
            nrAssert(
                hasInstanceLayer(validationLayer),
                "Debug builds require '{}'. The Vulkan loader did not enumerate this layer on the current machine. "
                "Validation layers are provided by the Vulkan SDK / validation-layer installation, not by the GPU "
                "or display driver.",
                validationLayer);
            addIfMissing(instanceEnabledLayers, validationLayer);
        }

        if (hasInstanceExtension(vk::EXTDebugUtilsExtensionName))
        {
            addIfMissing(instanceEnabledExtensions, vk::EXTDebugUtilsExtensionName);
        }
        else
        {
            nrLog<LogLevel::warning>("VK_EXT_debug_utils is unavailable; validation callbacks, debug labels, and object "
                                    "names require this extension.");
            nrAssert(false, "VK_EXT_debug_utils is required when validation or GPU debug names are enabled.");
        }
    }
}

[[nodiscard]] std::uint32_t Device::requiredQueueFamily(QueueFamilyKind kind) const
{
    std::size_t index = static_cast<std::size_t>(kind);
    std::size_t familyIndex = queueFamilyDict[index];
    nrAssert(familyIndex != std::numeric_limits<std::size_t>::max(),
             "Queue family not found - device capability contract violated.");
    return static_cast<std::uint32_t>(familyIndex);
}

[[nodiscard]] std::uint32_t Device::presentQueueFamilyIndex() const
{
    return requiredQueueFamily(QueueFamilyKind::compute);
}

} // namespace nr::rhi
