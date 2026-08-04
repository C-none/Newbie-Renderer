module;
#include "nsightGraphicsSdkBridge.h"

module dependency.nsight;

namespace nr::platform_detail
{
[[nodiscard]] NrPlatformNsightGraphicsActivity toPlatform(nr::platform::NsightGraphicsActivity activity) noexcept
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

[[nodiscard]] NrPlatformNsightGraphicsCaptureDelimiter toPlatform(
    nr::platform::NsightGraphicsCaptureDelimiter delimiter) noexcept
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

[[nodiscard]] nr::platform::NsightGraphicsResult toNsightGraphicsResult(NrPlatformNsightGraphicsResult result) noexcept
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

namespace nr::platform
{
[[nodiscard]] bool nsightGraphicsSdkCompiled() noexcept
{
    return nrPlatformNsightGraphicsSdkCompiled();
}

[[nodiscard]] NsightGraphicsResult injectNsightGraphics(const NsightGraphicsConfig &config) noexcept
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

[[nodiscard]] NsightGraphicsResult initializeNsightGraphics(NsightGraphicsActivity activity) noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(
        nrPlatformNsightGraphicsInitialize(nr::platform_detail::toPlatform(activity)));
}

[[nodiscard]] NsightGraphicsResult activateNsightTrace(VkQueue queue) noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsActivateTrace(queue));
}

[[nodiscard]] NsightGraphicsResult requestNsightCapture(const NsightGraphicsCaptureRequest &request) noexcept
{
    auto platformRequest = NrPlatformNsightGraphicsCaptureRequest{
        .delimiter = nr::platform_detail::toPlatform(request.delimiter),
        .framesBeforeStart = request.framesBeforeStart,
        .framesToCapture = request.framesToCapture,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsRequestCapture(&platformRequest));
}

[[nodiscard]] NsightGraphicsResult startNsightTrace() noexcept
{
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsStartTrace());
}

[[nodiscard]] NsightGraphicsResult stopNsightTrace(const NsightGraphicsTraceStop &desc) noexcept
{
    auto platformDesc = NrPlatformNsightGraphicsTraceStop{
        .queue = desc.queue,
        .outputImage = desc.outputImage,
        .hasOutputImage = desc.hasOutputImage,
        .stopOnNextFrameBoundary = desc.stopOnNextFrameBoundary,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsStopTrace(&platformDesc));
}

[[nodiscard]] NsightGraphicsResult markNsightFrameBoundary(const NsightGraphicsFrameBoundary &desc) noexcept
{
    auto platformDesc = NrPlatformNsightGraphicsFrameBoundary{
        .queue = desc.queue,
        .outputImage = desc.outputImage,
        .hasOutputImage = desc.hasOutputImage,
    };
    return nr::platform_detail::toNsightGraphicsResult(nrPlatformNsightGraphicsMarkFrameBoundary(&platformDesc));
}

bool isNsightInjected() noexcept
{
    return nrPlatformNsightInjected();
}
} // namespace nr::platform
