#include "models_page.h"

#include "languages.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString to_qstring(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString megabytes(std::uint64_t bytes) {
    return QStringLiteral("%1 MB").arg((bytes + 500'000) / 1'000'000);
}

QString language_label(const QString& code) {
    const LanguageInfo* language = find_language(code.toStdString());
    return language == nullptr ? code : QString::fromUtf8(language->native_name.data(),
                                                          static_cast<qsizetype>(language->native_name.size()));
}

} // namespace

ModelsPage::ModelsPage(ModelManager* manager, ModelChoices choices, QWidget* parent)
    : QWidget(parent), manager_(manager), choices_(std::move(choices)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    addDirectoryControls();
    addRow(recognizer_, QCoreApplication::translate("SpeakAnything", "语音识别"),
           QCoreApplication::translate("SpeakAnything",
               "SenseVoice 支持中、英、粤、日、韩；其他语言（如保加利亚语、德语）需要 Whisper。"));
    addRow(translator_, QCoreApplication::translate("SpeakAnything", "翻译"),
           QCoreApplication::translate("SpeakAnything",
               "更大的模型对不常见的语言翻译得更好，但更慢、占用更多内存。"));
    addRow(voice_, QCoreApplication::translate("SpeakAnything", "朗读声音"),
           QCoreApplication::translate("SpeakAnything",
               "Kokoro 说中文和英文；其他语言用 Piper 声音，每种语言单独下载。"));
    auto* note = new QLabel(QCoreApplication::translate("SpeakAnything",
        "自动：使用已安装的最快模型（神经网络引擎 > GPU > CPU）。程序优先使用可执行目录下的 modelspt 文件夹；也可以选择自定义目录，或在下面对应的模型上点“导入”。当前下载目录：%1")
            .arg(manager_->userDirectory()));
    note->setWordWrap(true);
    note->setTextInteractionFlags(Qt::TextSelectableByMouse);
    note->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(note);
    layout->addStretch();

    handlers_ = manager_->addProgressHandler([this](const QString& id, qint64 received, qint64 total) {
        for (Row* row : {&recognizer_, &translator_, &voice_}) {
            if (row->combo->currentData().toString() != id) continue;
            row->progress->setVisible(true);
            row->progress->setRange(0, 1000);
            row->progress->setValue(total > 0 ? static_cast<int>(received * 1000 / total) : 0);
        }
    });
    const int finished = manager_->addFinishedHandler([this](const QString&, bool ok, const QString& error) {
        if (!ok && error != QStringLiteral("cancelled")) {
            QMessageBox::warning(this, QCoreApplication::translate("SpeakAnything", "下载失败"), error);
        }
        for (Row* row : {&recognizer_, &translator_, &voice_}) fill(*row);
    });
    // Both handlers share one removal: the finished token follows the progress one.
    Q_UNUSED(finished);
    for (Row* row : {&recognizer_, &translator_, &voice_}) fill(*row);
}

ModelsPage::~ModelsPage() {
    manager_->removeHandlers(handlers_);
    manager_->removeHandlers(handlers_ + 1);
}

void ModelsPage::addDirectoryControls() {
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    auto* heading = new QLabel(QCoreApplication::translate("SpeakAnything", "模型目录"));
    heading->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(heading);
    auto* hint = new QLabel(QCoreApplication::translate("SpeakAnything",
        "默认在可执行目录下的 modelspt 文件夹查找。已经下载到其他位置的模型，"
        "可以选择它们所在的目录，或在下面对应的模型上点“导入”。"));
    hint->setObjectName(QStringLiteral("cardHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    directory_label_ = new QLabel;
    directory_label_->setWordWrap(true);
    directory_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    directory_label_->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(directory_label_);

    auto* controls = new QHBoxLayout;
    auto* choose = new QPushButton(QCoreApplication::translate("SpeakAnything", "选择目录..."));
    auto* reset = new QPushButton(QCoreApplication::translate("SpeakAnything", "恢复默认"));
    controls->addWidget(choose);
    controls->addWidget(reset);
    controls->addStretch();
    layout->addLayout(controls);
    static_cast<QVBoxLayout*>(this->layout())->addWidget(card);

    connect(choose, &QPushButton::clicked, this, [this] { chooseDirectory(); });
    connect(reset, &QPushButton::clicked, this, [this] { resetDirectory(); });
    refreshDirectoryLabel();
}

void ModelsPage::refreshDirectoryLabel() {
    if (directory_label_ == nullptr) return;
    const QString bundled = manager_->bundledDirectory();
    const QString custom = manager_->customDirectory();
    directory_label_->setText(custom.isEmpty()
        ? QCoreApplication::translate("SpeakAnything", "当前：%1").arg(bundled)
        : QCoreApplication::translate("SpeakAnything", "自定义：%1\n默认：%2")
              .arg(custom, bundled));
}

void ModelsPage::chooseDirectory() {
    const QString start = manager_->customDirectory().isEmpty()
        ? manager_->bundledDirectory() : manager_->customDirectory();
    const QString directory = QFileDialog::getExistingDirectory(
        this, QCoreApplication::translate("SpeakAnything", "选择存放模型的目录"), start);
    if (directory.isEmpty()) return;
    choices_.custom_directory = directory;
    manager_->setCustomDirectory(directory);
    refreshDirectoryLabel();
    for (Row* row : {&recognizer_, &translator_, &voice_}) fill(*row);
}

void ModelsPage::resetDirectory() {
    choices_.custom_directory.clear();
    manager_->setCustomDirectory({});
    refreshDirectoryLabel();
    for (Row* row : {&recognizer_, &translator_, &voice_}) fill(*row);
}

void ModelsPage::importModel(Row& row) {
    const QString id = row.combo->currentData().toString();
    const CatalogModel* model = find_model(id.toStdString());
    if (model == nullptr) {
        QMessageBox::information(this,
            QCoreApplication::translate("SpeakAnything", "导入模型"),
            QCoreApplication::translate("SpeakAnything", "请先在列表中选择一个具体模型。"));
        return;
    }
    if (model->engine == ModelEngine::SystemVoice) return;
    const QString current = manager_->customPaths().value(id);
    const QString installed = manager_->locate(*model);
    const QString start = !current.isEmpty()
        ? (QFileInfo(current).isDir() ? current : QFileInfo(current).absolutePath())
        : (!installed.isEmpty()
            ? (QFileInfo(installed).isDir() ? installed : QFileInfo(installed).absolutePath())
            : (manager_->customDirectory().isEmpty()
                ? manager_->bundledDirectory() : manager_->customDirectory()));

    if (model->engine == ModelEngine::Kokoro || model->engine == ModelEngine::Piper) {
        const QString directory = QFileDialog::getExistingDirectory(
            this, QCoreApplication::translate("SpeakAnything", "选择模型文件夹"), start);
        if (directory.isEmpty()) return;
        const QString marker = model->engine == ModelEngine::Kokoro
            ? QStringLiteral("model.onnx") : QStringLiteral("tokens.txt");
        if (!QFileInfo::exists(QDir(directory).filePath(marker))) {
            QMessageBox::warning(this,
                QCoreApplication::translate("SpeakAnything", "导入模型"),
                QCoreApplication::translate("SpeakAnything", "这个文件夹里没有 %1，无法作为该模型使用。")
                    .arg(marker));
            return;
        }
        choices_.custom_paths.insert(id, directory);
        manager_->setCustomPath(id, directory);
        fill(row);
        return;
    }

    const QString filter = model->engine == ModelEngine::Whisper
        ? QCoreApplication::translate("SpeakAnything", "Whisper 模型 (*.bin);;所有文件 (*)")
        : QCoreApplication::translate("SpeakAnything", "模型文件 (*.gguf *.bin);;所有文件 (*)");
    const QString file = QFileDialog::getOpenFileName(
        this, QCoreApplication::translate("SpeakAnything", "选择模型文件"), start, filter);
    if (file.isEmpty()) return;
    const QString suffix = QFileInfo(file).suffix().toLower();
    const bool expected = model->engine == ModelEngine::Whisper
        ? suffix == QStringLiteral("bin")
        : suffix == QStringLiteral("gguf");
    if (!expected) {
        QMessageBox::warning(this,
            QCoreApplication::translate("SpeakAnything", "导入模型"),
            QCoreApplication::translate("SpeakAnything", "%1 需要 %2 文件，选中的是 %3。")
                .arg(to_qstring(model->name),
                     model->engine == ModelEngine::Whisper ? QStringLiteral(".bin")
                                                           : QStringLiteral(".gguf"),
                     QFileInfo(file).fileName()));
        return;
    }
    choices_.custom_paths.insert(id, file);
    manager_->setCustomPath(id, file);
    fill(row);
}

void ModelsPage::addRow(Row& row, const QString& title, const QString& hint) {
    auto* card = new QFrame;
    card->setObjectName(QStringLiteral("card"));
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(8);
    auto* heading = new QLabel(title);
    heading->setObjectName(QStringLiteral("cardTitle"));
    layout->addWidget(heading);
    auto* hint_label = new QLabel(hint);
    hint_label->setObjectName(QStringLiteral("cardHint"));
    hint_label->setWordWrap(true);
    layout->addWidget(hint_label);

    auto* controls = new QHBoxLayout;
    row.combo = new QComboBox;
    row.download = new QPushButton(QCoreApplication::translate("SpeakAnything", "下载"));
    row.browser = new QPushButton(QCoreApplication::translate("SpeakAnything", "浏览器下载"));
    row.import_button = new QPushButton(QCoreApplication::translate("SpeakAnything", "导入..."));
    row.import_button->setToolTip(QCoreApplication::translate("SpeakAnything",
        "使用已经下载好的模型文件或文件夹"));
    row.remove = new QPushButton(QCoreApplication::translate("SpeakAnything", "删除"));
    controls->addWidget(row.combo, 1);
    controls->addWidget(row.download);
    controls->addWidget(row.browser);
    controls->addWidget(row.import_button);
    controls->addWidget(row.remove);
    layout->addLayout(controls);
    row.status = new QLabel;
    row.status->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(row.status);
    row.progress = new QProgressBar;
    row.progress->setTextVisible(false);
    row.progress->setFixedHeight(6);
    row.progress->setVisible(false);
    layout->addWidget(row.progress);
    static_cast<QVBoxLayout*>(this->layout())->addWidget(card);

    connect(row.combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, &row] {
        if (filling_) return;
        storeChoice(row);
        refresh(row);
    });
    connect(row.download, &QPushButton::clicked, this, [this, &row] {
        const QString id = row.combo->currentData().toString();
        if (manager_->downloading(id)) {
            manager_->cancel(id);
        } else {
            manager_->download(id);
        }
        refresh(row);
    });
    connect(row.browser, &QPushButton::clicked, this, [this, &row] {
        const CatalogModel* model = find_model(row.combo->currentData().toString().toStdString());
        if (model == nullptr) return;
        for (const ModelPart* part : downloadable_parts(*model)) {
            QDesktopServices::openUrl(QUrl(to_qstring(part->url)));
        }
    });
    connect(row.import_button, &QPushButton::clicked, this, [this, &row] { importModel(row); });
    connect(row.remove, &QPushButton::clicked, this, [this, &row] {
        const CatalogModel* model = find_model(row.combo->currentData().toString().toStdString());
        if (model == nullptr) return;
        choices_.custom_paths.remove(row.combo->currentData().toString());
        manager_->remove(*model);
        fill(row);
    });
}

QString ModelsPage::language(const Row& row) const {
    switch (row.kind) {
    case ModelKind::Recognizer: return source_;
    case ModelKind::Voice: return target_;
    case ModelKind::Translator: return target_;
    }
    return source_;
}

QString ModelsPage::choiceFor(const Row& row) const {
    switch (row.kind) {
    case ModelKind::Recognizer: return choices_.recognizer.value(source_, QStringLiteral("auto"));
    case ModelKind::Voice: return choices_.voice.value(target_, QStringLiteral("auto"));
    case ModelKind::Translator: return choices_.translator;
    }
    return QStringLiteral("auto");
}

void ModelsPage::storeChoice(const Row& row) {
    const QString id = row.combo->currentData().toString();
    switch (row.kind) {
    case ModelKind::Recognizer: choices_.recognizer.insert(source_, id); break;
    case ModelKind::Voice: choices_.voice.insert(target_, id); break;
    case ModelKind::Translator: choices_.translator = id; break;
    }
}

void ModelsPage::fill(Row& row) {
    filling_ = true;
    const QString lang = language(row);
    const QString selected = row.combo->count() > 0 && row.combo->currentData().toString() != QStringLiteral("auto")
        ? row.combo->currentData().toString() : choiceFor(row);
    row.combo->clear();
    row.combo->addItem(QCoreApplication::translate("SpeakAnything", "自动"), QStringLiteral("auto"));
    const auto models = row.kind == ModelKind::Translator
        ? models_for(ModelKind::Translator, "")
        : models_for(row.kind, lang.toStdString());
    for (const CatalogModel* model : models) {
        const QString suffix = manager_->installed(*model)
            ? QString()
            : QCoreApplication::translate("SpeakAnything", "（未下载，%1）").arg(megabytes(model->download_bytes()));
        row.combo->addItem(to_qstring(model->name) + suffix, to_qstring(model->id));
    }
    const int index = row.combo->findData(selected);
    row.combo->setCurrentIndex(index >= 0 ? index : 0);
    filling_ = false;
    refresh(row);
}

void ModelsPage::refresh(Row& row) {
    const QString id = row.combo->currentData().toString();
    const CatalogModel* model = find_model(id.toStdString());
    const bool busy = manager_->downloading(id);
    if (model == nullptr) {
        // Automatic: say what it resolves to.
        const CatalogModel* best = nullptr;
        const auto models = row.kind == ModelKind::Translator
            ? models_for(ModelKind::Translator, "")
            : models_for(row.kind, language(row).toStdString());
        for (const CatalogModel* candidate : models) {
            if (manager_->installed(*candidate)) {
                best = candidate;
                break;
            }
        }
        row.status->setText(best != nullptr
            ? QCoreApplication::translate("SpeakAnything", "将使用：%1").arg(to_qstring(best->name))
            : models.empty()
            ? QCoreApplication::translate("SpeakAnything", "%1 目前没有可用的模型。")
                  .arg(language_label(language(row)))
            : QCoreApplication::translate("SpeakAnything", "%1 还没有可用的模型，请选择一个下载。")
                  .arg(language_label(language(row))));
        row.download->setVisible(false);
        row.browser->setVisible(false);
        row.import_button->setVisible(false);
        row.remove->setVisible(false);
        row.progress->setVisible(false);
        return;
    }
    const bool installed = manager_->installed(*model);
    const QString custom = manager_->customPaths().value(id);
    const bool imported = !custom.isEmpty();
    row.download->setVisible(!installed || busy);
    row.download->setText(busy ? QCoreApplication::translate("SpeakAnything", "取消下载")
                               : QCoreApplication::translate("SpeakAnything", "下载"));
    row.browser->setVisible(!installed && !busy);
    row.import_button->setVisible(!busy && model->engine != ModelEngine::SystemVoice);
    row.import_button->setText(imported ? QCoreApplication::translate("SpeakAnything", "重新导入...")
                                 : QCoreApplication::translate("SpeakAnything", "导入..."));
    row.remove->setVisible(installed && !busy && (!model->bundled || imported));
    row.remove->setText(imported ? QCoreApplication::translate("SpeakAnything", "移除导入")
                                 : QCoreApplication::translate("SpeakAnything", "删除"));
    row.progress->setVisible(busy);
    row.status->setText(imported
        ? QCoreApplication::translate("SpeakAnything", "已导入：%1").arg(custom)
        : installed
        ? (model->bundled ? QCoreApplication::translate("SpeakAnything", "已内置")
                          : QCoreApplication::translate("SpeakAnything", "已下载"))
        : QCoreApplication::translate("SpeakAnything", "需要下载 %1").arg(megabytes(model->download_bytes())));
}

void ModelsPage::setLanguages(const QString& source, const QString& target) {
    source_ = source;
    target_ = target;
    for (Row* row : {&recognizer_, &voice_}) {
        row->combo->clear(); // forget the previous language's selection
        fill(*row);
    }
}

ModelChoices ModelsPage::choices() const {
    return choices_;
}
