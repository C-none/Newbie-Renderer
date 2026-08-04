module nr.rhi;
import :vk;
import dependency.vulkan;
import nr.utils;
import :type;
import std;

namespace nr::rhi
{
namespace
{
constexpr char validationLayerName[] = "VK_LAYER_KHRONOS_validation";
} // namespace

[[nodiscard]] std::optional<RequiredQueueFamilySelection> selectRequiredQueueFamilies(
    std::span<const vk::QueueFamilyProperties> queueFamilyProperties)
{
    const auto queueIndices = std::views::iota(std::size_t{0}, queueFamilyProperties.size());
    auto findFirst = [&](auto predicate) -> std::optional<std::size_t> {
        auto it = std::ranges::find_if(queueIndices, predicate);
        if (it == std::ranges::end(queueIndices))
        {
            return std::nullopt;
        }
        return *it;
    };

    const auto hasFlags = [&](std::size_t index, vk::QueueFlags flags) {
        const auto &family = queueFamilyProperties[index];
        return family.queueCount > 0 && (family.queueFlags & flags) == flags;
    };

    auto graphicsFamily = findFirst([&](std::size_t index) { return hasFlags(index, vk::QueueFlagBits::eGraphics); });
    if (!graphicsFamily.has_value())
    {
        return std::nullopt;
    }

    auto dedicatedComputeFamily = findFirst([&](std::size_t index) {
        const auto &family = queueFamilyProperties[index];
        return family.queueCount > 0 && (family.queueFlags & vk::QueueFlagBits::eCompute) &&
               !(family.queueFlags & vk::QueueFlagBits::eGraphics);
    });
    auto computeFamily = dedicatedComputeFamily;
    if (!computeFamily.has_value())
    {
        computeFamily = findFirst([&](std::size_t index) { return hasFlags(index, vk::QueueFlagBits::eCompute); });
    }
    if (!computeFamily.has_value())
    {
        return std::nullopt;
    }

    auto transferFamily = findFirst([&](std::size_t index) {
        const auto &family = queueFamilyProperties[index];
        return family.queueCount > 0 && (family.queueFlags & vk::QueueFlagBits::eTransfer) &&
               !(family.queueFlags & vk::QueueFlagBits::eCompute) &&
               !(family.queueFlags & vk::QueueFlagBits::eGraphics);
    });
    if (!transferFamily.has_value())
    {
        return std::nullopt;
    }

    return RequiredQueueFamilySelection{
        .graphics = static_cast<std::uint32_t>(*graphicsFamily),
        .compute = static_cast<std::uint32_t>(*computeFamily),
        .transfer = static_cast<std::uint32_t>(*transferFamily),
    };
}

[[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(vk::raii::Instance const &instance)
{
    vk::raii::PhysicalDevices physicalDevices(instance);
    nrAssert(!physicalDevices.empty(), "No Vulkan physical devices are available.");

    auto supportsRequiredQueues = [](const vk::raii::PhysicalDevice &device) {
        auto queueFamilies = device.getQueueFamilyProperties();
        return selectRequiredQueueFamilies(queueFamilies).has_value();
    };
    auto deviceRank = [](const vk::raii::PhysicalDevice &device) {
        auto props = device.getProperties();
        return std::tuple{
            props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu,
            props.limits.maxImageDimension2D,
        };
    };

    std::optional<std::reference_wrapper<const vk::raii::PhysicalDevice>> bestDevice{};
    std::ranges::for_each(physicalDevices, [&](const vk::raii::PhysicalDevice &device) {
        if (!supportsRequiredQueues(device))
        {
            return;
        }
        if (!bestDevice.has_value() || deviceRank(bestDevice->get()) < deviceRank(device))
        {
            bestDevice = std::cref(device);
        }
    });
    nrAssert(bestDevice.has_value(),
             "No GPU exposes the required graphics, compute, and dedicated physical copy/transfer queue families.");
    return bestDevice->get();
}

[[nodiscard]] std::vector<char const *> gatherLayers(std::span<const std::string> layers)
{
    std::set<std::string_view> uniqueLayers(layers.begin(), layers.end());
    const auto layerProperties = vk::enumerateInstanceLayerProperties();

    std::vector<char const *> enabledLayers;
    enabledLayers.reserve(uniqueLayers.size());

    for (std::string_view layer : uniqueLayers)
    {
        bool found = std::ranges::any_of(layerProperties, [layer](const vk::LayerProperties &lp) {
            return layer == std::string_view(lp.layerName);
        });
        nrAssert(found, std::format("Requested layer '{}' is not available.", layer));
        enabledLayers.emplace_back(layer.data());
    }
    return enabledLayers;
}

[[nodiscard]] bool hasInstanceLayer(std::string_view layer)
{
    const auto layerProperties = vk::enumerateInstanceLayerProperties();
    return std::ranges::any_of(layerProperties, [layer](const vk::LayerProperties &property) {
        return layer == std::string_view(property.layerName);
    });
}

[[nodiscard]] std::vector<char const *> gatherInstanceExtensions(std::span<const std::string> extensions)
{
    std::set<std::string_view> uniqueExtensions(extensions.begin(), extensions.end());
    const auto extensionProperties = vk::enumerateInstanceExtensionProperties();

    std::vector<char const *> enabledExtensions;
    enabledExtensions.reserve(uniqueExtensions.size());

    for (std::string_view extension : uniqueExtensions)
    {
        bool found = std::ranges::any_of(extensionProperties, [extension](const vk::ExtensionProperties &ep) {
            return extension == std::string_view(ep.extensionName);
        });
        nrAssert(found, std::format("Requested extension '{}' is not available.", extension));
        enabledExtensions.emplace_back(extension.data());
    }
    return enabledExtensions;
}

[[nodiscard]] bool hasInstanceExtension(std::string_view extension)
{
    const auto extensionProperties = vk::enumerateInstanceExtensionProperties();
    return std::ranges::any_of(extensionProperties, [extension](const vk::ExtensionProperties &property) {
        return extension == std::string_view(property.extensionName);
    });
}

vk::Bool32 debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                       vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                       const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                       void * /*pUserData*/)
{
    if constexpr (isDebugMode)
    {
        switch (static_cast<std::uint32_t>(pCallbackData->messageIdNumber))
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

    std::string logMessage = std::format("severity={} types={} messageIDName=<{}> messageIdNumber={} message=<{}>",
                                         severityStr, typesStr, idName, pCallbackData->messageIdNumber, message);

    auto queueLabels = std::span(pCallbackData->pQueueLabels, pCallbackData->queueLabelCount);
    if (!queueLabels.empty())
    {
        std::ranges::for_each(queueLabels, [&](const auto &lbl) {
            logMessage += std::format("\nqueueLabel=<{}>", lbl.pLabelName ? lbl.pLabelName : "");
        });
    }

    auto cmdBufLabels = std::span(pCallbackData->pCmdBufLabels, pCallbackData->cmdBufLabelCount);
    if (!cmdBufLabels.empty())
    {
        std::ranges::for_each(cmdBufLabels, [&](const auto &lbl) {
            logMessage += std::format("\ncmdLabel=<{}>", lbl.pLabelName ? lbl.pLabelName : "");
        });
    }

    auto objects = std::span(pCallbackData->pObjects, pCallbackData->objectCount);
    if (!objects.empty())
    {
        auto objectIndices = std::views::iota(std::size_t{0}, objects.size());
        std::ranges::for_each(objectIndices, [&](std::size_t i) {
            auto const &obj = objects[i];
            logMessage +=
                std::format("\nobject[{}]: type={} handle={}", i, vk::to_string(obj.objectType), obj.objectHandle);
            if (obj.pObjectName)
            {
                logMessage += std::format(" name=<{}>", obj.pObjectName);
            }
        });
    }

    auto level = LogLevel::info;
    if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eError)
    {
        level = LogLevel::error;
    }
    else if (messageSeverity == vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning)
    {
        level = LogLevel::warning;
    }
    nrVulkan(level, logMessage);

    return vk::False;
}

vk::DebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfoEXT()
{
    constexpr auto severityFlags =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    constexpr auto messageTypeFlags = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                                      vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
                                      vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation;
    return {{}, severityFlags, messageTypeFlags, &debugUtilsMessengerCallback};
}

DebugValidationLayerSettings::DebugValidationLayerSettings()
{
    settings_.reserve(64);

    auto addSetting = [this](const char *settingName, vk::LayerSettingTypeEXT type, const auto &values) {
        settings_.emplace_back(validationLayerName, settingName, type, static_cast<std::uint32_t>(values.size()),
                               values.data());
    };

    auto addEmptyStringSetting = [this](const char *settingName) {
        settings_.emplace_back(validationLayerName, settingName, vk::LayerSettingTypeEXT::eString, 0u, nullptr);
    };

    auto addBoolSetting = [&](const char *settingName, bool enabled) {
        const auto &values = enabled ? enabledValue_ : disabledValue_;
        addSetting(settingName, vk::LayerSettingTypeEXT::eBool32, values);
    };

    auto addBoolSettings = [&](std::initializer_list<const char *> settingNames, bool enabled) {
        std::ranges::for_each(settingNames, [&](const char *settingName) { addBoolSetting(settingName, enabled); });
    };

    addBoolSettings(
        {
            "validate_core",
            "check_image_layout",
            "check_command_buffer",
            "check_object_in_use",
            "check_query",
            "check_shaders",
            "check_shaders_caching",
            "unique_handles",
            "object_lifetime",
            "stateless_param",
            "thread_safety",
            "validate_sync",
            "syncval_submit_time_validation",
            "syncval_shader_accesses_heuristic",
            "syncval_message_extra_properties",
        },
        true);

    addBoolSettings(
        {
            "printf_enable",
            "printf_verbose",
        },
        debugPrintfEnabled_);

    addBoolSettings(
        {
            "gpuav_enable",
            "gpuav_safe_mode",
            "gpuav_force_on_robustness",
            "gpuav_shader_instrumentation",
            "gpuav_descriptor_checks",
            "gpuav_post_process_descriptor_indexing",
            "gpuav_buffer_address_oob",
            "gpuav_mesh_shading",
            "gpuav_validate_ray_query",
            "gpuav_validate_trace_ray",
            "gpuav_vertex_attribute_fetch_oob",
            "gpuav_shader_sanitizer",
            "gpuav_shared_memory_data_race",
            "gpuav_buffers_validation",
            "gpuav_indirect_draws_buffers",
            "gpuav_indirect_dispatches_buffers",
            "gpuav_indirect_trace_rays_buffers",
            "gpuav_buffer_copies",
            "gpuav_copy_memory_indirect",
            "gpuav_index_buffers",
            "gpuav_acceleration_structures_builds",
            "gpuav_ray_tracing_buffers_consistency",
        },
        gpuAssistedValidationEnabled_);

    addBoolSettings(
        {
            "legacy_detection",
            "printf_only_preset",
            "printf_to_stdout",
            "gpuav_select_instrumented_shaders",
            "gpu_dump_descriptors",
            "gpu_dump_copy_memory_indirect",
            "gpu_dump_device_generated_commands",
            "gpu_dump_to_stdout",
            "gpu_dump_device_copy",
            "enable_message_limit",
            "message_format_json",
        },
        false);

    addSetting("printf_buffer_size", vk::LayerSettingTypeEXT::eUint32, printfBufferSize_);
    addSetting("gpuav_max_indices_count", vk::LayerSettingTypeEXT::eUint32, gpuavMaxIndicesCount_);
    addEmptyStringSetting("debug_action");
    addSetting("report_flags", vk::LayerSettingTypeEXT::eString, reportFlags_);
}

[[nodiscard]] vk::LayerSettingsCreateInfoEXT DebugValidationLayerSettings::createInfo(const void *pNext) const noexcept
{
    return {
        static_cast<std::uint32_t>(settings_.size()),
        settings_.data(),
        pNext,
    };
}
} // namespace nr::rhi
