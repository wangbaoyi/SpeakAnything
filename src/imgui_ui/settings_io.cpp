#include "imgui_ui/settings_io.h"

#include "compute_policy.h"
#include "text_processor.h"
#include "translation_session.h"

#include <QSettings>

#include <algorithm>

namespace sa::imgui_ui {
namespace {

sa::settings::SettingsDraft draftFromSnapshot(const sa::settings::SettingsSnapshot& snapshot) {
    sa::settings::SettingsDraft draft;
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

QString policyCode(ComputePolicy policy) {
    const std::string_view code = compute_policy_code(policy);
    return QString::fromLatin1(code.data(), static_cast<int>(code.size()));
}

} // namespace

void loadSettings(SettingsState& state, const std::filesystem::path& hotwords_path) {
    QSettings settings(QStringLiteral("SenseVoice"), QStringLiteral("LocalDictation"));
    const sa::settings::SettingsLoadInputs inputs{
        settings, hotwords_path, false, sa::settings::BubbleStyle::Ring};
    const sa::settings::SettingsLoadResult result = sa::settings::loadSettingsSnapshot(inputs);
    state.snapshot = result.snapshot;
    state.draft = draftFromSnapshot(result.snapshot);
}

bool saveSettings(const SettingsState& state, const std::filesystem::path& hotwords_path,
                  std::string* error) {
    QSettings settings(QStringLiteral("SenseVoice"), QStringLiteral("LocalDictation"));
    const sa::settings::SettingsDraft& draft = state.draft;
    settings.setValue(QStringLiteral("vad/endpoint_ms"), draft.vad.endpoint_ms);
    settings.setValue(QStringLiteral("vad/threshold_percent"), draft.vad.threshold_percent);
    settings.setValue(QStringLiteral("vad/minimum_db"), draft.vad.minimum_db);
    settings.setValue(QStringLiteral("vad/snr_db"), draft.vad.snr_db);
    settings.setValue(QString::fromLatin1(compute_policy_settings_key), policyCode(draft.compute));
    settings.setValue(QStringLiteral("hotkey/shortcut"), draft.shortcut);
    settings.setValue(QStringLiteral("text/mode"), draft.mode == sa::settings::TextMode::Raw ? 0 : 1);
    settings.setValue(QStringLiteral("output/destination"),
                      draft.destination == sa::settings::ResultDestination::Insert
                          ? QStringLiteral("insert")
                          : QStringLiteral("copy"));
    settings.setValue(QStringLiteral("output/content"),
                      sa::settings::resultContentName(draft.popup_content));
    settings.setValue(QStringLiteral("output/clipboard_content"),
                      sa::settings::resultContentName(draft.clipboard_content));
    settings.setValue(QStringLiteral("hotkey/trigger"),
                      draft.trigger == sa::settings::HotkeyTrigger::Realtime
                          ? QStringLiteral("realtime")
                      : draft.trigger == sa::settings::HotkeyTrigger::Toggle
                          ? QStringLiteral("toggle")
                          : QStringLiteral("hold"));
    settings.setValue(QStringLiteral("output/paste_delay_ms"), draft.paste_delay_ms);
    settings.setValue(QStringLiteral("output/clipboard_ttl_seconds"),
                      draft.clipboard_ttl_seconds);
    settings.setValue(QStringLiteral("audio/mute_while_recording"), draft.mute_while_recording);
    settings.setValue(QStringLiteral("translation/enabled"), draft.translation.enabled);
    settings.setValue(QStringLiteral("translation/direction"),
                      QString::fromStdString(direction_code(draft.translation.direction)));
    settings.setValue(QString::fromLatin1(translation_compute_policy_settings_key),
                      policyCode(draft.translation.device));
    settings.setValue(QStringLiteral("translation/separator"),
                      sa::settings::sanitizedSeparator(draft.translation.separator));
    settings.setValue(QStringLiteral("speech/enabled"), draft.speech.enabled);
    settings.setValue(QStringLiteral("speech/device_id"), draft.speech.device_id);
    settings.setValue(QStringLiteral("speech/monitor"), draft.speech.monitor);
    settings.setValue(QStringLiteral("speech/voice_zh"), draft.speech.voice_zh);
    settings.setValue(QStringLiteral("speech/voice_en"), draft.speech.voice_en);
    settings.setValue(QStringLiteral("speech/speed_percent"), draft.speech.speed_percent);
    settings.setValue(QStringLiteral("speech/live"), draft.speech.live);
    settings.setValue(QStringLiteral("appearance/preset"),
                      sa::settings::themePresetName(state.snapshot.appearance.preset));
    settings.setValue(QStringLiteral("appearance/custom_background"),
                      state.snapshot.appearance.custom.background.name());
    settings.setValue(QStringLiteral("appearance/custom_text"),
                      state.snapshot.appearance.custom.text.name());
    settings.setValue(QStringLiteral("appearance/custom_accent"),
                      state.snapshot.appearance.custom.accent.name());
    settings.setValue(QStringLiteral("appearance/custom_font_family"),
                      state.snapshot.appearance.custom_font_family);
    settings.setValue(QStringLiteral("appearance/custom_font_size"),
                      state.snapshot.appearance.custom_font_size);
    settings.setValue(QStringLiteral("appearance/linger_ms"),
                      state.snapshot.appearance.linger_ms);
    settings.setValue(QStringLiteral("appearance/pinned"), state.snapshot.appearance.pinned);
    settings.sync();

    TextProcessor processor;
    processor.set_hotwords(draft.hotwords);
    std::string hotword_error;
    if (!processor.save_hotwords(hotwords_path, hotword_error)) {
        if (error != nullptr) *error = hotword_error;
        return false;
    }
    return true;
}

void saveTranslationEnabled(bool enabled) {
    QSettings settings(QStringLiteral("SenseVoice"), QStringLiteral("LocalDictation"));
    settings.setValue(QStringLiteral("translation/enabled"), enabled);
    settings.sync();
}

} // namespace sa::imgui_ui
