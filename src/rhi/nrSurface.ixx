export module nr.rhi:surface;
import dependency;
import nr.utils;
import std;

export namespace nr::rhi
{
struct Surface
{
    class GlfwContext final
    {
      public:
        GlfwContext()
        {
            if (static_cast<bool>(glfwInit()) != true)
            {
                throw std::runtime_error("Failed to initialize GLFW.");
            }
            glfwSetErrorCallback(&GlfwContext::errorCallback);
        }
        GlfwContext(GlfwContext const &) = delete;
        GlfwContext &operator=(GlfwContext const &) = delete;
        ~GlfwContext()
        {
            glfwTerminate();
        }

      private:
        static void errorCallback(int error, const char *msg)
        {
            nrInfo<LogLevel::error>(std::format("glfw: (error number:{}) {}", error, msg));
        }
    } inline static glfwCtx;

    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> handle{nullptr, &glfwDestroyWindow};
    vk::Extent2D extent{1920, 1080};
    vk::raii::SurfaceKHR surface = {nullptr};
    vk::Format format = vk::Format::eUndefined;
    Surface()
    {
        (void)&glfwCtx;
    }
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    Surface(Surface &&) = default;
    Surface &operator=(Surface &&) = default;

    static void ensureGlfwInitialized()
    {
        (void)&glfwCtx;
    }

    /**
     * @brief Create GLFW window and Vulkan surface pair.
     *
     * The returned Surface owns both window and vk::raii::SurfaceKHR handles.
     */
    [[nodiscard]] static Surface create(const vk::raii::Instance &instance, std::string_view windowTitle, vk::Extent2D initialExtent = {1920, 1080})
    {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

        Surface result;
        result.extent = initialExtent;
        result.handle.reset(glfw::createWindow(static_cast<int>(initialExtent.width), static_cast<int>(initialExtent.height), std::string(windowTitle).c_str(), nullptr, nullptr));
        nrAssert(result.handle != nullptr, "Surface::create failed to create GLFW window.");

        VkSurfaceKHR rawSurface{};
        vk::detail::resultCheck(glfw::createWindowSurface(*instance, result.handle.get(), nullptr, &rawSurface), "Failed to create window surface");
        result.surface = vk::raii::SurfaceKHR(instance, rawSurface);
        result.refreshExtentFromFramebuffer();
        return result;
    }

    /**
     * @brief Refresh extent from current framebuffer size.
     *
     * Width/height are clamped to at least 1 to keep swapchain creation valid.
     */
    void refreshExtentFromFramebuffer()
    {
        nrAssert(handle != nullptr, "Surface::refreshExtentFromFramebuffer requires a valid window handle.");
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(handle.get(), &width, &height);
        extent = vk::Extent2D{
            static_cast<std::uint32_t>(std::max(width, 1)),
            static_cast<std::uint32_t>(std::max(height, 1)),
        };
    }
};
} // namespace nr::rhi
