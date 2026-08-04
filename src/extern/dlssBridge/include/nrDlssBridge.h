#ifndef NR_DLSS_BRIDGE_H
#define NR_DLSS_BRIDGE_H

#include <stdint.h>
#include <vulkan/vulkan.h>

#if defined(_WIN32)
#define NR_DLSS_BRIDGE_CALL __cdecl
#if defined(NR_DLSS_BRIDGE_BUILD)
#define NR_DLSS_BRIDGE_API __declspec(dllexport)
#else
#define NR_DLSS_BRIDGE_API
#endif
#else
#define NR_DLSS_BRIDGE_CALL
#define NR_DLSS_BRIDGE_API
#endif

#ifdef __cplusplus
extern "C"
{
#endif

#define NR_DLSS_BRIDGE_ABI_VERSION 1u
#define NR_DLSS_BRIDGE_NGX_SDK_VERSION 3100700u
#define NR_DLSS_BRIDGE_STATUS_MESSAGE_CAPACITY 512u
#define NR_DLSS_BRIDGE_EXTENSION_NAME_CAPACITY 256u
#define NR_DLSS_BRIDGE_RR_QUALITY_COUNT 5u
#define NR_DLSS_BRIDGE_RR_RESOURCE_COUNT 64u
#define NR_DLSS_BRIDGE_RR_SUBRECT_COUNT 39u

    typedef struct NrDlssBridgeContext NrDlssBridgeContext;
    typedef struct NrDlssBridgeFeature NrDlssBridgeFeature;

    typedef enum NrDlssBridgeStatusCode
    {
        NR_DLSS_BRIDGE_STATUS_SUCCESS = 0,
        NR_DLSS_BRIDGE_STATUS_UNAVAILABLE = 1,
        NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT = 2,
        NR_DLSS_BRIDGE_STATUS_API_FAILURE = 3,
        NR_DLSS_BRIDGE_STATUS_INCOMPATIBLE_ABI = 4,
        NR_DLSS_BRIDGE_STATUS_BUFFER_TOO_SMALL = 5,
        NR_DLSS_BRIDGE_STATUS_OUT_OF_MEMORY = 6,
    } NrDlssBridgeStatusCode;

    typedef enum NrDlssBridgeQuality
    {
        NR_DLSS_BRIDGE_QUALITY_PERFORMANCE = 0,
        NR_DLSS_BRIDGE_QUALITY_BALANCED = 1,
        NR_DLSS_BRIDGE_QUALITY_QUALITY = 2,
        NR_DLSS_BRIDGE_QUALITY_ULTRA_PERFORMANCE = 3,
        NR_DLSS_BRIDGE_QUALITY_DLAA = 4,
    } NrDlssBridgeQuality;

    typedef enum NrDlssBridgePreset
    {
        NR_DLSS_BRIDGE_PRESET_DEFAULT = 0,
        NR_DLSS_BRIDGE_PRESET_D = 1,
        NR_DLSS_BRIDGE_PRESET_E = 2,
    } NrDlssBridgePreset;

    typedef enum NrDlssBridgeRoughnessMode
    {
        NR_DLSS_BRIDGE_ROUGHNESS_UNPACKED = 0,
        NR_DLSS_BRIDGE_ROUGHNESS_PACKED = 1,
    } NrDlssBridgeRoughnessMode;

    typedef enum NrDlssBridgeDepthType
    {
        NR_DLSS_BRIDGE_DEPTH_LINEAR = 0,
        NR_DLSS_BRIDGE_DEPTH_HARDWARE = 1,
    } NrDlssBridgeDepthType;

    typedef enum NrDlssBridgeToneMapper
    {
        NR_DLSS_BRIDGE_TONE_MAPPER_STRING = 0,
        NR_DLSS_BRIDGE_TONE_MAPPER_REINHARD = 1,
        NR_DLSS_BRIDGE_TONE_MAPPER_ONE_OVER_LUMA = 2,
        NR_DLSS_BRIDGE_TONE_MAPPER_ACES = 3,
    } NrDlssBridgeToneMapper;

    typedef enum NrDlssBridgeCreateFlagBits
    {
        NR_DLSS_BRIDGE_CREATE_FLAG_HDR = 1u << 0u,
        NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_LOW_RESOLUTION = 1u << 1u,
        NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_JITTERED = 1u << 2u,
        NR_DLSS_BRIDGE_CREATE_FLAG_DEPTH_INVERTED = 1u << 3u,
        NR_DLSS_BRIDGE_CREATE_FLAG_AUTO_EXPOSURE = 1u << 4u,
        NR_DLSS_BRIDGE_CREATE_FLAG_ALPHA_UPSCALING = 1u << 5u,
    } NrDlssBridgeCreateFlagBits;

    typedef struct NrDlssBridgeStatus
    {
        uint32_t structSize;
        uint32_t code;
        uint32_t nativeCode;
        char message[NR_DLSS_BRIDGE_STATUS_MESSAGE_CAPACITY];
    } NrDlssBridgeStatus;

    typedef struct NrDlssBridgeDimensions
    {
        uint32_t width;
        uint32_t height;
    } NrDlssBridgeDimensions;

    typedef struct NrDlssBridgeCoordinates
    {
        uint32_t x;
        uint32_t y;
    } NrDlssBridgeCoordinates;

    typedef struct NrDlssBridgeExtensionName
    {
        char value[NR_DLSS_BRIDGE_EXTENSION_NAME_CAPACITY];
    } NrDlssBridgeExtensionName;

    typedef struct NrDlssBridgeContextDesc
    {
        uint32_t structSize;
        VkInstance instance;
        VkPhysicalDevice physicalDevice;
        VkDevice device;
        const char *applicationDataPathUtf8;
    } NrDlssBridgeContextDesc;

    typedef struct NrDlssBridgeOptimalSettings
    {
        uint32_t structSize;
        NrDlssBridgeDimensions optimalRenderSize;
        NrDlssBridgeDimensions minimumRenderSize;
        NrDlssBridgeDimensions maximumRenderSize;
    } NrDlssBridgeOptimalSettings;

    typedef struct NrDlssBridgeRayReconstructionCreateDesc
    {
        uint32_t structSize;
        NrDlssBridgeDimensions renderSize;
        NrDlssBridgeDimensions targetSize;
        uint32_t quality;
        uint32_t presets[NR_DLSS_BRIDGE_RR_QUALITY_COUNT];
        uint32_t roughnessMode;
        uint32_t depthType;
        uint32_t createFlags;
        uint32_t enableOutputSubrects;
    } NrDlssBridgeRayReconstructionCreateDesc;

    typedef struct NrDlssBridgeVulkanImage
    {
        uint32_t structSize;
        uint32_t present;
        VkImage image;
        VkImageView view;
        VkImageSubresourceRange subresourceRange;
        VkFormat format;
        NrDlssBridgeDimensions extent;
        uint32_t readWrite;
    } NrDlssBridgeVulkanImage;

    typedef struct NrDlssBridgeRayReconstructionEvalDesc
    {
        uint32_t structSize;
        NrDlssBridgeVulkanImage resources[NR_DLSS_BRIDGE_RR_RESOURCE_COUNT];
        NrDlssBridgeCoordinates subrectBases[NR_DLSS_BRIDGE_RR_SUBRECT_COUNT];
        float jitterOffset[2];
        NrDlssBridgeDimensions renderSubrectDimensions;
        uint32_t reset;
        float motionVectorScale[2];
        float preExposure;
        float exposureScale;
        uint32_t indicatorInvertXAxis;
        uint32_t indicatorInvertYAxis;
        uint32_t hasWorldToView;
        float worldToViewRowMajor[16];
        uint32_t hasViewToClip;
        float viewToClipRowMajor[16];
        uint32_t toneMapper;
        float frameTimeDeltaMilliseconds;
    } NrDlssBridgeRayReconstructionEvalDesc;

    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeGetInstanceExtensionsFn)(uint32_t *count,
                                                                               NrDlssBridgeExtensionName *names,
                                                                               NrDlssBridgeStatus *status);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeGetDeviceExtensionsFn)(VkInstance instance,
                                                                             VkPhysicalDevice physicalDevice,
                                                                             uint32_t *count,
                                                                             NrDlssBridgeExtensionName *names,
                                                                             NrDlssBridgeStatus *status);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeCreateContextFn)(const NrDlssBridgeContextDesc *desc,
                                                                       NrDlssBridgeContext **context,
                                                                       NrDlssBridgeStatus *status);
    typedef void(NR_DLSS_BRIDGE_CALL *NrDlssBridgeDestroyContextFn)(NrDlssBridgeContext *context);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeContextAvailableFn)(const NrDlssBridgeContext *context);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeGetOptimalSettingsFn)(NrDlssBridgeContext *context,
                                                                            NrDlssBridgeDimensions targetSize,
                                                                            uint32_t quality,
                                                                            NrDlssBridgeOptimalSettings *settings,
                                                                            NrDlssBridgeStatus *status);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeCreateRayReconstructionFn)(
        NrDlssBridgeContext *context, VkCommandBuffer commandBuffer,
        const NrDlssBridgeRayReconstructionCreateDesc *desc, NrDlssBridgeFeature **feature, NrDlssBridgeStatus *status);
    typedef void(NR_DLSS_BRIDGE_CALL *NrDlssBridgeDestroyFeatureFn)(NrDlssBridgeFeature *feature);
    typedef uint32_t(NR_DLSS_BRIDGE_CALL *NrDlssBridgeEvaluateRayReconstructionFn)(
        NrDlssBridgeFeature *feature, VkCommandBuffer commandBuffer, const NrDlssBridgeRayReconstructionEvalDesc *desc,
        NrDlssBridgeStatus *status);

    typedef struct NrDlssBridgeApi
    {
        uint32_t structSize;
        uint32_t abiVersion;
        uint32_t ngxSdkVersion;
        NrDlssBridgeGetInstanceExtensionsFn getInstanceExtensions;
        NrDlssBridgeGetDeviceExtensionsFn getDeviceExtensions;
        NrDlssBridgeCreateContextFn createContext;
        NrDlssBridgeDestroyContextFn destroyContext;
        NrDlssBridgeContextAvailableFn contextAvailable;
        NrDlssBridgeGetOptimalSettingsFn getOptimalSettings;
        NrDlssBridgeCreateRayReconstructionFn createRayReconstruction;
        NrDlssBridgeDestroyFeatureFn destroyFeature;
        NrDlssBridgeEvaluateRayReconstructionFn evaluateRayReconstruction;
    } NrDlssBridgeApi;

    NR_DLSS_BRIDGE_API uint32_t NR_DLSS_BRIDGE_CALL nrDlssBridgeGetApi(uint32_t requestedAbiVersion,
                                                                       NrDlssBridgeApi *api);

#ifdef __cplusplus
}
#endif

#endif
