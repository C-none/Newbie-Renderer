module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

module dependency.window;

namespace glfw
{
GLFWwindow *createWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share)
{
    return glfwCreateWindow(width, height, title, monitor, share);
}

vk::Result createWindowSurface(VkInstance instance, GLFWwindow *window, const VkAllocationCallbacks *allocator,
                               VkSurfaceKHR *surface)
{
    return static_cast<vk::Result>(glfwCreateWindowSurface(instance, window, allocator, surface));
}

[[nodiscard]] NativeMonitorHandle nativeMonitorFromWindow(GLFWwindow *window) noexcept
{
    if (window == nullptr)
    {
        return 0;
    }

    auto hwnd = glfwGetWin32Window(window);
    if (hwnd == nullptr)
    {
        return 0;
    }

    return reinterpret_cast<NativeMonitorHandle>(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
}
} // namespace glfw
