module;
#include <vulkan/vulkan_raii.hpp>
export module nr.rhi;
import nr.rhi.vk;
export namespace nr::rhi
{
template <typename Derived> class Device
{
  public:
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};
    Device() = default;
    Device(Device &) = delete;
    Device &operator=(Device &) = delete;
    void initialize(std::string const &appName = {"DefaultApp"}, std::string const &engineName = {"DefaultEngine"});
    vk::raii::Instance makeInstance(std::string const &appName, std::string const &engineName, uint32_t apiVersion = VK_API_VERSION_1_4) const;
    vk::raii::Device makeDevice() const;
    ~Device() = default;

  protected:
    void setupInitialFlags();
    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{VK_KHR_SURFACE_EXTENSION_NAME};
    // std::vector<std::string> physicalDeviceFeatures{};
    std::vector<std::string> deviceEnabledExtensions{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    std::array<size_t, static_cast<size_t>(QueueKind::Size)> queueFamilyDict{};
};

void rhiTest();
} // namespace nr::rhi
