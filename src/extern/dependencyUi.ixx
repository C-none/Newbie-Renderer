module;
#include <cstddef>
#include <imgui.h>

export module dependency.ui;

export namespace ImGui
{
using ::ImGui::Begin;
using ::ImGui::Checkbox;
using ::ImGui::CollapsingHeader;
using ::ImGui::CreateContext;
using ::ImGui::DestroyContext;
using ::ImGui::End;
using ::ImGui::EndFrame;
using ::ImGui::GetDrawData;
using ::ImGui::GetIO;
using ::ImGui::Indent;
using ::ImGui::NewFrame;
using ::ImGui::Render;
using ::ImGui::Separator;
using ::ImGui::SetCurrentContext;
using ::ImGui::SetNextWindowPos;
using ::ImGui::SetNextWindowSize;
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
} // namespace imgui
