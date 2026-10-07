#include "model_manager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace {

QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

void append_varint(QByteArray& out, quint64 value) {
    do {
        char byte = static_cast<char>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) byte = static_cast<char>(byte | 0x80);
        out.append(byte);
    } while (value != 0);
}

void append_bytes_field(QByteArray& out, int field, const QByteArray& bytes) {
    append_varint(out, (static_cast<quint64>(field) << 3U) | 2U); // length-delimited
    append_varint(out, static_cast<quint64>(bytes.size()));
    out.append(bytes);
}

// ModelProto.metadata_props is field 14 of StringStringEntryProto {key = 1,
// value = 2}. Protobuf merges repeated fields across a concatenated message,
// so appending entries to the end of the file adds metadata without an ONNX
// library.
QByteArray onnx_metadata(const QList<QPair<QByteArray, QByteArray>>& entries) {
    QByteArray out;
    for (const auto& [key, value] : entries) {
        QByteArray entry;
        append_bytes_field(entry, 1, key);
        append_bytes_field(entry, 2, value);
        append_bytes_field(out, 14, entry);
    }
    return out;
}

bool copy_directory(const QString& source, const QString& destination) {
    QDir().mkpath(destination);
    const QDir directory(source);
    for (const QFileInfo& entry : directory.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot)) {
        const QString target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir() ? !copy_directory(entry.filePath(), target)
                          : !QFile::copy(entry.filePath(), target)) {
            return false;
        }
    }
    return true;
}

bool part_applies(const ModelPart& part) {
#ifdef __APPLE__
    (void)part;
    return true;
#else
    return !part.apple_only;
#endif
}

} // namespace

ModelManager::ModelManager(QString bundled_directory, QObject* parent)
    : QObject(parent),
      bundled_directory_(std::move(bundled_directory)),
      // ~/Library/Application Support/SpeakAnything/models, or
      // %LOCALAPPDATA%/SpeakAnything/models on Windows.
      user_directory_(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                          .filePath(QStringLiteral("SpeakAnything/models"))),
      network_(new QNetworkAccessManager(this)) {
    QDir().mkpath(user_directory_);
}

ModelManager::~ModelManager() {
    if (reply_ != nullptr) reply_->abort();
    if (extractor_ != nullptr) extractor_->kill();
}

QString ModelManager::locate(const CatalogModel& model) const {
    // System voices have no files; the marker stands in for a path.
    if (model.engine == ModelEngine::SystemVoice) return QStringLiteral("system");
    for (const QString& root : {user_directory_, bundled_directory_}) {
        const QString path = QDir(root).filePath(to_qstring(model.path));
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

QString ModelManager::locateDirectory(const CatalogModel& model) const {
    const QString path = locate(model);
    if (path.isEmpty() || model.engine == ModelEngine::SystemVoice) return path;
    const QString relative = to_qstring(model.path);
    const QString first = relative.section(QLatin1Char('/'), 0, 0);
    return path.left(path.size() - relative.size()) + first;
}

bool ModelManager::installed(const CatalogModel& model) const {
    return !locate(model).isEmpty();
}

bool ModelManager::downloading(const QString& id) const {
    return (job_ && job_->id == id) ||
        std::find(queue_.begin(), queue_.end(), id) != queue_.end();
}

void ModelManager::download(const QString& id) {
    if (find_model(id.toStdString()) == nullptr || downloading(id)) return;
    queue_.push_back(id);
    if (!job_) startNext();
}

void ModelManager::cancel(const QString& id) {
    queue_.erase(std::remove(queue_.begin(), queue_.end(), id), queue_.end());
    if (job_ && job_->id == id) {
        if (reply_ != nullptr) reply_->abort();
        if (extractor_ != nullptr) extractor_->kill();
    }
}

bool ModelManager::remove(const CatalogModel& model) {
    if (model.bundled && !QFileInfo::exists(QDir(user_directory_).filePath(to_qstring(model.path)))) {
        return false;
    }
    bool ok = true;
    for (const ModelPart& part : model.parts) {
        if (part.archive == ArchiveKind::File) {
            QFile::remove(QDir(user_directory_).filePath(to_qstring(part.destination)));
        }
    }
    // Directory models: drop the folder that holds the marker path.
    const QString relative = to_qstring(model.path);
    if (relative.contains(QLatin1Char('/'))) {
        const QString folder = QDir(user_directory_).filePath(relative.section(QLatin1Char('/'), 0, 0));
        if (folder != QDir(user_directory_).filePath(QStringLiteral("whisper"))) {
            ok = QDir(folder).removeRecursively();
        }
    }
    return ok;
}

int ModelManager::addProgressHandler(ProgressHandler handler) {
    progress_handlers_.insert(next_token_, std::move(handler));
    return next_token_++;
}

int ModelManager::addFinishedHandler(FinishedHandler handler) {
    finished_handlers_.insert(next_token_, std::move(handler));
    return next_token_++;
}

void ModelManager::removeHandlers(int token) {
    progress_handlers_.remove(token);
    finished_handlers_.remove(token);
}

void ModelManager::startNext() {
    if (job_ || queue_.empty()) return;
    job_ = Job{queue_.front(), 0};
    queue_.pop_front();
    completed_bytes_ = 0;
    startPart();
}

void ModelManager::startPart() {
    const CatalogModel* model = find_model(job_->id.toStdString());
    while (job_->part < model->parts.size() && !part_applies(model->parts[job_->part])) {
        ++job_->part;
    }
    if (job_->part >= model->parts.size()) {
        finishJob(true, {});
        return;
    }
    const ModelPart& part = model->parts[job_->part];
    const QString temporary = QDir(user_directory_).filePath(
        QStringLiteral(".download-%1-%2").arg(job_->id).arg(job_->part));
    file_ = new QFile(temporary, this);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishJob(false, file_->errorString());
        return;
    }
    QNetworkRequest request(QUrl(to_qstring(part.url)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    reply_ = network_->get(request);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (file_ != nullptr) file_->write(reply_->readAll());
    });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64) { emitProgress(received); });
    connect(reply_, &QNetworkReply::finished, this, [this] { partDownloaded(); });
}

void ModelManager::partDownloaded() {
    QNetworkReply* reply = reply_;
    reply_ = nullptr;
    reply->deleteLater();
    if (file_ != nullptr) file_->write(reply->readAll());
    const QString temporary = file_->fileName();
    file_->close();
    file_->deleteLater();
    file_ = nullptr;
    if (reply->error() != QNetworkReply::NoError) {
        QFile::remove(temporary);
        finishJob(false, reply->error() == QNetworkReply::OperationCanceledError
                             ? QStringLiteral("cancelled") : reply->errorString());
        return;
    }

    const CatalogModel* model = find_model(job_->id.toStdString());
    const ModelPart& part = model->parts[job_->part];
    completed_bytes_ += QFileInfo(temporary).size();
    const QString destination = QDir(user_directory_).filePath(to_qstring(part.destination));

    if (part.archive == ArchiveKind::File) {
        QDir().mkpath(QFileInfo(destination).absolutePath());
        QFile::remove(destination);
        if (!QFile::rename(temporary, destination)) {
            finishJob(false, QStringLiteral("could not save %1").arg(destination));
            return;
        }
        ++job_->part;
        startPart();
        return;
    }

    // Archives: tar handles .tar.bz2 everywhere (bsdtar on Windows 10+ also
    // reads zip); macOS uses ditto for zip to keep Core ML bundles intact.
    QDir().mkpath(destination);
    extractor_ = new QProcess(this);
    extractor_->setProperty("archive", temporary);
#ifdef __APPLE__
    if (part.archive == ArchiveKind::Zip) {
        extractor_->setProgram(QStringLiteral("/usr/bin/ditto"));
        extractor_->setArguments({QStringLiteral("-x"), QStringLiteral("-k"), temporary, destination});
    } else
#endif
    {
        extractor_->setProgram(QStringLiteral("tar"));
        extractor_->setArguments({QStringLiteral("-xf"), temporary, QStringLiteral("-C"), destination});
    }
    connect(extractor_, &QProcess::finished, this, [this](int exit_code) { extractFinished(exit_code); });
    connect(extractor_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (extractor_ != nullptr && extractor_->state() == QProcess::NotRunning) extractFinished(-1);
    });
    extractor_->start();
}

void ModelManager::extractFinished(int exit_code) {
    if (extractor_ == nullptr) return;
    const QString archive = extractor_->property("archive").toString();
    extractor_->deleteLater();
    extractor_ = nullptr;
    QFile::remove(archive);
    if (exit_code != 0) {
        finishJob(false, QStringLiteral("could not unpack the download"));
        return;
    }
    ++job_->part;
    startPart();
}

void ModelManager::emitProgress(qint64 received) {
    if (!job_) return;
    const CatalogModel* model = find_model(job_->id.toStdString());
    const auto total = static_cast<qint64>(model->download_bytes());
    for (const auto& handler : progress_handlers_) {
        handler(job_->id, completed_bytes_ + received, total);
    }
}

QString ModelManager::importPiper(const CatalogModel& model) const {
    const QString relative = to_qstring(model.path);
    const QString folder = QDir(user_directory_).filePath(relative.section(QLatin1Char('/'), 0, 0));
    QString onnx_path;
    QString json_path;
    for (const ModelPart& part : model.parts) {
        const QString path = QDir(user_directory_).filePath(to_qstring(part.destination));
        (path.endsWith(QStringLiteral(".json")) ? json_path : onnx_path) = path;
    }
    QFile json_file(json_path);
    if (!json_file.open(QIODevice::ReadOnly)) return QStringLiteral("missing voice description");
    const QJsonObject config = QJsonDocument::fromJson(json_file.readAll()).object();
    const QString phonemes = config.value(QStringLiteral("phoneme_type")).toString(QStringLiteral("espeak"));
    if (phonemes != QStringLiteral("espeak")) {
        return QStringLiteral("this voice needs the %1 phonemizer, which is not supported").arg(phonemes);
    }

    // Metadata sherpa-onnx reads from Piper models.
    const QByteArray metadata = onnx_metadata({
        {"model_type", "vits"},
        {"comment", "piper"},
        {"language", to_qstring(model.languages.front()).toUtf8()},
        {"voice", config.value(QStringLiteral("espeak")).toObject()
                      .value(QStringLiteral("voice")).toString().toUtf8()},
        {"has_espeak", "1"},
        {"n_speakers", QByteArray::number(config.value(QStringLiteral("num_speakers")).toInt(1))},
        {"sample_rate", QByteArray::number(
            config.value(QStringLiteral("audio")).toObject().value(QStringLiteral("sample_rate")).toInt(22050))},
    });
    QFile onnx(onnx_path);
    if (!onnx.open(QIODevice::Append) || onnx.write(metadata) != metadata.size()) {
        return QStringLiteral("could not prepare the voice model");
    }
    onnx.close();

    // espeak-ng data is shared with any installed Kokoro or Piper voice
    // (Kokoro ships with the app).
    QString espeak_source;
    for (const CatalogModel& voice : model_catalog()) {
        if (voice.kind != ModelKind::Voice || &voice == &model || voice.import_piper) continue;
        const QString directory = locateDirectory(voice);
        if (directory.isEmpty()) continue;
        const QString candidate = QDir(directory).filePath(QStringLiteral("espeak-ng-data"));
        if (QFileInfo::exists(QDir(candidate).filePath(QStringLiteral("phontab")))) {
            espeak_source = candidate;
            break;
        }
    }
    if (espeak_source.isEmpty()) return QStringLiteral("missing espeak-ng data (install the Kokoro voice)");
    const QString espeak_target = QDir(folder).filePath(QStringLiteral("espeak-ng-data"));
    // A copy (18 MB), not a link: the source may sit inside an app bundle that
    // gets moved or replaced on update.
    QFile::remove(espeak_target);
    QDir(espeak_target).removeRecursively();
    if (!copy_directory(espeak_source, espeak_target)) return QStringLiteral("could not copy espeak-ng data");

    // tokens.txt last: it marks the voice as installed.
    QFile tokens(QDir(folder).filePath(QStringLiteral("tokens.txt")));
    if (!tokens.open(QIODevice::WriteOnly | QIODevice::Truncate)) return tokens.errorString();
    const QJsonObject map = config.value(QStringLiteral("phoneme_id_map")).toObject();
    for (auto it = map.begin(); it != map.end(); ++it) {
        const QJsonArray ids = it.value().toArray();
        if (ids.isEmpty()) continue;
        tokens.write(it.key().toUtf8() + ' ' + QByteArray::number(ids.first().toInt()) + '\n');
    }
    return {};
}

void ModelManager::finishJob(bool ok, QString error) {
    const QString id = job_->id;
    if (ok) {
        const CatalogModel* model = find_model(id.toStdString());
        if (model != nullptr && model->import_piper) {
            error = importPiper(*model);
            ok = error.isEmpty();
        }
    }
    job_.reset();
    const auto handlers = finished_handlers_;
    for (const auto& handler : handlers) handler(id, ok, error);
    startNext();
}
