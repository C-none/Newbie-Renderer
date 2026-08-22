module nr.rhi;
import :device;
import dependency.window;
import dependency.nsight;
import dependency.dlss;
import dependency.vulkan;
import :vk;
import :surface;
import :swapchain;
import :type;
import :queue;
import :cooperativeVector;
import :frameContext;
import :command;
import :memoryAllocator;
import :nsightGraphics;
import :resourcePool;
import :pipeline;
import :resourceOps;
import :dlss;
import nr.utils;
import std;

namespace nr::rhi
{
namespace
{
[[nodiscard]] double elapsedMilliseconds(std::chrono::steady_clock::time_point begin,
                                         std::chrono::steady_clock::time_point end) noexcept
{
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

template <typename TFeature> struct RequiredFeature
{
    vk::Bool32 TFeature::*member;
    std::string_view name;
};

template <typename TFeature>
void enableRequiredFeatures(const TFeature &supported, TFeature &requested, std::string_view structName,
                            std::initializer_list<RequiredFeature<TFeature>> requiredFeatures)
{
    for (auto const feature : requiredFeatures)
    {
        nrAssert(supported.*(feature.member) == vk::True,
                 "Required Vulkan device feature {}::{} is not supported on this device.", structName, feature.name);
        requested.*(feature.member) = vk::True;
    }
}

template <typename TFeature, typename... TSupported, typename... TRequested>
void enableRequiredFeatures(const vk::StructureChain<TSupported...> &supportedChain,
                            vk::StructureChain<TRequested...> &requestedChain, std::string_view structName,
                            std::initializer_list<RequiredFeature<TFeature>> requiredFeatures)
{
    enableRequiredFeatures(supportedChain.template get<TFeature>(), requestedChain.template get<TFeature>(),
                           structName, requiredFeatures);
}

using QueueFamilyDict = std::array<std::size_t, static_cast<std::size_t>(QueueFamilyKind::size)>;

struct InstanceFlags
{
    std::vector<std::string> enabledLayers;
    std::vector<std::string> enabledExtensions;
};

[[nodiscard]] InstanceFlags setupInitialFlags()
{
    InstanceFlags flags;
    Surface::ensureGlfwInitialized();

    std::uint32_t glfwCount = 0;
    const char **glfwExt = glfwGetRequiredInstanceExtensions(&glfwCount);
    nrAssert(glfwExt != nullptr && glfwCount > 0, "GLFW did not report Vulkan instance extensions.");
    flags.enabledExtensions.assign(glfwExt, glfwExt + glfwCount);

    auto addIfMissing = [](std::vector<std::string> &list, std::string_view item) {
        if (std::ranges::none_of(list, [item](const auto &s) { return s == item; }))
            list.push_back(std::string(item));
    };

    if (nr::dependency::dlss::sdkCompiled())
    {
        auto const instanceExtensionQuery = nr::dependency::dlss::rayReconstructionInstanceExtensions();
        nrAssert(instanceExtensionQuery.status.success(),
                 "DLSS RR Vulkan instance-extension discovery failed: {} (native code {}).",
                 instanceExtensionQuery.status.message, instanceExtensionQuery.status.nativeCode);
        std::ranges::for_each(instanceExtensionQuery.names,
                              [&](std::string_view extension) { addIfMissing(flags.enabledExtensions, extension); });
    }

    if (hasInstanceExtension(vk::EXTSwapchainColorSpaceExtensionName))
    {
        addIfMissing(flags.enabledExtensions, vk::EXTSwapchainColorSpaceExtensionName);
    }
    else
    {
        nrLog<LogLevel::info>(
            "VK_EXT_swapchain_colorspace is unavailable; swapchain format selection is limited to core color spaces.");
    }

    nrAssert(
        hasInstanceExtension(vk::KHRGetSurfaceCapabilities2ExtensionName),
        "VK_KHR_get_surface_capabilities2 is required for VK_EXT_full_screen_exclusive surface capability queries.");
    addIfMissing(flags.enabledExtensions, vk::KHRGetSurfaceCapabilities2ExtensionName);

    nrAssert(hasInstanceExtension(vk::EXTSurfaceMaintenance1ExtensionName),
             "VK_EXT_surface_maintenance1 is required by VK_EXT_swapchain_maintenance1.");
    addIfMissing(flags.enabledExtensions, vk::EXTSurfaceMaintenance1ExtensionName);

    if constexpr (isDebugMode || gpuDebugNamesEnabled)
    {
        if constexpr (isDebugMode)
        {
            constexpr std::string_view validationLayer = "VK_LAYER_KHRONOS_validation";
            nrAssert(
                hasInstanceLayer(validationLayer),
                "Debug builds require '{}'. The Vulkan loader did not enumerate this layer on the current machine. "
                "Validation layers are provided by the Vulkan SDK / validation-layer installation, not by the GPU "
                "or display driver.",
                validationLayer);
            addIfMissing(flags.enabledLayers, validationLayer);
        }

        if (hasInstanceExtension(vk::EXTDebugUtilsExtensionName))
        {
            addIfMissing(flags.enabledExtensions, vk::EXTDebugUtilsExtensionName);
        }
        else
        {
            nrLog<LogLevel::warning>("VK_EXT_debug_utils is unavailable; validation callbacks, debug labels, and object "
                                    "names require this extension.");
            nrAssert(false, "VK_EXT_debug_utils is required when validation or GPU debug names are enabled.");
        }
    }
    return flags;
}

[[nodiscard]] vk::raii::Instance makeInstance(const vk::raii::Context &context, const std::string &appName,
                                              const std::string &engineName,
                                              std::span<const std::string> instanceEnabledLayers,
                                              std::span<const std::string> instanceEnabledExtensions,
                                              std::uint32_t apiVersion, bool debugShaderInstrumentationEnabled)
{
    const vk::ApplicationInfo applicationInfo(appName.c_str(), 1, engineName.c_str(), 1, apiVersion);
    std::vector<char const *> enabledLayers = gatherLayers(instanceEnabledLayers);
    std::vector<char const *> enabledExtensions = gatherInstanceExtensions(instanceEnabledExtensions);

    vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    vk::InstanceCreateInfo instanceCreateInfo({}, &applicationInfo, enabledLayers, enabledExtensions);
    if constexpr (isDebugMode)
    {
        const void *debugPNext = nullptr;
        if (std::ranges::any_of(instanceEnabledExtensions, [](const std::string &extension) {
                return extension == vk::EXTDebugUtilsExtensionName;
            }))
        {
            debugCreateInfo = makeDebugUtilsMessengerCreateInfoEXT();
            debugPNext = &debugCreateInfo;
        }
        DebugValidationLayerSettings validationLayerSettings(debugShaderInstrumentationEnabled);
        auto validationLayerSettingsCreateInfo = validationLayerSettings.createInfo(debugPNext);
        instanceCreateInfo.pNext = &validationLayerSettingsCreateInfo;
        nrLog<LogLevel::info>("Debug Vulkan validation layer settings enabled programmatically: "
                              "Core=on, SyncValidation=on, ObjectValidation=on, GPU-AV={}, DebugPrintf={}, "
                              "report_flags=verbose/error/perf/info/warn, "
                              "debug_action=none (routed through nrVulkan callback), "
                              "duplicate message limit disabled.",
                              validationLayerSettings.gpuAssistedValidationEnabled() ? "on" : "off",
                              validationLayerSettings.debugPrintfEnabled() ? "on" : "off");
        return vk::raii::Instance(context, instanceCreateInfo);
    }
    return vk::raii::Instance(context, instanceCreateInfo);
}

struct LogicalDevice
{
    vk::raii::Device device{nullptr};
    QueueFamilyDict queueFamilyDict{};
    std::vector<std::string> enabledDeviceExtensions;
    RayTracingCapabilitySnapshot rtCapabilities{};
    CooperativeVectorCapabilitySnapshot cooperativeVectorCapabilities{};
    ops::QueueFamilyTransferPolicy queueFamilyTransferPolicy{};
    bool frameBoundaryEnabled = false;
    bool hdrMetadataEnabled = false;
};

[[nodiscard]] LogicalDevice makeDevice(const vk::raii::PhysicalDevice &physicalDevice,
                                       const vk::raii::SurfaceKHR &surface,
                                       std::span<const std::string> requestedDeviceExtensions)
{
    LogicalDevice result;
    auto queueFamilyProperties = physicalDevice.getQueueFamilyProperties();
    std::ranges::fill(result.queueFamilyDict, std::numeric_limits<std::size_t>::max());

    auto queueIndices = std::views::iota(std::size_t{0}, queueFamilyProperties.size());
    auto presentSupport = queueIndices | std::views::transform([&](std::size_t index) {
                              return physicalDevice.getSurfaceSupportKHR(static_cast<std::uint32_t>(index), surface)
                                         ? vk::True
                                         : vk::False;
                          }) |
                          std::ranges::to<std::vector>();
    auto queueFamilies = selectRequiredQueueFamilies(queueFamilyProperties, presentSupport);
    nrAssert(queueFamilies.has_value(),
             "Selected GPU does not expose required graphics, present-capable compute, and dedicated physical "
             "copy/transfer queue families.");

    auto toQueueIndex = [](QueueFamilyKind kind) { return static_cast<std::size_t>(kind); };
    result.queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)] = queueFamilies->graphics;
    result.queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)] = queueFamilies->compute;
    result.queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)] = queueFamilies->transfer;

    auto queueFamilySummary = [&](std::uint32_t familyIndex) {
        const auto &family = queueFamilyProperties[familyIndex];
        return std::format("index={} flags={} queueCount={}", familyIndex, vk::to_string(family.queueFlags),
                           family.queueCount);
    };
    auto queueFamilySelectionMessage =
        std::format("Vulkan queue family selection: graphics{{{}}} compute{{{}}} transfer{{{}}}",
                    queueFamilySummary(queueFamilies->graphics), queueFamilySummary(queueFamilies->compute),
                    queueFamilySummary(queueFamilies->transfer));
    nrLog<LogLevel::info>("{}", queueFamilySelectionMessage);

    constexpr float queuePriority = 1.0f;
    auto uniqueFamilies =
        std::array{static_cast<std::uint32_t>(result.queueFamilyDict[toQueueIndex(QueueFamilyKind::graphics)]),
                   static_cast<std::uint32_t>(result.queueFamilyDict[toQueueIndex(QueueFamilyKind::compute)]),
                   static_cast<std::uint32_t>(result.queueFamilyDict[toQueueIndex(QueueFamilyKind::transfer)])};
    std::ranges::sort(uniqueFamilies);

    auto queueCreateInfos = uniqueFamilies | std::views::filter([last = std::uint32_t(-1)](std::uint32_t f) mutable {
                                if (f == last)
                                    return false;
                                last = f;
                                return true;
                            }) |
                            std::views::transform([&](std::uint32_t familyIndex) {
                                return vk::DeviceQueueCreateInfo({}, familyIndex, 1, &queuePriority);
                            }) |
                            std::ranges::to<std::vector>();

    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    auto isExtensionSupported = [&availableExtensions](std::string_view extensionName) {
        return std::ranges::any_of(availableExtensions, [extensionName](const vk::ExtensionProperties &property) {
            return std::string_view(property.extensionName) == extensionName;
        });
    };

    result.enabledDeviceExtensions.clear();
    std::vector<char const *> enabledExtensions;
    enabledExtensions.reserve(requestedDeviceExtensions.size() + 2u);
    std::set<std::string_view> enabledExtensionSet;

    auto enableExtension = [&](std::string_view extensionName, std::string_view reason) {
        if (!isExtensionSupported(extensionName))
        {
            nrAssert(false, "Required device extension '{}' is not supported ({})", extensionName, reason);
            return false;
        }
        if (enabledExtensionSet.insert(extensionName).second)
        {
            enabledExtensions.push_back(extensionName.data());
        }
        return true;
    };

    std::ranges::for_each(requestedDeviceExtensions, [&](std::string_view extensionName) {
        auto reason = std::string_view{"modern pipeline backend"};
        if (extensionName == vk::KHRMaintenance8ExtensionName)
            reason = "precise queue-family ownership transfer synchronization scopes";
        if (extensionName == vk::KHRMaintenance9ExtensionName)
            reason = "maintenance9 queue-family ownership transfer rules";
        if (extensionName == vk::EXTRayTracingInvocationReorderExtensionName)
            reason = "path-tracing shader invocation reordering";
        if (extensionName == vk::EXTFullScreenExclusiveExtensionName)
            reason = "application-controlled fullscreen exclusive swapchain ownership";
        if (extensionName == vk::EXTSwapchainMaintenance1ExtensionName)
            reason = "per-present completion fences and swapchain generation retirement";
        if (extensionName == vk::NVCooperativeVectorExtensionName)
            reason = "global neural-material cooperative-vector inference and training";
        if (extensionName == vk::EXTShaderReplicatedCompositesExtensionName)
            reason = "SPIR-V emitted for cooperative-vector arithmetic";
        if (extensionName == vk::EXTShaderFloat8ExtensionName)
            reason = "FP8 E4M3 cooperative-vector matrix conversion and inference";
        enableExtension(extensionName, reason);
    });

    auto const frameBoundaryExtensionSupported = isExtensionSupported(vk::EXTFrameBoundaryExtensionName);
    auto const hdrMetadataExtensionSupported = isExtensionSupported(vk::EXTHdrMetadataExtensionName);
    auto frameBoundaryFeatureSupported = false;
    if (frameBoundaryExtensionSupported)
    {
        auto frameBoundaryFeatureQuery =
            physicalDevice.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
        auto const &frameBoundaryFeatures = frameBoundaryFeatureQuery.get<vk::PhysicalDeviceFrameBoundaryFeaturesEXT>();
        frameBoundaryFeatureSupported = frameBoundaryFeatures.frameBoundary == vk::True;
    }

    auto supportedFeatures = physicalDevice.getFeatures2<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDevicePipelineBinaryFeaturesKHR,
        vk::PhysicalDeviceMaintenance8FeaturesKHR, vk::PhysicalDeviceMaintenance9FeaturesKHR,
        vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
        vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT, vk::PhysicalDeviceCooperativeVectorFeaturesNV,
        vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT, vk::PhysicalDeviceShaderFloat8FeaturesEXT>();

    auto requestedFeatures = vk::StructureChain<
        vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features,
        vk::PhysicalDeviceVulkan13Features, vk::PhysicalDeviceVulkan14Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDevicePipelineBinaryFeaturesKHR,
        vk::PhysicalDeviceMaintenance8FeaturesKHR, vk::PhysicalDeviceMaintenance9FeaturesKHR,
        vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT,
        vk::PhysicalDeviceAccelerationStructureFeaturesKHR, vk::PhysicalDeviceRayTracingPipelineFeaturesKHR,
        vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT, vk::PhysicalDeviceCooperativeVectorFeaturesNV,
        vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT, vk::PhysicalDeviceShaderFloat8FeaturesEXT>{};

    using CoreFeatures = std::remove_cvref_t<decltype(std::declval<vk::PhysicalDeviceFeatures2>().features)>;

    enableRequiredFeatures<CoreFeatures>(supportedFeatures.get<vk::PhysicalDeviceFeatures2>().features,
                                         requestedFeatures.get<vk::PhysicalDeviceFeatures2>().features,
                                         "vk::PhysicalDeviceFeatures",
                                         {{&CoreFeatures::shaderStorageImageReadWithoutFormat,
                                           "shaderStorageImageReadWithoutFormat"},
                                          {&CoreFeatures::shaderStorageImageWriteWithoutFormat,
                                           "shaderStorageImageWriteWithoutFormat"}});
    enableRequiredFeatures<vk::PhysicalDeviceVulkan11Features>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceVulkan11Features",
        {{&vk::PhysicalDeviceVulkan11Features::shaderDrawParameters, "shaderDrawParameters"},
         {&vk::PhysicalDeviceVulkan11Features::storageBuffer16BitAccess, "storageBuffer16BitAccess"},
         {&vk::PhysicalDeviceVulkan11Features::uniformAndStorageBuffer16BitAccess,
          "uniformAndStorageBuffer16BitAccess"}});
    enableRequiredFeatures<vk::PhysicalDeviceVulkan12Features>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceVulkan12Features",
        {{&vk::PhysicalDeviceVulkan12Features::bufferDeviceAddress, "bufferDeviceAddress"},
         {&vk::PhysicalDeviceVulkan12Features::descriptorIndexing, "descriptorIndexing"},
         {&vk::PhysicalDeviceVulkan12Features::runtimeDescriptorArray, "runtimeDescriptorArray"},
         {&vk::PhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound, "descriptorBindingPartiallyBound"},
         {&vk::PhysicalDeviceVulkan12Features::descriptorBindingVariableDescriptorCount,
          "descriptorBindingVariableDescriptorCount"},
         {&vk::PhysicalDeviceVulkan12Features::scalarBlockLayout, "scalarBlockLayout"},
         {&vk::PhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing,
          "shaderSampledImageArrayNonUniformIndexing"},
         {&vk::PhysicalDeviceVulkan12Features::timelineSemaphore, "timelineSemaphore"},
         {&vk::PhysicalDeviceVulkan12Features::shaderFloat16, "shaderFloat16"},
         {&vk::PhysicalDeviceVulkan12Features::vulkanMemoryModel, "vulkanMemoryModel"}});
    enableRequiredFeatures<vk::PhysicalDeviceVulkan13Features>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceVulkan13Features",
        {{&vk::PhysicalDeviceVulkan13Features::inlineUniformBlock, "inlineUniformBlock"},
         {&vk::PhysicalDeviceVulkan13Features::dynamicRendering, "dynamicRendering"},
         {&vk::PhysicalDeviceVulkan13Features::synchronization2, "synchronization2"},
         {&vk::PhysicalDeviceVulkan13Features::maintenance4, "maintenance4"}});
    enableRequiredFeatures<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT",
        {{&vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT::extendedDynamicState, "extendedDynamicState"}});
    enableRequiredFeatures<vk::PhysicalDevicePipelineBinaryFeaturesKHR>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDevicePipelineBinaryFeaturesKHR",
        {{&vk::PhysicalDevicePipelineBinaryFeaturesKHR::pipelineBinaries, "pipelineBinaries"}});
    enableRequiredFeatures<vk::PhysicalDeviceVulkan14Features>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceVulkan14Features",
        {{&vk::PhysicalDeviceVulkan14Features::maintenance5, "maintenance5"}});
    if (enabledExtensionSet.contains("VK_KHR_push_descriptor"))
    {
        enableRequiredFeatures<vk::PhysicalDeviceVulkan14Features>(
            supportedFeatures, requestedFeatures, "vk::PhysicalDeviceVulkan14Features",
            {{&vk::PhysicalDeviceVulkan14Features::pushDescriptor, "pushDescriptor"}});
    }
    enableRequiredFeatures<vk::PhysicalDeviceMaintenance8FeaturesKHR>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceMaintenance8FeaturesKHR",
        {{&vk::PhysicalDeviceMaintenance8FeaturesKHR::maintenance8, "maintenance8"}});
    enableRequiredFeatures<vk::PhysicalDeviceMaintenance9FeaturesKHR>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceMaintenance9FeaturesKHR",
        {{&vk::PhysicalDeviceMaintenance9FeaturesKHR::maintenance9, "maintenance9"}});
    enableRequiredFeatures<vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT",
        {{&vk::PhysicalDeviceRayTracingInvocationReorderFeaturesEXT::rayTracingInvocationReorder,
          "rayTracingInvocationReorder"}});
    enableRequiredFeatures<vk::PhysicalDeviceAccelerationStructureFeaturesKHR>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceAccelerationStructureFeaturesKHR",
        {{&vk::PhysicalDeviceAccelerationStructureFeaturesKHR::accelerationStructure, "accelerationStructure"}});
    enableRequiredFeatures<vk::PhysicalDeviceRayTracingPipelineFeaturesKHR>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceRayTracingPipelineFeaturesKHR",
        {{&vk::PhysicalDeviceRayTracingPipelineFeaturesKHR::rayTracingPipeline, "rayTracingPipeline"}});
    enableRequiredFeatures<vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT",
        {{&vk::PhysicalDeviceSwapchainMaintenance1FeaturesEXT::swapchainMaintenance1, "swapchainMaintenance1"}});
    enableRequiredFeatures<vk::PhysicalDeviceCooperativeVectorFeaturesNV>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceCooperativeVectorFeaturesNV",
        {{&vk::PhysicalDeviceCooperativeVectorFeaturesNV::cooperativeVector, "cooperativeVector"},
         {&vk::PhysicalDeviceCooperativeVectorFeaturesNV::cooperativeVectorTraining, "cooperativeVectorTraining"}});
    enableRequiredFeatures<vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT",
        {{&vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT::shaderReplicatedComposites,
          "shaderReplicatedComposites"}});
    enableRequiredFeatures<vk::PhysicalDeviceShaderFloat8FeaturesEXT>(
        supportedFeatures, requestedFeatures, "vk::PhysicalDeviceShaderFloat8FeaturesEXT",
        {{&vk::PhysicalDeviceShaderFloat8FeaturesEXT::shaderFloat8, "shaderFloat8"}});

    auto &requestedVulkan11 = requestedFeatures.get<vk::PhysicalDeviceVulkan11Features>();
    auto &requestedVulkan12 = requestedFeatures.get<vk::PhysicalDeviceVulkan12Features>();
    auto &requestedCooperativeVector = requestedFeatures.get<vk::PhysicalDeviceCooperativeVectorFeaturesNV>();
    auto &requestedReplicatedComposites =
        requestedFeatures.get<vk::PhysicalDeviceShaderReplicatedCompositesFeaturesEXT>();

    auto properties2 = physicalDevice.getProperties2<vk::PhysicalDeviceProperties2,
                                                     vk::PhysicalDeviceRayTracingPipelinePropertiesKHR,
                                                     vk::PhysicalDeviceCooperativeVectorPropertiesNV>();
    auto const &physicalDeviceProperties = properties2.get<vk::PhysicalDeviceProperties2>();
    auto const &rayTracingPipelineProperties =
        properties2.get<vk::PhysicalDeviceRayTracingPipelinePropertiesKHR>();
    auto const &cooperativeVectorProperties =
        properties2.get<vk::PhysicalDeviceCooperativeVectorPropertiesNV>();
    auto const requiredCooperativeVectorStages =
        vk::ShaderStageFlagBits::eCompute | vk::ShaderStageFlagBits::eRaygenKHR | vk::ShaderStageFlagBits::eClosestHitKHR;
    nrAssert((cooperativeVectorProperties.cooperativeVectorSupportedStages & requiredCooperativeVectorStages) ==
                 requiredCooperativeVectorStages,
             "VK_NV_cooperative_vector must support compute, raygen, and closest-hit shader stages.");
    nrAssert(cooperativeVectorProperties.cooperativeVectorTrainingFloat16Accumulation == vk::True,
             "VK_NV_cooperative_vector must support FP16 TrainingOptimal accumulation.");
    nrAssert(cooperativeVectorProperties.maxCooperativeVectorComponents >= 32u,
             "VK_NV_cooperative_vector requires at least 32 cooperative-vector components, but the device exposes {}.",
             cooperativeVectorProperties.maxCooperativeVectorComponents);

    auto const cooperativeVectorTypes = physicalDevice.getCooperativeVectorPropertiesNV();
    auto const supportsFullFloat16Tuple = [&cooperativeVectorTypes](bool requireTranspose) {
        // A true advertised transpose bit is a capability superset: the same tuple
        // remains valid for a non-transposed multiply. Vulkan only requires the bit
        // to be true when the SPIR-V Transpose operand is true.
        return std::ranges::any_of(cooperativeVectorTypes, [requireTranspose](const vk::CooperativeVectorPropertiesNV &properties) {
            return properties.inputType == vk::ComponentTypeKHR::eFloat16 &&
                   properties.inputInterpretation == vk::ComponentTypeKHR::eFloat16 &&
                   properties.matrixInterpretation == vk::ComponentTypeKHR::eFloat16 &&
                   properties.biasInterpretation == vk::ComponentTypeKHR::eFloat16 &&
                   properties.resultType == vk::ComponentTypeKHR::eFloat16 &&
                   (!requireTranspose || properties.transpose == vk::True);
        });
    };
    auto const supportsFullFloat16TupleWithoutTranspose = supportsFullFloat16Tuple(false);
    auto const supportsFullFloat16TupleWithTranspose = supportsFullFloat16Tuple(true);
    nrAssert(supportsFullFloat16TupleWithoutTranspose,
             "VK_NV_cooperative_vector must expose the required all-FP16 tuple without transpose support.");
    nrAssert(supportsFullFloat16TupleWithTranspose,
             "VK_NV_cooperative_vector must expose the required all-FP16 tuple with transpose support.");
    result.cooperativeVectorCapabilities = CooperativeVectorCapabilitySnapshot{
        .extensionEnabled = true,
        .cooperativeVectorFeatureEnabled = requestedCooperativeVector.cooperativeVector == vk::True,
        .cooperativeVectorTrainingFeatureEnabled = requestedCooperativeVector.cooperativeVectorTraining == vk::True,
        .shaderFloat16FeatureEnabled = requestedVulkan12.shaderFloat16 == vk::True,
        .vulkanMemoryModelFeatureEnabled = requestedVulkan12.vulkanMemoryModel == vk::True,
        .shaderReplicatedCompositesFeatureEnabled =
            requestedReplicatedComposites.shaderReplicatedComposites == vk::True,
        .storageBuffer16BitAccessFeatureEnabled = requestedVulkan11.storageBuffer16BitAccess == vk::True,
        .uniformAndStorageBuffer16BitAccessFeatureEnabled =
            requestedVulkan11.uniformAndStorageBuffer16BitAccess == vk::True,
        .computeStage = (cooperativeVectorProperties.cooperativeVectorSupportedStages &
                         vk::ShaderStageFlagBits::eCompute) != vk::ShaderStageFlags{},
        .raygenStage = (cooperativeVectorProperties.cooperativeVectorSupportedStages &
                        vk::ShaderStageFlagBits::eRaygenKHR) != vk::ShaderStageFlags{},
        .closestHitStage = (cooperativeVectorProperties.cooperativeVectorSupportedStages &
                            vk::ShaderStageFlagBits::eClosestHitKHR) != vk::ShaderStageFlags{},
        .trainingFloat16Accumulation =
            cooperativeVectorProperties.cooperativeVectorTrainingFloat16Accumulation == vk::True,
        .fullFloat16Tuple = supportsFullFloat16TupleWithoutTranspose,
        .fullFloat16TupleWithTranspose = supportsFullFloat16TupleWithTranspose,
        .maxComponents = cooperativeVectorProperties.maxCooperativeVectorComponents,
        .supportedTuples = cooperativeVectorTypes,
    };

    auto queueOwnershipPropertyChains = physicalDevice.getQueueFamilyProperties2<
        vk::StructureChain<vk::QueueFamilyProperties2, vk::QueueFamilyOwnershipTransferPropertiesKHR>>();
    auto ownershipTransferMasks = queueOwnershipPropertyChains | std::views::transform([](const auto &chain) {
                                      return chain.template get<vk::QueueFamilyOwnershipTransferPropertiesKHR>()
                                          .optimalImageTransferToQueueFamilies;
                                  }) |
                                  std::ranges::to<std::vector>();
    nrAssert(ownershipTransferMasks.size() == queueFamilyProperties.size(),
             "VK_KHR_maintenance9 queue-family ownership transfer property query returned an unexpected family count.");
    result.queueFamilyTransferPolicy = nr::rhi::ops::QueueFamilyTransferPolicy{
        .maintenance9 = true,
        .optimalImageTransferToQueueFamilies = std::move(ownershipTransferMasks),
    };

    auto frameBoundaryCreateFeatures = vk::PhysicalDeviceFrameBoundaryFeaturesEXT{};
    result.frameBoundaryEnabled = frameBoundaryExtensionSupported && frameBoundaryFeatureSupported;
    if (result.frameBoundaryEnabled)
    {
        enableExtension(vk::EXTFrameBoundaryExtensionName, "graphics debugger frame boundary metadata");
        frameBoundaryCreateFeatures.frameBoundary = vk::True;
        auto &requestedFeatureList = requestedFeatures.get<vk::PhysicalDeviceFeatures2>();
        frameBoundaryCreateFeatures.pNext = requestedFeatureList.pNext;
        requestedFeatureList.pNext = std::addressof(frameBoundaryCreateFeatures);
        nrLog<LogLevel::info>("VK_EXT_frame_boundary enabled for graphics debugger frame capture.");
    }
    else if (frameBoundaryExtensionSupported)
    {
        nrLog<LogLevel::warning>(
            "VK_EXT_frame_boundary was exposed without its frameBoundary feature; frame-boundary tagging is disabled.");
    }

    result.hdrMetadataEnabled = false;
    if (hdrMetadataExtensionSupported)
    {
        result.hdrMetadataEnabled = enableExtension(vk::EXTHdrMetadataExtensionName, "HDR10 swapchain metadata");
    }
    else
    {
        nrLog<LogLevel::info>(
            "VK_EXT_hdrmetadata is unavailable; HDR swapchain output can still run without presentation metadata.");
    }

    auto const &limits = physicalDeviceProperties.properties.limits;
    result.rtCapabilities = RayTracingCapabilitySnapshot{
        .shaderGroupHandleSize = rayTracingPipelineProperties.shaderGroupHandleSize,
        .shaderGroupHandleAlignment = rayTracingPipelineProperties.shaderGroupHandleAlignment,
        .shaderGroupBaseAlignment = rayTracingPipelineProperties.shaderGroupBaseAlignment,
        .maxShaderGroupStride = rayTracingPipelineProperties.maxShaderGroupStride,
        .maxRayDispatchInvocationCount = rayTracingPipelineProperties.maxRayDispatchInvocationCount,
        .maxRayRecursionDepth = rayTracingPipelineProperties.maxRayRecursionDepth,
        .maxDispatchDimensions =
            {
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[0]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[0]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[1]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[1]),
                static_cast<std::uint64_t>(limits.maxComputeWorkGroupCount[2]) *
                    static_cast<std::uint64_t>(limits.maxComputeWorkGroupSize[2]),
            },
    };

    auto &requestedFeatureList = requestedFeatures.get<vk::PhysicalDeviceFeatures2>();
    vk::DeviceCreateInfo deviceCreateInfo(vk::DeviceCreateFlags(), queueCreateInfos,
                                          {} /* EnabledLayerNames is deprecated and ignored.*/, enabledExtensions,
                                          nullptr, &requestedFeatureList);
    auto enabledExtensionNames = enabledExtensions | std::views::transform([](const char *extensionName) {
                                     return std::string{extensionName};
                                 }) |
                                 std::ranges::to<std::vector>();
    try
    {
        auto logicalDevice = vk::raii::Device(physicalDevice, deviceCreateInfo);
        result.enabledDeviceExtensions = std::move(enabledExtensionNames);
        result.device = std::move(logicalDevice);
    }
    catch (const vk::SystemError &error)
    {
        nrLog<LogLevel::error, "LOG">("Vulkan logical-device creation failed: {}", error.what());
    }
    return result;
}

[[nodiscard]] std::uint32_t requiredQueueFamily(QueueFamilyKind kind, const QueueFamilyDict &queueFamilyDict)
{
    std::size_t index = static_cast<std::size_t>(kind);
    std::size_t familyIndex = queueFamilyDict[index];
    nrAssert(familyIndex != std::numeric_limits<std::size_t>::max(),
             "Queue family not found - device capability contract violated.");
    return static_cast<std::uint32_t>(familyIndex);
}

[[nodiscard]] std::uint32_t presentQueueFamilyIndex(const QueueFamilyDict &queueFamilyDict)
{
    return requiredQueueFamily(QueueFamilyKind::compute, queueFamilyDict);
}

[[nodiscard]] QueueManager makeQueueManager(const vk::raii::Device &device, const QueueFamilyDict &queueFamilyDict)
{
    std::uint32_t graphicsFamily = requiredQueueFamily(QueueFamilyKind::graphics, queueFamilyDict);
    std::uint32_t computeFamily = requiredQueueFamily(QueueFamilyKind::compute, queueFamilyDict);
    std::uint32_t transferFamily = requiredQueueFamily(QueueFamilyKind::transfer, queueFamilyDict);

    GpuQueue graphicsQueue(device, graphicsFamily);
    GpuQueue computeQueue(device, computeFamily);
    GpuQueue transferQueue(device, transferFamily);

    return QueueManager(std::move(graphicsQueue), std::move(computeQueue), std::move(transferQueue));
}

[[nodiscard]] FrameManager makeFrameManager(const vk::raii::Device &device, const QueueFamilyIndices &familyIndices)
{
    FrameContext::PoolConfig graphicsConfig{.queueFamilyIndex = familyIndices.graphics};
    FrameContext::PoolConfig computeConfig{.queueFamilyIndex = familyIndices.compute};
    FrameContext::PoolConfig transferConfig{.queueFamilyIndex = familyIndices.transfer};

    return FrameManager(device, graphicsConfig, computeConfig, transferConfig);
}
} // namespace

struct Device::Bootstrap
{
    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance{nullptr};
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger{nullptr};
    Surface surface;
    vk::raii::PhysicalDevice physicalDevice{nullptr};
    vk::raii::Device device{nullptr};

    std::vector<std::string> instanceEnabledLayers;
    std::vector<std::string> instanceEnabledExtensions;
    std::vector<std::string> requestedDeviceExtensions{
        vk::KHRSwapchainExtensionName,
        vk::EXTSwapchainMaintenance1ExtensionName,
        vk::KHRDeferredHostOperationsExtensionName,
        vk::KHRAccelerationStructureExtensionName,
        vk::KHRRayTracingPipelineExtensionName,
        vk::EXTRayTracingInvocationReorderExtensionName,
        vk::KHRPipelineLibraryExtensionName,
        vk::KHRPipelineBinaryExtensionName,
        vk::EXTMemoryBudgetExtensionName,
        vk::KHRMaintenance8ExtensionName,
        vk::KHRMaintenance9ExtensionName,
        vk::EXTFullScreenExclusiveExtensionName,
        vk::NVCooperativeVectorExtensionName,
        vk::EXTShaderReplicatedCompositesExtensionName,
        vk::EXTShaderFloat8ExtensionName,
    };
    std::vector<std::string> enabledDeviceExtensions;
    RayTracingCapabilitySnapshot rtCapabilities{};
    CooperativeVectorCapabilitySnapshot cooperativeVectorCapabilities{};
    ops::QueueFamilyTransferPolicy queueFamilyTransferPolicy{};
    bool frameBoundaryEnabled = false;
    bool hdrMetadataEnabled = false;
    NsightGraphicsFrameHelper nsightGraphics{};
    QueueFamilyDict queueFamilyDict{};
};

[[nodiscard]] const RayTracingCapabilitySnapshot &Device::rayTracingCapabilities() const noexcept
{
    return rtCapabilities_;
}

[[nodiscard]] const CooperativeVectorCapabilitySnapshot &Device::cooperativeVectorCapabilities() const noexcept
{
    return cooperativeVectorCapabilities_;
}

[[nodiscard]] const ops::QueueFamilyTransferPolicy &Device::queueFamilyTransferPolicy() const noexcept
{
    return queueFamilyTransferPolicy_;
}

[[nodiscard]] bool Device::frameBoundaryEnabled() const noexcept
{
    return frameBoundaryEnabled_;
}

[[nodiscard]] bool Device::hdrMetadataEnabled() const noexcept
{
    return hdrMetadataEnabled_;
}

[[nodiscard]] bool Device::nsightGraphicsEnabled() const noexcept
{
    return nsightGraphics_.enabled();
}

[[nodiscard]] bool Device::hasEnabledInstanceExtension(std::string_view extension) const
{
    return std::ranges::any_of(instanceEnabledExtensions,
                               [extension](const std::string &item) { return item == extension; });
}

[[nodiscard]] bool Device::hasEnabledDeviceExtension(std::string_view extension) const
{
    return std::ranges::any_of(enabledDeviceExtensions_,
                               [extension](const std::string &item) { return item == extension; });
}

[[nodiscard]] Device::Bootstrap Device::makeBootstrap(std::string appName, std::string engineName,
                                                     bool debugShaderInstrumentationEnabled)
{
    Bootstrap bootstrap;
    bootstrap.appName = std::move(appName);
    bootstrap.engineName = std::move(engineName);

    auto instanceFlags = setupInitialFlags();
    bootstrap.instanceEnabledLayers = std::move(instanceFlags.enabledLayers);
    bootstrap.instanceEnabledExtensions = std::move(instanceFlags.enabledExtensions);

    bootstrap.nsightGraphics.configureFromEnvironment();
    bootstrap.nsightGraphics.injectIfRequested();
    bootstrap.instance = makeInstance(bootstrap.context, bootstrap.appName, bootstrap.engineName,
                                      bootstrap.instanceEnabledLayers, bootstrap.instanceEnabledExtensions,
                                      vk::ApiVersion14, debugShaderInstrumentationEnabled);
    if constexpr (isDebugMode)
    {
        if (std::ranges::any_of(bootstrap.instanceEnabledExtensions, [](const std::string &extension) {
                return extension == vk::EXTDebugUtilsExtensionName;
            }))
        {
            bootstrap.debugUtilsMessenger =
                vk::raii::DebugUtilsMessengerEXT(bootstrap.instance, makeDebugUtilsMessengerCreateInfoEXT());
        }
    }
    bootstrap.surface = Surface::create(bootstrap.instance, bootstrap.appName);
    bootstrap.physicalDevice =
        selectPhysicalDevice(bootstrap.instance, bootstrap.surface.surface, bootstrap.requestedDeviceExtensions);
    {
        auto gpuProps = bootstrap.physicalDevice.getProperties();
        nrLog<LogLevel::info>("Selected GPU: {}", gpuProps.deviceName.data());
    }
    if (nr::dependency::dlss::sdkCompiled())
    {
        auto const deviceExtensionQuery =
            nr::dependency::dlss::rayReconstructionDeviceExtensions(*bootstrap.instance, *bootstrap.physicalDevice);
        nrAssert(deviceExtensionQuery.status.success(),
                 "DLSS RR Vulkan device-extension discovery failed: {} (native code {}).",
                 deviceExtensionQuery.status.message, deviceExtensionQuery.status.nativeCode);
        auto addDeviceExtensionIfMissing = [&](std::string_view extension) {
            if (std::ranges::none_of(bootstrap.requestedDeviceExtensions,
                                     [extension](const std::string &item) { return item == extension; }))
            {
                bootstrap.requestedDeviceExtensions.emplace_back(extension);
            }
        };
        std::ranges::for_each(deviceExtensionQuery.names, addDeviceExtensionIfMissing);
    }
    auto logicalDevice =
        makeDevice(bootstrap.physicalDevice, bootstrap.surface.surface, bootstrap.requestedDeviceExtensions);
    bootstrap.device = std::move(logicalDevice.device);
    bootstrap.queueFamilyDict = logicalDevice.queueFamilyDict;
    bootstrap.enabledDeviceExtensions = std::move(logicalDevice.enabledDeviceExtensions);
    bootstrap.rtCapabilities = std::move(logicalDevice.rtCapabilities);
    bootstrap.cooperativeVectorCapabilities = std::move(logicalDevice.cooperativeVectorCapabilities);
    bootstrap.queueFamilyTransferPolicy = std::move(logicalDevice.queueFamilyTransferPolicy);
    bootstrap.frameBoundaryEnabled = logicalDevice.frameBoundaryEnabled;
    bootstrap.hdrMetadataEnabled = logicalDevice.hdrMetadataEnabled;

    return bootstrap;
}

[[nodiscard]] Device Device::create(std::string appName, std::string engineName,
                                    std::filesystem::path pipelineBinaryRoot, bool debugShaderInstrumentationEnabled)
{
    return Device{makeBootstrap(std::move(appName), std::move(engineName), debugShaderInstrumentationEnabled),
                  std::move(pipelineBinaryRoot)};
}

[[nodiscard]] std::unique_ptr<Device> Device::createUnique(std::string appName, std::string engineName,
                                                          std::filesystem::path pipelineBinaryRoot,
                                                          bool debugShaderInstrumentationEnabled)
{
    return std::unique_ptr<Device>{
        new Device{makeBootstrap(std::move(appName), std::move(engineName), debugShaderInstrumentationEnabled),
                   std::move(pipelineBinaryRoot)}};
}

Device::Device(Bootstrap &&bootstrap, std::filesystem::path pipelineBinaryRoot)
    : appName(std::move(bootstrap.appName)), engineName(std::move(bootstrap.engineName)),
      context(std::move(bootstrap.context)), instance(std::move(bootstrap.instance)),
      debugUtilsMessenger(std::move(bootstrap.debugUtilsMessenger)),
      physicalDevice(std::move(bootstrap.physicalDevice)), device(std::move(bootstrap.device)),
      memoryAllocator(instance, physicalDevice, device), resourceFactory(memoryAllocator, device),
      resourcePool(resourceFactory), queueManager(makeQueueManager(device, bootstrap.queueFamilyDict)),
      frameManager(makeFrameManager(device, queueManager.familyIndices())),
      presentationContext(std::move(bootstrap.surface)),
      instanceEnabledLayers(std::move(bootstrap.instanceEnabledLayers)),
      instanceEnabledExtensions(std::move(bootstrap.instanceEnabledExtensions)),
      requestedDeviceExtensions_(std::move(bootstrap.requestedDeviceExtensions)),
      enabledDeviceExtensions_(std::move(bootstrap.enabledDeviceExtensions)),
      rtCapabilities_(std::move(bootstrap.rtCapabilities)),
      cooperativeVectorCapabilities_(std::move(bootstrap.cooperativeVectorCapabilities)),
      queueFamilyTransferPolicy_(std::move(bootstrap.queueFamilyTransferPolicy)),
      frameBoundaryEnabled_(bootstrap.frameBoundaryEnabled), hdrMetadataEnabled_(bootstrap.hdrMetadataEnabled),
      nsightGraphics_(std::move(bootstrap.nsightGraphics)), queueFamilyDict(bootstrap.queueFamilyDict),
      swapChainConfig_{
          .hdrMetadataEnabled = bootstrap.hdrMetadataEnabled,
          .fullScreenExclusiveEnabled = true,
      }
{
    nsightGraphics_.initializeIfRequested(presentQueueRawForExternalTools());
    uploadReadbackContext_.emplace(device, resourceFactory, queueManager, queueFamilyTransferPolicy_);

    presentationContext.initializeSwapchain(physicalDevice, device, swapChainConfig_,
                                            presentQueueFamilyIndex(queueFamilyDict));
    pipelineService.bindDevice(device, physicalDevice.getProperties().limits.maxBoundDescriptorSets, rtCapabilities_,
                               std::move(pipelineBinaryRoot));
}

[[nodiscard]] Device::FrameBeginResult Device::beginFrame()
{
    auto &frame = frameManager.current();
    const auto frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
    auto const waitGpuStart = std::chrono::steady_clock::now();
    nrAssert(frame.waitForFence(), "Device::beginFrame timeout waiting for frame fence.");
    auto const cpuWaitGpuMilliseconds = elapsedMilliseconds(waitGpuStart, std::chrono::steady_clock::now());

    // After this frame slot's fence: its previous final submit has completed, meaning the
    // imageAvailable wait bound to THIS frame slot was executed. Return that slot to the pool.
    presentationContext.returnAcquireSemaphore(frameIndex);

    resourcePool.resetFrame(frameIndex);

    memoryAllocator.resetFramePool(frameIndex);

    frame.resetFence();

    frame.resetPools();

    constexpr auto minimumSecondaryPoolSlots = 2u;
    const auto secondaryPoolSlotCount =
        std::min<std::uint32_t>(maxThreads, std::max(minimumSecondaryPoolSlots, std::thread::hardware_concurrency()));
    frame.prepareSecondaryPools(secondaryPoolSlotCount, secondaryPoolSlotCount, secondaryPoolSlotCount);

    if (presentationContext.consumeSwapchainRecreateRequest())
    {
        recreateSwapchain();
    }

    nrAssert(!presentationContext.hasActiveSwapchainImage(),
             "Device::beginFrame requires the previous frame's active swapchain image to be cleared.");
    presentationContext.setFrameSubmitted(false);
    presentFrameBoundaryFrameID_.reset();
    frameAcquireRequiresRecreate_ = false;
    nsightGraphics_.beginFrame(frameBoundaryEnabled_);

    return FrameBeginResult{
        .frameIndex = frameIndex,
        .cpuWaitGpuMilliseconds = cpuWaitGpuMilliseconds,
    };
}

[[nodiscard]] Device::FrameAcquireResult Device::acquireFrameImage(std::uint64_t acquireTimeout)
{
    nrAssert(!presentationContext.hasActiveSwapchainImage(),
             "Device::acquireFrameImage can only acquire once per frame.");

    auto const frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
    auto acquire = presentationContext.acquireNextImage(frameIndex, acquireTimeout);
    auto recreatedSwapchain = false;
    if (PresentationContext::needsSwapchainRecreate(acquire.result) && acquire.result != vk::Result::eSuboptimalKHR)
    {
        recreateSwapchain();
        recreatedSwapchain = true;
        acquire = presentationContext.acquireNextImage(frameIndex, acquireTimeout);
    }

    nrAssert(acquire.result == vk::Result::eSuccess || acquire.result == vk::Result::eSuboptimalKHR,
             "Device::acquireFrameImage failed to acquire a valid swapchain image after recreation.");

    presentationContext.setActiveSwapchainImage(acquire.imageIndex);
    frameAcquireRequiresRecreate_ = PresentationContext::needsSwapchainRecreate(acquire.result);

    return FrameAcquireResult{
        .swapchainImageIndex = acquire.imageIndex,
        .swapchainResult = acquire.result,
        .recreatedSwapchain = recreatedSwapchain,
    };
}

void Device::submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent,
                              vk::PipelineStageFlags2 imageAvailableWaitStage)
{
    nrAssert(!presentationContext.hasSubmittedCurrentFrame(),
             "Device::submitFrameBatch cannot submit additional batches after final present-signaling submit.");

    if (signalForPresent)
    {
        nrAssert(presentationContext.hasActiveSwapchainImage(),
                 "Device::submitFrameBatch final submission requires acquireFrameImage().");
        nrAssert(
            submitRole == presentSubmitRole(),
            "Device::submitFrameBatch compute-present policy requires the compute queue when signalForPresent=true.");
    }

    auto &frame = frameManager.current();

    // Keep pre-present work decoupled from swapchain availability.
    // Waiting on imageAvailable only at the present-signaling submit prevents vblank pacing
    // from stalling earlier GPU batches that do not touch the swapchain image.
    if (signalForPresent)
    {
        auto const frameIndex = static_cast<std::uint32_t>(frameManager.currentIndex());
        batch.addWait(presentationContext.borrowedAcquireSemaphore(frameIndex), imageAvailableWaitStage);
    }

    if (signalForPresent)
    {
        batch.addSignal(presentationContext.activePresentSemaphore());
    }

    auto frameBoundaryFrameID = batch.frameBoundaryFrameID();
    auto fence = signalForPresent
                     ? std::optional<std::reference_wrapper<const vk::raii::Fence>>(std::cref(frame.fence()))
                     : std::nullopt;
    queueManager.forRole(submitRole).submit(std::move(batch), fence);

    if (signalForPresent)
    {
        presentationContext.setFrameSubmitted(true);
        presentFrameBoundaryFrameID_.reset();
        if (frameBoundaryEnabled_)
        {
            presentFrameBoundaryFrameID_ = frameBoundaryFrameID;
        }
    }
}

void Device::submitFrameBatch(CommandBatch &&batch, QueueRole submitRole, bool signalForPresent)
{
    submitFrameBatch(std::move(batch), submitRole, signalForPresent,
                     vk::PipelineStageFlags2{vk::PipelineStageFlagBits2::eAllCommands});
}

[[nodiscard]] PresentResult Device::presentFrame()
{
    nrAssert(presentationContext.hasActiveSwapchainImage(),
             "Device::presentFrame requires beginFrame() before present.");
    nrAssert(presentationContext.hasSubmittedCurrentFrame(),
             "Device::presentFrame compute-present policy requires a final submission that signals the active present "
             "semaphore.");

    auto const presentImage = activeSwapchainImageRawForExternalTools();
    nsightGraphics_.stopTraceBeforeBoundaryIfNeeded(presentImage);

    auto presentResult = presentationContext.present(queueManager, presentFrameBoundaryFrameID_);
    nsightGraphics_.markFrameBoundaryAfterPresent(presentResult.result, presentImage);

    auto const recreateRequested = presentationContext.consumeSwapchainRecreateRequest();
    if (frameAcquireRequiresRecreate_ || PresentationContext::needsSwapchainRecreate(presentResult.result) ||
        recreateRequested)
    {
        if (presentationContext.framebufferAvailable())
        {
            recreateSwapchain();
        }
    }

    frameManager.advanceFrame();
    presentationContext.clearActiveSwapchainImage();
    presentationContext.setFrameSubmitted(false);
    presentFrameBoundaryFrameID_.reset();
    frameAcquireRequiresRecreate_ = false;

    return presentResult;
}

void Device::waitIdle()
{
    queueManager.waitAllIdle();
    frameManager.waitAll();
}

void Device::recreateSwapchain()
{
    presentationContext.recreate(physicalDevice, device, queueManager);
    ++swapchainRecreationGeneration_;
}

[[nodiscard]] std::uint64_t Device::swapchainRecreationGeneration() const noexcept
{
    return swapchainRecreationGeneration_;
}

[[nodiscard]] PipelineService &Device::pipeline() noexcept
{
    return pipelineService;
}

[[nodiscard]] const PipelineService &Device::pipeline() const noexcept
{
    return pipelineService;
}

[[nodiscard]] ShaderService &Device::shaderCompiler() const
{
    return ShaderService::instance();
}

[[nodiscard]] ops::UploadReadbackContext &Device::uploadReadback() noexcept
{
    nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
    return *uploadReadbackContext_;
}

[[nodiscard]] const ops::UploadReadbackContext &Device::uploadReadback() const noexcept
{
    nrAssert(uploadReadbackContext_.has_value(), "Device::uploadReadback requires initialize() first.");
    return *uploadReadbackContext_;
}

[[nodiscard]] std::shared_ptr<DlssContext> Device::dlssContext()
{
    nrAssert(nr::dependency::dlss::sdkCompiled(),
             "DLSS execution was requested, but the deployed NGX bridge is unavailable. Configure "
             "with NR_ENABLE_DLSS_NGX_SDK=ON and deploy the validated bridge artifact.");
    if (!dlssContext_)
    {
        auto pathError = std::error_code{};
        auto applicationDataPath = std::filesystem::current_path(pathError);
        nrAssert(!pathError, "DLSS NGX application-data path resolution failed: {}", pathError.message());
        applicationDataPath /= "ngx";
        dlssContext_ = std::make_shared<DlssContext>(static_cast<vk::Instance>(*instance),
                                                     static_cast<vk::PhysicalDevice>(*physicalDevice),
                                                     static_cast<vk::Device>(*device), std::move(applicationDataPath));
        nrAssert(dlssContext_->valid(), "DLSS NGX context initialization failed: {}", dlssContext_->status().message);
    }
    return dlssContext_;
}

[[nodiscard]] std::unique_ptr<DlssRayReconstructionFeature> Device::createDlssRayReconstructionFeature(
    const nr::dependency::dlss::RayReconstructionCreateDesc &desc)
{
    auto sharedContext = dlssContext();
    // Prepare runs after beginFrame(), whose current fence is intentionally
    // unsignaled. Wait only for queues here; waiting every frame fence would
    // deadlock before the current frame has been submitted.
    queueManager.waitAllIdle();
    return DlssRayReconstructionFeature::create(std::move(sharedContext), device, queueManager.compute(), desc);
}

Device::~Device()
{
    if (*device != nullptr)
    {
        waitIdle();
    }
}

[[nodiscard]] VkQueue Device::presentQueueRawForExternalTools() const noexcept
{
    return static_cast<VkQueue>(*queueManager.compute().handle());
}

[[nodiscard]] VkImage Device::activeSwapchainImageRawForExternalTools() const
{
    auto const imageIndex = presentationContext.activeSwapchainImageIndex();
    return static_cast<VkImage>(presentationContext.swapchainImage(imageIndex));
}

} // namespace nr::rhi
