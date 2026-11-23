#include "ui/overlay_theme.h"

#include <QApplication>

#include <cmath>

namespace sa::ui {

ThemeColors styleDefaultColors(BubbleStyle style) {
    switch (style) {
    case BubbleStyle::Capsule:
        return {QColor(27, 45, 51), QColor(235, 250, 246), QColor(99, 217, 181)};
    case BubbleStyle::Ring:
        return {QColor(78, 80, 86), QColor(242, 242, 247), QColor(69, 184, 158)};
    case BubbleStyle::Panel:
        return {QColor(251, 252, 253), QColor(31, 37, 43), QColor(69, 184, 158)};
    }
    return {QColor(251, 252, 253), QColor(31, 37, 43), QColor(69, 184, 158)};
}

std::optional<ThemeColors> themeColors(const AppearanceSettings& appearance) {
    switch (appearance.preset) {
    case sa::settings::ThemePreset::Default: return std::nullopt;
    case sa::settings::ThemePreset::Light:
        return ThemeColors{QColor(251, 252, 253), QColor(31, 37, 43), QColor(47, 158, 133)};
    case sa::settings::ThemePreset::Dark:
        return ThemeColors{QColor(30, 35, 42), QColor(238, 241, 244), QColor(69, 184, 158)};
    case sa::settings::ThemePreset::HighContrast:
        return ThemeColors{QColor(0, 0, 0), QColor(255, 255, 255), QColor(255, 212, 0)};
    case sa::settings::ThemePreset::Custom: return appearance.custom;
    }
    return std::nullopt;
}

int themeFontSize(const AppearanceSettings& appearance) {
    switch (appearance.preset) {
    case sa::settings::ThemePreset::Dark: return 11;
    case sa::settings::ThemePreset::HighContrast: return 12;
    case sa::settings::ThemePreset::Custom: return appearance.custom_font_size;
    default: return 10;
    }
}

QFont themeFont(const AppearanceSettings& appearance) {
    QFont font = QApplication::font();
    if (appearance.preset == sa::settings::ThemePreset::Custom &&
        !appearance.custom_font_family.isEmpty()) {
        font.setFamily(appearance.custom_font_family);
    }
    font.setPointSize(themeFontSize(appearance));
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QColor mixColor(const QColor& from, const QColor& to, double amount) {
    const auto channel = [amount](int a, int b) {
        return static_cast<int>(std::lround(a + (b - a) * amount));
    };
    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

QColor readableOn(const QColor& background) {
    return background.lightnessF() > 0.6 ? QColor(16, 20, 24) : QColor(250, 251, 252);
}

} // namespace sa::ui
