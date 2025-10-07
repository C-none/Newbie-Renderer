module;
#include <vulkan/vulkan_raii.hpp>
export module nr.rhi.vk;
import nr.utils;
import std;
export namespace nr::rhi
{

enum class QueueKind
{
    graphics,
    compute,
    transfer,
    videoDecode,
    videoEncode,
    opticalFlow,
    size
};

[[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(vk::raii::Instance const &instance)
{
    vk::raii::PhysicalDevices physicalDevices(instance);
    nrAssert(!physicalDevices.empty(), "No Available GPU!!!!!");
    vk::raii::PhysicalDevice bestDevice = physicalDevices.front();
    vk::PhysicalDeviceProperties bestProps = bestDevice.getProperties();

    for (const auto &device : physicalDevices)
    {
        vk::PhysicalDeviceProperties props = device.getProperties();
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
        {
            if (bestProps.deviceType != vk::PhysicalDeviceType::eDiscreteGpu || props.limits.maxImageDimension2D > bestProps.limits.maxImageDimension2D)
            {
                bestDevice = device;
                bestProps = props;
            }
        }
        else if (bestProps.deviceType != vk::PhysicalDeviceType::eDiscreteGpu)
        {
            if (props.limits.maxImageDimension2D > bestProps.limits.maxImageDimension2D)
            {
                bestDevice = device;
                bestProps = props;
            }
        }
    }
    return bestDevice;
}

[[nodiscard]] std::vector<char const *> gatherLayers(std::vector<std::string> const &layers)
{
    auto uniqueLayers = layers | std::ranges::to<std::set<std::string_view>>();

    const std::vector<vk::LayerProperties> &layerProperties = vk::enumerateInstanceLayerProperties();
    std::vector<char const *> enabledLayers;

    for (auto const &layer : uniqueLayers)
    {
        nrAssert(std::ranges::any_of(layerProperties, [layer](vk::LayerProperties const &lp) { return layer == lp.layerName; }), "Requested layer '{}' is not available.", layer);
        enabledLayers.push_back(layer.data());
    }
    return enabledLayers;
}

[[nodiscard]] std::vector<char const *> gatherInstanceExtensions(std::vector<std::string> const &extensions)
{
    auto uniqueExtensions = extensions | std::ranges::to<std::set<std::string_view>>();

    const std::vector<vk::ExtensionProperties> &extensionProperties = vk::enumerateInstanceExtensionProperties();
    std::vector<char const *> enabledExtensions;
    for (auto const &extension : uniqueExtensions)
    {
        nrAssert(std::any_of(extensionProperties.begin(), extensionProperties.end(), [extension](vk::ExtensionProperties const &ep) { return extension == ep.extensionName; }), "Requested extension '{}' is not available.", extension);
        enabledExtensions.push_back(extension.data());
    }
    return enabledExtensions;
}

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

vk::DebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfoEXT()
{
    return {
        {}, vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError, vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation, &debugUtilsMessengerCallback};
}

} // namespace nr::rhi