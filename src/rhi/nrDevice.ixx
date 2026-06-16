export module nr.rhi:device;
import dependency;

import :vk;
import :surface;
import :swapchain;
import :type;
import :queue;
import :frameContext;
import :memoryAllocator;
import :resourcePool;
import :pipeline;
import :resourceOps;
import nr.utils;
import std;

export namespace nr::rhi
{
struct DescriptorIndexingCapabilitySnapshot
{
    bool descriptorIndexing = false;
    bool runtimeDescriptorArray = false;
    bool descriptorBindingPartiallyBound = false;
    bool descriptorBindingVariableDescriptorCount = false;
    bool descriptorBindingSampledImageUpdateAfterBind = false;
    bool descriptorBindingUpdateUnusedWhilePending = false;
    bool shaderSampledImageArrayNonUniformIndexing = false;
    std::uint32_t maxPerStageDescriptorUpdateAfterBindSampledImages = 0;
    std::uint32_t maxDescriptorSetUpdateAfterBindSampledImages = 0;
};

struct BufferDeviceAddressCapabilitySnapshot
{
    bool bufferDeviceAddress = false;
    bool bufferDeviceAddressCaptureReplay = false;
    bool bufferDeviceAddressMultiDevice = false;
};

struct Vulkan14CapabilitySnapshot
{
    bool globalPriorityQuery = false;
    bool shaderSubgroupRotate = false;
    bool shaderSubgroupRotateClustered = false;
    bool shaderFloatControls2 = false;
    bool shaderExpectAssume = false;
    bool rectangularLines = false;
    bool bresenhamLines = false;
    bool smoothLines = false;
    bool stippledRectangularLines = false;
    bool stippledBresenhamLines = false;
    bool stippledSmoothLines = false;
    bool vertexAttributeInstanceRateDivisor = false;
    bool vertexAttributeInstanceRateZeroDivisor = false;
    bool indexTypeUint8 = false;
    bool dynamicRenderingLocalRead = false;
    bool maintenance5 = false;
    bool maintenance6 = false;
    bool pipelineProtectedAccess = false;
    bool pipelineRobustness = false;
    bool hostImageCopy = false;
    bool pushDescriptor = false;
};

struct Vulkan14PropertySnapshot
{
    std::uint32_t lineSubPixelPrecisionBits = 0;
    std::uint32_t maxVertexAttribDivisor = 0;
    bool supportsNonZeroFirstInstance = false;
    std::uint32_t maxPushDescriptors = 0;
    bool dynamicRenderingLocalReadDepthStencilAttachments = false;
    bool dynamicRenderingLocalReadMultisampledAttachments = false;
    bool earlyFragmentMultisampleCoverageAfterSampleCounting = false;
    bool earlyFragmentSampleMaskTestBeforeSampleCounting = false;
    bool depthStencilSwizzleOneSupport = false;
    bool polygonModePointSize = false;
    bool nonStrictSinglePixelWideLinesUseParallelogram = false;
    bool nonStrictWideLinesUseParallelogram = false;
    bool blockTexelViewCompatibleMultipleLayers = false;
    std::uint32_t maxCombinedImageSamplerDescriptorCount = 0;
    bool fragmentShadingRateClampCombinerInputs = false;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessStorageBuffers = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessUniformBuffers = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessBufferBehavior defaultRobustnessVertexInputs = vk::PipelineRobustnessBufferBehavior::eDeviceDefault;
    vk::PipelineRobustnessImageBehavior defaultRobustnessImages = vk::PipelineRobustnessImageBehavior::eDeviceDefault;
    std::vector<vk::ImageLayout> hostImageCopySrcLayouts{};
    std::vector<vk::ImageLayout> hostImageCopyDstLayouts{};
    std::array<std::uint8_t, vk::UuidSize> optimalTilingLayoutUUID{};
    bool identicalMemoryTypeRequirements = false;
};

class Device
{
  public:
    struct FrameBeginResult
    {
        std::uint32_t frameIndex = 0;
        std::uint32_t swapchainImageIndex = 0;
        vk::Result swapchainResult = vk::Result::eSuccess;
    };

    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};

    MemoryAllocator memoryAllocator;
    ResourceFactory resourceFactory;
    ResourcePool resourcePool;

    QueueManager queueManager;
    FrameManager frameManager;

    PresentationContext presentationContext;
    PipelineService pipelineService;
    std::optional<ops::UploadReadbackContext> uploadReadbackContext_{};

    Device() = default;
    Device(Device &) = delete;
    Device &operator=(Device &) = delete;

    [[nodiscard]] const RayTracingCapabilitySnapshot &rayTracingCapabilities() const noexcept
    {
        return rtCapabilities_;
    }

    [[nodiscard]] const DescriptorIndexingCapabilitySnapshot &descriptorIndexingCapabilities() const noexcept
    {
        return descriptorIndexingCapabilities_;
    }

    [[nodiscard]] const BufferDeviceAddressCapabilitySnapshot &bufferDeviceAddressCapabilities() const noexcept
    {
        return bufferDeviceAddressCapabilities_;
    }

    [[nodiscard]] const Vulkan14CapabilitySnapshot &vulkan14Capabilities() const noexcept
    {
        return vulkan14Capabilities_;
    }

    [[nodiscard]] const Vulkan14PropertySnapshot &vulkan14Properties() const noexcept
    {
        return vulkan14Properties_;
    }

    [[nodiscard]] static constexpr QueueRole presentSubmitRole() noexcept
    {
        return QueueRole::Compute;
    }

    [[nodiscard]] bool hasEnabledInstanceExtension(std::string_view extension) const
    {
        return std::ranges::any_of(instanceEnabledExtensions,
                                   [extension](const std::string &item) { return item == extension; });
    }

    void initialize(std::string const &_appName = {"DefaultApp"}, std::string const &_engineName = {"DefaultEngine"})
    {
        appName = _appName;
        engineName = _engineName;
        setupInitialFlags();
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
        device = makeDevice();

        memoryAllocator.initialize(instance, physicalDevice, device);
        resourceFactory.initialize(memoryAllocator, device);
        resourcePool.initialize(memoryAllocator, device);

        initializeCommandSystem();
        uploadReadbackContext_.emplace(device, resourceFactory, queueManager);

        presentationContext.initialize(instance, physicalDevice, device, appName, swapChainConfig_, presentQueueFamilyIndex());
        refreshPresentSemaphores();
        pipelineService.bindDevice(device, std::cref(rtCapabilities_));

        // Prime the first frame's acquire so beginFrame() can immediately consume it.
        presentationContext.issueFirstAcquire();
    }

    [[nodiscard]] FrameBeginResult beginFrame(std::uint64_t acquireTimeout = std::numeric_limits<std::uint64_t>::max())
    {
        using ProfileClock = std::chrono::steady_clock;
        auto profileMark = ProfileClock::now();
        auto elapsedMicros = [&profileMark]() {
            auto now = ProfileClock::now();
            auto us = std::chrono::duration<double, std::micro>(now - profileMark).count();
            profileMark = now;
            return us;
        };
        static double accumFence = 0.0, accumResetResourcePool = 0.0, accumResetVmaPool = 0.0;
        static double accumResetFence = 0.0, accumResetCmdPools = 0.0, accumPreparePools = 0.0;
        static std::uint32_t beginProfileCount = 0;

        auto &frame = frameManager.current();
        const auto frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
        nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");
        accumFence += elapsedMicros();

        // After this frame slot's fence: its previous final submit has completed, meaning the
        // imageAvailable wait bound to THIS frame slot was executed. Return that slot to the pool.
        presentationContext.returnAcquireSemaphore(frameIndex);

        resourcePool.resetFrame(frameIndex);
        accumResetResourcePool += elapsedMicros();

        memoryAllocator.resetFramePool(frameIndex);
        accumResetVmaPool += elapsedMicros();

        frame.resetFence();
        accumResetFence += elapsedMicros();

        frame.resetPools();
        accumResetCmdPools += elapsedMicros();

        const auto workerCount = std::min<std::uint32_t>(maxThreads, std::max(1u, std::thread::hardware_concurrency()));
        frame.prepareSecondaryPools(workerCount, workerCount, 0u);
        accumPreparePools += elapsedMicros();

        // Consume the pre-acquired image (issued at end of previous presentFrame or initialize).
        // If the pending acquire is absent (e.g., after a recreate that failed), issue now.
        if (!presentationContext.hasPendingAcquire())
        {
            presentationContext.issueNextAcquire(acquireTimeout);
        }

        auto acquire = presentationContext.consumePendingAcquire(frameIndex);

        if (PresentationContext::needsSwapchainRecreate(acquire.result))
        {
            presentationContext.recreate(physicalDevice, device, queueManager);
            refreshPresentSemaphores();
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

        ++beginProfileCount;
        constexpr auto kBeginProfileWindow = 1000u;
        if (beginProfileCount >= kBeginProfileWindow)
        {
            auto inv = 1.0 / static_cast<double>(beginProfileCount);
            nrInfo(std::format(
                "beginFrame sub-phase avg over {} frames (us): "
                "waitFence={:.1f} resetResourcePool={:.1f} resetVmaPool={:.1f} "
                "resetFence={:.1f} resetCmdPools={:.1f} preparePools={:.1f}",
                beginProfileCount,
                accumFence * inv, accumResetResourcePool * inv, accumResetVmaPool * inv,
                accumResetFence * inv, accumResetCmdPools * inv, accumPreparePools * inv));
            accumFence = 0; accumResetResourcePool = 0; accumResetVmaPool = 0;
            accumResetFence = 0; accumResetCmdPools = 0; accumPreparePools = 0;
            beginProfileCount = 0;
        }

        return FrameBeginResult{
            .frameIndex = frameIndex,
            .swapchainImageIndex = acquire.imageIndex,
            .swapchainResult = acquire.result,
        };
    }

    void submitFrameBatch(
        const CommandBatch& batch,
        QueueRole submitRole,
        bool signalForPresent,
        vk::PipelineStageFlags2 imageAvailableWaitStage)
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::submitFrameBatch requires beginFrame() before submission.");
        nrAssert(
            !(frameFinalSubmitRole_.has_value() && !signalForPresent),
            "Device::submitFrameBatch cannot submit additional batches after final present-signaling submit.");

        if (signalForPresent)
        {
            nrAssert(!frameFinalSubmitRole_.has_value(), "Device::submitFrameBatch final present-signaling submit can only happen once per frame.");
            nrAssert(submitRole == presentSubmitRole(), "Device::submitFrameBatch compute-present policy requires the compute queue when signalForPresent=true.");
        }

        auto& frame = frameManager.current();
        auto submitBatch = batch;

        // Keep pre-present work decoupled from swapchain availability.
        // Waiting on imageAvailable only at the present-signaling submit prevents vblank pacing
        // from stalling earlier GPU batches that do not touch the swapchain image.
        if (signalForPresent)
        {
            submitBatch.addWait(frame.imageAvailable(), imageAvailableWaitStage);
        }

        if (signalForPresent)
        {
            submitBatch.addSignal(activePresentSemaphore());
        }

        auto submitToRole = [&](QueueRole role, std::optional<std::reference_wrapper<const vk::raii::Fence>> fence) {
            if (role == QueueRole::Graphics)
            {
                queueManager.graphics().submit(submitBatch, fence);
                return;
            }
            if (role == QueueRole::Compute)
            {
                queueManager.compute().submit(submitBatch, fence);
                return;
            }
            queueManager.transfer().submit(submitBatch, fence);
        };

        auto fence = signalForPresent
                         ? std::optional<std::reference_wrapper<const vk::raii::Fence>>(std::cref(frame.fence()))
                         : std::nullopt;
        submitToRole(submitRole, fence);

        ++frameSubmitCount_;

        if (signalForPresent)
        {
            frameFinalSubmitRole_ = submitRole;
            presentationContext.setFrameSubmitted(true);
        }
    }

    void submitFrameBatch(const CommandBatch& batch, QueueRole submitRole = QueueRole::Compute, bool signalForPresent = false)
    {
        submitFrameBatch(
            batch,
            submitRole,
            signalForPresent,
            vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands});
    }

    void submitFrame(const CommandBatch& batch, QueueRole submitRole = QueueRole::Compute)
    {
        submitFrameBatch(batch, submitRole, true);
    }

    [[nodiscard]] bool canPresentCurrentFrame() const noexcept
    {
        return frameSubmitCount_ > 0 &&
               frameFinalSubmitRole_.has_value() &&
               *frameFinalSubmitRole_ == presentSubmitRole() &&
               presentationContext.hasSubmittedCurrentFrame();
    }

    [[nodiscard]] QueueRole submitRoleForPresent() const noexcept
    {
        return frameFinalSubmitRole_.value_or(presentSubmitRole());
    }

    [[nodiscard]] std::uint32_t frameSubmitCount() const noexcept
    {
        return frameSubmitCount_;
    }

    [[nodiscard]] PresentResult presentFrame()
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::presentFrame requires beginFrame() before present.");
        nrAssert(canPresentCurrentFrame(), "Device::presentFrame compute-present policy requires a compute-queue final submission that signals renderFinished.");

        auto presentResult = presentationContext.present(queueManager, activePresentSemaphore());

        if (PresentationContext::needsSwapchainRecreate(presentResult.result))
        {
            presentationContext.recreate(physicalDevice, device, queueManager);
            refreshPresentSemaphores();
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

        return presentResult;
    }

    [[nodiscard]] PresentResult endFrame(const CommandBatch& batch, QueueRole submitRole = QueueRole::Compute)
    {
        submitFrame(batch, submitRole);
        return presentFrame();
    }

    vk::raii::Instance makeInstance(std::uint32_t apiVersion = vk::ApiVersion14) const
    {
        const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
        std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
        std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);

        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        vk::InstanceCreateInfo instanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions);
        if constexpr (isDebugMode)
        {
            if (hasEnabledInstanceExtension(vk::EXTDebugUtilsExtensionName))
            {
                debugCreateInfo = makeDebugUtilsMessengerCreateInfoEXT();
                instanceCreateInfo.pNext = &debugCreateInfo;
            }
        }
        return vk::raii::Instance(context, instanceCreateInfo);
    }

    vk::raii::Device makeDevice()
    {
        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        std::ranges::fill(queueFamilyDict, std::numeric_limits<std::size_t>::max());

        auto queueFamilies = selectRequiredQueueFamilies(queueFamilyProperties);
        nrAssert(
            queueFamilies.has_value(),
            "Selected GPU does not expose required graphics, compute, and dedicated physical copy/transfer queue families.");

        auto toQueueIndex = [](QueueFamilyKind kind) { return static_cast<std::size_t>(kind); };
        queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)] = queueFamilies->graphics;
        queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)] = queueFamilies->compute;
        queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)] = queueFamilies->transfer;

        constexpr float queuePriority = 1.0f;
        auto uniqueFamilies = std::array{
            static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)]),
            static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]),
            static_cast<std::uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)])
        };
        std::ranges::sort(uniqueFamilies);
        
        auto queueCreateInfos = uniqueFamilies | 
                                std::views::filter([last = std::uint32_t(-1)](std::uint32_t f) mutable { 
                                    if (f == last) return false; 
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

        std::vector<char const *> enabledExtensions;
        enabledExtensions.reserve(deviceEnabledExtensions.size());
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
            enableExtension(extensionName, reason);
        });

        auto features2 = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
                                 vk::PhysicalDeviceVulkan11Features,
                                 vk::PhysicalDeviceVulkan12Features,
                                                     vk::PhysicalDeviceVulkan13Features,
                                                     vk::PhysicalDeviceVulkan14Features,
                                                     vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV,
                                                     vk::PhysicalDeviceCooperativeVectorFeaturesNV,
                                                     vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT,
                                                     vk::PhysicalDeviceMeshShaderFeaturesEXT,
                                                     vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                                                     vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                                                     vk::PhysicalDeviceRayQueryFeaturesKHR,
                                                     vk::PhysicalDeviceOpacityMicromapFeaturesEXT>();

        auto &featureList = features2.get<vk::PhysicalDeviceFeatures2>();
        auto &vulkan11Features = features2.get<vk::PhysicalDeviceVulkan11Features>();
        auto &vulkan12Features = features2.get<vk::PhysicalDeviceVulkan12Features>();
        auto &vulkan13Features = features2.get<vk::PhysicalDeviceVulkan13Features>();
        auto &vulkan14Features = features2.get<vk::PhysicalDeviceVulkan14Features>();
        auto &invocationReorderFeatures = features2.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV>();
        auto &cooperativeVectorFeatures = features2.get<vk::PhysicalDeviceCooperativeVectorFeaturesNV>();
        auto &extendedDynamicState3Features = features2.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>();
        auto &meshShaderFeatures = features2.get<vk::PhysicalDeviceMeshShaderFeaturesEXT>();
        auto &accelerationStructureFeatures = features2.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        auto &rayTracingPipelineFeatures = features2.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
        auto &rayQueryFeatures = features2.get<vk::PhysicalDeviceRayQueryFeaturesKHR>();
        auto &opacityMicromapFeatures = features2.get<vk::PhysicalDeviceOpacityMicromapFeaturesEXT>();

        // Keep core mesh/task shader support enabled, but avoid optional mesh sub-features
        // that require additional feature chains we do not currently enable.
        meshShaderFeatures.multiviewMeshShader = vk::False;
        meshShaderFeatures.primitiveFragmentShadingRateMeshShader = vk::False;

        auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                 vk::PhysicalDeviceDescriptorIndexingProperties,
                                 vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
        auto &physicalDeviceProperties = properties2.get<vk::PhysicalDeviceProperties2>();
        auto &descriptorIndexingProperties = properties2.get<vk::PhysicalDeviceDescriptorIndexingProperties>();
        auto &rayTracingPipelineProperties = properties2.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();

#define REQUIRE_FEATURE(feature_field, feature_name) \
        nrAssert(feature_field == vk::True, std::format("Required feature {} is not enabled.", feature_name))

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
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3PolygonMode, "extendedDynamicState3PolygonMode");
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3RasterizationSamples, "extendedDynamicState3RasterizationSamples");
        REQUIRE_FEATURE(meshShaderFeatures.meshShader, "meshShader");
        REQUIRE_FEATURE(meshShaderFeatures.taskShader, "taskShader");
        REQUIRE_FEATURE(accelerationStructureFeatures.accelerationStructure, "accelerationStructure");
        REQUIRE_FEATURE(rayTracingPipelineFeatures.rayTracingPipeline, "rayTracingPipeline");
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
            .pipelineProtectedAccess = vulkan14Features.pipelineProtectedAccess == vk::True,
            .pipelineRobustness = vulkan14Features.pipelineRobustness == vk::True,
            .hostImageCopy = vulkan14Features.hostImageCopy == vk::True,
            .pushDescriptor = vulkan14Features.pushDescriptor == vk::True,
        };
        vulkan14Properties_ = queryVulkan14PropertySnapshot();

        auto const &limits = physicalDeviceProperties.properties.limits;
        rtCapabilities_ = RayTracingCapabilitySnapshot{
            .rayTracingPipelineTraceRaysIndirect = rayTracingPipelineFeatures.rayTracingPipelineTraceRaysIndirect == vk::True,
            .opacityMicromap = opacityMicromapFeatures.micromap == vk::True,
            .opacityMicromapCaptureReplay = opacityMicromapFeatures.micromapCaptureReplay == vk::True,
            .opacityMicromapHostCommands = opacityMicromapFeatures.micromapHostCommands == vk::True,
            .shaderGroupHandleSize = rayTracingPipelineProperties.shaderGroupHandleSize,
            .shaderGroupHandleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment,
            .shaderGroupBaseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment,
            .maxShaderGroupStride = rayTracingPipelineProperties.maxShaderGroupStride,
            .maxRayDispatchInvocationCount = rayTracingPipelineProperties.maxRayDispatchInvocationCount,
            .maxRayRecursionDepth = rayTracingPipelineProperties.maxRayRecursionDepth,
            .maxDispatchDimensions = {
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[0]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[0]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[1]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[1]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[2]) * static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[2]),
            },
        };

        vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr, &featureList);
        return vk::raii::Device(physicalDevice, deviceCreateInfo);
    }

    void initializeCommandSystem()
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

        nrInfo(std::format("Command system initialized: {} frames in flight, max {} worker threads", maxFrameInFlight, maxThreads));
    }

    void waitIdle()
    {
        queueManager.waitAllIdle();
        frameManager.waitAll();
        uploadReadbackContext_.reset();
    }

    [[nodiscard]] PipelineService &pipeline() noexcept
    {
        return pipelineService;
    }

    [[nodiscard]] const PipelineService &pipeline() const noexcept
    {
        return pipelineService;
    }

    [[nodiscard]] ShaderService &shaderCompiler() const
    {
        return ShaderService::instance();
    }

    [[nodiscard]] ops::UploadReadbackContext &uploadReadback() noexcept
    {
        nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
        return *uploadReadbackContext_;
    }

    [[nodiscard]] const ops::UploadReadbackContext &uploadReadback() const noexcept
    {
        nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
        return *uploadReadbackContext_;
    }

    ~Device()
    {
        if (*device != nullptr)
        {
            waitIdle();
        }
    }

  protected:
    [[nodiscard]] Vulkan14PropertySnapshot queryVulkan14PropertySnapshot() const
    {
        auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan14Properties>();
        auto vulkan14Properties = properties2.get<vk::PhysicalDeviceVulkan14Properties>();

        std::vector<vk::ImageLayout> hostCopySrcLayouts(vulkan14Properties.copySrcLayoutCount);
        std::vector<vk::ImageLayout> hostCopyDstLayouts(vulkan14Properties.copyDstLayoutCount);
        if (!hostCopySrcLayouts.empty() || !hostCopyDstLayouts.empty())
        {
            vk::StructureChain<vk::PhysicalDeviceProperties2, vk::PhysicalDeviceVulkan14Properties> layoutPropertyChain{};
            auto& layoutProperties2 = layoutPropertyChain.get<vk::PhysicalDeviceProperties2>();
            auto& layoutVulkan14Properties = layoutPropertyChain.get<vk::PhysicalDeviceVulkan14Properties>();

            layoutVulkan14Properties.copySrcLayoutCount = static_cast<std::uint32_t>(hostCopySrcLayouts.size());
            layoutVulkan14Properties.pCopySrcLayouts = hostCopySrcLayouts.data();
            layoutVulkan14Properties.copyDstLayoutCount = static_cast<std::uint32_t>(hostCopyDstLayouts.size());
            layoutVulkan14Properties.pCopyDstLayouts = hostCopyDstLayouts.data();

            (*physicalDevice).getProperties2(&layoutProperties2, *physicalDevice.getDispatcher());
            vulkan14Properties = layoutVulkan14Properties;
        }

        auto uuid = std::array<std::uint8_t, vk::UuidSize>{};
        auto uuidIndices = std::views::iota(std::size_t{0}, uuid.size());
        std::ranges::for_each(uuidIndices, [&](std::size_t i) {
            uuid[i] = vulkan14Properties.optimalTilingLayoutUUID[i];
        });

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

    void setupInitialFlags()
    {
        Surface::ensureGlfwInitialized();

        std::uint32_t glfwCount = 0;
        const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
        nrAssert(glfwExt != nullptr && glfwCount > 0, "GLFW did not report Vulkan instance extensions.");
        instanceEnabledExtensions.assign(glfwExt, glfwExt + glfwCount);
        
        if constexpr (isDebugMode)
        {
            auto addIfMissing = [](std::vector<std::string> &list, std::string_view item) {
                if (std::ranges::none_of(list, [item](const auto &s) { return s == item; }))
                    list.push_back(std::string(item));
            };
            constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
            nrAssert(
                hasInstanceLayer(validationLayer),
                std::format(
                    "Debug builds require '{}'. The Vulkan loader did not enumerate this layer on the current machine. "
                    "Validation layers are provided by the Vulkan SDK / validation-layer installation, not by the GPU or display driver.",
                    validationLayer));
            addIfMissing(instanceEnabledLayers, validationLayer);

            if (hasInstanceExtension(vk::EXTDebugUtilsExtensionName))
            {
                addIfMissing(instanceEnabledExtensions, vk::EXTDebugUtilsExtensionName);
            }
            else
            {
                nrInfo<LogLevel::error>("VK_EXT_debug_utils is unavailable in a debug build; debug labels and object names require this extension.");
                nrAssert(false, "Debug builds require VK_EXT_debug_utils.");
            }
        }
    }

    [[nodiscard]] std::uint32_t requiredQueueFamily(QueueFamilyKind kind) const
    {
        std::size_t index = static_cast<std::size_t>(kind);
        std::size_t familyIndex = queueFamilyDict[index];
        nrAssert(familyIndex != std::numeric_limits<std::size_t>::max(), "Queue family not found - device capability contract violated.");
        return static_cast<std::uint32_t>(familyIndex);
    }

    [[nodiscard]] std::uint32_t presentQueueFamilyIndex() const
    {
        return requiredQueueFamily(QueueFamilyKind::compute);
    }

    void refreshPresentSemaphores()
    {
        auto swapchainImageCount = presentationContext.swapchainImageCount();
        auto upToDate = presentSemaphoresByImage_.size() == swapchainImageCount &&
                        std::ranges::all_of(presentSemaphoresByImage_, [](const vk::raii::Semaphore& semaphore) {
                            return *semaphore != nullptr;
                        });
        if (upToDate)
        {
            return;
        }

        presentSemaphoresByImage_.clear();
        presentSemaphoresByImage_.reserve(swapchainImageCount);

        auto semaphoreCreateInfo = vk::SemaphoreCreateInfo{};
        auto imageIndices = std::views::iota(std::uint32_t{0}, swapchainImageCount);
        std::ranges::for_each(imageIndices, [&](std::uint32_t) {
            presentSemaphoresByImage_.emplace_back(device, semaphoreCreateInfo);
        });
    }

    [[nodiscard]] const vk::raii::Semaphore& activePresentSemaphore() const
    {
        auto imageIndex = presentationContext.activeSwapchainImageIndex();
        nrAssert(
            imageIndex < presentSemaphoresByImage_.size(),
            std::format("Device::activePresentSemaphore image index {} is out of range for {} present semaphores.", imageIndex, presentSemaphoresByImage_.size()));
        return presentSemaphoresByImage_[imageIndex];
    }

    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    std::vector<std::string> deviceEnabledExtensions{
        vk::KHRSwapchainExtensionName,
        vk::KHRDeferredHostOperationsExtensionName,
        vk::EXTMeshShaderExtensionName,
        vk::KHRAccelerationStructureExtensionName,
        vk::KHRRayTracingPipelineExtensionName,
        vk::KHRPipelineLibraryExtensionName,
        vk::KHRRayQueryExtensionName,
        vk::EXTOpacityMicromapExtensionName,
        vk::EXTRayTracingInvocationReorderExtensionName,
        vk::NVCooperativeVectorExtensionName,
        vk::EXTExtendedDynamicState3ExtensionName,
        vk::EXTMemoryBudgetExtensionName,
    };
    RayTracingCapabilitySnapshot rtCapabilities_{};
    DescriptorIndexingCapabilitySnapshot descriptorIndexingCapabilities_{};
    BufferDeviceAddressCapabilitySnapshot bufferDeviceAddressCapabilities_{};
    Vulkan14CapabilitySnapshot vulkan14Capabilities_{};
    Vulkan14PropertySnapshot vulkan14Properties_{};

    std::array<std::size_t, static_cast<std::size_t>(QueueFamilyKind::size)> queueFamilyDict{};
    SwapChainConfig swapChainConfig_{};
    std::uint32_t frameSubmitCount_ = 0;
    std::optional<QueueRole> frameFinalSubmitRole_{};
    std::vector<vk::raii::Semaphore> presentSemaphoresByImage_{};
};

void rhiTest()
{
    Device device;
    device.initialize("HelloVulkan", "VKEngine");
}
} // namespace nr::rhi
