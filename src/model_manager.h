#pragma once

#include "model_catalog.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <deque>
#include <functional>
#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Finds installed models and downloads missing ones. Models bundled with the
// app live in its data directory; downloads go to a per-user directory
// (Application Support on macOS, %LOCALAPPDATA% on Windows), which is searched
// first. Lives on the UI thread; one download runs at a time, others queue.
class ModelManager final : public QObject {
public:
    using ProgressHandler = std::function<void(const QString& id, qint64 received, qint64 total)>;
    using FinishedHandler = std::function<void(const QString& id, bool ok, const QString& error)>;

    ModelManager(QString bundled_directory, QObject* parent = nullptr);
    ~ModelManager() override;

    [[nodiscard]] QString userDirectory() const { return user_directory_; }
    [[nodiscard]] bool installed(const CatalogModel& model) const;
    // Absolute path of the model's `path` entry, or empty when not installed.
    [[nodiscard]] QString locate(const CatalogModel& model) const;
    // Directory holding the model (its first path component), for directory models.
    [[nodiscard]] QString locateDirectory(const CatalogModel& model) const;

    void download(const QString& id);
    void cancel(const QString& id);
    [[nodiscard]] bool downloading(const QString& id) const;
    // Removes a downloaded model; bundled copies are never touched.
    bool remove(const CatalogModel& model);

    // Several observers (the dialog, the main window) can listen.
    int addProgressHandler(ProgressHandler handler);
    int addFinishedHandler(FinishedHandler handler);
    void removeHandlers(int token);

private:
    struct Job {
        QString id;
        std::size_t part = 0;
    };

    void startNext();
    void startPart();
    void partDownloaded();
    void extractFinished(int exit_code);
    void finishJob(bool ok, QString error);
    // Turns a downloaded rhasspy Piper voice into a sherpa-onnx one; returns
    // an error message, or empty on success.
    [[nodiscard]] QString importPiper(const CatalogModel& model) const;
    void emitProgress(qint64 received);

    QString bundled_directory_;
    QString user_directory_;
    QNetworkAccessManager* network_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QFile* file_ = nullptr;
    QProcess* extractor_ = nullptr;
    std::deque<QString> queue_;
    std::optional<Job> job_;
    qint64 completed_bytes_ = 0;
    int next_token_ = 1;
    QHash<int, ProgressHandler> progress_handlers_;
    QHash<int, FinishedHandler> finished_handlers_;
};
