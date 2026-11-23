#pragma once

#include "model_catalog.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

#include <deque>
#include <functional>
#include <optional>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;
class QProcess;

// Finds installed models and downloads missing ones. The modelspt directory
// next to the executable is both the bundled location and the preferred
// download location when it is writable. A legacy models directory and the
// per-user fallback directory are searched as well. Users can also register a
// custom search directory or a path per model.
// Lives on the UI thread; one download runs at a time, others queue.
class ModelManager final : public QObject {
public:
    using ProgressHandler = std::function<void(const QString& id, qint64 received, qint64 total)>;
    using FinishedHandler = std::function<void(const QString& id, bool ok, const QString& error)>;

    ModelManager(QString bundled_directory, QObject* parent = nullptr);
    ~ModelManager() override;

    [[nodiscard]] QString bundledDirectory() const { return bundled_directory_; }
    [[nodiscard]] QString userDirectory() const { return user_directory_; }
    [[nodiscard]] QString customDirectory() const { return custom_directory_; }
    [[nodiscard]] QHash<QString, QString> customPaths() const { return custom_paths_; }
    // A user-selected directory searched after the bundled modelspt folder.
    // Empty clears it. Paths that do not exist are ignored when resolving.
    void setCustomDirectory(const QString& directory);
    // Exact file or directory for one catalog model, searched before every
    // root. Empty removes the override.
    void setCustomPath(const QString& id, const QString& path);
    void clearCustomPath(const QString& id);
    void clearCustomPaths();
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
    [[nodiscard]] QStringList searchRoots() const;

    QString bundled_directory_;
    QString legacy_directory_;
    QString alternate_directory_;
    QString custom_directory_;
    QHash<QString, QString> custom_paths_;
    QString user_directory_;
    QString legacy_user_directory_;
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
