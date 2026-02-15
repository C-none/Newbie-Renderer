module;
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
};
} // namespace nr::rhi
