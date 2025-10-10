module;
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>
export module nr.rhi;
import nr.rhi.vk;
import nr.utils;
export namespace nr::rhi
{

struct Surface
{
    class GlfwContext final
    {
      public:
        GlfwContext()
        {
            if (glfwInit() != GLFW_TRUE)
            {
                throw std::runtime_error("Failed to initialize GLFW.");
            }
            glfwSetErrorCallback([](int error, const char *msg) { nrInfo()("glfw: (error number:{}) {}", error, msg); });
        }
        GlfwContext(GlfwContext const &) = delete;
        GlfwContext &operator=(GlfwContext const &) = delete;
        ~GlfwContext()
        {
            glfwTerminate();
        }
    } inline static glfwCtx;

    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> handle{nullptr, &glfwDestroyWindow};
    vk::Extent2D extent{1920, 1080};
    vk::raii::SurfaceKHR surface = {nullptr};
    vk::Format format;
    Surface()
    {
        (void)&glfwCtx;
    }
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    Surface(Surface &&) = default;
    Surface &operator=(Surface &&) = default;
};

struct SwapChain
{
    vk::raii::SwapchainKHR swapChain = {nullptr};
    std::vector<vk::Image> swapChainImages;
    std::vector<vk::raii::ImageView> imageViews;
    SwapChain() = default;
    SwapChain(const SwapChain &) = delete;
    SwapChain &operator=(const SwapChain &) = delete;
    SwapChain(SwapChain &&) = default;
    SwapChain &operator=(SwapChain &&) = default;
};

struct Command
{
    vk::raii::CommandPool commandPool = {nullptr};
    std::vector<vk::raii::CommandBuffer> commandBuffers;
};

template <typename Derived> class Device
{
  public:
    std::string appName;
    std::string engineName;
    vk::raii::Context context;
    vk::raii::Instance instance = {nullptr};
    vk::raii::DebugUtilsMessengerEXT debugUtilsMessenger = {nullptr};
    vk::raii::PhysicalDevice physicalDevice = {nullptr};
    vk::raii::Device device = {nullptr};
    Surface surface;
    SwapChain swapChain;
    Device() = default;
    Device(Device &) = delete;
    Device &operator=(Device &) = delete;
    void initialize(std::string const &appName = {"DefaultApp"}, std::string const &_engineName = {"DefaultEngine"});
    vk::raii::Instance makeInstance(uint32_t apiVersion = VK_API_VERSION_1_4) const;
    vk::raii::Device makeDevice();
    std::tuple<Surface, SwapChain> makeSurfaceAndSwapChain();
    ~Device() = default;

  protected:
    void setupInitialFlags();
    std::vector<std::string> instanceEnabledLayers{};
    std::vector<std::string> instanceEnabledExtensions{};
    // std::vector<std::string> physicalDeviceFeatures{};
    std::vector<std::string> deviceEnabledExtensions{VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME, VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    std::array<size_t, static_cast<size_t>(QueueKind::size)> queueFamilyDict{};
};

void rhiTest();
} // namespace nr::rhi
