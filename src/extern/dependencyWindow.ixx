module;
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

export module dependency.window;
export import dependency.vulkan;

export using ::GLFWmonitor;
export using ::GLFWvidmode;
export using ::GLFWwindow;
export using ::glfwDestroyWindow;
export using ::glfwGetCursorPos;
export using ::glfwGetFramebufferSize;
export using ::glfwGetKey;
export using ::glfwGetMonitorPos;
export using ::glfwGetMonitors;
export using ::glfwGetMouseButton;
export using ::glfwGetPrimaryMonitor;
export using ::glfwGetRequiredInstanceExtensions;
export using ::glfwGetVideoMode;
export using ::glfwGetWindowAttrib;
export using ::glfwGetWindowPos;
export using ::glfwGetWindowSize;
export using ::glfwGetWindowUserPointer;
export using ::glfwInit;
export using ::glfwPollEvents;
export using ::glfwSetCharCallback;
export using ::glfwSetErrorCallback;
export using ::glfwSetWindowAttrib;
export using ::glfwSetWindowPos;
export using ::glfwSetWindowSize;
export using ::glfwSetWindowUserPointer;
export using ::glfwTerminate;
export using ::glfwWindowHint;
export using ::glfwWindowShouldClose;


#ifdef GLFW_NO_API
#undef GLFW_NO_API
#endif
export inline constexpr int GLFW_NO_API = 0;

#ifdef GLFW_CLIENT_API
#undef GLFW_CLIENT_API
#endif
export inline constexpr int GLFW_CLIENT_API = 0x00022001;

#ifdef GLFW_DECORATED
#undef GLFW_DECORATED
#endif
export inline constexpr int GLFW_DECORATED = 0x00020005;

#ifdef GLFW_FALSE
#undef GLFW_FALSE
#endif
export inline constexpr int GLFW_FALSE = 0;

#ifdef GLFW_TRUE
#undef GLFW_TRUE
#endif
export inline constexpr int GLFW_TRUE = 1;

export namespace glfw
{
GLFWwindow* createWindow(
    int width,
    int height,
    const char* title,
    GLFWmonitor* monitor,
    GLFWwindow* share);

vk::Result createWindowSurface(
    VkInstance instance,
    GLFWwindow* window,
    const VkAllocationCallbacks* allocator,
    VkSurfaceKHR* surface);
} // namespace glfw
