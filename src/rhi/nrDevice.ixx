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
import :resourceOps;
import nr.utils;
import std;

export namespace nr::rhi
{
class Device
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

    void initialize(std::string const &_appName = {"DefaultApp"}, std::string const &_engineName = {"DefaultEngine"})
    {
        appName = _appName;
        engineName = _engineName;
        setupInitialFlags();
        instance = makeInstance();
        if constexpr (isDebugMode)
        {
            debugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(instance, makeDebugUtilsMessengerCreateInfoEXT());
        }
        physicalDevice = selectPhysicalDevice(instance);
        device = makeDevice();

        memoryAllocator.initialize(instance, physicalDevice, device);
        resourceFactory.initialize(memoryAllocator, device);
        resourcePool.initialize(memoryAllocator, device);

        initializeCommandSystem();
        uploadReadbackContext_.emplace(device, resourceFactory, queueManager);

        presentationContext.initialize(instance, physicalDevice, device, appName, swapChainConfig_, presentQueueFamilyIndex());
        pipelineService.bindDevice(device);
    }

    [[nodiscard]] FrameBeginResult beginFrame(uint64_t acquireTimeout = std::numeric_limits<uint64_t>::max())
    {
        auto &frame = frameManager.current();
        nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");

        const auto frameIndex = static_cast<uint32_t>(frameManager.currentIndex());
        resourcePool.resetFrame(frameIndex);
        memoryAllocator.resetFramePool(frameIndex);

        frame.resetFence();
        frame.resetPools();
        const auto workerCount = std::min<uint32_t>(maxThreads, std::max(1u, std::thread::hardware_concurrency()));
        frame.prepareSecondaryPools(workerCount, workerCount, 0u);

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
            .frameIndex = frameIndex,
            .swapchainImageIndex = acquire.imageIndex,
            .swapchainResult = acquire.result,
        };
    }

    void submitFrame(const CommandBatch &batch, QueueRole submitRole = QueueRole::Graphics)
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

    [[nodiscard]] PresentResult endFrame(const CommandBatch &batch, QueueRole submitRole = QueueRole::Graphics)
    {
        submitFrame(batch, submitRole);
        return presentFrame();
    }

    vk::raii::Instance makeInstance(uint32_t apiVersion = vk::ApiVersion14) const
    {
        const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
        std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
        std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);

        vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
        vk::InstanceCreateInfo instanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions);
        if constexpr (isDebugMode)
        {
            debugCreateInfo = makeDebugUtilsMessengerCreateInfoEXT();
            instanceCreateInfo.pNext = &debugCreateInfo;
        }
        return vk::raii::Instance(context, instanceCreateInfo);
    }

    vk::raii::Device makeDevice()
    {
        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
        std::ranges::fill(queueFamilyDict, std::numeric_limits<size_t>::max());

        const auto queueIndices = std::views::iota(size_t{0}, queueFamilyProperties.size());
        auto findFirst = [&](auto predicate) -> std::optional<size_t> {
            auto it = std::ranges::find_if(queueIndices, predicate);
            if (it == std::ranges::end(queueIndices))
            {
                return std::nullopt;
            }
            return *it;
        };

        const auto hasFlags = [&](size_t i, vk::QueueFlags flags) {
            return (queueFamilyProperties[i].queueFlags & flags) == flags;
        };
        
        auto toQueueIndex = [](QueueFamilyKind kind) { return static_cast<size_t>(kind); };

        auto graphicsFamily = findFirst([&](size_t i) { return hasFlags(i, vk::QueueFlagBits::eGraphics); });
        nrAssert(graphicsFamily.has_value(), "No graphics queue family available.");
        queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)] = *graphicsFamily;

        auto dedicatedCompute = findFirst([&](size_t i) {
            auto flags = queueFamilyProperties[i].queueFlags;
            return (flags & vk::QueueFlagBits::eCompute) && !(flags & vk::QueueFlagBits::eGraphics);
        });
        queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)] = dedicatedCompute.value_or(*graphicsFamily);

        auto dedicatedTransfer = findFirst([&](size_t i) {
            auto flags = queueFamilyProperties[i].queueFlags;
            return (flags & vk::QueueFlagBits::eTransfer) && !(flags & vk::QueueFlagBits::eCompute) && !(flags & vk::QueueFlagBits::eGraphics);
        });
        queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)] = dedicatedTransfer.value_or(queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]);

        constexpr float queuePriority = 1.0f;
        auto uniqueFamilies = std::array{
            static_cast<uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)]),
            static_cast<uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]),
            static_cast<uint32_t>(queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)])
        };
        std::ranges::sort(uniqueFamilies);
        
        auto queueCreateInfos = uniqueFamilies | 
                                std::views::filter([last = uint32_t(-1)](uint32_t f) mutable { 
                                    if (f == last) return false; 
                                    last = f; 
                                    return true; 
                                }) |
                                std::views::transform([&](uint32_t familyIndex) {
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
            enableExtension(extensionName, reason);
        });

        auto features2 = physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2,
                                                     vk::PhysicalDeviceVulkan12Features,
                                                     vk::PhysicalDeviceVulkan13Features,
                                                     vk::PhysicalDeviceVulkan14Features,
                                                     vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV,
                                                     vk::PhysicalDeviceCooperativeVectorFeaturesNV,
                                                     vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT,
                                                     vk::PhysicalDeviceAccelerationStructureFeaturesKHR,
                                                     vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
                                                     vk::PhysicalDeviceRayQueryFeaturesKHR>();

        auto &featureList = features2.get<vk::PhysicalDeviceFeatures2>();
        auto &vulkan12Features = features2.get<vk::PhysicalDeviceVulkan12Features>();
        auto &vulkan13Features = features2.get<vk::PhysicalDeviceVulkan13Features>();
        auto &invocationReorderFeatures = features2.get<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV>();
        auto &cooperativeVectorFeatures = features2.get<vk::PhysicalDeviceCooperativeVectorFeaturesNV>();
        auto &extendedDynamicState3Features = features2.get<vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT>();
        auto &accelerationStructureFeatures = features2.get<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>();
        auto &rayTracingPipelineFeatures = features2.get<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>();
        auto &rayQueryFeatures = features2.get<vk::PhysicalDeviceRayQueryFeaturesKHR>();

#define REQUIRE_FEATURE(feature_field, feature_name) \
        nrAssert(feature_field == vk::True, std::format("Required feature {} is not enabled.", feature_name))

        REQUIRE_FEATURE(vulkan12Features.bufferDeviceAddress, "bufferDeviceAddress");
        REQUIRE_FEATURE(vulkan12Features.descriptorIndexing, "descriptorIndexing");
        REQUIRE_FEATURE(vulkan13Features.inlineUniformBlock, "inlineUniformBlock");
        REQUIRE_FEATURE(vulkan13Features.dynamicRendering, "dynamicRendering");
        REQUIRE_FEATURE(vulkan13Features.synchronization2, "synchronization2");
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3PolygonMode, "extendedDynamicState3PolygonMode");
        REQUIRE_FEATURE(extendedDynamicState3Features.extendedDynamicState3RasterizationSamples, "extendedDynamicState3RasterizationSamples");
        REQUIRE_FEATURE(accelerationStructureFeatures.accelerationStructure, "accelerationStructure");
        REQUIRE_FEATURE(rayTracingPipelineFeatures.rayTracingPipeline, "rayTracingPipeline");
        REQUIRE_FEATURE(rayQueryFeatures.rayQuery, "rayQuery");
        REQUIRE_FEATURE(invocationReorderFeatures.rayTracingInvocationReorder, "rayTracingInvocationReorder");
        REQUIRE_FEATURE(cooperativeVectorFeatures.cooperativeVector, "cooperativeVector");

#undef REQUIRE_FEATURE

        vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr, &featureList);
        return vk::raii::Device(physicalDevice, deviceCreateInfo);
    }

    void initializeCommandSystem()
    {
        uint32_t graphicsFamily = getQueueFamilyWithFallback(QueueFamilyKind::graphics);
        uint32_t computeFamily = getQueueFamilyWithFallback(QueueFamilyKind::compute);
        uint32_t transferFamily = getQueueFamilyWithFallback(QueueFamilyKind::transfer);

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
        if (device)
        {
            waitIdle();
        }
    }

  protected:
    void setupInitialFlags()
    {
        uint32_t glfwCount = 0;
        const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
        instanceEnabledExtensions.assign(glfwExt, glfwExt + glfwCount);
        
        if constexpr (isDebugMode)
        {
            auto addIfMissing = [](std::vector<std::string> &list, std::string_view item) {
                if (std::ranges::none_of(list, [item](const auto &s) { return s == item; }))
                    list.push_back(std::string(item));
            };
            addIfMissing(instanceEnabledLayers, "VK_LAYER_KHRONOS_validation");
            addIfMissing(instanceEnabledExtensions, vk::EXTDebugUtilsExtensionName);
        }
    }

    [[nodiscard]] uint32_t getQueueFamilyWithFallback(QueueFamilyKind kind) const
    {
        size_t index = static_cast<size_t>(kind);
        size_t familyIndex = queueFamilyDict[index];
        nrAssert(familyIndex != std::numeric_limits<size_t>::max(), "Queue family not found - device capability contract violated.");
        return static_cast<uint32_t>(familyIndex);
    }

    [[nodiscard]] uint32_t presentQueueFamilyIndex() const
    {
        return getQueueFamilyWithFallback(QueueFamilyKind::compute);
    }

    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    std::vector<std::string> deviceEnabledExtensions{
        vk::KHRSwapchainExtensionName,
        vk::KHRDeferredHostOperationsExtensionName,
        vk::KHRAccelerationStructureExtensionName,
        vk::KHRRayTracingPipelineExtensionName,
        vk::KHRRayQueryExtensionName,
        vk::EXTRayTracingInvocationReorderExtensionName,
        vk::NVCooperativeVectorExtensionName,
        vk::EXTExtendedDynamicState3ExtensionName,
        vk::EXTMemoryBudgetExtensionName,
    };

    std::array<size_t, static_cast<size_t>(QueueFamilyKind::size)> queueFamilyDict{};
    SwapChainConfig swapChainConfig_{};
};

void rhiTest()
{
    Device device;
    device.initialize("HelloVulkan", "VKEngine");
}
} // namespace nr::rhi
