module nr.rhi;
import :nsightGraphics;
import dependency.nsight;
import dependency.vulkan;
import nr.utils;
import std;

namespace nr::rhi
{
[[nodiscard]] bool NsightGraphicsFrameHelper::enabled() const noexcept
{
        return state_.config.activity != nr::platform::NsightGraphicsActivity::Off &&
               state_.injected &&
               state_.initialized;
    }

void NsightGraphicsFrameHelper::configureFromEnvironment()
{
        auto state = RuntimeState{};
        auto activityText = readEnvironmentVariable("NR_NSIGHT_GRAPHICS_ACTIVITY");
        if (!activityText.has_value())
        {
            state_ = std::move(state);
            return;
        }

        state.config.activity = parseActivity(*activityText);
        if (state.config.activity == nr::platform::NsightGraphicsActivity::Off)
        {
            state_ = std::move(state);
            return;
        }

        if (auto installDir = readEnvironmentVariable("NR_NSIGHT_GRAPHICS_INSTALL_DIR"); installDir.has_value())
        {
            state.config.installationPath = widenEnvironmentPath(*installDir);
        }

        if (auto outputDir = readEnvironmentVariable("NR_NSIGHT_GRAPHICS_OUTPUT_DIR"); outputDir.has_value())
        {
            state.config.outputDir = *outputDir;
            if (state.config.activity == nr::platform::NsightGraphicsActivity::Trace)
            {
                nrInfo<LogLevel::warning>("NR_NSIGHT_GRAPHICS_OUTPUT_DIR is not exposed by Nsight Graphics SDK 0.9.0 GPU Trace injection settings; Nsight controls trace output location.");
            }
        }

        if (auto framesText = readEnvironmentVariable("NR_NSIGHT_GRAPHICS_FRAMES"); framesText.has_value())
        {
            auto frameCount = parseUnsignedEnvironmentInteger(*framesText);
            if (frameCount.has_value() && *frameCount > 0)
            {
                auto const clamped = std::min<std::uint64_t>(*frameCount, std::numeric_limits<std::uint32_t>::max());
                state.config.frameCount = static_cast<std::uint32_t>(clamped);
            }
            else
            {
                nrInfo<LogLevel::warning>("Ignoring invalid NR_NSIGHT_GRAPHICS_FRAMES value; using 1.");
            }
        }

        if (auto frameText = readEnvironmentVariable("NR_NSIGHT_GRAPHICS_FRAME"); frameText.has_value())
        {
            auto targetFrame = parseUnsignedEnvironmentInteger(*frameText);
            if (targetFrame.has_value() && *targetFrame > 0)
            {
                state.targetFrame = *targetFrame;
            }
            else
            {
                nrInfo<LogLevel::warning>("Ignoring invalid NR_NSIGHT_GRAPHICS_FRAME value; Nsight activity will initialize but not auto-trigger.");
            }
        }
        else
        {
            nrInfo(std::format(
                "NR_NSIGHT_GRAPHICS_ACTIVITY='{}' requested without NR_NSIGHT_GRAPHICS_FRAME; SDK will initialize but not auto-trigger.",
                activityName(state.config.activity)));
        }

        state_ = std::move(state);
    }

void NsightGraphicsFrameHelper::injectIfRequested()
{
        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Off)
        {
            return;
        }

        if (!nr::platform::nsightGraphicsSdkCompiled())
        {
            nrInfo<LogLevel::warning>("Nsight Graphics SDK integration was requested, but dependency was built without SDK support.");
            disableActivity();
            return;
        }

        auto const result = nr::platform::injectNsightGraphics(state_.config);
        if (result != nr::platform::NsightGraphicsResult::Success)
        {
            reportFailure("inject", result);
            disableActivity();
            return;
        }

        state_.injected = true;
        nrInfo(std::format(
            "Nsight Graphics SDK {} activity injected.",
            activityName(state_.config.activity)));
    }

void NsightGraphicsFrameHelper::initializeIfRequested(VkQueue presentQueue)
{
        presentQueue_ = presentQueue;

        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Off || !state_.injected)
        {
            return;
        }

        auto result = nr::platform::initializeNsightGraphics(state_.config.activity);
        if (result != nr::platform::NsightGraphicsResult::Success)
        {
            reportFailure("initialize", result);
            disableActivity();
            return;
        }

        state_.initialized = true;

        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Trace)
        {
            result = nr::platform::activateNsightTrace(presentQueue_);
            if (result != nr::platform::NsightGraphicsResult::Success)
            {
                reportFailure("activate GPU Trace", result);
                disableActivity();
                return;
            }
            state_.traceActivated = true;
        }

        nrInfo(std::format(
            "Nsight Graphics SDK {} activity initialized.",
            activityName(state_.config.activity)));
    }

void NsightGraphicsFrameHelper::beginFrame(bool vkFrameBoundaryEnabled)
{
        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Off)
        {
            return;
        }

        ++state_.currentFrameOrdinal;
        if (!enabled() || !state_.targetFrame.has_value())
        {
            return;
        }

        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Capture &&
            !state_.captureRequested &&
            state_.currentFrameOrdinal == *state_.targetFrame)
        {
            requestCapture(vkFrameBoundaryEnabled);
            return;
        }

        if (state_.config.activity == nr::platform::NsightGraphicsActivity::Trace &&
            state_.traceActivated &&
            !state_.traceRunning &&
            state_.currentFrameOrdinal == *state_.targetFrame)
        {
            startTrace();
        }
    }

void NsightGraphicsFrameHelper::stopTraceBeforeBoundaryIfNeeded(VkImage outputImage)
{
        if (!enabled() ||
            state_.config.activity != nr::platform::NsightGraphicsActivity::Trace ||
            !state_.traceRunning ||
            state_.traceStopRequested ||
            state_.currentFrameOrdinal < state_.traceStopFrameOrdinal)
        {
            return;
        }

        auto stopDesc = nr::platform::NsightGraphicsTraceStop{
            .queue = presentQueue_,
            .outputImage = outputImage,
            .hasOutputImage = outputImage != nullptr,
        };
        auto const result = nr::platform::stopNsightTrace(stopDesc);
        if (result != nr::platform::NsightGraphicsResult::Success)
        {
            reportFailure("stop GPU Trace", result);
            disableActivity();
            return;
        }

        state_.traceRunning = false;
        state_.traceStopRequested = true;
        nrInfo(std::format(
            "Nsight Graphics GPU Trace stop requested before frame boundary {}.",
            state_.currentFrameOrdinal));
    }

void NsightGraphicsFrameHelper::markFrameBoundaryAfterPresent(vk::Result presentResult, VkImage outputImage)
{
        if (!enabled())
        {
            return;
        }
        if (presentResult != vk::Result::eSuccess && presentResult != vk::Result::eSuboptimalKHR)
        {
            return;
        }

        auto boundary = nr::platform::NsightGraphicsFrameBoundary{
            .queue = presentQueue_,
            .outputImage = outputImage,
            .hasOutputImage = outputImage != nullptr,
        };
        auto const result = nr::platform::markNsightFrameBoundary(boundary);
        if (result != nr::platform::NsightGraphicsResult::Success && !state_.frameBoundaryFailureLogged)
        {
            state_.frameBoundaryFailureLogged = true;
            reportFailure("frame boundary", result);
        }
    }

[[nodiscard]] std::optional<std::string> NsightGraphicsFrameHelper::readEnvironmentVariable(const char* name)
{
        const auto* value = std::getenv(name);
        if (value == nullptr || value[0] == '\0')
        {
            return {};
        }
        return std::string(value);
    }

[[nodiscard]] bool NsightGraphicsFrameHelper::textEqualsAny(std::string_view text, std::initializer_list<std::string_view> candidates)
{
        return std::ranges::any_of(candidates, [text](std::string_view candidate) { return text == candidate; });
    }

[[nodiscard]] std::wstring NsightGraphicsFrameHelper::widenEnvironmentPath(std::string_view text)
{
        return text |
               std::views::transform([](char value) {
                   return static_cast<wchar_t>(static_cast<unsigned char>(value));
               }) |
               std::ranges::to<std::wstring>();
    }

[[nodiscard]] std::optional<std::uint64_t> NsightGraphicsFrameHelper::parseUnsignedEnvironmentInteger(std::string_view text)
{
        auto value = std::uint64_t{0};
        auto const* begin = text.data();
        auto const* end = text.data() + text.size();
        auto const [cursor, error] = std::from_chars(begin, end, value);
        if (error == std::errc{} && cursor == end)
        {
            return value;
        }
        return {};
    }

[[nodiscard]] nr::platform::NsightGraphicsActivity NsightGraphicsFrameHelper::parseActivity(std::string_view text)
{
        if (textEqualsAny(text, {"off", "OFF", "Off", "0"}))
        {
            return nr::platform::NsightGraphicsActivity::Off;
        }
        if (textEqualsAny(text, {"capture", "CAPTURE", "Capture"}))
        {
            return nr::platform::NsightGraphicsActivity::Capture;
        }
        if (textEqualsAny(text, {"trace", "TRACE", "Trace"}))
        {
            return nr::platform::NsightGraphicsActivity::Trace;
        }

        nrInfo<LogLevel::warning>(std::format(
            "Ignoring unsupported NR_NSIGHT_GRAPHICS_ACTIVITY='{}'; expected off, capture, or trace.",
            text));
        return nr::platform::NsightGraphicsActivity::Off;
    }

[[nodiscard]] std::string_view NsightGraphicsFrameHelper::activityName(nr::platform::NsightGraphicsActivity activity) noexcept
{
        switch (activity)
        {
        case nr::platform::NsightGraphicsActivity::Off:
            return "off";
        case nr::platform::NsightGraphicsActivity::Capture:
            return "capture";
        case nr::platform::NsightGraphicsActivity::Trace:
            return "trace";
        }
        return "unknown";
    }

[[nodiscard]] std::string_view NsightGraphicsFrameHelper::resultName(nr::platform::NsightGraphicsResult result) noexcept
{
        switch (result)
        {
        case nr::platform::NsightGraphicsResult::Success:
            return "success";
        case nr::platform::NsightGraphicsResult::Unavailable:
            return "unavailable";
        case nr::platform::NsightGraphicsResult::NotFound:
            return "not_found";
        case nr::platform::NsightGraphicsResult::DifferentActivity:
            return "different_activity";
        case nr::platform::NsightGraphicsResult::InvalidParameter:
            return "invalid_parameter";
        case nr::platform::NsightGraphicsResult::InvalidState:
            return "invalid_state";
        case nr::platform::NsightGraphicsResult::Timeout:
            return "timeout";
        case nr::platform::NsightGraphicsResult::Failed:
            return "failed";
        }
        return "unknown";
    }

[[nodiscard]] nr::platform::NsightGraphicsCaptureDelimiter NsightGraphicsFrameHelper::captureDelimiter(bool vkFrameBoundaryEnabled) noexcept
{
        return vkFrameBoundaryEnabled
                   ? nr::platform::NsightGraphicsCaptureDelimiter::VkFrameBoundaryExt
                   : nr::platform::NsightGraphicsCaptureDelimiter::FrameBoundary;
    }

void NsightGraphicsFrameHelper::disableActivity() noexcept
{
        state_.config.activity = nr::platform::NsightGraphicsActivity::Off;
        state_.traceRunning = false;
        state_.traceStopRequested = false;
    }

void NsightGraphicsFrameHelper::reportFailure(std::string_view operation, nr::platform::NsightGraphicsResult result) const
{
        if (result == nr::platform::NsightGraphicsResult::Success)
        {
            return;
        }
        nrInfo<LogLevel::warning>(std::format(
            "Nsight Graphics SDK {} failed with result '{}'.",
            operation,
            resultName(result)));
    }

void NsightGraphicsFrameHelper::requestCapture(bool vkFrameBoundaryEnabled)
{
        auto request = nr::platform::NsightGraphicsCaptureRequest{
            .delimiter = captureDelimiter(vkFrameBoundaryEnabled),
            .framesToCapture = state_.config.frameCount,
        };
        auto const result = nr::platform::requestNsightCapture(request);
        if (result != nr::platform::NsightGraphicsResult::Success)
        {
            reportFailure("request capture", result);
            disableActivity();
            return;
        }

        state_.captureRequested = true;
        nrInfo(std::format(
            "Nsight Graphics capture requested at frame {} for {} frame(s).",
            state_.currentFrameOrdinal,
            state_.config.frameCount));
    }

void NsightGraphicsFrameHelper::startTrace()
{
        auto const result = nr::platform::startNsightTrace();
        if (result != nr::platform::NsightGraphicsResult::Success)
        {
            reportFailure("start GPU Trace", result);
            disableActivity();
            return;
        }

        state_.traceRunning = true;
        state_.traceStopRequested = false;
        state_.traceStopFrameOrdinal = state_.currentFrameOrdinal + std::max(1u, state_.config.frameCount) - 1u;
        nrInfo(std::format(
            "Nsight Graphics GPU Trace started at frame {} and will stop at frame boundary {}.",
            state_.currentFrameOrdinal,
            state_.traceStopFrameOrdinal));
    }
} // namespace nr::rhi
