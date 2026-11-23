#include "imgui_ui/theme.h"

namespace sa::imgui_ui::theme {

void applySettingsStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ChildRounding = 10.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 8.0f;
    style.GrabRounding = 6.0f;
    style.WindowRounding = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;

    style.Colors[ImGuiCol_Text] = TextPrimary;
    style.Colors[ImGuiCol_TextDisabled] = TextMuted;
    style.Colors[ImGuiCol_WindowBg] = Background;
    style.Colors[ImGuiCol_ChildBg] = Surface;
    style.Colors[ImGuiCol_PopupBg] = Surface;
    style.Colors[ImGuiCol_Border] = Border;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_FrameBg] = Surface;
    style.Colors[ImGuiCol_FrameBgHovered] = SurfaceSubtle;
    style.Colors[ImGuiCol_FrameBgActive] = Selection;
    style.Colors[ImGuiCol_TitleBg] = Background;
    style.Colors[ImGuiCol_TitleBgActive] = Background;
    style.Colors[ImGuiCol_MenuBarBg] = Surface;
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.78f, 0.81f, 0.86f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.66f, 0.70f, 0.77f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = Accent;
    style.Colors[ImGuiCol_SliderGrab] = Accent;
    style.Colors[ImGuiCol_SliderGrabActive] = AccentHover;
    style.Colors[ImGuiCol_Button] = Surface;
    style.Colors[ImGuiCol_ButtonHovered] = SurfaceSubtle;
    style.Colors[ImGuiCol_ButtonActive] = Selection;
    style.Colors[ImGuiCol_Header] = Selection;
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.945f, 0.961f, 0.996f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = Selection;
    style.Colors[ImGuiCol_Separator] = Border;
    style.Colors[ImGuiCol_SeparatorHovered] = Accent;
    style.Colors[ImGuiCol_SeparatorActive] = Accent;
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Tab] = Sidebar;
    style.Colors[ImGuiCol_TabHovered] = Selection;
    style.Colors[ImGuiCol_TabSelected] = Surface;
    style.Colors[ImGuiCol_TableHeaderBg] = SurfaceSubtle;
    style.Colors[ImGuiCol_TableBorderStrong] = Border;
    style.Colors[ImGuiCol_TableBorderLight] = Border;
    style.Colors[ImGuiCol_TableRowBg] = Surface;
    style.Colors[ImGuiCol_TableRowBgAlt] = SurfaceSubtle;
    style.Colors[ImGuiCol_TextSelectedBg] = Selection;
    style.Colors[ImGuiCol_NavCursor] = Accent;
}

void applyOverlayStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(8, 5);
    style.ItemSpacing = ImVec2(8, 6);
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Text] = OverlayText;
    style.Colors[ImGuiCol_Button] = OverlaySurface;
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.239f, 0.286f, 0.365f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.278f, 0.333f, 0.424f, 1.0f);
}

} // namespace sa::imgui_ui::theme
