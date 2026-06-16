module;
#include <cstddef>
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
#include <stb_image.h>
#include <turbojpeg.h>
#include <vk_mem_alloc.h>

export module dependency;

export namespace vk
{
using ::vk::AccelerationStructureBuildGeometryInfoKHR;
using ::vk::AccelerationStructureBuildRangeInfoKHR;
using ::vk::AccelerationStructureBuildTypeKHR;
using ::vk::AccelerationStructureCreateInfoKHR;
using ::vk::AccelerationStructureDeviceAddressInfoKHR;
using ::vk::AccelerationStructureGeometryInstancesDataKHR;
using ::vk::AccelerationStructureGeometryKHR;
using ::vk::AccelerationStructureGeometryTrianglesDataKHR;
using ::vk::AccelerationStructureKHR;
using ::vk::AccelerationStructureTypeKHR;
using ::vk::AccessFlagBits2;
using ::vk::AccessFlags2;
using ::vk::ApiVersion14;
using ::vk::ApplicationInfo;
using ::vk::ArrayProxy;
using ::vk::AttachmentLoadOp;
using ::vk::AttachmentStoreOp;
using ::vk::BlendFactor;
using ::vk::BlendOp;
using ::vk::Bool32;
using ::vk::BorderColor;
using ::vk::Buffer;
using ::vk::BufferCopy;
using ::vk::BufferCopy2;
using ::vk::BufferCreateFlags;
using ::vk::BufferCreateInfo;
using ::vk::BufferDeviceAddressInfo;
using ::vk::BufferImageCopy;
using ::vk::BufferImageCopy2;
using ::vk::BufferMemoryBarrier2;
using ::vk::BufferUsageFlagBits;
using ::vk::BufferUsageFlags;
using ::vk::BufferView;
using ::vk::BufferViewCreateInfo;
using ::vk::BuildAccelerationStructureFlagBitsKHR;
using ::vk::BuildAccelerationStructureFlagsKHR;
using ::vk::BuildAccelerationStructureModeKHR;
using ::vk::ClearColorValue;
using ::vk::ClearDepthStencilValue;
using ::vk::ClearValue;
using ::vk::ColorComponentFlagBits;
using ::vk::CommandBuffer;
using ::vk::CommandBufferAllocateInfo;
using ::vk::CommandBufferBeginInfo;
using ::vk::CommandBufferInheritanceInfo;
using ::vk::CommandBufferLevel;
using ::vk::CommandBufferSubmitInfo;
using ::vk::CommandBufferUsageFlagBits;
using ::vk::CommandBufferUsageFlags;
using ::vk::CommandPool;
using ::vk::CommandPoolCreateFlagBits;
using ::vk::CommandPoolCreateFlags;
using ::vk::CommandPoolCreateInfo;
using ::vk::CommandPoolResetFlags;
using ::vk::CompareOp;
using ::vk::ComponentMapping;
using ::vk::CompositeAlphaFlagBitsKHR;
using ::vk::ComputePipelineCreateInfo;
using ::vk::CopyAccelerationStructureInfoKHR;
using ::vk::CopyBufferInfo2;
using ::vk::CopyBufferToImageInfo2;
using ::vk::CopyImageInfo2;
using ::vk::CopyImageToBufferInfo2;
using ::vk::CullModeFlagBits;
using ::vk::CullModeFlags;
using ::vk::DebugUtilsLabelEXT;
using ::vk::DebugUtilsMessageSeverityFlagBitsEXT;
using ::vk::DebugUtilsMessageSeverityFlagsEXT;
using ::vk::DebugUtilsMessageTypeFlagBitsEXT;
using ::vk::DebugUtilsMessageTypeFlagsEXT;
using ::vk::DebugUtilsMessengerCallbackDataEXT;
using ::vk::DebugUtilsMessengerCreateInfoEXT;
using ::vk::DebugUtilsObjectNameInfoEXT;
using ::vk::DependencyInfo;
using ::vk::DescriptorBindingFlagBits;
using ::vk::DescriptorBindingFlags;
using ::vk::DescriptorBufferInfo;
using ::vk::DescriptorImageInfo;
using ::vk::DescriptorPoolCreateFlagBits;
using ::vk::DescriptorPoolCreateFlags;
using ::vk::DescriptorPoolCreateInfo;
using ::vk::DescriptorPoolInlineUniformBlockCreateInfo;
using ::vk::DescriptorPoolSize;
using ::vk::DescriptorSet;
using ::vk::DescriptorSetAllocateInfo;
using ::vk::DescriptorSetLayout;
using ::vk::DescriptorSetLayoutBinding;
using ::vk::DescriptorSetLayoutBindingFlagsCreateInfo;
using ::vk::DescriptorSetLayoutCreateFlagBits;
using ::vk::DescriptorSetLayoutCreateInfo;
using ::vk::DescriptorSetVariableDescriptorCountAllocateInfo;
using ::vk::DescriptorType;
using ::vk::DeviceAddress;
using ::vk::DeviceCreateFlags;
using ::vk::DeviceCreateInfo;
using ::vk::DeviceQueueCreateInfo;
using ::vk::DeviceSize;
using ::vk::DynamicState;
using ::vk::enumerateInstanceExtensionProperties;
using ::vk::enumerateInstanceLayerProperties;
using ::vk::EXTDebugUtilsExtensionName;
using ::vk::ExtensionProperties;
using ::vk::Extent2D;
using ::vk::Extent3D;
using ::vk::EXTExtendedDynamicState3ExtensionName;
using ::vk::EXTMemoryBudgetExtensionName;
using ::vk::EXTMeshShaderExtensionName;
using ::vk::EXTOpacityMicromapExtensionName;
using ::vk::EXTRayTracingInvocationReorderExtensionName;
using ::vk::False;
using ::vk::Fence;
using ::vk::FenceCreateFlagBits;
using ::vk::FenceCreateInfo;
using ::vk::Filter;
using ::vk::Format;
using ::vk::FrontFace;
using ::vk::GeometryFlagsKHR;
using ::vk::GeometryTypeKHR;
using ::vk::GraphicsPipelineCreateInfo;
using ::vk::Image;
using ::vk::ImageAspectFlagBits;
using ::vk::ImageAspectFlags;
using ::vk::ImageCopy;
using ::vk::ImageCopy2;
using ::vk::ImageCreateFlags;
using ::vk::ImageCreateInfo;
using ::vk::ImageLayout;
using ::vk::ImageMemoryBarrier2;
using ::vk::ImageSubresourceLayers;
using ::vk::ImageSubresourceRange;
using ::vk::ImageTiling;
using ::vk::ImageType;
using ::vk::ImageUsageFlagBits;
using ::vk::ImageUsageFlags;
using ::vk::ImageView;
using ::vk::ImageViewCreateInfo;
using ::vk::ImageViewType;
using ::vk::IndexType;
using ::vk::InstanceCreateInfo;
using ::vk::KHRAccelerationStructureExtensionName;
using ::vk::KHRDeferredHostOperationsExtensionName;
using ::vk::KHRPipelineLibraryExtensionName;
using ::vk::KHRRayQueryExtensionName;
using ::vk::KHRRayTracingPipelineExtensionName;
using ::vk::KHRShaderFloatControlsExtensionName;
using ::vk::KHRSpirv14ExtensionName;
using ::vk::KHRSwapchainExtensionName;
using ::vk::LayerProperties;
using ::vk::LodClampNone;
using ::vk::MemoryBarrier2;
using ::vk::NVCooperativeVectorExtensionName;
using ::vk::ObjectType;
using ::vk::Offset2D;
using ::vk::Optional;
using ::vk::PhysicalDeviceAccelerationStructureFeaturesKHR;
using ::vk::PhysicalDeviceAccelerationStructurePropertiesKHR;
using ::vk::PhysicalDeviceCooperativeVectorFeaturesNV;
using ::vk::PhysicalDeviceDescriptorIndexingProperties;
using ::vk::PhysicalDeviceExtendedDynamicState3FeaturesEXT;
using ::vk::PhysicalDeviceFeatures2;
using ::vk::PhysicalDeviceMeshShaderFeaturesEXT;
using ::vk::PhysicalDeviceOpacityMicromapFeaturesEXT;
using ::vk::PhysicalDeviceProperties;
using ::vk::PhysicalDeviceProperties2;
using ::vk::PhysicalDeviceRayQueryFeaturesKHR;
using ::vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV;
using ::vk::PhysicalDeviceRayTracingPipelineFeaturesKHR;
using ::vk::PhysicalDeviceRayTracingPipelinePropertiesKHR;
using ::vk::PhysicalDeviceType;
using ::vk::PhysicalDeviceVulkan11Features;
using ::vk::PhysicalDeviceVulkan12Features;
using ::vk::PhysicalDeviceVulkan13Features;
using ::vk::PhysicalDeviceVulkan14Features;
using ::vk::PhysicalDeviceVulkan14Properties;
using ::vk::Pipeline;
using ::vk::PipelineBindPoint;
using ::vk::PipelineCacheCreateInfo;
using ::vk::PipelineColorBlendAttachmentState;
using ::vk::PipelineColorBlendStateCreateInfo;
using ::vk::PipelineCreateFlags;
using ::vk::PipelineDepthStencilStateCreateInfo;
using ::vk::PipelineDynamicStateCreateInfo;
using ::vk::PipelineInputAssemblyStateCreateInfo;
using ::vk::PipelineLayout;
using ::vk::PipelineLayoutCreateInfo;
using ::vk::PipelineLibraryCreateInfoKHR;
using ::vk::PipelineMultisampleStateCreateInfo;
using ::vk::PipelineRasterizationStateCreateInfo;
using ::vk::PipelineRenderingCreateInfo;
using ::vk::PipelineRobustnessBufferBehavior;
using ::vk::PipelineRobustnessImageBehavior;
using ::vk::PipelineShaderStageCreateInfo;
using ::vk::PipelineStageFlagBits2;
using ::vk::PipelineStageFlags2;
using ::vk::PipelineVertexInputStateCreateInfo;
using ::vk::PipelineViewportStateCreateInfo;
using ::vk::PolygonMode;
using ::vk::PresentInfoKHR;
using ::vk::PresentModeKHR;
using ::vk::PrimitiveTopology;
using ::vk::PushConstantRange;
using ::vk::QueueFlagBits;
using ::vk::QueueFlags;
using ::vk::RayTracingPipelineCreateInfoKHR;
using ::vk::RayTracingPipelineInterfaceCreateInfoKHR;
using ::vk::RayTracingShaderGroupCreateInfoKHR;
using ::vk::RayTracingShaderGroupTypeKHR;
using ::vk::Rect2D;
using ::vk::RenderingAttachmentInfo;
using ::vk::RenderingFlags;
using ::vk::RenderingInfo;
using ::vk::ResolveModeFlagBits;
using ::vk::Result;
using ::vk::SampleCountFlagBits;
using ::vk::Sampler;
using ::vk::SamplerAddressMode;
using ::vk::SamplerCreateInfo;
using ::vk::SamplerMipmapMode;
using ::vk::Semaphore;
using ::vk::SemaphoreCreateInfo;
using ::vk::SemaphoreSubmitInfo;
using ::vk::SemaphoreType;
using ::vk::SemaphoreTypeCreateInfo;
using ::vk::SemaphoreWaitInfo;
using ::vk::ShaderModuleCreateInfo;
using ::vk::ShaderStageFlagBits;
using ::vk::ShaderStageFlags;
using ::vk::SharingMode;
using ::vk::StridedDeviceAddressRegionKHR;
using ::vk::SubmitInfo2;
using ::vk::SurfaceFormatKHR;
using ::vk::SurfaceTransformFlagBitsKHR;
using ::vk::SwapchainCreateFlagsKHR;
using ::vk::SwapchainCreateInfoKHR;
using ::vk::SwapchainKHR;
using ::vk::SystemError;
using ::vk::StructureChain;
using ::vk::to_string;
using ::vk::True;
using ::vk::VertexInputAttributeDescription;
using ::vk::VertexInputBindingDescription;
using ::vk::VertexInputRate;
using ::vk::Viewport;
using ::vk::WholeSize;
using ::vk::WriteDescriptorSet;
using ::vk::WriteDescriptorSetAccelerationStructureKHR;
using ::vk::WriteDescriptorSetInlineUniformBlock;
using ::vk::operator&;
using ::vk::operator|;
using ::vk::UuidSize;

namespace detail
{
using ::vk::detail::DispatchLoaderStatic;
using ::vk::detail::getDispatchLoaderStatic;
using ::vk::detail::resultCheck;
}

namespace raii
{
using ::vk::raii::AccelerationStructureKHR;
using ::vk::raii::BufferView;
using ::vk::raii::CommandBuffer;
using ::vk::raii::CommandBuffers;
using ::vk::raii::CommandPool;
using ::vk::raii::Context;
using ::vk::raii::DebugUtilsMessengerEXT;
using ::vk::raii::DeferredOperationKHR;
using ::vk::raii::DescriptorPool;
using ::vk::raii::DescriptorSetLayout;
using ::vk::raii::Device;
using ::vk::raii::Fence;
using ::vk::raii::ImageView;
using ::vk::raii::Instance;
using ::vk::raii::PhysicalDevice;
using ::vk::raii::PhysicalDevices;
using ::vk::raii::Pipeline;
using ::vk::raii::PipelineCache;
using ::vk::raii::PipelineLayout;
using ::vk::raii::Queue;
using ::vk::raii::Sampler;
using ::vk::raii::Semaphore;
using ::vk::raii::ShaderModule;
using ::vk::raii::SurfaceKHR;
using ::vk::raii::SwapchainKHR;
}
} // namespace vk

export namespace glm
{
using ::glm::all;
using ::glm::any;
using ::glm::cross;
using ::glm::determinant;
using ::glm::dot;
using ::glm::dvec2;
using ::glm::half_pi;
using ::glm::inverse;
using ::glm::length;
using ::glm::length_t;
using ::glm::lessThanEqual;
using ::glm::lookAtRH;
using ::glm::mat4;
using ::glm::max;
using ::glm::min;
using ::glm::normalize;
using ::glm::notEqual;
using ::glm::orthoRH_ZO;
using ::glm::packSnorm2x16;
using ::glm::perspectiveRH_ZO;
using ::glm::pi;
using ::glm::qualifier;
using ::glm::quat;
using ::glm::radians;
using ::glm::transpose;
using ::glm::uvec2;
using ::glm::uvec3;
using ::glm::uvec4;
using ::glm::vec;
using ::glm::vec2;
using ::glm::vec3;
using ::glm::vec4;
using ::glm::operator!=;
using ::glm::operator*;
using ::glm::operator+;
using ::glm::operator-;
using ::glm::operator/;
using ::glm::operator==;
} // namespace glm

export namespace ImGui
{
using ::ImGui::Begin;
using ::ImGui::Checkbox;
using ::ImGui::CreateContext;
using ::ImGui::DestroyContext;
using ::ImGui::End;
using ::ImGui::EndFrame;
using ::ImGui::GetDrawData;
using ::ImGui::GetIO;
using ::ImGui::NewFrame;
using ::ImGui::Render;
using ::ImGui::Separator;
using ::ImGui::SetCurrentContext;
using ::ImGui::SetNextWindowPos;
using ::ImGui::SetNextWindowSize;
using ::ImGui::StyleColorsDark;
using ::ImGui::TextUnformatted;
} // namespace ImGui

export namespace Assimp
{
using ::Assimp::Importer;
} // namespace Assimp

export namespace flecs
{
using ::flecs::entity;
using ::flecs::entity_t;
using ::flecs::query;
using ::flecs::world;
} // namespace flecs

export namespace Slang
{
using ::Slang::ComPtr;
} // namespace Slang

export namespace slang
{
using ::slang::BindingType;
using ::slang::CompilerOptionEntry;
using ::slang::CompilerOptionName;
using ::slang::CompilerOptionValue;
using ::slang::CompilerOptionValueKind;
using ::slang::createGlobalSession;
using ::slang::EntryPointReflection;
using ::slang::IBlob;
using ::slang::IComponentType;
using ::slang::IEntryPoint;
using ::slang::IGlobalSession;
using ::slang::IModule;
using ::slang::ISession;
using ::slang::ParameterCategory;
using ::slang::PreprocessorMacroDesc;
using ::slang::ProgramLayout;
using ::slang::SessionDesc;
using ::slang::TargetDesc;
using ::slang::TypeLayoutReflection;
using ::slang::TypeReflection;
} // namespace slang

export using ::GLFWmonitor;
export using ::GLFWwindow;
export using ::glfwDestroyWindow;
export using ::glfwGetCursorPos;
export using ::glfwGetFramebufferSize;
export using ::glfwGetKey;
export using ::glfwGetMouseButton;
export using ::glfwGetRequiredInstanceExtensions;
export using ::glfwInit;
export using ::glfwPollEvents;
export using ::glfwSetErrorCallback;
export using ::glfwTerminate;
export using ::glfwWindowHint;
export using ::glfwWindowShouldClose;

export using ::ai_real;
export using ::aiColor3D;
export using ::aiColor4D;
export using ::aiGetMaterialColor;
export using ::aiGetMaterialFloatArray;
export using ::aiGetMaterialIntegerArray;
export using ::aiLightSource_AMBIENT;
export using ::aiLightSource_AREA;
export using ::aiLightSource_DIRECTIONAL;
export using ::aiLightSource_POINT;
export using ::aiLightSource_SPOT;
export using ::aiLightSource_UNDEFINED;
export using ::aiLightSourceType;
export using ::aiMaterial;
export using ::aiMatrix4x4;
export using ::aiNode;
export using ::aiPostProcessSteps;
export using ::aiProcess_CalcTangentSpace;
export using ::aiProcess_GenSmoothNormals;
export using ::aiProcess_JoinIdenticalVertices;
export using ::aiProcess_OptimizeGraph;
export using ::aiProcess_OptimizeMeshes;
export using ::aiProcess_PreTransformVertices;
export using ::aiProcess_SortByPType;
export using ::aiProcess_Triangulate;
export using ::aiProcess_ValidateDataStructure;
export using ::aiReturn_SUCCESS;
export using ::aiScene;
export using ::aiString;
export using ::aiTexture;
export using ::aiTextureType;
export using ::aiTextureType_AMBIENT;
export using ::aiTextureType_DIFFUSE;
export using ::aiTextureType_DISPLACEMENT;
export using ::aiTextureType_EMISSIVE;
export using ::aiTextureType_HEIGHT;
export using ::aiTextureType_LIGHTMAP;
export using ::aiTextureType_NONE;
export using ::aiTextureType_NORMALS;
export using ::aiTextureType_OPACITY;
export using ::aiTextureType_REFLECTION;
export using ::aiTextureType_SHININESS;
export using ::aiTextureType_SPECULAR;
export using ::aiTextureType_UNKNOWN;
export using ::aiVector3D;

export using ::EcsChildOf;
export using ::EcsIsA;
export using ::EcsParent;
export using ::EcsPrefab;
export using ::ecs_children;
export using ::ecs_children_next;
export using ::ecs_get_parent;
export using ::ecs_init;

export using ::ImDrawData;
export using ::ImDrawIdx;
export using ::ImDrawVert;
export using ::ImGuiBackendFlags_RendererHasTextures;
export using ::ImGuiBackendFlags_RendererHasVtxOffset;
export using ::ImGuiCond_FirstUseEver;
export using ::ImGuiContext;
export using ::ImGuiWindowFlags;
export using ::ImTextureData;
export using ::ImTextureFormat_Alpha8;
export using ::ImTextureFormat_RGBA32;
export using ::ImTextureID;
export using ::ImTextureStatus_Destroyed;
export using ::ImTextureStatus_OK;
export using ::ImTextureStatus_WantCreate;
export using ::ImTextureStatus_WantDestroy;
export using ::ImTextureStatus_WantUpdates;
export using ::ImVec2;

export using ::SlangCompileTarget;
export using ::SlangInt;
export using ::SlangInt32;
export using ::SlangResourceAccess;
export using ::SlangResourceShape;
export using ::SlangResult;
export using ::SlangStage;
export using ::SlangUInt;
export using ::slang_createBlob;
export using ::SLANG_DEBUG_INFO_LEVEL_MAXIMAL;
export using ::SLANG_DEBUG_INFO_LEVEL_NONE;
export using ::SLANG_OPTIMIZATION_LEVEL_MAXIMAL;
export using ::SLANG_OPTIMIZATION_LEVEL_NONE;
export using ::SLANG_PARAMETER_CATEGORY_UNIFORM;
export using ::SLANG_PROFILE_UNKNOWN;
export using ::SLANG_SPIRV;
export using ::SLANG_STAGE_AMPLIFICATION;
export using ::SLANG_STAGE_ANY_HIT;
export using ::SLANG_STAGE_CALLABLE;
export using ::SLANG_STAGE_CLOSEST_HIT;
export using ::SLANG_STAGE_COMPUTE;
export using ::SLANG_STAGE_DOMAIN;
export using ::SLANG_STAGE_FRAGMENT;
export using ::SLANG_STAGE_GEOMETRY;
export using ::SLANG_STAGE_HULL;
export using ::SLANG_STAGE_INTERSECTION;
export using ::SLANG_STAGE_MESH;
export using ::SLANG_STAGE_MISS;
export using ::SLANG_STAGE_NONE;
export using ::SLANG_STAGE_RAY_GENERATION;
export using ::SLANG_STAGE_VERTEX;

export using ::stbi_failure_reason;
export using ::stbi_image_free;
export using ::stbi_load_from_memory;
export using ::stbi_uc;
export using ::tjDecompress2;
export using ::tjDecompressHeader3;
export using ::tjDestroy;
export using ::tjGetErrorStr2;
export using ::tjhandle;
export using ::tjInitDecompress;
export using ::TJPF_RGBA;

export using ::VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
export using ::VK_PIPELINE_CREATE_LIBRARY_BIT_KHR;
export using ::VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
export using ::VK_SUCCESS;
export using ::VkAccelerationStructureKHR;
export using ::VkBool32;
export using ::VkBuffer;
export using ::VkBufferCreateInfo;
export using ::VkBufferDeviceAddressInfo;
export using ::VkBufferUsageFlags;
export using ::VkBufferView;
export using ::VkCommandBuffer;
export using ::VkDevice;
export using ::VkDeviceAddress;
export using ::VkDeviceSize;
export using ::VkImage;
export using ::VkImageCreateInfo;
export using ::VkImageView;
export using ::VkInstance;
export using ::VkPipeline;
export using ::VkImportMemoryHostPointerInfoEXT;
export using ::VkAllocationCallbacks;
export using ::VkMemoryAllocateInfo;
export using ::VkMemoryPropertyFlags;
export using ::VkResult;
export using ::VkSurfaceKHR;

export using ::VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
export using ::VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
export using ::VMA_ALLOCATION_CREATE_MAPPED_BIT;
export using ::VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
export using ::VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;
export using ::VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE4_BIT;
export using ::VMA_ALLOCATOR_CREATE_KHR_MAINTENANCE5_BIT;
export using ::VMA_MEMORY_USAGE_AUTO;
export using ::VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
export using ::VMA_POOL_CREATE_LINEAR_ALGORITHM_BIT;
export using ::VmaAllocation;
export using ::VmaAllocationCreateInfo;
export using ::VmaAllocationInfo;
export using ::VmaAllocator;
export using ::VmaAllocatorCreateInfo;
export using ::VmaBudget;
export using ::VmaPool;
export using ::VmaPoolCreateInfo;
export using ::VmaStatistics;
export using ::VmaTotalStatistics;
export using ::vmaBuildStatsString;
export using ::vmaCalculateStatistics;
export using ::vmaCreateAllocator;
export using ::vmaCreateBuffer;
export using ::vmaCreateImage;
export using ::vmaCreatePool;
export using ::vmaDestroyAllocator;
export using ::vmaDestroyBuffer;
export using ::vmaDestroyImage;
export using ::vmaDestroyPool;
export using ::vmaFindMemoryTypeIndexForBufferInfo;
export using ::vmaFindMemoryTypeIndexForImageInfo;
export using ::vmaFlushAllocation;
export using ::vmaFreeStatsString;
export using ::vmaGetAllocationMemoryProperties;
export using ::vmaGetHeapBudgets;
export using ::vmaGetPoolStatistics;
export using ::vmaInvalidateAllocation;

#ifdef GLFW_NO_API
#undef GLFW_NO_API
#endif
export inline constexpr int GLFW_NO_API = 0;

#ifdef GLFW_CLIENT_API
#undef GLFW_CLIENT_API
#endif
export inline constexpr int GLFW_CLIENT_API = 0x00022001;

#ifdef TJFLAG_FASTDCT
#undef TJFLAG_FASTDCT
#endif
export inline constexpr unsigned int TJFLAG_FASTDCT = 2048u;

export namespace imgui
{
inline constexpr unsigned int drawVertPosOffset = static_cast<unsigned int>(offsetof(ImDrawVert, pos));
inline constexpr unsigned int drawVertUvOffset = static_cast<unsigned int>(offsetof(ImDrawVert, uv));
inline constexpr unsigned int drawVertColorOffset = static_cast<unsigned int>(offsetof(ImDrawVert, col));
} // namespace imgui

export namespace glfw
{
inline GLFWwindow* createWindow(
    int width,
    int height,
    const char* title,
    GLFWmonitor* monitor,
    GLFWwindow* share)
{
    return glfwCreateWindow(width, height, title, monitor, share);
}

inline vk::Result createWindowSurface(
    VkInstance instance,
    GLFWwindow* window,
    const VkAllocationCallbacks* allocator,
    VkSurfaceKHR* surface)
{
    return static_cast<vk::Result>(
        glfwCreateWindowSurface(instance, window, allocator, surface));
}
} // namespace glfw
