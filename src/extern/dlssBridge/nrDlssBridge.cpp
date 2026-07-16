#include "include/nrDlssBridge.h"

#include "DLSS/include/nvsdk_ngx_helpers_vk.h"
#include "DLSS/include/nvsdk_ngx_helpers_dlssd.h"
#include "DLSS/include/nvsdk_ngx_helpers_dlssd_vk.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <new>
#include <ranges>
#include <string_view>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

namespace
{
constexpr auto kProjectId = std::string_view{"eb1ed2e1-c610-47ef-a126-3e97625116d6"};
constexpr auto kEngineVersion = std::string_view{"0.1.0"};
constexpr auto kMaximumWindowsPathLength = std::size_t{32768u};

std::mutex ngxMutex{};

enum class ResourceSlot : std::size_t
{
    DiffuseAlbedo,
    SpecularAlbedo,
    Normals,
    Roughness,
    Color,
    Alpha,
    Output,
    OutputAlpha,
    Depth,
    MotionVectors,
    TransparencyMask,
    ExposureTexture,
    BiasCurrentColorMask,
    ReflectedAlbedo,
    ColorBeforeParticles,
    ColorAfterParticles,
    ColorBeforeTransparency,
    ColorAfterTransparency,
    ColorBeforeFog,
    ColorAfterFog,
    ScreenSpaceSubsurfaceScatteringGuide,
    ColorBeforeScreenSpaceSubsurfaceScattering,
    ColorAfterScreenSpaceSubsurfaceScattering,
    ScreenSpaceRefractionGuide,
    ColorBeforeScreenSpaceRefraction,
    ColorAfterScreenSpaceRefraction,
    DepthOfFieldGuide,
    ColorBeforeDepthOfField,
    ColorAfterDepthOfField,
    DiffuseHitDistance,
    SpecularHitDistance,
    DiffuseRayDirection,
    SpecularRayDirection,
    DiffuseRayDirectionHitDistance,
    SpecularRayDirectionHitDistance,
    GBufferAlbedo,
    GBufferRoughness,
    GBufferMetallic,
    GBufferSpecular,
    GBufferSubsurface,
    GBufferNormals,
    GBufferShadingModelId,
    GBufferMaterialId,
    GBufferSpecularAlbedo,
    GBufferIndirectAlbedo,
    GBufferSpecularMotionVectors,
    GBufferDisocclusionMask,
    GBufferEmissive,
    GBufferResponsivityMask,
    GBufferReserved14,
    GBufferReserved15,
    GBufferReserved16,
    MotionVectors3D,
    ParticleMask,
    AnimatedTextureMask,
    DepthHighResolution,
    PositionViewSpace,
    RayTracingHitDistance,
    ReflectionMotionVectors,
    TransparencyLayer,
    TransparencyLayerOpacity,
    TransparencyLayerMotionVectors,
    DisocclusionMask,
    ResponsivityMask,
    Count,
};
static_assert(static_cast<std::size_t>(ResourceSlot::Count) == NR_DLSS_BRIDGE_RR_RESOURCE_COUNT);

enum class SubrectSlot : std::size_t
{
    Alpha,
    OutputAlpha,
    DiffuseAlbedo,
    SpecularAlbedo,
    Normals,
    Roughness,
    Color,
    Depth,
    MotionVectors,
    Translucency,
    BiasCurrentColor,
    Output,
    ReflectedAlbedo,
    ColorBeforeParticles,
    ColorAfterParticles,
    ColorBeforeTransparency,
    ColorAfterTransparency,
    ColorBeforeFog,
    ColorAfterFog,
    ScreenSpaceSubsurfaceScatteringGuide,
    ColorBeforeScreenSpaceSubsurfaceScattering,
    ColorAfterScreenSpaceSubsurfaceScattering,
    ScreenSpaceRefractionGuide,
    ColorBeforeScreenSpaceRefraction,
    ColorAfterScreenSpaceRefraction,
    DepthOfFieldGuide,
    ColorBeforeDepthOfField,
    ColorAfterDepthOfField,
    DiffuseHitDistance,
    SpecularHitDistance,
    DiffuseRayDirection,
    SpecularRayDirection,
    DiffuseRayDirectionHitDistance,
    SpecularRayDirectionHitDistance,
    TransparencyLayer,
    TransparencyLayerOpacity,
    TransparencyLayerMotionVectors,
    DisocclusionMask,
    ResponsivityMask,
    Count,
};
static_assert(static_cast<std::size_t>(SubrectSlot::Count) == NR_DLSS_BRIDGE_RR_SUBRECT_COUNT);

void resetStatus(NrDlssBridgeStatus* status) noexcept
{
    if (status == nullptr || status->structSize < sizeof(NrDlssBridgeStatus))
        return;
    auto const size = status->structSize;
    *status = NrDlssBridgeStatus{};
    status->structSize = size;
}

uint32_t setStatus(
    NrDlssBridgeStatus* status,
    NrDlssBridgeStatusCode code,
    uint32_t nativeCode,
    const char* format,
    ...) noexcept
{
    if (status != nullptr && status->structSize >= sizeof(NrDlssBridgeStatus))
    {
        auto const size = status->structSize;
        *status = NrDlssBridgeStatus{};
        status->structSize = size;
        status->code = static_cast<uint32_t>(code);
        status->nativeCode = nativeCode;
        if (format != nullptr)
        {
            va_list arguments;
            va_start(arguments, format);
            static_cast<void>(vsnprintf_s(
                status->message,
                NR_DLSS_BRIDGE_STATUS_MESSAGE_CAPACITY,
                _TRUNCATE,
                format,
                arguments));
            va_end(arguments);
        }
    }
    return static_cast<uint32_t>(code);
}

uint32_t fromNgxResult(
    NVSDK_NGX_Result result,
    std::string_view operation,
    NrDlssBridgeStatus* status) noexcept
{
    if (NVSDK_NGX_SUCCEED(result))
    {
        resetStatus(status);
        return NR_DLSS_BRIDGE_STATUS_SUCCESS;
    }
    return setStatus(
        status,
        NR_DLSS_BRIDGE_STATUS_API_FAILURE,
        static_cast<uint32_t>(result),
        "%.*s failed with NGX result 0x%08x.",
        static_cast<int>(operation.size()),
        operation.data(),
        static_cast<uint32_t>(result));
}

bool validDimensions(NrDlssBridgeDimensions dimensions) noexcept
{
    return dimensions.width != 0u && dimensions.height != 0u;
}

bool utf8ToWide(
    const char* input,
    std::array<wchar_t, kMaximumWindowsPathLength>& output) noexcept
{
    if (input == nullptr || input[0] == '\0')
        return false;
    auto const length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        input,
        -1,
        output.data(),
        static_cast<int>(output.size()));
    return length > 0;
}

bool currentNgxDataPath(std::filesystem::path& output) noexcept
{
    auto error = std::error_code{};
    output = std::filesystem::current_path(error);
    if (error)
        return false;
    output /= L"ngx";
    std::filesystem::create_directories(output, error);
    return !error;
}

NVSDK_NGX_PerfQuality_Value toNativeQuality(uint32_t quality) noexcept
{
    switch (quality)
    {
    case NR_DLSS_BRIDGE_QUALITY_PERFORMANCE:
        return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case NR_DLSS_BRIDGE_QUALITY_BALANCED:
        return NVSDK_NGX_PerfQuality_Value_Balanced;
    case NR_DLSS_BRIDGE_QUALITY_QUALITY:
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case NR_DLSS_BRIDGE_QUALITY_ULTRA_PERFORMANCE:
        return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case NR_DLSS_BRIDGE_QUALITY_DLAA:
        return NVSDK_NGX_PerfQuality_Value_DLAA;
    default:
        return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    }
}

NVSDK_NGX_RayReconstruction_Hint_Render_Preset toNativePreset(uint32_t preset) noexcept
{
    switch (preset)
    {
    case NR_DLSS_BRIDGE_PRESET_D:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D;
    case NR_DLSS_BRIDGE_PRESET_E:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E;
    default:
        return NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default;
    }
}

int toNativeFlags(uint32_t flags) noexcept
{
    auto result = static_cast<int>(NVSDK_NGX_DLSS_Feature_Flags_None);
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_HDR) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_LOW_RESOLUTION) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_MVLowRes;
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_JITTERED) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_MVJittered;
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_DEPTH_INVERTED) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_DepthInverted;
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_AUTO_EXPOSURE) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    if ((flags & NR_DLSS_BRIDGE_CREATE_FLAG_ALPHA_UPSCALING) != 0u)
        result |= NVSDK_NGX_DLSS_Feature_Flags_AlphaUpscaling;
    return result;
}

NVSDK_NGX_Application_Identifier makeIdentifier() noexcept
{
    auto identifier = NVSDK_NGX_Application_Identifier{};
    identifier.IdentifierType = NVSDK_NGX_Application_Identifier_Type_Project_Id;
    identifier.v.ProjectDesc.ProjectId = kProjectId.data();
    identifier.v.ProjectDesc.EngineType = NVSDK_NGX_ENGINE_TYPE_CUSTOM;
    identifier.v.ProjectDesc.EngineVersion = kEngineVersion.data();
    return identifier;
}

NVSDK_NGX_FeatureDiscoveryInfo makeDiscoveryInfo(const std::filesystem::path& dataPath) noexcept
{
    auto result = NVSDK_NGX_FeatureDiscoveryInfo{};
    result.SDKVersion = NVSDK_NGX_Version_API;
    result.FeatureID = NVSDK_NGX_Feature_RayReconstruction;
    result.Identifier = makeIdentifier();
    result.ApplicationDataPath = dataPath.c_str();
    return result;
}

NVSDK_NGX_Resource_VK makeNativeImage(const NrDlssBridgeVulkanImage& image) noexcept
{
    auto result = NVSDK_NGX_Resource_VK{};
    result.Resource.ImageViewInfo.ImageView = image.view;
    result.Resource.ImageViewInfo.Image = image.image;
    result.Resource.ImageViewInfo.SubresourceRange = image.subresourceRange;
    result.Resource.ImageViewInfo.Format = image.format;
    result.Resource.ImageViewInfo.Width = image.extent.width;
    result.Resource.ImageViewInfo.Height = image.extent.height;
    result.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    result.ReadWrite = image.readWrite != 0u;
    return result;
}

NVSDK_NGX_Coordinates toNativeCoordinates(NrDlssBridgeCoordinates value) noexcept
{
    return NVSDK_NGX_Coordinates{value.x, value.y};
}

template<typename Query>
uint32_t queryExtensions(
    uint32_t* count,
    NrDlssBridgeExtensionName* names,
    NrDlssBridgeStatus* status,
    Query&& query) noexcept
{
    if (count == nullptr)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "Extension count is required.");

    auto required = uint32_t{0u};
    auto* properties = static_cast<VkExtensionProperties*>(nullptr);
    auto const result = query(&required, &properties);
    if (NVSDK_NGX_FAILED(result))
        return fromNgxResult(result, "NGX extension discovery", status);
    if (required != 0u && properties == nullptr)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_API_FAILURE, 0u, "NGX returned no extension array.");

    if (names == nullptr)
    {
        *count = required;
        resetStatus(status);
        return NR_DLSS_BRIDGE_STATUS_SUCCESS;
    }
    auto const capacity = *count;
    *count = required;
    if (capacity < required)
    {
        return setStatus(
            status,
            NR_DLSS_BRIDGE_STATUS_BUFFER_TOO_SMALL,
            0u,
            "Extension output capacity %u is smaller than required count %u.",
            capacity,
            required);
    }

    auto const indices = std::views::iota(uint32_t{0u}, required);
    std::ranges::for_each(indices, [&](uint32_t index) {
        static_cast<void>(strncpy_s(
            names[index].value,
            NR_DLSS_BRIDGE_EXTENSION_NAME_CAPACITY,
            properties[index].extensionName,
            _TRUNCATE));
    });
    resetStatus(status);
    return NR_DLSS_BRIDGE_STATUS_SUCCESS;
}
} // namespace

struct NrDlssBridgeContext
{
    VkDevice device = VK_NULL_HANDLE;
    NVSDK_NGX_Parameter* capabilityParameters = nullptr;
    bool available = false;
};

struct NrDlssBridgeFeature
{
    NVSDK_NGX_Handle* handle = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
};

namespace
{
uint32_t NR_DLSS_BRIDGE_CALL getInstanceExtensions(
    uint32_t* count,
    NrDlssBridgeExtensionName* names,
    NrDlssBridgeStatus* status) noexcept
{
    auto dataPath = std::filesystem::path{};
    if (!currentNgxDataPath(dataPath))
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_API_FAILURE, GetLastError(), "Could not create the NGX data directory.");
    auto discovery = makeDiscoveryInfo(dataPath);
    std::scoped_lock lock(ngxMutex);
    return queryExtensions(count, names, status, [&](uint32_t* nativeCount, VkExtensionProperties** properties) {
        return NVSDK_NGX_VULKAN_GetFeatureInstanceExtensionRequirements(
            &discovery,
            nativeCount,
            properties);
    });
}

uint32_t NR_DLSS_BRIDGE_CALL getDeviceExtensions(
    VkInstance instance,
    VkPhysicalDevice physicalDevice,
    uint32_t* count,
    NrDlssBridgeExtensionName* names,
    NrDlssBridgeStatus* status) noexcept
{
    if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "Vulkan instance and physical device are required.");
    auto dataPath = std::filesystem::path{};
    if (!currentNgxDataPath(dataPath))
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_API_FAILURE, GetLastError(), "Could not create the NGX data directory.");
    auto discovery = makeDiscoveryInfo(dataPath);
    std::scoped_lock lock(ngxMutex);
    return queryExtensions(count, names, status, [&](uint32_t* nativeCount, VkExtensionProperties** properties) {
        return NVSDK_NGX_VULKAN_GetFeatureDeviceExtensionRequirements(
            instance,
            physicalDevice,
            &discovery,
            nativeCount,
            properties);
    });
}

void destroyContextUnlocked(NrDlssBridgeContext* context) noexcept
{
    if (context->capabilityParameters != nullptr)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(context->capabilityParameters);
        context->capabilityParameters = nullptr;
    }
    if (context->device != VK_NULL_HANDLE)
    {
        NVSDK_NGX_VULKAN_Shutdown1(context->device);
        context->device = VK_NULL_HANDLE;
    }
    delete context;
}

uint32_t NR_DLSS_BRIDGE_CALL createContext(
    const NrDlssBridgeContextDesc* desc,
    NrDlssBridgeContext** output,
    NrDlssBridgeStatus* status) noexcept
{
    if (output != nullptr)
        *output = nullptr;
    if (desc == nullptr || desc->structSize < sizeof(NrDlssBridgeContextDesc) || output == nullptr ||
        desc->instance == VK_NULL_HANDLE || desc->physicalDevice == VK_NULL_HANDLE || desc->device == VK_NULL_HANDLE ||
        desc->applicationDataPathUtf8 == nullptr)
    {
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "A complete DLSS Vulkan context descriptor is required.");
    }

    auto dataPath = std::array<wchar_t, kMaximumWindowsPathLength>{};
    if (!utf8ToWide(desc->applicationDataPathUtf8, dataPath))
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, GetLastError(), "The NGX data path is not valid UTF-8.");

    auto* context = new (std::nothrow) NrDlssBridgeContext{};
    if (context == nullptr)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_OUT_OF_MEMORY, 0u, "Could not allocate the DLSS context.");

    std::scoped_lock lock(ngxMutex);
    context->device = desc->device;
    auto const initResult = NVSDK_NGX_VULKAN_Init_with_ProjectID(
        kProjectId.data(),
        NVSDK_NGX_ENGINE_TYPE_CUSTOM,
        kEngineVersion.data(),
        dataPath.data(),
        desc->instance,
        desc->physicalDevice,
        context->device,
        nullptr,
        nullptr,
        nullptr,
        NVSDK_NGX_Version_API);
    auto result = fromNgxResult(initResult, "NVSDK_NGX_VULKAN_Init_with_ProjectID", status);
    if (result != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        context->device = VK_NULL_HANDLE;
        delete context;
        return result;
    }

    auto const parametersResult = NVSDK_NGX_VULKAN_GetCapabilityParameters(&context->capabilityParameters);
    result = fromNgxResult(parametersResult, "NVSDK_NGX_VULKAN_GetCapabilityParameters", status);
    if (result != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        destroyContextUnlocked(context);
        return result;
    }

    auto available = 0;
    auto const availabilityResult = NVSDK_NGX_Parameter_GetI(
        context->capabilityParameters,
        NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,
        &available);
    result = fromNgxResult(availabilityResult, "DLSS Ray Reconstruction capability query", status);
    if (result != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        destroyContextUnlocked(context);
        return result;
    }

    context->available = available != 0;
    *output = context;
    if (!context->available)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_UNAVAILABLE, 0u, "NGX reports that DLSS Ray Reconstruction is unavailable on this system.");
    resetStatus(status);
    return NR_DLSS_BRIDGE_STATUS_SUCCESS;
}

void NR_DLSS_BRIDGE_CALL destroyContext(NrDlssBridgeContext* context) noexcept
{
    if (context == nullptr)
        return;
    std::scoped_lock lock(ngxMutex);
    destroyContextUnlocked(context);
}

uint32_t NR_DLSS_BRIDGE_CALL contextAvailable(const NrDlssBridgeContext* context) noexcept
{
    return context != nullptr && context->available ? 1u : 0u;
}

uint32_t NR_DLSS_BRIDGE_CALL getOptimalSettings(
    NrDlssBridgeContext* context,
    NrDlssBridgeDimensions targetSize,
    uint32_t quality,
    NrDlssBridgeOptimalSettings* settings,
    NrDlssBridgeStatus* status) noexcept
{
    if (context == nullptr || !context->available || !validDimensions(targetSize) ||
        quality >= NR_DLSS_BRIDGE_RR_QUALITY_COUNT || settings == nullptr ||
        settings->structSize < sizeof(NrDlssBridgeOptimalSettings))
    {
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "Valid context, dimensions, quality, and output settings are required.");
    }

    auto output = NrDlssBridgeOptimalSettings{};
    output.structSize = sizeof(output);
    auto ignoredSharpness = 0.0f;
    std::scoped_lock lock(ngxMutex);
    auto const result = NGX_DLSSD_GET_OPTIMAL_SETTINGS(
        context->capabilityParameters,
        targetSize.width,
        targetSize.height,
        toNativeQuality(quality),
        &output.optimalRenderSize.width,
        &output.optimalRenderSize.height,
        &output.maximumRenderSize.width,
        &output.maximumRenderSize.height,
        &output.minimumRenderSize.width,
        &output.minimumRenderSize.height,
        &ignoredSharpness);
    auto const code = fromNgxResult(result, "NGX_DLSSD_GET_OPTIMAL_SETTINGS", status);
    if (code == NR_DLSS_BRIDGE_STATUS_SUCCESS)
        *settings = output;
    return code;
}

void setPresets(
    NVSDK_NGX_Parameter& parameters,
    const NrDlssBridgeRayReconstructionCreateDesc& desc) noexcept
{
    auto set = [&](uint32_t quality, const char* parameterName) {
        NVSDK_NGX_Parameter_SetI(
            &parameters,
            parameterName,
            static_cast<int>(toNativePreset(desc.presets[quality])));
    };
    set(NR_DLSS_BRIDGE_QUALITY_DLAA, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA);
    set(NR_DLSS_BRIDGE_QUALITY_QUALITY, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality);
    set(NR_DLSS_BRIDGE_QUALITY_BALANCED, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced);
    set(NR_DLSS_BRIDGE_QUALITY_PERFORMANCE, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance);
    set(NR_DLSS_BRIDGE_QUALITY_ULTRA_PERFORMANCE, NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance);
}

uint32_t NR_DLSS_BRIDGE_CALL createRayReconstruction(
    NrDlssBridgeContext* context,
    VkCommandBuffer commandBuffer,
    const NrDlssBridgeRayReconstructionCreateDesc* desc,
    NrDlssBridgeFeature** output,
    NrDlssBridgeStatus* status) noexcept
{
    if (output != nullptr)
        *output = nullptr;
    if (context == nullptr || !context->available || commandBuffer == VK_NULL_HANDLE || desc == nullptr ||
        desc->structSize < sizeof(NrDlssBridgeRayReconstructionCreateDesc) || output == nullptr ||
        !validDimensions(desc->renderSize) || !validDimensions(desc->targetSize) ||
        desc->quality >= NR_DLSS_BRIDGE_RR_QUALITY_COUNT)
    {
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "A valid DLSS RR creation descriptor is required.");
    }

    auto* feature = new (std::nothrow) NrDlssBridgeFeature{};
    if (feature == nullptr)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_OUT_OF_MEMORY, 0u, "Could not allocate the DLSS RR feature.");

    std::scoped_lock lock(ngxMutex);
    auto const parametersResult = NVSDK_NGX_VULKAN_GetCapabilityParameters(&feature->parameters);
    auto result = fromNgxResult(parametersResult, "NVSDK_NGX_VULKAN_GetCapabilityParameters for RR feature", status);
    if (result != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        delete feature;
        return result;
    }
    setPresets(*feature->parameters, *desc);

    auto native = NVSDK_NGX_DLSSD_Create_Params{};
    native.InDenoiseMode = NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
    native.InRoughnessMode = desc->roughnessMode == NR_DLSS_BRIDGE_ROUGHNESS_PACKED
                                 ? NVSDK_NGX_DLSS_Roughness_Mode_Packed
                                 : NVSDK_NGX_DLSS_Roughness_Mode_Unpacked;
    native.InUseHWDepth = desc->depthType == NR_DLSS_BRIDGE_DEPTH_HARDWARE
                              ? NVSDK_NGX_DLSS_Depth_Type_HW
                              : NVSDK_NGX_DLSS_Depth_Type_Linear;
    native.InWidth = desc->renderSize.width;
    native.InHeight = desc->renderSize.height;
    native.InTargetWidth = desc->targetSize.width;
    native.InTargetHeight = desc->targetSize.height;
    native.InPerfQualityValue = toNativeQuality(desc->quality);
    native.InFeatureCreateFlags = toNativeFlags(desc->createFlags);
    native.InEnableOutputSubrects = desc->enableOutputSubrects != 0u;

    auto const createResult = NGX_VULKAN_CREATE_DLSSD_EXT1(
        context->device,
        commandBuffer,
        1u,
        1u,
        &feature->handle,
        feature->parameters,
        &native);
    result = fromNgxResult(createResult, "NGX_VULKAN_CREATE_DLSSD_EXT1", status);
    if (result != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(feature->parameters);
        feature->parameters = nullptr;
        delete feature;
        return result;
    }
    *output = feature;
    return NR_DLSS_BRIDGE_STATUS_SUCCESS;
}

void NR_DLSS_BRIDGE_CALL destroyFeature(NrDlssBridgeFeature* feature) noexcept
{
    if (feature == nullptr)
        return;
    std::scoped_lock lock(ngxMutex);
    if (feature->handle != nullptr)
    {
        NVSDK_NGX_VULKAN_ReleaseFeature(feature->handle);
        feature->handle = nullptr;
    }
    if (feature->parameters != nullptr)
    {
        NVSDK_NGX_VULKAN_DestroyParameters(feature->parameters);
        feature->parameters = nullptr;
    }
    delete feature;
}

uint32_t NR_DLSS_BRIDGE_CALL evaluateRayReconstruction(
    NrDlssBridgeFeature* feature,
    VkCommandBuffer commandBuffer,
    const NrDlssBridgeRayReconstructionEvalDesc* desc,
    NrDlssBridgeStatus* status) noexcept
{
    if (feature == nullptr || feature->handle == nullptr || feature->parameters == nullptr ||
        commandBuffer == VK_NULL_HANDLE || desc == nullptr ||
        desc->structSize < sizeof(NrDlssBridgeRayReconstructionEvalDesc))
    {
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "A valid DLSS RR evaluation descriptor is required.");
    }

    auto nativeResources = std::array<NVSDK_NGX_Resource_VK, NR_DLSS_BRIDGE_RR_RESOURCE_COUNT>{};
    auto resourcePresent = std::array<bool, NR_DLSS_BRIDGE_RR_RESOURCE_COUNT>{};
    auto const indices = std::views::iota(std::size_t{0u}, std::size_t{NR_DLSS_BRIDGE_RR_RESOURCE_COUNT});
    auto invalidResource = false;
    std::ranges::for_each(indices, [&](std::size_t index) {
        auto const& resource = desc->resources[index];
        if (resource.present == 0u)
            return;
        if (resource.structSize < sizeof(NrDlssBridgeVulkanImage) || resource.image == VK_NULL_HANDLE ||
            resource.view == VK_NULL_HANDLE || resource.format == VK_FORMAT_UNDEFINED || !validDimensions(resource.extent))
        {
            invalidResource = true;
            return;
        }
        nativeResources[index] = makeNativeImage(resource);
        resourcePresent[index] = true;
    });
    if (invalidResource)
        return setStatus(status, NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT, 0u, "A present DLSS RR image descriptor is invalid.");

    auto image = [&](ResourceSlot slot) -> NVSDK_NGX_Resource_VK* {
        auto const index = static_cast<std::size_t>(slot);
        return resourcePresent[index] ? &nativeResources[index] : nullptr;
    };
    auto subrect = [&](SubrectSlot slot) {
        return toNativeCoordinates(desc->subrectBases[static_cast<std::size_t>(slot)]);
    };

    auto native = NVSDK_NGX_VK_DLSSD_Eval_Params{};
    native.pInDiffuseAlbedo = image(ResourceSlot::DiffuseAlbedo);
    native.pInSpecularAlbedo = image(ResourceSlot::SpecularAlbedo);
    native.pInNormals = image(ResourceSlot::Normals);
    native.pInRoughness = image(ResourceSlot::Roughness);
    native.pInColor = image(ResourceSlot::Color);
    native.pInAlpha = image(ResourceSlot::Alpha);
    native.pInOutput = image(ResourceSlot::Output);
    native.pInOutputAlpha = image(ResourceSlot::OutputAlpha);
    native.pInDepth = image(ResourceSlot::Depth);
    native.pInMotionVectors = image(ResourceSlot::MotionVectors);
    native.InJitterOffsetX = desc->jitterOffset[0];
    native.InJitterOffsetY = desc->jitterOffset[1];
    native.InRenderSubrectDimensions = {desc->renderSubrectDimensions.width, desc->renderSubrectDimensions.height};
    native.InReset = desc->reset != 0u ? 1 : 0;
    native.InMVScaleX = desc->motionVectorScale[0];
    native.InMVScaleY = desc->motionVectorScale[1];
    native.pInTransparencyMask = image(ResourceSlot::TransparencyMask);
    native.pInExposureTexture = image(ResourceSlot::ExposureTexture);
    native.pInBiasCurrentColorMask = image(ResourceSlot::BiasCurrentColorMask);
    native.InAlphaSubrectBase = subrect(SubrectSlot::Alpha);
    native.InOutputAlphaSubrectBase = subrect(SubrectSlot::OutputAlpha);
    native.InDiffuseAlbedoSubrectBase = subrect(SubrectSlot::DiffuseAlbedo);
    native.InSpecularAlbedoSubrectBase = subrect(SubrectSlot::SpecularAlbedo);
    native.InNormalsSubrectBase = subrect(SubrectSlot::Normals);
    native.InRoughnessSubrectBase = subrect(SubrectSlot::Roughness);
    native.InColorSubrectBase = subrect(SubrectSlot::Color);
    native.InDepthSubrectBase = subrect(SubrectSlot::Depth);
    native.InMVSubrectBase = subrect(SubrectSlot::MotionVectors);
    native.InTranslucencySubrectBase = subrect(SubrectSlot::Translucency);
    native.InBiasCurrentColorSubrectBase = subrect(SubrectSlot::BiasCurrentColor);
    native.InOutputSubrectBase = subrect(SubrectSlot::Output);
    native.InPreExposure = desc->preExposure;
    native.InExposureScale = desc->exposureScale;
    native.InIndicatorInvertXAxis = desc->indicatorInvertXAxis != 0u ? 1 : 0;
    native.InIndicatorInvertYAxis = desc->indicatorInvertYAxis != 0u ? 1 : 0;

#define NR_DLSS_ASSIGN_RESOURCE(field, slot) native.field = image(ResourceSlot::slot)
#define NR_DLSS_ASSIGN_SUBRECT(field, slot) native.field = subrect(SubrectSlot::slot)
    NR_DLSS_ASSIGN_RESOURCE(pInReflectedAlbedo, ReflectedAlbedo);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeParticles, ColorBeforeParticles);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterParticles, ColorAfterParticles);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeTransparency, ColorBeforeTransparency);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterTransparency, ColorAfterTransparency);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeFog, ColorBeforeFog);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterFog, ColorAfterFog);
    NR_DLSS_ASSIGN_RESOURCE(pInScreenSpaceSubsurfaceScatteringGuide, ScreenSpaceSubsurfaceScatteringGuide);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeScreenSpaceSubsurfaceScattering, ColorBeforeScreenSpaceSubsurfaceScattering);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterScreenSpaceSubsurfaceScattering, ColorAfterScreenSpaceSubsurfaceScattering);
    NR_DLSS_ASSIGN_RESOURCE(pInScreenSpaceRefractionGuide, ScreenSpaceRefractionGuide);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeScreenSpaceRefraction, ColorBeforeScreenSpaceRefraction);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterScreenSpaceRefraction, ColorAfterScreenSpaceRefraction);
    NR_DLSS_ASSIGN_RESOURCE(pInDepthOfFieldGuide, DepthOfFieldGuide);
    NR_DLSS_ASSIGN_RESOURCE(pInColorBeforeDepthOfField, ColorBeforeDepthOfField);
    NR_DLSS_ASSIGN_RESOURCE(pInColorAfterDepthOfField, ColorAfterDepthOfField);
    NR_DLSS_ASSIGN_RESOURCE(pInDiffuseHitDistance, DiffuseHitDistance);
    NR_DLSS_ASSIGN_RESOURCE(pInSpecularHitDistance, SpecularHitDistance);
    NR_DLSS_ASSIGN_RESOURCE(pInDiffuseRayDirection, DiffuseRayDirection);
    NR_DLSS_ASSIGN_RESOURCE(pInSpecularRayDirection, SpecularRayDirection);
    NR_DLSS_ASSIGN_RESOURCE(pInDiffuseRayDirectionHitDistance, DiffuseRayDirectionHitDistance);
    NR_DLSS_ASSIGN_RESOURCE(pInSpecularRayDirectionHitDistance, SpecularRayDirectionHitDistance);
    NR_DLSS_ASSIGN_SUBRECT(InReflectedAlbedoSubrectBase, ReflectedAlbedo);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeParticlesSubrectBase, ColorBeforeParticles);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterParticlesSubrectBase, ColorAfterParticles);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeTransparencySubrectBase, ColorBeforeTransparency);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterTransparencySubrectBase, ColorAfterTransparency);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeFogSubrectBase, ColorBeforeFog);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterFogSubrectBase, ColorAfterFog);
    NR_DLSS_ASSIGN_SUBRECT(InScreenSpaceSubsurfaceScatteringGuideSubrectBase, ScreenSpaceSubsurfaceScatteringGuide);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeScreenSpaceSubsurfaceScatteringSubrectBase, ColorBeforeScreenSpaceSubsurfaceScattering);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterScreenSpaceSubsurfaceScatteringSubrectBase, ColorAfterScreenSpaceSubsurfaceScattering);
    NR_DLSS_ASSIGN_SUBRECT(InScreenSpaceRefractionGuideSubrectBase, ScreenSpaceRefractionGuide);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeScreenSpaceRefractionSubrectBase, ColorBeforeScreenSpaceRefraction);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterScreenSpaceRefractionSubrectBase, ColorAfterScreenSpaceRefraction);
    NR_DLSS_ASSIGN_SUBRECT(InDepthOfFieldGuideSubrectBase, DepthOfFieldGuide);
    NR_DLSS_ASSIGN_SUBRECT(InColorBeforeDepthOfFieldSubrectBase, ColorBeforeDepthOfField);
    NR_DLSS_ASSIGN_SUBRECT(InColorAfterDepthOfFieldSubrectBase, ColorAfterDepthOfField);
    NR_DLSS_ASSIGN_SUBRECT(InDiffuseHitDistanceSubrectBase, DiffuseHitDistance);
    NR_DLSS_ASSIGN_SUBRECT(InSpecularHitDistanceSubrectBase, SpecularHitDistance);
    NR_DLSS_ASSIGN_SUBRECT(InDiffuseRayDirectionSubrectBase, DiffuseRayDirection);
    NR_DLSS_ASSIGN_SUBRECT(InSpecularRayDirectionSubrectBase, SpecularRayDirection);
    NR_DLSS_ASSIGN_SUBRECT(InDiffuseRayDirectionHitDistanceSubrectBase, DiffuseRayDirectionHitDistance);
    NR_DLSS_ASSIGN_SUBRECT(InSpecularRayDirectionHitDistanceSubrectBase, SpecularRayDirectionHitDistance);

    auto const gBufferFirst = static_cast<std::size_t>(ResourceSlot::GBufferAlbedo);
    auto const gBufferIndices = std::views::iota(std::size_t{0u}, std::size_t{17u});
    std::ranges::for_each(gBufferIndices, [&](std::size_t index) {
        native.GBufferSurface.pInAttrib[index] = image(static_cast<ResourceSlot>(gBufferFirst + index));
    });
    native.pInWorldToViewMatrix = desc->hasWorldToView != 0u
                                      ? const_cast<float*>(desc->worldToViewRowMajor)
                                      : nullptr;
    native.pInViewToClipMatrix = desc->hasViewToClip != 0u
                                     ? const_cast<float*>(desc->viewToClipRowMajor)
                                     : nullptr;
    native.InToneMapperType = static_cast<NVSDK_NGX_ToneMapperType>(desc->toneMapper);
    NR_DLSS_ASSIGN_RESOURCE(pInMotionVectors3D, MotionVectors3D);
    NR_DLSS_ASSIGN_RESOURCE(pInIsParticleMask, ParticleMask);
    NR_DLSS_ASSIGN_RESOURCE(pInAnimatedTextureMask, AnimatedTextureMask);
    NR_DLSS_ASSIGN_RESOURCE(pInDepthHighRes, DepthHighResolution);
    NR_DLSS_ASSIGN_RESOURCE(pInPositionViewSpace, PositionViewSpace);
    native.InFrameTimeDeltaInMsec = desc->frameTimeDeltaMilliseconds;
    NR_DLSS_ASSIGN_RESOURCE(pInRayTracingHitDistance, RayTracingHitDistance);
    auto* explicitReflectionMotionVectors = image(ResourceSlot::ReflectionMotionVectors);
    auto* gBufferSpecularMotionVectors = image(ResourceSlot::GBufferSpecularMotionVectors);
    native.pInMotionVectorsReflections = explicitReflectionMotionVectors != nullptr
                                             ? explicitReflectionMotionVectors
                                             : gBufferSpecularMotionVectors;
    NR_DLSS_ASSIGN_RESOURCE(pInTransparencyLayer, TransparencyLayer);
    NR_DLSS_ASSIGN_SUBRECT(InTransparencyLayerSubrectBase, TransparencyLayer);
    NR_DLSS_ASSIGN_RESOURCE(pInTransparencyLayerOpacity, TransparencyLayerOpacity);
    NR_DLSS_ASSIGN_SUBRECT(InTransparencyLayerOpacitySubrectBase, TransparencyLayerOpacity);
    NR_DLSS_ASSIGN_RESOURCE(pInTransparencyLayerMvecs, TransparencyLayerMotionVectors);
    NR_DLSS_ASSIGN_SUBRECT(InTransparencyLayerMvecsSubrectBase, TransparencyLayerMotionVectors);
    NR_DLSS_ASSIGN_RESOURCE(pInDisocclusionMask, DisocclusionMask);
    NR_DLSS_ASSIGN_SUBRECT(InDisocclusionMaskSubrectBase, DisocclusionMask);
    NR_DLSS_ASSIGN_RESOURCE(pInResponsivityMask, ResponsivityMask);
    NR_DLSS_ASSIGN_SUBRECT(InResponsivityMaskSubrectBase, ResponsivityMask);
#undef NR_DLSS_ASSIGN_SUBRECT
#undef NR_DLSS_ASSIGN_RESOURCE

    std::scoped_lock lock(ngxMutex);
    NVSDK_NGX_Parameter_SetVoidPointer(
        feature->parameters,
        "GBuffer.Attrib.16",
        native.GBufferSurface.pInAttrib[16]);
    auto const result = NGX_VULKAN_EVALUATE_DLSSD_EXT(
        commandBuffer,
        feature->handle,
        feature->parameters,
        &native);
    return fromNgxResult(result, "NGX_VULKAN_EVALUATE_DLSSD_EXT", status);
}
} // namespace

extern "C" NR_DLSS_BRIDGE_API uint32_t NR_DLSS_BRIDGE_CALL nrDlssBridgeGetApi(
    uint32_t requestedAbiVersion,
    NrDlssBridgeApi* api)
{
    if (requestedAbiVersion != NR_DLSS_BRIDGE_ABI_VERSION || api == nullptr ||
        api->structSize < sizeof(NrDlssBridgeApi))
    {
        return NR_DLSS_BRIDGE_STATUS_INCOMPATIBLE_ABI;
    }

    *api = NrDlssBridgeApi{
        .structSize = sizeof(NrDlssBridgeApi),
        .abiVersion = NR_DLSS_BRIDGE_ABI_VERSION,
        .ngxSdkVersion = NR_DLSS_BRIDGE_NGX_SDK_VERSION,
        .getInstanceExtensions = &getInstanceExtensions,
        .getDeviceExtensions = &getDeviceExtensions,
        .createContext = &createContext,
        .destroyContext = &destroyContext,
        .contextAvailable = &contextAvailable,
        .getOptimalSettings = &getOptimalSettings,
        .createRayReconstruction = &createRayReconstruction,
        .destroyFeature = &destroyFeature,
        .evaluateRayReconstruction = &evaluateRayReconstruction,
    };
    return NR_DLSS_BRIDGE_STATUS_SUCCESS;
}
