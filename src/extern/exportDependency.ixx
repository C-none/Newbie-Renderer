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
#include "nsightGraphicsSdkBridge.h"

export module dependency;
import std;

export namespace vk
{
using ::vk::AccelerationStructureBuildGeometryInfoKHR;
using ::vk::AccelerationStructureBuildRangeInfoKHR;
using ::vk::AccelerationStructureBuildTypeKHR;
using ::vk::AccelerationStructureCreateInfoKHR;
using ::vk::AccelerationStructureCreateFlagsKHR;
using ::vk::AccelerationStructureDeviceAddressInfoKHR;
using ::vk::AccelerationStructureGeometryAabbsDataKHR;
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
using ::vk::CopyAccelerationStructureModeKHR;
using ::vk::CopyAccelerationStructureToMemoryInfoKHR;
using ::vk::CopyBufferInfo2;
using ::vk::CopyBufferToImageInfo2;
using ::vk::CopyImageInfo2;
using ::vk::CopyImageToBufferInfo2;
using ::vk::CopyMemoryToAccelerationStructureInfoKHR;
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
using ::vk::DeferredOperationKHR;
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
using ::vk::DeviceOrHostAddressConstKHR;
using ::vk::DeviceOrHostAddressKHR;
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
using ::vk::EXTFrameBoundaryExtensionName;
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
using ::vk::FrameBoundaryEXT;
using ::vk::FrameBoundaryFlagBitsEXT;
using ::vk::FrameBoundaryFlagsEXT;
using ::vk::FrontFace;
using ::vk::GeometryFlagsKHR;
using ::vk::GeometryTypeKHR;
using ::vk::GraphicsPipelineCreateInfo;
using ::vk::AabbPositionsKHR;
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
using ::vk::KHRRayTracingMaintenance1ExtensionName;
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
using ::vk::PhysicalDeviceFrameBoundaryFeaturesEXT;
using ::vk::PhysicalDeviceMeshShaderFeaturesEXT;
using ::vk::PhysicalDeviceOpacityMicromapFeaturesEXT;
using ::vk::PhysicalDeviceProperties;
using ::vk::PhysicalDeviceProperties2;
using ::vk::PhysicalDeviceRayQueryFeaturesKHR;
using ::vk::PhysicalDeviceRayTracingInvocationReorderFeaturesNV;
using ::vk::PhysicalDeviceRayTracingMaintenance1FeaturesKHR;
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
using ::vk::PipelineCreateFlagBits;
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
using ::vk::QueryPool;
using ::vk::QueryType;
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
using ::vk::ShaderGroupShaderKHR;
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
using ::vk::StructureType;
using ::vk::to_string;
using ::vk::TraceRaysIndirectCommand2KHR;
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
export using ::VkQueue;
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

export namespace nr::platform
{
enum class NsightGraphicsActivity : std::uint32_t
{
    Off,
    Capture,
    Trace,
};

enum class NsightGraphicsResult : std::uint32_t
{
    Success,
    Unavailable,
    NotFound,
    DifferentActivity,
    InvalidParameter,
    InvalidState,
    Timeout,
    Failed,
};

enum class NsightGraphicsCaptureDelimiter : std::uint32_t
{
    Present,
    FrameBoundary,
    VkFrameBoundaryExt,
};

struct NsightGraphicsConfig
{
    NsightGraphicsActivity activity = NsightGraphicsActivity::Off;
    std::wstring installationPath{};
    std::string outputDir{};
    std::uint32_t frameCount = 1;
    bool noHud = true;
};

struct NsightGraphicsCaptureRequest
{
    NsightGraphicsCaptureDelimiter delimiter = NsightGraphicsCaptureDelimiter::VkFrameBoundaryExt;
    std::uint32_t framesBeforeStart = 0;
    std::uint32_t framesToCapture = 1;
};

struct NsightGraphicsFrameBoundary
{
    VkQueue queue = nullptr;
    VkImage outputImage = nullptr;
    bool hasOutputImage = false;
};

struct NsightGraphicsTraceStop
{
    VkQueue queue = nullptr;
    VkImage outputImage = nullptr;
    bool hasOutputImage = false;
    bool stopOnNextFrameBoundary = true;
};
} // namespace nr::platform

namespace nr::platform_detail
{
[[nodiscard]] inline NrPlatformNsightGraphicsActivity toPlatform(nr::platform::NsightGraphicsActivity activity) noexcept
{
    switch (activity)
    {
    case nr::platform::NsightGraphicsActivity::Off:
        return NrPlatformNsightGraphicsActivity::Off;
    case nr::platform::NsightGraphicsActivity::Capture:
        return NrPlatformNsightGraphicsActivity::Capture;
    case nr::platform::NsightGraphicsActivity::Trace:
        return NrPlatformNsightGraphicsActivity::Trace;
    }
    return NrPlatformNsightGraphicsActivity::Off;
}

[[nodiscard]] inline NrPlatformNsightGraphicsCaptureDelimiter toPlatform(nr::platform::NsightGraphicsCaptureDelimiter delimiter) noexcept
{
    switch (delimiter)
    {
    case nr::platform::NsightGraphicsCaptureDelimiter::Present:
        return NrPlatformNsightGraphicsCaptureDelimiter::Present;
    case nr::platform::NsightGraphicsCaptureDelimiter::FrameBoundary:
        return NrPlatformNsightGraphicsCaptureDelimiter::FrameBoundary;
    case nr::platform::NsightGraphicsCaptureDelimiter::VkFrameBoundaryExt:
        return NrPlatformNsightGraphicsCaptureDelimiter::VkFrameBoundaryExt;
    }
    return NrPlatformNsightGraphicsCaptureDelimiter::FrameBoundary;
}

[[nodiscard]] inline nr::platform::NsightGraphicsResult toNsightGraphicsResult(NrPlatformNsightGraphicsResult result) noexcept
{
    switch (result)
    {
    case NrPlatformNsightGraphicsResult::Success:
        return nr::platform::NsightGraphicsResult::Success;
    case NrPlatformNsightGraphicsResult::Unavailable:
        return nr::platform::NsightGraphicsResult::Unavailable;
    case NrPlatformNsightGraphicsResult::NotFound:
        return nr::platform::NsightGraphicsResult::NotFound;
    case NrPlatformNsightGraphicsResult::DifferentActivity:
        return nr::platform::NsightGraphicsResult::DifferentActivity;
    case NrPlatformNsightGraphicsResult::InvalidParameter:
        return nr::platform::NsightGraphicsResult::InvalidParameter;
    case NrPlatformNsightGraphicsResult::InvalidState:
        return nr::platform::NsightGraphicsResult::InvalidState;
    case NrPlatformNsightGraphicsResult::Timeout:
        return nr::platform::NsightGraphicsResult::Timeout;
    case NrPlatformNsightGraphicsResult::Failed:
        return nr::platform::NsightGraphicsResult::Failed;
    }
    return nr::platform::NsightGraphicsResult::Failed;
}
} // namespace nr::platform_detail

export namespace nr::platform
{
[[nodiscard]] inline bool nsightGraphicsSdkCompiled() noexcept
{
    return nrPlatformNsightGraphicsSdkCompiled();
}

[[nodiscard]] inline NsightGraphicsResult injectNsightGraphics(const NsightGraphicsConfig& config) noexcept
{
    auto desc = NrPlatformNsightGraphicsInjectDesc{
        .activity = nr::platform_detail::toPlatform(config.activity),
        .installationPath = config.installationPath.empty() ? nullptr : config.installationPath.c_str(),
        .outputDir = config.outputDir.empty() ? nullptr : config.outputDir.c_str(),
        .frameCount = config.frameCount,
        .noHud = config.noHud,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsInject(&desc));
}

[[nodiscard]] inline NsightGraphicsResult initializeNsightGraphics(NsightGraphicsActivity activity) noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsInitialize(nr::platform_detail::toPlatform(activity)));
}

[[nodiscard]] inline NsightGraphicsResult activateNsightTrace(VkQueue queue) noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsActivateTrace(queue));
}

[[nodiscard]] inline NsightGraphicsResult requestNsightCapture(const NsightGraphicsCaptureRequest& request) noexcept
{
    auto platformRequest = NrPlatformNsightGraphicsCaptureRequest{
        .delimiter = nr::platform_detail::toPlatform(request.delimiter),
        .framesBeforeStart = request.framesBeforeStart,
        .framesToCapture = request.framesToCapture,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsRequestCapture(&platformRequest));
}

[[nodiscard]] inline NsightGraphicsResult startNsightTrace() noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsStartTrace());
}

[[nodiscard]] inline NsightGraphicsResult stopNsightTrace(const NsightGraphicsTraceStop& desc) noexcept
{
    auto platformDesc = NrPlatformNsightGraphicsTraceStop{
        .queue = desc.queue,
        .outputImage = desc.outputImage,
        .hasOutputImage = desc.hasOutputImage,
        .stopOnNextFrameBoundary = desc.stopOnNextFrameBoundary,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsStopTrace(&platformDesc));
}

[[nodiscard]] inline NsightGraphicsResult markNsightFrameBoundary(const NsightGraphicsFrameBoundary& desc) noexcept
{
    auto platformDesc = NrPlatformNsightGraphicsFrameBoundary{
        .queue = desc.queue,
        .outputImage = desc.outputImage,
        .hasOutputImage = desc.hasOutputImage,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsMarkFrameBoundary(&platformDesc));
}

/**
 * @brief Whether NVIDIA Nsight Graphics is intercepting the current process.
 *
 * Windows-only; returns false otherwise. The memory allocator queries this to
 * switch GpuOnly buffers onto a profiler-safe path that avoids
 * VkMemoryDedicatedAllocateInfo, which conflicts with the
 * VkImportMemoryHostPointerInfoEXT Nsight injects (VUID-VkMemoryAllocateInfo-pNext-02806).
 */
inline bool isNsightInjected() noexcept
{
    return nrPlatformNsightInjected();
}
} // namespace nr::platform
