export module nr.rhi:vk;
import dependency.vulkan;
import nr.utils;
import :type;
import std;
export namespace nr::rhi
{

struct RequiredQueueFamilySelection
{
    std::uint32_t graphics = 0;
    std::uint32_t compute = 0;
    std::uint32_t transfer = 0;
};

[[nodiscard]] std::optional<RequiredQueueFamilySelection> selectRequiredQueueFamilies(
    std::span<const vk::QueueFamilyProperties> queueFamilyProperties,
    std::span<const vk::Bool32> presentSupport);

[[nodiscard]] vk::raii::PhysicalDevice selectPhysicalDevice(
    const vk::raii::Instance &instance, const vk::raii::SurfaceKHR &surface,
    std::span<const std::string> requiredDeviceExtensions);

// Helper: Convert strings to const char* pointers with deduplication and validation
[[nodiscard]] std::vector<char const *> gatherLayers(std::span<const std::string> layers);

[[nodiscard]] bool hasInstanceLayer(std::string_view layer);

// Helper: Convert extension strings to const char* pointers with deduplication and validation
[[nodiscard]] std::vector<char const *> gatherInstanceExtensions(std::span<const std::string> extensions);

[[nodiscard]] bool hasInstanceExtension(std::string_view extension);

vk::Bool32 debugUtilsMessengerCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
                                       vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
                                       const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
                                       void * /*pUserData*/);

vk::DebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfoEXT();

struct DebugValidationLayerSettings
{
    explicit DebugValidationLayerSettings(bool debugShaderInstrumentationEnabled = true);
    DebugValidationLayerSettings(const DebugValidationLayerSettings &) = delete;
    DebugValidationLayerSettings &operator=(const DebugValidationLayerSettings &) = delete;
    DebugValidationLayerSettings(DebugValidationLayerSettings &&) = delete;
    DebugValidationLayerSettings &operator=(DebugValidationLayerSettings &&) = delete;

    [[nodiscard]] bool gpuAssistedValidationEnabled() const noexcept
    {
        return gpuAssistedValidationEnabled_;
    }

    [[nodiscard]] bool debugPrintfEnabled() const noexcept
    {
        return debugPrintfEnabled_;
    }

    [[nodiscard]] vk::LayerSettingsCreateInfoEXT createInfo(const void *pNext = nullptr) const noexcept;

  private:
    bool gpuAssistedValidationEnabled_ = true;
    bool debugPrintfEnabled_ = true;
    std::array<vk::Bool32, 1> enabledValue_{vk::True};
    std::array<vk::Bool32, 1> disabledValue_{vk::False};
    std::array<std::uint32_t, 1> printfBufferSize_{8192u};
    std::array<std::uint32_t, 1> gpuavMaxIndicesCount_{8192u};
    std::array<const char *, 5> reportFlags_{"verbose", "error", "perf", "info", "warn"};
    std::vector<vk::LayerSettingEXT> settings_;
};

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
[[nodiscard]] constexpr vk::ImageViewType inferViewType(vk::ImageType imageType, std::uint32_t arrayLayers) noexcept
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
