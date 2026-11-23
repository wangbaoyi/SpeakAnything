#pragma once

#include "imgui_ui/settings_ui.h"
#include "model_manager.h"

#include <QHash>
#include <QString>

#include <string>
#include <vector>

namespace sa::imgui_ui {

class ModelsUi {
public:
    ModelsUi(ModelManager& manager, SettingsState& state);
    ~ModelsUi();

    ModelsUi(const ModelsUi&) = delete;
    ModelsUi& operator=(const ModelsUi&) = delete;

    void draw();

private:
    void drawDirectoryCard();
    void drawModelCard(ModelKind kind, const char* title, const char* hint);
    void saveChoices() const;
    void importModel(ModelKind kind, const CatalogModel& model);
    [[nodiscard]] QString languageFor(ModelKind kind) const;
    [[nodiscard]] QString choiceFor(ModelKind kind) const;
    void storeChoice(ModelKind kind, const QString& id);

    ModelManager& manager_;
    SettingsState& state_;
    int progress_token_ = 0;
    int finished_token_ = 0;
    QHash<QString, int> progress_;
    std::string message_;
};

} // namespace sa::imgui_ui
