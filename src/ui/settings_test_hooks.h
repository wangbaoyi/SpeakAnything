#pragma once

#include "ui/settings_types.h"

class QWidget;
class ModelManager;

namespace sa::settings {

QWidget* createSettingsDialogForTest(const SettingsSnapshot& snapshot,
                                     const SettingsEnvironment& environment,
                                     ModelManager* model_manager);
int settingsPageCountForTest(QWidget* dialog);
void settingsSelectPageForTest(QWidget* dialog, int index);
int settingsCurrentPageForTest(QWidget* dialog);
SettingsDraft settingsDraftForTest(QWidget* dialog);

} // namespace sa::settings
