module;

// #include <vk_mem_alloc.h>
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
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
    surface = makeSurface();
}

template <typename Derived> void Device<Derived>::setupInitialFlags()
{
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

template <typename Derived> Surface Device<Derived>::makeSurface()
{
    vk::su::WindowData window = vk::su::createWindow(AppName, {width, height});
    VkSurfaceKHR _surface;

    // glfwCreateWindowSurface(*instance, nullptr, nullptr, &_surface);
    vk::raii::SurfaceKHR surface(instance, _surface);
    nrAssert(physicalDevice.getSurfaceSupportKHR(queueFamilyDict[static_cast<size_t>(QueueKind::graphics)], surface), "Surface not supported");
    return Surface{extent : {window.width, window.height}, surface : surface, format : vk::Format::eB8G8R8A8Sr gb};
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
    print("hello from rhiTest:{}", foo(1));
    application();
}
} // namespace nr::rhi