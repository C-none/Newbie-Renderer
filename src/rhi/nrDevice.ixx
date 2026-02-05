module;
export module nr.rhi:device;

import dependency;
import :vk;
import :surface;
import :swapchain;
import :queue;
import :frameContext;
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
    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    // vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};
    Surface surface;
    SwapChain swapChain;

    // =========================================================================
    // COMMAND SUBMISSION SYSTEM MEMBERS
    // =========================================================================
    QueueManager queueManager;
    FrameManager frameManager;
    // =========================================================================

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

        // Initialize command submission system after device creation
        initializeCommandSystem();

        auto [s, sc] = makeSurfaceAndSwapChain();
        surface = std::move(s);
        swapChain = std::move(sc);
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
        // Optimize: Direct conversion without intermediate std::set
        auto enabledExtensions = deviceEnabledExtensions | std::views::transform([](std::string_view ext) { return ext.data(); }) | std::ranges::to<std::vector<char const *>>();

        auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

        // Populate queueFamilyDict with queue family indices for each queue type

        {
            // record the index of each queue family
            enum class MatchType
            {
                Contains,
                Exact
            };
            const std::array<std::tuple<QueueKind, vk::QueueFlags, MatchType>, 6> queueKindMapping = {{
                {QueueKind::graphics, vk::QueueFlagBits::eGraphics, MatchType::Contains},
                {QueueKind::compute, vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding | vk::QueueFlagBits::eCompute, MatchType::Exact},
                {QueueKind::transfer, vk::QueueFlagBits::eTransfer | vk::QueueFlagBits::eSparseBinding, MatchType::Exact},
                {QueueKind::videoDecode, vk::QueueFlagBits::eVideoDecodeKHR, MatchType::Contains},
                {QueueKind::videoEncode, vk::QueueFlagBits::eVideoEncodeKHR, MatchType::Contains},
                {QueueKind::opticalFlow, vk::QueueFlagBits::eOpticalFlowNV, MatchType::Contains},
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

            std::ranges::for_each(results, [this](auto const &pair) { queueFamilyDict[static_cast<size_t>(pair.first)] = pair.second; });
        }
        constexpr float queuePriority = 1.0f;
        std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = queueFamilyProperties | std::views::enumerate | std::views::transform([&](auto &&p) {
                                                                      auto &&[i, _] = p;
                                                                      return vk::DeviceQueueCreateInfo({}, static_cast<uint32_t>(i), 1, &queuePriority);
                                                                  }) |
                                                                  std::ranges::to<std::vector>();

        // VK_EXT_pageable_device_local_memory
        vk::PhysicalDevicePageableDeviceLocalMemoryFeaturesEXT pageableFeatures(true, nullptr);

        vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr, &pageableFeatures);

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
        // =====================================================================
        // STEP 1: Create GpuQueues from queue family indices
        // =====================================================================
        // Queue family indices were populated during makeDevice()
        uint32_t graphicsFamily = static_cast<uint32_t>(queueFamilyDict[static_cast<size_t>(QueueKind::graphics)]);
        uint32_t computeFamily = static_cast<uint32_t>(queueFamilyDict[static_cast<size_t>(QueueKind::compute)]);
        uint32_t transferFamily = static_cast<uint32_t>(queueFamilyDict[static_cast<size_t>(QueueKind::transfer)]);

        // Create typed queue wrappers
        GpuQueue graphicsQueue(device, graphicsFamily, QueueType::Graphics);
        GpuQueue computeQueue(device, computeFamily, QueueType::Compute);
        GpuQueue transferQueue(device, transferFamily, QueueType::Transfer);

        // Initialize QueueManager with all queues
        queueManager = QueueManager(std::move(graphicsQueue), std::move(computeQueue), std::move(transferQueue));

        // =====================================================================
        // STEP 2: Create FrameManager with per-frame command pools
        // =====================================================================
        // Configure pool settings for each queue type
        FrameContext::PoolConfig graphicsConfig{.queueFamilyIndex = graphicsFamily};
        FrameContext::PoolConfig computeConfig{.queueFamilyIndex = computeFamily};
        FrameContext::PoolConfig transferConfig{.queueFamilyIndex = transferFamily};

        // Create FrameManager with N frames in flight
        frameManager = FrameManager(device, graphicsConfig, computeConfig, transferConfig);

        nrInfo(std::format("Command system initialized: {} frames in flight, max {} worker threads", maxFrameInFlight, maxThreads));
    }
    
    std::tuple<Surface, SwapChain> makeSurfaceAndSwapChain()
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        Surface resultSurface;
        resultSurface.handle.reset(glfw::createWindow(resultSurface.extent.width, resultSurface.extent.height, appName.c_str(), nullptr, nullptr));
        VkSurfaceKHR rawSurface;
        vk::detail::resultCheck(glfw::createWindowSurface(*instance, resultSurface.handle.get(), nullptr, &rawSurface), "Failed to create window surface");

        resultSurface.surface = vk::raii::SurfaceKHR(instance, rawSurface);

        nrAssert(physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(queueFamilyDict[static_cast<size_t>(QueueKind::compute)]), resultSurface.surface), std::format("Compute queue does not support present"));
        std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(resultSurface.surface);
        nrAssert(!formats.empty(), std::format("No available surface formats"));

        auto selectedFormat = [&formats]() -> vk::SurfaceFormatKHR {
            auto it = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eB8G8R8A8Srgb; });
            if (it != formats.end())
                return *it;

            it = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eR8G8B8A8Srgb; });
            if (it != formats.end())
                return *it;
            nrInfo<nr::LogLevel::warning>(std::format("Your device does not support basic sRGB format. You may need to convert output color space manually. You may choose graphic queue instead."));
            return formats.front();
        }();
        resultSurface.format = selectedFormat.format;

        vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(resultSurface.surface);
        // auto property = physicalDevice.getImageFormatProperties(resultSurface.format, vk::ImageType::e2D, vk::ImageTiling::eLinear, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eStorage);
        vk::SwapchainCreateInfoKHR swapChainCreateInfo(vk::SwapchainCreateFlagsKHR(), resultSurface.surface, std::clamp(3u, surfaceCapabilities.minImageCount, surfaceCapabilities.maxImageCount), selectedFormat.format, selectedFormat.colorSpace, resultSurface.extent, 1,
                                                       vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eColorAttachment, vk::SharingMode::eExclusive, {}, vk::SurfaceTransformFlagBitsKHR::eIdentity, vk::CompositeAlphaFlagBitsKHR::eOpaque, vk::PresentModeKHR::eMailbox, vk::False);
        SwapChain resultSwapChain;
        resultSwapChain.swapChain = {device, swapChainCreateInfo};
        resultSwapChain.swapChainImages = resultSwapChain.swapChain.getImages();
        vk::ImageViewCreateInfo imageViewCreateInfo({}, {}, vk::ImageViewType::e2D, selectedFormat.format, {}, {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1});
        resultSwapChain.imageViews = resultSwapChain.swapChainImages | std::views::transform([&](const auto &img) {
                                         imageViewCreateInfo.image = img;
                                         return vk::raii::ImageView(device, imageViewCreateInfo);
                                     }) |
                                     std::ranges::to<std::vector>();
        return {std::move(resultSurface), std::move(resultSwapChain)};
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
            // if (std::ranges::none_of(instanceEnabledExtensions, [](std::string const &ext) { return ext == VK_EXT_DEBUG_UTILS_EXTENSION_NAME; }))
            //{
            //     instanceEnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            // }
        }
    }
    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    // std::vector<std::string> physicalDeviceFeatures{}; // Currently not used
    std::vector<std::string> deviceEnabledExtensions{vk::KHRAccelerationStructureExtensionName, vk::KHRRayTracingPipelineExtensionName, vk::KHRDeferredHostOperationsExtensionName, vk::KHRSwapchainExtensionName, vk::EXTMemoryPriorityExtensionName, vk::EXTPageableDeviceLocalMemoryExtensionName};
    std::array<size_t, static_cast<size_t>(QueueKind::size)> queueFamilyDict{};
};
void rhiTest()
{
    Device<void> device;
    device.initialize("HelloVulkan", "VKEngine");
}
} // namespace nr::rhi
