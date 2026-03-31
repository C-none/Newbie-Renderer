module;
export module nr.app:ui;

import dependency;
import nr.rhi;
import nr.utils;
import std;

namespace
{
inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;
inline constexpr int kMouseButtonMiddle = 2;
inline constexpr float kUiWindowMargin = 16.0f;
inline constexpr float kUiWindowDefaultWidth = 360.0f;
inline constexpr float kUiWindowDefaultHeight = 160.0f;
inline constexpr float kUiWindowVerticalStride = 184.0f;

[[nodiscard]] float sanitizeUiDeltaSeconds(float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
    {
        return 1.0f / 60.0f;
    }

    return std::clamp(deltaSeconds, 1.0f / 240.0f, 0.1f);
}
} // namespace

export namespace nr::app
{
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

class UiSystem
{
  public:
    class WindowScope
    {
      public:
        WindowScope() = default;
        WindowScope(UiSystem& owner, bool visible) noexcept
            : owner_(std::ref(owner))
            , visible_(visible)
        {
        }

        WindowScope(const WindowScope&) = delete;
        WindowScope& operator=(const WindowScope&) = delete;

        WindowScope(WindowScope&& other) noexcept
            : owner_(other.owner_)
            , visible_(other.visible_)
        {
            other.owner_.reset();
            other.visible_ = false;
        }

        WindowScope& operator=(WindowScope&& other) noexcept
        {
            if (this == &other)
            {
                return *this;
            }

            close();
            owner_ = other.owner_;
            visible_ = other.visible_;
            other.owner_.reset();
            other.visible_ = false;
            return *this;
        }

        ~WindowScope()
        {
            close();
        }

        [[nodiscard]] explicit operator bool() const noexcept
        {
            return visible_;
        }

      private:
        void close() noexcept
        {
            if (!owner_.has_value())
            {
                return;
            }

            owner_->get().endWindow();
            owner_.reset();
            visible_ = false;
        }

        std::optional<std::reference_wrapper<UiSystem>> owner_{};
        bool visible_ = false;
    };

    UiSystem() = default;
    UiSystem(const UiSystem&) = delete;
    UiSystem& operator=(const UiSystem&) = delete;
    UiSystem(UiSystem&&) = delete;
    UiSystem& operator=(UiSystem&&) = delete;

    ~UiSystem()
    {
        shutdown();
    }

    void initialize()
    {
        if (initialized())
        {
            return;
        }

        context_ = ImGui::CreateContext();
        nrAssert(context_ != nullptr, "UiSystem::initialize failed to create ImGui context.");

        setCurrentContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.BackendPlatformName = "NewbieRenderer.UiSystem";
        io.BackendRendererName = "NewbieRenderer.UiNode";
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

        ImGui::StyleColorsDark();
    }

    void shutdown()
    {
        if (!initialized())
        {
            return;
        }

        setCurrentContext();
        if (frameActive_ && !frameFinalized_)
        {
            ImGui::EndFrame();
        }

        ImGui::DestroyContext(context_);
        context_ = nullptr;
        frameActive_ = false;
        frameFinalized_ = false;
        captureState_ = {};
        frameStats_ = {};
    }

    [[nodiscard]] bool initialized() const noexcept
    {
        return context_ != nullptr;
    }

    void beginFrame(const nr::rhi::PresentationContext& presentation, float deltaSeconds)
    {
        nrAssert(initialized(), "UiSystem::beginFrame requires initialize() first.");

        setCurrentContext();
        if (frameActive_ && !frameFinalized_)
        {
            ImGui::EndFrame();
        }

        auto const sanitizedDelta = sanitizeUiDeltaSeconds(deltaSeconds);
        frameStats_.deltaSeconds = sanitizedDelta;
        frameStats_.smoothedDeltaSeconds = frameStats_.frameCounter == 0
                                              ? sanitizedDelta
                                              : std::lerp(frameStats_.smoothedDeltaSeconds, sanitizedDelta, 0.1f);
        frameStats_.frameTimeMilliseconds = frameStats_.smoothedDeltaSeconds * 1000.0f;
        frameStats_.framesPerSecond = frameStats_.smoothedDeltaSeconds > 0.0f
                                          ? 1.0f / frameStats_.smoothedDeltaSeconds
                                          : 0.0f;
        ++frameStats_.frameCounter;

        auto& io = ImGui::GetIO();
        auto const swapchainExtent = presentation.swapchainExtent();
        io.DisplaySize = ImVec2{
            static_cast<float>(std::max(1u, swapchainExtent.width)),
            static_cast<float>(std::max(1u, swapchainExtent.height)),
        };
        io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};
        io.DeltaTime = sanitizedDelta;

        auto const cursorPosition = presentation.cursorPosition();
        io.AddMousePosEvent(
            static_cast<float>(cursorPosition.x),
            static_cast<float>(cursorPosition.y));
        io.AddMouseButtonEvent(0, presentation.mouseButtonDown(kMouseButtonLeft));
        io.AddMouseButtonEvent(1, presentation.mouseButtonDown(kMouseButtonRight));
        io.AddMouseButtonEvent(2, presentation.mouseButtonDown(kMouseButtonMiddle));

        ImGui::NewFrame();
        frameActive_ = true;
        frameFinalized_ = false;
        windowsOpenedThisFrame_ = 0u;
    }

    void finalizeFrame()
    {
        if (!frameActive_ || frameFinalized_)
        {
            return;
        }

        setCurrentContext();
        ImGui::Render();

        auto& io = ImGui::GetIO();
        captureState_ = UiCaptureState{
            .wantsMouse = io.WantCaptureMouse,
            .wantsKeyboard = io.WantCaptureKeyboard,
        };

        frameActive_ = false;
        frameFinalized_ = true;
    }

    [[nodiscard]] WindowScope window(std::string_view title, ImGuiWindowFlags flags = 0)
    {
        nrAssert(frameActive_ && !frameFinalized_, "UiSystem::window requires an active UI frame.");
        setCurrentContext();

        prepareWindowDefaults();

        auto const ownedTitle = std::string{title};
        auto const visible = ImGui::Begin(ownedTitle.c_str(), nullptr, flags);
        return WindowScope{*this, visible};
    }

    void separator()
    {
        nrAssert(frameActive_ && !frameFinalized_, "UiSystem::separator requires an active UI frame.");
        setCurrentContext();
        ImGui::Separator();
    }

    void text(std::string_view content)
    {
        nrAssert(frameActive_ && !frameFinalized_, "UiSystem::text requires an active UI frame.");
        setCurrentContext();
        ImGui::TextUnformatted(content.data(), content.data() + content.size());
    }

    template <typename... TArgs>
    void textFmt(std::format_string<TArgs...> format, TArgs&&... args)
    {
        text(std::format(format, std::forward<TArgs>(args)...));
    }

    [[nodiscard]] bool checkbox(std::string_view label, bool& value)
    {
        nrAssert(frameActive_ && !frameFinalized_, "UiSystem::checkbox requires an active UI frame.");
        setCurrentContext();

        auto const ownedLabel = std::string{label};
        return ImGui::Checkbox(ownedLabel.c_str(), &value);
    }

    [[nodiscard]] const UiFrameStats& stats() const noexcept
    {
        return frameStats_;
    }

    [[nodiscard]] UiCaptureState captureState() const noexcept
    {
        return captureState_;
    }

    [[nodiscard]] std::optional<std::reference_wrapper<const ImDrawData>> drawData() const noexcept
    {
        if (!frameFinalized_)
        {
            return std::nullopt;
        }

        setCurrentContext();
        auto* drawData = ImGui::GetDrawData();
        if (drawData == nullptr)
        {
            return std::nullopt;
        }

        return std::cref(*drawData);
    }

  private:
    void setCurrentContext() const noexcept
    {
        ImGui::SetCurrentContext(context_);
    }

    void endWindow()
    {
        setCurrentContext();
        ImGui::End();
    }

    void prepareWindowDefaults()
    {
        auto& io = ImGui::GetIO();
        auto const displayWidth = std::max(io.DisplaySize.x, kUiWindowDefaultWidth + kUiWindowMargin * 2.0f);
        auto const displayHeight = std::max(io.DisplaySize.y, kUiWindowDefaultHeight + kUiWindowMargin * 2.0f);

        auto const columnIndex = windowsOpenedThisFrame_ / 4u;
        auto const rowIndex = windowsOpenedThisFrame_ % 4u;
        auto const positionX = std::min(
            displayWidth - kUiWindowDefaultWidth - kUiWindowMargin,
            kUiWindowMargin + static_cast<float>(columnIndex) * (kUiWindowDefaultWidth + kUiWindowMargin));
        auto const positionY = std::min(
            displayHeight - kUiWindowDefaultHeight - kUiWindowMargin,
            kUiWindowMargin + static_cast<float>(rowIndex) * kUiWindowVerticalStride);

        ImGui::SetNextWindowPos(ImVec2{positionX, positionY}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(
            ImVec2{kUiWindowDefaultWidth, kUiWindowDefaultHeight},
            ImGuiCond_FirstUseEver);
        ++windowsOpenedThisFrame_;
    }

    ImGuiContext* context_ = nullptr;
    bool frameActive_ = false;
    bool frameFinalized_ = false;
    UiCaptureState captureState_{};
    UiFrameStats frameStats_{};
    std::uint32_t windowsOpenedThisFrame_ = 0u;
};
} // namespace nr::app
