module;
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

module dependency.window;

namespace glfw
{
GLFWwindow* createWindow(
    int width,
    int height,
    const char* title,
    GLFWmonitor* monitor,
    GLFWwindow* share)
{
    return glfwCreateWindow(width, height, title, monitor, share);
}

vk::Result createWindowSurface(
    VkInstance instance,
    GLFWwindow* window,
    const VkAllocationCallbacks* allocator,
    VkSurfaceKHR* surface)
{
    return static_cast<vk::Result>(
        glfwCreateWindowSurface(instance, window, allocator, surface));
}
} // namespace glfw
