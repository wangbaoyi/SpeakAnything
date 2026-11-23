#pragma once

#include "ui/settings_types.h"

#include <QColor>
#include <QFont>

#include <optional>

namespace sa::ui {

using sa::settings::AppearanceSettings;
using sa::settings::BubbleStyle;
using sa::settings::ThemeColors;

// The colours each bubble style has always drawn; the Default theme keeps them.
[[nodiscard]] ThemeColors styleDefaultColors(BubbleStyle style);
// nullopt means "Default": every style keeps its own built-in palette.
[[nodiscard]] std::optional<ThemeColors> themeColors(const AppearanceSettings& appearance);
[[nodiscard]] int themeFontSize(const AppearanceSettings& appearance);
[[nodiscard]] QFont themeFont(const AppearanceSettings& appearance);
[[nodiscard]] QColor mixColor(const QColor& from, const QColor& to, double amount);
[[nodiscard]] QColor readableOn(const QColor& background);

} // namespace sa::ui
