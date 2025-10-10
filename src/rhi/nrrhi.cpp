module;

// #include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <GLFW/glfw3.h>

module nr.rhi;

import std;
import nr.utils;
import nr.rhi.vk;

template <typename T>
concept hasCustomSetupInitialFlags = requires(T *t) {
    { t->setupInitialFlags() } -> std::same_as<void>;
};

namespace nr::rhi
{

template <typename Derived> void Device<Derived>::initialize(std::string const &_appName, std::string const &_engineName)
{
    appName = _appName;
    engineName = _engineName;
    setupInitialFlags();
    if constexpr (hasCustomSetupInitialFlags<Derived>)
    {
        static_cast<Derived *>(this)->setupInitialFlags();
    }
    instance = makeInstance();
    if constexpr (isDebugMode())
    {
        debugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(instance, makeDebugUtilsMessengerCreateInfoEXT());
    }
    physicalDevice = selectPhysicalDevice(instance);
    device = makeDevice();
    std::apply(
        [this](Surface &&s, SwapChain &&sc) {
            surface = std::move(s);
            swapChain = std::move(sc);
        },
        makeSurfaceAndSwapChain());
}

template <typename Derived> void Device<Derived>::setupInitialFlags()
{
    uint32_t glfwCount = 0;
    const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
    for (uint32_t i = 0; i < glfwCount; ++i)
    {
        instanceEnabledExtensions.push_back(glfwExt[i]);
    }
    if constexpr (isDebugMode())
    {
        if (std::ranges::none_of(instanceEnabledLayers, [](std::string const &layer) { return layer == "VK_LAYER_KHRONOS_validation"; }))
        {
            instanceEnabledLayers.push_back("VK_LAYER_KHRONOS_validation");
        }
        if (std::ranges::none_of(instanceEnabledExtensions, [](std::string const &ext) { return ext == VK_EXT_DEBUG_UTILS_EXTENSION_NAME; }))
        {
            instanceEnabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }
}

template <typename Derived> vk::raii::Instance Device<Derived>::makeInstance(const uint32_t apiVersion) const
{
    const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
    std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
    std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);
    return vk::raii::Instance(context, vk::InstanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions));
}

template <typename Derived> vk::raii::Device Device<Derived>::makeDevice()
{
    std::vector<char const *> enabledExtensions = deviceEnabledExtensions | std::ranges::to<std::set<std::string_view>>() | std::views::transform([](auto const &ext) { return ext.data(); }) | std::ranges::to<std::vector<char const *>>();

    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

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
    vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr);

    return vk::raii::Device(physicalDevice, deviceCreateInfo);
}

template <typename Derived> std::tuple<Surface, SwapChain> Device<Derived>::makeSurfaceAndSwapChain()
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    Surface resultSurface;
    resultSurface.handle.reset(glfwCreateWindow(resultSurface.extent.width, resultSurface.extent.height, appName.c_str(), nullptr, nullptr));
    VkSurfaceKHR rawSurface;
    vk::detail::resultCheck(static_cast<vk::Result>(glfwCreateWindowSurface(*instance, resultSurface.handle.get(), nullptr, &rawSurface)), "Failed to create window surface");

    resultSurface.surface = vk::raii::SurfaceKHR(instance, rawSurface);

    nrAssert(physicalDevice.getSurfaceSupportKHR(static_cast<uint32_t>(queueFamilyDict[static_cast<size_t>(QueueKind::compute)]), resultSurface.surface))("Queue does not support present");
    std::vector<vk::SurfaceFormatKHR> formats = physicalDevice.getSurfaceFormatsKHR(resultSurface.surface);
    nrAssert(!formats.empty())("No available surface formats");

    auto selectedFormat = [&formats]() -> vk::SurfaceFormatKHR {
        auto it = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eB8G8R8A8Srgb; });
        if (it != formats.end())
            return *it;

        it = std::ranges::find_if(formats, [](const auto &f) { return f.format == vk::Format::eR8G8B8A8Srgb; });
        if (it != formats.end())
            return *it;
        nrInfo(nr::LogLevel::warning)("Your device does not support basic sRGB format. You may need to convert output color space manually.");
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

void application()
{
    Device<void> device;
    device.initialize("HelloVulkan", "VKEngine");
}
void rhiTest()
{
    using namespace std;
    auto foo = [](const int x) { return x + 1; };
    // print("hello from rhiTest:{}\n", foo(1));
    application();
}
} // namespace nr::rhi