module;

#include <vulkan/vulkan.h>

#if NR_DLSS_BRIDGE_ENABLED
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "nrDlssBridge.h"
#endif

module dependency.dlss;

import std;

namespace nr::dependency::dlss
{
namespace
{
[[nodiscard]] Status bridgeNotAvailableStatus()
{
    return Status{
        .code = StatusCode::SdkNotCompiled,
        .message = "The DLSS bridge is not available in this process. Enable NR_ENABLE_DLSS_NGX_SDK and deploy "
                   "nr_dlss_bridge.dll.",
    };
}

#if NR_DLSS_BRIDGE_ENABLED
struct BridgeRuntime
{
    HMODULE module = nullptr;
    NrDlssBridgeApi api{};
    Status status = bridgeNotAvailableStatus();
};

[[nodiscard]] StatusCode fromBridgeCode(uint32_t code) noexcept
{
    switch (code)
    {
    case NR_DLSS_BRIDGE_STATUS_SUCCESS:
        return StatusCode::Success;
    case NR_DLSS_BRIDGE_STATUS_UNAVAILABLE:
        return StatusCode::Unavailable;
    case NR_DLSS_BRIDGE_STATUS_INVALID_ARGUMENT:
        return StatusCode::InvalidArgument;
    default:
        return StatusCode::ApiFailure;
    }
}

[[nodiscard]] Status fromBridgeStatus(uint32_t code, const NrDlssBridgeStatus &native)
{
    return Status{
        .code = fromBridgeCode(code),
        .nativeCode = native.nativeCode,
        .message = native.message[0] != '\0' ? std::string{native.message}
                                             : std::format("The DLSS bridge failed with status code {}.", code),
    };
}

[[nodiscard]] std::wstring bridgeAbsolutePath()
{
    auto executablePath = std::array<wchar_t, 32768u>{};
    auto const length = GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    if (length == 0u || length >= static_cast<DWORD>(executablePath.size()))
        return {};
    auto const view = std::wstring_view{executablePath.data(), length};
    auto const separator = view.find_last_of(L"\\/");
    if (separator == std::wstring_view::npos)
        return {};
    auto result = std::wstring{view.substr(0u, separator + 1u)};
    result += L"nr_dlss_bridge.dll";
    return result;
}

[[nodiscard]] BridgeRuntime initializeBridge()
{
    auto result = BridgeRuntime{};
    result.api.structSize = sizeof(NrDlssBridgeApi);
    auto const path = bridgeAbsolutePath();
    if (path.empty())
    {
        result.status = Status{
            .code = StatusCode::ApiFailure,
            .nativeCode = GetLastError(),
            .message = "Could not resolve the absolute executable directory for nr_dlss_bridge.dll.",
        };
        return result;
    }

    result.module =
        LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (result.module == nullptr)
    {
        auto const error = GetLastError();
        result.status = Status{
            .code = StatusCode::SdkNotCompiled,
            .nativeCode = error,
            .message = std::format("Could not load the deployed DLSS bridge (Windows error {}).", error),
        };
        return result;
    }

    auto const getApiAddress = GetProcAddress(result.module, "nrDlssBridgeGetApi");
    auto const getApi =
        getApiAddress != nullptr ? std::bit_cast<decltype(&nrDlssBridgeGetApi)>(getApiAddress) : nullptr;
    if (getApi == nullptr)
    {
        result.status = Status{
            .code = StatusCode::ApiFailure,
            .nativeCode = GetLastError(),
            .message = "The deployed DLSS bridge does not export nrDlssBridgeGetApi.",
        };
        FreeLibrary(result.module);
        result.module = nullptr;
        return result;
    }

    auto const code = getApi(NR_DLSS_BRIDGE_ABI_VERSION, &result.api);
    auto const functionsPresent =
        result.api.getInstanceExtensions != nullptr && result.api.getDeviceExtensions != nullptr &&
        result.api.createContext != nullptr && result.api.destroyContext != nullptr &&
        result.api.contextAvailable != nullptr && result.api.getOptimalSettings != nullptr &&
        result.api.createRayReconstruction != nullptr && result.api.destroyFeature != nullptr &&
        result.api.evaluateRayReconstruction != nullptr;
    if (code != NR_DLSS_BRIDGE_STATUS_SUCCESS || result.api.structSize < sizeof(NrDlssBridgeApi) ||
        result.api.abiVersion != NR_DLSS_BRIDGE_ABI_VERSION ||
        result.api.ngxSdkVersion != NR_DLSS_BRIDGE_NGX_SDK_VERSION || !functionsPresent)
    {
        result.status = Status{
            .code = StatusCode::ApiFailure,
            .nativeCode = code,
            .message = "The deployed DLSS bridge ABI, SDK version, or function table is incompatible.",
        };
        FreeLibrary(result.module);
        result.module = nullptr;
        return result;
    }

    result.status = {};
    // Keep the module loaded for the process lifetime. Every opaque object and
    // its allocator remain owned by the same MSVC runtime until NGX teardown.
    return result;
}

[[nodiscard]] BridgeRuntime &bridgeRuntime()
{
    static auto runtime = initializeBridge();
    return runtime;
}

[[nodiscard]] NrDlssBridgeStatus emptyBridgeStatus() noexcept
{
    auto result = NrDlssBridgeStatus{};
    result.structSize = sizeof(NrDlssBridgeStatus);
    return result;
}

[[nodiscard]] NrDlssBridgeQuality toBridge(Quality quality) noexcept
{
    return static_cast<NrDlssBridgeQuality>(quality);
}

[[nodiscard]] NrDlssBridgePreset toBridge(Preset preset) noexcept
{
    return static_cast<NrDlssBridgePreset>(preset);
}

[[nodiscard]] uint32_t toBridgeFlags(const RayReconstructionCreateFlags &flags) noexcept
{
    auto result = uint32_t{0u};
    if (flags.hdr)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_HDR;
    if (flags.motionVectorsLowResolution)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_LOW_RESOLUTION;
    if (flags.motionVectorsJittered)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_MOTION_VECTORS_JITTERED;
    if (flags.depthInverted)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_DEPTH_INVERTED;
    if (flags.autoExposure)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_AUTO_EXPOSURE;
    if (flags.alphaUpscaling)
        result |= NR_DLSS_BRIDGE_CREATE_FLAG_ALPHA_UPSCALING;
    return result;
}

[[nodiscard]] NrDlssBridgeVulkanImage toBridge(const VulkanImage &image) noexcept
{
    return NrDlssBridgeVulkanImage{
        .structSize = sizeof(NrDlssBridgeVulkanImage),
        .present = 1u,
        .image = static_cast<VkImage>(image.image),
        .view = static_cast<VkImageView>(image.view),
        .subresourceRange =
            VkImageSubresourceRange{static_cast<VkImageAspectFlags>(image.subresourceRange.aspectMask),
                                    image.subresourceRange.baseMipLevel, image.subresourceRange.levelCount,
                                    image.subresourceRange.baseArrayLayer, image.subresourceRange.layerCount},
        .format = static_cast<VkFormat>(image.format),
        .extent = NrDlssBridgeDimensions{image.extent.width, image.extent.height},
        .readWrite = image.readWrite ? 1u : 0u,
    };
}
#endif
} // namespace

struct Context::Impl
{
    Status status = bridgeNotAvailableStatus();
    bool available = false;
#if NR_DLSS_BRIDGE_ENABLED
    NrDlssBridgeContext *context = nullptr;

    ~Impl()
    {
        if (context != nullptr)
        {
            bridgeRuntime().api.destroyContext(context);
        }
    }
#endif
};

struct RayReconstructionFeature::Impl
{
    Status status = bridgeNotAvailableStatus();
#if NR_DLSS_BRIDGE_ENABLED
    NrDlssBridgeFeature *feature = nullptr;

    ~Impl()
    {
        if (feature != nullptr)
        {
            bridgeRuntime().api.destroyFeature(feature);
        }
    }
#endif
};

Context::Context(const VulkanContextDesc &desc) : impl_(std::make_unique<Impl>())
{
#if NR_DLSS_BRIDGE_ENABLED
    auto &runtime = bridgeRuntime();
    if (!runtime.status.success())
    {
        impl_->status = runtime.status;
        return;
    }
    if (!desc.instance || !desc.physicalDevice || !desc.device)
    {
        impl_->status = Status{
            .code = StatusCode::InvalidArgument,
            .message = "DLSS context requires valid Vulkan instance, physical-device, and device handles.",
        };
        return;
    }

    auto applicationDataPath = desc.applicationDataPath;
    auto pathError = std::error_code{};
    if (applicationDataPath.empty())
    {
        applicationDataPath = std::filesystem::current_path(pathError);
        applicationDataPath /= "ngx";
    }
    if (!pathError)
        std::filesystem::create_directories(applicationDataPath, pathError);
    if (pathError)
    {
        impl_->status = Status{
            .code = StatusCode::ApiFailure,
            .message = std::format("DLSS NGX application-data path preparation failed: {}", pathError.message()),
        };
        return;
    }

    auto const pathUtf8 = applicationDataPath.u8string();
    auto const pathBytes = std::string{reinterpret_cast<const char *>(pathUtf8.data()), pathUtf8.size()};
    auto const nativeDesc = NrDlssBridgeContextDesc{
        .structSize = sizeof(NrDlssBridgeContextDesc),
        .instance = static_cast<VkInstance>(desc.instance),
        .physicalDevice = static_cast<VkPhysicalDevice>(desc.physicalDevice),
        .device = static_cast<VkDevice>(desc.device),
        .applicationDataPathUtf8 = pathBytes.c_str(),
    };
    auto nativeStatus = emptyBridgeStatus();
    auto const code = runtime.api.createContext(&nativeDesc, &impl_->context, &nativeStatus);
    impl_->status = code == NR_DLSS_BRIDGE_STATUS_SUCCESS ? Status{} : fromBridgeStatus(code, nativeStatus);
    impl_->available = impl_->context != nullptr && runtime.api.contextAvailable(impl_->context) != 0u;
#else
    static_cast<void>(desc);
#endif
}

Context::~Context() = default;

Context::Context(Context &&) noexcept = default;
Context &Context::operator=(Context &&) noexcept = default;

bool Context::valid() const noexcept
{
    return impl_ && impl_->status.success() && impl_->available;
}

const Status &Context::status() const noexcept
{
    static const auto movedFrom = Status{
        .code = StatusCode::Unavailable,
        .message = "DLSS context is in a moved-from state.",
    };
    return impl_ ? impl_->status : movedFrom;
}

bool Context::rayReconstructionAvailable() const noexcept
{
    return valid();
}

OptimalSettings Context::optimalSettings(Dimensions targetSize, Quality quality)
{
    auto result = OptimalSettings{};
#if NR_DLSS_BRIDGE_ENABLED
    if (!valid() || !targetSize.valid() || quality == Quality::Count)
    {
        result.status =
            !valid() ? status()
                     : Status{
                           .code = StatusCode::InvalidArgument,
                           .message = "DLSS optimal-settings query requires valid target dimensions and quality.",
                       };
        return result;
    }
    auto native = NrDlssBridgeOptimalSettings{};
    native.structSize = sizeof(NrDlssBridgeOptimalSettings);
    auto nativeStatus = emptyBridgeStatus();
    auto const code = bridgeRuntime().api.getOptimalSettings(
        impl_->context, NrDlssBridgeDimensions{targetSize.width, targetSize.height},
        static_cast<uint32_t>(toBridge(quality)), &native, &nativeStatus);
    result.status = code == NR_DLSS_BRIDGE_STATUS_SUCCESS ? Status{} : fromBridgeStatus(code, nativeStatus);
    if (result.status.success())
    {
        result.optimalRenderSize = {native.optimalRenderSize.width, native.optimalRenderSize.height};
        result.minimumRenderSize = {native.minimumRenderSize.width, native.minimumRenderSize.height};
        result.maximumRenderSize = {native.maximumRenderSize.width, native.maximumRenderSize.height};
    }
#else
    static_cast<void>(targetSize);
    static_cast<void>(quality);
    result.status = bridgeNotAvailableStatus();
#endif
    return result;
}

std::unique_ptr<RayReconstructionFeature> Context::createRayReconstruction(vk::CommandBuffer commandBuffer,
                                                                           const RayReconstructionCreateDesc &desc)
{
    auto featureImpl = std::make_unique<RayReconstructionFeature::Impl>();
#if NR_DLSS_BRIDGE_ENABLED
    if (!valid() || !commandBuffer || !desc.renderSize.valid() || !desc.targetSize.valid() ||
        desc.quality == Quality::Count)
    {
        featureImpl->status = !valid() ? status()
                                       : Status{
                                             .code = StatusCode::InvalidArgument,
                                             .message = "DLSS Ray Reconstruction creation requires a valid context, "
                                                        "command buffer, dimensions, and quality.",
                                         };
        return std::unique_ptr<RayReconstructionFeature>(new RayReconstructionFeature(std::move(featureImpl)));
    }

    auto native = NrDlssBridgeRayReconstructionCreateDesc{};
    native.structSize = sizeof(NrDlssBridgeRayReconstructionCreateDesc);
    native.renderSize = {desc.renderSize.width, desc.renderSize.height};
    native.targetSize = {desc.targetSize.width, desc.targetSize.height};
    native.quality = static_cast<uint32_t>(toBridge(desc.quality));
    native.roughnessMode = static_cast<uint32_t>(desc.roughnessMode);
    native.depthType = static_cast<uint32_t>(desc.depthType);
    native.createFlags = toBridgeFlags(desc.flags);
    native.enableOutputSubrects = desc.enableOutputSubrects ? 1u : 0u;
    auto const qualityIndices = std::views::iota(std::size_t{0u}, desc.presets.size());
    std::ranges::for_each(qualityIndices, [&](std::size_t index) {
        native.presets[index] = static_cast<uint32_t>(toBridge(desc.presets[index]));
    });
    auto nativeStatus = emptyBridgeStatus();
    auto const code = bridgeRuntime().api.createRayReconstruction(
        impl_->context, static_cast<VkCommandBuffer>(commandBuffer), &native, &featureImpl->feature, &nativeStatus);
    featureImpl->status = code == NR_DLSS_BRIDGE_STATUS_SUCCESS ? Status{} : fromBridgeStatus(code, nativeStatus);
#else
    static_cast<void>(commandBuffer);
    static_cast<void>(desc);
#endif
    return std::unique_ptr<RayReconstructionFeature>(new RayReconstructionFeature(std::move(featureImpl)));
}

RayReconstructionFeature::RayReconstructionFeature(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl))
{
}

RayReconstructionFeature::~RayReconstructionFeature() = default;

RayReconstructionFeature::RayReconstructionFeature(RayReconstructionFeature &&) noexcept = default;
RayReconstructionFeature &RayReconstructionFeature::operator=(RayReconstructionFeature &&) noexcept = default;

bool RayReconstructionFeature::valid() const noexcept
{
#if NR_DLSS_BRIDGE_ENABLED
    return impl_ && impl_->status.success() && impl_->feature != nullptr;
#else
    return false;
#endif
}

const Status &RayReconstructionFeature::status() const noexcept
{
    static const auto movedFrom = Status{
        .code = StatusCode::Unavailable,
        .message = "DLSS feature is in a moved-from state.",
    };
    return impl_ ? impl_->status : movedFrom;
}

Status RayReconstructionFeature::evaluate(vk::CommandBuffer commandBuffer, const RayReconstructionEvalDesc &desc)
{
#if NR_DLSS_BRIDGE_ENABLED
    if (!valid() || !commandBuffer)
    {
        return !valid() ? status()
                        : Status{
                              .code = StatusCode::InvalidArgument,
                              .message = "DLSS Ray Reconstruction evaluation requires a valid command buffer.",
                          };
    }

    auto native = NrDlssBridgeRayReconstructionEvalDesc{};
    native.structSize = sizeof(NrDlssBridgeRayReconstructionEvalDesc);
    native.jitterOffset[0] = desc.jitterOffset[0];
    native.jitterOffset[1] = desc.jitterOffset[1];
    native.renderSubrectDimensions = {desc.renderSubrectDimensions.width, desc.renderSubrectDimensions.height};
    native.reset = desc.reset ? 1u : 0u;
    native.motionVectorScale[0] = desc.motionVectorScale[0];
    native.motionVectorScale[1] = desc.motionVectorScale[1];
    native.preExposure = desc.preExposure;
    native.exposureScale = desc.exposureScale;
    native.indicatorInvertXAxis = desc.indicatorInvertXAxis ? 1u : 0u;
    native.indicatorInvertYAxis = desc.indicatorInvertYAxis ? 1u : 0u;
    native.hasWorldToView = desc.worldToViewRowMajor.has_value() ? 1u : 0u;
    native.hasViewToClip = desc.viewToClipRowMajor.has_value() ? 1u : 0u;
    native.toneMapper = static_cast<uint32_t>(desc.toneMapper);
    native.frameTimeDeltaMilliseconds = desc.frameTimeDeltaMilliseconds;
    auto const resourceIndices = std::views::iota(std::size_t{0u}, rayReconstructionResourceSlotCount);
    std::ranges::for_each(resourceIndices, [&](std::size_t index) {
        native.resources[index].structSize = sizeof(NrDlssBridgeVulkanImage);
        if (desc.resources[index].has_value())
            native.resources[index] = toBridge(*desc.resources[index]);
    });
    auto const subrectIndices = std::views::iota(std::size_t{0u}, rayReconstructionSubrectSlotCount);
    std::ranges::for_each(subrectIndices, [&](std::size_t index) {
        native.subrectBases[index] = {desc.subrectBases[index].x, desc.subrectBases[index].y};
    });
    if (desc.worldToViewRowMajor.has_value())
        std::ranges::copy(*desc.worldToViewRowMajor, native.worldToViewRowMajor);
    if (desc.viewToClipRowMajor.has_value())
        std::ranges::copy(*desc.viewToClipRowMajor, native.viewToClipRowMajor);

    auto nativeStatus = emptyBridgeStatus();
    auto const code = bridgeRuntime().api.evaluateRayReconstruction(
        impl_->feature, static_cast<VkCommandBuffer>(commandBuffer), &native, &nativeStatus);
    return code == NR_DLSS_BRIDGE_STATUS_SUCCESS ? Status{} : fromBridgeStatus(code, nativeStatus);
#else
    static_cast<void>(commandBuffer);
    static_cast<void>(desc);
    return bridgeNotAvailableStatus();
#endif
}

bool sdkCompiled() noexcept
{
#if NR_DLSS_BRIDGE_ENABLED
    return bridgeRuntime().status.success();
#else
    return false;
#endif
}

#if NR_DLSS_BRIDGE_ENABLED
template <typename Query> [[nodiscard]] ExtensionQueryResult queryExtensionNames(Query &&query)
{
    auto count = uint32_t{0u};
    auto status = emptyBridgeStatus();
    auto code = query(&count, nullptr, &status);
    if (code != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        return ExtensionQueryResult{.status = fromBridgeStatus(code, status)};
    }
    if (count == 0u)
    {
        return {};
    }

    auto nativeNames = std::vector<NrDlssBridgeExtensionName>(count);
    auto capacity = count;
    status = emptyBridgeStatus();
    code = query(&capacity, nativeNames.data(), &status);
    if (code != NR_DLSS_BRIDGE_STATUS_SUCCESS)
    {
        return ExtensionQueryResult{.status = fromBridgeStatus(code, status)};
    }

    return ExtensionQueryResult{
        .names = nativeNames | std::views::take(capacity) |
                 std::views::transform([](const NrDlssBridgeExtensionName &name) { return std::string{name.value}; }) |
                 std::ranges::to<std::vector>(),
    };
}
#endif

ExtensionQueryResult rayReconstructionInstanceExtensions()
{
#if NR_DLSS_BRIDGE_ENABLED
    auto &runtime = bridgeRuntime();
    if (!runtime.status.success())
    {
        return ExtensionQueryResult{.status = bridgeNotAvailableStatus()};
    }
    return queryExtensionNames([&](uint32_t *count, NrDlssBridgeExtensionName *names, NrDlssBridgeStatus *status) {
        return runtime.api.getInstanceExtensions(count, names, status);
    });
#else
    return ExtensionQueryResult{.status = bridgeNotAvailableStatus()};
#endif
}

ExtensionQueryResult rayReconstructionDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physicalDevice)
{
#if NR_DLSS_BRIDGE_ENABLED
    auto &runtime = bridgeRuntime();
    if (!runtime.status.success())
    {
        return ExtensionQueryResult{.status = bridgeNotAvailableStatus()};
    }
    if (!instance || !physicalDevice)
    {
        return ExtensionQueryResult{
            .status =
                Status{
                    .code = StatusCode::InvalidArgument,
                    .message =
                        "DLSS device-extension query requires valid Vulkan instance and physical-device handles.",
                },
        };
    }
    return queryExtensionNames([&](uint32_t *count, NrDlssBridgeExtensionName *names, NrDlssBridgeStatus *status) {
        return runtime.api.getDeviceExtensions(static_cast<VkInstance>(instance),
                                               static_cast<VkPhysicalDevice>(physicalDevice), count, names, status);
    });
#else
    static_cast<void>(instance);
    static_cast<void>(physicalDevice);
    return ExtensionQueryResult{.status = bridgeNotAvailableStatus()};
#endif
}

std::string_view qualityName(Quality quality) noexcept
{
    constexpr auto names = std::array{
        std::string_view{"Performance"},       std::string_view{"Balanced"}, std::string_view{"Quality"},
        std::string_view{"Ultra Performance"}, std::string_view{"DLAA"},
    };
    auto const index = static_cast<std::size_t>(quality);
    return index < names.size() ? names[index] : std::string_view{"Invalid"};
}

std::string_view presetName(Preset preset) noexcept
{
    constexpr auto names = std::array{
        std::string_view{"Default"},
        std::string_view{"D"},
        std::string_view{"E"},
    };
    auto const index = static_cast<std::size_t>(preset);
    return index < names.size() ? names[index] : std::string_view{"Invalid"};
}

std::string_view resourceSlotName(RayReconstructionResourceSlot slot) noexcept
{
    constexpr auto names = std::array{
        "diffuseAlbedo",
        "specularAlbedo",
        "normals",
        "roughness",
        "color",
        "alpha",
        "output",
        "outputAlpha",
        "depth",
        "motionVectors",
        "transparencyMask",
        "exposureTexture",
        "biasCurrentColorMask",
        "reflectedAlbedo",
        "colorBeforeParticles",
        "colorAfterParticles",
        "colorBeforeTransparency",
        "colorAfterTransparency",
        "colorBeforeFog",
        "colorAfterFog",
        "screenSpaceSubsurfaceScatteringGuide",
        "colorBeforeScreenSpaceSubsurfaceScattering",
        "colorAfterScreenSpaceSubsurfaceScattering",
        "screenSpaceRefractionGuide",
        "colorBeforeScreenSpaceRefraction",
        "colorAfterScreenSpaceRefraction",
        "depthOfFieldGuide",
        "colorBeforeDepthOfField",
        "colorAfterDepthOfField",
        "diffuseHitDistance",
        "specularHitDistance",
        "diffuseRayDirection",
        "specularRayDirection",
        "diffuseRayDirectionHitDistance",
        "specularRayDirectionHitDistance",
        "gBufferAlbedo",
        "gBufferRoughness",
        "gBufferMetallic",
        "gBufferSpecular",
        "gBufferSubsurface",
        "gBufferNormals",
        "gBufferShadingModelId",
        "gBufferMaterialId",
        "gBufferSpecularAlbedo",
        "gBufferIndirectAlbedo",
        "gBufferSpecularMotionVectors",
        "gBufferDisocclusionMask",
        "gBufferEmissive",
        "gBufferResponsivityMask",
        "gBufferReserved14",
        "gBufferReserved15",
        "gBufferReserved16",
        "motionVectors3D",
        "particleMask",
        "animatedTextureMask",
        "depthHighResolution",
        "positionViewSpace",
        "rayTracingHitDistance",
        "reflectionMotionVectors",
        "transparencyLayer",
        "transparencyLayerOpacity",
        "transparencyLayerMotionVectors",
        "disocclusionMask",
        "responsivityMask",
    };
    static_assert(names.size() == rayReconstructionResourceSlotCount);
    auto const index = static_cast<std::size_t>(slot);
    return index < names.size() ? std::string_view{names[index]} : std::string_view{"invalid"};
}

std::string_view subrectSlotName(RayReconstructionSubrectSlot slot) noexcept
{
    constexpr auto names = std::array{
        "alpha",
        "outputAlpha",
        "diffuseAlbedo",
        "specularAlbedo",
        "normals",
        "roughness",
        "color",
        "depth",
        "motionVectors",
        "translucency",
        "biasCurrentColor",
        "output",
        "reflectedAlbedo",
        "colorBeforeParticles",
        "colorAfterParticles",
        "colorBeforeTransparency",
        "colorAfterTransparency",
        "colorBeforeFog",
        "colorAfterFog",
        "screenSpaceSubsurfaceScatteringGuide",
        "colorBeforeScreenSpaceSubsurfaceScattering",
        "colorAfterScreenSpaceSubsurfaceScattering",
        "screenSpaceRefractionGuide",
        "colorBeforeScreenSpaceRefraction",
        "colorAfterScreenSpaceRefraction",
        "depthOfFieldGuide",
        "colorBeforeDepthOfField",
        "colorAfterDepthOfField",
        "diffuseHitDistance",
        "specularHitDistance",
        "diffuseRayDirection",
        "specularRayDirection",
        "diffuseRayDirectionHitDistance",
        "specularRayDirectionHitDistance",
        "transparencyLayer",
        "transparencyLayerOpacity",
        "transparencyLayerMotionVectors",
        "disocclusionMask",
        "responsivityMask",
    };
    static_assert(names.size() == rayReconstructionSubrectSlotCount);
    auto const index = static_cast<std::size_t>(slot);
    return index < names.size() ? std::string_view{names[index]} : std::string_view{"invalid"};
}
} // namespace nr::dependency::dlss
