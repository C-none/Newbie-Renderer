#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>

inline constexpr std::uint32_t NrPlatformNsightGraphicsInstallationPathCapacity = 1024;

enum class NrPlatformNsightGraphicsActivity : std::uint32_t
{
    Off,
    Capture,
    Trace,
};

enum class NrPlatformNsightGraphicsResult : std::uint32_t
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

enum class NrPlatformNsightGraphicsCaptureDelimiter : std::uint32_t
{
    Present,
    FrameBoundary,
    VkFrameBoundaryExt,
};

struct NrPlatformNsightGraphicsInstallation
{
    wchar_t path[NrPlatformNsightGraphicsInstallationPathCapacity]{};
    std::uint16_t versionMajor = 0;
    std::uint16_t versionMinor = 0;
    std::uint16_t versionPatch = 0;
    bool valid = false;
};

struct NrPlatformNsightGraphicsInjectDesc
{
    NrPlatformNsightGraphicsActivity activity = NrPlatformNsightGraphicsActivity::Off;
    const wchar_t* installationPath = nullptr;
    const char* outputDir = nullptr;
    std::uint32_t frameCount = 1;
    bool noHud = true;
};

struct NrPlatformNsightGraphicsCaptureRequest
{
    NrPlatformNsightGraphicsCaptureDelimiter delimiter = NrPlatformNsightGraphicsCaptureDelimiter::VkFrameBoundaryExt;
    std::uint32_t framesBeforeStart = 0;
    std::uint32_t framesToCapture = 1;
};

struct NrPlatformNsightGraphicsFrameBoundary
{
    VkQueue queue = nullptr;
    VkImage outputImage = nullptr;
    bool hasOutputImage = false;
};

struct NrPlatformNsightGraphicsTraceStop
{
    VkQueue queue = nullptr;
    VkImage outputImage = nullptr;
    bool hasOutputImage = false;
    bool stopOnNextFrameBoundary = true;
};

extern "C" bool nrPlatformNsightInjected() noexcept;
extern "C" bool nrPlatformNsightGraphicsSdkCompiled() noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsFindLatestInstallation(NrPlatformNsightGraphicsInstallation* outInstallation) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInject(const NrPlatformNsightGraphicsInjectDesc* desc) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsInitialize(NrPlatformNsightGraphicsActivity activity) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsActivateTrace(VkQueue queue) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsRequestCapture(const NrPlatformNsightGraphicsCaptureRequest* request) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStartTrace() noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsStopTrace(const NrPlatformNsightGraphicsTraceStop* desc) noexcept;
extern "C" NrPlatformNsightGraphicsResult nrPlatformNsightGraphicsMarkFrameBoundary(const NrPlatformNsightGraphicsFrameBoundary* desc) noexcept;
