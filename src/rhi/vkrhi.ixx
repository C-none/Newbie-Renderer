module;
export module nr.rhi:vk;
import dependency;
import nr.utils;
import :type;
import std;
export namespace nr::rhi
{

[[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(vk::raii::Instance const &instance)
{
    vk::raii::PhysicalDevices physicalDevices(instance);
    nrAssert(!physicalDevices.empty(),std::format("No Available GPU!!!!!"));
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

// Helper: Convert strings to const char* pointers with deduplication and validation
[[nodiscard]] std::vector<char const *> gatherLayers(std::span<const std::string> layers)
{
    std::set<std::string_view> uniqueLayers(layers.begin(), layers.end());
    const auto layerProperties = vk::enumerateInstanceLayerProperties();
    
    std::vector<char const *> enabledLayers;
    enabledLayers.reserve(uniqueLayers.size());
    
    for (std::string_view layer : uniqueLayers)
    {
        bool found = std::ranges::any_of(layerProperties, 
            [layer](const vk::LayerProperties &lp) { return layer == std::string_view(lp.layerName); });
        nrAssert(found, std::format("Requested layer '{}' is not available.", layer));
        enabledLayers.emplace_back(layer.data());
    }
    return enabledLayers;
}

// Helper: Convert extension strings to const char* pointers with deduplication and validation
[[nodiscard]] std::vector<char const *> gatherInstanceExtensions(std::span<const std::string> extensions)
{
    std::set<std::string_view> uniqueExtensions(extensions.begin(), extensions.end());
    const auto extensionProperties = vk::enumerateInstanceExtensionProperties();
    
    std::vector<char const *> enabledExtensions;
    enabledExtensions.reserve(uniqueExtensions.size());
    
    for (std::string_view extension : uniqueExtensions)
    {
        bool found = std::ranges::any_of(extensionProperties,
            [extension](const vk::ExtensionProperties &ep) { return extension == std::string_view(ep.extensionName); });
        nrAssert(found, std::format("Requested extension '{}' is not available.", extension));
        enabledExtensions.emplace_back(extension.data());
    }
    return enabledExtensions;
}

vk::Bool32 debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity, vk::DebugUtilsMessageTypeFlagsEXT messageTypes, const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData, void * /*pUserData*/)
{
    if constexpr (isDebugMode)
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

/**
 * @brief Check if a Vulkan format contains a depth component
 */
[[nodiscard]] constexpr bool isDepthFormat(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eD16Unorm:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32Sfloat:
    case vk::Format::eD32SfloatS8Uint:
    case vk::Format::eX8D24UnormPack32:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Check if a Vulkan format contains a stencil component
 */
[[nodiscard]] constexpr bool isStencilFormat(vk::Format format) noexcept
{
    switch (format)
    {
    case vk::Format::eS8Uint:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
        return true;
    default:
        return false;
    }
}

/**
 * @brief Check if a Vulkan format is a depth-stencil combined format
 */
[[nodiscard]] constexpr bool isDepthStencilFormat(vk::Format format) noexcept
{
    return isDepthFormat(format) && isStencilFormat(format);
}

/**
 * @brief Infer the appropriate vk::ImageAspectFlags from a Vulkan format
 *
 * Returns Depth, Stencil, or Depth|Stencil for depth/stencil formats,
 * and Color for everything else.
 */
[[nodiscard]] constexpr vk::ImageAspectFlags inferAspectFlags(vk::Format format) noexcept
{
    vk::ImageAspectFlags flags{};
    if (isDepthFormat(format))
        flags |= vk::ImageAspectFlagBits::eDepth;
    if (isStencilFormat(format))
        flags |= vk::ImageAspectFlagBits::eStencil;
    if (!flags)
        flags = vk::ImageAspectFlagBits::eColor;
    return flags;
}

/**
 * @brief Infer vk::ImageViewType from vk::ImageType and array layer count
 */
[[nodiscard]] constexpr vk::ImageViewType inferViewType(vk::ImageType imageType, uint32_t arrayLayers) noexcept
{
    switch (imageType)
    {
    case vk::ImageType::e1D:
        return arrayLayers > 1 ? vk::ImageViewType::e1DArray : vk::ImageViewType::e1D;
    case vk::ImageType::e2D:
        return arrayLayers > 1 ? vk::ImageViewType::e2DArray : vk::ImageViewType::e2D;
    case vk::ImageType::e3D:
        return vk::ImageViewType::e3D;
    default:
        return vk::ImageViewType::e2D;
    }
}

} // namespace nr::rhi