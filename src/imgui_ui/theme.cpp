#include "imgui_ui/theme.h"

namespace sa::imgui_ui::theme {
namespace {

ImVec4 mix(const ImVec4& first, const ImVec4& second, float amount) {
    return ImVec4(first.x + (second.x - first.x) * amount,
                  first.y + (second.y - first.y) * amount,
                  first.z + (second.z - first.z) * amount,
                  first.w + (second.w - first.w) * amount);
}

ImVec4 fromColor(const QColor& color) {
    return ImVec4(color.redF(), color.greenF(), color.blueF(), 1.0f);
}

float luminance(const ImVec4& color) {
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

void applyCommon(const ImVec4& background, const ImVec4& sidebar, const ImVec4& surface,
                 const ImVec4& subtle, const ImVec4& text, const ImVec4& text_secondary,
                 const ImVec4& text_muted, const ImVec4& border, const ImVec4& border_strong,
                 const ImVec4& accent, const ImVec4& accent_hover, const ImVec4& selection,
                 const ImVec4& overlay_surface, const ImVec4& overlay_border) {
    Background = background;
    Sidebar = sidebar;
    Surface = surface;
    SurfaceSubtle = subtle;
    TextPrimary = text;
    TextSecondary = text_secondary;
    TextMuted = text_muted;
    Border = border;
    BorderStrong = border_strong;
    Accent = accent;
    AccentHover = accent_hover;
    Selection = selection;
    OverlaySurface = overlay_surface;
    OverlayBorder = overlay_border;
    OverlayText = text;
    OverlayMuted = text_muted;
}

void setDefaultTheme() {
    applyCommon(ImVec4(0.035f, 0.043f, 0.067f, 1.0f), ImVec4(0.055f, 0.067f, 0.098f, 1.0f),
                ImVec4(0.090f, 0.106f, 0.149f, 0.96f), ImVec4(0.078f, 0.094f, 0.149f, 1.0f),
                ImVec4(0.933f, 0.945f, 0.973f, 1.0f), ImVec4(0.760f, 0.792f, 0.859f, 1.0f),
                ImVec4(0.549f, 0.576f, 0.659f, 1.0f), ImVec4(0.106f, 0.125f, 0.180f, 1.0f),
                ImVec4(0.165f, 0.188f, 0.259f, 1.0f), ImVec4(1.000f, 0.435f, 0.612f, 1.0f),
                ImVec4(0.984f, 0.302f, 0.525f, 1.0f), ImVec4(0.227f, 0.110f, 0.165f, 1.0f),
                ImVec4(0.090f, 0.106f, 0.149f, 1.0f), ImVec4(0.165f, 0.188f, 0.259f, 1.0f));
}

void setLightTheme() {
    applyCommon(ImVec4(0.961f, 0.969f, 0.984f, 1.0f), ImVec4(0.918f, 0.941f, 0.973f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.973f, 0.980f, 0.992f, 1.0f),
                ImVec4(0.122f, 0.161f, 0.216f, 1.0f), ImVec4(0.294f, 0.337f, 0.388f, 1.0f),
                ImVec4(0.420f, 0.447f, 0.502f, 1.0f), ImVec4(0.867f, 0.890f, 0.925f, 1.0f),
                ImVec4(0.780f, 0.812f, 0.867f, 1.0f), ImVec4(0.878f, 0.267f, 0.478f, 1.0f),
                ImVec4(0.780f, 0.184f, 0.400f, 1.0f), ImVec4(1.000f, 0.894f, 0.933f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.867f, 0.890f, 0.925f, 1.0f));
}

void setDarkTheme() {
    applyCommon(ImVec4(0.043f, 0.055f, 0.078f, 1.0f), ImVec4(0.067f, 0.082f, 0.114f, 1.0f),
                ImVec4(0.090f, 0.110f, 0.145f, 1.0f), ImVec4(0.071f, 0.090f, 0.125f, 1.0f),
                ImVec4(0.910f, 0.925f, 0.957f, 1.0f), ImVec4(0.760f, 0.792f, 0.859f, 1.0f),
                ImVec4(0.545f, 0.580f, 0.655f, 1.0f), ImVec4(0.133f, 0.161f, 0.212f, 1.0f),
                ImVec4(0.188f, 0.227f, 0.294f, 1.0f), ImVec4(0.220f, 0.741f, 0.973f, 1.0f),
                ImVec4(0.055f, 0.647f, 0.914f, 1.0f), ImVec4(0.071f, 0.173f, 0.239f, 1.0f),
                ImVec4(0.090f, 0.110f, 0.145f, 1.0f), ImVec4(0.188f, 0.227f, 0.294f, 1.0f));
}

void setHighContrastTheme() {
    applyCommon(ImVec4(0, 0, 0, 1), ImVec4(0.039f, 0.039f, 0.039f, 1),
                ImVec4(0.067f, 0.067f, 0.067f, 1), ImVec4(0.039f, 0.039f, 0.039f, 1),
                ImVec4(1, 1, 1, 1), ImVec4(0.850f, 0.850f, 0.850f, 1),
                ImVec4(0.816f, 0.816f, 0.816f, 1), ImVec4(0.400f, 0.400f, 0.400f, 1),
                ImVec4(0.667f, 0.667f, 0.667f, 1), ImVec4(1.000f, 0.831f, 0.0f, 1),
                ImVec4(1.0f, 0.902f, 0.302f, 1), ImVec4(0.227f, 0.188f, 0.0f, 1),
                ImVec4(0, 0, 0, 1), ImVec4(1, 1, 1, 1));
}

void setCustomTheme(const sa::settings::ThemeColors& custom) {
    const ImVec4 background = fromColor(custom.background);
    const ImVec4 text = fromColor(custom.text);
    const ImVec4 accent = fromColor(custom.accent);
    const bool light = luminance(background) > 0.55f;
    const ImVec4 surface = light ? mix(background, ImVec4(1, 1, 1, 1), 0.55f)
                                 : mix(background, ImVec4(1, 1, 1, 1), 0.06f);
    const ImVec4 subtle = light ? mix(background, ImVec4(1, 1, 1, 1), 0.30f)
                                : mix(background, ImVec4(1, 1, 1, 1), 0.03f);
    const ImVec4 sidebar = light ? mix(background, accent, 0.08f)
                                 : mix(background, accent, 0.04f);
    const ImVec4 secondary = mix(text, background, 0.22f);
    const ImVec4 muted = mix(text, background, 0.42f);
    const ImVec4 border = light ? mix(text, background, 0.78f) : mix(background, text, 0.14f);
    const ImVec4 border_strong =
        light ? mix(text, background, 0.62f) : mix(background, text, 0.24f);
    const ImVec4 accent_hover = light ? mix(accent, ImVec4(0, 0, 0, 1), 0.18f)
                                      : mix(accent, ImVec4(1, 1, 1, 1), 0.18f);
    const ImVec4 selection = mix(background, accent, light ? 0.18f : 0.24f);
    applyCommon(background, sidebar, surface, subtle, text, secondary, muted, border, border_strong,
                accent, accent_hover, selection, surface, border);
}

} // namespace

void setTheme(sa::settings::ThemePreset preset, const sa::settings::ThemeColors& custom) {
    switch (preset) {
    case sa::settings::ThemePreset::Light: setLightTheme(); break;
    case sa::settings::ThemePreset::Dark: setDarkTheme(); break;
    case sa::settings::ThemePreset::HighContrast: setHighContrastTheme(); break;
    case sa::settings::ThemePreset::Custom: setCustomTheme(custom); break;
    case sa::settings::ThemePreset::Default:
    default: setDefaultTheme(); break;
    }
}

void applySettingsStyle() {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(22, 18);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 9);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ChildRounding = 14.0f;
    style.FrameRounding = 10.0f;
    style.PopupRounding = 12.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 10.0f;
    style.WindowRounding = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;

    style.Colors[ImGuiCol_Text] = TextPrimary;
    style.Colors[ImGuiCol_TextDisabled] = TextMuted;
    style.Colors[ImGuiCol_WindowBg] = Background;
    style.Colors[ImGuiCol_ChildBg] = Surface;
    style.Colors[ImGuiCol_PopupBg] = SurfaceSubtle;
    style.Colors[ImGuiCol_Border] = Border;
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_FrameBg] = SurfaceSubtle;
    style.Colors[ImGuiCol_FrameBgHovered] = mix(SurfaceSubtle, TextPrimary, 0.06f);
    style.Colors[ImGuiCol_FrameBgActive] = Selection;
    style.Colors[ImGuiCol_TitleBg] = Background;
    style.Colors[ImGuiCol_TitleBgActive] = Background;
    style.Colors[ImGuiCol_MenuBarBg] = Surface;
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_ScrollbarGrab] = mix(TextMuted, Background, 0.50f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = mix(TextMuted, Background, 0.28f);
    style.Colors[ImGuiCol_CheckMark] = Accent;
    style.Colors[ImGuiCol_SliderGrab] = Accent;
    style.Colors[ImGuiCol_SliderGrabActive] = AccentHover;
    style.Colors[ImGuiCol_Button] = SurfaceSubtle;
    style.Colors[ImGuiCol_ButtonHovered] = mix(SurfaceSubtle, TextPrimary, 0.08f);
    style.Colors[ImGuiCol_ButtonActive] = Selection;
    style.Colors[ImGuiCol_Header] = Selection;
    style.Colors[ImGuiCol_HeaderHovered] = mix(Surface, Selection, 0.55f);
    style.Colors[ImGuiCol_HeaderActive] = Selection;
    style.Colors[ImGuiCol_Separator] = Border;
    style.Colors[ImGuiCol_SeparatorHovered] = Accent;
    style.Colors[ImGuiCol_SeparatorActive] = Accent;
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Tab] = Sidebar;
    style.Colors[ImGuiCol_TabHovered] = Selection;
    style.Colors[ImGuiCol_TabSelected] = Surface;
    style.Colors[ImGuiCol_TableHeaderBg] = SurfaceSubtle;
    style.Colors[ImGuiCol_TableBorderStrong] = BorderStrong;
    style.Colors[ImGuiCol_TableBorderLight] = Border;
    style.Colors[ImGuiCol_TableRowBg] = Surface;
    style.Colors[ImGuiCol_TableRowBgAlt] = mix(Surface, Background, 0.55f);
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
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.145f, 0.161f, 0.224f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.227f, 0.110f, 0.165f, 1.0f);
}

} // namespace sa::imgui_ui::theme
