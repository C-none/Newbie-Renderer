export module nr.rhi:nsightGraphics;
import dependency.nsight;
import dependency.vulkan;
import nr.utils;
import std;

export namespace nr::rhi
{
class NsightGraphicsFrameHelper
{
  public:
    [[nodiscard]] bool enabled() const noexcept;

    void configureFromEnvironment();

    void injectIfRequested();

    void initializeIfRequested(VkQueue presentQueue);

    void beginFrame(bool vkFrameBoundaryEnabled);

    void stopTraceBeforeBoundaryIfNeeded(VkImage outputImage);

    void markFrameBoundaryAfterPresent(vk::Result presentResult, VkImage outputImage);

  private:
    struct RuntimeState
    {
        nr::platform::NsightGraphicsConfig config{};
        std::optional<std::uint64_t> targetFrame{};
        std::uint64_t currentFrameOrdinal = 0;
        std::uint64_t traceStopFrameOrdinal = 0;
        bool injected = false;
        bool initialized = false;
        bool traceActivated = false;
        bool captureRequested = false;
        bool traceRunning = false;
        bool traceStopRequested = false;
        bool frameBoundaryFailureLogged = false;
    };

    [[nodiscard]] static std::optional<std::string> readEnvironmentVariable(const char *name);

    [[nodiscard]] static bool textEqualsAny(std::string_view text, std::initializer_list<std::string_view> candidates);

    [[nodiscard]] static std::wstring widenEnvironmentPath(std::string_view text);

    [[nodiscard]] static std::optional<std::uint64_t> parseUnsignedEnvironmentInteger(std::string_view text);

    [[nodiscard]] static nr::platform::NsightGraphicsActivity parseActivity(std::string_view text);

    [[nodiscard]] static std::string_view activityName(nr::platform::NsightGraphicsActivity activity) noexcept;

    [[nodiscard]] static std::string_view resultName(nr::platform::NsightGraphicsResult result) noexcept;

    [[nodiscard]] static nr::platform::NsightGraphicsCaptureDelimiter captureDelimiter(
        bool vkFrameBoundaryEnabled) noexcept;

    void disableActivity() noexcept;

    void reportFailure(std::string_view operation, nr::platform::NsightGraphicsResult result) const;

    void requestCapture(bool vkFrameBoundaryEnabled);

    void startTrace();

    RuntimeState state_{};
    VkQueue presentQueue_{};
};
} // namespace nr::rhi
