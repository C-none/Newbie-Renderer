module;

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

module nr.rhi;

import std;
import nr.utils;

namespace nr
{
namespace rhi
{

VKAPI_ATTR vk::Bool32 VKAPI_CALL debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageTypes, const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void * /*pUserData*/)
{
    if constexpr (isDebugMode())
    {
        switch (static_cast<uint32_t>(pCallbackData->messageIdNumber))
        {
        case 0:
            // Validation Warning: Override layer has override paths set to C:/VulkanSDK/<version>/Bin
            return vk::False;
        case 0x822806fa:
            // Validation Warning: vkCreateInstance(): to enable extension VK_EXT_debug_utils, but this extension is intended to support use by applications when
            // debugging and it is strongly recommended that it be otherwise avoided.
            return vk::False;
        case 0xe8d1a9fe:
            // Validation Performance Warning: Using debug builds of the validation layers *will* adversely affect performance.
            return vk::False;
        }
    }

    const auto severityStr = vk::to_string(messageSeverity);
    const auto typesStr = vk::to_string(messageTypes);

    const char *idName = pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "";
    const char *message = pCallbackData->pMessage ? pCallbackData->pMessage : "";

    print("{}: {}:\n"
          "\tmessageIDName   = <{}>\n"
          "\tmessageIdNumber = {}\n"
          "\tmessage         = <{}>",
          severityStr, typesStr, idName, pCallbackData->messageIdNumber, message);

    auto queueLabels = std::span(pCallbackData->pQueueLabels, pCallbackData->queueLabelCount);
    if (!queueLabels.empty())
    {
        std::print("\tQueue Labels:");
        for (const auto &lbl : queueLabels)
        {
            std::print("\t\tlabelName = <{}>", lbl.pLabelName ? lbl.pLabelName : "");
        }
    }

    auto cmdBufLabels = std::span(pCallbackData->pCmdBufLabels, pCallbackData->cmdBufLabelCount);
    if (!cmdBufLabels.empty())
    {
        std::print("\tCommandBuffer Labels:");
        for (const auto &lbl : cmdBufLabels)
        {
            std::print("\t\tlabelName = <{}>", lbl.pLabelName ? lbl.pLabelName : "");
        }
    }

    auto objects = std::span(pCallbackData->pObjects, pCallbackData->objectCount);
    if (!objects.empty())
    {
        std::print("\tObjects:");
        for (size_t i = 0; i < objects.size(); ++i)
        {
            const auto &obj = objects[i];
            std::print("\t\tObject {}", i);
            std::print("\t\t\tobjectType   = {}", vk::to_string(obj.objectType));
            std::print("\t\t\tobjectHandle = {}", obj.objectHandle);
            if (obj.pObjectName)
            {
                std::print("\t\t\tobjectName   = <{}>", obj.pObjectName);
            }
        }
    }
    return vk::False;
}

// Create a structure chain for vk::InstanceCreateInfo, adding a debug messenger create info if in debug mode
// vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> if in debug mode
// vk::StructureChain<vk::InstanceCreateInfo> if not in debug mode
[[nodiscard]] vk::StructureChain<vk::InstanceCreateInfo> makeInstanceCreateInfoChain(vk::InstanceCreateFlagBits instanceCreateFlagBits, vk::ApplicationInfo const &applicationInfo, std::vector<char const *> const &layers, std::vector<char const *> const &extensions)
{

    // if constexpr (isDebugMode())
    //{
    //     vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);
    //     vk::DebugUtilsMessageTypeFlagsEXT messageTypeFlags(vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);
    //     vk::StructureChain<vk::InstanceCreateInfo, vk::DebugUtilsMessengerCreateInfoEXT> instanceCreateInfo({instanceCreateFlagBits, &applicationInfo, layers, extensions}, {{}, severityFlags, messageTypeFlags, &debugUtilsMessengerCallback});
    //     return instanceCreateInfo;
    // }
    // else
    //{
    return vk::StructureChain<vk::InstanceCreateInfo>({instanceCreateFlagBits, &applicationInfo, layers, extensions});
    //}
}

[[nodiscard]] std::vector<char const *> gatherLayers(std::vector<std::string> const &layers)
{
    const std::vector<vk::LayerProperties> &layerProperties = vk::enumerateInstanceLayerProperties();
    std::vector<char const *> enabledLayers;
    enabledLayers.reserve(layers.size());
    for (auto const &layer : layers)
    {
        assert(std::ranges::any_of(layerProperties, [layer](vk::LayerProperties const &lp) { return layer == lp.layerName; }));
        enabledLayers.push_back(layer.data());
    }
    if constexpr (isDebugMode())
    {
        if (std::ranges::none_of(layers, [](std::string const &layer) { return layer == "VK_LAYER_KHRONOS_validation"; }) && std::ranges::any_of(layerProperties, [](vk::LayerProperties const &lp) { return (strcmp("VK_LAYER_KHRONOS_validation", lp.layerName) == 0); }))
        {
            enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
        }
    }
    return enabledLayers;
}

[[nodiscard]] std::vector<char const *> gatherExtensions(std::vector<std::string> const &extensions)
{
    const std::vector<vk::ExtensionProperties> &extensionProperties = vk::enumerateInstanceExtensionProperties();
    std::vector<char const *> enabledExtensions;
    enabledExtensions.reserve(extensions.size());
    for (auto const &extension : extensions)
    {
        assert(std::any_of(extensionProperties.begin(), extensionProperties.end(), [extension](vk::ExtensionProperties const &ep) { return extension == ep.extensionName; }));
        enabledExtensions.push_back(extension.data());
    }
    if constexpr (isDebugMode())
    {
        if (std::none_of(extensions.begin(), extensions.end(), [](std::string const &extension) { return extension == VK_EXT_DEBUG_UTILS_EXTENSION_NAME; }) &&
            std::any_of(extensionProperties.begin(), extensionProperties.end(), [](vk::ExtensionProperties const &ep) { return (strcmp(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, ep.extensionName) == 0); }))
        {
            enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
    }
    return enabledExtensions;
}

void Device::createInstaceVK(std::string const &appName, std::string const &engineName, std::vector<std::string> const &layers, std::vector<std::string> const &extensions, uint32_t apiVersion)
{
    // vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
    // std::vector<char const *> enabledLayers = gatherLayers(layers);
    // std::vector<char const *> enabledExtensions = gatherExtensions(extensions);
    //// instance = vk::createInstance(makeInstanceCreateInfoChain({}, applicationInfo, enabledLayers, enabledExtensions).get<vk::InstanceCreateInfo>());
    // instance = vk::raii::Instance(makeInstanceCreateInfoChain({}, applicationInfo, enabledLayers, enabledExtensions).get<vk::InstanceCreateInfo>());
}

void Device::destroy()
{
    // instance.destroy();
}

void application()
{
    Device device;
    device.createInstaceVK("HelloVulkan", "VKEngine");
    device.destroy();

    //    try
    //    {
    //        vk::Instance instance = vk::su::createInstance(AppName, EngineName);
    // #if !defined(NDEBUG)
    //        vk::DebugUtilsMessengerEXT debugUtilsMessenger = instance.createDebugUtilsMessengerEXT(vk::su::makeDebugUtilsMessengerCreateInfoEXT());
    // #endif
    //
    //        /* VULKAN_HPP_KEY_START */
    //
    //        // enumerate the physicalDevices
    //        vk::PhysicalDevice physicalDevice = instance.enumeratePhysicalDevices().front();
    //
    //        /* VULKAN_HPP_KEY_END */
    //
    // #if !defined(NDEBUG)
    //        instance.destroyDebugUtilsMessengerEXT(debugUtilsMessenger);
    // #endif
    //        instance.destroy();
    //    }
    //    catch (vk::SystemError &err)
    //    {
    //        std::cout << "vk::SystemError: " << err.what() << std::endl;
    //        exit(-1);
    //    }
    //    catch (std::exception &err)
    //    {
    //        std::cout << "std::exception: " << err.what() << std::endl;
    //        exit(-1);
    //    }
    //    catch (...)
    //    {
    //        std::cout << "unknown error\n";
    //        exit(-1);
    //    }
}
void rhiTest()
{
    using namespace std;
    auto foo = [](int x) { return x + 1; };
    print("hello from rhiTest:{}", foo(1));
    application();
}
} // namespace rhi
} // namespace nr