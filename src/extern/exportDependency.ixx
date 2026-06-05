module;
#include <vulkan/vulkan_raii.hpp>
#include <slang.h>
#include <slang-com-ptr.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <flecs.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#undef STB_IMAGE_IMPLEMENTATION
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
#include <turbojpeg.h>
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
// #define TRACY_ENABLE
// #include <Tracy.hpp>
export module dependency;
export import <vulkan/vulkan_raii.hpp>;
export import <slang.h>;
export import <slang-com-ptr.h>;
export import <GLFW/glfw3.h>;
export import <glm/glm.hpp>;
export import <glm/gtc/quaternion.hpp>;
export import <imgui.h>;
export import <assimp/Importer.hpp>;
export import <assimp/material.h>;
export import <assimp/postprocess.h>;
export import <assimp/scene.h>;
export import <stb_image.h>;
export import <turbojpeg.h>;
export import <vk_mem_alloc.h>;
// export import <Tracy.hpp>

export namespace flecs
{
using ::flecs::entity_t;
using ::flecs::entity;
using ::flecs::world;
using ::flecs::query;
} // namespace flecs

// Narrow flecs C API re-exports used by scene internals.
export using ::ecs_entity_t;
export using ::ecs_iter_t;
export using ::ecs_world_t;
export using ::EcsParent;
export using ::EcsChildOf;
export using ::EcsIsA;
export using ::EcsPrefab;
export using ::ecs_children;
export using ::ecs_children_next;
export using ::ecs_get_parent;
export using ::ecs_init;

#ifdef GLFW_NO_API
#undef GLFW_NO_API
#endif // GLFW_NO_API
export inline constexpr int GLFW_NO_API = 0;

#ifdef GLFW_CLIENT_API
#undef GLFW_CLIENT_API
#endif
export inline constexpr int GLFW_CLIENT_API = 0x00022001;

#ifdef TJFLAG_FASTDCT
#undef TJFLAG_FASTDCT
#endif
export constexpr inline uint32_t TJFLAG_FASTDCT = 2048;

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
