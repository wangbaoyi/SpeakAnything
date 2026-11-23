#pragma once

#include "imgui.h"

namespace sa::imgui_ui::theme {

inline const ImVec4 Background(0.949f, 0.961f, 0.976f, 1.0f);
inline const ImVec4 Sidebar(0.914f, 0.937f, 0.969f, 1.0f);
inline const ImVec4 Surface(1.0f, 1.0f, 1.0f, 1.0f);
inline const ImVec4 SurfaceSubtle(0.973f, 0.980f, 0.992f, 1.0f);
inline const ImVec4 TextPrimary(0.118f, 0.153f, 0.204f, 1.0f);
inline const ImVec4 TextSecondary(0.275f, 0.314f, 0.376f, 1.0f);
inline const ImVec4 TextMuted(0.400f, 0.443f, 0.510f, 1.0f);
inline const ImVec4 Border(0.855f, 0.882f, 0.922f, 1.0f);
inline const ImVec4 Accent(0.145f, 0.388f, 0.922f, 1.0f);
inline const ImVec4 AccentHover(0.114f, 0.306f, 0.847f, 1.0f);
inline const ImVec4 Selection(0.918f, 0.945f, 1.0f, 1.0f);
inline const ImVec4 Success(0.180f, 0.659f, 0.549f, 1.0f);
inline const ImVec4 Danger(0.863f, 0.322f, 0.361f, 1.0f);

inline const ImVec4 OverlaySurface(0.118f, 0.153f, 0.220f, 1.0f);
inline const ImVec4 OverlayBorder(0.208f, 0.255f, 0.333f, 1.0f);
inline const ImVec4 OverlayText(0.961f, 0.969f, 0.980f, 1.0f);
inline const ImVec4 OverlayMuted(0.706f, 0.741f, 0.800f, 1.0f);

inline ImU32 u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

void applySettingsStyle();
void applyOverlayStyle();

} // namespace sa::imgui_ui::theme
