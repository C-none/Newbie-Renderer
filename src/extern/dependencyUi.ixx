module;
#include <cstddef>
#include <imgui.h>

export module dependency.ui;

export namespace ImGui
{
using ::ImGui::Begin;
using ::ImGui::BeginCombo;
using ::ImGui::BeginDisabled;
using ::ImGui::Button;
using ::ImGui::Checkbox;
using ::ImGui::CollapsingHeader;
using ::ImGui::CreateContext;
using ::ImGui::DestroyContext;
using ::ImGui::End;
using ::ImGui::EndCombo;
using ::ImGui::EndDisabled;
using ::ImGui::EndFrame;
using ::ImGui::GetDrawData;
using ::ImGui::GetIO;
using ::ImGui::Indent;
using ::ImGui::InputFloat;
using ::ImGui::InputScalar;
using ::ImGui::InputText;
using ::ImGui::IsItemActive;
using ::ImGui::IsItemDeactivatedAfterEdit;
using ::ImGui::IsKeyPressed;
using ::ImGui::NewFrame;
using ::ImGui::Render;
using ::ImGui::SameLine;
using ::ImGui::Selectable;
using ::ImGui::Separator;
using ::ImGui::SetCurrentContext;
using ::ImGui::SetItemDefaultFocus;
using ::ImGui::SetNextItemWidth;
using ::ImGui::SetNextWindowPos;
using ::ImGui::SetNextWindowSize;
using ::ImGui::SliderFloat;
using ::ImGui::SliderInt;
using ::ImGui::Spacing;
using ::ImGui::StyleColorsDark;
using ::ImGui::TextUnformatted;
using ::ImGui::Unindent;
} // namespace ImGui

export using ::ImDrawData;
export using ::ImDrawIdx;
export using ::ImDrawVert;
export using ::ImGuiBackendFlags_RendererHasTextures;
export using ::ImGuiBackendFlags_RendererHasVtxOffset;
export using ::ImGuiCond_FirstUseEver;
export using ::ImGuiContext;
export using ::ImGuiDataType_Float;
export using ::ImGuiDataType_S32;
export using ::ImGuiDataType_U32;
export using ::ImGuiInputTextCallbackData;
export using ::ImGuiInputTextFlags;
export using ::ImGuiInputTextFlags_CallbackResize;
export using ::ImGuiInputTextFlags_CharsDecimal;
export using ::ImGuiKey;
export using ::ImGuiTreeNodeFlags_DefaultOpen;
export using ::ImGuiTreeNodeFlags_None;
export using ::ImGuiWindowFlags;
export using ::ImTextureData;
export using ::ImTextureFormat_Alpha8;
export using ::ImTextureFormat_RGBA32;
export using ::ImTextureID;
export using ::ImTextureStatus_Destroyed;
export using ::ImTextureStatus_OK;
export using ::ImTextureStatus_WantCreate;
export using ::ImTextureStatus_WantDestroy;
export using ::ImTextureStatus_WantUpdates;
export using ::ImVec2;

export namespace imgui
{
inline constexpr unsigned int drawVertPosOffset = static_cast<unsigned int>(offsetof(ImDrawVert, pos));
inline constexpr unsigned int drawVertUvOffset = static_cast<unsigned int>(offsetof(ImDrawVert, uv));
inline constexpr unsigned int drawVertColorOffset = static_cast<unsigned int>(offsetof(ImDrawVert, col));

inline constexpr ImGuiKey keyTab = ImGuiKey_Tab;
inline constexpr ImGuiKey keyLeftArrow = ImGuiKey_LeftArrow;
inline constexpr ImGuiKey keyRightArrow = ImGuiKey_RightArrow;
inline constexpr ImGuiKey keyUpArrow = ImGuiKey_UpArrow;
inline constexpr ImGuiKey keyDownArrow = ImGuiKey_DownArrow;
inline constexpr ImGuiKey keyPageUp = ImGuiKey_PageUp;
inline constexpr ImGuiKey keyPageDown = ImGuiKey_PageDown;
inline constexpr ImGuiKey keyHome = ImGuiKey_Home;
inline constexpr ImGuiKey keyEnd = ImGuiKey_End;
inline constexpr ImGuiKey keyInsert = ImGuiKey_Insert;
inline constexpr ImGuiKey keyDelete = ImGuiKey_Delete;
inline constexpr ImGuiKey keyBackspace = ImGuiKey_Backspace;
inline constexpr ImGuiKey keySpace = ImGuiKey_Space;
inline constexpr ImGuiKey keyEnter = ImGuiKey_Enter;
inline constexpr ImGuiKey keyKeypadEnter = ImGuiKey_KeypadEnter;
inline constexpr ImGuiKey keyEscape = ImGuiKey_Escape;
inline constexpr ImGuiKey keyLeftCtrl = ImGuiKey_LeftCtrl;
inline constexpr ImGuiKey keyLeftShift = ImGuiKey_LeftShift;
inline constexpr ImGuiKey keyLeftAlt = ImGuiKey_LeftAlt;
inline constexpr ImGuiKey keyLeftSuper = ImGuiKey_LeftSuper;
inline constexpr ImGuiKey keyRightCtrl = ImGuiKey_RightCtrl;
inline constexpr ImGuiKey keyRightShift = ImGuiKey_RightShift;
inline constexpr ImGuiKey keyRightAlt = ImGuiKey_RightAlt;
inline constexpr ImGuiKey keyRightSuper = ImGuiKey_RightSuper;
inline constexpr ImGuiKey keyA = ImGuiKey_A;
inline constexpr ImGuiKey keyC = ImGuiKey_C;
inline constexpr ImGuiKey keyV = ImGuiKey_V;
inline constexpr ImGuiKey keyX = ImGuiKey_X;
inline constexpr ImGuiKey keyY = ImGuiKey_Y;
inline constexpr ImGuiKey keyZ = ImGuiKey_Z;
inline constexpr ImGuiKey keyModCtrl = ImGuiMod_Ctrl;
inline constexpr ImGuiKey keyModShift = ImGuiMod_Shift;
inline constexpr ImGuiKey keyModAlt = ImGuiMod_Alt;
inline constexpr ImGuiKey keyModSuper = ImGuiMod_Super;
} // namespace imgui
