#pragma once

#include "model_manager.h"
#include "ui/settings_types.h"

#include <QHash>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

using sa::settings::ModelChoices;

// Settings page: pick the recognizer, translator and voice for the current
// languages, and download or remove models.
class ModelsPage final : public QWidget {
public:
    ModelsPage(ModelManager* manager, ModelChoices choices, QWidget* parent = nullptr);
    ~ModelsPage() override;

    void setLanguages(const QString& source, const QString& target);
    [[nodiscard]] ModelChoices choices() const;

private:
    struct Row {
        ModelKind kind;
        QComboBox* combo = nullptr;
        QLabel* state = nullptr;
        QLabel* status = nullptr;
        QPushButton* download = nullptr;
        QPushButton* browser = nullptr;
        QPushButton* import_button = nullptr;
        QPushButton* remove = nullptr;
        QLabel* delete_hint = nullptr;
        QWidget* actions = nullptr;
        QProgressBar* progress = nullptr;
    };

    void addRow(Row& row, const QString& title, const QString& hint);
    void addDirectoryControls();
    void importModel(Row& row);
    void chooseDirectory();
    void resetDirectory();
    void refreshDirectoryLabel();
    void fill(Row& row);
    void refresh(Row& row);
    [[nodiscard]] QString language(const Row& row) const;
    [[nodiscard]] QString choiceFor(const Row& row) const;
    void storeChoice(const Row& row);

    ModelManager* manager_;
    ModelChoices choices_;
    QLabel* directory_label_ = nullptr;
    QString source_ = QStringLiteral("zh");
    QString target_ = QStringLiteral("en");
    Row recognizer_{ModelKind::Recognizer};
    Row translator_{ModelKind::Translator};
    Row voice_{ModelKind::Voice};
    int handlers_ = 0;
    bool filling_ = false;
};
