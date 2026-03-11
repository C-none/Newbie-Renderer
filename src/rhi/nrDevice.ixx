module;
export module nr.rhi:device;

import dependency;
import :vk;
import :surface;
import :swapchain;
import :queue;
import :frameContext;
import :memoryAllocator;
import :resourcePool;
import :pipeline;
import nr.utils;
import std;

template <typename T>
concept HasCustomSetupInitialFlags = requires(T *t) {
    { t->setupInitialFlags() } -> std::same_as<void>;
};

// Concept to validate Device compliance
template <typename T>
concept ValidDevice = requires(T *d, std::string_view name) {
    typename T::element_type; // Must have pointer-like behavior if template
} || requires(T *d) {
    { d->setupInitialFlags() } -> std::same_as<void>;
};

export namespace nr::rhi
{
template <typename Derived> class Device
{
  public:
        struct FrameBeginResult
        {
                uint32_t frameIndex = 0;
                uint32_t swapchainImageIndex = 0;
        vk::Result swapchainResult = vk::Result::eSuccess;
        };

    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    // vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};

    // Memory stack lifetime:
    //   frameResourceArena/resourceFactory -> memoryAllocator -> vk::raii::Device
    // (declared in reverse dependency order for safe destruction)
    MemoryAllocator memoryAllocator;
    ResourceFactory resourceFactory;
    ResourcePool resourcePool;

    QueueManager queueManager;
    FrameManager frameManager;

    PresentationContext presentationContext;
    PipelineService pipelineService;

    Device() = default;
    Device(Device &) = delete;
    Device &operator=(Device &) = delete;

    void initialize(std::string const &_appName = {"DefaultApp"}, std::string const &_engineName = {"DefaultEngine"})
    {
        appName = _appName;
        engineName = _engineName;
        setupInitialFlags();
        if constexpr (HasCustomSetupInitialFlags<Derived>)
        {
            static_cast<Derived *>(this)->setupInitialFlags();
        }
        instance = makeInstance();
        // if constexpr (isDebugMode)
        //{
        //     debugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(instance, makeDebugUtilsMessengerCreateInfoEXT());
        // }
        physicalDevice = selectPhysicalDevice(instance);
        device = makeDevice();

        // Initialize memory stack in dependency order:
        // 1) MemoryAllocator owns VMA allocator and internal pools
        // 2) ResourceFactory provides persistent caller-owned Buffer/Image creation
        // 3) ResourcePool provides frame-local scratch allocation only
        memoryAllocator.initialize(instance, physicalDevice, device);
        resourceFactory.initialize(memoryAllocator, device);
        resourcePool.initialize(memoryAllocator, device);

        // Initialize command submission system after device creation
        initializeCommandSystem();

        presentationContext.initialize(instance, physicalDevice, device, appName, swapChainConfig_, presentQueueFamilyIndex());
        pipelineService.bindDevice(device);
    }

    /**
     * @brief Begin one frame transaction.
     *
     * Waits for the current frame fence, resets per-frame state, acquires one swapchain image,
     * and lazily recreates the swapchain when acquire returns out-of-date/suboptimal.
     */
    [[nodiscard]] FrameBeginResult beginFrame(uint64_t acquireTimeout = std::numeric_limits<uint64_t>::max())
    {
        auto &frame = frameManager.current();
        nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");
        frame.resetFence();
        frame.resetPools();
        frame.prepareSecondaryPools();

        auto acquire = presentationContext.acquireNextImage(frame.imageAvailable(), acquireTimeout);
        if (PresentationContext::needsSwapchainRecreate(acquire.result))
        {
            presentationContext.recreate(physicalDevice, device, queueManager);
            acquire = presentationContext.acquireNextImage(frame.imageAvailable(), acquireTimeout);
        }

        nrAssert(acquire.result != vk::Result::eErrorOutOfDateKHR, "Device::beginFrame failed to acquire a valid swapchain image after recreation.");

        presentationContext.setActiveSwapchainImage(acquire.imageIndex);
        presentationContext.setFrameSubmitted(false);

        return FrameBeginResult{
            .frameIndex = static_cast<uint32_t>(frameManager.currentIndex()),
            .swapchainImageIndex = acquire.imageIndex,
            .swapchainResult = acquire.result,
        };
    }

    /**
     * @brief Submit one frame batch using frame-semaphores wired for present.
     *
     * Automatically waits on `imageAvailable` and signals `renderFinished` for the current frame.
     */
    void submitFrame(const CommandBatch &batch, QueueRole submitRole = QueueRole::Compute)
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::submitFrame requires beginFrame() before submission.");

        auto &frame = frameManager.current();
        auto submitBatch = batch;
        submitBatch.addWait(frame.imageAvailable(), vk::PipelineStageFlagBits2::eColorAttachmentOutput);
        submitBatch.addSignal(frame.renderFinished());

        if (submitRole == QueueRole::Graphics)
        {
            queueManager.graphics().submit(submitBatch, std::cref(frame.fence()));
        }
        else if (submitRole == QueueRole::Compute)
        {
            queueManager.compute().submit(submitBatch, std::cref(frame.fence()));
        }
        else
        {
            queueManager.transfer().submit(submitBatch, std::cref(frame.fence()));
        }

        presentationContext.setFrameSubmitted(true);
    }

    /**
     * @brief Present the current frame and rotate frame-in-flight index.
     *
     * Uses compute queue as present carrier by default. If present reports out-of-date/suboptimal,
     * swapchain recreation is triggered before advancing to the next frame.
     */
    [[nodiscard]] PresentResult presentFrame()
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(), "Device::presentFrame requires beginFrame() before present.");
        nrAssert(presentationContext.hasSubmittedCurrentFrame(), "Device::presentFrame requires submitFrame() before present.");

        auto &frame = frameManager.current();
        auto presentResult = presentationContext.present(queueManager, frame.renderFinished());

        if (PresentationContext::needsSwapchainRecreate(presentResult.result))
        {
            presentationContext.recreate(physicalDevice, device, queueManager);
        }

        frameManager.advanceFrame();
        presentationContext.clearActiveSwapchainImage();
        presentationContext.setFrameSubmitted(false);

        return presentResult;
    }

    /**
     * @brief Convenience API for the common frame tail path: submit then present.
     */
    [[nodiscard]] PresentResult endFrame(const CommandBatch &batch, QueueRole submitRole = QueueRole::Compute)
    {
        submitFrame(batch, submitRole);
        return presentFrame();
    }

    vk::raii::Instance makeInstance(uint32_t apiVersion = vk::ApiVersion14) const
    {
        const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
        std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
        std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);
        return vk::raii::Instance(context, vk::InstanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions));
    }

    vk::raii::Device makeDevice()
    {
        auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();

        auto isExtensionSupported = [&availableExtensions](std::string_view extensionName) { return std::ranges::any_of(availableExtensions, [extensionName](const vk::ExtensionProperties &property) { return std::string_view(property.extensionName) == extensionName; }); };

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
            std::string_view reason = "modern pipeline backend";
            if (extensionName == vk::KHRDynamicRenderingExtensionName)
                reason = "dynamic rendering path";
            else if (extensionName == vk::EXTExtendedDynamicStateExtensionName)
                reason = "extended dynamic pipeline state";
            else if (extensionName == vk::EXTExtendedDynamicState3ExtensionName)
                reason = "extended dynamic pipeline state v3";
            else if (extensionName == vk::KHRPipelineLibraryExtensionName)
                reason = "pipeline library path";
            else if (extensionName == vk::EXTDescriptorIndexingExtensionName)
                reason = "descriptor indexing path";
            else if (extensionName == vk::EXTPipelineRobustnessExtensionName)
                reason = "pipeline robustness controls";

            enableExtension(extensionName, reason);
        });

        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // Populate queueFamilyDict with queue family indices for each queue type

        {
            // record the index of each queue family
            enum class MatchType
            {
                Contains,
                Exact
            };
            const std::array<std::tuple<QueueFamilyKind, vk::QueueFlags, MatchType>, 6> queueKindMapping = {{
                {QueueFamilyKind::graphics, vk::QueueFlagBits::eGraphics, MatchType::Contains},
                {QueueFamilyKind::compute, vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding | vk::QueueFlagBits::eCompute, MatchType::Exact},
                {QueueFamilyKind::transfer, vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding, MatchType::Exact},
                {QueueFamilyKind::videoDecode, vk::QueueFlagBits::eVideoDecodeKHR, MatchType::Contains},
                {QueueFamilyKind::videoEncode, vk::QueueFlagBits::eVideoEncodeKHR, MatchType::Contains},
                {QueueFamilyKind::opticalFlow, vk::QueueFlagBits::eOpticalFlowNV, MatchType::Contains},
            }};

            auto results = std::views::cartesian_product(std::views::enumerate(queueFamilyProperties), queueKindMapping) | std::views::filter([](auto const &pair) {
                               auto const &[i, props] = std::get<0>(pair);
                               auto const &[kind, flags, matchType] = std::get<1>(pair);
                               return matchType == MatchType::Exact ? (props.queueFlags == flags) : ((props.queueFlags & flags) == flags);
                           }) |
                           std::views::transform([](auto const &pair) {
                               auto const &[i, props] = std::get<0>(pair);
                               auto const &[kind, flags, matchType] = std::get<1>(pair);
                               return std::make_pair(kind, i);
                           });

            // Initialize with SIZE_MAX to mark "not found"
            std::ranges::fill(queueFamilyDict, std::numeric_limits<size_t>::max());
            // Populate found queue families
            std::ranges::for_each(results, [this](auto const &pair) { queueFamilyDict[static_cast<size_t>(pair.first)] = pair.second; });
        }
        constexpr float queuePriority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = queueFamilyProperties | std::views::enumerate | std::views::transform([&](auto &&p) {
                                                                      auto &&[i, _] = p;
                                                                      return vk::DeviceQueueCreateInfo({}, static_cast<uint32_t>(i), 1, &queuePriority);
                                                                  }) |
                                                                  std::ranges::to<std::vector>();

        // Query features into a chain, then pass the same chain to DeviceCreateInfo.
        auto features2 = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceBufferDeviceAddressFeaturesEXT, vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV, vk::PhysicalDeviceDynamicRenderingFeatures, vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
                                                     vk::PhysicalDevicePipelineRobustnessFeatures, vk::PhysicalDeviceSynchronization2Features, vk::PhysicalDeviceDescriptorIndexingFeatures, vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                                                     vk::PhysicalDeviceRayQueryFeaturesKHR>();
        auto featureList = features2.get<vk::PhysicalDeviceFeatures2>();
        [[maybe_unused]] auto &bufferDeviceAddressFeatures = features2.get<vk::PhysicalDeviceBufferDeviceAddressFeaturesEXT>();
        [[maybe_unused]] auto &invocationReorderFeatures = features2.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV>();
        [[maybe_unused]] auto &dynamicRenderingFeatures = features2.get<vk::PhysicalDeviceDynamicRenderingFeatures>();
        [[maybe_unused]] auto &extendedDynamicStateFeatures = features2.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>();
        [[maybe_unused]] auto &pipelineRobustnessFeatures = features2.get<vk::PhysicalDevicePipelineRobustnessFeatures>();
        [[maybe_unused]] auto &synchronization2Features = features2.get<vk::PhysicalDeviceSynchronization2Features>();
        [[maybe_unused]] auto &descriptorIndexingFeatures = features2.get<vk::PhysicalDeviceDescriptorIndexingFeatures>();
        [[maybe_unused]] auto &accelerationStructureFeatures = features2.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        [[maybe_unused]] auto &rayTracingPipelineFeatures = features2.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
        [[maybe_unused]] auto &rayQueryFeatures = features2.get<vk::PhysicalDeviceRayQueryFeaturesKHR>();

        vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr, featureList.pNext);

        auto resultDevice = vk::raii::Device(physicalDevice, deviceCreateInfo);

        return resultDevice;
    }

    /**
     * @brief Initialize the command submission system (queues and frame management)
     *
     * This method sets up:
     * 1. QueueManager with GpuQueue instances for graphics, compute, and transfer
     * 2. FrameManager with per-frame command pools and synchronization primitives
     *
     * Called automatically by initialize() after device creation.
     */

    void initializeCommandSystem()
    {
        // Queue families are mandatory under the hard-fail device capability contract.
        uint32_t graphicsFamily = getQueueFamilyWithFallback(QueueFamilyKind::graphics);

        // Compute and transfer queues are also required; helper asserts if unavailable.
        uint32_t computeFamily = getQueueFamilyWithFallback(QueueFamilyKind::compute);
        uint32_t transferFamily = getQueueFamilyWithFallback(QueueFamilyKind::transfer);

        // Create typed queue wrappers
        GpuQueue graphicsQueue(device, graphicsFamily, QueueRole::Graphics);
        GpuQueue computeQueue(device, computeFamily, QueueRole::Compute);
        GpuQueue transferQueue(device, transferFamily, QueueRole::Transfer);

        // Initialize QueueManager with all queues
        queueManager = QueueManager(std::move(graphicsQueue), std::move(computeQueue), std::move(transferQueue));

        // Configure pool settings for each queue type
        FrameContext::PoolConfig graphicsConfig{.queueFamilyIndex = graphicsFamily};
        FrameContext::PoolConfig computeConfig{.queueFamilyIndex = computeFamily};
        FrameContext::PoolConfig transferConfig{.queueFamilyIndex = transferFamily};

        // Create FrameManager with N frames in flight
        frameManager = FrameManager(device, graphicsConfig, computeConfig, transferConfig);

        nrInfo(std::format("Command system initialized: {} frames in flight, max {} worker threads", maxFrameInFlight, maxThreads));
    }

    /**
     * @brief Wait for all GPU work to complete and clean up resources
     *
     * Call before destruction to ensure safe cleanup.
     */
    void waitIdle()
    {
        // Wait for all queues to finish
        queueManager.waitAllIdle();

        // Wait for all frames to complete
        frameManager.waitAll();
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

    ~Device() = default;

  protected:
    void setupInitialFlags()
    {
        uint32_t glfwCount = 0;
        const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
        for (uint32_t i = 0; i < glfwCount; ++i)
        {
            instanceEnabledExtensions.push_back(glfwExt[i]);
        }
        if constexpr (isDebugMode)
        {
            if (std::ranges::none_of(instanceEnabledLayers, [](std::string const &layer) { return layer == "VK_LAYER_KHRONOS_validation"; }))
            {
                // Check CPU error. switch to GPU AV to ckech error on GPU side.
                instanceEnabledLayers.push_back("VK_LAYER_KHRONOS_validation");
            }
            if (std::ranges::none_of(instanceEnabledExtensions, [](std::string const &ext) { return ext == vk::EXTDebugUtilsExtensionName; }))
            {
                instanceEnabledExtensions.push_back(vk::EXTDebugUtilsExtensionName);
            }
        }
    }

    [[nodiscard]] uint32_t getQueueFamilyWithFallback(QueueFamilyKind kind) const
    {
        size_t index = static_cast<size_t>(kind);
        size_t familyIndex = queueFamilyDict[index];

        // If found (not SIZE_MAX), return it
        if (familyIndex != std::numeric_limits<size_t>::max())
        {
            return static_cast<uint32_t>(familyIndex);
        }
        // Hard-fail policy: required queue families are part of the device contract.
        nrAssert(false, "Queue family not found - device capability contract violated.");
        std::unreachable();
    }

    [[nodiscard]] uint32_t presentQueueFamilyIndex() const
    {
        // Stage-1 policy: compute queue is the present carrier.
        return getQueueFamilyWithFallback(QueueFamilyKind::compute);
    }

    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    // std::vector<std::string> physicalDeviceFeatures{}; // Currently not used
    std::vector<std::string> deviceEnabledExtensions{
        vk::KHRSwapchainExtensionName,           vk::KHRDeferredHostOperationsExtensionName, vk::KHRAccelerationStructureExtensionName, vk::KHRRayTracingPipelineExtensionName,   vk::KHRRayQueryExtensionName,        vk::NVRayTracingInvocationReorderExtensionName,
        vk::KHRBufferDeviceAddressExtensionName, vk::KHRDynamicRenderingExtensionName,       vk::EXTExtendedDynamicState3ExtensionName, vk::EXTExtendedDynamicStateExtensionName, vk::KHRPipelineLibraryExtensionName, vk::EXTDescriptorIndexingExtensionName,
        vk::EXTPipelineRobustnessExtensionName,  vk::KHRSynchronization2ExtensionName,       vk::EXTMemoryBudgetExtensionName,
    };

    std::array<size_t, static_cast<size_t>(QueueFamilyKind::size)> queueFamilyDict{};
    SwapChainConfig swapChainConfig_{};
};
void rhiTest()
{
    Device<void> device;
    device.initialize("HelloVulkan", "VKEngine");
}
} // namespace nr::rhi
