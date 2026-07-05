module nr.rhi;
import :device;
import dependency.window;
import dependency.nsight;
import dependency.vulkan;
import :vk;
import :surface;
import :swapchain;
import :type;
import :queue;
import :frameContext;
import :memoryAllocator;
import :nsightGraphics;
import :resourcePool;
import :pipeline;
import :resourceOps;
import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
[[nodiscard]] double elapsedMilliseconds(
    std::chrono::steady_clock::time_point begin,
    std::chrono::steady_clock::time_point end) noexcept
{
        return std::chrono::duration<double, std::milli>(end - begin).count();
    }
} // namespace

[[nodiscard]] const RayTracingCapabilitySnapshot &Device::rayTracingCapabilities() const noexcept
{
        return rtCapabilities_;
    }

[[nodiscard]] const DescriptorIndexingCapabilitySnapshot &Device::descriptorIndexingCapabilities() const noexcept
{
        return descriptorIndexingCapabilities_;
    }

[[nodiscard]] const BufferDeviceAddressCapabilitySnapshot &Device::bufferDeviceAddressCapabilities() const noexcept
{
        return bufferDeviceAddressCapabilities_;
    }

[[nodiscard]] const Vulkan14CapabilitySnapshot &Device::vulkan14Capabilities() const noexcept
{
        return vulkan14Capabilities_;
    }

[[nodiscard]] const Vulkan14PropertySnapshot &Device::vulkan14Properties() const noexcept
{
        return vulkan14Properties_;
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
        return std::ranges::any_of(instanceEnabledExtensions, [extension](const std::string &item) { return item == extension; });
    }

[[nodiscard]] bool Device::hasEnabledDeviceExtension(std::string_view extension) const
{
        return std::ranges::any_of(deviceEnabledExtensions, [extension](const std::string &item) { return item == extension; });
    }

void Device::initialize(std::string const &_appName, std::string const &_engineName)
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
        physicalDevice = selectPhysicalDevice(instance);
        {
            auto gpuProps = physicalDevice.getProperties();
            nrInfo<>(std::format("Selected GPU: {}", gpuProps.deviceName.data()));
        }
        try
        {
            device = makeDevice();
        }
        catch (const vk::SystemError& error)
        {
            nrLog(
                LogLevel::error,
                std::format("Device::initialize failed while creating the Vulkan logical device: {}", error.what()),
                std::source_location::current(),
                true);
        }
        catch (const std::exception& error)
        {
            nrLog(
                LogLevel::error,
                std::format("Device::initialize failed while creating the Vulkan logical device: {}", error.what()),
                std::source_location::current(),
                true);
        }

        memoryAllocator.initialize(instance, physicalDevice, device);
        resourceFactory.initialize(memoryAllocator, device);
        resourcePool.initialize(memoryAllocator, device);

        initializeCommandSystem();
        nsightGraphics_.initializeIfRequested(presentQueueRawForExternalTools());
        uploadReadbackContext_.emplace(device, resourceFactory, queueManager, queueFamilyTransferPolicy_);

        swapChainConfig_.hdrMetadataEnabled = hdrMetadataEnabled_;
        presentationContext.initialize(instance, physicalDevice, device, appName, swapChainConfig_, presentQueueFamilyIndex());
        refreshPresentSemaphores();
        pipelineService.bindDevice(device, std::cref(rtCapabilities_));

        // Prime the first frame's acquire so beginFrame() can immediately consume it.
        presentationContext.issueFirstAcquire();
    }

[[nodiscard]] Device::FrameBeginResult Device::beginFrame(std::uint64_t acquireTimeout)
{
        auto &frame = frameManager.current();
        const auto frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
        auto const waitGpuStart = std::chrono::steady_clock::now();
        nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");
        auto const cpuWaitGpuMilliseconds = elapsedMilliseconds(
            waitGpuStart,
            std::chrono::steady_clock::now());

        // After this frame slot's fence: its previous final submit has completed, meaning the
        // imageAvailable wait bound to THIS frame slot was executed. Return that slot to the pool.
        presentationContext.returnAcquireSemaphore(frameIndex);

        resourcePool.resetFrame(frameIndex);

        memoryAllocator.resetFramePool(frameIndex);

        frame.resetFence();

        frame.resetPools();

        constexpr auto minimumSecondaryPoolSlots = 2u;
        const auto secondaryPoolSlotCount = std::min<std::uint32_t>(
            maxThreads,
            std::max(minimumSecondaryPoolSlots, std::thread::hardware_concurrency()));
        frame.prepareSecondaryPools(secondaryPoolSlotCount, secondaryPoolSlotCount, secondaryPoolSlotCount);

        if (presentationContext.consumeSwapchainRecreateRequest())
        {
            recreateSwapchain();
        }

        // Consume the pre-acquired image (issued at end of previous presentFrame or initialize).
        // If the pending acquire is absent (e.g., after a recreate that failed), issue now.
        if (!presentationContext.hasPendingAcquire())
        {
            presentationContext.issueNextAcquire(acquireTimeout);
        }

        auto acquire = presentationContext.consumePendingAcquire(frameIndex);

        if (PresentationContext::needsSwapchainRecreate(acquire.result))
        {
            recreateSwapchain();
            presentationContext.issueNextAcquire(acquireTimeout);
            acquire = presentationContext.consumePendingAcquire(frameIndex);
        }

        nrAssert(acquire.result != vk::Result::eErrorOutOfDateKHR, "Device::beginFrame failed to acquire a valid swapchain image after recreation.");

        // Inject the borrowed semaphore into the current frame context for use in submitFrameBatch.
        frame.setBorrowedAcquireSemaphore(&presentationContext.borrowedAcquireSemaphore(frameIndex));

        presentationContext.setActiveSwapchainImage(acquire.imageIndex);
        presentationContext.setFrameSubmitted(false);
        frameSubmitCount_ = 0;
        frameFinalSubmitRole_.reset();
        presentFrameBoundaryFrameID_.reset();
        nsightGraphics_.beginFrame(frameBoundaryEnabled_);

        return FrameBeginResult{
            .frameIndex = frameIndex,
            .swapchainImageIndex = acquire.imageIndex,
            .swapchainResult = acquire.result,
            .cpuWaitGpuMilliseconds = cpuWaitGpuMilliseconds,
        };
    }

void Device::submitFrameBatch(CommandBatch&& batch, QueueRole submitRole, bool signalForPresent, vk::PipelineStageFlags2 imageAvailableWaitStage)
{
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::submitFrameBatch requires beginFrame() before submission.");
        nrAssert(!(frameFinalSubmitRole_.has_value() && !signalForPresent), "Device::submitFrameBatch cannot submit additional batches after final present-signaling submit.");

        if (signalForPresent)
        {
            nrAssert(!frameFinalSubmitRole_.has_value(), "Device::submitFrameBatch final present-signaling submit can only happen once per frame.");
            nrAssert(submitRole == presentSubmitRole(), "Device::submitFrameBatch compute-present policy requires the compute queue when signalForPresent=true.");
        }

        auto &frame = frameManager.current();

        // Keep pre-present work decoupled from swapchain availability.
        // Waiting on imageAvailable only at the present-signaling submit prevents vblank pacing
        // from stalling earlier GPU batches that do not touch the swapchain image.
        if (signalForPresent)
        {
            batch.addWait(frame.imageAvailable(), imageAvailableWaitStage);
        }

        if (signalForPresent)
        {
            batch.addSignal(activePresentSemaphore());
        }

        auto frameBoundaryFrameID = batch.frameBoundaryFrameID();
        auto submitToRole = [&](CommandBatch&& consumedBatch, QueueRole role, std::optional<std::reference_wrapper<const vk::raii::Fence>> fence) {
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

        auto fence = signalForPresent ? std::optional<std::reference_wrapper<const vk::raii::Fence>>(std::cref(frame.fence())) : std::nullopt;
        submitToRole(std::move(batch), submitRole, fence);

        ++frameSubmitCount_;

        if (signalForPresent)
        {
            frameFinalSubmitRole_ = submitRole;
            presentationContext.setFrameSubmitted(true);
            presentFrameBoundaryFrameID_.reset();
            if (frameBoundaryEnabled_)
            {
                presentFrameBoundaryFrameID_ = frameBoundaryFrameID;
            }
        }
    }

void Device::submitFrameBatch(CommandBatch&& batch, QueueRole submitRole, bool signalForPresent)
{
        submitFrameBatch(std::move(batch), submitRole, signalForPresent, vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands});
    }

void Device::submitFrame(CommandBatch&& batch, QueueRole submitRole)
{
        submitFrameBatch(std::move(batch), submitRole, true);
    }

[[nodiscard]] bool Device::canPresentCurrentFrame() const noexcept
{
        return frameSubmitCount_ > 0 && frameFinalSubmitRole_.has_value() && *frameFinalSubmitRole_ == presentSubmitRole() && presentationContext.hasSubmittedCurrentFrame();
    }

[[nodiscard]] QueueRole Device::submitRoleForPresent() const noexcept
{
        return frameFinalSubmitRole_.value_or(presentSubmitRole());
    }

[[nodiscard]] std::uint32_t Device::frameSubmitCount() const noexcept
{
        return frameSubmitCount_;
    }

[[nodiscard]] PresentResult Device::presentFrame()
{
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::presentFrame requires beginFrame() before present.");
        nrAssert(canPresentCurrentFrame(), "Device::presentFrame compute-present policy requires a compute-queue final submission that signals the active present semaphore.");

        auto const presentImage = activeSwapchainImageRawForExternalTools();
        nsightGraphics_.stopTraceBeforeBoundaryIfNeeded(presentImage);

        auto presentResult = presentationContext.present(queueManager, activePresentSemaphore(), presentFrameBoundaryFrameID_);
        nsightGraphics_.markFrameBoundaryAfterPresent(presentResult.result, presentImage);

        auto const recreateRequested = presentationContext.consumeSwapchainRecreateRequest();
        if (PresentationContext::needsSwapchainRecreate(presentResult.result) || recreateRequested)
        {
            recreateSwapchain();
            // After recreate the acquire pool was rebuilt; issue fresh acquire.
            presentationContext.issueNextAcquire();
        }
        else
        {
            // Issue next frame's acquire immediately while GPU is busy rendering.
            presentationContext.issueNextAcquire();
        }

        frameManager.advanceFrame();
        presentationContext.clearActiveSwapchainImage();
        presentationContext.setFrameSubmitted(false);
        frameSubmitCount_ = 0;
        frameFinalSubmitRole_.reset();
        presentFrameBoundaryFrameID_.reset();

        return presentResult;
    }

[[nodiscard]] PresentResult Device::endFrame(CommandBatch&& batch, QueueRole submitRole)
{
        submitFrame(std::move(batch), submitRole);
        return presentFrame();
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
            nrInfo(
                "Debug Vulkan validation layer settings enabled programmatically: "
                "Core, Sync Validation, GPU-AV shader instrumentation, TraceRay/RayQuery, "
                "Best Practices, NVIDIA Best Practices, Debug Printf, GPU-AV descriptor checks, "
                "report_flags=verbose/error/perf/info/warn, "
                "message_id_filter=BestPractices-Pipeline-SortAndBind, duplicate message limit disabled.");
            return vk::raii::Instance(context, instanceCreateInfo);
        }
        return vk::raii::Instance(context, instanceCreateInfo);
    }

vk::raii::Device Device::makeDevice()
{
        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        std::ranges::fill(queueFamilyDict, std::numeric_limits<std::size_t>::max());

        auto queueFamilies = selectRequiredQueueFamilies(queueFamilyProperties);
        nrAssert(queueFamilies.has_value(), "Selected GPU does not expose required graphics, compute, and dedicated physical copy/transfer queue families.");

        auto toQueueIndex = [](QueueFamilyKind kind) { return static_cast<std::size_t>(kind); };
        queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)] = queueFamilies->graphics;
        queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)] = queueFamilies->compute;
        queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)] = queueFamilies->transfer;

        auto queueFamilySummary = [&](std::uint32_t familyIndex) {
            const auto& family = queueFamilyProperties[familyIndex];
            return std::format(
                "index={} flags={} queueCount={}",
                familyIndex,
                vk::to_string(family.queueFlags),
                family.queueCount);
        };
        auto queueFamilySelectionMessage = std::format(
            "Vulkan queue family selection: graphics{{{}}} compute{{{}}} transfer{{{}}}",
            queueFamilySummary(queueFamilies->graphics),
            queueFamilySummary(queueFamilies->compute),
            queueFamilySummary(queueFamilies->transfer));
        nrInfo(queueFamilySelectionMessage);

        constexpr float queuePriority = 1.0f;
        auto uniqueFamilies = std::array{static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)]), static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]), static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)])};
        std::ranges::sort(uniqueFamilies);

        auto queueCreateInfos = uniqueFamilies | std::views::filter([last = std::uint32_t(-1)](std::uint32_t f) mutable {
                                    if (f == last)
                                        return false;
                                    last = f;
                                    return true;
                                }) |
                                std::views::transform([&](std::uint32_t familyIndex) { return vk::DeviceQueueCreateInfo({}, familyIndex, 1, &queuePriority); }) | std::ranges::to<std::vector>();

        auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
        auto isExtensionSupported = [&availableExtensions](std::string_view extensionName) { return std::ranges::any_of(availableExtensions, [extensionName](const vk::ExtensionProperties &property) { return std::string_view(property.extensionName) == extensionName; }); };

        std::vector<char const *> enabledExtensions;
        enabledExtensions.reserve(deviceEnabledExtensions.size() + 1);
        std::set<std::string_view> enabledExtensionSet;

        auto enableExtension = [&](std::string_view extensionName, std::string_view reason) {
            if (!isExtensionSupported(extensionName))
            {
                nrAssert(false, std::format("Required device extension '{}' is not supported ({})", extensionName, reason));
                return false;
            }
            if (enabledExtensionSet.insert(extensionName).second)
            {
                enabledExtensions.push_back(extensionName.data());
            }
            return true;
        };

        std::ranges::for_each(deviceEnabledExtensions, [&](std::string_view extensionName) {
            auto reason = std::string_view{"modern pipeline backend"};
            if (extensionName == vk::EXTExtendedDynamicState3ExtensionName)
                reason = "extended dynamic pipeline state v3";
            if (extensionName == vk::EXTOpacityMicromapExtensionName)
                reason = "ray tracing opacity micromap support";
            if (extensionName == vk::KHRMaintenance9ExtensionName)
                reason = "maintenance9 queue-family ownership transfer rules";
            enableExtension(extensionName, reason);
        });

        auto const frameBoundaryExtensionSupported = isExtensionSupported(vk::EXTFrameBoundaryExtensionName);
        auto const hdrMetadataExtensionSupported = isExtensionSupported(vk::EXTHdrMetadataExtensionName);
        auto frameBoundaryFeatureSupported = false;
        if (frameBoundaryExtensionSupported)
        {
            auto frameBoundaryFeatureQuery = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
            auto const &frameBoundaryFeatures = frameBoundaryFeatureQuery.get<vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
            frameBoundaryFeatureSupported = frameBoundaryFeatures.frameBoundary == vk::True;
        }

        auto features2 = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features, vk::PhysicalDeviceMaintenance9FeaturesKHR, vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV,
                                                     vk::PhysicalDeviceCooperativeVectorFeaturesNV, vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT, vk::PhysicalDeviceMeshShaderFeaturesEXT, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                                                     vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR, vk::PhysicalDeviceRayQueryFeaturesKHR, vk::PhysicalDeviceOpacityMicromapFeaturesEXT>();

        auto &featureList = features2.get<vk::PhysicalDeviceFeatures2>();
        auto &vulkan11Features = features2.get<vk::PhysicalDeviceVulkan11Features>();
        auto &vulkan12Features = features2.get<vk::PhysicalDeviceVulkan12Features>();
        auto &vulkan13Features = features2.get<vk::PhysicalDeviceVulkan13Features>();
        auto &vulkan14Features = features2.get<vk::PhysicalDeviceVulkan14Features>();
        auto &maintenance9Features = features2.get<vk::PhysicalDeviceMaintenance9FeaturesKHR>();
        auto &invocationReorderFeatures = features2.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV>();
        auto &cooperativeVectorFeatures = features2.get<vk::PhysicalDeviceCooperativeVectorFeaturesNV>();
        auto &extendedDynamicState3Features = features2.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>();
        auto &meshShaderFeatures = features2.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
        auto &accelerationStructureFeatures = features2.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        auto &rayTracingPipelineFeatures = features2.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
        auto &rayTracingMaintenance1Features = features2.get<vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR>();
        auto &rayQueryFeatures = features2.get<vk::PhysicalDeviceRayQueryFeaturesKHR>();
        auto &opacityMicromapFeatures = features2.get<vk::PhysicalDeviceOpacityMicromapFeaturesEXT>();

        // Keep core mesh/task shader support enabled, but avoid optional mesh sub-features
        // that require additional feature chains we do not currently enable.
        meshShaderFeatures.multiviewMeshShader = vk::False;
        meshShaderFeatures.primitiveFragmentShadingRateMeshShader = vk::False;

        auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceDescriptorIndexingProperties, vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        auto &physicalDeviceProperties = properties2.get<vk::PhysicalDeviceProperties2>();
        auto &descriptorIndexingProperties = properties2.get<vk::PhysicalDeviceDescriptorIndexingProperties>();
        auto &rayTracingPipelineProperties = properties2.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

#define REQUIRE_FEATURE(feature_field, feature_name) nrAssert(feature_field == vk::True, std::format("Required feature {} is not enabled.", feature_name))

        REQUIRE_FEATURE(vulkan11Features.shaderDrawParameters, "shaderDrawParameters");
        REQUIRE_FEATURE(vulkan12Features.bufferDeviceAddress, "bufferDeviceAddress");
        REQUIRE_FEATURE(vulkan12Features.descriptorIndexing, "descriptorIndexing");
        REQUIRE_FEATURE(vulkan12Features.runtimeDescriptorArray, "runtimeDescriptorArray");
        REQUIRE_FEATURE(vulkan12Features.descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound");
        REQUIRE_FEATURE(vulkan12Features.descriptorBindingVariableDescriptorCount, "descriptorBindingVariableDescriptorCount");
        REQUIRE_FEATURE(vulkan12Features.descriptorBindingSampledImageUpdateAfterBind, "descriptorBindingSampledImageUpdateAfterBind");
        REQUIRE_FEATURE(vulkan12Features.descriptorBindingUpdateUnusedWhilePending, "descriptorBindingUpdateUnusedWhilePending");
        REQUIRE_FEATURE(vulkan13Features.inlineUniformBlock, "inlineUniformBlock");
        REQUIRE_FEATURE(vulkan13Features.dynamicRendering, "dynamicRendering");
        REQUIRE_FEATURE(vulkan13Features.synchronization2, "synchronization2");
        REQUIRE_FEATURE(maintenance9Features.maintenance9, "maintenance9");
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3PolygonMode, "extendedDynamicState3PolygonMode");
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3RasterizationSamples, "extendedDynamicState3RasterizationSamples");
        REQUIRE_FEATURE(meshShaderFeatures.meshShader, "meshShader");
        REQUIRE_FEATURE(meshShaderFeatures.taskShader, "taskShader");
        REQUIRE_FEATURE(accelerationStructureFeatures.accelerationStructure, "accelerationStructure");
        REQUIRE_FEATURE(rayTracingPipelineFeatures.rayTracingPipeline, "rayTracingPipeline");
        REQUIRE_FEATURE(rayTracingMaintenance1Features.rayTracingMaintenance1, "rayTracingMaintenance1");
        REQUIRE_FEATURE(rayQueryFeatures.rayQuery, "rayQuery");
        REQUIRE_FEATURE(opacityMicromapFeatures.micromap, "opacityMicromap");
        REQUIRE_FEATURE(invocationReorderFeatures.rayTracingInvocationReorder, "rayTracingInvocationReorder");
        REQUIRE_FEATURE(cooperativeVectorFeatures.cooperativeVector, "cooperativeVector");

#undef REQUIRE_FEATURE

        descriptorIndexingCapabilities_ = DescriptorIndexingCapabilitySnapshot{
            .descriptorIndexing = vulkan12Features.descriptorIndexing == vk::True,
            .runtimeDescriptorArray = vulkan12Features.runtimeDescriptorArray == vk::True,
            .descriptorBindingPartiallyBound = vulkan12Features.descriptorBindingPartiallyBound == vk::True,
            .descriptorBindingVariableDescriptorCount = vulkan12Features.descriptorBindingVariableDescriptorCount == vk::True,
            .descriptorBindingSampledImageUpdateAfterBind = vulkan12Features.descriptorBindingSampledImageUpdateAfterBind == vk::True,
            .descriptorBindingUpdateUnusedWhilePending = vulkan12Features.descriptorBindingUpdateUnusedWhilePending == vk::True,
            .shaderSampledImageArrayNonUniformIndexing = vulkan12Features.shaderSampledImageArrayNonUniformIndexing == vk::True,
            .maxPerStageDescriptorUpdateAfterBindSampledImages = descriptorIndexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages,
            .maxDescriptorSetUpdateAfterBindSampledImages = descriptorIndexingProperties.maxDescriptorSetUpdateAfterBindSampledImages,
        };
        bufferDeviceAddressCapabilities_ = BufferDeviceAddressCapabilitySnapshot{
            .bufferDeviceAddress = vulkan12Features.bufferDeviceAddress == vk::True,
            .bufferDeviceAddressCaptureReplay = vulkan12Features.bufferDeviceAddressCaptureReplay == vk::True,
            .bufferDeviceAddressMultiDevice = vulkan12Features.bufferDeviceAddressMultiDevice == vk::True,
        };
        vulkan14Capabilities_ = Vulkan14CapabilitySnapshot{
            .globalPriorityQuery = vulkan14Features.globalPriorityQuery == vk::True,
            .shaderSubgroupRotate = vulkan14Features.shaderSubgroupRotate == vk::True,
            .shaderSubgroupRotateClustered = vulkan14Features.shaderSubgroupRotateClustered == vk::True,
            .shaderFloatControls2 = vulkan14Features.shaderFloatControls2 == vk::True,
            .shaderExpectAssume = vulkan14Features.shaderExpectAssume == vk::True,
            .rectangularLines = vulkan14Features.rectangularLines == vk::True,
            .bresenhamLines = vulkan14Features.bresenhamLines == vk::True,
            .smoothLines = vulkan14Features.smoothLines == vk::True,
            .stippledRectangularLines = vulkan14Features.stippledRectangularLines == vk::True,
            .stippledBresenhamLines = vulkan14Features.stippledBresenhamLines == vk::True,
            .stippledSmoothLines = vulkan14Features.stippledSmoothLines == vk::True,
            .vertexAttributeInstanceRateDivisor = vulkan14Features.vertexAttributeInstanceRateDivisor == vk::True,
            .vertexAttributeInstanceRateZeroDivisor = vulkan14Features.vertexAttributeInstanceRateZeroDivisor == vk::True,
            .indexTypeUint8 = vulkan14Features.indexTypeUint8 == vk::True,
            .dynamicRenderingLocalRead = vulkan14Features.dynamicRenderingLocalRead == vk::True,
            .maintenance5 = vulkan14Features.maintenance5 == vk::True,
            .maintenance6 = vulkan14Features.maintenance6 == vk::True,
            .maintenance9 = maintenance9Features.maintenance9 == vk::True,
            .pipelineProtectedAccess = vulkan14Features.pipelineProtectedAccess == vk::True,
            .pipelineRobustness = vulkan14Features.pipelineRobustness == vk::True,
            .hostImageCopy = vulkan14Features.hostImageCopy == vk::True,
            .pushDescriptor = vulkan14Features.pushDescriptor == vk::True,
        };
        vulkan14Properties_ = queryVulkan14PropertySnapshot();

        auto queueOwnershipPropertyChains = physicalDevice.getQueueFamilyProperties2<
            vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyOwnershipTransferPropertiesKHR>>();
        auto ownershipTransferMasks =
            queueOwnershipPropertyChains |
            std::views::transform([](const auto& chain) {
                return chain.template get<vk::QueueFamilyOwnershipTransferPropertiesKHR>()
                    .optimalImageTransferToQueueFamilies;
            }) |
            std::ranges::to<std::vector>();
        nrAssert(
            ownershipTransferMasks.size() == queueFamilyProperties.size(),
            "VK_KHR_maintenance9 queue-family ownership transfer property query returned an unexpected family count.");
        queueFamilyTransferPolicy_ = nr::rhi::ops::QueueFamilyTransferPolicy{
            .maintenance9 = maintenance9Features.maintenance9 == vk::True,
            .optimalImageTransferToQueueFamilies = std::move(ownershipTransferMasks),
        };

        auto frameBoundaryCreateFeatures = vk::PhysicalDeviceFrameBoundaryFeaturesEXT{};
        frameBoundaryEnabled_ = frameBoundaryExtensionSupported && frameBoundaryFeatureSupported;
        if (frameBoundaryEnabled_)
        {
            enableExtension(vk::EXTFrameBoundaryExtensionName, "graphics debugger frame boundary metadata");
            frameBoundaryCreateFeatures.frameBoundary = vk::True;
            frameBoundaryCreateFeatures.pNext = featureList.pNext;
            featureList.pNext = std::addressof(frameBoundaryCreateFeatures);
            nrInfo("VK_EXT_frame_boundary enabled for graphics debugger frame capture.");
        }
        else if (frameBoundaryExtensionSupported)
        {
            nrInfo<LogLevel::warning>("VK_EXT_frame_boundary was exposed without its frameBoundary feature; frame-boundary tagging is disabled.");
        }

        hdrMetadataEnabled_ = false;
        if (hdrMetadataExtensionSupported)
        {
            hdrMetadataEnabled_ = enableExtension(vk::EXTHdrMetadataExtensionName, "HDR10 swapchain metadata");
        }
        else
        {
            nrInfo("VK_EXT_hdr_metadata is unavailable; HDR swapchain output can still run without presentation metadata.");
        }

        auto const &limits = physicalDeviceProperties.properties.limits;
        rtCapabilities_ = RayTracingCapabilitySnapshot{
            .rayTracingMaintenance1 = rayTracingMaintenance1Features.rayTracingMaintenance1 == vk::True,
            .rayTracingPipelineTraceRaysIndirect = rayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect == vk::True,
            .rayTracingPipelineTraceRaysIndirect2 = rayTracingMaintenance1Features.rayTracingPipelineTraceRaysIndirect2 == vk::True,
            .rayTracingPipelineShaderGroupHandleCaptureReplay = rayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplay == vk::True,
            .rayTracingPipelineShaderGroupHandleCaptureReplayMixed = rayTracingPipelineFeatures.rayTracingPipelineShaderGroupHandleCaptureReplayMixed == vk::True,
            .rayTraversalPrimitiveCulling = rayTracingPipelineFeatures.rayTraversalPrimitiveCulling == vk::True,
            .opacityMicromap = opacityMicromapFeatures.micromap == vk::True,
            .opacityMicromapCaptureReplay = opacityMicromapFeatures.micromapCaptureReplay == vk::True,
            .opacityMicromapHostCommands = opacityMicromapFeatures.micromapHostCommands == vk::True,
            .shaderGroupHandleSize = rayTracingPipelineProperties.shaderGroupHandleSize,
            .shaderGroupHandleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment,
            .shaderGroupBaseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment,
            .shaderGroupHandleCaptureReplaySize = rayTracingPipelineProperties.shaderGroupHandleCaptureReplaySize,
            .maxShaderGroupStride = rayTracingPipelineProperties.maxShaderGroupStride,
            .maxRayDispatchInvocationCount = rayTracingPipelineProperties.maxRayDispatchInvocationCount,
            .maxRayRecursionDepth = rayTracingPipelineProperties.maxRayRecursionDepth,
            .maxRayHitAttributeSize = rayTracingPipelineProperties.maxRayHitAttributeSize,
            .maxDispatchDimensions =
                {
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[0]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[0]),
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[1]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[1]),
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[2]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[2]),
                },
        };

        vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr, &featureList);
        return vk::raii::Device(physicalDevice, deviceCreateInfo);
    }

void Device::initializeCommandSystem()
{
        std::uint32_t graphicsFamily = requiredQueueFamily(QueueFamilyKind::graphics);
        std::uint32_t computeFamily = requiredQueueFamily(QueueFamilyKind::compute);
        std::uint32_t transferFamily = requiredQueueFamily(QueueFamilyKind::transfer);

        GpuQueue graphicsQueue(device, graphicsFamily, QueueRole::Graphics);
        GpuQueue computeQueue(device, computeFamily, QueueRole::Compute);
        GpuQueue transferQueue(device, transferFamily, QueueRole::Transfer);

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
        refreshPresentSemaphores();
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

Device::~Device()
{
        if (*device != nullptr)
        {
            waitIdle();
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

[[nodiscard]] Vulkan14PropertySnapshot Device::queryVulkan14PropertySnapshot() const
{
        auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan14Properties>();
        auto vulkan14Properties = properties2.get<vk::PhysicalDeviceVulkan14Properties>();

        std::vector<vk::ImageLayout> hostCopySrcLayouts(vulkan14Properties.copySrcLayoutCount);
        std::vector<vk::ImageLayout> hostCopyDstLayouts(vulkan14Properties.copyDstLayoutCount);
        if (!hostCopySrcLayouts.empty() || !hostCopyDstLayouts.empty())
        {
            vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan14Properties> layoutPropertyChain{};
            auto &layoutProperties2 = layoutPropertyChain.get<vk::PhysicalDeviceProperties2>();
            auto &layoutVulkan14Properties = layoutPropertyChain.get<vk::PhysicalDeviceVulkan14Properties>();

            layoutVulkan14Properties.copySrcLayoutCount = static_cast<std::uint32_t>(hostCopySrcLayouts.size());
            layoutVulkan14Properties.pCopySrcLayouts = hostCopySrcLayouts.data();
            layoutVulkan14Properties.copyDstLayoutCount = static_cast<std::uint32_t>(hostCopyDstLayouts.size());
            layoutVulkan14Properties.pCopyDstLayouts = hostCopyDstLayouts.data();

            (*physicalDevice).getProperties2(&layoutProperties2, *physicalDevice.getDispatcher());
            vulkan14Properties = layoutVulkan14Properties;
        }

        auto uuid = std::array<std::uint8_t, vk::UuidSize>{};
        auto uuidIndices = std::views::iota(std::size_t{0}, uuid.size());
        std::ranges::for_each(uuidIndices, [&](std::size_t i) { uuid[i] = vulkan14Properties.optimalTilingLayoutUUID[i]; });

        return Vulkan14PropertySnapshot{
            .lineSubPixelPrecisionBits = vulkan14Properties.lineSubPixelPrecisionBits,
            .maxVertexAttribDivisor = vulkan14Properties.maxVertexAttribDivisor,
            .supportsNonZeroFirstInstance = vulkan14Properties.supportsNonZeroFirstInstance == vk::True,
            .maxPushDescriptors = vulkan14Properties.maxPushDescriptors,
            .dynamicRenderingLocalReadDepthStencilAttachments = vulkan14Properties.dynamicRenderingLocalReadDepthStencilAttachments == vk::True,
            .dynamicRenderingLocalReadMultisampledAttachments = vulkan14Properties.dynamicRenderingLocalReadMultisampledAttachments == vk::True,
            .earlyFragmentMultisampleCoverageAfterSampleCounting = vulkan14Properties.earlyFragmentMultisampleCoverageAfterSampleCounting == vk::True,
            .earlyFragmentSampleMaskTestBeforeSampleCounting = vulkan14Properties.earlyFragmentSampleMaskTestBeforeSampleCounting == vk::True,
            .depthStencilSwizzleOneSupport = vulkan14Properties.depthStencilSwizzleOneSupport == vk::True,
            .polygonModePointSize = vulkan14Properties.polygonModePointSize == vk::True,
            .nonStrictSinglePixelWideLinesUseParallelogram = vulkan14Properties.nonStrictSinglePixelWideLinesUseParallelogram == vk::True,
            .nonStrictWideLinesUseParallelogram = vulkan14Properties.nonStrictWideLinesUseParallelogram == vk::True,
            .blockTexelViewCompatibleMultipleLayers = vulkan14Properties.blockTexelViewCompatibleMultipleLayers == vk::True,
            .maxCombinedImageSamplerDescriptorCount = vulkan14Properties.maxCombinedImageSamplerDescriptorCount,
            .fragmentShadingRateClampCombinerInputs = vulkan14Properties.fragmentShadingRateClampCombinerInputs == vk::True,
            .defaultRobustnessStorageBuffers = vulkan14Properties.defaultRobustnessStorageBuffers,
            .defaultRobustnessUniformBuffers = vulkan14Properties.defaultRobustnessUniformBuffers,
            .defaultRobustnessVertexInputs = vulkan14Properties.defaultRobustnessVertexInputs,
            .defaultRobustnessImages = vulkan14Properties.defaultRobustnessImages,
            .hostImageCopySrcLayouts = std::move(hostCopySrcLayouts),
            .hostImageCopyDstLayouts = std::move(hostCopyDstLayouts),
            .optimalTilingLayoutUUID = uuid,
            .identicalMemoryTypeRequirements = vulkan14Properties.identicalMemoryTypeRequirements == vk::True,
        };
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

        if (hasInstanceExtension(vk::EXTSwapchainColorSpaceExtensionName))
        {
            addIfMissing(instanceEnabledExtensions, vk::EXTSwapchainColorSpaceExtensionName);
        }
        else
        {
            nrInfo("VK_EXT_swapchain_colorspace is unavailable; swapchain format selection is limited to core color spaces.");
        }

        if constexpr (isDebugMode || gpuDebugNamesEnabled)
        {
            if constexpr (isDebugMode)
            {
                constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
                nrAssert(hasInstanceLayer(validationLayer), std::format("Debug builds require '{}'. The Vulkan loader did not enumerate this layer on the current machine. "
                                                                        "Validation layers are provided by the Vulkan SDK / validation-layer installation, not by the GPU or display driver.",
                                                                        validationLayer));
                addIfMissing(instanceEnabledLayers, validationLayer);
            }

            if (hasInstanceExtension(vk::EXTDebugUtilsExtensionName))
            {
                addIfMissing(instanceEnabledExtensions, vk::EXTDebugUtilsExtensionName);
            }
            else
            {
                nrInfo<LogLevel::error>("VK_EXT_debug_utils is unavailable; validation callbacks, debug labels, and object names require this extension.");
                nrAssert(false, "VK_EXT_debug_utils is required when validation or GPU debug names are enabled.");
            }
        }
    }

[[nodiscard]] std::uint32_t Device::requiredQueueFamily(QueueFamilyKind kind) const
{
        std::size_t index = static_cast<std::size_t>(kind);
        std::size_t familyIndex = queueFamilyDict[index];
        nrAssert(familyIndex != std::numeric_limits<std::size_t>::max(), "Queue family not found - device capability contract violated.");
        return static_cast<std::uint32_t>(familyIndex);
    }

[[nodiscard]] std::uint32_t Device::presentQueueFamilyIndex() const
{
        return requiredQueueFamily(QueueFamilyKind::compute);
    }

void Device::refreshPresentSemaphores()
{
        auto swapchainImageCount = presentationContext.swapchainImageCount();
        auto upToDate = presentSemaphoresByImage_.size() == swapchainImageCount && std::ranges::all_of(presentSemaphoresByImage_, [](const vk::raii::Semaphore &semaphore) { return *semaphore != nullptr; });
        if (upToDate)
        {
            return;
        }

        presentSemaphoresByImage_.clear();
        presentSemaphoresByImage_.reserve(swapchainImageCount);

        auto semaphoreCreateInfo = vk::SemaphoreCreateInfo{};
        auto imageIndices = std::views::iota(std::uint32_t{0}, swapchainImageCount);
        std::ranges::for_each(imageIndices, [&](std::uint32_t) { presentSemaphoresByImage_.emplace_back(device, semaphoreCreateInfo); });
    }

[[nodiscard]] const vk::raii::Semaphore &Device::activePresentSemaphore() const
{
        auto imageIndex = presentationContext.activeSwapchainImageIndex();
        nrAssert(imageIndex < presentSemaphoresByImage_.size(), std::format("Device::activePresentSemaphore image index {} is out of range for {} present semaphores.", imageIndex, presentSemaphoresByImage_.size()));
        return presentSemaphoresByImage_[imageIndex];
    }

void rhiTest()
{
    Device device;
    device.initialize("HelloVulkan", "VKEngine");
}
} // namespace nr::rhi
