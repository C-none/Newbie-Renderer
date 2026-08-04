module;
#include "nsightGraphicsSdkBridge.h"

export module dependency.nsight;
export import dependency.vulkan;
import std;

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
[[nodiscard]] NrPlatformNsightGraphicsActivity toPlatform(nr::platform::NsightGraphicsActivity activity) noexcept;
[[nodiscard]] NrPlatformNsightGraphicsCaptureDelimiter toPlatform(
    nr::platform::NsightGraphicsCaptureDelimiter delimiter) noexcept;
[[nodiscard]] nr::platform::NsightGraphicsResult toNsightGraphicsResult(NrPlatformNsightGraphicsResult result) noexcept;
} // namespace nr::platform_detail

export namespace nr::platform
{
[[nodiscard]] bool nsightGraphicsSdkCompiled() noexcept;
[[nodiscard]] NsightGraphicsResult injectNsightGraphics(const NsightGraphicsConfig &config) noexcept;
[[nodiscard]] NsightGraphicsResult initializeNsightGraphics(NsightGraphicsActivity activity) noexcept;
[[nodiscard]] NsightGraphicsResult activateNsightTrace(VkQueue queue) noexcept;
[[nodiscard]] NsightGraphicsResult requestNsightCapture(const NsightGraphicsCaptureRequest &request) noexcept;
[[nodiscard]] NsightGraphicsResult startNsightTrace() noexcept;
[[nodiscard]] NsightGraphicsResult stopNsightTrace(const NsightGraphicsTraceStop &desc) noexcept;
[[nodiscard]] NsightGraphicsResult markNsightFrameBoundary(const NsightGraphicsFrameBoundary &desc) noexcept;

/**
 * @brief Whether NVIDIA Nsight Graphics is intercepting the current process.
 *
 * Windows-only; returns false otherwise. The memory allocator queries this to
 * switch GpuOnly buffers onto a profiler-safe path that avoids
 * VkMemoryDedicatedAllocateInfo, which conflicts with the
 * VkImportMemoryHostPointerInfoEXT Nsight injects (VUID-VkMemoryAllocateInfo-pNext-02806).
 */
bool isNsightInjected() noexcept;
} // namespace nr::platform
