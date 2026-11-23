#pragma once

#include "compute_policy.h"
#include "speech_player.h"
#include "text_processor.h"
#include "translation_session.h"

#include <QColor>
#include <QHash>
#include <QList>
#include <QPair>
#include <QString>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class QSettings;

namespace sa::settings {

enum class BubbleStyle {
    Capsule,
    Panel,
    Ring,
};

enum class TextMode {
    Raw,
    Clean,
};

enum class ResultDestination { Insert, CopyOnly };
enum class ResultContent { Original, Translation, Both };
enum class HotkeyTrigger { Hold, Toggle, Realtime };
enum class ThemePreset { Default, Light, Dark, HighContrast, Custom };

#ifdef __APPLE__
inline constexpr int default_endpoint_ms = 1200;
inline constexpr auto default_hotkey = "Meta+Alt+Space";
#else
inline constexpr int default_endpoint_ms = 700;
inline constexpr auto default_hotkey = "Ctrl+Alt+Space";
#endif
inline constexpr auto control_windows_hotkey = "Ctrl+Win";
inline constexpr auto default_translation_separator = " / ";
inline constexpr int default_speech_voice_en = 0;
inline constexpr int default_speech_voice_zh = 3;
inline constexpr int default_paste_delay_ms = 150;
inline constexpr int maximum_paste_delay_ms = 2000;
inline constexpr int linger_maximum_ms = 10'000;
inline constexpr int linger_step_ms = 500;

[[nodiscard]] QString resultContentName(ResultContent content);
[[nodiscard]] ResultContent resultContentFromName(const QString& name);
[[nodiscard]] QString bubbleStyleName(BubbleStyle style);
[[nodiscard]] BubbleStyle bubbleStyleFromName(const QString& value);
[[nodiscard]] QString themePresetName(ThemePreset preset);
[[nodiscard]] ThemePreset themePresetFromName(const QString& name);
[[nodiscard]] QString sanitizedSeparator(QString separator);
[[nodiscard]] QString canonicalShortcut(const QString& value);
[[nodiscard]] bool hasUsableShortcutKey(const QString& shortcut);
[[nodiscard]] QString shortcutDisplayName(const QString& shortcut);
[[nodiscard]] QList<QPair<int, QString>> kokoroChineseVoices();

struct VadSettings {
    int endpoint_ms = default_endpoint_ms;
    int threshold_percent = 55;
    int minimum_db = -60;
    int snr_db = 3;
};

struct TranslationSettings {
    bool enabled = false;
    TranslationDirection direction = TranslationDirection::ZhToEn;
    ComputePolicy device = ComputePolicy::Auto;
    QString separator = QString::fromLatin1(default_translation_separator);
};

struct SpeechSettings {
    bool enabled = false;
    QString device_id;
    bool monitor = false;
    int voice_zh = default_speech_voice_zh;
    int voice_en = default_speech_voice_en;
    int speed_percent = 100;
    bool live = false;
};

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

// Which model to use for each job. "auto" picks the best installed one.
struct ModelChoices {
    QHash<QString, QString> recognizer; // language -> model id
    QHash<QString, QString> voice;      // language -> model id
    QString translator = QStringLiteral("auto");
    QString custom_directory;
    QHash<QString, QString> custom_paths; // model id -> file or folder
};

struct SettingsEnvironment {
    GpuCapability gpu;
    std::vector<AudioOutputDevice> audio_outputs;
};

struct SettingsSnapshot {
    VadSettings vad;
    ComputePolicy compute = ComputePolicy::Auto;
    QString shortcut = QString::fromLatin1(default_hotkey);
    TextMode mode = TextMode::Clean;
    HotkeyTrigger trigger = HotkeyTrigger::Hold;
    ResultDestination destination = ResultDestination::Insert;
    ResultContent popup_content = ResultContent::Original;
    ResultContent clipboard_content = ResultContent::Original;
    int paste_delay_ms = default_paste_delay_ms;
    int clipboard_ttl_seconds = 0;
    bool startup_enabled = false;
    bool mute_while_recording = false;
    std::vector<HotwordEntry> hotwords;
    TranslationSettings translation;
    SpeechSettings speech;
    AppearanceSettings appearance;
    BubbleStyle bubble_style = BubbleStyle::Ring;
    ModelChoices model_choices;
};

// Apply-bound fields; appearance is excluded because it commits immediately.
struct SettingsDraft {
    VadSettings vad;
    ComputePolicy compute = ComputePolicy::Auto;
    QString shortcut = QString::fromLatin1(default_hotkey);
    TextMode mode = TextMode::Clean;
    HotkeyTrigger trigger = HotkeyTrigger::Hold;
    ResultDestination destination = ResultDestination::Insert;
    ResultContent popup_content = ResultContent::Original;
    ResultContent clipboard_content = ResultContent::Original;
    int paste_delay_ms = default_paste_delay_ms;
    int clipboard_ttl_seconds = 0;
    bool startup_enabled = false;
    bool mute_while_recording = false;
    std::vector<HotwordEntry> hotwords;
    TranslationSettings translation;
    SpeechSettings speech;
    ModelChoices model_choices;
};

struct SettingsCallbacks {
    std::function<void(const AppearanceSettings&)> appearance_changed;
    std::function<void()> restore_model_locations;
};

struct SettingsLoadInputs {
    const QSettings& settings;
    const std::filesystem::path& hotwords_path;
    bool startup_enabled = false;
    BubbleStyle bubble_style = BubbleStyle::Ring;
};

enum class SettingsMigration {
    CopyBoth,
    MissingContent,
    VadProfile,
};

struct SettingsLoadResult {
    SettingsSnapshot snapshot;
    std::vector<SettingsMigration> migrations;
    std::string error;
};

[[nodiscard]] SettingsSnapshot defaultSettingsSnapshot();
[[nodiscard]] SettingsDraft defaultSettingsDraft();
[[nodiscard]] SettingsLoadResult loadSettingsSnapshot(const SettingsLoadInputs& inputs);
// Production path only; tests call it against a temporary INI file.
void migrateSettings(QSettings& settings, const SettingsLoadResult& result);

} // namespace sa::settings
