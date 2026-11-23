#pragma once

#include <QColor>

#include <array>

namespace sa::ui {

enum class ColorRole {
    Background,
    Sidebar,
    Surface,
    SurfaceSubtle,
    TextPrimary,
    TextSecondary,
    TextMuted,
    Border,
    Focus,
    Selection,
    Accent,
    AccentHover,
    SliderFill,
    StatusSuccess,
    StatusWarning,
    StatusDanger,
    DisabledSurface,
    DisabledText,
};

[[nodiscard]] QColor color(ColorRole role);
[[nodiscard]] double contrastRatio(const QColor& first, const QColor& second);

inline constexpr std::array<int, 6> spacing_scale{4, 8, 12, 16, 24, 32};
inline constexpr std::array<int, 3> radius_scale{6, 8, 12};
inline constexpr std::array<int, 8> font_size_scale{11, 12, 13, 14, 15, 22, 24, 26};
inline constexpr int page_title_size = 24;
inline constexpr int card_title_size = 15;
inline constexpr int body_size = 13;
inline constexpr int hint_size = 12;
inline constexpr int row_min_height = 44;
inline constexpr int nav_item_height = 44;
inline constexpr int border_width = 1;
inline constexpr int focus_ring_width = 2;

} // namespace sa::ui
