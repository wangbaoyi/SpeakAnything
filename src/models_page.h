#pragma once

#include "model_manager.h"

#include <QHash>
#include <QString>
#include <QWidget>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;

// Which model to use for each job. "auto" picks the best installed one.
// Recognizers are chosen per spoken language and voices per output language.
struct ModelChoices {
    QHash<QString, QString> recognizer; // language -> model id
    QHash<QString, QString> voice;      // language -> model id
    QString translator = QStringLiteral("auto");
};

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
        QLabel* status = nullptr;
        QPushButton* download = nullptr;
        QPushButton* remove = nullptr;
        QProgressBar* progress = nullptr;
    };

    void addRow(Row& row, const QString& title, const QString& hint);
    void fill(Row& row);
    void refresh(Row& row);
    [[nodiscard]] QString language(const Row& row) const;
    [[nodiscard]] QString choiceFor(const Row& row) const;
    void storeChoice(const Row& row);

    ModelManager* manager_;
    ModelChoices choices_;
    QString source_ = QStringLiteral("zh");
    QString target_ = QStringLiteral("en");
    Row recognizer_{ModelKind::Recognizer};
    Row translator_{ModelKind::Translator};
    Row voice_{ModelKind::Voice};
    int handlers_ = 0;
    bool filling_ = false;
};
