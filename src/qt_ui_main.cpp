#include "audio_io.h"
#include "fsmn_vad_engine.h"
#include "sensevoice_engine.h"
#include "stream_recognizer.h"
#include "speech_player.h"
#include "speech_queue.h"
#include "system_audio_mute.h"
#include "text_processor.h"
#include "translator.h"
#include "windows_text_injector.h"

#include <QActionGroup>
#include <QApplication>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QCloseEvent>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFontComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMessageBox>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QRegion>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShowEvent>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct AccentPolicy {
    int state;
    int flags;
    DWORD gradient_color;
    int animation_id;
};

struct WindowCompositionAttributeData {
    int attribute;
    void* data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL (WINAPI *)(HWND, WindowCompositionAttributeData*);

struct DwmBlurBehind {
    DWORD flags;
    BOOL enable;
    HRGN blur_region;
    BOOL transition_on_maximized;
};

using DwmEnableBlurBehindWindowFn = HRESULT (WINAPI *)(HWND, const DwmBlurBehind*);
#endif

namespace {

constexpr int bubble_minimum_width = 132;
constexpr int bubble_maximum_size = 520;
constexpr int bubble_maximum_width = bubble_maximum_size;
constexpr int bubble_maximum_height = bubble_maximum_size;
constexpr int bubble_minimum_height = 32;
constexpr int bubble_horizontal_padding = 32;
constexpr int bubble_vertical_padding = 24;
constexpr int window_margin = 6;
constexpr int control_spacing = 8;

enum class BubbleStyle {
    Capsule,
    Panel,
    Ring,
};

QString bubbleStyleName(BubbleStyle style) {
    switch (style) {
    case BubbleStyle::Capsule:
        return QStringLiteral("capsule");
    case BubbleStyle::Panel:
        return QStringLiteral("panel");
    case BubbleStyle::Ring:
        return QStringLiteral("ring");
    }
    return QStringLiteral("panel");
}

BubbleStyle bubbleStyleFromName(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("capsule") || normalized == QStringLiteral("pill")) {
        return BubbleStyle::Capsule;
    }
    if (normalized == QStringLiteral("ring") || normalized == QStringLiteral("circle")) {
        return BubbleStyle::Ring;
    }
    return BubbleStyle::Panel;
}

QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string to_utf8_string(const QString& text) {
    const QByteArray bytes = text.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

struct VadSettings {
    int endpoint_ms = 700;
    int threshold_percent = 55;
    int minimum_db = -60;
    int snr_db = 3;
};

constexpr auto default_translation_separator = " / ";

struct TranslationSettings {
    bool enabled = false;
    TranslationDirection direction = TranslationDirection::ZhToEn;
    QString separator = QString::fromLatin1(default_translation_separator);
};

// 朗读: speak each session's 译文 into the 朗读设备 (see GLOSSARY.md).
// Voices are Kokoro v1.1 speaker ids.
constexpr int default_speech_voice_en = 0; // af_maple
constexpr int default_speech_voice_zh = 3; // zf_001

struct SpeechSettings {
    bool enabled = false;
    QString device_id;
    bool monitor = false;
    int voice_zh = default_speech_voice_zh;
    int voice_en = default_speech_voice_en;
    int speed_percent = 100;
};

// Speaker order inside voices.bin of kokoro-multi-lang-v1_1: three English
// voices, then the Chinese female voices, then the Chinese male voices.
QList<QPair<int, QString>> kokoroChineseVoices() {
    static constexpr int female[] = {1, 2, 3, 4, 5, 6, 7, 8, 17, 18, 19, 21, 22, 23, 24, 26, 27, 28, 32,
                                     36, 38, 39, 40, 42, 43, 44, 46, 47, 48, 49, 51, 59, 60, 67, 70, 71,
                                     72, 73, 74, 75, 76, 77, 78, 79, 83, 84, 85, 86, 87, 88, 90, 92, 93,
                                     94, 99};
    static constexpr int male[] = {9, 10, 11, 12, 13, 14, 15, 16, 20, 25, 29, 30, 31, 33, 34, 35, 37, 41,
                                   45, 50, 52, 53, 54, 55, 56, 57, 58, 61, 62, 63, 64, 65, 66, 68, 69,
                                   80, 81, 82, 89, 91, 95, 96, 97, 98, 100};
    QList<QPair<int, QString>> voices;
    int id = 3;
    for (const int number : female) {
        voices.append({id++, QStringLiteral("女声 %1").arg(number, 3, 10, QLatin1Char('0'))});
    }
    for (const int number : male) {
        voices.append({id++, QStringLiteral("男声 %1").arg(number, 3, 10, QLatin1Char('0'))});
    }
    return voices;
}

// The separator sits between the original and the translation on one line;
// line breaks could send a chat message, so they are never allowed.
QString sanitizedSeparator(QString separator) {
    separator.remove(QLatin1Char('\r'));
    separator.remove(QLatin1Char('\n'));
    if (separator.trimmed().isEmpty()) return QString::fromLatin1(default_translation_separator);
    return separator;
}

enum class ThemePreset { Default, Light, Dark, HighContrast, Custom };

struct ThemeColors {
    QColor background;
    QColor text;
    QColor accent;
};

struct AppearanceSettings {
    ThemePreset preset = ThemePreset::Default;
    ThemeColors custom{QColor(32, 37, 44), QColor(245, 246, 247), QColor(69, 184, 158)};
    QString custom_font_family;
    int custom_font_size = 10;
    int linger_ms = 500;
    bool pinned = false;
};

constexpr int linger_maximum_ms = 10'000;
constexpr int linger_step_ms = 500;

QString themePresetName(ThemePreset preset) {
    switch (preset) {
    case ThemePreset::Default: return QStringLiteral("default");
    case ThemePreset::Light: return QStringLiteral("light");
    case ThemePreset::Dark: return QStringLiteral("dark");
    case ThemePreset::HighContrast: return QStringLiteral("high-contrast");
    case ThemePreset::Custom: return QStringLiteral("custom");
    }
    return QStringLiteral("default");
}

ThemePreset themePresetFromName(const QString& name) {
    for (ThemePreset preset : {ThemePreset::Light, ThemePreset::Dark,
                               ThemePreset::HighContrast, ThemePreset::Custom}) {
        if (name == themePresetName(preset)) return preset;
    }
    return ThemePreset::Default;
}

// The colours each bubble style has always drawn; the Default theme keeps them.
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

// nullopt means "Default": every style keeps its own built-in palette.
std::optional<ThemeColors> themeColors(const AppearanceSettings& appearance) {
    switch (appearance.preset) {
    case ThemePreset::Default: return std::nullopt;
    case ThemePreset::Light: return ThemeColors{QColor(251, 252, 253), QColor(31, 37, 43), QColor(47, 158, 133)};
    case ThemePreset::Dark: return ThemeColors{QColor(30, 35, 42), QColor(238, 241, 244), QColor(69, 184, 158)};
    case ThemePreset::HighContrast: return ThemeColors{QColor(0, 0, 0), QColor(255, 255, 255), QColor(255, 212, 0)};
    case ThemePreset::Custom: return appearance.custom;
    }
    return std::nullopt;
}

int themeFontSize(const AppearanceSettings& appearance) {
    switch (appearance.preset) {
    case ThemePreset::Dark: return 11;
    case ThemePreset::HighContrast: return 12;
    case ThemePreset::Custom: return appearance.custom_font_size;
    default: return 10;
    }
}

QFont themeFont(const AppearanceSettings& appearance) {
    QFont font = QApplication::font();
    if (appearance.preset == ThemePreset::Custom && !appearance.custom_font_family.isEmpty()) {
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

enum class TextMode {
    Raw,
    Clean,
};

enum class ResultDestination { Insert, CopyOnly };
enum class ResultContent { Original, Translation, Both };
enum class HotkeyTrigger { Hold, Toggle };

constexpr int toggle_session_limit_ms = 5 * 60 * 1000;

constexpr int default_paste_delay_ms = 150;
constexpr int maximum_paste_delay_ms = 2000;

QString resultContentName(ResultContent content) {
    switch (content) {
    case ResultContent::Original: return QStringLiteral("original");
    case ResultContent::Translation: return QStringLiteral("translation");
    case ResultContent::Both: return QStringLiteral("both");
    }
    return QStringLiteral("original");
}

ResultContent resultContentFromName(const QString& name) {
    if (name == resultContentName(ResultContent::Translation)) return ResultContent::Translation;
    if (name == resultContentName(ResultContent::Both)) return ResultContent::Both;
    return ResultContent::Original;
}

constexpr auto default_hotkey = "Ctrl+Alt+Space";
constexpr auto control_windows_hotkey = "Ctrl+Win";

#ifdef _WIN32
constexpr auto windows_startup_run_key =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr auto windows_startup_value_name = "SenseVoice";

QString windowsStartupCommand() {
    const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    return QStringLiteral("\"%1\"").arg(executable);
}

QString windowsLegacyStartupShortcut() {
    const QString app_data = QString::fromLocal8Bit(qgetenv("APPDATA"));
    return QDir::toNativeSeparators(QDir(app_data).filePath(
        QStringLiteral("Microsoft/Windows/Start Menu/Programs/Startup/SenseVoice.lnk")));
}

bool windowsStartupEnabled() {
    QSettings startup_settings(
        QString::fromLatin1(windows_startup_run_key), QSettings::NativeFormat);
    const QString configured = startup_settings.value(
        QString::fromLatin1(windows_startup_value_name)).toString().trimmed();
    if (!configured.isEmpty()) {
        const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        if (configured.contains(executable, Qt::CaseInsensitive)) return true;
    }
    // Older installers used a Startup-folder shortcut. Treat it as enabled so
    // the first settings save can migrate it to the registry Run entry.
    return QFile::exists(windowsLegacyStartupShortcut());
}

bool setWindowsStartupEnabled(bool enabled) {
    QSettings startup_settings(
        QString::fromLatin1(windows_startup_run_key), QSettings::NativeFormat);
    const QString value_name = QString::fromLatin1(windows_startup_value_name);
    if (enabled) {
        startup_settings.setValue(value_name, windowsStartupCommand());
    } else {
        startup_settings.remove(value_name);
    }
    // Remove the legacy shortcut in both modes. Keeping two startup entries
    // would launch duplicate processes after an upgrade.
    QFile::remove(windowsLegacyStartupShortcut());
    startup_settings.sync();
    return startup_settings.status() == QSettings::NoError;
}
#endif

QString canonicalShortcut(const QString& value) {
    QString compact = value;
    compact.remove(QLatin1Char(' '));
    if (compact.compare(QString::fromLatin1(control_windows_hotkey), Qt::CaseInsensitive) == 0) {
        return QString::fromLatin1(control_windows_hotkey);
    }

    const QKeySequence sequence = QKeySequence::fromString(value, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown) {
        return {};
    }
    return sequence.toString(QKeySequence::PortableText);
}

bool hasUsableShortcutKey(const QString& shortcut) {
    if (shortcut == QString::fromLatin1(control_windows_hotkey)) return true;

    const QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
    if (sequence.isEmpty() || sequence.count() != 1) return false;
    const Qt::Key key = sequence[0].key();
    return key != Qt::Key_unknown && key != Qt::Key_Control && key != Qt::Key_Shift &&
        key != Qt::Key_Alt && key != Qt::Key_Meta;
}

QString shortcutDisplayName(const QString& shortcut) {
    if (shortcut == QString::fromLatin1(control_windows_hotkey)) {
        return QStringLiteral("Ctrl + Win");
    }
    const QKeySequence sequence = QKeySequence::fromString(shortcut, QKeySequence::PortableText);
    const QString native_text = sequence.toString(QKeySequence::NativeText);
    return native_text.isEmpty() ? shortcut : native_text;
}

QIcon sensevoiceIcon() {
    static const QIcon icon(QStringLiteral(":/icons/sensevoice.ico"));
    return icon;
}

class LevelWaveform final : public QWidget {
public:
    explicit LevelWaveform(QWidget* parent = nullptr)
        : QWidget(parent), ios9_animation_timer_(this) {
        setFixedSize(88, 24);
        ios9_animation_timer_.setInterval(16);
        ios9_animation_timer_.setTimerType(Qt::PreciseTimer);
        connect(&ios9_animation_timer_, &QTimer::timeout, this, [this] {
            advanceIos9Animation();
        });
    }

    void setCircular(bool circular) {
        circular_ = circular;
        if (circular_) {
            strip_ = false;
            setFixedSize(54, 54);
        } else if (!strip_) {
            setFixedSize(88, 24);
        }
        update();
    }

    void setStrip(bool strip) {
        strip_ = strip;
        if (strip_) {
            circular_ = false;
            setFixedSize(98, 24);
        } else if (circular_) {
            setFixedSize(54, 54);
        } else {
            setFixedSize(88, 24);
        }
        update();
    }

    void setTelemetry(float input_db, VadActivity activity) {
        input_db_ = input_db;
        activity_ = activity;
        if (strip_) {
            const float normalized = std::clamp((input_db_ + 60.0F) / 48.0F, 0.0F, 1.0F);
            ios9_target_amplitude_ = active_
                ? std::clamp((normalized - 0.08F) / 0.48F, 0.0F, 1.0F)
                : 0.0F;
        } else {
            phase_ += 0.32F;
        }
        update();
    }

    void setActive(bool active) {
        const bool was_active = active_;
        active_ = active;
        if (strip_) {
            if (active_) {
                if (!was_active) {
                    resetIos9Animation();
                    ios9_clock_.restart();
                }
                ios9_animation_timer_.start();
            } else {
                ios9_animation_timer_.stop();
                ios9_target_amplitude_ = 0.0F;
                ios9_amplitude_ = 0.0F;
            }
        }
        update();
    }

    void setPreviewSignal() {
        setActive(true);
        ios9_target_amplitude_ = 1.0F;
        for (int frame = 0; frame < 42; ++frame) advanceIos9Animation();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing |
                               QPainter::TextAntialiasing |
                               QPainter::SmoothPixmapTransform);
        const float normalized_level = active_
            ? std::clamp((input_db_ + 60.0F) / 48.0F, 0.0F, 1.0F)
            : 0.0F;
        QColor color(116, 121, 130);
        if (activity_ == VadActivity::Candidate) color = QColor(232, 128, 104);
        else if (activity_ == VadActivity::Speech) color = QColor(243, 184, 75);
        else if (activity_ == VadActivity::EndpointWait) color = QColor(103, 193, 170);
        else if (circular_) color = QColor(78, 211, 178);

        if (circular_) {
            const QPointF center(width() / 2.0, height() / 2.0);
            const qreal radius = std::min(width(), height()) / 2.0 - 2.0;
            const qreal wave_radius = radius - 5.0;
            const float level = active_
                ? 0.55F + normalized_level * 0.85F
                : 0.50F + 0.06F * std::sin(phase_ * 0.35F);

            // SiriWave-style construction: several attenuated sine curves with
            // independent phase/frequency, clipped to a compact circular orb.
            QRadialGradient glow(center, radius * 0.95);
            const int glow_alpha = active_ ? 34 + static_cast<int>(normalized_level * 34.0F) : 18;
            glow.setColorAt(0.0, QColor(color.red(), color.green(), color.blue(), glow_alpha));
            glow.setColorAt(0.55, QColor(color.red(), color.green(), color.blue(), glow_alpha / 3));
            glow.setColorAt(1.0, QColor(color.red(), color.green(), color.blue(), 0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            painter.drawEllipse(center, radius, radius);

            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(78, 211, 178, active_ ? 190 : 120),
                                active_ ? 1.4 : 1.1));
            painter.drawEllipse(center, radius - 3.0, radius - 3.0);

            QPainterPath clip_path;
            clip_path.addEllipse(center, wave_radius, wave_radius);
            painter.save();
            painter.setClipPath(clip_path);

            const std::array<float, 5> amplitudes = {0.66F, 0.88F, 1.0F, 0.84F, 0.62F};
            const std::array<float, 5> frequencies = {2.1F, 2.55F, 2.9F, 2.45F, 2.0F};
            const std::array<float, 5> phase_offsets = {0.25F, 1.45F, 2.4F, 3.35F, 4.7F};
            const std::array<QColor, 5> wave_colors = {
                QColor(87, 155, 255),
                QColor(70, 218, 224),
                QColor(92, 238, 173),
                QColor(174, 135, 255),
                QColor(255, 126, 176),
            };
            constexpr int sample_count = 44;
            const qreal vertical_scale = wave_radius * 0.72 * level;
            for (std::size_t curve = 0; curve < amplitudes.size(); ++curve) {
                QPainterPath path;
                for (int sample = 0; sample <= sample_count; ++sample) {
                    const qreal x = -1.0 + 2.0 * sample / sample_count;
                    const qreal attenuation = std::pow(std::max(0.0, 1.0 - x * x), 1.15);
                    const qreal wave = std::sin(
                        frequencies[curve] * M_PI * x + phase_ * (0.72 + curve * 0.08) +
                        phase_offsets[curve]);
                    const qreal y = wave * attenuation * vertical_scale * amplitudes[curve];
                    const QPointF point(center.x() + x * wave_radius,
                                        center.y() + y);
                    if (sample == 0) path.moveTo(point);
                    else path.lineTo(point);
                }

                const QColor wave_color = wave_colors[curve];
                painter.setPen(QPen(QColor(wave_color.red(), wave_color.green(), wave_color.blue(),
                                           active_ ? 52 : 34),
                                    active_ ? 4.0 : 3.2,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
                painter.setPen(QPen(QColor(wave_color.red(), wave_color.green(), wave_color.blue(),
                                           active_ ? 218 : 172),
                                    active_ ? 1.5 : 1.2,
                                    Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter.drawPath(path);
            }
            painter.restore();

            QRadialGradient core(center, active_ ? 6.5 : 4.8);
            core.setColorAt(0.0, QColor(239, 255, 250, active_ ? 245 : 220));
            core.setColorAt(0.35, QColor(111, 239, 198, active_ ? 180 : 130));
            core.setColorAt(1.0, QColor(78, 211, 178, 0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(core);
            painter.drawEllipse(center, active_ ? 6.5 : 4.8, active_ ? 6.5 : 4.8);
            painter.setBrush(QColor(241, 255, 251, active_ ? 245 : 215));
            painter.drawEllipse(center, active_ ? 2.5 : 2.2, active_ ? 2.5 : 2.2);
            return;
        }

        if (strip_) {
            drawIos9Waveform(painter);
            return;
        }

        painter.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap));
        constexpr int bars = 16;
        constexpr qreal gap = 5.4;
        const qreal first_x = (width() - (bars - 1) * gap) / 2.0;
        for (int index = 0; index < bars; ++index) {
            const float wave = 0.35F + 0.65F * std::abs(std::sin(phase_ + index * 0.82F));
            const qreal line_height = active_ ? 2.0 + normalized_level * (4.0 + wave * 13.0) : 2.0;
            const qreal x = first_x + index * gap;
            painter.drawLine(QPointF(x, height() / 2.0 - line_height / 2.0),
                             QPointF(x, height() / 2.0 + line_height / 2.0));
        }
    }

private:
    struct Ios9CurveState {
        int no_of_curves = 0;
        qint64 spawn_at_ms = 0;
        float previous_max_y = 0.0F;
        std::array<float, 5> phases{};
        std::array<float, 5> amplitudes{};
        std::array<float, 5> despawn_timeouts{};
        std::array<float, 5> offsets{};
        std::array<float, 5> speeds{};
        std::array<float, 5> final_amplitudes{};
        std::array<float, 5> widths{};
        std::array<float, 5> verses{};
    };

    float randomRange(float minimum, float maximum) {
        std::uniform_real_distribution<float> distribution(minimum, maximum);
        return distribution(random_engine_);
    }

    static qreal ios9GlobalAttenuation(qreal x) {
        constexpr qreal attack_factor = 4.0;
        return std::pow(attack_factor / (attack_factor + std::pow(x, 2.0)), attack_factor);
    }

    void spawnIos9Layer(Ios9CurveState& layer) {
        layer = Ios9CurveState{};
        layer.spawn_at_ms = ios9_clock_.elapsed();
        layer.no_of_curves = static_cast<int>(std::floor(randomRange(2.0F, 5.0F)));
        for (int index = 0; index < layer.no_of_curves; ++index) {
            layer.despawn_timeouts[index] = randomRange(500.0F, 2000.0F);
            layer.offsets[index] = randomRange(-3.0F, 3.0F);
            layer.speeds[index] = randomRange(0.5F, 1.0F);
            layer.final_amplitudes[index] = randomRange(0.3F, 1.0F);
            layer.widths[index] = randomRange(1.0F, 3.0F);
            layer.verses[index] = randomRange(-1.0F, 1.0F);
        }
    }

    void resetIos9Animation() {
        ios9_amplitude_ = 0.0F;
        ios9_target_amplitude_ = 0.0F;
        for (Ios9CurveState& layer : ios9_layers_) layer = Ios9CurveState{};
    }

    void advanceIos9Animation() {
        if (!strip_ || !active_) return;

        // This is SiriWave's lerpSpeed (0.1) and speed (0.2), sampled at
        // roughly the same 60 Hz cadence as the original canvas animation.
        ios9_amplitude_ += (ios9_target_amplitude_ - ios9_amplitude_) * 0.1F;
        const qint64 now_ms = ios9_clock_.elapsed();
        for (Ios9CurveState& layer : ios9_layers_) {
            if (layer.no_of_curves == 0) spawnIos9Layer(layer);
            for (int index = 0; index < layer.no_of_curves; ++index) {
                if (layer.spawn_at_ms + layer.despawn_timeouts[index] <= now_ms) {
                    layer.amplitudes[index] -= 0.02F;
                } else {
                    layer.amplitudes[index] += 0.02F;
                }
                layer.amplitudes[index] = std::clamp(
                    layer.amplitudes[index], 0.0F, layer.final_amplitudes[index]);
                layer.phases[index] = std::fmod(
                    layer.phases[index] + 0.2F * layer.speeds[index],
                    static_cast<float>(2.0 * M_PI));
            }
        }
        update();
    }

    qreal ios9RelativePosition(const Ios9CurveState& layer, qreal i) const {
        qreal y = 0.0;
        for (int index = 0; index < layer.no_of_curves; ++index) {
            const qreal t = 4.0 * (-1.0 +
                (static_cast<qreal>(index) / (layer.no_of_curves - 1)) * 2.0) +
                layer.offsets[index];
            const qreal x = i / layer.widths[index] - t;
            y += std::abs(layer.amplitudes[index] *
                std::sin(layer.verses[index] * x - layer.phases[index]) *
                ios9GlobalAttenuation(x));
        }
        return layer.no_of_curves == 0 ? 0.0 : y / layer.no_of_curves;
    }

    void drawIos9Waveform(QPainter& painter) {
        constexpr qreal graph_x = 25.0;
        constexpr qreal amplitude_factor = 3.8;
        const qreal height_max = height() / 2.0;
        const qreal baseline = height_max;

        QLinearGradient support_line(0.0, 0.0, width(), 0.0);
        support_line.setColorAt(0.0, QColor(255, 255, 255, 0));
        support_line.setColorAt(0.1, QColor(255, 255, 255, 128));
        support_line.setColorAt(0.8, QColor(255, 255, 255, 128));
        support_line.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(support_line);
        painter.drawRect(QRectF(0.0, baseline, width(), 1.0));

        if (!active_ || ios9_amplitude_ <= 0.001F) return;

        static constexpr std::array<QColor, 3> colors = {
            QColor(15, 82, 169),
            QColor(173, 57, 76),
            QColor(48, 220, 155),
        };
        painter.save();
        painter.setOpacity(0.7);
        painter.setCompositionMode(QPainter::CompositionMode_Plus);
        for (std::size_t layer_index = 0; layer_index < colors.size(); ++layer_index) {
            Ios9CurveState& layer = ios9_layers_[layer_index];
            if (layer.no_of_curves == 0) spawnIos9Layer(layer);
            qreal maximum_y = -std::numeric_limits<qreal>::infinity();
            for (const qreal sign : {1.0, -1.0}) {
                QPainterPath path;
                bool first_point = true;
                for (qreal i = -graph_x; i <= graph_x; i += 0.02) {
                    const qreal x = width() * ((i + graph_x) / (graph_x * 2.0));
                    const qreal raw_y = amplitude_factor * height_max * ios9_amplitude_ *
                        ios9RelativePosition(layer, i) *
                        ios9GlobalAttenuation((i / graph_x) * 2.0);
                    const qreal y = std::clamp(raw_y, 0.0, std::max(0.0, height_max - 1.0));
                    const QPointF point(x, baseline - sign * y);
                    if (first_point) {
                        path.moveTo(point);
                        first_point = false;
                    } else {
                        path.lineTo(point);
                    }
                    maximum_y = std::max(maximum_y, y);
                }
                path.closeSubpath();
                painter.setBrush(colors[layer_index]);
                painter.setPen(QPen(colors[layer_index], 1.0));
                painter.drawPath(path);
            }
            if (maximum_y < 2.0 && layer.previous_max_y > maximum_y) {
                layer = Ios9CurveState{};
            }
            layer.previous_max_y = static_cast<float>(maximum_y);
        }
        painter.restore();
    }

    float input_db_ = -100.0F;
    float phase_ = 0.0F;
    VadActivity activity_ = VadActivity::Silence;
    bool active_ = false;
    bool circular_ = false;
    bool strip_ = false;
    QTimer ios9_animation_timer_;
    QElapsedTimer ios9_clock_;
    std::mt19937 random_engine_{std::random_device{}()};
    std::array<Ios9CurveState, 3> ios9_layers_{};
    float ios9_amplitude_ = 0.0F;
    float ios9_target_amplitude_ = 0.0F;
};

class VadStatusDot final : public QWidget {
public:
    explicit VadStatusDot(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(8, 8);
        setToolTip(QStringLiteral("VAD 静音"));
    }

    void setActivity(VadActivity activity) {
        activity_ = activity;
        switch (activity_) {
        case VadActivity::Speech:
            setToolTip(QStringLiteral("VAD 已触发：语音"));
            break;
        case VadActivity::EndpointWait:
            setToolTip(QStringLiteral("VAD 已触发：等待句尾"));
            break;
        case VadActivity::Candidate:
            setToolTip(QStringLiteral("VAD 候选"));
            break;
        case VadActivity::Silence:
            setToolTip(QStringLiteral("VAD 静音"));
            break;
        }
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QColor color(220, 82, 92);
        if (activity_ == VadActivity::Candidate) color = QColor(242, 183, 74);
        else if (activity_ == VadActivity::Speech || activity_ == VadActivity::EndpointWait) {
            color = QColor(78, 220, 157);
        }

        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing |
                               QPainter::TextAntialiasing |
                               QPainter::SmoothPixmapTransform);
        const QPointF center(width() / 2.0, height() / 2.0);
        QRadialGradient glow(center, 4.0);
        glow.setColorAt(0.0, QColor(color.red(), color.green(), color.blue(), 155));
        glow.setColorAt(0.65, QColor(color.red(), color.green(), color.blue(), 45));
        glow.setColorAt(1.0, QColor(color.red(), color.green(), color.blue(), 0));
        painter.setPen(Qt::NoPen);
        painter.setBrush(glow);
        painter.drawEllipse(center, 4.0, 4.0);
        painter.setBrush(color);
        painter.drawEllipse(center, 2.5, 2.5);
    }

private:
    VadActivity activity_ = VadActivity::Silence;
};

class RecordingControl final : public QWidget {
public:
    explicit RecordingControl(BubbleStyle style = BubbleStyle::Ring, QWidget* parent = nullptr)
        : QWidget(parent), style_(style) {
        setObjectName(QStringLiteral("recordingControl"));
        setAttribute(Qt::WA_StyledBackground, true);
        layout_ = new QHBoxLayout(this);
        layout_->setContentsMargins(8, 7, 8, 7);
        layout_->setSpacing(8);

        cancel_button_ = new QToolButton;
        cancel_button_->setIcon(QWidget::style()->standardIcon(QStyle::SP_DialogCancelButton));
        cancel_button_->setToolTip(QStringLiteral("取消"));
        vad_dot_ = new VadStatusDot;
        waveform_ = new LevelWaveform;
        elapsed_label_ = new QLabel(QStringLiteral("00:00"));
        elapsed_label_->setFixedWidth(42);
        elapsed_label_->setAlignment(Qt::AlignCenter);
        elapsed_label_->setStyleSheet(QStringLiteral(
            "color: #D6D8DC; font-family: Consolas; font-size: 11px;"));
        mode_label_ = new QLabel(QStringLiteral("精简"));
        mode_label_->setObjectName(QStringLiteral("mode"));
        mode_label_->setFixedWidth(38);
        mode_label_->setAlignment(Qt::AlignCenter);
        mode_label_->setStyleSheet(QStringLiteral("color: #969BA4; font-size: 10px;"));
        primary_button_ = new QToolButton;
        primary_button_->setObjectName(QStringLiteral("primary"));
        primary_button_->setToolTip(QStringLiteral("开始"));
        translate_button_ = new QToolButton;
        translate_button_->setObjectName(QStringLiteral("translate"));
        translate_button_->setText(QStringLiteral("译"));
        translate_button_->setCheckable(true);
        direction_button_ = new QToolButton;
        direction_button_->setObjectName(QStringLiteral("direction"));
        direction_button_->setToolButtonStyle(Qt::ToolButtonTextOnly);
        close_button_ = new QToolButton;
        close_button_->setObjectName(QStringLiteral("close"));
        close_button_->setText(QStringLiteral("×"));
        close_button_->setToolTip(QStringLiteral("关闭浮窗"));
        close_button_->setVisible(false);

        layout_->addWidget(vad_dot_);
        layout_->addWidget(cancel_button_);
        layout_->addWidget(waveform_);
        layout_->addWidget(elapsed_label_);
        layout_->addWidget(mode_label_);
        layout_->addWidget(translate_button_);
        layout_->addWidget(direction_button_);
        layout_->addStretch();
        layout_->addWidget(primary_button_);
        layout_->addWidget(close_button_);

        connect(close_button_, &QToolButton::clicked, this, [this] {
            if (close_handler_) close_handler_();
        });
        connect(cancel_button_, &QToolButton::clicked, this, [this] {
            if (cancel_handler_) cancel_handler_();
        });
        connect(primary_button_, &QToolButton::clicked, this, [this] {
            if (primary_handler_) primary_handler_();
        });
        // The checked state is owned by the window; undo Qt's own toggle so a
        // rejected request never leaves the button showing the wrong state.
        connect(translate_button_, &QToolButton::clicked, this, [this] {
            translate_button_->setChecked(translation_enabled_);
            if (translate_handler_) translate_handler_();
        });
        connect(direction_button_, &QToolButton::clicked, this, [this] {
            if (direction_handler_) direction_handler_();
        });
        spinner_timer_.setInterval(120);
        connect(&spinner_timer_, &QTimer::timeout, this, [this] {
            spinner_frame_ = (spinner_frame_ + 1) % 4;
            updateDirectionText();
        });
        setVisualStyle(style_);
        setListening(false);
        setTranslationState(false, TranslationDirection::ZhToEn, false);
    }

    void setVisualStyle(BubbleStyle style) {
        style_ = style;
        const int width = style == BubbleStyle::Capsule ? 384 :
            (style == BubbleStyle::Ring ? 214 : 366);
        const int height = style == BubbleStyle::Ring ? 32 :
            (style == BubbleStyle::Capsule ? 54 : 48);
        setFixedSize(width, height);
        if (style == BubbleStyle::Ring) {
            layout_->setContentsMargins(10, 4, 10, 4);
            layout_->setSpacing(5);
        } else {
            layout_->setContentsMargins(8, 7, 8, 7);
            layout_->setSpacing(8);
        }
        waveform_->setCircular(false);
        waveform_->setStrip(style == BubbleStyle::Ring);
        if (style != BubbleStyle::Ring) waveform_->setFixedSize(88, 24);
        cancel_button_->setVisible(style != BubbleStyle::Ring);
        elapsed_label_->setVisible(style != BubbleStyle::Ring);
        primary_button_->setVisible(style != BubbleStyle::Ring);
        mode_label_->setVisible(style != BubbleStyle::Ring);
        applyColors();
    }

    void setTheme(std::optional<ThemeColors> theme) {
        theme_ = std::move(theme);
        applyColors();
        update();
    }

    void setPinned(bool pinned) {
        close_button_->setVisible(pinned);
    }

    void setCloseHandler(std::function<void()> close) {
        close_handler_ = std::move(close);
    }

    void setHandlers(std::function<void()> primary, std::function<void()> cancel) {
        primary_handler_ = std::move(primary);
        cancel_handler_ = std::move(cancel);
    }

    void setTranslationHandlers(std::function<void()> toggle, std::function<void()> flip_direction) {
        translate_handler_ = std::move(toggle);
        direction_handler_ = std::move(flip_direction);
    }

    // locked: a recording is in progress, so the session keeps its settings.
    void setTranslationState(bool enabled, TranslationDirection direction, bool loading,
                             bool locked = false) {
        translation_enabled_ = enabled;
        translation_direction_ = direction;
        translation_loading_ = enabled && loading;
        translate_button_->setChecked(enabled);
        translate_button_->setEnabled(!locked);
        direction_button_->setEnabled(!locked);
        direction_button_->setProperty("active", enabled && !translation_loading_);
        direction_button_->style()->unpolish(direction_button_);
        direction_button_->style()->polish(direction_button_);
        translate_button_->setToolTip(enabled
            ? QStringLiteral("翻译模式：开（点击关闭）")
            : QStringLiteral("翻译模式：关（点击开启）"));
        direction_button_->setToolTip(translation_loading_
            ? QStringLiteral("正在加载翻译模型...")
            : QStringLiteral("翻译方向（点击切换）"));
        if (translation_loading_) {
            if (!spinner_timer_.isActive()) spinner_timer_.start();
        } else {
            spinner_timer_.stop();
            spinner_frame_ = 0;
        }
        updateDirectionText();
    }

    void setListening(bool listening) {
        listening_ = listening;
        waveform_->setActive(listening);
        cancel_button_->setEnabled(listening);
        primary_button_->setEnabled(true);
        primary_button_->setIcon(QWidget::style()->standardIcon(
            listening ? QStyle::SP_DialogApplyButton : QStyle::SP_MediaPlay));
        primary_button_->setToolTip(listening ? QStringLiteral("完成") : QStringLiteral("开始"));
        close_button_->setEnabled(!listening);
        elapsed_label_->setText(listening ? QStringLiteral("00:00") : QStringLiteral("就绪"));
    }

    void setStopping(bool stopping) {
        waveform_->setActive(!stopping && listening_);
        cancel_button_->setEnabled(!stopping && listening_);
        primary_button_->setEnabled(!stopping);
        close_button_->setEnabled(!stopping && !listening_);
        if (stopping) elapsed_label_->setText(QStringLiteral("处理中"));
    }

    void setTelemetry(float input_db, VadActivity activity) {
        waveform_->setTelemetry(input_db, activity);
        vad_dot_->setActivity(activity);
    }

    void setPreviewSignal() {
        setListening(true);
        waveform_->setPreviewSignal();
        waveform_->setTelemetry(-30.0F, VadActivity::Speech);
        vad_dot_->setActivity(VadActivity::Speech);
    }

    void setElapsedMilliseconds(qint64 milliseconds) {
        if (!listening_) return;
        const qint64 seconds = std::max<qint64>(0, milliseconds / 1000);
        elapsed_label_->setText(QStringLiteral("%1:%2")
                                    .arg(seconds / 60, 2, 10, QLatin1Char('0'))
                                    .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    }

    void setMode(TextMode mode) {
        mode_label_->setText(mode == TextMode::Raw ? QStringLiteral("原文") : QStringLiteral("精简"));
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        if (style_ == BubbleStyle::Ring) {
            QPainter painter(this);
            painter.setRenderHints(QPainter::Antialiasing |
                                   QPainter::TextAntialiasing |
                                   QPainter::SmoothPixmapTransform);
            const QRectF control_rect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
            if (theme_) {
                painter.setPen(QPen(mixColor(theme_->background, theme_->text, 0.25), 1.0));
                painter.setBrush(theme_->background);
            } else {
                painter.setPen(QPen(QColor(235, 236, 240, 78), 1.0));
                painter.setBrush(QColor(78, 80, 86, 72));
            }
            painter.drawRoundedRect(control_rect, height() / 2.0, height() / 2.0);
        }
        QWidget::paintEvent(event);
    }

private:
    void applyColors() {
        if (theme_) {
            setStyleSheet(themedStyleSheet(style_, *theme_));
            elapsed_label_->setStyleSheet(QStringLiteral("color: %1; font-family: Consolas; font-size: 11px;")
                                              .arg(theme_->text.name()));
            mode_label_->setStyleSheet(QStringLiteral("color: %1; font-size: 10px;")
                                           .arg(mixColor(theme_->text, theme_->background, 0.35).name()));
        } else {
            setStyleSheet(styleSheetFor(style_));
            elapsed_label_->setStyleSheet(QStringLiteral(
                "color: #D6D8DC; font-family: Consolas; font-size: 11px;"));
            mode_label_->setStyleSheet(QStringLiteral("color: #969BA4; font-size: 10px;"));
        }
    }

    static QString themedStyleSheet(BubbleStyle style, const ThemeColors& c) {
        const bool ring = style == BubbleStyle::Ring;
        const int button_size = ring ? 24 : 30;
        const int button_radius = style == BubbleStyle::Capsule ? 15 : (ring ? 12 : 7);
        const QString container = ring
            ? QStringLiteral("QWidget#recordingControl { background: transparent; border: none; }")
            : QStringLiteral("QWidget#recordingControl { background: %1; border: 1px solid %2; border-radius: %3px; }")
                  .arg(c.background.name(), mixColor(c.background, c.text, 0.25).name())
                  .arg(style == BubbleStyle::Capsule ? 27 : 10);
        const QString button = mixColor(c.background, c.text, 0.14).name();
        const QString muted = mixColor(c.text, c.background, 0.35).name();
        return container + QStringLiteral(
            "QToolButton { width: %1px; height: %1px; border: none; border-radius: %2px; background: %3; color: %4; }"
            "QToolButton:hover { background: %5; }"
            "QToolButton:disabled { background: %6; color: %7; }"
            "QToolButton#primary { background: %8; color: %9; }"
            "QToolButton#primary:hover { background: %10; }"
            "QLabel { border: none; background: transparent; color: %4; }"
            "QToolButton#translate { font-size: %11px; font-weight: 600; color: %12; }"
            "QToolButton#translate:checked { background: %8; color: %9; }"
            "QToolButton#direction { width: %13px; font-size: %14px; background: transparent; color: %12; }"
            "QToolButton#direction[active=\"true\"] { color: %4; }"
            "QToolButton#direction:hover { background: %3; }"
            "QToolButton#close { font-size: 14px; }")
            .arg(button_size).arg(button_radius)
            .arg(button, c.text.name(), mixColor(c.background, c.text, 0.22).name(),
                 mixColor(c.background, c.text, 0.08).name(), mixColor(c.text, c.background, 0.5).name(),
                 c.accent.name(), readableOn(c.accent).name())
            .arg(c.accent.lighter(115).name())
            .arg(ring ? 11 : 12)
            .arg(muted)
            .arg(ring ? 52 : 60)
            .arg(ring ? 10 : 11);
    }

    static QString styleSheetFor(BubbleStyle style) {
        switch (style) {
        case BubbleStyle::Capsule:
            return QStringLiteral(
                "QWidget#recordingControl { background: #14252B; border: 1px solid #31545A; border-radius: 27px; }"
                "QToolButton { width: 30px; height: 30px; border: none; border-radius: 15px; background: #213A40; color: #F1FCF8; }"
                "QToolButton:hover { background: #2D4B50; }"
                "QToolButton:disabled { background: #1C3035; color: #6E8D8D; }"
                "QToolButton#primary { background: #63D9B5; color: #0C2927; }"
                "QToolButton#primary:hover { background: #81E7C7; }"
                "QLabel { border: none; background: transparent; color: #D7F2EA; }"
                "QLabel#mode { color: #84B4AC; }"
                "QToolButton#translate { font-size: 12px; font-weight: 600; color: #84B4AC; }"
                "QToolButton#translate:checked { background: #2F7F6A; color: #F1FCF8; }"
                "QToolButton#direction { width: 60px; font-size: 11px; background: transparent; color: #5E7F7C; }"
                "QToolButton#direction[active=\"true\"] { color: #D7F2EA; }"
                "QToolButton#direction:hover { background: #213A40; }");
        case BubbleStyle::Ring:
            return QStringLiteral(
                "QWidget#recordingControl { background: transparent; border: none; }"
                "QToolButton { width: 24px; height: 24px; border: none; border-radius: 12px; background: #5C5D63; color: #F2F2F7; }"
                "QToolButton:hover { background: #6B6C72; }"
                "QToolButton:disabled { background: #505158; color: #A5A6AC; }"
                "QToolButton#primary { background: #D1D1D6; color: #1C1C1E; }"
                "QToolButton#primary:hover { background: #E5E5EA; }"
                "QLabel { border: none; background: transparent; color: #F2F2F7; }"
                "QToolButton#translate { font-size: 11px; font-weight: 600; background: transparent; color: #A5A6AC; }"
                "QToolButton#translate:checked { background: #45B89E; color: #10201C; }"
                "QToolButton#direction { width: 52px; font-size: 10px; background: transparent; color: #8E8F95; }"
                "QToolButton#direction[active=\"true\"] { color: #F2F2F7; }"
                "QToolButton#direction:hover { background: #5C5D63; }");
        case BubbleStyle::Panel:
            return QStringLiteral(
                "QWidget#recordingControl { background: #20252C; border: 1px solid #3A424B; border-radius: 10px; }"
                "QToolButton { width: 30px; height: 30px; border: none; border-radius: 7px; background: #2D333B; color: #F5F6F7; }"
                "QToolButton:hover { background: #3A424C; }"
                "QToolButton:disabled { background: #252A30; color: #737B85; }"
                "QToolButton#primary { background: #F1F4F6; color: #1D2329; }"
                "QToolButton#primary:hover { background: #FFFFFF; }"
                "QLabel { border: none; background: transparent; color: #D6DBE1; }"
                "QToolButton#translate { font-size: 12px; font-weight: 600; color: #9AA3AD; }"
                "QToolButton#translate:checked { background: #45B89E; color: #10201C; }"
                "QToolButton#direction { width: 60px; font-size: 11px; background: transparent; color: #737B85; }"
                "QToolButton#direction[active=\"true\"] { color: #F5F6F7; }"
                "QToolButton#direction:hover { background: #2D333B; }");
        }
        return {};
    }

    void updateDirectionText() {
        static const std::array<QString, 4> spinner = {
            QStringLiteral("◐"), QStringLiteral("◓"), QStringLiteral("◑"), QStringLiteral("◒")};
        QString text = translation_direction_ == TranslationDirection::ZhToEn
            ? QStringLiteral("中→英")
            : QStringLiteral("英→中");
        if (translation_loading_) {
            text = spinner[static_cast<std::size_t>(spinner_frame_)] + QLatin1Char(' ') + text;
        }
        direction_button_->setText(text);
    }

    QHBoxLayout* layout_ = nullptr;
    QToolButton* cancel_button_ = nullptr;
    QToolButton* primary_button_ = nullptr;
    QToolButton* translate_button_ = nullptr;
    QToolButton* direction_button_ = nullptr;
    QToolButton* close_button_ = nullptr;
    std::function<void()> close_handler_;
    std::optional<ThemeColors> theme_;
    QTimer spinner_timer_;
    int spinner_frame_ = 0;
    bool translation_enabled_ = false;
    bool translation_loading_ = false;
    TranslationDirection translation_direction_ = TranslationDirection::ZhToEn;
    std::function<void()> translate_handler_;
    std::function<void()> direction_handler_;
    VadStatusDot* vad_dot_ = nullptr;
    LevelWaveform* waveform_ = nullptr;
    QLabel* elapsed_label_ = nullptr;
    QLabel* mode_label_ = nullptr;
    std::function<void()> primary_handler_;
    std::function<void()> cancel_handler_;
    BubbleStyle style_ = BubbleStyle::Ring;
    bool listening_ = false;
};

class TranscriptBubble final : public QWidget {
public:
    explicit TranscriptBubble(BubbleStyle style = BubbleStyle::Ring, QWidget* parent = nullptr)
        : QWidget(parent), style_(style) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        QFont text_font = font();
        text_font.setPointSize(10);
        text_font.setStyleStrategy(QFont::PreferAntialias);
        setFont(text_font);
        base_font_ = text_font;
        document_.setDocumentMargin(0);
        document_.setDefaultFont(text_font);
        QTextOption text_option;
        text_option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        document_.setDefaultTextOption(text_option);
    }

    void setStyle(BubbleStyle style) {
        style_ = style;
        setBubbleContent(text_, secondary_, secondary_tone_);
    }

    void setTheme(std::optional<ThemeColors> theme, const QFont& font) {
        theme_ = std::move(theme);
        base_font_ = font;
        setFont(font);
        setBubbleContent(text_, secondary_, secondary_tone_);
    }

    [[nodiscard]] int cornerRadius() const {
        if (style_ == BubbleStyle::Ring) {
            return static_cast<int>(std::max(1.0, std::min(15.0, height() / 2.0 - 1.0)));
        }
        if (style_ == BubbleStyle::Capsule) {
            return static_cast<int>(std::max(1.0, std::min(20.0, height() / 2.0 - 1.0)));
        }
        return std::min(10, std::min(width(), height()) / 2);
    }

    // How the optional second line (the translation) is drawn.
    enum class SecondaryTone { Muted, Pending, Warning };

    void setBubbleText(const QString& text) {
        setBubbleContent(text, {}, SecondaryTone::Muted);
    }

    void setBubbleContent(const QString& text, const QString& secondary, SecondaryTone tone) {
        text_ = text;
        secondary_ = secondary;
        secondary_tone_ = tone;
        document_.setDefaultFont(base_font_);
        document_.setPlainText(text);
        if (!secondary.isEmpty()) {
            QTextCursor cursor(&document_);
            cursor.movePosition(QTextCursor::End);
            QTextBlockFormat block_format;
            block_format.setTopMargin(4.0);
            QTextCharFormat char_format;
            char_format.setFontPointSize(std::max(7, base_font_.pointSize() - 1));
            char_format.setForeground(secondaryColor(tone));
            cursor.insertBlock(block_format, char_format);
            cursor.insertText(secondary, char_format);
        }
        document_.setTextWidth(-1);

        const Metrics metrics = metricsFor(style_);
        QTextOption text_option = document_.defaultTextOption();
        text_option.setAlignment(style_ == BubbleStyle::Panel
            ? Qt::AlignLeft
            : Qt::AlignHCenter);
        document_.setDefaultTextOption(text_option);
        const int natural_width = static_cast<int>(std::ceil(document_.idealWidth()));
        int horizontal_padding = metrics.horizontal_padding;
        int content_width = 1;
        // A pill's top and bottom corners consume more horizontal space as it
        // grows taller. Increase the text inset until the entire text box is
        // inside the pill, including multi-line first and last rows.
        for (int iteration = 0; iteration < 12; ++iteration) {
            const int minimum_content_width = std::max(1, metrics.minimum_width - horizontal_padding);
            const int maximum_content_width = std::max(
                minimum_content_width, metrics.maximum_width - horizontal_padding);
            content_width = std::clamp(natural_width, minimum_content_width, maximum_content_width);
            document_.setTextWidth(content_width);

            const int candidate_width = std::clamp(
                content_width + horizontal_padding, metrics.minimum_width, metrics.maximum_width);
            const int candidate_height = std::max(
                static_cast<int>(std::ceil(document_.size().height())) + metrics.vertical_padding + 2,
                metrics.minimum_height);
            const qreal radius = std::min(candidate_width, candidate_height) / 2.0;
            const qreal text_top = std::max(1.0, metrics.vertical_padding / 2.0);
            const qreal curve_inset = radius > text_top
                ? radius - std::sqrt(std::max(0.0, radius * radius -
                                                (radius - text_top) * (radius - text_top)))
                : 0.0;
            const int required_padding = std::max(
                metrics.horizontal_padding,
                2 * static_cast<int>(std::ceil(curve_inset + 8.0)));
            if (required_padding <= horizontal_padding) break;
            horizontal_padding = std::min(required_padding, metrics.maximum_width - 24);
        }
        const int minimum_content_width = std::max(1, metrics.minimum_width - horizontal_padding);
        const int maximum_content_width = std::max(
            minimum_content_width, metrics.maximum_width - horizontal_padding);
        content_width = std::clamp(natural_width, minimum_content_width, maximum_content_width);
        document_.setTextWidth(content_width);

        const int maximum_content_height = metrics.maximum_height - metrics.vertical_padding - 2;
        QFont fitted_font = document_.defaultFont();
        while (document_.size().height() > maximum_content_height && fitted_font.pointSize() > 7) {
            fitted_font.setPointSize(fitted_font.pointSize() - 1);
            document_.setDefaultFont(fitted_font);
            document_.setTextWidth(content_width);
        }

        const int bubble_height = std::max(
            static_cast<int>(std::ceil(document_.size().height())) + metrics.vertical_padding + 2,
            metrics.minimum_height);
        horizontal_padding_ = horizontal_padding;
        int final_width = std::clamp(content_width + horizontal_padding,
                                     metrics.minimum_width, metrics.maximum_width);
        if ((final_width & 1) != 0 && final_width < metrics.maximum_width) ++final_width;
        setFixedSize(final_width,
                     std::min(bubble_height, metrics.maximum_height));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing |
                               QPainter::TextAntialiasing |
                               QPainter::SmoothPixmapTransform);
        const Metrics metrics = metricsFor(style_);
        const QRectF bubble_rect = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);
        qreal radius = 10.0;
        QColor border;
        QColor background;
        QColor text_color;
        if (style_ == BubbleStyle::Capsule) {
            border = QColor(61, 96, 101);
            background = QColor(27, 45, 51);
            text_color = QColor(235, 250, 246);
            radius = std::min(20.0, height() / 2.0 - 1.0);
        } else if (style_ == BubbleStyle::Ring) {
            border = QColor(235, 236, 240, 78);
            background = QColor(78, 80, 86, 72);
            text_color = QColor(242, 242, 247);
            radius = std::max(1.0, std::min(15.0, height() / 2.0 - 1.0));
        } else {
            border = QColor(214, 220, 225);
            background = QColor(251, 252, 253);
            text_color = QColor(31, 37, 43);
            radius = 10.0;
        }
        QColor stripe(69, 184, 158);
        if (theme_) {
            background = theme_->background;
            text_color = theme_->text;
            border = mixColor(theme_->background, theme_->text, 0.25);
            stripe = theme_->accent;
        }
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(background);
        painter.drawRoundedRect(bubble_rect, radius, radius);

        if (style_ == BubbleStyle::Panel) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(stripe);
            painter.drawRoundedRect(QRectF(1.0, 12.0, 4.0, std::max(12, height() - 24)), 2.0, 2.0);
        }

        painter.save();
        const qreal vertical_offset = std::max(
            metrics.vertical_padding / 2.0,
            (height() - document_.size().height()) / 2.0);
        painter.translate(horizontal_padding_ / 2.0, vertical_offset);
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette.setColor(QPalette::Text, text_color);
        document_.documentLayout()->draw(&painter, context);
        painter.restore();
    }

private:
    struct Metrics {
        int minimum_width;
        int maximum_width;
        int minimum_height;
        int maximum_height;
        int horizontal_padding;
        int vertical_padding;
    };

    QColor secondaryColor(SecondaryTone tone) const {
        if (tone == SecondaryTone::Warning) return QColor(240, 160, 75);
        // The translation reads as a quieter echo of the bubble's text colour.
        const bool light_panel = style_ == BubbleStyle::Panel;
        QColor color = theme_ ? theme_->text
            : (light_panel ? QColor(31, 37, 43) : QColor(242, 242, 247));
        color.setAlpha(tone == SecondaryTone::Pending ? 120 : 165);
        return color;
    }

    static Metrics metricsFor(BubbleStyle style) {
        switch (style) {
        case BubbleStyle::Capsule:
            return {220, 560, 64, 560, 40, 30};
        case BubbleStyle::Ring:
            return {132, 480, 32, 540, 56, 10};
        case BubbleStyle::Panel:
            return {300, 520, 58, 560, 48, 28};
        }
        return {300, 520, 58, 560, 48, 28};
    }

    QTextDocument document_;
    QFont base_font_;
    std::optional<ThemeColors> theme_;
    QString text_;
    QString secondary_;
    SecondaryTone secondary_tone_ = SecondaryTone::Muted;
    BubbleStyle style_ = BubbleStyle::Ring;
    int horizontal_padding_ = 32;
};

class InputSettingsDialog final : public QDialog {
public:
    InputSettingsDialog(const VadSettings& values,
                        const QString& shortcut,
                        TextMode mode,
                        HotkeyTrigger trigger,
                        ResultDestination destination,
                        ResultContent input_content,
                        ResultContent clipboard_content,
                        int paste_delay_ms,
                        bool startup_enabled,
                        const std::vector<HotwordEntry>& hotwords,
                        const TranslationSettings& translation,
                        const SpeechSettings& speech,
                        const AppearanceSettings& appearance,
                        BubbleStyle bubble_style,
                        std::function<void(const AppearanceSettings&)> on_appearance_changed,
                        QWidget* parent = nullptr)
        : QDialog(parent), appearance_(appearance), bubble_style_(bubble_style),
          on_appearance_changed_(std::move(on_appearance_changed)) {
        setWindowTitle(QStringLiteral("语音输入设置"));
        setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        resize(680, 720);
        setMinimumSize(600, 520);
        setStyleSheet(QStringLiteral(
            "QDialog { background: #F7F8FA; }"
            "QLabel { color: #202329; }"
            "QTabWidget::pane { border: 1px solid #DADDE2; border-radius: 6px; background: #F2F4F7; top: -1px; }"
            "QScrollArea, QScrollArea > QWidget > QWidget#page { background: transparent; border: none; }"
            "QFrame#card { background: #FFFFFF; border: 1px solid #E3E6EB; border-radius: 8px; }"
            "QFrame#card QLabel { background: transparent; }"
            "QLabel#cardTitle { color: #202329; font-weight: 600; }"
            "QLabel#cardHint { color: #7A808A; }"
            "QLabel#rowLabel { color: #4A505A; }"
            "QTabBar::tab { color: #676D77; padding: 8px 20px; background: #EEF0F3; border: 1px solid #DADDE2; border-bottom: none; }"
            "QTabBar::tab:selected { color: #202329; background: #F2F4F7; font-weight: 600; }"
            "QComboBox, QKeySequenceEdit, QDoubleSpinBox { min-height: 32px; padding: 0 9px; border: 1px solid #D7DAE0; border-radius: 5px; background: #FFFFFF; color: #30343A; }"
            "QComboBox:focus, QKeySequenceEdit:focus, QDoubleSpinBox:focus { border: 1px solid #356AE6; }"
            "QKeySequenceEdit:disabled, QKeySequenceEdit QLineEdit:disabled { background: #F0F2F5; color: #858B95; selection-background-color: #F0F2F5; selection-color: #858B95; }"
            "QSlider::groove:horizontal { height: 4px; background: #E2E5E9; border-radius: 2px; }"
            "QSlider::sub-page:horizontal { background: #E6A52C; border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 16px; margin: -6px 0; background: white; border: 2px solid #E6A52C; border-radius: 8px; }"
            "QPushButton { min-width: 76px; min-height: 30px; border: 1px solid #D7DAE0; border-radius: 5px; background: #FFFFFF; color: #30343A; }"
            "QPushButton:hover { background: #F0F2F5; }"
            "QPushButton#primary { border: none; background: #202329; color: white; }"
            "QPushButton#primary:hover { background: #343840; }"
            "QPushButton#modeSegment { min-width: 88px; border-radius: 4px; background: #F0F2F5; }"
            "QPushButton#modeSegment:checked { border-color: #356AE6; background: #EAF0FF; color: #244FB7; }"
            "QToolButton { width: 30px; height: 30px; border: 1px solid #D7DAE0; border-radius: 5px; background: #FFFFFF; color: #30343A; }"
            "QToolButton:hover { background: #F0F2F5; }"
            "QTableWidget { border: 1px solid #DADDE2; border-radius: 5px; background: #FFFFFF; gridline-color: #ECEEF1; color: #30343A; }"
            "QTableWidget::item:selected { background: #EAF0FF; color: #202329; }"
            "QHeaderView::section { padding: 7px; border: none; border-bottom: 1px solid #DADDE2; background: #F5F6F8; color: #676D77; }"));

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(24, 20, 24, 20);
        root->setSpacing(16);

        auto* heading = new QLabel(QStringLiteral("语音输入设置"));
        QFont heading_font = heading->font();
        heading_font.setPointSize(13);
        heading_font.setWeight(QFont::DemiBold);
        heading->setFont(heading_font);
        root->addWidget(heading);

        auto* tabs = new QTabWidget;
        tabs->setDocumentMode(false);
        tabs->addTab(scrollable(createInputPage(shortcut, trigger, startup_enabled)), QStringLiteral("输入"));
        tabs->addTab(scrollable(createOutputPage(mode, destination, input_content, clipboard_content, paste_delay_ms)), QStringLiteral("输出"));
        tabs->addTab(scrollable(carded(createAppearancePage())), QStringLiteral("外观"));
        tabs->addTab(scrollable(carded(createTranslationPage(translation))), QStringLiteral("翻译"));
        tabs->addTab(scrollable(createSpeechPage(speech)), QStringLiteral("朗读"));
        tabs->addTab(createHotwordPage(hotwords), QStringLiteral("热词"));
        tabs->addTab(scrollable(carded(createVadPage(values))), QStringLiteral("VAD"));
        root->addWidget(tabs);
        for (QComboBox* combo : {content_, clipboard_content_}) {
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged), this,
                    [this](int) { updateContentHint(); });
        }
        connect(translation_enabled_checkbox_, &QCheckBox::toggled, this,
                [this](bool) { updateContentHint(); updateSpeechControls(); });
        updateContentHint();
        updateSpeechControls();

        auto* buttons = new QHBoxLayout;
        buttons->setSpacing(8);
        auto* reset_button = new QPushButton(QStringLiteral("恢复默认"));
        auto* cancel_button = new QPushButton(QStringLiteral("取消"));
        auto* apply_button = new QPushButton(QStringLiteral("应用"));
        apply_button->setObjectName(QStringLiteral("primary"));
        buttons->addWidget(reset_button);
        buttons->addStretch();
        buttons->addWidget(cancel_button);
        buttons->addWidget(apply_button);
        root->addLayout(buttons);

        connect(reset_button, &QPushButton::clicked, this, [this] {
            endpoint_slider_->setValue(700);
            threshold_slider_->setValue(55);
            minimum_db_slider_->setValue(-60);
            snr_slider_->setValue(3);
            const int default_index = shortcut_preset_->findData(QString::fromLatin1(default_hotkey));
            if (default_index >= 0) shortcut_preset_->setCurrentIndex(default_index);
            clean_mode_button_->setChecked(true);
            destination_->setCurrentIndex(destination_->findData(static_cast<int>(ResultDestination::Insert)));
            content_->setCurrentIndex(content_->findData(static_cast<int>(ResultContent::Original)));
            clipboard_content_->setCurrentIndex(
                clipboard_content_->findData(static_cast<int>(ResultContent::Original)));
            trigger_->setCurrentIndex(trigger_->findData(static_cast<int>(HotkeyTrigger::Hold)));
            paste_delay_slider_->setValue(default_paste_delay_ms);
            translation_enabled_checkbox_->setChecked(false);
            translation_direction_->setCurrentIndex(0);
            translation_separator_->setText(QString::fromLatin1(default_translation_separator));
            speech_enabled_checkbox_->setChecked(false);
            speech_monitor_checkbox_->setChecked(false);
            speech_voice_zh_->setCurrentIndex(speech_voice_zh_->findData(default_speech_voice_zh));
            speech_voice_en_->setCurrentIndex(speech_voice_en_->findData(default_speech_voice_en));
            speech_speed_slider_->setValue(100);
        });
        connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
        connect(apply_button, &QPushButton::clicked, this, [this] {
            if (!hasUsableShortcutKey(this->shortcut())) {
                shortcut_error_->setText(QStringLiteral("请录入一个包含普通按键的组合，例如 Ctrl + Alt + Space。"));
                shortcut_error_->setVisible(true);
                return;
            }
            accept();
        });
    }

    VadSettings values() const {
        return VadSettings{
            .endpoint_ms = endpoint_slider_->value(),
            .threshold_percent = threshold_slider_->value(),
            .minimum_db = minimum_db_slider_->value(),
            .snr_db = snr_slider_->value(),
        };
    }

    QString shortcut() const {
        const QString preset = shortcut_preset_->currentData().toString();
        if (preset != QStringLiteral("custom")) return preset;
        return canonicalShortcut(shortcut_edit_->keySequence().toString(QKeySequence::PortableText));
    }

    TextMode mode() const {
        return mode_group_->checkedId() == 0 ? TextMode::Raw : TextMode::Clean;
    }

    int pasteDelayMs() const {
        return paste_delay_slider_->value();
    }

    HotkeyTrigger trigger() const {
        return static_cast<HotkeyTrigger>(trigger_->currentData().toInt());
    }

    ResultContent inputContent() const {
        return static_cast<ResultContent>(content_->currentData().toInt());
    }

    ResultContent clipboardContent() const {
        return static_cast<ResultContent>(clipboard_content_->currentData().toInt());
    }

    ResultDestination destination() const {
        return static_cast<ResultDestination>(destination_->currentData().toInt());
    }

    bool startupEnabled() const {
        return startup_checkbox_ != nullptr && startup_checkbox_->isChecked();
    }

    TranslationSettings translation() const {
        return TranslationSettings{
            .enabled = translation_enabled_checkbox_->isChecked(),
            .direction = translation_direction_->currentIndex() == 0
                ? TranslationDirection::ZhToEn
                : TranslationDirection::EnToZh,
            .separator = sanitizedSeparator(translation_separator_->text()),
        };
    }

    SpeechSettings speech() const {
        return SpeechSettings{
            .enabled = speech_enabled_checkbox_->isChecked(),
            .device_id = speech_device_->currentData().toString(),
            .monitor = speech_monitor_checkbox_->isChecked(),
            .voice_zh = speech_voice_zh_->currentData().toInt(),
            .voice_en = speech_voice_en_->currentData().toInt(),
            .speed_percent = speech_speed_slider_->value(),
        };
    }

    std::vector<HotwordEntry> hotwords() const {
        std::vector<HotwordEntry> entries;
        entries.reserve(static_cast<std::size_t>(hotword_table_->rowCount()));
        for (int row = 0; row < hotword_table_->rowCount(); ++row) {
            const QTableWidgetItem* phrase_item = hotword_table_->item(row, 1);
            const QString phrase = phrase_item == nullptr ? QString{} : phrase_item->text().trimmed();
            if (phrase.isEmpty()) continue;

            HotwordEntry entry;
            entry.phrase = to_utf8_string(phrase);
            if (const auto* enabled = qobject_cast<QCheckBox*>(hotword_table_->cellWidget(row, 0))) {
                entry.enabled = enabled->isChecked();
            }
            const QTableWidgetItem* aliases_item = hotword_table_->item(row, 2);
            const QString aliases_text = aliases_item == nullptr ? QString{} : aliases_item->text();
            for (const QString& alias : aliases_text.split(QLatin1Char('|'), Qt::SkipEmptyParts)) {
                const QString trimmed = alias.trimmed();
                if (!trimmed.isEmpty()) entry.aliases.push_back(to_utf8_string(trimmed));
            }
            if (const auto* boost = qobject_cast<QDoubleSpinBox*>(hotword_table_->cellWidget(row, 3))) {
                entry.boost = static_cast<float>(boost->value());
            }
            const QTableWidgetItem* hits_item = hotword_table_->item(row, 4);
            if (hits_item != nullptr) entry.hits = hits_item->text().toULongLong();
            entries.push_back(std::move(entry));
        }
        return entries;
    }

private:
    enum class ValueFormat { Milliseconds, Threshold, Dbfs, Db, HalfSeconds, Speed };

    void updateContentHint() {
        if (content_hint_ == nullptr || translation_enabled_checkbox_ == nullptr) return;
        const auto needs_translation = [](const QComboBox* combo) {
            return combo->isEnabled() &&
                combo->currentData().toInt() != static_cast<int>(ResultContent::Original);
        };
        content_hint_->setVisible((needs_translation(content_) || needs_translation(clipboard_content_)) &&
                                  !translation_enabled_checkbox_->isChecked());
    }

    QWidget* createAppearancePage() {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(18, 18, 18, 16);
        layout->setSpacing(10);

        const auto add_heading = [layout](const QString& text) {
            auto* label = new QLabel(text);
            QFont font = label->font();
            font.setWeight(QFont::DemiBold);
            label->setFont(font);
            layout->addWidget(label);
        };

        add_heading(QStringLiteral("配色主题与字体"));
        theme_preset_ = new QComboBox;
        theme_preset_->addItem(QStringLiteral("默认（各浮窗样式原有配色）"), static_cast<int>(ThemePreset::Default));
        theme_preset_->addItem(QStringLiteral("亮色"), static_cast<int>(ThemePreset::Light));
        theme_preset_->addItem(QStringLiteral("暗色"), static_cast<int>(ThemePreset::Dark));
        theme_preset_->addItem(QStringLiteral("高对比度"), static_cast<int>(ThemePreset::HighContrast));
        theme_preset_->addItem(QStringLiteral("自定义"), static_cast<int>(ThemePreset::Custom));
        layout->addWidget(theme_preset_);

        auto* colors_row = new QHBoxLayout;
        colors_row->setSpacing(8);
        const auto add_color_button = [this, colors_row](const QString& label_text, QColor ThemeColors::*member) {
            colors_row->addWidget(new QLabel(label_text));
            auto* button = new QPushButton;
            button->setFixedSize(56, 28);
            colors_row->addWidget(button);
            colors_row->addSpacing(8);
            connect(button, &QPushButton::clicked, this, [this, member, label_text] {
                const ThemeColors colors = effectiveColors();
                const QColor chosen = QColorDialog::getColor(colors.*member, this, label_text);
                if (!chosen.isValid()) return;
                beginCustomEdit();
                appearance_.custom.*member = chosen;
                appearanceChanged();
            });
            return button;
        };
        background_button_ = add_color_button(QStringLiteral("背景"), &ThemeColors::background);
        text_button_ = add_color_button(QStringLiteral("文字"), &ThemeColors::text);
        accent_button_ = add_color_button(QStringLiteral("强调色"), &ThemeColors::accent);
        colors_row->addStretch();
        layout->addLayout(colors_row);

        auto* font_row = new QHBoxLayout;
        font_row->setSpacing(8);
        font_family_ = new QFontComboBox;
        font_size_ = new QSpinBox;
        font_size_->setRange(8, 24);
        font_size_->setSuffix(QStringLiteral(" pt"));
        font_size_->setMinimumHeight(32);
        font_row->addWidget(new QLabel(QStringLiteral("字体")));
        font_row->addWidget(font_family_, 1);
        font_row->addWidget(font_size_);
        layout->addLayout(font_row);

        layout->addSpacing(6);
        add_heading(QStringLiteral("浮窗显示"));
        linger_slider_ = addSlider(layout, QStringLiteral("识别完成后的停留时长"), 0,
                                   linger_maximum_ms / linger_step_ms, 1,
                                   appearance_.linger_ms / linger_step_ms, ValueFormat::HalfSeconds);
        pinned_checkbox_ = new QCheckBox(QStringLiteral("常驻显示（按快捷键唤出后不自动隐藏，点浮窗上的 × 关闭）"));
        pinned_checkbox_->setChecked(appearance_.pinned);
        layout->addWidget(pinned_checkbox_);
        layout->addStretch();

        auto* note = new QLabel(QStringLiteral("外观修改即时生效，不需要点“应用”，点“取消”也不会撤回。"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: #69707A;"));
        layout->addWidget(note);

        refreshAppearanceControls();

        connect(theme_preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
            if (updating_appearance_) return;
            appearance_.preset = static_cast<ThemePreset>(theme_preset_->currentData().toInt());
            appearanceChanged();
        });
        connect(font_family_, &QFontComboBox::currentFontChanged, this, [this](const QFont& font) {
            if (updating_appearance_) return;
            beginCustomEdit();
            appearance_.custom_font_family = font.family();
            appearanceChanged();
        });
        connect(font_size_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int size) {
            if (updating_appearance_) return;
            beginCustomEdit();
            appearance_.custom_font_size = size;
            appearanceChanged();
        });
        connect(linger_slider_, &QSlider::valueChanged, this, [this](int steps) {
            appearance_.linger_ms = steps * linger_step_ms;
            appearanceChanged();
        });
        connect(pinned_checkbox_, &QCheckBox::toggled, this, [this](bool checked) {
            appearance_.pinned = checked;
            appearanceChanged();
        });
        return page;
    }

    ThemeColors effectiveColors() const {
        return themeColors(appearance_).value_or(styleDefaultColors(bubble_style_));
    }

    // Editing a preset forks it into Custom, starting from what the user sees.
    void beginCustomEdit() {
        if (appearance_.preset == ThemePreset::Custom) return;
        const QFont current = themeFont(appearance_);
        appearance_.custom = effectiveColors();
        appearance_.custom_font_family = current.family();
        appearance_.custom_font_size = current.pointSize();
        appearance_.preset = ThemePreset::Custom;
    }

    void appearanceChanged() {
        refreshAppearanceControls();
        if (on_appearance_changed_) on_appearance_changed_(appearance_);
    }

    void refreshAppearanceControls() {
        updating_appearance_ = true;
        theme_preset_->setCurrentIndex(theme_preset_->findData(static_cast<int>(appearance_.preset)));
        const ThemeColors colors = effectiveColors();
        const std::array<std::pair<QPushButton*, QColor>, 3> swatches = {{
            {background_button_, colors.background},
            {text_button_, colors.text},
            {accent_button_, colors.accent},
        }};
        for (const auto& [button, color] : swatches) {
            button->setStyleSheet(QStringLiteral(
                "QPushButton { min-width: 0; background: %1; border: 1px solid #C9CDD3; border-radius: 5px; }")
                                      .arg(color.name()));
            button->setToolTip(color.name().toUpper());
        }
        const QFont font = themeFont(appearance_);
        font_family_->setCurrentFont(font);
        font_size_->setValue(font.pointSize());
        updating_appearance_ = false;
    }

    static QWidget* scrollable(QWidget* page) {
        page->setObjectName(QStringLiteral("page"));
        auto* area = new QScrollArea;
        area->setWidget(page);
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        return area;
    }

    static QVBoxLayout* addCard(QVBoxLayout* page_layout, const QString& title, const QString& hint = {}) {
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 16);
        layout->setSpacing(10);
        auto* title_label = new QLabel(title);
        title_label->setObjectName(QStringLiteral("cardTitle"));
        layout->addWidget(title_label);
        if (!hint.isEmpty()) {
            auto* hint_label = new QLabel(hint);
            hint_label->setObjectName(QStringLiteral("cardHint"));
            hint_label->setWordWrap(true);
            layout->addWidget(hint_label);
        }
        page_layout->addWidget(card);
        return layout;
    }

    static void addRow(QVBoxLayout* card, const QString& label_text, QWidget* field) {
        auto* row = new QHBoxLayout;
        row->setSpacing(12);
        auto* label = new QLabel(label_text);
        label->setObjectName(QStringLiteral("rowLabel"));
        label->setFixedWidth(76);
        row->addWidget(label);
        row->addWidget(field, 1);
        card->addLayout(row);
    }

    static QWidget* carded(QWidget* inner) {
        auto* page = new QWidget;
        auto* layout = pageLayout(page);
        auto* card = new QFrame;
        card->setObjectName(QStringLiteral("card"));
        auto* card_layout = new QVBoxLayout(card);
        card_layout->setContentsMargins(0, 0, 0, 0);
        card_layout->addWidget(inner);
        layout->addWidget(card);
        return page;
    }

    static QVBoxLayout* pageLayout(QWidget* page) {
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(12);
        return layout;
    }

    QWidget* createInputPage(const QString& current_shortcut, HotkeyTrigger trigger, bool startup_enabled) {
        auto* page = new QWidget;
        auto* layout = pageLayout(page);

        QVBoxLayout* hotkey_card = addCard(layout, QStringLiteral("快捷键"));
        shortcut_preset_ = new QComboBox;
        shortcut_preset_->addItem(QStringLiteral("Ctrl + Alt + Space（推荐）"),
                                  QString::fromLatin1(default_hotkey));
        shortcut_preset_->addItem(QStringLiteral("Ctrl + Win"),
                                  QString::fromLatin1(control_windows_hotkey));
        shortcut_preset_->addItem(QStringLiteral("Ctrl + Shift + Space"),
                                  QStringLiteral("Ctrl+Shift+Space"));
        shortcut_preset_->addItem(QStringLiteral("F8"), QStringLiteral("F8"));
        shortcut_preset_->addItem(QStringLiteral("自定义"), QStringLiteral("custom"));
        addRow(hotkey_card, QStringLiteral("组合键"), shortcut_preset_);

        shortcut_edit_ = new QKeySequenceEdit;
        shortcut_edit_->setMaximumSequenceLength(1);
        shortcut_edit_->setToolTip(QStringLiteral("点击后按下一个组合键"));
        shortcut_edit_row_ = new QWidget;
        auto* edit_row = new QVBoxLayout(shortcut_edit_row_);
        edit_row->setContentsMargins(0, 0, 0, 0);
        auto* edit_wrapper = new QVBoxLayout;
        addRow(edit_wrapper, QStringLiteral("录入"), shortcut_edit_);
        edit_row->addLayout(edit_wrapper);
        hotkey_card->addWidget(shortcut_edit_row_);

        shortcut_error_ = new QLabel;
        shortcut_error_->setWordWrap(true);
        shortcut_error_->setStyleSheet(QStringLiteral("color: #C43B3B;"));
        shortcut_error_->setVisible(false);
        hotkey_card->addWidget(shortcut_error_);

        QVBoxLayout* trigger_card = addCard(layout, QStringLiteral("触发方式"),
            QStringLiteral("切换方式下，文字输入到第二次按快捷键时光标所在的位置，最长录音 5 分钟。"));
        trigger_ = new QComboBox;
        trigger_->addItem(QStringLiteral("按住说话：按住开始，松开结束"), static_cast<int>(HotkeyTrigger::Hold));
        trigger_->addItem(QStringLiteral("按一下切换：按一下开始，再按一下结束"), static_cast<int>(HotkeyTrigger::Toggle));
        trigger_->setCurrentIndex(trigger_->findData(static_cast<int>(trigger)));
        trigger_card->addWidget(trigger_);

        QVBoxLayout* startup_card = addCard(layout, QStringLiteral("启动"));
        startup_checkbox_ = new QCheckBox(QStringLiteral("开机自动启动"));
        startup_checkbox_->setChecked(startup_enabled);
        startup_checkbox_->setToolTip(QStringLiteral(
            "使用当前用户的 Windows 启动项注册，不需要管理员权限。"));
        startup_card->addWidget(startup_checkbox_);
        layout->addStretch();

        const QString canonical = canonicalShortcut(current_shortcut);
        const int preset_index = shortcut_preset_->findData(canonical);
        if (preset_index >= 0) {
            shortcut_preset_->setCurrentIndex(preset_index);
        } else {
            shortcut_preset_->setCurrentIndex(shortcut_preset_->findData(QStringLiteral("custom")));
            shortcut_edit_->setKeySequence(QKeySequence::fromString(canonical, QKeySequence::PortableText));
        }
        updateShortcutEditor();

        const auto clear_error = [this] {
            shortcut_error_->clear();
            shortcut_error_->setVisible(false);
        };
        connect(shortcut_preset_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, clear_error](int) {
                    clear_error();
                    updateShortcutEditor();
                });
        connect(shortcut_edit_, &QKeySequenceEdit::keySequenceChanged, this,
                [clear_error](const QKeySequence&) { clear_error(); });
        return page;
    }

    QWidget* createOutputPage(TextMode mode,
                              ResultDestination destination,
                              ResultContent input_content,
                              ResultContent clipboard_content,
                              int paste_delay_ms) {
        auto* page = new QWidget;
        auto* layout = pageLayout(page);

        QVBoxLayout* text_card = addCard(layout, QStringLiteral("文字整理"),
            QStringLiteral("精简会去掉口头语和重复，原文保留识别出的每个字。"));
        auto* mode_row = new QHBoxLayout;
        mode_row->setSpacing(6);
        mode_group_ = new QButtonGroup(this);
        raw_mode_button_ = new QPushButton(QStringLiteral("原文"));
        clean_mode_button_ = new QPushButton(QStringLiteral("精简"));
        for (QPushButton* button : {raw_mode_button_, clean_mode_button_}) {
            button->setObjectName(QStringLiteral("modeSegment"));
            button->setCheckable(true);
            mode_row->addWidget(button);
        }
        mode_group_->addButton(raw_mode_button_, 0);
        mode_group_->addButton(clean_mode_button_, 1);
        (mode == TextMode::Raw ? raw_mode_button_ : clean_mode_button_)->setChecked(true);
        mode_row->addStretch();
        text_card->addLayout(mode_row);

        QVBoxLayout* result_card = addCard(layout, QStringLiteral("识别结果"));
        destination_ = new QComboBox;
        destination_->addItem(QStringLiteral("输入到光标位置（同时复制到剪贴板）"),
                              static_cast<int>(ResultDestination::Insert));
        destination_->addItem(QStringLiteral("只复制到剪贴板"),
                              static_cast<int>(ResultDestination::CopyOnly));
        destination_->setCurrentIndex(destination_->findData(static_cast<int>(destination)));
        destination_->setToolTip(QStringLiteral("只复制时不会自动输入，需要自己按 Ctrl+V 粘贴。"));
        addRow(result_card, QStringLiteral("去向"), destination_);
        const auto content_combo = [](ResultContent current) {
            auto* combo = new QComboBox;
            combo->addItem(QStringLiteral("原文"), static_cast<int>(ResultContent::Original));
            combo->addItem(QStringLiteral("译文"), static_cast<int>(ResultContent::Translation));
            combo->addItem(QStringLiteral("原文 + 译文"), static_cast<int>(ResultContent::Both));
            combo->setCurrentIndex(combo->findData(static_cast<int>(current)));
            return combo;
        };
        content_ = content_combo(input_content);
        content_->setToolTip(QStringLiteral("识别完成后浮窗上显示的文字，只给自己看。"));
        addRow(result_card, QStringLiteral("浮窗内容"), content_);
        clipboard_content_ = content_combo(clipboard_content);
        clipboard_content_->setToolTip(QStringLiteral("放进剪贴板的文字；输入到光标位置的也是这一段。"));
        addRow(result_card, QStringLiteral("剪贴板内容"), clipboard_content_);
        content_hint_ = new QLabel(QStringLiteral("需要在「翻译」页开启翻译模式，否则会输出原文。"));
        content_hint_->setWordWrap(true);
        content_hint_->setStyleSheet(QStringLiteral("color: #C77A14;"));
        result_card->addWidget(content_hint_);

        QVBoxLayout* paste_card = addCard(layout, QStringLiteral("粘贴兜底"),
            QStringLiteral("目标程序不支持直接输入时，等待这段时间后按 Ctrl+V 粘贴。只在「输入到光标位置」时生效。"));
        auto* paste_layout = new QVBoxLayout;
        paste_card->addLayout(paste_layout);
        paste_delay_slider_ = addSlider(paste_layout, QStringLiteral("粘贴延迟"),
                                        0, maximum_paste_delay_ms, 50, paste_delay_ms,
                                        ValueFormat::Milliseconds);
        const auto update_paste_delay = [this] {
            const bool insert =
                destination_->currentData().toInt() == static_cast<int>(ResultDestination::Insert);
            paste_delay_slider_->setEnabled(insert);
        };
        connect(destination_, qOverload<int>(&QComboBox::currentIndexChanged), this,
                [update_paste_delay](int) { update_paste_delay(); });
        update_paste_delay();
        layout->addStretch();
        return page;
    }

    QWidget* createTranslationPage(const TranslationSettings& translation) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(18, 18, 18, 16);
        layout->setSpacing(12);

        const auto add_heading = [layout](const QString& text) {
            auto* label = new QLabel(text);
            QFont font = label->font();
            font.setWeight(QFont::DemiBold);
            label->setFont(font);
            layout->addWidget(label);
        };

        translation_enabled_checkbox_ = new QCheckBox(QStringLiteral("开启翻译模式"));
        translation_enabled_checkbox_->setChecked(translation.enabled);
        translation_enabled_checkbox_->setToolTip(QStringLiteral(
            "开启后，原文和译文会一起写入光标位置。"));
        layout->addWidget(translation_enabled_checkbox_);

        add_heading(QStringLiteral("翻译方向"));
        translation_direction_ = new QComboBox;
        translation_direction_->addItem(QStringLiteral("中文 → 英文"));
        translation_direction_->addItem(QStringLiteral("英文 → 中文"));
        translation_direction_->setCurrentIndex(
            translation.direction == TranslationDirection::ZhToEn ? 0 : 1);
        layout->addWidget(translation_direction_);

        add_heading(QStringLiteral("原文与译文之间的分隔符"));
        translation_separator_ = new QLineEdit(translation.separator);
        translation_separator_->setMaxLength(8);
        translation_separator_->setStyleSheet(QStringLiteral(
            "QLineEdit { min-height: 32px; padding: 0 9px; border: 1px solid #D7DAE0; border-radius: 5px; background: #FFFFFF; color: #30343A; }"
            "QLineEdit:focus { border: 1px solid #356AE6; }"));
        layout->addWidget(translation_separator_);

        auto* example = new QLabel;
        example->setWordWrap(true);
        example->setStyleSheet(QStringLiteral("color: #69707A;"));
        layout->addWidget(example);
        const auto update_example = [this, example] {
            const bool zh_to_en = translation_direction_->currentIndex() == 0;
            const QString separator = sanitizedSeparator(translation_separator_->text());
            example->setText(QStringLiteral("示例：%1")
                                 .arg(zh_to_en
                                     ? QStringLiteral("今天开会。") + separator +
                                         QStringLiteral("We have a meeting today.")
                                     : QStringLiteral("See you tomorrow.") + separator +
                                         QStringLiteral("明天见。")));
        };
        connect(translation_direction_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, update_example);
        connect(translation_separator_, &QLineEdit::textChanged, this, update_example);
        update_example();

        auto* note = new QLabel(QStringLiteral(
            "翻译失败、超时或说的语言与方向不符时，只输入原文。"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: #69707A;"));
        layout->addWidget(note);
        layout->addStretch();
        return page;
    }

    // 朗读 only works on 译文, so it follows the translation switch on the 翻译 page.
    void updateSpeechControls() {
        if (speech_enabled_checkbox_ == nullptr) return;
        const bool translation = translation_enabled_checkbox_->isChecked();
        speech_enabled_checkbox_->setEnabled(translation);
        speech_requires_translation_->setVisible(!translation);
    }

    QWidget* createSpeechPage(const SpeechSettings& speech) {
        auto* page = new QWidget;
        auto* layout = pageLayout(page);

        QVBoxLayout* switch_card = addCard(layout, QStringLiteral("朗读译文"),
            QStringLiteral("松开快捷键后，把这一段的译文念到朗读设备，通话对方就能听到。翻译失败时不朗读。"));
        speech_enabled_checkbox_ = new QCheckBox(QStringLiteral("开启朗读"));
        speech_enabled_checkbox_->setChecked(speech.enabled);
        switch_card->addWidget(speech_enabled_checkbox_);
        speech_requires_translation_ = new QLabel(QStringLiteral("需要先在「翻译」页开启翻译模式。"));
        speech_requires_translation_->setWordWrap(true);
        speech_requires_translation_->setStyleSheet(QStringLiteral("color: #C77A14;"));
        switch_card->addWidget(speech_requires_translation_);

        QVBoxLayout* device_card = addCard(layout, QStringLiteral("朗读设备"),
            QStringLiteral("通常选虚拟声卡（如 CABLE Input），再在通话软件里把麦克风选成它对应的输入（如 CABLE Output）。"));
        speech_device_ = new QComboBox;
        speech_device_->addItem(QStringLiteral("（未选择）"), QString{});
        for (const AudioOutputDevice& device : list_audio_output_devices()) {
            speech_device_->addItem(QString::fromStdWString(device.name), QString::fromStdWString(device.id));
        }
        if (!speech.device_id.isEmpty() && speech_device_->findData(speech.device_id) < 0) {
            speech_device_->addItem(QStringLiteral("之前选择的设备（当前不可用）"), speech.device_id);
        }
        speech_device_->setCurrentIndex(std::max(0, speech_device_->findData(speech.device_id)));
        addRow(device_card, QStringLiteral("设备"), speech_device_);
        speech_monitor_checkbox_ = new QCheckBox(QStringLiteral("监听：同时在自己的默认播放设备上播放"));
        speech_monitor_checkbox_->setChecked(speech.monitor);
        speech_monitor_checkbox_->setToolTip(QStringLiteral("录音期间自动静音，避免被重新录进去。"));
        device_card->addWidget(speech_monitor_checkbox_);

        QVBoxLayout* voice_card = addCard(layout, QStringLiteral("声音"),
            QStringLiteral("中文 → 英文时用英文声音，英文 → 中文时用中文声音。"));
        speech_voice_en_ = new QComboBox;
        speech_voice_en_->addItem(QStringLiteral("美式女声 Maple"), 0);
        speech_voice_en_->addItem(QStringLiteral("美式女声 Sol"), 1);
        speech_voice_en_->addItem(QStringLiteral("英式女声 Vale"), 2);
        speech_voice_en_->setCurrentIndex(std::max(0, speech_voice_en_->findData(speech.voice_en)));
        addRow(voice_card, QStringLiteral("英文声音"), speech_voice_en_);
        speech_voice_zh_ = new QComboBox;
        for (const auto& [id, name] : kokoroChineseVoices()) speech_voice_zh_->addItem(name, id);
        speech_voice_zh_->setCurrentIndex(std::max(0, speech_voice_zh_->findData(speech.voice_zh)));
        addRow(voice_card, QStringLiteral("中文声音"), speech_voice_zh_);
        auto* speed_layout = new QVBoxLayout;
        voice_card->addLayout(speed_layout);
        speech_speed_slider_ = addSlider(speed_layout, QStringLiteral("语速"), 50, 200, 10,
                                         speech.speed_percent, ValueFormat::Speed);
        layout->addStretch();
        return page;
    }

    QWidget* createHotwordPage(const std::vector<HotwordEntry>& entries) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(8);

        hotword_table_ = new QTableWidget;
        hotword_table_->setColumnCount(5);
        hotword_table_->setHorizontalHeaderLabels({
            QStringLiteral("启用"),
            QStringLiteral("标准词"),
            QStringLiteral("别名（用 | 分隔）"),
            QStringLiteral("增强"),
            QStringLiteral("命中"),
        });
        hotword_table_->verticalHeader()->setVisible(false);
        hotword_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        hotword_table_->setSelectionMode(QAbstractItemView::SingleSelection);
        hotword_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        hotword_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        hotword_table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        hotword_table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        hotword_table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        for (const HotwordEntry& entry : entries) addHotwordRow(entry);
        layout->addWidget(hotword_table_);

        auto* controls = new QHBoxLayout;
        controls->setSpacing(6);
        auto* add_button = new QToolButton;
        add_button->setText(QStringLiteral("+"));
        add_button->setToolTip(QStringLiteral("添加热词"));
        auto* remove_button = new QToolButton;
        remove_button->setIcon(style()->standardIcon(QStyle::SP_DialogDiscardButton));
        remove_button->setToolTip(QStringLiteral("删除所选热词"));
        controls->addWidget(add_button);
        controls->addWidget(remove_button);
        controls->addStretch();
        layout->addLayout(controls);

        connect(add_button, &QToolButton::clicked, this, [this] {
            addHotwordRow(HotwordEntry{});
            const int row = hotword_table_->rowCount() - 1;
            hotword_table_->setCurrentCell(row, 1);
            hotword_table_->editItem(hotword_table_->item(row, 1));
        });
        connect(remove_button, &QToolButton::clicked, this, [this] {
            const int row = hotword_table_->currentRow();
            if (row >= 0) hotword_table_->removeRow(row);
        });
        return page;
    }

    void addHotwordRow(const HotwordEntry& entry) {
        const int row = hotword_table_->rowCount();
        hotword_table_->insertRow(row);

        auto* enabled = new QCheckBox;
        enabled->setChecked(entry.enabled);
        enabled->setToolTip(QStringLiteral("启用热词"));
        hotword_table_->setCellWidget(row, 0, enabled);

        auto* phrase = new QTableWidgetItem(to_qstring(entry.phrase));
        hotword_table_->setItem(row, 1, phrase);

        QStringList aliases;
        for (const std::string& alias : entry.aliases) aliases.push_back(to_qstring(alias));
        hotword_table_->setItem(row, 2, new QTableWidgetItem(aliases.join(QLatin1Char('|'))));

        auto* boost = new QDoubleSpinBox;
        boost->setRange(0.0, 12.0);
        boost->setDecimals(1);
        boost->setSingleStep(0.5);
        boost->setValue(entry.boost);
        boost->setToolTip(QStringLiteral("CTC 热词增强强度"));
        hotword_table_->setCellWidget(row, 3, boost);

        auto* hits = new QTableWidgetItem(QString::number(entry.hits));
        hits->setFlags(hits->flags() & ~Qt::ItemIsEditable);
        hits->setTextAlignment(Qt::AlignCenter);
        hotword_table_->setItem(row, 4, hits);
    }

    QWidget* createVadPage(const VadSettings& values) {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(18, 18, 18, 16);
        layout->setSpacing(15);
        endpoint_slider_ = addSlider(layout, QStringLiteral("句尾静音"), 300, 2000, 100,
                                     values.endpoint_ms, ValueFormat::Milliseconds);
        threshold_slider_ = addSlider(layout, QStringLiteral("模型阈值"), 5, 95, 5,
                                      values.threshold_percent, ValueFormat::Threshold);
        minimum_db_slider_ = addSlider(layout, QStringLiteral("最低响度"), -80, -20, 1,
                                       values.minimum_db, ValueFormat::Dbfs);
        snr_slider_ = addSlider(layout, QStringLiteral("底噪余量"), 0, 20, 1,
                                values.snr_db, ValueFormat::Db);
        layout->addStretch();
        return page;
    }

    void updateShortcutEditor() {
        const QString value = shortcut_preset_->currentData().toString();
        const bool custom = value == QStringLiteral("custom");
        shortcut_edit_->setEnabled(custom);
        shortcut_edit_row_->setVisible(custom);
        if (!custom) {
            shortcut_edit_->setKeySequence(
                QKeySequence::fromString(value, QKeySequence::PortableText));
        }
    }

    QSlider* addSlider(QVBoxLayout* root,
                       const QString& label_text,
                       int minimum,
                       int maximum,
                       int step,
                       int value,
                       ValueFormat format) {
        auto* container = new QWidget;
        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(5);
        auto* labels = new QHBoxLayout;
        auto* label = new QLabel(label_text);
        auto* value_label = new QLabel;
        value_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_label->setStyleSheet(QStringLiteral("color: #69707A;"));
        labels->addWidget(label);
        labels->addStretch();
        labels->addWidget(value_label);
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(minimum, maximum);
        slider->setSingleStep(step);
        slider->setPageStep(step);
        layout->addLayout(labels);
        layout->addWidget(slider);
        root->addWidget(container);

        const auto update_value = [value_label, format](int current) {
            switch (format) {
            case ValueFormat::Milliseconds:
                value_label->setText(QString::number(current) + QStringLiteral(" ms"));
                break;
            case ValueFormat::Threshold:
                value_label->setText(QString::number(current / 100.0, 'f', 2));
                break;
            case ValueFormat::Dbfs:
                value_label->setText(QString::number(current) + QStringLiteral(" dBFS"));
                break;
            case ValueFormat::Db:
                value_label->setText(QString::number(current) + QStringLiteral(" dB"));
                break;
            case ValueFormat::HalfSeconds:
                value_label->setText(QString::number(current * linger_step_ms / 1000.0, 'f', 1) +
                                     QStringLiteral(" 秒"));
                break;
            case ValueFormat::Speed:
                value_label->setText(QStringLiteral("%1 倍").arg(current / 100.0, 0, 'f', 1));
                break;
            }
        };
        connect(slider, &QSlider::valueChanged, this, update_value);
        slider->setValue(value);
        update_value(slider->value());
        return slider;
    }

    QSlider* endpoint_slider_ = nullptr;
    QSlider* threshold_slider_ = nullptr;
    QSlider* minimum_db_slider_ = nullptr;
    QSlider* snr_slider_ = nullptr;
    QComboBox* shortcut_preset_ = nullptr;
    QKeySequenceEdit* shortcut_edit_ = nullptr;
    QWidget* shortcut_edit_row_ = nullptr;
    QLabel* shortcut_error_ = nullptr;
    QCheckBox* startup_checkbox_ = nullptr;
    QButtonGroup* mode_group_ = nullptr;
    QPushButton* raw_mode_button_ = nullptr;
    QPushButton* clean_mode_button_ = nullptr;
    QComboBox* destination_ = nullptr;
    QComboBox* content_ = nullptr;
    QComboBox* clipboard_content_ = nullptr;
    QLabel* content_hint_ = nullptr;
    QComboBox* trigger_ = nullptr;
    QSlider* paste_delay_slider_ = nullptr;
    QTableWidget* hotword_table_ = nullptr;
    QCheckBox* translation_enabled_checkbox_ = nullptr;
    QComboBox* translation_direction_ = nullptr;
    QLineEdit* translation_separator_ = nullptr;
    QCheckBox* speech_enabled_checkbox_ = nullptr;
    QLabel* speech_requires_translation_ = nullptr;
    QComboBox* speech_device_ = nullptr;
    QCheckBox* speech_monitor_checkbox_ = nullptr;
    QComboBox* speech_voice_zh_ = nullptr;
    QComboBox* speech_voice_en_ = nullptr;
    QSlider* speech_speed_slider_ = nullptr;
    AppearanceSettings appearance_;
    BubbleStyle bubble_style_;
    std::function<void(const AppearanceSettings&)> on_appearance_changed_;
    bool updating_appearance_ = false;
    QComboBox* theme_preset_ = nullptr;
    QPushButton* background_button_ = nullptr;
    QPushButton* text_button_ = nullptr;
    QPushButton* accent_button_ = nullptr;
    QFontComboBox* font_family_ = nullptr;
    QSpinBox* font_size_ = nullptr;
    QSlider* linger_slider_ = nullptr;
    QCheckBox* pinned_checkbox_ = nullptr;
};

class VoiceInputWindow final : public QWidget {
public:
    explicit VoiceInputWindow(BubbleStyle bubble_style = BubbleStyle::Ring,
                              bool preview_mode = false)
        : settings_(QStringLiteral("SenseVoice"), QStringLiteral("LocalDictation")),
          bubble_style_(bubble_style), preview_mode_(preview_mode) {
        setWindowTitle(QStringLiteral("SenseVoice 语音输入"));
        setWindowIcon(sensevoiceIcon());
        // The live overlay is a tool window: it stays above the target app but
        // never creates a taskbar button. Preview mode remains a normal window
        // so screenshots and manual layout inspection can still find it.
        Qt::WindowFlags window_flags =
            Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
            Qt::WindowDoesNotAcceptFocus;
        if (!preview_mode_) window_flags |= Qt::Tool;
        setWindowFlags(window_flags);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        if (preview_mode_) {
            setWindowFlag(Qt::WindowDoesNotAcceptFocus, false);
            setAttribute(Qt::WA_ShowWithoutActivating, false);
            setFocusPolicy(Qt::StrongFocus);
        }
        resize(bubble_minimum_width + window_margin * 2,
               bubble_minimum_height + 48 + control_spacing + window_margin * 2);
        setFocusPolicy(Qt::NoFocus);
        loadSettings();
        buildUi();
        if (!preview_mode_) buildTrayMenu();
        centerNearBottom();

        translation_deadline_.setSingleShot(true);
        connect(&translation_deadline_, &QTimer::timeout, this, [this] { completeTranslation(true); });
        if (!preview_mode_) createTranslationWorker();
        if (!preview_mode_) createSpeechQueue();
        updateTranslationUi();

        meter_timer_.setInterval(50);
        connect(&meter_timer_, &QTimer::timeout, this, [this] { updateMeter(); });
        meter_timer_.start();
        status_reset_timer_.setSingleShot(true);
        connect(&status_reset_timer_, &QTimer::timeout, this, [this] {
            if (state_ == State::Ready) {
                if (committed_text_.isEmpty()) setBubbleStatus(
                    idleHint());
                else refreshTranscript();
            }
        });
#ifdef _WIN32
        if (!preview_mode_) {
            hotkey_hold_timer_.setSingleShot(true);
            connect(&hotkey_hold_timer_, &QTimer::timeout, this, [this] { beginHotkeySession(); });
            toggle_limit_timer_.setSingleShot(true);
            connect(&toggle_limit_timer_, &QTimer::timeout, this, [this] {
                if (toggle_active_ && state_ == State::Listening) stopSession(true);
            });
            hotkey_release_timer_.setInterval(20);
            connect(&hotkey_release_timer_, &QTimer::timeout, this, [this] { pollHotkeyRelease(); });
            rebuildHotkeyBinding();
            registerHotkeys();
            installKeyboardHook();
        }
#endif
        if (preview_mode_) {
            state_ = State::Ready;
            recording_control_->setEnabled(true);
            setBubbleStatus(QStringLiteral("这一句用于比较浮窗方案的文字布局。说长一点时，气泡会自动扩展，不滚动，也不会裁切内容。"));
        } else {
            beginLoadModels();
        }
    }

    ~VoiceInputWindow() override {
        shutting_down_.store(true, std::memory_order_release);
        translation_worker_.reset();
        translation_deadline_.stop();
        meter_timer_.stop();
        status_reset_timer_.stop();
#ifdef _WIN32
        hotkey_hold_timer_.stop();
        hotkey_release_timer_.stop();
        uninstallKeyboardHook();
        unregisterHotkeys();
#endif

        if (stopper_.joinable()) {
            stopper_.join();
        } else if (microphone_ != nullptr && recognizer_ != nullptr) {
            microphone_->stop();
            recognizer_->cancel();
        }
        playback_mute_.restore();
        microphone_.reset();
        recognizer_.reset();
        if (loader_.joinable()) loader_.join();
    }

    void hideUntilInput() {
        hide();
    }

    void setPreviewContent(const QString& text) {
        if (!preview_mode_) return;
        state_ = State::Ready;
        recording_control_->setEnabled(true);
        setBubbleStatus(text);
    }

    void setGeometryLogPath(const QString& path) {
        if (path.trimmed().isEmpty()) return;
        geometry_log_.setFileName(path);
        if (!geometry_log_.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            geometry_log_.setFileName({});
        }
    }

    void setPreviewSignal() {
        if (!preview_mode_) return;
        meter_timer_.stop();
        recording_control_->setPreviewSignal();
    }

    void startPreviewGeometryTest() {
        if (!preview_mode_) return;
        setPreviewGeometryStep(0);
    }

    // Renders the bilingual layout without loading any model.
    void setPreviewTranslation(const QString& original, const QString& translation) {
        if (!preview_mode_) return;
        translation_settings_.enabled = true;
        updateTranslationUi();
        transcript_->setBubbleContent(original, translation, TranscriptBubble::SecondaryTone::Muted);
        updateWindowGeometry();
    }

protected:
    void showEvent(QShowEvent* event) override {
        QWidget::showEvent(event);
#ifdef _WIN32
        applyGlassBackdrop();
#endif
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
#ifdef _WIN32
        updateGlassMask();
#endif
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            drag_offset_ = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && (event->buttons() & Qt::LeftButton)) {
            move(event->globalPosition().toPoint() - drag_offset_);
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragging_) {
            geometry_anchor_center_ = x() + width() / 2;
            geometry_anchor_bottom_ = y() + height();
            geometry_anchor_valid_ = true;
        }
        dragging_ = false;
        QWidget::mouseReleaseEvent(event);
    }

    void contextMenuEvent(QContextMenuEvent* event) override {
        QMenu menu(this);
        QAction* settings_action = menu.addAction(QStringLiteral("设置..."));
        QAction* cancel_action = menu.addAction(QStringLiteral("取消本次输入"));
        cancel_action->setEnabled(state_ == State::Listening);
        QAction* copy_action = menu.addAction(QStringLiteral("复制当前文字"));
        copy_action->setEnabled(!currentTranscript().trimmed().isEmpty());
        QMenu* recent_menu = menu.addMenu(QStringLiteral("最近输入"));
        populateRecentMenu(recent_menu);
        menu.addSeparator();
        QAction* exit_action = menu.addAction(QStringLiteral("退出"));
        QAction* selected = menu.exec(event->globalPos());
        if (selected == settings_action) openSettings();
        else if (selected == cancel_action) stopSession(false);
        else if (selected == copy_action) {
            QApplication::clipboard()->setText(currentTranscript().trimmed());
            setTransientStatus(QStringLiteral("已复制当前文字"));
        }
        else if (selected == exit_action) close();
    }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Escape && state_ == State::Listening) {
            stopSession(false);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_Escape && speech_queue_ != nullptr) {
            speech_queue_->clear();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void closeEvent(QCloseEvent* event) override {
        event->accept();
        if (preview_mode_) QCoreApplication::quit();
    }

#ifdef _WIN32
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override {
        (void)event_type;
        auto* native_message = static_cast<MSG*>(message);
        if (native_message->message == WM_HOTKEY &&
            (native_message->wParam == hotkey_primary_id ||
             native_message->wParam == hotkey_left_id ||
             native_message->wParam == hotkey_right_id)) {
            if (result != nullptr) *result = 0;
            handleHotkeyPressed();
            return true;
        }
        if (native_message->message == hotkey_state_message) {
            (void)native_message->wParam;
            (void)native_message->lParam;
            if (hotkeyKeysDown()) handleHotkeyPressed();
            else if (hotkey_pending_ || hotkey_recording_ || toggle_latch_) pollHotkeyRelease();
            if (result != nullptr) *result = 0;
            return true;
        }
        return QWidget::nativeEvent(event_type, message, result);
    }
#endif

private:
    enum class State { Loading, Ready, Listening, Stopping, Error };

    void setPreviewGeometryStep(int step) {
        static const std::array<QString, 6> samples = {
            QStringLiteral("短句"),
            QStringLiteral("文字逐步变长"),
            QStringLiteral("文字逐步变长时窗口视觉中心应该保持不动"),
            QStringLiteral("文字逐步变长时窗口视觉中心应该保持不动，底边也应该保持不动。"),
            QStringLiteral("这是一个更长的预览句子，用来验证窗口宽高动画不会导致水平抖动。"),
            QStringLiteral("这是一个更长的预览句子，用来验证窗口宽高动画不会导致水平抖动，内容变成多行后仍然保持稳定。"),
        };
        if (step >= static_cast<int>(samples.size())) return;
        setPreviewContent(samples[static_cast<std::size_t>(step)]);
        if (step + 1 < static_cast<int>(samples.size())) {
            QTimer::singleShot(180, this, [this, step] {
                setPreviewGeometryStep(step + 1);
            });
        }
    }

    void buildUi() {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(window_margin, window_margin, window_margin, window_margin);
        root->setSpacing(control_spacing);
        root->setAlignment(Qt::AlignHCenter);

        transcript_ = new TranscriptBubble(bubble_style_);
        setBubbleStatus(QStringLiteral("正在加载..."));

        recording_control_ = new RecordingControl(bubble_style_);
        recording_control_->setEnabled(false);
        recording_control_->setMode(text_mode_);
        root->addWidget(transcript_, 0, Qt::AlignHCenter);
        root->addWidget(recording_control_, 0, Qt::AlignHCenter);

        recording_control_->setHandlers(
            [this] {
                if (state_ == State::Ready) startSession(false);
                else if (state_ == State::Listening) stopSession(true);
            },
            [this] {
                if (state_ == State::Listening) stopSession(false);
            });
        recording_control_->setCloseHandler([this] {
            if (state_ == State::Listening || state_ == State::Stopping) return;
            hide();
            geometry_anchor_valid_ = false;
        });
        applyAppearance();
        recording_control_->setTranslationHandlers(
            [this] { setTranslationEnabled(!translation_settings_.enabled); },
            [this] {
                setTranslationDirection(translation_settings_.direction == TranslationDirection::ZhToEn
                    ? TranslationDirection::EnToZh
                    : TranslationDirection::ZhToEn);
            });
    }

    void buildTrayMenu() {
        tray_icon_ = new QSystemTrayIcon(this);
        tray_icon_->setIcon(sensevoiceIcon());
        tray_icon_->setToolTip(QStringLiteral("SenseVoice 语音输入"));
        auto* tray_menu = new QMenu(this);
        QAction* settings_action = tray_menu->addAction(QStringLiteral("设置..."));
        QAction* show_action = tray_menu->addAction(QStringLiteral("显示输入窗"));
        history_menu_ = tray_menu->addMenu(QStringLiteral("最近输入"));
        populateRecentMenu(history_menu_);
        tray_menu->addSeparator();
        tray_translation_action_ = tray_menu->addAction(QStringLiteral("翻译模式"));
        tray_translation_action_->setCheckable(true);
        QMenu* direction_menu = tray_menu->addMenu(QStringLiteral("翻译方向"));
        auto* direction_group = new QActionGroup(direction_menu);
        tray_zh_en_action_ = direction_menu->addAction(QStringLiteral("中文 → 英文"));
        tray_en_zh_action_ = direction_menu->addAction(QStringLiteral("英文 → 中文"));
        for (QAction* action : {tray_zh_en_action_, tray_en_zh_action_}) {
            action->setCheckable(true);
            direction_group->addAction(action);
        }
        connect(tray_translation_action_, &QAction::triggered, this,
                [this](bool checked) { setTranslationEnabled(checked); });
        connect(tray_zh_en_action_, &QAction::triggered, this,
                [this] { setTranslationDirection(TranslationDirection::ZhToEn); });
        connect(tray_en_zh_action_, &QAction::triggered, this,
                [this] { setTranslationDirection(TranslationDirection::EnToZh); });
        QAction* stop_speech_action = tray_menu->addAction(QStringLiteral("停止朗读"));
        connect(stop_speech_action, &QAction::triggered, this, [this] {
            if (speech_queue_ != nullptr) speech_queue_->clear();
        });
        tray_menu->addSeparator();
        QAction* exit_action = tray_menu->addAction(QStringLiteral("退出"));
        connect(settings_action, &QAction::triggered, this, [this] { openSettings(); });
        connect(show_action, &QAction::triggered, this, [this] {
            showPopup();
            setBubbleStatus(idleHint());
        });
        connect(exit_action, &QAction::triggered, this, [this] {
            close();
            QCoreApplication::quit();
        });
        tray_icon_->setContextMenu(tray_menu);
        connect(tray_icon_, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
                    if (reason == QSystemTrayIcon::Trigger ||
                        reason == QSystemTrayIcon::DoubleClick) {
                        showPopup();
                    }
                });
        tray_icon_->show();
    }

    void populateRecentMenu(QMenu* menu) {
        if (menu == nullptr) return;
        menu->clear();
        const QStringList entries = settings_.value(QStringLiteral("history/entries")).toStringList();
        if (entries.isEmpty()) {
            QAction* empty_action = menu->addAction(QStringLiteral("暂无记录"));
            empty_action->setEnabled(false);
            return;
        }
        for (const QString& entry : entries) {
            QString label = entry.simplified();
            if (label.size() > 32) label = label.left(32) + QStringLiteral("...");
            QAction* action = menu->addAction(label);
            action->setToolTip(entry);
            connect(action, &QAction::triggered, this, [this, entry] {
                QApplication::clipboard()->setText(entry);
                setTransientStatus(QStringLiteral("已复制最近输入"));
            });
        }
    }

    void recordHistory(const QString& text) {
        QStringList entries = settings_.value(QStringLiteral("history/entries")).toStringList();
        entries.prepend(text);
        while (entries.size() > 20) entries.removeLast();
        settings_.setValue(QStringLiteral("history/entries"), entries);
        settings_.sync();
        populateRecentMenu(history_menu_);
    }

    void showPopup() {
        if (isVisible()) return;
        geometry_anchor_valid_ = false;
        centerNearBottom();
        show();
    }

    void hidePopup() {
        if (state_ == State::Listening || state_ == State::Stopping) return;
        if (appearance_.pinned) return;
        hide();
        geometry_anchor_valid_ = false;
    }

    void loadSettings() {
        vad_settings_.endpoint_ms = settings_.value(QStringLiteral("vad/endpoint_ms"), 700).toInt();
        vad_settings_.threshold_percent = settings_.value(QStringLiteral("vad/threshold_percent"), 55).toInt();
        vad_settings_.minimum_db = settings_.value(QStringLiteral("vad/minimum_db"), -60).toInt();
        vad_settings_.snr_db = settings_.value(QStringLiteral("vad/snr_db"), 3).toInt();
        text_mode_ = settings_.value(QStringLiteral("text/mode"), 1).toInt() == 0
            ? TextMode::Raw
            : TextMode::Clean;
        const int profile_version = settings_.value(QStringLiteral("vad/profile_version"), 0).toInt();
        if (profile_version < 4 && vad_settings_.endpoint_ms == 700 &&
            vad_settings_.threshold_percent == 80 && vad_settings_.minimum_db == -45 &&
            vad_settings_.snr_db == 8) {
            vad_settings_ = {};
            settings_.setValue(QStringLiteral("vad/endpoint_ms"), vad_settings_.endpoint_ms);
            settings_.setValue(QStringLiteral("vad/threshold_percent"), vad_settings_.threshold_percent);
            settings_.setValue(QStringLiteral("vad/minimum_db"), vad_settings_.minimum_db);
            settings_.setValue(QStringLiteral("vad/snr_db"), vad_settings_.snr_db);
        }
        settings_.setValue(QStringLiteral("vad/profile_version"), 4);
        settings_.sync();
        translation_settings_.enabled =
            settings_.value(QStringLiteral("translation/enabled"), false).toBool();
        const QByteArray direction_code_value = settings_.value(
            QStringLiteral("translation/direction"), QStringLiteral("zh-en")).toString().toLatin1();
        translation_settings_.direction = parse_direction_code(direction_code_value.constData())
            .value_or(TranslationDirection::ZhToEn);
        translation_settings_.separator = sanitizedSeparator(settings_.value(
            QStringLiteral("translation/separator"),
            QString::fromLatin1(default_translation_separator)).toString());
        speech_settings_.enabled = settings_.value(QStringLiteral("speech/enabled"), false).toBool();
        speech_settings_.device_id = settings_.value(QStringLiteral("speech/device_id")).toString();
        speech_settings_.monitor = settings_.value(QStringLiteral("speech/monitor"), false).toBool();
        speech_settings_.voice_zh =
            settings_.value(QStringLiteral("speech/voice_zh"), default_speech_voice_zh).toInt();
        speech_settings_.voice_en =
            settings_.value(QStringLiteral("speech/voice_en"), default_speech_voice_en).toInt();
        speech_settings_.speed_percent =
            std::clamp(settings_.value(QStringLiteral("speech/speed_percent"), 100).toInt(), 50, 200);
        hotkey_shortcut_ = canonicalShortcut(settings_.value(
            QStringLiteral("hotkey/shortcut"), QString::fromLatin1(default_hotkey)).toString());
        if (!hasUsableShortcutKey(hotkey_shortcut_)) {
            hotkey_shortcut_ = QString::fromLatin1(default_hotkey);
        }
#ifdef _WIN32
        startup_enabled_ = windowsStartupEnabled();
#endif
        // Older builds stored one combined value; map it onto destination + content.
        const QString destination_value =
            settings_.value(QStringLiteral("output/destination"), QStringLiteral("insert")).toString();
        result_destination_ = destination_value == QStringLiteral("insert")
            ? ResultDestination::Insert : ResultDestination::CopyOnly;
        ResultContent legacy_content = translation_settings_.enabled ? ResultContent::Both : ResultContent::Original;
        if (destination_value == QStringLiteral("copy-translation")) legacy_content = ResultContent::Translation;
        else if (destination_value == QStringLiteral("copy-both")) legacy_content = ResultContent::Both;
        else if (destination_value == QStringLiteral("copy-original")) legacy_content = ResultContent::Original;
        result_content_ = settings_.contains(QStringLiteral("output/content"))
            ? resultContentFromName(settings_.value(QStringLiteral("output/content")).toString())
            : legacy_content;
        clipboard_content_ = settings_.contains(QStringLiteral("output/clipboard_content"))
            ? resultContentFromName(settings_.value(QStringLiteral("output/clipboard_content")).toString())
            : result_content_;
        hotkey_trigger_ = settings_.value(QStringLiteral("hotkey/trigger")).toString() == QStringLiteral("toggle")
            ? HotkeyTrigger::Toggle : HotkeyTrigger::Hold;
        paste_delay_ms_ = std::clamp(settings_.value(
            QStringLiteral("output/paste_delay_ms"), default_paste_delay_ms).toInt(), 0, maximum_paste_delay_ms);
        const AppearanceSettings defaults;
        appearance_.preset = themePresetFromName(settings_.value(
            QStringLiteral("appearance/preset"), themePresetName(defaults.preset)).toString());
        const auto load_color = [this](const QString& key, const QColor& fallback) {
            const QColor color(settings_.value(key, fallback.name()).toString());
            return color.isValid() ? color : fallback;
        };
        appearance_.custom.background =
            load_color(QStringLiteral("appearance/custom_background"), defaults.custom.background);
        appearance_.custom.text =
            load_color(QStringLiteral("appearance/custom_text"), defaults.custom.text);
        appearance_.custom.accent =
            load_color(QStringLiteral("appearance/custom_accent"), defaults.custom.accent);
        appearance_.custom_font_family =
            settings_.value(QStringLiteral("appearance/custom_font_family")).toString();
        appearance_.custom_font_size = std::clamp(
            settings_.value(QStringLiteral("appearance/custom_font_size"), defaults.custom_font_size).toInt(), 8, 24);
        const int linger_ms = settings_.value(QStringLiteral("appearance/linger_ms"), defaults.linger_ms).toInt();
        appearance_.linger_ms = std::clamp(linger_ms / linger_step_ms * linger_step_ms, 0, linger_maximum_ms);
        appearance_.pinned = settings_.value(QStringLiteral("appearance/pinned"), defaults.pinned).toBool();
    }

    void saveAppearanceSettings() {
        if (preview_mode_) return;
        settings_.setValue(QStringLiteral("appearance/preset"), themePresetName(appearance_.preset));
        settings_.setValue(QStringLiteral("appearance/custom_background"), appearance_.custom.background.name());
        settings_.setValue(QStringLiteral("appearance/custom_text"), appearance_.custom.text.name());
        settings_.setValue(QStringLiteral("appearance/custom_accent"), appearance_.custom.accent.name());
        settings_.setValue(QStringLiteral("appearance/custom_font_family"), appearance_.custom_font_family);
        settings_.setValue(QStringLiteral("appearance/custom_font_size"), appearance_.custom_font_size);
        settings_.setValue(QStringLiteral("appearance/linger_ms"), appearance_.linger_ms);
        settings_.setValue(QStringLiteral("appearance/pinned"), appearance_.pinned);
        settings_.sync();
    }

    void applyAppearance() {
        const std::optional<ThemeColors> colors = themeColors(appearance_);
        transcript_->setTheme(colors, themeFont(appearance_));
        recording_control_->setTheme(colors);
        recording_control_->setPinned(appearance_.pinned);
        updateWindowGeometry();
    }

#ifdef _WIN32
    // In toggle mode the stop press may still be held; Ctrl+V must not mix with it.
    void pasteWhenKeysReleased(std::uint64_t session_id, const WindowsTextInputTarget& target,
                               const QString& text, const QString& notice, int attempts_left) {
        if (shutting_down_.load(std::memory_order_acquire) || session_id != session_generation_) return;
        if (hotkeyKeysDown() && attempts_left > 0) {
            QTimer::singleShot(30, this, [this, session_id, target, text, notice, attempts_left] {
                pasteWhenKeysReleased(session_id, target, text, notice, attempts_left - 1);
            });
            return;
        }
        const bool pasted = !hotkeyKeysDown() && usableInjectionTarget(target) &&
            paste_clipboard_into_windows_text_input(target, GetCurrentProcessId());
        reportInjection(session_id, target, text, notice, pasted);
    }

    void reportInjection(std::uint64_t session_id, const WindowsTextInputTarget& target,
                         const QString& text, const QString& notice, bool injected) {
        if (injected) {
            last_target_ = {
                .window = target.window,
                .focus = target.focus,
                .process_id = target.process_id,
                .thread_id = target.thread_id,
            };
            recording_control_->setToolTip(QStringLiteral("已输入到光标位置"));
            if (notice.isEmpty()) scheduleLingerHide(session_id);
            else showTranslationNotice(text, notice);
            return;
        }
        recording_control_->setToolTip(QStringLiteral("已复制到剪贴板"));
        if (state_ == State::Ready) {
            showPopup();
            setBubbleStatus(QStringLiteral("注入失败，已复制到剪贴板"));
            scheduleLingerHide(session_id);
        }
    }
#endif

    QString idleHint() const {
        return (hotkey_trigger_ == HotkeyTrigger::Toggle ? QStringLiteral("按 %1 开始说话")
                                                        : QStringLiteral("按住 %1"))
            .arg(shortcutDisplayName(hotkey_shortcut_));
    }

    QString listeningHint() const {
        if (!toggle_active_) return QStringLiteral("正在听取...");
        return QStringLiteral("正在听取...再按 %1 结束").arg(shortcutDisplayName(hotkey_shortcut_));
    }

    void scheduleLingerHide(std::uint64_t session_id) {
        QTimer::singleShot(appearance_.linger_ms, this, [this, session_id] {
            if (!shutting_down_.load(std::memory_order_acquire) && session_id == session_generation_) {
                hidePopup();
            }
        });
    }

    void saveSettings() {
        settings_.setValue(QStringLiteral("vad/endpoint_ms"), vad_settings_.endpoint_ms);
        settings_.setValue(QStringLiteral("vad/threshold_percent"), vad_settings_.threshold_percent);
        settings_.setValue(QStringLiteral("vad/minimum_db"), vad_settings_.minimum_db);
        settings_.setValue(QStringLiteral("vad/snr_db"), vad_settings_.snr_db);
        settings_.setValue(QStringLiteral("hotkey/shortcut"), hotkey_shortcut_);
        settings_.setValue(QStringLiteral("text/mode"), text_mode_ == TextMode::Raw ? 0 : 1);
        settings_.setValue(QStringLiteral("output/destination"),
                           result_destination_ == ResultDestination::Insert ? QStringLiteral("insert")
                                                                            : QStringLiteral("copy"));
        settings_.setValue(QStringLiteral("output/content"), resultContentName(result_content_));
        settings_.setValue(QStringLiteral("output/clipboard_content"), resultContentName(clipboard_content_));
        settings_.setValue(QStringLiteral("hotkey/trigger"),
                           hotkey_trigger_ == HotkeyTrigger::Toggle ? QStringLiteral("toggle")
                                                                    : QStringLiteral("hold"));
        settings_.setValue(QStringLiteral("output/paste_delay_ms"), paste_delay_ms_);
        settings_.setValue(QStringLiteral("speech/enabled"), speech_settings_.enabled);
        settings_.setValue(QStringLiteral("speech/device_id"), speech_settings_.device_id);
        settings_.setValue(QStringLiteral("speech/monitor"), speech_settings_.monitor);
        settings_.setValue(QStringLiteral("speech/voice_zh"), speech_settings_.voice_zh);
        settings_.setValue(QStringLiteral("speech/voice_en"), speech_settings_.voice_en);
        settings_.setValue(QStringLiteral("speech/speed_percent"), speech_settings_.speed_percent);
        settings_.sync();
    }

    void saveTranslationSettings() {
        if (preview_mode_) return;
        settings_.setValue(QStringLiteral("translation/enabled"), translation_settings_.enabled);
        settings_.setValue(QStringLiteral("translation/direction"),
                           QString::fromLatin1(direction_code(translation_settings_.direction)));
        settings_.setValue(QStringLiteral("translation/separator"), translation_settings_.separator);
        settings_.sync();
    }

    void openSettings() {
        if (state_ == State::Listening || state_ == State::Stopping) return;
        if (state_ == State::Loading) {
            setTransientStatus(QStringLiteral("模型加载中"));
            return;
        }
        InputSettingsDialog dialog(
            vad_settings_, hotkey_shortcut_, text_mode_, hotkey_trigger_, result_destination_, result_content_, clipboard_content_, paste_delay_ms_, startup_enabled_,
            text_processor_.hotwords(), translation_settings_, speech_settings_, appearance_, bubble_style_,
            [this](const AppearanceSettings& appearance) {
                appearance_ = appearance;
                applyAppearance();
                saveAppearanceSettings();
            },
            this);
        if (dialog.exec() == QDialog::Accepted) {
            speech_settings_ = dialog.speech();
            applyTranslationSettings(dialog.translation());
            vad_settings_ = dialog.values();
            text_mode_ = dialog.mode();
            result_destination_ = dialog.destination();
            result_content_ = dialog.inputContent();
            clipboard_content_ = dialog.clipboardContent();
            hotkey_trigger_ = dialog.trigger();
            paste_delay_ms_ = dialog.pasteDelayMs();
            recording_control_->setMode(text_mode_);
            text_processor_.set_hotwords(dialog.hotwords());
            const QString selected_shortcut = dialog.shortcut();
#ifdef _WIN32
            applyHotkey(selected_shortcut);
            const bool requested_startup = dialog.startupEnabled();
            if (!setWindowsStartupEnabled(requested_startup)) {
                QMessageBox::warning(
                    this, QStringLiteral("开机启动设置失败"),
                    QStringLiteral("无法写入当前用户的 Windows 启动项。"));
            } else {
                startup_enabled_ = requested_startup;
            }
#else
            hotkey_shortcut_ = selected_shortcut;
#endif
            saveSettings();
            std::string hotword_error;
            const std::filesystem::path hotword_path =
                std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
                L"hotwords.tsv";
            if (!text_processor_.save_hotwords(hotword_path, hotword_error)) {
                QMessageBox::warning(this, QStringLiteral("热词未保存"), to_qstring(hotword_error));
            }
            setTransientStatus(QStringLiteral("已应用：%1")
                                   .arg(shortcutDisplayName(hotkey_shortcut_)));
            QTimer::singleShot(1600, this, [this] { hidePopup(); });
        }
    }

    void centerNearBottom() {
        QScreen* screen = QGuiApplication::primaryScreen();
        if (screen == nullptr) return;
        const QRect area = screen->availableGeometry();
        move(area.center().x() - width() / 2, area.bottom() - height() - 72);
    }

#ifdef _WIN32
    static QRegion roundedRegion(const QRect& rect, int radius) {
        if (rect.isEmpty()) return {};
        const int clamped_radius = std::clamp(
            radius, 0, std::min(rect.width(), rect.height()) / 2);
        if (clamped_radius == 0) return QRegion(rect);

        const int diameter = clamped_radius * 2;
        QRegion region(rect.adjusted(clamped_radius, 0, -clamped_radius, 0));
        region += QRegion(rect.adjusted(0, clamped_radius, 0, -clamped_radius));
        region += QRegion(QRect(rect.left(), rect.top(), diameter, diameter), QRegion::Ellipse);
        region += QRegion(QRect(rect.right() - diameter + 1, rect.top(), diameter, diameter),
                          QRegion::Ellipse);
        region += QRegion(QRect(rect.left(), rect.bottom() - diameter + 1, diameter, diameter),
                          QRegion::Ellipse);
        region += QRegion(QRect(rect.right() - diameter + 1,
                                rect.bottom() - diameter + 1,
                                diameter,
                                diameter),
                          QRegion::Ellipse);
        return region;
    }

    void updateGlassMask() {
        // Do not use QWidget::setMask here. Windows regions are integer-pixel
        // clips and produce the jagged gray fringe visible around translucent
        // text bubbles. Qt's per-pixel alpha already gives us smooth corners.
    }

    void applyGlassBackdrop() {
        // Keep the top-level layered window transparent between its two child
        // bubbles. System BlurBehind applies to the entire rectangular window
        // and makes that transparent gap look opaque on some Windows builds.
        clearMask();
    }

    struct HotkeyBinding {
        UINT modifiers = 0;
        UINT virtual_key = 0;
        bool control_windows = false;
        bool valid = false;
    };

    static UINT virtualKeyForQtKey(Qt::Key key) {
        if (key >= Qt::Key_A && key <= Qt::Key_Z) return static_cast<UINT>(key);
        if (key >= Qt::Key_0 && key <= Qt::Key_9) return static_cast<UINT>(key);
        if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
            return VK_F1 + static_cast<UINT>(key - Qt::Key_F1);
        }
        switch (key) {
        case Qt::Key_Space: return VK_SPACE;
        case Qt::Key_Return:
        case Qt::Key_Enter: return VK_RETURN;
        case Qt::Key_Tab: return VK_TAB;
        case Qt::Key_Backspace: return VK_BACK;
        case Qt::Key_Delete: return VK_DELETE;
        case Qt::Key_Insert: return VK_INSERT;
        case Qt::Key_Escape: return VK_ESCAPE;
        case Qt::Key_Left: return VK_LEFT;
        case Qt::Key_Right: return VK_RIGHT;
        case Qt::Key_Up: return VK_UP;
        case Qt::Key_Down: return VK_DOWN;
        case Qt::Key_Home: return VK_HOME;
        case Qt::Key_End: return VK_END;
        case Qt::Key_PageUp: return VK_PRIOR;
        case Qt::Key_PageDown: return VK_NEXT;
        case Qt::Key_Comma: return VK_OEM_COMMA;
        case Qt::Key_Period: return VK_OEM_PERIOD;
        case Qt::Key_Slash: return VK_OEM_2;
        case Qt::Key_Semicolon: return VK_OEM_1;
        case Qt::Key_Minus: return VK_OEM_MINUS;
        case Qt::Key_Equal: return VK_OEM_PLUS;
        case Qt::Key_BracketLeft: return VK_OEM_4;
        case Qt::Key_BracketRight: return VK_OEM_6;
        case Qt::Key_Backslash: return VK_OEM_5;
        case Qt::Key_Apostrophe: return VK_OEM_7;
        case Qt::Key_QuoteLeft: return VK_OEM_3;
        default: return 0;
        }
    }

    void rebuildHotkeyBinding() {
        hotkey_binding_ = {};
        if (hotkey_shortcut_ == QString::fromLatin1(control_windows_hotkey)) {
            hotkey_binding_.modifiers = MOD_CONTROL;
            hotkey_binding_.virtual_key = VK_LWIN;
            hotkey_binding_.control_windows = true;
            hotkey_binding_.valid = true;
            return;
        }

        const QKeySequence sequence = QKeySequence::fromString(
            hotkey_shortcut_, QKeySequence::PortableText);
        if (sequence.isEmpty() || sequence.count() != 1) return;
        const QKeyCombination combination = sequence[0];
        hotkey_binding_.virtual_key = virtualKeyForQtKey(combination.key());
        if (hotkey_binding_.virtual_key == 0) return;
        const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
        if (modifiers.testFlag(Qt::ControlModifier)) hotkey_binding_.modifiers |= MOD_CONTROL;
        if (modifiers.testFlag(Qt::AltModifier)) hotkey_binding_.modifiers |= MOD_ALT;
        if (modifiers.testFlag(Qt::ShiftModifier)) hotkey_binding_.modifiers |= MOD_SHIFT;
        if (modifiers.testFlag(Qt::MetaModifier)) hotkey_binding_.modifiers |= MOD_WIN;
        hotkey_binding_.valid = true;
    }

    void registerHotkeys() {
        if (!hotkey_binding_.valid) return;
        const HWND handle = reinterpret_cast<HWND>(winId());
        if (hotkey_binding_.control_windows) {
            hotkey_left_registered_ = RegisterHotKey(
                handle, hotkey_left_id, MOD_CONTROL | MOD_NOREPEAT, VK_LWIN) != FALSE;
            hotkey_right_registered_ = RegisterHotKey(
                handle, hotkey_right_id, MOD_CONTROL | MOD_NOREPEAT, VK_RWIN) != FALSE;
        } else {
            hotkey_primary_registered_ = RegisterHotKey(
                handle, hotkey_primary_id, hotkey_binding_.modifiers | MOD_NOREPEAT,
                hotkey_binding_.virtual_key) != FALSE;
        }
        const bool registered = hotkey_primary_registered_ || hotkey_left_registered_ ||
            hotkey_right_registered_;
        if (!registered) {
            recording_control_->setToolTip(QStringLiteral("系统快捷键被占用，已切换为后台按键监听"));
        }
    }

    void unregisterHotkeys() {
        const HWND handle = reinterpret_cast<HWND>(winId());
        if (hotkey_primary_registered_) UnregisterHotKey(handle, hotkey_primary_id);
        if (hotkey_left_registered_) UnregisterHotKey(handle, hotkey_left_id);
        if (hotkey_right_registered_) UnregisterHotKey(handle, hotkey_right_id);
        hotkey_primary_registered_ = false;
        hotkey_left_registered_ = false;
        hotkey_right_registered_ = false;
    }

    void applyHotkey(const QString& requested_shortcut) {
        const QString canonical = canonicalShortcut(requested_shortcut);
        if (!hasUsableShortcutKey(canonical)) return;
        if (canonical == hotkey_shortcut_) return;

        hotkey_hold_timer_.stop();
        hotkey_release_timer_.stop();
        hotkey_pending_ = false;
        hotkey_recording_ = false;
        unregisterHotkeys();
        hotkey_shortcut_ = canonical;
        rebuildHotkeyBinding();
        registerHotkeys();
    }

    static LRESULT CALLBACK keyboardHookProcedure(int code, WPARAM message, LPARAM data) {
        if (code == HC_ACTION && hotkey_message_window_ != nullptr) {
            const auto* event = reinterpret_cast<const KBDLLHOOKSTRUCT*>(data);
            const bool pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            const bool released = message == WM_KEYUP || message == WM_SYSKEYUP;
            if ((pressed || released) && event->vkCode <= 0xFF) {
                PostMessageW(hotkey_message_window_, hotkey_state_message,
                             static_cast<WPARAM>(event->vkCode), pressed ? 1 : 0);
            }
        }
        return CallNextHookEx(keyboard_hook_, code, message, data);
    }

    void installKeyboardHook() {
        hotkey_message_window_ = reinterpret_cast<HWND>(winId());
        keyboard_hook_ = SetWindowsHookExW(
            WH_KEYBOARD_LL, keyboardHookProcedure, GetModuleHandleW(nullptr), 0);
        if (keyboard_hook_ == nullptr) {
            recording_control_->setToolTip(QStringLiteral("按住快捷键监听失败，请点击开始"));
        }
    }

    void uninstallKeyboardHook() {
        hotkey_message_window_ = nullptr;
        if (keyboard_hook_ != nullptr) {
            UnhookWindowsHookEx(keyboard_hook_);
            keyboard_hook_ = nullptr;
        }
    }

    bool hotkeyKeysDown() const {
        if (!hotkey_binding_.valid) return false;
        const auto key_down = [](int key) {
            return (GetAsyncKeyState(key) & 0x8000) != 0;
        };
        const auto control_down = [&key_down] {
            return key_down(VK_LCONTROL) || key_down(VK_RCONTROL);
        };
        const auto shift_down = [&key_down] {
            return key_down(VK_LSHIFT) || key_down(VK_RSHIFT);
        };
        const auto alt_down = [&key_down] {
            return key_down(VK_LMENU) || key_down(VK_RMENU);
        };
        const auto windows_down = [&key_down] {
            return key_down(VK_LWIN) || key_down(VK_RWIN);
        };

        if (hotkey_binding_.control_windows) {
            return control_down() && windows_down();
        }
        if ((hotkey_binding_.modifiers & MOD_CONTROL) != 0 && !control_down()) return false;
        if ((hotkey_binding_.modifiers & MOD_SHIFT) != 0 && !shift_down()) return false;
        if ((hotkey_binding_.modifiers & MOD_ALT) != 0 && !alt_down()) return false;
        if ((hotkey_binding_.modifiers & MOD_WIN) != 0 && !windows_down()) return false;
        return key_down(static_cast<int>(hotkey_binding_.virtual_key));
    }

    static bool usableInjectionTarget(const WindowsTextInputTarget& target) {
        if (!target.valid()) return false;
        const HWND window = reinterpret_cast<HWND>(target.window);
        if (window == nullptr || IsWindow(window) == FALSE ||
            GetAncestor(window, GA_ROOT) != window) {
            return false;
        }
        DWORD process_id = 0;
        if (GetWindowThreadProcessId(window, &process_id) == 0 ||
            process_id == GetCurrentProcessId()) {
            return false;
        }
        return target.process_id == 0 || target.process_id == process_id;
    }

    void selectInjectionTarget() {
        const WindowsTextInputTarget captured =
            capture_windows_text_input_target(GetCurrentProcessId());
        if (usableInjectionTarget(captured)) {
            target_ = captured;
        } else if (usableInjectionTarget(last_target_)) {
            target_ = last_target_;
        } else {
            target_ = {};
        }
        inject_on_complete_ = target_.valid();
    }

    void handleHotkeyPressed() {
        if (hotkey_trigger_ == HotkeyTrigger::Toggle) {
            handleTogglePress();
            return;
        }
        if (hotkey_pending_ || hotkey_recording_) return;
        if (state_ == State::Ready) selectInjectionTarget();
        showPopup();
        if (state_ != State::Ready) {
            if (state_ == State::Loading) setTransientStatus(QStringLiteral("模型加载中"));
            return;
        }
        hotkey_pending_ = true;
        setBubbleStatus(QStringLiteral("继续按住..."));
        hotkey_hold_timer_.start(280);
        hotkey_release_timer_.start();
    }

    // Holding the keys repeats the press, so act only on the first one until release.
    void handleTogglePress() {
        if (toggle_latch_) return;
        toggle_latch_ = true;
        hotkey_release_timer_.start();
        if (toggle_active_) {
            if (state_ != State::Listening) return;
            const WindowsTextInputTarget captured =
                capture_windows_text_input_target(GetCurrentProcessId());
            if (usableInjectionTarget(captured)) {
                target_ = captured;
                inject_on_complete_ = true;
            }
            stopSession(true);
            hotkey_release_timer_.start();
            return;
        }
        if (state_ == State::Ready) selectInjectionTarget();
        showPopup();
        if (state_ != State::Ready) {
            if (state_ == State::Loading) setTransientStatus(QStringLiteral("模型加载中"));
            return;
        }
        startSession(true);
        if (state_ == State::Listening) {
            toggle_active_ = true;
            toggle_limit_timer_.start(toggle_session_limit_ms);
            setBubbleStatus(listeningHint());
        }
    }

    void beginHotkeySession() {
        if (!hotkey_pending_) return;
        if (!hotkeyKeysDown()) {
            hotkey_pending_ = false;
            hotkey_release_timer_.stop();
            setTransientStatus(QStringLiteral("按住时间太短"));
            return;
        }
        hotkey_pending_ = false;
        hotkey_recording_ = true;
        startSession(true);
        if (state_ != State::Listening) hotkey_recording_ = false;
    }

    void pollHotkeyRelease() {
        if (hotkeyKeysDown()) return;
        if (toggle_latch_) {
            toggle_latch_ = false;
            hotkey_release_timer_.stop();
            return;
        }
        if (hotkey_pending_) {
            hotkey_pending_ = false;
            hotkey_hold_timer_.stop();
            hotkey_release_timer_.stop();
            setTransientStatus(QStringLiteral("按住时间太短"));
            QTimer::singleShot(800, this, [this] { hidePopup(); });
            return;
        }
        if (hotkey_recording_) {
            hotkey_recording_ = false;
            hotkey_release_timer_.stop();
            if (state_ == State::Listening) stopSession(true);
            return;
        }
        hotkey_release_timer_.stop();
    }

    bool injectIntoTarget(WindowsTextInputTarget target, const QString& text, bool* paste_safe) {
        *paste_safe = false;
        if (!usableInjectionTarget(target) || text.trimmed().isEmpty()) return false;
        try {
            const std::wstring wide_text = text.toStdWString();
            return inject_text_into_windows_text_input(
                std::move(target),
                wide_text,
                GetCurrentProcessId(),
                paste_safe);
        } catch (...) {
            // A dead target or a failing COM provider must degrade to the
            // clipboard fallback instead of terminating the voice input UI.
            return false;
        }
    }
#endif

    void beginLoadModels() {
        loader_ = std::thread([this] {
            const std::filesystem::path directory =
                QCoreApplication::applicationDirPath().toStdWString();
            std::string error;
            bool success = engine_.load(directory / L"models" / L"sensevoice-small-q8.gguf", 8, error) &&
                vad_.load(directory / L"models" / L"fsmn-vad.gguf", 8, error);
            if (success && std::filesystem::exists(directory / L"hotwords.tsv")) {
                success = text_processor_.load_hotwords(directory / L"hotwords.tsv", error);
            }
            if (success && std::filesystem::exists(directory / L"corrections.tsv")) {
                success = text_processor_.load_correction_rules(directory / L"corrections.tsv", error);
            }
            if (success && std::filesystem::exists(directory / L"dict")) {
                success = text_processor_.initialize_segmenter(directory / L"dict", error);
            }
            if (shutting_down_.load(std::memory_order_acquire)) return;
            QMetaObject::invokeMethod(this, [this, success, error] {
                if (success) {
                    state_ = State::Ready;
                    setBubbleStatus(idleHint());
                    recording_control_->setEnabled(true);
                    if (translation_settings_.enabled) requestTranslationModel();
                } else {
                    state_ = State::Error;
                    setBubbleStatus(QStringLiteral("模型加载失败"));
                    recording_control_->setToolTip(to_qstring(error));
                }
            }, Qt::QueuedConnection);
        });
    }

    void startSession(bool via_hotkey) {
        if (state_ != State::Ready) return;
        if (stopper_.joinable()) stopper_.join();
        ++session_generation_;
        status_reset_timer_.stop();
        committed_text_.clear();
        partial_text_.clear();
        stop_should_commit_ = false;
        translation_tickets_.clear();
        awaiting_translation_ = false;
        if (translation_settings_.enabled) {
            translation_session_.emplace(translation_settings_.direction);
        } else {
            translation_session_.reset();
        }
#ifdef _WIN32
        if (!via_hotkey) {
            selectInjectionTarget();
        }
#else
        (void)via_hotkey;
        inject_on_complete_ = false;
#endif

        recognizer_ = std::make_unique<StreamRecognizer>(
            engine_,
            &vad_,
            StreamRecognizerConfig{
                .partial_interval_ms = 450,
                .minimum_audio_ms = 600,
                .minimum_new_audio_ms = 240,
                .endpoint_silence_ms = vad_settings_.endpoint_ms,
                .maximum_utterance_ms = 15'000,
                .memory_limit_mb = 300,
                .vad_speech_threshold = vad_settings_.threshold_percent / 100.0F,
                .vad_minimum_db = static_cast<float>(vad_settings_.minimum_db),
                .vad_minimum_snr_db = static_cast<float>(vad_settings_.snr_db),
                .vad_minimum_speech_ms = 200,
            },
            [this](const RecognitionEvent& event) {
                if (shutting_down_.load(std::memory_order_acquire)) return;
                QMetaObject::invokeMethod(this, [this, event] { handleRecognition(event); }, Qt::QueuedConnection);
            },
            &text_processor_);
        std::string playback_mute_error;
        if (!playback_mute_.mute(playback_mute_error)) {
            recording_control_->setToolTip(QStringLiteral("无法暂时静音系统播放：%1")
                                               .arg(to_qstring(playback_mute_error)));
        }
        microphone_ = std::make_unique<MicrophoneCapture>();
        recognizer_->start();

        std::string error;
        if (!microphone_->start(
                [this](std::span<const float> samples) {
                    if (recognizer_ != nullptr) recognizer_->accept_pcm(samples);
                },
                error)) {
            recognizer_->cancel();
            recognizer_.reset();
            playback_mute_.restore();
            microphone_.reset();
            state_ = State::Ready;
            recording_control_->setListening(false);
            recording_control_->setEnabled(true);
            recording_control_->setToolTip(to_qstring(error));
#ifdef _WIN32
            hotkey_recording_ = false;
            hotkey_release_timer_.stop();
#endif
            setTransientStatus(QStringLiteral("麦克风不可用"));
            return;
        }

        state_ = State::Listening;
        session_elapsed_.start();
        setBubbleStatus(listeningHint());
        beginStableSessionGeometry();
        recording_control_->setListening(true);
        recording_control_->setEnabled(true);
        updateTranslationUi();
    }

    void stopSession(bool commit) {
        if (state_ != State::Listening || microphone_ == nullptr || recognizer_ == nullptr) return;
        state_ = State::Stopping;
        stop_should_commit_ = commit;
        if (!commit) inject_on_complete_ = false;
        toggle_active_ = false;
        toggle_limit_timer_.stop();
#ifdef _WIN32
        hotkey_recording_ = false;
        hotkey_release_timer_.stop();
#endif
        setBubbleStatus(commit ? QStringLiteral("正在完成...") : QStringLiteral("正在取消..."));
        recording_control_->setStopping(true);

        if (stopper_.joinable()) stopper_.join();
        stopper_ = std::thread([this, commit] {
            microphone_->stop();
            if (commit) recognizer_->finish();
            else recognizer_->cancel();
            if (shutting_down_.load(std::memory_order_acquire)) return;
            QMetaObject::invokeMethod(this, [this, commit] { stopWorkerFinished(commit); }, Qt::QueuedConnection);
        });
    }

    void stopWorkerFinished(bool commit) {
        if (stopper_.joinable()) stopper_.join();
        playback_mute_.restore();
        QTimer::singleShot(0, this, [this, commit] { sessionStopped(commit); });
    }

    void sessionStopped(bool commit) {
        microphone_.reset();
        recognizer_.reset();
        partial_text_.clear();
        if (commit && translation_session_.has_value() && !translation_session_->empty()) {
            beginTranslationWait();
            return;
        }
        translation_session_.reset();
        translation_tickets_.clear();

        QString final_text = committed_text_.trimmed();
        if (commit && !final_text.isEmpty() && text_mode_ == TextMode::Clean) {
            final_text = polished(final_text);
        }
        finishSession(commit, final_text, final_text, {}, {});
    }

    // final_text: what Insert types (bilingual when translated). translation is
    // empty when translation was off or fell back. notice: orange fallback hint.
    void finishSession(bool commit, QString final_text, const QString& original,
                       const QString& translation, QString notice) {
        const std::uint64_t session_id = session_generation_;
        const bool copy_only = result_destination_ == ResultDestination::CopyOnly;
        const auto text_for = [&](ResultContent content) {
            if (translation.isEmpty() || content == ResultContent::Original) return original;
            return content == ResultContent::Translation ? translation : final_text;
        };
        const QString bubble_text = text_for(result_content_);
        const QString clipboard_text = text_for(clipboard_content_);
        const QString& input_text = clipboard_text;
        const bool wants_translation = clipboard_content_ != ResultContent::Original ||
            result_content_ != ResultContent::Original;
        if (!wants_translation) {
            notice.clear();
        } else if (translation.isEmpty() && notice.isEmpty()) {
            notice = QStringLiteral("未开启翻译，已输入原文");
        }
        if (copy_only) notice.replace(QStringLiteral("已输入"), QStringLiteral("已复制"));
        // 朗读 never falls back to the 原文: no 译文, nothing is spoken.
        if (commit && !final_text.isEmpty() && speechActive()) {
            if (translation.isEmpty()) {
                notice = notice.isEmpty() ? QStringLiteral("没有译文，未朗读")
                                          : notice + QStringLiteral("，未朗读");
            } else {
                speak(translation);
            }
        }
        const QString output_text = bubble_text;
        if (!commit) {
            committed_text_.clear();
        } else if (!final_text.isEmpty()) {
            committed_text_ = output_text;
            QApplication::clipboard()->setText(clipboard_text);
            recordHistory(clipboard_text);
            std::string hotword_error;
            const std::filesystem::path hotword_path =
                std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) /
                L"hotwords.tsv";
            if (!text_processor_.save_hotwords(hotword_path, hotword_error)) {
                recording_control_->setToolTip(to_qstring(hotword_error));
            }
        }

        state_ = State::Ready;
        stop_should_commit_ = false;
        recording_control_->setListening(false);
        recording_control_->setEnabled(true);
        updateTranslationUi();
        if (!commit) {
#ifdef _WIN32
            target_ = {};
            inject_on_complete_ = false;
#endif
            setTransientStatus(QStringLiteral("已取消"));
            QTimer::singleShot(500, this, [this] { hidePopup(); });
        } else if (output_text.isEmpty()) {
#ifdef _WIN32
            target_ = {};
            inject_on_complete_ = false;
#endif
            setTransientStatus(QStringLiteral("未识别到"));
            QTimer::singleShot(500, this, [this] { hidePopup(); });
        } else {
#ifdef _WIN32
            WindowsTextInputTarget target = std::move(target_);
            if (copy_only) {
                inject_on_complete_ = false;
                const QString copy_status = QStringLiteral("已复制到剪贴板");
                transcript_->setBubbleContent(
                    output_text,
                    notice.isEmpty() ? copy_status : notice,
                    notice.isEmpty() ? TranscriptBubble::SecondaryTone::Muted
                                     : TranscriptBubble::SecondaryTone::Warning);
                updateWindowGeometry();
                recording_control_->setToolTip(copy_status);
                scheduleLingerHide(session_id);
                return;
            }
            const QString text_to_inject = input_text;
            const bool should_inject = inject_on_complete_ && usableInjectionTarget(target);
            inject_on_complete_ = false;
            refreshTranscript();
            QTimer::singleShot(160, this, [this, session_id, target = std::move(target), text_to_inject, should_inject, notice] {
                if (shutting_down_.load(std::memory_order_acquire) ||
                    session_id != session_generation_ || recording_control_ == nullptr) {
                    return;
                }
                if (!should_inject) {
                    recording_control_->setToolTip(QStringLiteral("没有输入目标，已复制到剪贴板"));
                    if (notice.isEmpty()) scheduleLingerHide(session_id);
                    else showTranslationNotice(text_to_inject, notice);
                    return;
                }
                bool paste_safe = false;
                if (injectIntoTarget(target, text_to_inject, &paste_safe)) {
                    reportInjection(session_id, target, text_to_inject, notice, true);
                    return;
                }
                if (!paste_safe) {
                    reportInjection(session_id, target, text_to_inject, notice, false);
                    return;
                }
                // The clipboard already holds the text; give the user's own key
                // releases time to settle before sending Ctrl+V.
                QTimer::singleShot(paste_delay_ms_, this, [this, session_id, target, text_to_inject, notice] {
                    pasteWhenKeysReleased(session_id, target, text_to_inject, notice, 100);
                });
            });
#else
            (void)notice;
            refreshTranscript();
            recording_control_->setToolTip(QStringLiteral("已复制到剪贴板"));
            scheduleLingerHide(session_id);
#endif
        }
    }

    void handleRecognition(const RecognitionEvent& event) {
        if (state_ == State::Stopping && !stop_should_commit_) return;
        if (event.kind == RecognitionEventKind::Error) {
            setTransientStatus(QStringLiteral("识别失败"));
            return;
        }
        const QString value = to_qstring(event.text).trimmed();
        if (event.kind == RecognitionEventKind::Final) {
            if (!value.isEmpty()) {
                if (translation_session_.has_value()) submitForTranslation(value);
                else committed_text_ += value;
            }
            partial_text_.clear();
        } else {
            partial_text_ = value;
        }
        if (state_ != State::Stopping) refreshTranscript();
    }

    static QString polished(const QString& text) {
        const QByteArray utf8 = text.toUtf8();
        return to_qstring(TextProcessor::polish_dictation(
            std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size()))));
    }

    void createTranslationWorker() {
        const std::filesystem::path models =
            std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / L"models";
        translation_worker_ = std::make_unique<TranslationWorker>(
            make_opus_mt_factory(models, 4),
            [this](TranslationDirection direction, bool ok, const std::string& error) {
                if (shutting_down_.load(std::memory_order_acquire)) return;
                QMetaObject::invokeMethod(this, [this, direction, ok, error] {
                    translationModelLoaded(direction, ok, error);
                }, Qt::QueuedConnection);
            },
            [this](std::uint64_t ticket, std::optional<std::string> translation, const std::string&) {
                if (shutting_down_.load(std::memory_order_acquire)) return;
                QMetaObject::invokeMethod(this, [this, ticket, translation] {
                    translationFinished(ticket, translation);
                }, Qt::QueuedConnection);
            });
    }

    // Single place that changes the translation mode, so the floating control,
    // tray menu, settings dialog and persisted values never disagree.
    void applyTranslationSettings(const TranslationSettings& requested) {
        const bool locked = state_ == State::Listening || state_ == State::Stopping;
        if (!locked) {
            const bool model_changed = requested.enabled != translation_settings_.enabled ||
                requested.direction != translation_settings_.direction;
            translation_settings_ = requested;
            translation_settings_.separator = sanitizedSeparator(requested.separator);
            saveTranslationSettings();
            if (model_changed) {
                if (!translation_settings_.enabled) {
                    translation_loading_ = false;
                    translation_ready_direction_.reset();
                    if (translation_worker_) translation_worker_->unload();
                } else if (state_ != State::Loading) {
                    // While the recognizer is still loading, beginLoadModels requests it.
                    requestTranslationModel();
                }
            }
        }
        updateTranslationUi();
        applySpeechSettings();
    }

    void setTranslationEnabled(bool enabled) {
        TranslationSettings requested = translation_settings_;
        requested.enabled = enabled;
        applyTranslationSettings(requested);
    }

    void setTranslationDirection(TranslationDirection direction) {
        TranslationSettings requested = translation_settings_;
        requested.direction = direction;
        applyTranslationSettings(requested);
    }

    void requestTranslationModel() {
        if (!translation_worker_) return;
        if (translation_ready_direction_ == translation_settings_.direction) return;
        translation_ready_direction_.reset();
        translation_loading_ = true;
        translation_worker_->load(translation_settings_.direction);
        updateTranslationUi();
    }

    void translationModelLoaded(TranslationDirection direction, bool ok, const std::string& error) {
        // A newer request superseded this one; its own result will follow.
        if (!translation_settings_.enabled || direction != translation_settings_.direction) return;
        translation_loading_ = false;
        if (ok) {
            translation_ready_direction_ = direction;
        } else {
            translation_ready_direction_.reset();
            translation_settings_.enabled = false;
            saveTranslationSettings();
            if (translation_worker_) translation_worker_->unload();
            applySpeechSettings();
            if (tray_icon_ != nullptr) {
                tray_icon_->showMessage(
                    QStringLiteral("翻译模型加载失败"),
                    QStringLiteral("已关闭翻译模式。%1").arg(to_qstring(error)),
                    QSystemTrayIcon::Warning, 6000);
            }
            if (isVisible() && state_ == State::Ready) {
                setTransientStatus(QStringLiteral("翻译模型加载失败，已关闭翻译模式"));
            }
        }
        updateTranslationUi();
    }

    void updateTranslationUi() {
        const bool locked = state_ == State::Listening || state_ == State::Stopping;
        recording_control_->setTranslationState(
            translation_settings_.enabled, translation_settings_.direction,
            translation_loading_, locked);
        if (tray_translation_action_ != nullptr) {
            tray_translation_action_->setChecked(translation_settings_.enabled);
            tray_zh_en_action_->setChecked(translation_settings_.direction == TranslationDirection::ZhToEn);
            tray_en_zh_action_->setChecked(translation_settings_.direction == TranslationDirection::EnToZh);
            for (QAction* action : {tray_translation_action_, tray_zh_en_action_, tray_en_zh_action_}) {
                action->setEnabled(!locked);
            }
        }
    }

    // Organise first, then translate: the clean-mode polish runs per sentence so the
    // text the model sees is exactly the text that gets injected.
    void submitForTranslation(const QString& sentence) {
        const QString organised = text_mode_ == TextMode::Clean ? polished(sentence) : sentence;
        committed_text_ += organised;
        const std::string utf8 = to_utf8_string(organised);
        const auto index = translation_session_->add_sentence(utf8);
        if (!index.has_value() || !translation_session_->is_pending(*index)) return;
        if (!translation_worker_) {
            translation_session_->set_failed(*index);
            return;
        }
        const std::uint64_t ticket = ++next_translation_ticket_;
        translation_tickets_[ticket] = PendingTranslation{session_generation_, *index};
        translation_worker_->translate(ticket, utf8);
    }

    void translationFinished(std::uint64_t ticket, const std::optional<std::string>& translation) {
        const auto found = translation_tickets_.find(ticket);
        if (found == translation_tickets_.end()) return;
        const PendingTranslation pending = found->second;
        translation_tickets_.erase(found);
        if (!translation_session_.has_value() || pending.session != session_generation_) return;
        if (translation.has_value()) translation_session_->set_translation(pending.index, *translation);
        else translation_session_->set_failed(pending.index);

        if (awaiting_translation_) {
            if (!translation_session_->has_pending()) completeTranslation(false);
        } else if (state_ == State::Listening) {
            refreshTranscript();
        }
    }

    // The hotkey is released; hold the session in Stopping until every sentence
    // is translated or the deadline passes.
    void beginTranslationWait() {
        awaiting_translation_ = true;
        if (!translation_session_->has_pending()) {
            completeTranslation(false);
            return;
        }
        transcript_->setBubbleContent(
            to_qstring(translation_session_->original_text()),
            QStringLiteral("翻译中..."),
            TranscriptBubble::SecondaryTone::Pending);
        updateWindowGeometry();
        translation_deadline_.start(translation_timeout_ms);
    }

    void completeTranslation(bool timed_out) {
        if (!awaiting_translation_ || !translation_session_.has_value()) return;
        awaiting_translation_ = false;
        translation_deadline_.stop();
        const SessionInjection injection = translation_session_->compose(
            to_utf8_string(translation_settings_.separator), timed_out);
        const bool translated = injection.fallback == FallbackReason::None;
        const QString original = translated
            ? to_qstring(translation_session_->original_text())
            : to_qstring(injection.text);
        const QString translation = translated
            ? to_qstring(translation_session_->translated_text())
            : QString{};
        QString notice;
        switch (injection.fallback) {
        case FallbackReason::None:
            break;
        case FallbackReason::LanguageMismatch:
            notice = QStringLiteral("语种与方向不符，已输入原文");
            break;
        case FallbackReason::TranslationFailed:
            notice = QStringLiteral("翻译失败，已输入原文");
            break;
        case FallbackReason::Timeout:
            notice = QStringLiteral("翻译超时，已输入原文");
            break;
        }
        translation_session_.reset();
        translation_tickets_.clear();
        finishSession(true, to_qstring(injection.text), original, translation, notice);
    }

    void createSpeechQueue() {
        const QString app_directory = QCoreApplication::applicationDirPath();
        speech_queue_ = new SpeechQueue(
            QDir(app_directory).filePath(QStringLiteral("sensevoice-tts.exe")),
            QDir(app_directory).filePath(QStringLiteral("models/kokoro-multi-lang-v1_1")),
            [this](const QString& message) {
                if (shutting_down_.load(std::memory_order_acquire)) return;
                showSpeechNotice(message);
            },
            [this](SpeechQueue::State state, const QString& error) {
                if (shutting_down_.load(std::memory_order_acquire)) return;
                if (state != SpeechQueue::State::Failed) return;
                if (tray_icon_ != nullptr) {
                    tray_icon_->showMessage(QStringLiteral("朗读不可用"),
                                            QStringLiteral("朗读模型加载失败。%1").arg(error),
                                            QSystemTrayIcon::Warning, 6000);
                }
            },
            this);
        applySpeechSettings();
    }

    [[nodiscard]] bool speechActive() const {
        return speech_queue_ != nullptr && speech_settings_.enabled && translation_settings_.enabled;
    }

    // Starts or stops the synthesis helper to match the 朗读 and translation switches.
    void applySpeechSettings() {
        if (speech_queue_ == nullptr) return;
        speech_queue_->setOutput(speech_settings_.device_id, speech_settings_.monitor);
        const bool active = speechActive();
        if (!active || speech_queue_->state() != SpeechQueue::State::Failed) {
            speech_queue_->setActive(active);
        } else {
            // Retry a failed load when the user re-applies settings.
            speech_queue_->setActive(false);
            speech_queue_->setActive(true);
        }
    }

    void speak(const QString& translation) {
        // The 译文 is in the target language of the 翻译方向.
        const int voice = translation_settings_.direction == TranslationDirection::ZhToEn
            ? speech_settings_.voice_en
            : speech_settings_.voice_zh;
        speech_queue_->speak(translation, voice, speech_settings_.speed_percent / 100.0);
    }

    void showSpeechNotice(const QString& message) {
        if (state_ != State::Ready) {
            if (tray_icon_ != nullptr) {
                tray_icon_->showMessage(QStringLiteral("朗读"), message, QSystemTrayIcon::Warning, 3000);
            }
            return;
        }
        showTranslationNotice(QStringLiteral("朗读"), message);
    }

    void showTranslationNotice(const QString& text, const QString& notice) {
        if (notice.isEmpty() || state_ != State::Ready) return;
        showPopup();
        status_reset_timer_.stop();
        transcript_->setBubbleContent(text, notice, TranscriptBubble::SecondaryTone::Warning);
        updateWindowGeometry();
        const std::uint64_t session_id = session_generation_;
        QTimer::singleShot(2000, this, [this, session_id] {
            if (!shutting_down_.load(std::memory_order_acquire) && session_id == session_generation_) {
                hidePopup();
            }
        });
    }

    void setTransientStatus(const QString& text) {
        setBubbleStatus(text);
        status_reset_timer_.stop();
        status_reset_timer_.start(1500);
    }

    void setBubbleStatus(const QString& text) {
        transcript_->setBubbleText(text);
        updateWindowGeometry();
    }

    void refreshTranscript() {
        const QString display = currentTranscript();
        if (display.isEmpty()) {
            setBubbleStatus(state_ == State::Listening
                ? listeningHint()
                : idleHint());
            return;
        }
        // Partials stay monolingual; translations of finished sentences show beneath.
        const QString translated = translation_session_.has_value()
            ? to_qstring(translation_session_->translated_text())
            : QString{};
        transcript_->setBubbleContent(display, translated, TranscriptBubble::SecondaryTone::Muted);
        updateWindowGeometry();
    }

    QString currentTranscript() const {
        QString display = translation_session_.has_value()
            ? to_qstring(translation_session_->original_text())
            : committed_text_;
        if (!partial_text_.isEmpty()) {
            display += partial_text_;
        }
        return display;
    }

    void beginStableSessionGeometry() {
        if (!geometry_anchor_valid_) {
            geometry_anchor_center_ = x() + width() / 2;
            geometry_anchor_bottom_ = y() + height();
            geometry_anchor_valid_ = true;
        }
    }

    void updateWindowGeometry() {
        if (layout() != nullptr) layout()->activate();
        const QSize bubble_size = transcript_->size();
        const bool preserve_anchor = isVisible();
        if (preserve_anchor && !geometry_anchor_valid_) {
            geometry_anchor_center_ = x() + width() / 2;
            geometry_anchor_bottom_ = y() + height();
            geometry_anchor_valid_ = true;
        }
        const int horizontal_center_anchor = geometry_anchor_valid_
            ? geometry_anchor_center_
            : x() + width() / 2;
        const int bottom_anchor = geometry_anchor_valid_
            ? geometry_anchor_bottom_
            : y() + height();
        const int control_width = recording_control_ == nullptr ? 0 : recording_control_->width();
        const int new_width = std::max({bubble_size.width(), control_width, 74}) +
            window_margin * 2;
        const int control_height = recording_control_ == nullptr ? 48 : recording_control_->height();
        const int new_height = bubble_size.height() + control_height + control_spacing + window_margin * 2;
        int new_x = x();
        int new_y = y();
        if (preserve_anchor) {
            new_x = horizontal_center_anchor - new_width / 2;
            new_y = bottom_anchor - new_height;
            if (QScreen* current_screen = screen()) {
                const QRect area = current_screen->availableGeometry();
                new_y = new_height <= area.height()
                    ? std::clamp(new_y, area.top(), area.bottom() - new_height + 1)
                    : area.top();
            }
        }

        const QRect target_geometry(new_x, new_y, new_width, new_height);
        if (geometry() == target_geometry) {
            writeGeometrySnapshot();
            return;
        }
        // Partial results can arrive several times per second. Resizing through
        // a second animation lets stale targets race with newer text and causes
        // visible horizontal oscillation. Apply one anchored geometry atomically:
        // the center and bottom edge stay fixed while the content grows.
        setGeometry(target_geometry);
        if (layout() != nullptr) layout()->activate();
        writeGeometrySnapshot();
    }

    void writeGeometrySnapshot() {
        if (!geometry_log_.isOpen() || transcript_ == nullptr || recording_control_ == nullptr) return;
        if (layout() != nullptr) layout()->activate();
        const QRect bubble = transcript_->geometry();
        const QRect control = recording_control_->geometry();
        const double window_center = x() + width() / 2.0;
        const double bubble_center = x() + bubble.left() + bubble.width() / 2.0;
        const double control_center = x() + control.left() + control.width() / 2.0;
        QJsonObject snapshot;
        snapshot.insert(QStringLiteral("window_left"), x());
        snapshot.insert(QStringLiteral("window_top"), y());
        snapshot.insert(QStringLiteral("window_width"), width());
        snapshot.insert(QStringLiteral("window_height"), height());
        snapshot.insert(QStringLiteral("window_center_x"), window_center);
        snapshot.insert(QStringLiteral("window_bottom"), y() + height());
        snapshot.insert(QStringLiteral("bubble_left"), bubble.left());
        snapshot.insert(QStringLiteral("bubble_top"), bubble.top());
        snapshot.insert(QStringLiteral("bubble_width"), bubble.width());
        snapshot.insert(QStringLiteral("bubble_height"), bubble.height());
        snapshot.insert(QStringLiteral("bubble_center_x"), bubble_center);
        snapshot.insert(QStringLiteral("control_left"), control.left());
        snapshot.insert(QStringLiteral("control_top"), control.top());
        snapshot.insert(QStringLiteral("control_width"), control.width());
        snapshot.insert(QStringLiteral("control_height"), control.height());
        snapshot.insert(QStringLiteral("control_center_x"), control_center);
        snapshot.insert(QStringLiteral("center_delta"), bubble_center - control_center);
        snapshot.insert(QStringLiteral("window_bubble_delta"), bubble_center - window_center);
        geometry_log_.write(QJsonDocument(snapshot).toJson(QJsonDocument::Compact));
        geometry_log_.write("\n");
        geometry_log_.flush();
    }

    void updateMeter() {
        AudioLevelMetrics audio;
        VadResult vad;
        if (microphone_ != nullptr) audio = microphone_->level_metrics();
        if (recognizer_ != nullptr) vad = recognizer_->vad_telemetry();
        recording_control_->setTelemetry(audio.input_rms_db, vad.activity);
        if (state_ == State::Listening && session_elapsed_.isValid()) {
            recording_control_->setElapsedMilliseconds(session_elapsed_.elapsed());
        }

        if (state_ != State::Listening && state_ != State::Stopping) return;
        QString activity = QStringLiteral("静音");
        if (vad.activity == VadActivity::Candidate) activity = QStringLiteral("准备");
        else if (vad.activity == VadActivity::Speech) activity = QStringLiteral("语音");
        else if (vad.activity == VadActivity::EndpointWait) activity = QStringLiteral("等待句尾");
        if (audio.clipped_percent >= 0.1F) {
            recording_control_->setToolTip(QStringLiteral("输入削波 %1% · 请降低系统麦克风音量")
                                               .arg(audio.clipped_percent, 0, 'f', 1));
        } else {
            recording_control_->setToolTip(QStringLiteral("输入 %1 dBFS · VAD %2 · 门限 %3 dBFS")
                                               .arg(audio.input_rms_db, 0, 'f', 1)
                                               .arg(activity)
                                               .arg(vad.required_db, 0, 'f', 1));
        }
    }

    QSettings settings_;
    AppearanceSettings appearance_;
    ResultDestination result_destination_ = ResultDestination::Insert;
    ResultContent result_content_ = ResultContent::Original;
    ResultContent clipboard_content_ = ResultContent::Original;
    HotkeyTrigger hotkey_trigger_ = HotkeyTrigger::Hold;
    bool toggle_active_ = false;
    bool toggle_latch_ = false;
    QTimer toggle_limit_timer_;
    int paste_delay_ms_ = default_paste_delay_ms;
    VadSettings vad_settings_;
    TextMode text_mode_ = TextMode::Clean;
#ifdef _WIN32
    bool startup_enabled_ = false;
#endif
    QString hotkey_shortcut_ = QString::fromLatin1(default_hotkey);
    BubbleStyle bubble_style_ = BubbleStyle::Ring;
    bool preview_mode_ = false;
    TranscriptBubble* transcript_ = nullptr;
    RecordingControl* recording_control_ = nullptr;
    QSystemTrayIcon* tray_icon_ = nullptr;
    QMenu* history_menu_ = nullptr;
    QTimer meter_timer_;
    QTimer status_reset_timer_;
    QElapsedTimer session_elapsed_;
#ifdef _WIN32
    static constexpr int hotkey_primary_id = 0x5340;
    static constexpr int hotkey_left_id = 0x5341;
    static constexpr int hotkey_right_id = 0x5342;
    static constexpr UINT hotkey_state_message = WM_APP + 0x341;
    inline static HHOOK keyboard_hook_ = nullptr;
    inline static HWND hotkey_message_window_ = nullptr;
    QTimer hotkey_hold_timer_;
    QTimer hotkey_release_timer_;
    HotkeyBinding hotkey_binding_;
    WindowsTextInputTarget target_;
    WindowsTextInputTarget last_target_;
    bool hotkey_primary_registered_ = false;
    bool hotkey_left_registered_ = false;
    bool hotkey_right_registered_ = false;
    bool hotkey_pending_ = false;
    bool hotkey_recording_ = false;
#endif

    SenseVoiceEngine engine_;
    FsmnVadEngine vad_;
    TextProcessor text_processor_;
    SystemAudioMute playback_mute_;
    std::unique_ptr<StreamRecognizer> recognizer_;
    std::unique_ptr<MicrophoneCapture> microphone_;
    std::thread loader_;
    std::thread stopper_;
    std::atomic<bool> shutting_down_{false};
    State state_ = State::Loading;
    QString committed_text_;
    QString partial_text_;
    QPoint drag_offset_;
    bool dragging_ = false;
    int geometry_anchor_center_ = 0;
    int geometry_anchor_bottom_ = 0;
    bool geometry_anchor_valid_ = false;
    bool stop_should_commit_ = false;
    bool inject_on_complete_ = false;
    std::uint64_t session_generation_ = 0;
    QFile geometry_log_;

    struct PendingTranslation {
        std::uint64_t session = 0;
        std::size_t index = 0;
    };
    static constexpr int translation_timeout_ms = 5'000;
    TranslationSettings translation_settings_;
    SpeechSettings speech_settings_;
    SpeechQueue* speech_queue_ = nullptr;
    std::unique_ptr<TranslationWorker> translation_worker_;
    std::optional<TranslationDirection> translation_ready_direction_;
    bool translation_loading_ = false;
    std::optional<TranslationSession> translation_session_;
    std::unordered_map<std::uint64_t, PendingTranslation> translation_tickets_;
    std::uint64_t next_translation_ticket_ = 0;
    bool awaiting_translation_ = false;
    QTimer translation_deadline_;
    QAction* tray_translation_action_ = nullptr;
    QAction* tray_zh_en_action_ = nullptr;
    QAction* tray_en_zh_action_ = nullptr;
};

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("SenseVoice 语音输入"));
    application.setOrganizationName(QStringLiteral("SenseVoice"));
    application.setWindowIcon(sensevoiceIcon());

    const QStringList arguments = application.arguments();
#ifdef _WIN32
    const QString raw_command_line = QString::fromWCharArray(GetCommandLineW());
#else
    const QString raw_command_line;
#endif
    const bool preview_mode = arguments.contains(QStringLiteral("--preview")) ||
        raw_command_line.contains(QStringLiteral("--preview"), Qt::CaseInsensitive) ||
        qEnvironmentVariableIntValue("SENSEVOICE_PREVIEW") == 1;
    const bool preview_all = arguments.contains(QStringLiteral("--preview-all")) ||
        raw_command_line.contains(QStringLiteral("--preview-all"), Qt::CaseInsensitive);
    const bool preview_active = arguments.contains(QStringLiteral("--preview-active")) ||
        raw_command_line.contains(QStringLiteral("--preview-active"), Qt::CaseInsensitive);
    const bool preview_geometry_test = arguments.contains(QStringLiteral("--preview-geometry-test")) ||
        raw_command_line.contains(QStringLiteral("--preview-geometry-test"), Qt::CaseInsensitive);
    if (preview_mode || preview_all) {
        application.setQuitOnLastWindowClosed(false);
    }
    BubbleStyle preview_style = BubbleStyle::Ring;
    QString preview_image_path;
    QString preview_settings_dir;
    QString preview_geometry_log_path;
    QString preview_translation;
    QString preview_text = QStringLiteral(
        "这一句用于比较浮窗方案的文字布局。说长一点时，气泡会自动扩展，不滚动，也不会裁切内容。\n"
        "第二段会保留在同一个浮窗中，方便观察长内容的宽高变化。\n"
        "按住 Ctrl + Win 开始录音，松开后会注入到上一次定位的光标位置。");
    for (int index = 1; index < arguments.size(); ++index) {
        if (arguments[index] == QStringLiteral("--bubble-style") && index + 1 < arguments.size()) {
            preview_style = bubbleStyleFromName(arguments[++index]);
        } else if (arguments[index] == QStringLiteral("--preview-settings") && index + 1 < arguments.size()) {
            preview_settings_dir = arguments[++index];
        } else if (arguments[index] == QStringLiteral("--preview-image") && index + 1 < arguments.size()) {
            preview_image_path = arguments[++index];
        } else if (arguments[index] == QStringLiteral("--preview-geometry-log") && index + 1 < arguments.size()) {
            preview_geometry_log_path = arguments[++index];
        } else if (arguments[index] == QStringLiteral("--preview-text") && index + 1 < arguments.size()) {
            preview_text = arguments[++index];
        } else if (arguments[index] == QStringLiteral("--preview-translation") && index + 1 < arguments.size()) {
            preview_translation = arguments[++index];
        }
    }
    if (preview_style == BubbleStyle::Panel) {
        if (raw_command_line.contains(QStringLiteral("capsule"), Qt::CaseInsensitive)) {
            preview_style = BubbleStyle::Capsule;
        } else if (raw_command_line.contains(QStringLiteral("ring"), Qt::CaseInsensitive)) {
            preview_style = BubbleStyle::Ring;
        }
    }
#ifdef _WIN32
    HANDLE instance_mutex = nullptr;
    if (!preview_mode && !preview_all) {
        instance_mutex = CreateMutexW(nullptr, TRUE, L"Local\\SenseVoiceLocalDictation");
        if (instance_mutex == nullptr || GetLastError() == ERROR_ALREADY_EXISTS) {
            HWND existing_window = FindWindowW(nullptr, L"SenseVoice 语音输入");
            if (existing_window != nullptr) {
                ShowWindow(existing_window, SW_SHOWNOACTIVATE);
                SetWindowPos(existing_window, HWND_TOPMOST, 0, 0, 0, 0,
                             SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            MessageBoxW(nullptr, L"SenseVoice 语音输入已经在运行。请使用已有悬浮窗，或先退出旧实例。",
                         L"SenseVoice", MB_OK | MB_ICONINFORMATION);
            if (instance_mutex != nullptr) CloseHandle(instance_mutex);
            return 0;
        }
    }
#endif

    application.setFont(QFont(QStringLiteral("Microsoft YaHei UI"), 10));
    application.setStyleSheet(QStringLiteral(
        "QToolTip { color: #FFFFFF; background: #30343A; border: none; padding: 5px; }"
        "QMenu { background: white; color: #25292E; border: 1px solid #DFE2E5; padding: 5px; }"
        "QMenu::item { min-width: 130px; padding: 7px 18px; border-radius: 4px; }"
        "QMenu::item:selected { background: #EFF8F5; color: #168E68; }"
        "QMenu::item:disabled { color: #B6BBC1; }"));

    std::vector<std::unique_ptr<VoiceInputWindow>> preview_windows;
    std::unique_ptr<VoiceInputWindow> single_window;
    if (preview_all) {
        const std::array<BubbleStyle, 3> styles = {
            BubbleStyle::Capsule, BubbleStyle::Panel, BubbleStyle::Ring};
        QScreen* screen = QGuiApplication::primaryScreen();
        const QRect area = screen == nullptr ? QRect(0, 0, 1440, 900) : screen->availableGeometry();
        int left = area.left() + 36;
        for (const BubbleStyle style : styles) {
            auto window = std::make_unique<VoiceInputWindow>(style, true);
            window->setPreviewContent(preview_text);
            window->show();
            window->move(left, area.bottom() - window->height() - 64);
            window->raise();
            left += window->width() + 24;
            preview_windows.push_back(std::move(window));
        }
    } else {
        single_window = std::make_unique<VoiceInputWindow>(preview_style, preview_mode);
        if (preview_mode) {
            single_window->setPreviewContent(preview_text);
            if (!preview_translation.isEmpty()) {
                single_window->setPreviewTranslation(preview_text, preview_translation);
            }
            if (preview_active) single_window->setPreviewSignal();
            single_window->show();
            single_window->raise();
            single_window->setGeometryLogPath(preview_geometry_log_path);
            if (preview_geometry_test) single_window->startPreviewGeometryTest();
        } else {
            single_window->hideUntilInput();
        }
    }

    if (preview_mode && !preview_settings_dir.isEmpty()) {
        InputSettingsDialog dialog(VadSettings{}, QString::fromLatin1(default_hotkey), TextMode::Clean,
                                   HotkeyTrigger::Hold, ResultDestination::Insert, ResultContent::Both,
                                   ResultContent::Original,
                                   default_paste_delay_ms, false, {}, TranslationSettings{},
                                   SpeechSettings{}, AppearanceSettings{}, BubbleStyle::Panel, {});
        dialog.show();
        auto* tabs = dialog.findChild<QTabWidget*>();
        for (int tab = 0; tabs != nullptr && tab < tabs->count(); ++tab) {
            tabs->setCurrentIndex(tab);
            application.processEvents();
            dialog.grab().save(QDir(preview_settings_dir).filePath(QStringLiteral("settings-%1.png").arg(tab)));
        }
        return 0;
    }

    if (preview_mode && !preview_image_path.isEmpty() && single_window != nullptr) {
        application.processEvents();
        const QSize snapshot_size = single_window->size().expandedTo(QSize(1, 1));
        QImage snapshot(snapshot_size, QImage::Format_ARGB32_Premultiplied);
        snapshot.fill(Qt::transparent);
        single_window->render(&snapshot);
        snapshot.save(preview_image_path);
        return 0;
    }

    const int exit_code = application.exec();
#ifdef _WIN32
    if (instance_mutex != nullptr) {
        ReleaseMutex(instance_mutex);
        CloseHandle(instance_mutex);
    }
#endif
    return exit_code;
}
