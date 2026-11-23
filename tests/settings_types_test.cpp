#include "ui/settings_types.h"

#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <fstream>
#include <string>

using namespace sa::settings;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool hasMigration(const SettingsLoadResult& result, SettingsMigration migration) {
    for (const SettingsMigration candidate : result.migrations) {
        if (candidate == migration) return true;
    }
    return false;
}

void testDefaults() {
    const SettingsSnapshot snapshot;
    check(snapshot.mode == TextMode::Clean, "default text mode is clean");
    check(snapshot.shortcut == QString::fromLatin1(default_hotkey), "default shortcut");
    check(snapshot.trigger == HotkeyTrigger::Hold, "default trigger is hold");
    check(snapshot.destination == ResultDestination::Insert, "default destination");
    check(snapshot.popup_content == ResultContent::Original, "default content");
    check(snapshot.paste_delay_ms == 150, "default paste delay");
    check(snapshot.clipboard_ttl_seconds == 0, "default clipboard ttl");
    check(snapshot.translation.separator == QStringLiteral(" / "), "default separator");
    check(snapshot.speech.speed_percent == 100, "default speech speed");
    check(snapshot.bubble_style == BubbleStyle::Ring, "default bubble style");
    check(snapshot.appearance.linger_ms == 500, "default linger");

    const SettingsDraft draft = defaultSettingsDraft();
    check(draft.mode == snapshot.mode, "draft mode matches snapshot");
    check(draft.translation.device == ComputePolicy::Auto, "draft translation device");
}

void testCodecs() {
    for (const ResultContent content :
         {ResultContent::Original, ResultContent::Translation, ResultContent::Both}) {
        check(resultContentFromName(resultContentName(content)) == content,
              "result content roundtrip");
    }
    check(bubbleStyleFromName(QStringLiteral("capsule")) == BubbleStyle::Capsule, "capsule name");
    check(bubbleStyleFromName(QStringLiteral("pill")) == BubbleStyle::Capsule, "pill alias");
    check(bubbleStyleFromName(QStringLiteral("ring")) == BubbleStyle::Ring, "ring name");
    check(bubbleStyleFromName(QStringLiteral("circle")) == BubbleStyle::Ring, "circle alias");
    check(bubbleStyleFromName(QStringLiteral("other")) == BubbleStyle::Panel, "panel fallback");
    for (const ThemePreset preset : {ThemePreset::Default, ThemePreset::Light, ThemePreset::Dark,
                                     ThemePreset::HighContrast, ThemePreset::Custom}) {
        check(themePresetFromName(themePresetName(preset)) == preset, "theme preset roundtrip");
    }
    check(sanitizedSeparator(QStringLiteral("a\nb\r")) == QStringLiteral("ab"),
          "separator drops line breaks");
    check(sanitizedSeparator(QStringLiteral("   ")) == QStringLiteral(" / "),
          "blank separator falls back");
    check(canonicalShortcut(QStringLiteral("Ctrl+Alt+Space")) ==
              QStringLiteral("Ctrl+Alt+Space"),
          "shortcut canonical text");
    check(canonicalShortcut(QStringLiteral("Ctrl+Win")) == QStringLiteral("Ctrl+Win"),
          "Ctrl+Win shortcut");
    check(canonicalShortcut(QString()).isEmpty(), "empty shortcut is rejected");
    check(!hasUsableShortcutKey(QStringLiteral("Ctrl+Shift")), "modifier-only shortcut rejected");
    check(hasUsableShortcutKey(QStringLiteral("F8")), "F8 shortcut is usable");
}

void testLoader(QTemporaryDir& dir) {
    const QString settings_path = dir.filePath(QStringLiteral("settings.ini"));
    {
        QSettings settings(settings_path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("compute/device"), QStringLiteral("gpu"));
        settings.setValue(QStringLiteral("vad/endpoint_ms"), 900);
        settings.setValue(QStringLiteral("vad/threshold_percent"), 60);
        settings.setValue(QStringLiteral("vad/minimum_db"), -55);
        settings.setValue(QStringLiteral("vad/snr_db"), 4);
        settings.setValue(QStringLiteral("vad/profile_version"), 5);
        settings.setValue(QStringLiteral("text/mode"), 0);
        settings.setValue(QStringLiteral("translation/enabled"), true);
        settings.setValue(QStringLiteral("translation/direction"), QStringLiteral("en-zh"));
        settings.setValue(QStringLiteral("translation/device"), QStringLiteral("cpu"));
        settings.setValue(QStringLiteral("translation/separator"), QStringLiteral(" | "));
        settings.setValue(QStringLiteral("output/destination"), QStringLiteral("copy"));
        settings.setValue(QStringLiteral("output/content"), QStringLiteral("both"));
        settings.setValue(QStringLiteral("output/clipboard_content"),
                          QStringLiteral("translation"));
        settings.setValue(QStringLiteral("output/paste_delay_ms"), 250);
        settings.setValue(QStringLiteral("output/clipboard_ttl_seconds"), 45);
        settings.setValue(QStringLiteral("hotkey/shortcut"), QStringLiteral("F8"));
        settings.setValue(QStringLiteral("hotkey/trigger"), QStringLiteral("toggle"));
        settings.setValue(QStringLiteral("speech/enabled"), true);
        settings.setValue(QStringLiteral("speech/device_id"), QStringLiteral("device-1"));
        settings.setValue(QStringLiteral("speech/monitor"), true);
        settings.setValue(QStringLiteral("speech/voice_zh"), 5);
        settings.setValue(QStringLiteral("speech/voice_en"), 2);
        settings.setValue(QStringLiteral("speech/speed_percent"), 120);
        settings.setValue(QStringLiteral("speech/live"), true);
        settings.setValue(QStringLiteral("audio/mute_while_recording"), true);
        settings.setValue(QStringLiteral("appearance/preset"), QStringLiteral("dark"));
        settings.setValue(QStringLiteral("appearance/custom_background"),
                          QStringLiteral("#101010"));
        settings.setValue(QStringLiteral("appearance/custom_text"), QStringLiteral("#f0f0f0"));
        settings.setValue(QStringLiteral("appearance/custom_accent"), QStringLiteral("#00ff00"));
        settings.setValue(QStringLiteral("appearance/custom_font_family"), QStringLiteral("Test"));
        settings.setValue(QStringLiteral("appearance/custom_font_size"), 12);
        settings.setValue(QStringLiteral("appearance/linger_ms"), 1500);
        settings.setValue(QStringLiteral("appearance/pinned"), true);
        settings.setValue(QStringLiteral("models/recognizer/zh"), QStringLiteral("sensevoice"));
        settings.setValue(QStringLiteral("models/translator"), QStringLiteral("qwen"));
        settings.setValue(QStringLiteral("models/custom_directory"), QStringLiteral("D:/models"));
        settings.setValue(QStringLiteral("models/custom_paths/custom"), QStringLiteral("D:/m.bin"));
        settings.sync();
    }

    const std::filesystem::path hotwords = dir.filePath(QStringLiteral("hotwords.tsv")).toStdString();
    {
        std::ofstream output(hotwords);
        output << "你好\t你 好|您好\ttrue\t7\t4.5\ttrue\n";
        output << "OpenAI\t\tfalse\t0\t3.0\tfalse\n";
    }

    QSettings settings(settings_path, QSettings::IniFormat);
    SettingsLoadInputs inputs{settings, hotwords, true, BubbleStyle::Capsule};
    const SettingsLoadResult result = loadSettingsSnapshot(inputs);
    check(result.error.empty(), "loader has no error");
    check(result.migrations.empty(), "current schema has no migrations");

    const SettingsSnapshot& snapshot = result.snapshot;
    check(snapshot.vad.endpoint_ms == 900, "loader reads endpoint");
    check(snapshot.vad.threshold_percent == 60, "loader reads threshold");
    check(snapshot.vad.minimum_db == -55, "loader reads minimum db");
    check(snapshot.vad.snr_db == 4, "loader reads snr");
    check(snapshot.compute == ComputePolicy::Gpu, "loader reads compute policy");
    check(snapshot.mode == TextMode::Raw, "loader reads raw mode");
    check(snapshot.translation.enabled, "loader reads translation enabled");
    check(snapshot.translation.direction.source == "en", "loader reads direction");
    check(snapshot.translation.direction.target == "zh", "loader reads direction target");
    check(snapshot.translation.device == ComputePolicy::Cpu, "loader reads translation device");
    check(snapshot.translation.separator == QStringLiteral(" | "), "loader reads separator");
    check(snapshot.destination == ResultDestination::CopyOnly, "loader reads destination");
    check(snapshot.popup_content == ResultContent::Both, "loader reads popup content");
    check(snapshot.clipboard_content == ResultContent::Translation,
          "loader reads clipboard content");
    check(snapshot.paste_delay_ms == 250, "loader reads paste delay");
    check(snapshot.clipboard_ttl_seconds == 45, "loader reads clipboard ttl");
    check(snapshot.shortcut == QStringLiteral("F8"), "loader reads shortcut");
    check(snapshot.trigger == HotkeyTrigger::Toggle, "loader reads trigger");
    check(snapshot.speech.enabled, "loader reads speech enabled");
    check(snapshot.speech.device_id == QStringLiteral("device-1"), "loader reads speech device");
    check(snapshot.speech.monitor, "loader reads speech monitor");
    check(snapshot.speech.voice_zh == 5, "loader reads voice zh");
    check(snapshot.speech.voice_en == 2, "loader reads voice en");
    check(snapshot.speech.speed_percent == 120, "loader reads speech speed");
    check(snapshot.speech.live, "loader reads live speech");
    check(snapshot.mute_while_recording, "loader reads mute");
    check(snapshot.startup_enabled, "loader uses startup input");
    check(snapshot.bubble_style == BubbleStyle::Capsule, "loader uses bubble input");
    check(snapshot.appearance.preset == ThemePreset::Dark, "loader reads theme preset");
    check(snapshot.appearance.custom.background == QColor(QStringLiteral("#101010")),
          "loader reads background");
    check(snapshot.appearance.custom_font_size == 12, "loader reads font size");
    check(snapshot.appearance.linger_ms == 1500, "loader reads linger");
    check(snapshot.appearance.pinned, "loader reads pinned");
    check(snapshot.model_choices.recognizer.value(QStringLiteral("zh")) ==
              QStringLiteral("sensevoice"),
          "loader reads recognizer choice");
    check(snapshot.model_choices.translator == QStringLiteral("qwen"), "loader reads translator");
    check(snapshot.model_choices.custom_directory == QStringLiteral("D:/models"),
          "loader reads custom directory");
    check(snapshot.model_choices.custom_paths.value(QStringLiteral("custom")) ==
              QStringLiteral("D:/m.bin"),
          "loader reads custom path");
    check(snapshot.hotwords.size() == 2, "loader reads hotwords");
    if (snapshot.hotwords.size() == 2) {
        check(snapshot.hotwords[0].phrase == "你好", "hotword phrase");
        check(snapshot.hotwords[0].aliases.size() == 2, "hotword aliases");
        check(snapshot.hotwords[0].boost == 4.5F, "hotword boost");
        check(snapshot.hotwords[0].keep_untranslated, "hotword keep flag");
        check(!snapshot.hotwords[1].enabled, "hotword disabled flag");
    }

    const std::filesystem::path missing = dir.filePath(QStringLiteral("missing.tsv")).toStdString();
    SettingsLoadInputs missing_inputs{settings, missing, false, BubbleStyle::Ring};
    const SettingsLoadResult missing_result = loadSettingsSnapshot(missing_inputs);
    check(missing_result.error.empty(), "missing hotwords file is not an error");
    check(missing_result.snapshot.hotwords.empty(), "missing hotwords file yields empty list");
}

void testMigrations(QTemporaryDir& dir) {
    const QString settings_path = dir.filePath(QStringLiteral("legacy.ini"));
    {
        QSettings settings(settings_path, QSettings::IniFormat);
        settings.setValue(QStringLiteral("output/destination"), QStringLiteral("copy-translation"));
        settings.setValue(QStringLiteral("vad/profile_version"), 1);
        settings.setValue(QStringLiteral("vad/endpoint_ms"), 700);
        settings.setValue(QStringLiteral("vad/threshold_percent"), 80);
        settings.setValue(QStringLiteral("vad/minimum_db"), -45);
        settings.setValue(QStringLiteral("vad/snr_db"), 8);
        settings.sync();
    }

    const std::filesystem::path no_hotwords;
    QSettings settings(settings_path, QSettings::IniFormat);
    SettingsLoadInputs inputs{settings, no_hotwords, false, BubbleStyle::Ring};
    const SettingsLoadResult result = loadSettingsSnapshot(inputs);
    check(hasMigration(result, SettingsMigration::CopyBoth), "legacy destination detected");
    check(hasMigration(result, SettingsMigration::MissingContent), "missing content detected");
    check(hasMigration(result, SettingsMigration::VadProfile), "vad profile detected");
    check(result.snapshot.destination == ResultDestination::CopyOnly,
          "legacy destination maps to copy");
    check(result.snapshot.popup_content == ResultContent::Translation,
          "legacy content maps to translation");
    check(result.snapshot.clipboard_content == ResultContent::Translation,
          "legacy clipboard content follows");
    check(result.snapshot.vad.endpoint_ms == default_endpoint_ms, "old vad profile is reset");

    migrateSettings(settings, result);
    QSettings migrated(settings_path, QSettings::IniFormat);
    check(migrated.value(QStringLiteral("output/destination")).toString() == QStringLiteral("copy"),
          "migration writes destination");
    check(migrated.value(QStringLiteral("output/content")).toString() ==
              QStringLiteral("translation"),
          "migration writes content");
    check(migrated.value(QStringLiteral("output/clipboard_content")).toString() ==
              QStringLiteral("translation"),
          "migration writes clipboard content");
    check(migrated.value(QStringLiteral("vad/profile_version")).toInt() == 5,
          "migration writes vad profile version");
    check(migrated.value(QStringLiteral("vad/endpoint_ms")).toInt() == default_endpoint_ms,
          "migration writes endpoint default");
}

} // namespace

int main() {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FAIL: temporary directory\n");
        return 1;
    }

    testDefaults();
    testCodecs();
    testLoader(dir);
    testMigrations(dir);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("settings-types-test: all checks passed\n");
    return 0;
}
