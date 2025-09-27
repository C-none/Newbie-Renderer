module;

#include <vk_mem_alloc.h>
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
template <typename Derived> void Device<Derived>::initialize(std::string const &appName, std::string const &engineName)
{
    setupInitialFlags();
    if constexpr (hasCustomSetupInitialFlags<Derived>)
    {
        static_cast<Derived *>(this)->setupInitialFlags();
    }
    instance = makeInstance(appName, engineName);
    if constexpr (isDebugMode())
    {
        debugUtilsMessenger = vk::raii::DebugUtilsMessengerEXT(instance, makeDebugUtilsMessengerCreateInfoEXT());
    }
    physicalDevice = selectPhysicalDevice(instance);
    device = makeDevice();
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

template <typename Derived> vk::raii::Instance Device<Derived>::makeInstance(std::string const &appName, std::string const &engineName, const uint32_t apiVersion) const
{
    const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
    std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
    std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);
    return vk::raii::Instance(context, vk::InstanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions));
}

template <typename Derived> vk::raii::Device Device<Derived>::makeDevice() const
{
    std::vector<char const *> enabledExtensions = deviceEnabledExtensions | std::ranges::to<std::set<std::string_view>>() | std::views::transform([](auto const &ext) { return ext.data(); }) | std::ranges::to<std::vector<char const *>>();

    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    constexpr float queuePriority = 1.0f;
    std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos = queueFamilyProperties | std::views::enumerate | std::views::transform([&](auto &&p) {
                                                                  auto &&[i, _] = p;
                                                                  return vk::DeviceQueueCreateInfo({}, static_cast<uint32_t>(i), 1, &queuePriority);
                                                              }) |
                                                              std::ranges::to<std::vector>();
    vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos, {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions, nullptr);

    return vk::raii::Device(physicalDevice, deviceCreateInfo);
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