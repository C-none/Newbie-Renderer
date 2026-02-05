module;
#include <vulkan/vulkan_raii.hpp>
#include <slang.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <vk_mem_alloc.h>
export module dependency;
export import <vulkan/vulkan_raii.hpp>;
export import <slang.h>;
export import <GLFW/glfw3.h>;
export import <glm/glm.hpp>;
export import <imgui.h>;
export import <vk_mem_alloc.h>;
#ifdef GLFW_NO_API
#undef GLFW_NO_API
#endif // GLFW_NO_API
export inline constexpr int GLFW_NO_API = 0;

#ifdef GLFW_CLIENT_API
#undef GLFW_CLIENT_API
#endif
export inline constexpr int GLFW_CLIENT_API = 0x00022001;

export namespace glfw
{
inline GLFWwindow *createWindow(int width, int height, const char *title, GLFWmonitor *monitor, GLFWwindow *share)
{
    return glfwCreateWindow(width, height, title, monitor, share);
}
inline vk::Result createWindowSurface(VkInstance instance, GLFWwindow *window, const VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    return static_cast<vk::Result>(glfwCreateWindowSurface(instance, window, allocator, surface));
}
} // namespace glfw