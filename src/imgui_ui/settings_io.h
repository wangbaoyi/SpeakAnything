#pragma once

#include "imgui_ui/settings_ui.h"

#include <filesystem>
#include <string>

namespace sa::imgui_ui {

void loadSettings(SettingsState& state, const std::filesystem::path& hotwords_path);
bool saveSettings(const SettingsState& state, const std::filesystem::path& hotwords_path,
                  std::string* error = nullptr);
void saveTranslationEnabled(bool enabled);
void saveAppearance(const sa::settings::AppearanceSettings& appearance);

} // namespace sa::imgui_ui
