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

int resizeInputTextBuffer(ImGuiInputTextCallbackData *data)
{
    nrAssert(data != nullptr, "resizeInputTextBuffer requires ImGui callback data.");
    nrAssert(data->EventFlag == ImGuiInputTextFlags_CallbackResize,
             "resizeInputTextBuffer only handles resize callbacks.");

    auto *value = static_cast<std::string *>(data->UserData);
    nrAssert(value != nullptr, "resizeInputTextBuffer requires a string user data pointer.");
    value->resize(static_cast<std::size_t>(data->BufTextLen));
    data->Buf = value->data();
    return 0;
}

void submitKeyboardInput(auto &io, nr::rhi::WindowInput &input)
{
    auto const controlDown = input.keyDown(kKeyLeftControl) || input.keyDown(kKeyRightControl);
    auto const shiftDown = input.keyDown(kKeyLeftShift) || input.keyDown(kKeyRightShift);
    auto const altDown = input.keyDown(kKeyLeftAlt) || input.keyDown(kKeyRightAlt);
    auto const superDown = input.keyDown(kKeyLeftSuper) || input.keyDown(kKeyRightSuper);

    io.AddKeyEvent(imgui::keyModCtrl, controlDown);
    io.AddKeyEvent(imgui::keyModShift, shiftDown);
    io.AddKeyEvent(imgui::keyModAlt, altDown);
    io.AddKeyEvent(imgui::keyModSuper, superDown);

    std::ranges::for_each(kUiKeyBindings,
                          [&](const UiKeyBinding &binding) { io.AddKeyEvent(binding.key, input.keyDown(binding.glfwKey)); });

    auto codepoints = input.consumeTextInputCodepoints();
    std::ranges::for_each(codepoints, [&](std::uint32_t codepoint) { io.AddInputCharacter(codepoint); });
}

void drawCpuTimingLine(nr::app::UiSystem &ui, std::string_view label, double milliseconds)
{
    ui.textFmt("{}: {:.3f} ms", label, milliseconds);
}

[[nodiscard]] std::string_view queueDomainLabel(nr::renderer::QueueDomain queue) noexcept
{
    if (queue == nr::renderer::QueueDomain::Graphics)
    {
        return "Graphics";
    }
    if (queue == nr::renderer::QueueDomain::Compute)
    {
        return "Compute";
    }
    return "Transfer";
}

void drawGpuPassTimingLine(nr::app::UiSystem &ui, const nr::renderer::RendererGpuPassAverage &timing,
                           std::uint32_t averagedFrameCount)
{
    auto const passName = timing.debugName.empty() ? std::format("Pass {}", timing.pass.value) : timing.debugName;
    auto const passKind = timing.isCopyPass ? std::string_view{"Copy"} : queueDomainLabel(timing.queue);
    if (timing.sampleCount == averagedFrameCount)
    {
        ui.textFmt("{} [{}]: {:.3f} ms", passName, passKind, timing.milliseconds);
        return;
    }

    ui.textFmt("{} [{}]: {:.3f} ms ({} samples)", passName, passKind, timing.milliseconds, timing.sampleCount);
}

} // namespace nr::app::detail

namespace nr::app
{
void UiSystem::ImGuiContextDeleter::operator()(ImGuiContext *context) const noexcept
{
    ImGui::DestroyContext(context);
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

    context_.reset(ImGui::CreateContext());
    nrAssert(context_ != nullptr, "UiSystem::initialize failed to create ImGui context.");

    setCurrentContext();
    auto &io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.BackendPlatformName = "NewbieRenderer.UiSystem";
    io.BackendRendererName = "NewbieRenderer.UiNode";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.FontAllowUserScaling = false;
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
    if (frameState_ == FrameState::active)
    {
        ImGui::EndFrame();
    }

    context_.reset();
    frameState_ = FrameState::idle;
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

void UiSystem::beginFrame(nr::rhi::PresentationContext &presentation, float deltaSeconds)
{
    nrAssert(initialized(), "UiSystem::beginFrame requires initialize() first.");
    nrAssert(frameState_ != FrameState::active,
             "UiSystem::beginFrame requires the previous active frame to be finalized.");

    setCurrentContext();

    auto const sanitizedDelta = detail::sanitizeUiDeltaSeconds(deltaSeconds);
    frameStats_.deltaSeconds = sanitizedDelta;
    fpsSampleAccumulatedDeltaSeconds_ += sanitizedDelta;
    ++fpsSampleFrameCount_;
    if (fpsSampleFrameCount_ >= nr::statistics::sampleFrameCount())
    {
        auto const averagedDeltaSeconds = fpsSampleAccumulatedDeltaSeconds_ / static_cast<float>(fpsSampleFrameCount_);
        frameStats_.smoothedDeltaSeconds = averagedDeltaSeconds;
        frameStats_.frameTimeMilliseconds = averagedDeltaSeconds * 1000.0f;
        frameStats_.framesPerSecond = averagedDeltaSeconds > 0.0f ? 1.0f / averagedDeltaSeconds : 0.0f;
        nr::statistics::refreshSampleFrameCount(frameStats_.framesPerSecond);
        fpsSampleAccumulatedDeltaSeconds_ = 0.0f;
        fpsSampleFrameCount_ = 0u;
    }
    ++frameStats_.frameCounter;

    auto &io = ImGui::GetIO();
    auto const swapchainExtent = presentation.swapchainExtent();
    io.DisplaySize = ImVec2{
        static_cast<float>(std::max(1u, swapchainExtent.width)),
        static_cast<float>(std::max(1u, swapchainExtent.height)),
    };
    io.DisplayFramebufferScale = ImVec2{1.0f, 1.0f};
    io.DeltaTime = sanitizedDelta;

    auto &input = presentation.windowInput();
    auto const cursorPosition = input.cursorPosition();
    io.AddMousePosEvent(static_cast<float>(cursorPosition.x), static_cast<float>(cursorPosition.y));
    io.AddMouseButtonEvent(0, input.mouseButtonDown(detail::kMouseButtonLeft));
    io.AddMouseButtonEvent(1, input.mouseButtonDown(detail::kMouseButtonRight));
    io.AddMouseButtonEvent(2, input.mouseButtonDown(detail::kMouseButtonMiddle));
    auto const verticalScrollOffset = input.consumeVerticalScrollOffset();
    if (verticalScrollOffset != 0.0)
    {
        io.AddMouseWheelEvent(0.0f, static_cast<float>(verticalScrollOffset));
    }
    detail::submitKeyboardInput(io, input);

    ImGui::NewFrame();
    frameState_ = FrameState::active;
    unifiedWindowOpen_ = false;
    unifiedWindowVisible_ = false;
    unifiedWindowSectionCount_ = 0u;
    queuedSections_.clear();
}

UiCaptureState UiSystem::finalizeFrame()
{
    nrAssert(frameState_ == FrameState::active, "UiSystem::finalizeFrame requires one active UI frame.");

    auto const performanceSections = std::array{
        UiSection{
            .id = "cpu.performance",
            .title = "CPU Performance",
            .draw = [](UiSystem &ui) { ui.drawCpuPerformanceSection(); },
        },
        UiSection{
            .id = "gpu.performance",
            .title = "GPU Performance",
            .draw = [](UiSystem &ui) { ui.drawGpuPerformanceSection(); },
        },
    };
    renderSections(std::span<const UiSection>{}, std::span<const UiSection>{performanceSections});

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

    auto &io = ImGui::GetIO();
    auto const captureState = UiCaptureState{
        .wantsMouse = io.WantCaptureMouse,
        .wantsKeyboard = io.WantCaptureKeyboard,
    };

    frameState_ = FrameState::finalized;
    return captureState;
}

void UiSystem::queueSection(UiSection section)
{
    requireActiveFrame("queueSection");
    queuedSections_.push_back(std::move(section));
}

void UiSystem::renderSections(std::span<const UiSection> leadingSections, std::span<const UiSection> trailingSections,
                              ImGuiWindowFlags flags)
{
    requireActiveFrame("renderSections");
    setCurrentContext();

    if (leadingSections.empty() && queuedSections_.empty() && trailingSections.empty())
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

    auto drawAppSection = [&](const UiSection &section) {
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
    drawAppSections(trailingSections);
    queuedSections_.clear();
}

void UiSystem::separator()
{
    requireActiveFrame("separator");
    setCurrentContext();
    ImGui::Separator();
}

void UiSystem::text(std::string_view content)
{
    requireActiveFrame("text");
    setCurrentContext();
    ImGui::TextUnformatted(content.data(), content.data() + content.size());
}

bool UiSystem::checkbox(std::string_view label, bool &value)
{
    requireActiveFrame("checkbox");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Checkbox(ownedLabel.c_str(), &value);
}

bool UiSystem::button(std::string_view label)
{
    requireActiveFrame("button");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Button(ownedLabel.c_str());
}

bool UiSystem::inputText(std::string_view label, std::string &value)
{
    requireActiveFrame("inputText");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::InputText(ownedLabel.c_str(), value.data(), value.capacity() + 1u, ImGuiInputTextFlags_CallbackResize,
                            detail::resizeInputTextBuffer, std::addressof(value));
}

bool UiSystem::beginCombo(std::string_view label, std::string_view preview)
{
    requireActiveFrame("beginCombo");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const ownedPreview = std::string{preview};
    return ImGui::BeginCombo(ownedLabel.c_str(), ownedPreview.c_str());
}

void UiSystem::endCombo()
{
    requireActiveFrame("endCombo");
    setCurrentContext();
    ImGui::EndCombo();
}

bool UiSystem::selectable(std::string_view label, bool selected)
{
    requireActiveFrame("selectable");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::Selectable(ownedLabel.c_str(), selected);
}

bool UiSystem::sliderFloat(std::string_view label, float &value, float minValue, float maxValue)
{
    requireActiveFrame("sliderFloat");
    nrAssert(minValue <= maxValue, "UiSystem::sliderFloat requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    return ImGui::SliderFloat(ownedLabel.c_str(), &value, minValue, maxValue);
}

bool UiSystem::inputFloat(std::string_view label, float &value, float minValue, float maxValue)
{
    requireActiveFrame("inputFloat");
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

bool UiSystem::inputInt32(std::string_view label, std::int32_t &value, std::int32_t minValue, std::int32_t maxValue)
{
    requireActiveFrame("inputInt32");
    nrAssert(minValue <= maxValue, "UiSystem::inputInt32 requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const inputId = std::format("##{}", ownedLabel);
    auto inputValue = std::clamp(value, minValue, maxValue);
    ImGui::TextUnformatted(ownedLabel.data(), ownedLabel.data() + ownedLabel.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    auto const changed = ImGui::InputScalar(inputId.c_str(), ImGuiDataType_S32, std::addressof(inputValue), nullptr,
                                            nullptr, "%d", ImGuiInputTextFlags_CharsDecimal);
    value = std::clamp(inputValue, minValue, maxValue);
    return changed;
}

bool UiSystem::inputUInt(std::string_view label, std::uint32_t &value, std::uint32_t minValue, std::uint32_t maxValue)
{
    requireActiveFrame("inputUInt");
    nrAssert(minValue <= maxValue, "UiSystem::inputUInt requires minValue <= maxValue.");
    setCurrentContext();

    auto const ownedLabel = std::string{label};
    auto const inputId = std::format("##{}", ownedLabel);
    auto inputValue = std::clamp(value, minValue, maxValue);
    ImGui::TextUnformatted(ownedLabel.data(), ownedLabel.data() + ownedLabel.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(detail::compactUIntInputWidth(maxValue));
    auto const changed = ImGui::InputScalar(inputId.c_str(), ImGuiDataType_U32, std::addressof(inputValue), nullptr,
                                            nullptr, "%u", ImGuiInputTextFlags_CharsDecimal);
    value = std::clamp(inputValue, minValue, maxValue);
    return changed;
}

bool UiSystem::sliderUInt(std::string_view label, std::uint32_t &value, std::uint32_t minValue, std::uint32_t maxValue)
{
    requireActiveFrame("sliderUInt");
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

void UiSystem::beginDisabled(bool disabled)
{
    requireActiveFrame("beginDisabled");
    setCurrentContext();
    ImGui::BeginDisabled(disabled);
}

void UiSystem::endDisabled()
{
    requireActiveFrame("endDisabled");
    setCurrentContext();
    ImGui::EndDisabled();
}

bool UiSystem::itemEditCommitted() const
{
    requireActiveFrame("itemEditCommitted");
    setCurrentContext();
    auto const enterPressed = ImGui::IsItemActive() && (ImGui::IsKeyPressed(imgui::keyEnter, false) ||
                                                        ImGui::IsKeyPressed(imgui::keyKeypadEnter, false));
    return ImGui::IsItemDeactivatedAfterEdit() || enterPressed;
}

void UiSystem::setItemDefaultFocus()
{
    requireActiveFrame("setItemDefaultFocus");
    setCurrentContext();
    ImGui::SetItemDefaultFocus();
}

const UiFrameStats &UiSystem::stats() const noexcept
{
    return frameStats_;
}

void UiSystem::setCameraFrame(const nr::renderer::ViewerPerspectiveCameraFrame &cameraFrame) noexcept
{
    cameraFrame_ = cameraFrame;
}

const nr::renderer::ViewerPerspectiveCameraFrame &UiSystem::cameraFrame() const noexcept
{
    return cameraFrame_;
}

void UiSystem::setCpuStatistics(const nr::renderer::RendererCpuStatistics &statistics) noexcept
{
    cpuStatistics_ = statistics;
}

void UiSystem::drawCpuPerformanceSection()
{
    if (!cpuStatistics_.valid)
    {
        return;
    }

    auto const &average = cpuStatistics_.average;
    detail::drawCpuTimingLine(*this, "CPU Wait GPU", average.cpuWaitGpuMilliseconds);
    detail::drawCpuTimingLine(*this, "Frame Setup", average.frameSetupMilliseconds);
    detail::drawCpuTimingLine(*this, "Scene", average.sceneMilliseconds);
    detail::drawCpuTimingLine(*this, "Post Scene", average.postSceneMilliseconds);
    detail::drawCpuTimingLine(*this, "Build", average.buildMilliseconds);
    detail::drawCpuTimingLine(*this, "Compile", average.compileMilliseconds);
    detail::drawCpuTimingLine(*this, "Prepare", average.prepareMilliseconds);
    detail::drawCpuTimingLine(*this, "Execute", average.executeMilliseconds);
    detail::drawCpuTimingLine(*this, "Present", average.presentMilliseconds);
    detail::drawCpuTimingLine(*this, "Total", average.totalMilliseconds);
}

void UiSystem::setGpuPassStatistics(const nr::renderer::RendererGpuPassStatistics &statistics) noexcept
{
    gpuPassStatistics_ = statistics;
}

void UiSystem::drawGpuPerformanceSection()
{
    if (!gpuPassStatistics_.valid)
    {
        return;
    }

    if (gpuPassStatistics_.averages.empty())
    {
        text("No addPass samples");
        return;
    }

    std::ranges::for_each(gpuPassStatistics_.averages, [&](const nr::renderer::RendererGpuPassAverage &timing) {
        detail::drawGpuPassTimingLine(*this, timing, gpuPassStatistics_.averagedFrameCount);
    });
}

std::optional<std::reference_wrapper<const ImDrawData>> UiSystem::drawData() const noexcept
{
    if (frameState_ != FrameState::finalized)
    {
        return std::nullopt;
    }

    setCurrentContext();
    auto *drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
    {
        return std::nullopt;
    }

    return std::cref(*drawData);
}

void UiSystem::setCurrentContext() const noexcept
{
    ImGui::SetCurrentContext(context_.get());
}

void UiSystem::requireActiveFrame(std::string_view operation) const
{
    if (frameState_ == FrameState::active)
    {
        return;
    }

    nrAssert(false, "UiSystem::{} requires an active UI frame.", operation);
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

    auto const flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
    return ImGui::CollapsingHeader(label.c_str(), flags);
}

void UiSystem::prepareWindowDefaults()
{
    ImGui::SetNextWindowPos(ImVec2{detail::kUiWindowMargin, detail::kUiWindowMargin}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{detail::kUiWindowDefaultWidth, detail::kUiWindowDefaultHeight},
                             ImGuiCond_FirstUseEver);
}
} // namespace nr::app
