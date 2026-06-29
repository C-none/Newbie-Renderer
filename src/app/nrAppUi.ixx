export module nr.app:ui;
import dependency.ui;

import nr.renderer;
import nr.rhi;
import std;

export namespace nr::app
{
class UiSystem;

struct UiCaptureState
{
    bool wantsMouse = false;
    bool wantsKeyboard = false;
};

struct UiFrameStats
{
    float deltaSeconds = 1.0f / 60.0f;
    float smoothedDeltaSeconds = 1.0f / 60.0f;
    float frameTimeMilliseconds = 1000.0f / 60.0f;
    float framesPerSecond = 60.0f;
    std::uint64_t frameCounter = 0;
};

using UiSectionDrawCallback = std::function<void(UiSystem&)>;

struct UiSection
{
    std::string_view id{};
    std::string_view title{};
    UiSectionDrawCallback draw{};
    bool defaultOpen = true;
};

class UiSystem
{
  public:
    class WindowScope
    {
      public:
        WindowScope() = default;
        WindowScope(UiSystem& owner, bool visible, bool closesWindow) noexcept;

        WindowScope(const WindowScope&) = delete;
        WindowScope& operator=(const WindowScope&) = delete;

        WindowScope(WindowScope&& other) noexcept;
        WindowScope& operator=(WindowScope&& other) noexcept;
        ~WindowScope();

        [[nodiscard]] explicit operator bool() const noexcept;

      private:
        void close() noexcept;

        std::optional<std::reference_wrapper<UiSystem>> owner_{};
        bool visible_ = false;
        bool closesWindow_ = false;
    };

    UiSystem() = default;
    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;
    UiSystem(UiSystem&&) = delete;
    UiSystem& operator=(UiSystem&&) = delete;

    ~UiSystem();

    void initialize();
    void shutdown();

    [[nodiscard]] bool initialized() const noexcept;
    void beginFrame(const nr::rhi::PresentationContext& presentation, float deltaSeconds);
    void finalizeFrame();

    [[nodiscard]] WindowScope window(std::string_view title, ImGuiWindowFlags flags = 0);
    void queueSection(UiSection section);
    void renderSections(std::span<const UiSection> sections, ImGuiWindowFlags flags = 0);
    void renderSections(
        std::span<const UiSection> leadingSections,
        std::span<const UiSection> trailingSections,
        ImGuiWindowFlags flags = 0);
    void separator();
    void text(std::string_view content);

    template <typename... TArgs>
    void textFmt(std::format_string<TArgs...> format, TArgs&&... args)
    {
        text(std::format(format, std::forward<TArgs>(args)...));
    }

    [[nodiscard]] bool checkbox(std::string_view label, bool& value);
    [[nodiscard]] bool button(std::string_view label);
    [[nodiscard]] bool inputText(std::string_view label, std::string& value);
    [[nodiscard]] bool beginCombo(std::string_view label, std::string_view preview);
    void endCombo();
    [[nodiscard]] bool selectable(std::string_view label, bool selected = false);
    void setItemDefaultFocus();
    [[nodiscard]] const UiFrameStats& stats() const noexcept;
    void setCameraFrame(const nr::renderer::ViewerPerspectiveCameraFrame& cameraFrame) noexcept;
    [[nodiscard]] const nr::renderer::ViewerPerspectiveCameraFrame& cameraFrame() const noexcept;
    void setCpuStatistics(const nr::renderer::RendererCpuStatistics& statistics) noexcept;
    [[nodiscard]] const nr::renderer::RendererCpuStatistics& cpuStatistics() const noexcept;
    void setGpuPassStatistics(const nr::renderer::RendererGpuPassStatistics& statistics) noexcept;
    [[nodiscard]] const nr::renderer::RendererGpuPassStatistics& gpuPassStatistics() const noexcept;
    [[nodiscard]] UiCaptureState captureState() const noexcept;
    [[nodiscard]] std::optional<std::reference_wrapper<const ImDrawData>> drawData() const noexcept;

  private:
    void setCurrentContext() const noexcept;
    void endWindow(bool closesWindow);
    [[nodiscard]] bool beginSection(std::string_view id, std::string_view title, bool defaultOpen = true);
    void prepareWindowDefaults();

    ImGuiContext* context_ = nullptr;
    bool frameActive_ = false;
    bool frameFinalized_ = false;
    UiCaptureState captureState_{};
    UiFrameStats frameStats_{};
    nr::renderer::ViewerPerspectiveCameraFrame cameraFrame_{};
    nr::renderer::RendererCpuStatistics cpuStatistics_{};
    nr::renderer::RendererGpuPassStatistics gpuPassStatistics_{};
    std::vector<UiSection> queuedSections_{};
    std::uint32_t windowsOpenedThisFrame_ = 0u;
    float fpsSampleAccumulatedDeltaSeconds_ = 0.0f;
    std::uint32_t fpsSampleFrameCount_ = 0u;
    bool unifiedWindowOpen_ = false;
    bool unifiedWindowVisible_ = false;
    std::uint32_t unifiedWindowSectionCount_ = 0u;
};
} // namespace nr::app
