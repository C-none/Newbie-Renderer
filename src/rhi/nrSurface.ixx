export module nr.rhi:surface;
import dependency.window;
import dependency.vulkan;
import nr.utils;
import std;

export namespace nr::rhi
{
struct Surface
{
    class GlfwContext final
    {
      public:
        GlfwContext();
        GlfwContext(GlfwContext const &) = delete;
        GlfwContext &operator=(GlfwContext const &) = delete;
        ~GlfwContext();

      private:
        static void errorCallback(int error, const char *msg);
    };

    [[nodiscard]] static GlfwContext &glfwContext();

    std::unique_ptr<GLFWwindow, decltype(&glfwDestroyWindow)> handle{nullptr, &glfwDestroyWindow};
    vk::Extent2D extent{1920, 1080};
    vk::raii::SurfaceKHR surface = {nullptr};
    vk::Format format = vk::Format::eUndefined;
    Surface();
    Surface(const Surface &) = delete;
    Surface &operator=(const Surface &) = delete;
    Surface(Surface &&) = default;
    Surface &operator=(Surface &&) = default;

    static void ensureGlfwInitialized();

    /**
     * @brief Create GLFW window and Vulkan surface pair.
     *
     * The returned Surface owns both window and vk::raii::SurfaceKHR handles.
     */
    [[nodiscard]] static Surface create(const vk::raii::Instance &instance, std::string_view windowTitle, vk::Extent2D initialExtent = {1920, 1080});

    /**
     * @brief Refresh extent from current framebuffer size.
     *
     * Width/height are clamped to at least 1 to keep swapchain creation valid.
     */
    void refreshExtentFromFramebuffer();
};
} // namespace nr::rhi
