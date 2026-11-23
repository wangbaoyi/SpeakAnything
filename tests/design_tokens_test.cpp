#include "ui/design_tokens.h"

#include <QColor>

#include <cstdio>

using namespace sa::ui;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

} // namespace

int main() {
    check(color(ColorRole::Background) == QColor(QStringLiteral("#F5F7FB")), "background token");
    check(color(ColorRole::Sidebar) == QColor(QStringLiteral("#EEF3FA")), "sidebar token");
    check(color(ColorRole::Surface) == QColor(QStringLiteral("#FFFFFF")), "surface token");
    check(color(ColorRole::TextPrimary) == QColor(QStringLiteral("#1F2937")), "text primary token");
    check(color(ColorRole::TextSecondary) == QColor(QStringLiteral("#4B5563")), "text secondary token");
    check(color(ColorRole::TextMuted) == QColor(QStringLiteral("#5B6472")), "text muted token");
    check(color(ColorRole::Border) == QColor(QStringLiteral("#DDE3EC")), "border token");
    check(color(ColorRole::Focus) == QColor(QStringLiteral("#1D4ED8")), "focus token");
    check(color(ColorRole::Accent) == QColor(QStringLiteral("#1D4ED8")), "accent token");
    check(color(ColorRole::StatusSuccess) == QColor(QStringLiteral("#15803D")), "success token");
    check(color(ColorRole::StatusWarning) == QColor(QStringLiteral("#B45309")), "warning token");
    check(color(ColorRole::StatusDanger) == QColor(QStringLiteral("#DC2626")), "danger token");

    const QColor background = color(ColorRole::Background);
    const QColor surface = color(ColorRole::Surface);
    check(contrastRatio(color(ColorRole::TextPrimary), background) >= 4.5, "primary text contrast");
    check(contrastRatio(color(ColorRole::TextSecondary), background) >= 4.5,
          "secondary text contrast");
    check(contrastRatio(color(ColorRole::TextMuted), background) >= 4.5, "muted text contrast");
    check(contrastRatio(color(ColorRole::TextPrimary), surface) >= 4.5, "surface text contrast");
    check(contrastRatio(color(ColorRole::StatusSuccess), surface) >= 4.5, "success text contrast");
    check(contrastRatio(color(ColorRole::StatusWarning), surface) >= 4.5, "warning text contrast");
    check(contrastRatio(color(ColorRole::StatusDanger), surface) >= 4.5, "danger text contrast");
    check(contrastRatio(color(ColorRole::Focus), background) >= 3.0, "focus ring contrast");
    check(contrastRatio(color(ColorRole::Focus), surface) >= 3.0, "focus ring on surface");

    check(spacing_scale.front() == 4 && spacing_scale.back() == 32, "spacing scale");
    check(radius_scale.front() == 6 && radius_scale.back() == 12, "radius scale");
    check(font_size_scale.front() == 11 && font_size_scale.back() == 26, "font scale");
    check(page_title_size == 24 && card_title_size == 15 && body_size == 13, "type sizes");
    check(row_min_height == 44 && nav_item_height == 44, "row heights");
    check(border_width == 1 && focus_ring_width == 2, "stroke widths");

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("design-tokens-test: all checks passed\n");
    return 0;
}
