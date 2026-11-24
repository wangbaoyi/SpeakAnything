#pragma once

#include "imgui.h"
#include "ui/settings_types.h"

namespace sa::imgui_ui::theme {

// Palette mirrors WhaleTalk's dark UI: near-black canvas, translucent panels,
// hairline borders and a pink accent.
// Updated by setTheme(); shared by the settings context and the overlay context.
inline ImVec4 Background(0.035f, 0.043f, 0.067f, 1.0f);
inline ImVec4 Sidebar(0.055f, 0.067f, 0.098f, 1.0f);
inline ImVec4 Surface(0.090f, 0.106f, 0.149f, 0.96f);
inline ImVec4 SurfaceSubtle(0.078f, 0.094f, 0.149f, 1.0f);
inline ImVec4 TextPrimary(0.933f, 0.945f, 0.973f, 1.0f);
inline ImVec4 TextSecondary(0.760f, 0.792f, 0.859f, 1.0f);
inline ImVec4 TextMuted(0.549f, 0.576f, 0.659f, 1.0f);
inline ImVec4 Border(0.106f, 0.125f, 0.180f, 1.0f);
inline ImVec4 BorderStrong(0.165f, 0.188f, 0.259f, 1.0f);
inline ImVec4 Accent(1.000f, 0.435f, 0.612f, 1.0f);
inline ImVec4 AccentHover(0.984f, 0.302f, 0.525f, 1.0f);
inline ImVec4 Selection(0.227f, 0.110f, 0.165f, 1.0f);
inline ImVec4 Success(0.290f, 0.871f, 0.502f, 1.0f);
inline ImVec4 Danger(0.984f, 0.443f, 0.522f, 1.0f);
inline ImVec4 Info(0.220f, 0.741f, 0.973f, 1.0f);

inline ImVec4 OverlaySurface(0.090f, 0.106f, 0.149f, 1.0f);
inline ImVec4 OverlayBorder(0.165f, 0.188f, 0.259f, 1.0f);
inline ImVec4 OverlayText(0.933f, 0.945f, 0.973f, 1.0f);
inline ImVec4 OverlayMuted(0.549f, 0.576f, 0.659f, 1.0f);

inline ImU32 u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

void applySettingsStyle();
void applyOverlayStyle();
void setTheme(sa::settings::ThemePreset preset, const sa::settings::ThemeColors& custom);

} // namespace sa::imgui_ui::theme
