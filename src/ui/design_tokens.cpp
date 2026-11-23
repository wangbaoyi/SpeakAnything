#include "ui/design_tokens.h"

#include <algorithm>
#include <cmath>

namespace sa::ui {
namespace {

double linearChannel(int value) {
    const double channel = static_cast<double>(value) / 255.0;
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double luminance(const QColor& color) {
    return 0.2126 * linearChannel(color.red()) + 0.7152 * linearChannel(color.green()) +
        0.0722 * linearChannel(color.blue());
}

} // namespace

QColor color(ColorRole role) {
    switch (role) {
    case ColorRole::Background: return QColor(0xF5, 0xF7, 0xFB);
    case ColorRole::Sidebar: return QColor(0xEE, 0xF3, 0xFA);
    case ColorRole::Surface: return QColor(0xFF, 0xFF, 0xFF);
    case ColorRole::SurfaceSubtle: return QColor(0xF8, 0xFA, 0xFD);
    case ColorRole::TextPrimary: return QColor(0x1F, 0x29, 0x37);
    case ColorRole::TextSecondary: return QColor(0x4B, 0x55, 0x63);
    case ColorRole::TextMuted: return QColor(0x5B, 0x64, 0x72);
    case ColorRole::Border: return QColor(0xDD, 0xE3, 0xEC);
    case ColorRole::Focus: return QColor(0x1D, 0x4E, 0xD8);
    case ColorRole::Selection: return QColor(0xEA, 0xF1, 0xFF);
    case ColorRole::Accent: return QColor(0x1D, 0x4E, 0xD8);
    case ColorRole::AccentHover: return QColor(0x1E, 0x40, 0xAF);
    case ColorRole::SliderFill: return QColor(0x1D, 0x4E, 0xD8);
    case ColorRole::StatusSuccess: return QColor(0x15, 0x80, 0x3D);
    case ColorRole::StatusWarning: return QColor(0xB4, 0x53, 0x09);
    case ColorRole::StatusDanger: return QColor(0xDC, 0x26, 0x26);
    case ColorRole::DisabledSurface: return QColor(0xF3, 0xF4, 0xF6);
    case ColorRole::DisabledText: return QColor(0x9C, 0xA3, 0xAF);
    }
    return QColor(Qt::black);
}

double contrastRatio(const QColor& first, const QColor& second) {
    const double a = luminance(first);
    const double b = luminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

} // namespace sa::ui
