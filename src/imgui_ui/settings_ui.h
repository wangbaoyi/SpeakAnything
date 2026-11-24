#pragma once

#include "ui/settings_types.h"

#include <string>
#include <functional>
#include <vector>

namespace sa::imgui_ui {

class ModelsUi;

struct SettingsState {
    sa::settings::SettingsSnapshot snapshot;
    sa::settings::SettingsDraft draft;
    int page = 0;
    int source_language = 0;
    int target_language = 0;
    int speech_device = 0;
    int selected_hotword = -1;
    bool dirty = false;
    bool close_requested = false;
    bool apply_requested = false;
    std::string message;
    ModelsUi* models_ui = nullptr;
    std::function<void()> appearance_changed;
    std::vector<std::string> language_labels;
    std::vector<std::string> language_codes;
    std::vector<std::string> speech_devices;
};

void initialize(SettingsState& state);
void draw(SettingsState& state);

} // namespace sa::imgui_ui
