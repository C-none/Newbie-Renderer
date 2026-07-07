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
inline constexpr int kKeySpace = 32;
inline constexpr int kKeyA = 'A';
inline constexpr int kKeyC = 'C';
inline constexpr int kKeyV = 'V';
inline constexpr int kKeyX = 'X';
inline constexpr int kKeyY = 'Y';
inline constexpr int kKeyZ = 'Z';
inline constexpr int kKeyEscape = 256;
inline constexpr int kKeyEnter = 257;
inline constexpr int kKeyTab = 258;
inline constexpr int kKeyBackspace = 259;
inline constexpr int kKeyInsert = 260;
inline constexpr int kKeyDelete = 261;
inline constexpr int kKeyRight = 262;
inline constexpr int kKeyLeft = 263;
inline constexpr int kKeyDown = 264;
inline constexpr int kKeyUp = 265;
inline constexpr int kKeyPageUp = 266;
inline constexpr int kKeyPageDown = 267;
inline constexpr int kKeyHome = 268;
inline constexpr int kKeyEnd = 269;
inline constexpr int kKeyLeftShift = 340;
inline constexpr int kKeyLeftControl = 341;
inline constexpr int kKeyLeftAlt = 342;
inline constexpr int kKeyLeftSuper = 343;
inline constexpr int kKeyRightShift = 344;
inline constexpr int kKeyRightControl = 345;
inline constexpr int kKeyRightAlt = 346;
inline constexpr int kKeyRightSuper = 347;
inline constexpr float kUiWindowMargin = 16.0f;
inline constexpr float kUiWindowDefaultWidth = 720.0f;
inline constexpr float kUiWindowDefaultHeight = 960.0f;
inline constexpr float kUiWindowVerticalStride = 20.0f;
inline constexpr float kUiFontGlobalScale = 1.8f;
inline constexpr std::string_view kUnifiedUiWindowTitle = "Renderer Controls";

struct UiKeyBinding
{
    ImGuiKey key{};
    int glfwKey = 0;
};

inline constexpr std::array kUiKeyBindings{
    UiKeyBinding{.key = imgui::keyTab, .glfwKey = kKeyTab},
    UiKeyBinding{.key = imgui::keyLeftArrow, .glfwKey = kKeyLeft},
    UiKeyBinding{.key = imgui::keyRightArrow, .glfwKey = kKeyRight},
    UiKeyBinding{.key = imgui::keyUpArrow, .glfwKey = kKeyUp},
    UiKeyBinding{.key = imgui::keyDownArrow, .glfwKey = kKeyDown},
    UiKeyBinding{.key = imgui::keyPageUp, .glfwKey = kKeyPageUp},
    UiKeyBinding{.key = imgui::keyPageDown, .glfwKey = kKeyPageDown},
    UiKeyBinding{.key = imgui::keyHome, .glfwKey = kKeyHome},
    UiKeyBinding{.key = imgui::keyEnd, .glfwKey = kKeyEnd},
    UiKeyBinding{.key = imgui::keyInsert, .glfwKey = kKeyInsert},
    UiKeyBinding{.key = imgui::keyDelete, .glfwKey = kKeyDelete},
    UiKeyBinding{.key = imgui::keyBackspace, .glfwKey = kKeyBackspace},
    UiKeyBinding{.key = imgui::keySpace, .glfwKey = kKeySpace},
    UiKeyBinding{.key = imgui::keyEnter, .glfwKey = kKeyEnter},
    UiKeyBinding{.key = imgui::keyEscape, .glfwKey = kKeyEscape},
    UiKeyBinding{.key = imgui::keyLeftCtrl, .glfwKey = kKeyLeftControl},
    UiKeyBinding{.key = imgui::keyLeftShift, .glfwKey = kKeyLeftShift},
    UiKeyBinding{.key = imgui::keyLeftAlt, .glfwKey = kKeyLeftAlt},
    UiKeyBinding{.key = imgui::keyLeftSuper, .glfwKey = kKeyLeftSuper},
    UiKeyBinding{.key = imgui::keyRightCtrl, .glfwKey = kKeyRightControl},
    UiKeyBinding{.key = imgui::keyRightShift, .glfwKey = kKeyRightShift},
    UiKeyBinding{.key = imgui::keyRightAlt, .glfwKey = kKeyRightAlt},
    UiKeyBinding{.key = imgui::keyRightSuper, .glfwKey = kKeyRightSuper},
    UiKeyBinding{.key = imgui::keyA, .glfwKey = kKeyA},
    UiKeyBinding{.key = imgui::keyC, .glfwKey = kKeyC},
    UiKeyBinding{.key = imgui::keyV, .glfwKey = kKeyV},
    UiKeyBinding{.key = imgui::keyX, .glfwKey = kKeyX},
    UiKeyBinding{.key = imgui::keyY, .glfwKey = kKeyY},
    UiKeyBinding{.key = imgui::keyZ, .glfwKey = kKeyZ},
};

[[nodiscard]] float sanitizeUiDeltaSeconds(float deltaSeconds) noexcept
{
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f)
    {
        return 1.0f / 60.0f;
    }

    return std::min(deltaSeconds, 0.5f);
}

[[nodiscard]] float compactUIntInputWidth(std::uint32_t maxValue)
{
    auto const digitCount = std::to_string(maxValue).size();
    return std::clamp(40.0f + static_cast<float>(digitCount) * 8.0f, 64.0f, 140.0f);
}

int resizeInputTextBuffer(ImGuiInputTextCallbackData* data)
{
    nrAssert(data != nullptr, "resizeInputTextBuffer requires ImGui callback data.");
    nrAssert(data->EventFlag == ImGuiInputTextFlags_CallbackResize, "resizeInputTextBuffer only handles resize callbacks.");

    auto* value = static_cast<std::string*>(data->UserData);
    nrAssert(value != nullptr, "resizeInputTextBuffer requires a string user data pointer.");
    value->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = value->data();
    return 0;
}

void submitKeyboardInput(auto& io, const nr::rhi::PresentationContext& presentation)
{
    auto const controlDown = presentation.keyDown(kKeyLeftControl) ||
                             presentation.keyDown(kKeyRightControl);
    auto const shiftDown = presentation.keyDown(kKeyLeftShift) ||
                           presentation.keyDown(kKeyRightShift);
    auto const altDown = presentation.keyDown(kKeyLeftAlt) ||
                         presentation.keyDown(kKeyRightAlt);
    auto const superDown = presentation.keyDown(kKeyLeftSuper) ||
                           presentation.keyDown(kKeyRightSuper);

    io.AddKeyEvent(imgui::keyModCtrl, controlDown);
    io.AddKeyEvent(imgui::keyModShift, shiftDown);
    io.AddKeyEvent(imgui::keyModAlt, altDown);
    io.AddKeyEvent(imgui::keyModSuper, superDown);

    std::ranges::for_each(kUiKeyBindings, [&](const UiKeyBinding& binding) {
        io.AddKeyEvent(binding.key, presentation.keyDown(binding.glfwKey));
    });

    auto codepoints = presentation.consumeTextInputCodepoints();
    std::ranges::for_each(codepoints, [&](std::uint32_t codepoint) {
        io.AddInputCharacter(codepoint);
    });
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
    io.FontGlobalScale = detail::kUiFontGlobalScale;

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
    cameraFrame_ = {};
    cpuStatistics_ = {};
    gpuPassStatistics_ = {};
    fpsSampleAccumulatedDeltaSeconds_ = 0.0f;
    fpsSampleFrameCount_ = 0u;
    unifiedWindowOpen_ = false;
    unifiedWindowVisible_ = false;
    unifiedWindowSectionCount_ = 0u;
    queuedSections_.clear();
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
    detail::submitKeyboardInput(io, presentation);

    ImGui::NewFrame();
    frameActive_ = true;
    frameFinalized_ = false;
    windowsOpenedThisFrame_ = 0u;
    unifiedWindowOpen_ = false;
    unifiedWindowVisible_ = false;
    unifiedWindowSectionCount_ = 0u;
    queuedSections_.clear();
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
    queuedSections_.clear();
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

void UiSystem::queueSection(UiSection section)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::queueSection requires an active UI frame.");
    queuedSections_.push_back(std::move(section));
}

void UiSystem::renderSections(std::span<const UiSection> sections, ImGuiWindowFlags flags)
{
    renderSections(
        sections,
        std::span<const nr::renderer::NodeUiSection>{},
        std::span<const UiSection>{},
        flags);
}

void UiSystem::renderSections(
    std::span<const UiSection> leadingSections,
    std::span<const UiSection> trailingSections,
    ImGuiWindowFlags flags)
{
    renderSections(
        leadingSections,
        std::span<const nr::renderer::NodeUiSection>{},
        trailingSections,
        flags);
}

void UiSystem::renderSections(
    std::span<const UiSection> leadingSections,
    std::span<const nr::renderer::NodeUiSection> nodeSections,
    std::span<const UiSection> trailingSections,
    ImGuiWindowFlags flags)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::renderSections requires an active UI frame.");
    setCurrentContext();

    if (leadingSections.empty() && queuedSections_.empty() && nodeSections.empty() && trailingSections.empty())
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

    auto drawAppSection = [&](const UiSection& section) {
        if (!beginSection(section.id, section.title, section.defaultOpen) || !section.draw)
        {
            return;
        }

        ImGui::Indent();
        section.draw(*this);
        ImGui::Unindent();
        ImGui::Spacing();
    };

    auto drawNodeSection = [&](const nr::renderer::NodeUiSection& section) {
        if (!beginSection(section.id, section.title, section.defaultOpen) || !section.draw)
        {
            return;
        }

        ImGui::Indent();
        section.draw(*this);
        ImGui::Unindent();
        ImGui::Spacing();
    };

    auto drawAppSections = [&](std::span<const UiSection> sectionSpan) {
        std::ranges::for_each(sectionSpan, drawAppSection);
    };

    drawAppSections(leadingSections);
    std::ranges::for_each(queuedSections_, drawAppSection);
    std::ranges::for_each(nodeSections, drawNodeSection);
    drawAppSections(trailingSections);
    queuedSections_.clear();
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

bool UiSystem::button(std::string_view label)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::button requires an active UI frame.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Button(ownedLabel.c_str());
}

bool UiSystem::inputText(std::string_view label, std::string& value)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::inputText requires an active UI frame.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::InputText(
        ownedLabel.c_str(),
        value.data(),
        value.capacity() + 1u,
        ImGuiInputTextFlags_CallbackResize,
        detail::resizeInputTextBuffer,
        std::addressof(value));
}

bool UiSystem::beginCombo(std::string_view label, std::string_view preview)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::beginCombo requires an active UI frame.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const ownedPreview = std::string{preview};
    return ImGui::BeginCombo(ownedLabel.c_str(), ownedPreview.c_str());
}

void UiSystem::endCombo()
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::endCombo requires an active UI frame.");
    setCurrentContext();
    ImGui::EndCombo();
}

bool UiSystem::selectable(std::string_view label, bool selected)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::selectable requires an active UI frame.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Selectable(ownedLabel.c_str(), selected);
}

bool UiSystem::sliderFloat(std::string_view label, float& value, float minValue, float maxValue)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::sliderFloat requires an active UI frame.");
    nrAssert(minValue <= maxValue, "UiSystem::sliderFloat requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::SliderFloat(ownedLabel.c_str(), &value, minValue, maxValue);
}

bool UiSystem::inputFloat(std::string_view label, float& value, float minValue, float maxValue)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::inputFloat requires an active UI frame.");
    nrAssert(minValue <= maxValue, "UiSystem::inputFloat requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const inputId = std::format("##{}", ownedLabel);
    auto inputValue = std::clamp(value, minValue, maxValue);
    ImGui::TextUnformatted(ownedLabel.data(), ownedLabel.data() + ownedLabel.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    auto const changed = ImGui::InputFloat(inputId.c_str(), std::addressof(inputValue));
    value = std::clamp(inputValue, minValue, maxValue);
    return changed;
}

bool UiSystem::inputInt32(
    std::string_view label,
    std::int32_t& value,
    std::int32_t minValue,
    std::int32_t maxValue)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::inputInt32 requires an active UI frame.");
    nrAssert(minValue <= maxValue, "UiSystem::inputInt32 requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const inputId = std::format("##{}", ownedLabel);
    auto inputValue = std::clamp(value, minValue, maxValue);
    ImGui::TextUnformatted(ownedLabel.data(), ownedLabel.data() + ownedLabel.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    auto const changed = ImGui::InputScalar(
        inputId.c_str(),
        ImGuiDataType_S32,
        std::addressof(inputValue),
        nullptr,
        nullptr,
        "%d",
        ImGuiInputTextFlags_CharsDecimal);
    value = std::clamp(inputValue, minValue, maxValue);
    return changed;
}

bool UiSystem::inputUInt(
    std::string_view label,
    std::uint32_t& value,
    std::uint32_t minValue,
    std::uint32_t maxValue)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::inputUInt requires an active UI frame.");
    nrAssert(minValue <= maxValue, "UiSystem::inputUInt requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const inputId = std::format("##{}", ownedLabel);
    auto inputValue = std::clamp(value, minValue, maxValue);
    ImGui::TextUnformatted(ownedLabel.data(), ownedLabel.data() + ownedLabel.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(detail::compactUIntInputWidth(maxValue));
    auto const changed = ImGui::InputScalar(
        inputId.c_str(),
        ImGuiDataType_U32,
        std::addressof(inputValue),
        nullptr,
        nullptr,
        "%u",
        ImGuiInputTextFlags_CharsDecimal);
    value = std::clamp(inputValue, minValue, maxValue);
    return changed;
}

bool UiSystem::sliderUInt(
    std::string_view label,
    std::uint32_t& value,
    std::uint32_t minValue,
    std::uint32_t maxValue)
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::sliderUInt requires an active UI frame.");
    nrAssert(minValue <= maxValue, "UiSystem::sliderUInt requires minValue <= maxValue.");
    nrAssert(maxValue <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
             "UiSystem::sliderUInt range exceeds ImGui SliderInt capacity.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto intValue = static_cast<int>(std::clamp(value, minValue, maxValue));
    auto const intMin = static_cast<int>(minValue);
    auto const intMax = static_cast<int>(maxValue);
    auto const changed = ImGui::SliderInt(ownedLabel.c_str(), std::addressof(intValue), intMin, intMax);
    value = static_cast<std::uint32_t>(std::clamp(intValue, intMin, intMax));
    return changed;
}

void UiSystem::setItemDefaultFocus()
{
    nrAssert(frameActive_ && !frameFinalized_, "UiSystem::setItemDefaultFocus requires an active UI frame.");
    setCurrentContext();
    ImGui::SetItemDefaultFocus();
}

const UiFrameStats& UiSystem::stats() const noexcept
{
    return frameStats_;
}

void UiSystem::setCameraFrame(const nr::renderer::ViewerPerspectiveCameraFrame& cameraFrame) noexcept
{
    cameraFrame_ = cameraFrame;
}

const nr::renderer::ViewerPerspectiveCameraFrame& UiSystem::cameraFrame() const noexcept
{
    return cameraFrame_;
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

bool UiSystem::beginSection(std::string_view id, std::string_view title, bool defaultOpen)
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

    auto const flags = defaultOpen
                           ? ImGuiTreeNodeFlags_DefaultOpen
                           : ImGuiTreeNodeFlags_None;
    return ImGui::CollapsingHeader(label.c_str(), flags);
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
