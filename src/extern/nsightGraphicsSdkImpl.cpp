#include "nsightGraphicsSdkBridge.h"

#if defined(NR_ENABLE_NSIGHT_GRAPHICS_SDK) && NR_ENABLE_NSIGHT_GRAPHICS_SDK

#include <algorithm>
#include <array>
#include <cwchar>

#if defined(__MINGW32__) && !defined(__STRSAFE__NO_INLINE)
#define __STRSAFE__NO_INLINE
#endif

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#endif
#include <NGFX_GraphicsCapture_Vulkan.h>
#include <NGFX_GPUTrace_Vulkan.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

namespace
{
void ensureLibraryLoadFn() noexcept
{
    static const bool configured = [] {
        NGFX_SetLibraryLoadFn(NGFX_LoadLib_NoVerification);
        return true;
    }();
    static_cast<void>(configured);
}

[[nodiscard]] NrPlatformNsightGraphicsResult toPlatformResult(NGFX_Result result) noexcept
{
    switch (result)
    {
    case NGFX_Result_Success:
        return NrPlatformNsightGraphicsResult::Success;
    case NGFX_Result_NotImplemented:
        return NrPlatformNsightGraphicsResult::Unavailable;
    case NGFX_Result_LibNotFound:
    case NGFX_Result_InvalidLib:
        return NrPlatformNsightGraphicsResult::NotFound;
    case NGFX_Result_DifferentActivityInjected:
        return NrPlatformNsightGraphicsResult::DifferentActivity;
    case NGFX_Result_InvalidParameter:
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    case NGFX_Result_InvalidState:
        return NrPlatformNsightGraphicsResult::InvalidState;
    case NGFX_Result_Timeout:
        return NrPlatformNsightGraphicsResult::Timeout;
    case NGFX_Result_UnspecifiedError:
    case NGFX_Result_COUNT:
        return NrPlatformNsightGraphicsResult::Failed;
    }
    return NrPlatformNsightGraphicsResult::Failed;
}

[[nodiscard]] NGFX_GraphicsCapture_Delimiter toNgfxDelimiter(NrPlatformNsightGraphicsCaptureDelimiter delimiter) noexcept
{
    switch (delimiter)
    {
    case NrPlatformNsightGraphicsCaptureDelimiter::Present:
        return NGFX_GraphicsCapture_Delimiter_Present;
    case NrPlatformNsightGraphicsCaptureDelimiter::FrameBoundary:
        return NGFX_GraphicsCapture_Delimiter_FrameBoundary;
    case NrPlatformNsightGraphicsCaptureDelimiter::VkFrameBoundaryExt:
        return NGFX_GraphicsCapture_Delimiter_VkFrameBoundaryEXT;
    }
    return NGFX_GraphicsCapture_Delimiter_FrameBoundary;
}

[[nodiscard]] bool hasText(const wchar_t* value) noexcept
{
    return value != nullptr && value[0] != L'\0';
}

[[nodiscard]] bool hasText(const char* value) noexcept
{
    return value != nullptr && value[0] != '\0';
}

void copyInstallation(const NGFX_InstallationInfo& source, NrPlatformNsightGraphicsInstallation& destination) noexcept
{
    destination = {};
    destination.versionMajor = source.versionMajor;
    destination.versionMinor = source.versionMinor;
    destination.versionPatch = source.versionPatch;

    if (source.installationPath == nullptr)
    {
        return;
    }

#if defined(_WIN32)
    std::wcsncpy(destination.path, source.installationPath, NrPlatformNsightGraphicsInstallationPathCapacity - 1);
    destination.path[NrPlatformNsightGraphicsInstallationPathCapacity - 1] = L'\0';
    destination.valid = destination.path[0] != L'\0';
#else
    auto const sourceLength = std::char_traits<char>::length(source.installationPath);
    auto const copyCount = std::min<std::size_t>(sourceLength, NrPlatformNsightGraphicsInstallationPathCapacity - 1);
    auto indices = std::views::iota(std::size_t{0}, copyCount);
    std::ranges::for_each(indices, [&](std::size_t index) {
        destination.path[index] = static_cast<wchar_t>(source.installationPath[index]);
    });
    destination.path[copyCount] = L'\0';
    destination.valid = copyCount > 0;
#endif
}

[[nodiscard]] NrPlatformNsightGraphicsResult resolveInstallationPath(NrPlatformNsightGraphicsInstallation& outInstallation) noexcept
{
    ensureLibraryLoadFn();

    constexpr auto maxInstallations = std::uint32_t{8};
    auto installations = std::array<NGFX_InstallationInfo, maxInstallations>{};
    auto installationCount = std::uint32_t{0};

    auto const result = NGFX_EnumerateInstallations(
        installations.data(),
        static_cast<std::uint32_t>(installations.size()),
        &installationCount);

    if (installationCount > 0)
    {
        copyInstallation(installations.front(), outInstallation);
    }

    NGFX_FreeInstallations(installations.data(), installationCount);

    if (outInstallation.valid)
    {
        return NrPlatformNsightGraphicsResult::Success;
    }
    return toPlatformResult(result);
}

[[nodiscard]] NrPlatformNsightGraphicsResult resolveInstallationPath(
    const wchar_t* requestedPath,
    NrPlatformNsightGraphicsInstallation& fallback,
    const wchar_t*& resolvedPath) noexcept
{
    if (hasText(requestedPath))
    {
        resolvedPath = requestedPath;
        return NrPlatformNsightGraphicsResult::Success;
    }

    auto const result = resolveInstallationPath(fallback);
    if (result == NrPlatformNsightGraphicsResult::Success)
    {
        resolvedPath = fallback.path;
    }
    return result;
}

[[nodiscard]] NGFX_ResourceDescription_Vulkan makeImageResource(VkImage image) noexcept
{
    auto resource = NGFX_ResourceDescription_Vulkan{};
    resource.version = NGFX_ResourceDescription_Vulkan_VER;
    resource.type = NGFX_ResourceType_Vulkan_VkImage;
    resource.image = image;
    return resource;
}

[[nodiscard]] NrPlatformNsightGraphicsResult injectGraphicsCapture(const NrPlatformNsightGraphicsInjectDesc& desc, const wchar_t* installationPath) noexcept
{
    ensureLibraryLoadFn();

    auto settings = NGFX_GraphicsCapture_InjectionSettings{};
    if (auto const result = NGFX_GraphicsCapture_InjectionSettings_SetDefaults(&settings); result != NGFX_Result_Success)
    {
        return toPlatformResult(result);
    }

    settings.noHUD = desc.noHud;
    settings.outputDir = hasText(desc.outputDir) ? desc.outputDir : nullptr;
    settings.frameCount = std::max(1u, desc.frameCount);
    settings.captureDefaultHotkey = false;
    settings.captureHotkey = nullptr;
    settings.captureFrame = NGFX_INVALID_U32;
    settings.captureCountdownTimer = NGFX_INVALID_U32;
    settings.captureUntilHotkey = false;

    auto params = NGFX_GraphicsCapture_Inject_Vulkan_Params{};
    params.version = NGFX_GraphicsCapture_Inject_Vulkan_Params_VER;
    params.installationPath = installationPath;
    params.settings = &settings;

    return toPlatformResult(NGFX_GraphicsCapture_Inject_Vulkan(&params));
}

[[nodiscard]] NrPlatformNsightGraphicsResult injectGpuTrace(const NrPlatformNsightGraphicsInjectDesc& desc, const wchar_t* installationPath) noexcept
{
    ensureLibraryLoadFn();

    auto settings = NGFX_GPUTrace_InjectionSettings{};
    if (auto const result = NGFX_GPUTrace_InjectionSettings_SetDefaults(&settings); result != NGFX_Result_Success)
    {
        return toPlatformResult(result);
    }

    settings.startEvent = NGFX_GPUTrace_StartEvent_NGFXSDK;
    settings.stopEvent = NGFX_GPUTrace_StopEvent_NGFXSDK;
    settings.collectScreenshot = true;
    settings.hudPosition = desc.noHud ? NGFX_GPUTrace_HUDPosition_Hidden : NGFX_GPUTrace_HUDPosition_TopLeft;

    auto params = NGFX_GPUTrace_Inject_Vulkan_Params{};
    params.version = NGFX_GPUTrace_Inject_Vulkan_Params_VER;
    params.installationPath = installationPath;
    params.settings = &settings;

    return toPlatformResult(NGFX_GPUTrace_Inject_Vulkan(&params));
}
} // namespace

extern "C" bool nrPlatformNsightGraphicsSdkCompiled() noexcept
{
    return true;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsFindLatestInstallation(NrPlatformNsightGraphicsInstallation* outInstallation) noexcept
{
    if (outInstallation == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }
    return resolveInstallationPath(*outInstallation);
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInject(const NrPlatformNsightGraphicsInjectDesc* desc) noexcept
{
    if (desc == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }
    if (desc->activity == NrPlatformNsightGraphicsActivity::Off)
    {
        return NrPlatformNsightGraphicsResult::Success;
    }

    auto fallbackInstallation = NrPlatformNsightGraphicsInstallation{};
    auto* installationPath = static_cast<const wchar_t*>(nullptr);
    auto const resolveResult = resolveInstallationPath(desc->installationPath, fallbackInstallation, installationPath);
    if (resolveResult != NrPlatformNsightGraphicsResult::Success)
    {
        return resolveResult;
    }

    switch (desc->activity)
    {
    case NrPlatformNsightGraphicsActivity::Capture:
        return injectGraphicsCapture(*desc, installationPath);
    case NrPlatformNsightGraphicsActivity::Trace:
        return injectGpuTrace(*desc, installationPath);
    case NrPlatformNsightGraphicsActivity::Off:
        return NrPlatformNsightGraphicsResult::Success;
    }
    return NrPlatformNsightGraphicsResult::InvalidParameter;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInitialize(NrPlatformNsightGraphicsActivity activity) noexcept
{
    ensureLibraryLoadFn();

    switch (activity)
    {
    case NrPlatformNsightGraphicsActivity::Capture:
    {
        auto params = NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params{};
        params.version = NGFX_GraphicsCapture_InitializeActivity_Vulkan_Params_VER;
        return toPlatformResult(NGFX_GraphicsCapture_InitializeActivity_Vulkan(&params));
    }
    case NrPlatformNsightGraphicsActivity::Trace:
    {
        auto params = NGFX_GPUTrace_InitializeActivity_Vulkan_Params{};
        params.version = NGFX_GPUTrace_InitializeActivity_Vulkan_Params_VER;
        return toPlatformResult(NGFX_GPUTrace_InitializeActivity_Vulkan(&params));
    }
    case NrPlatformNsightGraphicsActivity::Off:
        return NrPlatformNsightGraphicsResult::Success;
    }
    return NrPlatformNsightGraphicsResult::InvalidParameter;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsActivateTrace(VkQueue queue) noexcept
{
    ensureLibraryLoadFn();

    if (queue == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }

    auto params = NGFX_GPUTrace_ActivateTrace_Vulkan_Params{};
    params.version = NGFX_GPUTrace_ActivateTrace_Vulkan_Params_VER;
    params.queue = queue;
    return toPlatformResult(NGFX_GPUTrace_ActivateTrace_Vulkan(&params));
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsRequestCapture(const NrPlatformNsightGraphicsCaptureRequest* request) noexcept
{
    ensureLibraryLoadFn();

    if (request == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }

    auto params = NGFX_GraphicsCapture_RequestCapture_Vulkan_Params{};
    params.version = NGFX_GraphicsCapture_RequestCapture_Vulkan_Params_VER;
    params.delimiter = toNgfxDelimiter(request->delimiter);
    params.framesBeforeStart = request->framesBeforeStart;
    params.framesToCapture = std::clamp(request->framesToCapture, 1u, 60u);
    return toPlatformResult(NGFX_GraphicsCapture_RequestCapture_Vulkan(&params));
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStartTrace() noexcept
{
    ensureLibraryLoadFn();

    auto params = NGFX_GPUTrace_StartTrace_Vulkan_Params{};
    params.version = NGFX_GPUTrace_StartTrace_Vulkan_Params_VER;
    return toPlatformResult(NGFX_GPUTrace_StartTrace_Vulkan(&params));
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStopTrace(const NrPlatformNsightGraphicsTraceStop* desc) noexcept
{
    ensureLibraryLoadFn();

    if (desc == nullptr || desc->queue == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }

    auto outputResource = makeImageResource(desc->outputImage);
    auto params = NGFX_GPUTrace_StopTrace_Vulkan_Params{};
    params.version = NGFX_GPUTrace_StopTrace_Vulkan_Params_VER;
    params.flags = desc->stopOnNextFrameBoundary ? NGFX_GPUTrace_StopTraceFlag_NextFrameBoundary : NGFX_GPUTrace_StopTraceFlag_None;
    params.queue = desc->queue;
    params.outputResources = desc->hasOutputImage ? &outputResource : nullptr;
    params.numOutputResources = desc->hasOutputImage ? 1 : 0;
    return toPlatformResult(NGFX_GPUTrace_StopTrace_Vulkan(&params));
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsMarkFrameBoundary(const NrPlatformNsightGraphicsFrameBoundary* desc) noexcept
{
    ensureLibraryLoadFn();

    if (desc == nullptr || desc->queue == nullptr)
    {
        return NrPlatformNsightGraphicsResult::InvalidParameter;
    }

    auto outputResource = makeImageResource(desc->outputImage);
    auto params = NGFX_FrameBoundary_Vulkan_Params{};
    params.version = NGFX_FrameBoundary_Vulkan_Params_VER;
    params.queue = desc->queue;
    params.outputResources = desc->hasOutputImage ? &outputResource : nullptr;
    params.numOutputResources = desc->hasOutputImage ? 1 : 0;
    return toPlatformResult(NGFX_FrameBoundary_Vulkan(&params));
}

#else

extern "C" bool nrPlatformNsightGraphicsSdkCompiled() noexcept
{
    return false;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsFindLatestInstallation(NrPlatformNsightGraphicsInstallation* outInstallation) noexcept
{
    if (outInstallation != nullptr)
    {
        *outInstallation = {};
    }
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInject(const NrPlatformNsightGraphicsInjectDesc*) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInitialize(NrPlatformNsightGraphicsActivity) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsActivateTrace(VkQueue) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsRequestCapture(const NrPlatformNsightGraphicsCaptureRequest*) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStartTrace() noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStopTrace(const NrPlatformNsightGraphicsTraceStop*) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsMarkFrameBoundary(const NrPlatformNsightGraphicsFrameBoundary*) noexcept
{
    return NrPlatformNsightGraphicsResult::Unavailable;
}

#endif
