module nr.app;
import dependency.ui;

import :ui;
import nr.renderer;
import nr.rhi;
import nr.utils;
import std;

namespace nr::app::detail
{
inline constexpr int kMouseButtonLeft = 0;
inline constexpr int kMouseButtonRight = 1;
inline constexpr int kMouseButtonMiddle = 2;
inline constexpr float kUiWindowMargin = 16.0f;
inline constexpr float kUiWindowDefaultWidth = 360.0f;
inline constexpr float kUiWindowDefaultHeight = 160.0f;
inline constexpr float kUiWindowVerticalStride = 184.0f;
inline constexpr std::string_view kUnifiedUiWindowTitle = "Renderer Controls";

[[nodiscard]] float sanitizeUiDeltaSeconds(float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
    {
        return 1.0f / 60.0f;
    }

    return std::min(deltaSeconds, 0.5f);
}
} // namespace nr::app::detail

namespace nr::app
{
UiSystem::WindowScope::WindowScope(UiSystem& owner, bool visible, bool closesWindow) noexcept
    : owner_(std::ref(owner))
    , visible_(visible)
    , closesWindow_(closesWindow)
{
}

UiSystem::WindowScope::WindowScope(WindowScope&& other) noexcept
    : owner_(other.owner_)
    , visible_(other.visible_)
    , closesWindow_(other.closesWindow_)
{
    other.owner_.reset();
    other.visible_ = false;
    other.closesWindow_ = false;
}

UiSystem::WindowScope& UiSystem::WindowScope::operator=(WindowScope&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    close();
    owner_ = other.owner_;
    visible_ = other.visible_;
    closesWindow_ = other.closesWindow_;
    other.owner_.reset();
    other.visible_ = false;
    other.closesWindow_ = false;
    return *this;
}

UiSystem::WindowScope::~WindowScope()
{
    close();
}

UiSystem::WindowScope::operator bool() const noexcept
{
    return visible_;
}

void UiSystem::WindowScope::close() noexcept
{
    if (!owner_.has_value())
    {
        return;
    }

    owner_->get().endWindow(closesWindow_);
    owner_.reset();
    visible_ = false;
    closesWindow_ = false;
}

UiSystem::~UiSystem()
{
    shutdown();
}

void UiSystem::initialize()
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

void UiSystem::shutdown()
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
    cpuStatistics_ = {};
    gpuPassStatistics_ = {};
    fpsSampleAccumulatedDeltaSeconds_ = 0.0f;
    fpsSampleFrameCount_ = 0u;
    unifiedWindowOpen_ = false;
    unifiedWindowVisible_ = false;
    unifiedWindowSectionCount_ = 0u;
}

bool UiSystem::initialized() const noexcept
{
    return context_ != nullptr;
}

void UiSystem::beginFrame(const nr::rhi::PresentationContext& presentation, float deltaSeconds)
{
    nrAssert(initialized(), "UiSystem::beginFrame requires initialize() first.");

    setCurrentContext();
    if (frameActive_ && !frameFinalized_)
    {
        ImGui::EndFrame();
    }

    auto const sanitizedDelta = detail::sanitizeUiDeltaSeconds(deltaSeconds);
    frameStats_.deltaSeconds = sanitizedDelta;
    fpsSampleAccumulatedDeltaSeconds_ += sanitizedDelta;
    ++fpsSampleFrameCount_;
    if (fpsSampleFrameCount_ >= nr::statisticsSampleFrameCount)
    {
        auto const averagedDeltaSeconds =
            fpsSampleAccumulatedDeltaSeconds_ / static_cast<float>(fpsSampleFrameCount_);
        frameStats_.smoothedDeltaSeconds = averagedDeltaSeconds;
        frameStats_.frameTimeMilliseconds = averagedDeltaSeconds * 1000.0f;
        frameStats_.framesPerSecond = averagedDeltaSeconds > 0.0f
                                          ? 1.0f / averagedDeltaSeconds
                                          : 0.0f;
        fpsSampleAccumulatedDeltaSeconds_ = 0.0f;
        fpsSampleFrameCount_ = 0u;
    }
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
    io.AddMouseButtonEvent(0, presentation.mouseButtonDown(detail::kMouseButtonLeft));
    io.AddMouseButtonEvent(1, presentation.mouseButtonDown(detail::kMouseButtonRight));
    io.AddMouseButtonEvent(2, presentation.mouseButtonDown(detail::kMouseButtonMiddle));

    ImGui::NewFrame();
    frameActive_ = true;
    frameFinalized_ = false;
    windowsOpenedThisFrame_ = 0u;
    unifiedWindowOpen_ = false;
    unifiedWindowVisible_ = false;
    unifiedWindowSectionCount_ = 0u;
}

void UiSystem::finalizeFrame()
{
    if (!frameActive_ || frameFinalized_)
    {
        return;
    }

    setCurrentContext();
    if (unifiedWindowOpen_)
    {
        ImGui::End();
        unifiedWindowOpen_ = false;
        unifiedWindowVisible_ = false;
        unifiedWindowSectionCount_ = 0u;
    }
    ImGui::Render();

    auto& io = ImGui::GetIO();
    captureState_ = UiCaptureState{
        .wantsMouse = io.WantCaptureMouse,
        .wantsKeyboard = io.WantCaptureKeyboard,
    };

    frameActive_ = false;
    frameFinalized_ = true;
}

UiSystem::WindowScope UiSystem::window(std::string_view title, ImGuiWindowFlags flags)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::window requires an active UI frame.");
    setCurrentContext();

    if (!unifiedWindowOpen_)
    {
        prepareWindowDefaults();
        auto const visible = ImGui::Begin(detail::kUnifiedUiWindowTitle.data(), nullptr, flags);
        unifiedWindowVisible_ = visible;
        unifiedWindowOpen_ = true;
    }

    auto const sectionVisible = beginSection(title, title);
    return WindowScope{*this, sectionVisible, false};
}

void UiSystem::renderSections(std::span<const UiSection> sections, ImGuiWindowFlags flags)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::renderSections requires an active UI frame.");
    setCurrentContext();

    if (sections.empty())
    {
        return;
    }

    if (!unifiedWindowOpen_)
    {
        prepareWindowDefaults();
        auto const visible = ImGui::Begin(detail::kUnifiedUiWindowTitle.data(), nullptr, flags);
        unifiedWindowVisible_ = visible;
        unifiedWindowOpen_ = true;
    }

    if (!unifiedWindowVisible_)
    {
        return;
    }

    std::ranges::for_each(sections, [&](const UiSection& section) {
        if (!beginSection(section.id, section.title) || !section.draw)
        {
            return;
        }

        ImGui::Indent();
        section.draw(*this);
        ImGui::Unindent();
        ImGui::Spacing();
    });
}

void UiSystem::separator()
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::separator requires an active UI frame.");
    setCurrentContext();
    ImGui::Separator();
}

void UiSystem::text(std::string_view content)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::text requires an active UI frame.");
    setCurrentContext();
    ImGui::TextUnformatted(content.data(), content.data() + content.size());
}

bool UiSystem::checkbox(std::string_view label, bool& value)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::checkbox requires an active UI frame.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Checkbox(ownedLabel.c_str(), &value);
}

const UiFrameStats& UiSystem::stats() const noexcept
{
    return frameStats_;
}

void UiSystem::setCpuStatistics(const nr::renderer::RendererCpuStatistics& statistics) noexcept
{
    cpuStatistics_ = statistics;
}

const nr::renderer::RendererCpuStatistics& UiSystem::cpuStatistics() const noexcept
{
    return cpuStatistics_;
}

void UiSystem::setGpuPassStatistics(const nr::renderer::RendererGpuPassStatistics& statistics) noexcept
{
    gpuPassStatistics_ = statistics;
}

const nr::renderer::RendererGpuPassStatistics& UiSystem::gpuPassStatistics() const noexcept
{
    return gpuPassStatistics_;
}

UiCaptureState UiSystem::captureState() const noexcept
{
    return captureState_;
}

std::optional<std::reference_wrapper<const ImDrawData>> UiSystem::drawData() const noexcept
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

void UiSystem::setCurrentContext() const noexcept
{
    ImGui::SetCurrentContext(context_);
}

void UiSystem::endWindow(bool closesWindow)
{
    if (!closesWindow)
    {
        return;
    }

    setCurrentContext();
    ImGui::End();
}

bool UiSystem::beginSection(std::string_view id, std::string_view title)
{
    if (!unifiedWindowVisible_ || title.empty())
    {
        return false;
    }

    if (unifiedWindowSectionCount_ > 0u)
    {
        ImGui::Spacing();
    }

    ++unifiedWindowSectionCount_;
    auto label = std::string{title};
    if (!id.empty() && id != title)
    {
        label += "##";
        label += id;
    }

    return ImGui::CollapsingHeader(label.c_str(), ImGuiTreeNodeFlags_None);
}

void UiSystem::prepareWindowDefaults()
{
    auto& io = ImGui::GetIO();
    auto const displayWidth = std::max(io.DisplaySize.x, detail::kUiWindowDefaultWidth + detail::kUiWindowMargin * 2.0f);
    auto const displayHeight = std::max(io.DisplaySize.y, detail::kUiWindowDefaultHeight + detail::kUiWindowMargin * 2.0f);

    auto const columnIndex = windowsOpenedThisFrame_ / 4u;
    auto const rowIndex = windowsOpenedThisFrame_ % 4u;
    auto const positionX = std::min(
        displayWidth - detail::kUiWindowDefaultWidth - detail::kUiWindowMargin,
        detail::kUiWindowMargin + static_cast<float>(columnIndex) * (detail::kUiWindowDefaultWidth + detail::kUiWindowMargin));
    auto const positionY = std::min(
        displayHeight - detail::kUiWindowDefaultHeight - detail::kUiWindowMargin,
        detail::kUiWindowMargin + static_cast<float>(rowIndex) * detail::kUiWindowVerticalStride);

    ImGui::SetNextWindowPos(ImVec2{positionX, positionY}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(
        ImVec2{detail::kUiWindowDefaultWidth, detail::kUiWindowDefaultHeight},
        ImGuiCond_FirstUseEver);
    ++windowsOpenedThisFrame_;
}
} // namespace nr::app
