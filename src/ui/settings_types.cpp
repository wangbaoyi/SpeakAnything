#include "ui/settings_types.h"
#include "ui/ui_strings.h"

#include <QKeySequence>
#include <QSettings>

#include <algorithm>

namespace sa::settings {
namespace {

QString policyCode(ComputePolicy policy) {
    const std::string_view code = compute_policy_code(policy);
    return QString::fromLatin1(code.data(), static_cast<int>(code.size()));
}

} // namespace

QString resultContentName(ResultContent content) {
    switch (content) {
    case ResultContent::Original: return QStringLiteral("original");
    case ResultContent::Translation: return QStringLiteral("translation");
    case ResultContent::Both: return QStringLiteral("both");
    }
    return QStringLiteral("original");
}

ResultContent resultContentFromName(const QString& name) {
    if (name == QStringLiteral("translation")) return ResultContent::Translation;
    if (name == QStringLiteral("both")) return ResultContent::Both;
    return ResultContent::Original;
}

QString bubbleStyleName(BubbleStyle style) {
    switch (style) {
    case BubbleStyle::Capsule: return QStringLiteral("capsule");
    case BubbleStyle::Panel: return QStringLiteral("panel");
    case BubbleStyle::Ring: return QStringLiteral("ring");
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
    for (const ThemePreset preset : {ThemePreset::Light, ThemePreset::Dark,
                                     ThemePreset::HighContrast, ThemePreset::Custom}) {
        if (name == themePresetName(preset)) return preset;
    }
    return ThemePreset::Default;
}

QString sanitizedSeparator(QString separator) {
    separator.remove(QLatin1Char('\r'));
    separator.remove(QLatin1Char('\n'));
    if (separator.trimmed().isEmpty()) return QString::fromLatin1(default_translation_separator);
    return separator;
}

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
#ifdef _WIN32
    if (shortcut == QString::fromLatin1(control_windows_hotkey)) return true;
#else
    if (shortcut == QString::fromLatin1(control_windows_hotkey)) return false;
#endif

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
        voices.append({id++, sa::ui::Ui::tr("女声 %1").arg(number, 3, 10, QLatin1Char('0'))});
    }
    for (const int number : male) {
        voices.append({id++, sa::ui::Ui::tr("男声 %1").arg(number, 3, 10, QLatin1Char('0'))});
    }
    return voices;
}

SettingsSnapshot defaultSettingsSnapshot() {
    return {};
}

SettingsDraft defaultSettingsDraft() {
    const SettingsSnapshot snapshot;
    SettingsDraft draft;
    draft.vad = snapshot.vad;
    draft.compute = snapshot.compute;
    draft.shortcut = snapshot.shortcut;
    draft.mode = snapshot.mode;
    draft.trigger = snapshot.trigger;
    draft.destination = snapshot.destination;
    draft.popup_content = snapshot.popup_content;
    draft.clipboard_content = snapshot.clipboard_content;
    draft.paste_delay_ms = snapshot.paste_delay_ms;
    draft.clipboard_ttl_seconds = snapshot.clipboard_ttl_seconds;
    draft.startup_enabled = snapshot.startup_enabled;
    draft.mute_while_recording = snapshot.mute_while_recording;
    draft.hotwords = snapshot.hotwords;
    draft.translation = snapshot.translation;
    draft.speech = snapshot.speech;
    draft.model_choices = snapshot.model_choices;
    return draft;
}

SettingsLoadResult loadSettingsSnapshot(const SettingsLoadInputs& inputs) {
    const QSettings& settings = inputs.settings;
    SettingsLoadResult result;
    SettingsSnapshot& snapshot = result.snapshot;

    snapshot.vad.endpoint_ms =
        settings.value(QStringLiteral("vad/endpoint_ms"), default_endpoint_ms).toInt();
    snapshot.vad.threshold_percent =
        settings.value(QStringLiteral("vad/threshold_percent"), 55).toInt();
    snapshot.vad.minimum_db = settings.value(QStringLiteral("vad/minimum_db"), -60).toInt();
    snapshot.vad.snr_db = settings.value(QStringLiteral("vad/snr_db"), 3).toInt();

    const int profile_version =
        settings.value(QStringLiteral("vad/profile_version"), 0).toInt();
    if (profile_version < 4 && snapshot.vad.endpoint_ms == 700 &&
        snapshot.vad.threshold_percent == 80 && snapshot.vad.minimum_db == -45 &&
        snapshot.vad.snr_db == 8) {
        snapshot.vad = VadSettings{};
    }
    if (profile_version < 5 && snapshot.vad.endpoint_ms == 700 && default_endpoint_ms != 700) {
        snapshot.vad.endpoint_ms = default_endpoint_ms;
    }
    if (profile_version < 5) {
        result.migrations.push_back(SettingsMigration::VadProfile);
    }

    snapshot.compute = parse_compute_policy(
        settings.value(QString::fromLatin1(compute_policy_settings_key), QStringLiteral("auto"))
            .toString()
            .toStdString())
                           .value_or(ComputePolicy::Auto);
    snapshot.mode = settings.value(QStringLiteral("text/mode"), 1).toInt() == 0
        ? TextMode::Raw
        : TextMode::Clean;

    snapshot.translation.enabled =
        settings.value(QStringLiteral("translation/enabled"), false).toBool();
    const QString direction =
        settings.value(QStringLiteral("translation/direction"), QStringLiteral("zh-en")).toString();
    snapshot.translation.direction =
        parse_direction_code(direction.toStdString()).value_or(TranslationDirection::ZhToEn);
    snapshot.translation.device = parse_compute_policy(
        settings
            .value(QString::fromLatin1(translation_compute_policy_settings_key),
                   QStringLiteral("auto"))
            .toString()
            .toStdString())
                                      .value_or(ComputePolicy::Auto);
    snapshot.translation.separator = sanitizedSeparator(
        settings
            .value(QStringLiteral("translation/separator"),
                   QString::fromLatin1(default_translation_separator))
            .toString());

    const QString destination_value =
        settings.value(QStringLiteral("output/destination"), QStringLiteral("insert")).toString();
    snapshot.destination = destination_value == QStringLiteral("insert")
        ? ResultDestination::Insert
        : ResultDestination::CopyOnly;
    ResultContent legacy_content =
        snapshot.translation.enabled ? ResultContent::Both : ResultContent::Original;
    if (destination_value == QStringLiteral("copy-translation")) {
        legacy_content = ResultContent::Translation;
    } else if (destination_value == QStringLiteral("copy-both")) {
        legacy_content = ResultContent::Both;
    } else if (destination_value == QStringLiteral("copy-original")) {
        legacy_content = ResultContent::Original;
    }
    const bool legacy_destination = destination_value == QStringLiteral("copy-translation") ||
        destination_value == QStringLiteral("copy-both") ||
        destination_value == QStringLiteral("copy-original");
    if (legacy_destination) {
        result.migrations.push_back(SettingsMigration::CopyBoth);
    }
    const bool has_content = settings.contains(QStringLiteral("output/content"));
    if (!has_content) {
        result.migrations.push_back(SettingsMigration::MissingContent);
    }
    snapshot.popup_content = has_content
        ? resultContentFromName(settings.value(QStringLiteral("output/content")).toString())
        : legacy_content;
    snapshot.clipboard_content = settings.contains(QStringLiteral("output/clipboard_content"))
        ? resultContentFromName(settings.value(QStringLiteral("output/clipboard_content")).toString())
        : snapshot.popup_content;

    const QString trigger_value = settings.value(QStringLiteral("hotkey/trigger")).toString();
    snapshot.trigger = trigger_value == QStringLiteral("realtime") ? HotkeyTrigger::Realtime
        : trigger_value == QStringLiteral("toggle")              ? HotkeyTrigger::Toggle
                                                                 : HotkeyTrigger::Hold;
    snapshot.paste_delay_ms = std::clamp(
        settings.value(QStringLiteral("output/paste_delay_ms"), default_paste_delay_ms).toInt(),
        0, maximum_paste_delay_ms);
    snapshot.clipboard_ttl_seconds = std::clamp(
        settings.value(QStringLiteral("output/clipboard_ttl_seconds"), 0).toInt(), 0, 600);

    const QString shortcut = canonicalShortcut(
        settings.value(QStringLiteral("hotkey/shortcut"), QString::fromLatin1(default_hotkey))
            .toString());
    snapshot.shortcut = hasUsableShortcutKey(shortcut) ? shortcut
                                                       : QString::fromLatin1(default_hotkey);

    snapshot.speech.enabled = settings.value(QStringLiteral("speech/enabled"), false).toBool();
    snapshot.speech.device_id = settings.value(QStringLiteral("speech/device_id")).toString();
    snapshot.speech.monitor = settings.value(QStringLiteral("speech/monitor"), false).toBool();
    snapshot.speech.voice_zh =
        settings.value(QStringLiteral("speech/voice_zh"), default_speech_voice_zh).toInt();
    snapshot.speech.voice_en =
        settings.value(QStringLiteral("speech/voice_en"), default_speech_voice_en).toInt();
    snapshot.speech.speed_percent = std::clamp(
        settings.value(QStringLiteral("speech/speed_percent"), 100).toInt(), 50, 200);
    snapshot.speech.live = settings.value(QStringLiteral("speech/live"), false).toBool();
    snapshot.mute_while_recording =
        settings.value(QStringLiteral("audio/mute_while_recording"), false).toBool();

    snapshot.startup_enabled = inputs.startup_enabled;
    snapshot.bubble_style = inputs.bubble_style;

    const AppearanceSettings appearance_defaults;
    snapshot.appearance.preset = themePresetFromName(
        settings
            .value(QStringLiteral("appearance/preset"),
                   themePresetName(appearance_defaults.preset))
            .toString());
    const auto load_color = [&settings](const QString& key, const QColor& fallback) {
        const QColor color(settings.value(key, fallback.name()).toString());
        return color.isValid() ? color : fallback;
    };
    snapshot.appearance.custom.background = load_color(
        QStringLiteral("appearance/custom_background"), appearance_defaults.custom.background);
    snapshot.appearance.custom.text =
        load_color(QStringLiteral("appearance/custom_text"), appearance_defaults.custom.text);
    snapshot.appearance.custom.accent =
        load_color(QStringLiteral("appearance/custom_accent"), appearance_defaults.custom.accent);
    snapshot.appearance.custom_font_family =
        settings.value(QStringLiteral("appearance/custom_font_family")).toString();
    snapshot.appearance.custom_font_size = std::clamp(
        settings
            .value(QStringLiteral("appearance/custom_font_size"),
                   appearance_defaults.custom_font_size)
            .toInt(),
        8, 24);
    const int linger_ms =
        settings.value(QStringLiteral("appearance/linger_ms"), appearance_defaults.linger_ms).toInt();
    snapshot.appearance.linger_ms =
        std::clamp(linger_ms / linger_step_ms * linger_step_ms, 0, linger_maximum_ms);
    snapshot.appearance.pinned =
        settings.value(QStringLiteral("appearance/pinned"), appearance_defaults.pinned).toBool();

    for (const QString& key : settings.allKeys()) {
        if (key.startsWith(QStringLiteral("models/recognizer/"))) {
            snapshot.model_choices.recognizer.insert(
                key.mid(QStringLiteral("models/recognizer/").size()),
                settings.value(key).toString());
        } else if (key.startsWith(QStringLiteral("models/voice/"))) {
            snapshot.model_choices.voice.insert(
                key.mid(QStringLiteral("models/voice/").size()), settings.value(key).toString());
        } else if (key.startsWith(QStringLiteral("models/custom_paths/"))) {
            const QString path = settings.value(key).toString();
            if (!path.isEmpty()) {
                snapshot.model_choices.custom_paths.insert(
                    key.mid(QStringLiteral("models/custom_paths/").size()), path);
            }
        }
    }
    snapshot.model_choices.translator =
        settings.value(QStringLiteral("models/translator"), QStringLiteral("auto")).toString();
    snapshot.model_choices.custom_directory =
        settings.value(QStringLiteral("models/custom_directory")).toString();

    if (!inputs.hotwords_path.empty() && std::filesystem::exists(inputs.hotwords_path)) {
        TextProcessor processor;
        std::string error;
        if (!processor.load_hotwords(inputs.hotwords_path, error)) {
            result.error = error;
        } else {
            snapshot.hotwords = processor.hotwords();
        }
    }

    return result;
}

void migrateSettings(QSettings& settings, const SettingsLoadResult& result) {
    if (result.migrations.empty()) return;

    const auto has = [&result](SettingsMigration migration) {
        return std::find(result.migrations.begin(), result.migrations.end(), migration) !=
            result.migrations.end();
    };

    if (has(SettingsMigration::CopyBoth) || has(SettingsMigration::MissingContent)) {
        settings.setValue(
            QStringLiteral("output/destination"),
            result.snapshot.destination == ResultDestination::Insert ? QStringLiteral("insert")
                                                                     : QStringLiteral("copy"));
        settings.setValue(QStringLiteral("output/content"),
                          resultContentName(result.snapshot.popup_content));
        settings.setValue(QStringLiteral("output/clipboard_content"),
                          resultContentName(result.snapshot.clipboard_content));
    }
    if (has(SettingsMigration::VadProfile)) {
        settings.setValue(QStringLiteral("vad/endpoint_ms"), result.snapshot.vad.endpoint_ms);
        settings.setValue(QStringLiteral("vad/threshold_percent"),
                          result.snapshot.vad.threshold_percent);
        settings.setValue(QStringLiteral("vad/minimum_db"), result.snapshot.vad.minimum_db);
        settings.setValue(QStringLiteral("vad/snr_db"), result.snapshot.vad.snr_db);
        settings.setValue(QStringLiteral("vad/profile_version"), 5);
    }
    settings.sync();
}

} // namespace sa::settings
