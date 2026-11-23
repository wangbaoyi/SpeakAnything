#include "imgui_ui/models_ui.h"

#include "imgui_ui/native_dialogs.h"
#include "imgui_ui/theme.h"
#include "model_catalog.h"

#include <QSettings>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cfloat>
#include <filesystem>

namespace sa::imgui_ui {
namespace {

QString toQString(const std::string& text) {
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string toStd(const QString& text) {
    return text.toStdString();
}

std::wstring toWide(const QString& text) {
    return text.toStdWString();
}

QString megabytes(std::uint64_t bytes) {
    return QStringLiteral("%1 MB").arg((bytes + 500'000) / 1'000'000);
}

void rowLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(150.0f);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

} // namespace

ModelsUi::ModelsUi(ModelManager& manager, SettingsState& state)
    : manager_(manager), state_(state) {
    progress_token_ = manager_.addProgressHandler(
        [this](const QString& id, qint64 received, qint64 total) {
            progress_.insert(id, total > 0 ? static_cast<int>(received * 100 / total) : 0);
        });
    finished_token_ = manager_.addFinishedHandler(
        [this](const QString& id, bool ok, const QString& error) {
            progress_.remove(id);
            if (!ok && error != QStringLiteral("cancelled")) {
                message_ = "下载失败：" + toStd(error);
            }
        });
}

ModelsUi::~ModelsUi() {
    manager_.removeHandlers(progress_token_);
    manager_.removeHandlers(finished_token_);
}

QString ModelsUi::languageFor(ModelKind kind) const {
    if (kind == ModelKind::Voice) {
        if (!state_.language_codes.empty()) {
            return QString::fromStdString(
                state_.language_codes[static_cast<std::size_t>(state_.target_language)]);
        }
        return QStringLiteral("en");
    }
    if (!state_.language_codes.empty()) {
        return QString::fromStdString(
            state_.language_codes[static_cast<std::size_t>(state_.source_language)]);
    }
    return QStringLiteral("zh");
}

QString ModelsUi::choiceFor(ModelKind kind) const {
    const QString language = languageFor(kind);
    switch (kind) {
    case ModelKind::Recognizer:
        return state_.draft.model_choices.recognizer.value(language, QStringLiteral("auto"));
    case ModelKind::Voice:
        return state_.draft.model_choices.voice.value(language, QStringLiteral("auto"));
    case ModelKind::Translator:
        return state_.draft.model_choices.translator;
    }
    return QStringLiteral("auto");
}

void ModelsUi::storeChoice(ModelKind kind, const QString& id) {
    const QString language = languageFor(kind);
    switch (kind) {
    case ModelKind::Recognizer:
        state_.draft.model_choices.recognizer.insert(language, id);
        break;
    case ModelKind::Voice:
        state_.draft.model_choices.voice.insert(language, id);
        break;
    case ModelKind::Translator:
        state_.draft.model_choices.translator = id;
        break;
    }
    state_.dirty = true;
    saveChoices();
}

void ModelsUi::saveChoices() const {
    QSettings settings(QStringLiteral("SenseVoice"), QStringLiteral("LocalDictation"));
    settings.remove(QStringLiteral("models/recognizer"));
    settings.beginGroup(QStringLiteral("models/recognizer"));
    for (auto it = state_.draft.model_choices.recognizer.begin();
         it != state_.draft.model_choices.recognizer.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.remove(QStringLiteral("models/voice"));
    settings.beginGroup(QStringLiteral("models/voice"));
    for (auto it = state_.draft.model_choices.voice.begin();
         it != state_.draft.model_choices.voice.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.setValue(QStringLiteral("models/translator"), state_.draft.model_choices.translator);
    settings.setValue(QStringLiteral("models/custom_directory"),
                      state_.draft.model_choices.custom_directory);
    settings.remove(QStringLiteral("models/custom_paths"));
    settings.beginGroup(QStringLiteral("models/custom_paths"));
    for (auto it = state_.draft.model_choices.custom_paths.begin();
         it != state_.draft.model_choices.custom_paths.end(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.sync();
}

void ModelsUi::drawDirectoryCard() {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Border);
    ImGui::BeginChild("models_directory", ImVec2(0, 0), ImGuiChildFlags_Borders |
                                                          ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(theme::TextPrimary, "模型目录");
    ImGui::Separator();
    ImGui::TextColored(theme::TextMuted, "默认：%s",
                       toStd(manager_.bundledDirectory()).c_str());
    if (manager_.customDirectory().isEmpty()) {
        ImGui::TextColored(theme::TextMuted, "当前使用默认目录");
    } else {
        ImGui::TextColored(theme::TextMuted, "自定义：%s",
                           toStd(manager_.customDirectory()).c_str());
    }
    if (ImGui::Button("选择目录...")) {
        const QString start = manager_.customDirectory().isEmpty()
            ? manager_.bundledDirectory()
            : manager_.customDirectory();
        if (const auto folder = pickFolder(L"选择存放模型的目录", toWide(start).c_str())) {
            const QString path = QString::fromStdWString(folder->wstring());
            manager_.setCustomDirectory(path);
            state_.draft.model_choices.custom_directory = path;
            saveChoices();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("恢复默认")) {
        manager_.setCustomDirectory({});
        state_.draft.model_choices.custom_directory.clear();
        saveChoices();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}

void ModelsUi::importModel(ModelKind kind, const CatalogModel& model) {
    (void)kind;
    const QString id = toQString(model.id);
    const QString start = manager_.customPaths().value(id, manager_.bundledDirectory());
    if (model.engine == ModelEngine::Kokoro || model.engine == ModelEngine::Piper) {
        const auto folder = pickFolder(L"选择模型文件夹", toWide(start).c_str());
        if (!folder) return;
        const wchar_t* marker = model.engine == ModelEngine::Kokoro ? L"model.onnx" : L"tokens.txt";
        if (!std::filesystem::exists(*folder / marker)) {
            message_ = "文件夹中缺少 model.onnx 或 tokens.txt";
            return;
        }
        const QString path = QString::fromStdWString(folder->wstring());
        manager_.setCustomPath(id, path);
        state_.draft.model_choices.custom_paths.insert(id, path);
        saveChoices();
        return;
    }
    const auto file = pickModelFile(L"选择模型文件", toWide(start).c_str(),
                                    model.engine == ModelEngine::Whisper);
    if (!file) return;
    const QString path = QString::fromStdWString(file->wstring());
    manager_.setCustomPath(id, path);
    state_.draft.model_choices.custom_paths.insert(id, path);
    saveChoices();
}

void ModelsUi::drawModelCard(ModelKind kind, const char* title, const char* hint) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Border);
    ImGui::BeginChild(title, ImVec2(0, 0), ImGuiChildFlags_Borders |
                                                ImGuiChildFlags_AutoResizeY);
    ImGui::TextColored(theme::TextPrimary, "%s", title);
    ImGui::Separator();
    ImGui::TextColored(theme::TextMuted, "%s", hint);

    const QString language = languageFor(kind);
    const std::vector<const CatalogModel*> models = kind == ModelKind::Translator
        ? models_for(kind, "")
        : models_for(kind, language.toStdString());
    QString selected = choiceFor(kind);
    QStringList ids{QStringLiteral("auto")};
    QStringList labels{QStringLiteral("自动")};
    for (const CatalogModel* model : models) {
        ids.push_back(toQString(model->id));
        QString label = toQString(model->name);
        if (!manager_.installed(*model)) {
            label += QStringLiteral("（未下载，%1）").arg(megabytes(model->download_bytes()));
        }
        labels.push_back(label);
    }
    int index = static_cast<int>(ids.indexOf(selected));
    if (index < 0) index = 0;

    rowLabel("模型");
    if (ImGui::BeginCombo("##model", labels[index].toUtf8().constData())) {
        for (int item = 0; item < ids.size(); ++item) {
            const bool active = item == index;
            if (ImGui::Selectable(labels[item].toUtf8().constData(), active)) {
                index = item;
                selected = ids[item];
                storeChoice(kind, selected);
            }
            if (active) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const CatalogModel* model = find_model(selected.toStdString());
    const bool downloading = !selected.isEmpty() && manager_.downloading(selected);
    const QString custom = manager_.customPaths().value(selected);
    const bool imported = !custom.isEmpty();
    QString status;
    if (model == nullptr) {
        const CatalogModel* best = nullptr;
        for (const CatalogModel* candidate : models) {
            if (manager_.installed(*candidate)) {
                best = candidate;
                break;
            }
        }
        status = best != nullptr
            ? QStringLiteral("将使用：%1").arg(toQString(best->name))
            : QStringLiteral("还没有可用模型，请选择一个下载");
    } else if (imported) {
        status = QStringLiteral("已导入：%1").arg(custom);
    } else if (manager_.installed(*model)) {
        status = model->bundled ? QStringLiteral("已内置") : QStringLiteral("已下载");
    } else {
        status = QStringLiteral("需要下载 %1").arg(megabytes(model->download_bytes()));
    }
    ImGui::TextColored(theme::TextMuted, "%s", status.toUtf8().constData());

    if (model != nullptr) {
        const bool installed = manager_.installed(*model);
        if (downloading) {
            if (ImGui::Button("取消下载")) manager_.cancel(selected);
        } else if (!installed) {
            if (ImGui::Button("下载")) manager_.download(selected);
            ImGui::SameLine();
            if (ImGui::Button("浏览器下载")) {
                for (const ModelPart* part : downloadable_parts(*model)) {
                    ShellExecuteW(nullptr, L"open", toWide(toQString(part->url)).c_str(),
                                  nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
        }
        if (!downloading && model->engine != ModelEngine::SystemVoice) {
            ImGui::SameLine();
            if (ImGui::Button(imported ? "重新导入..." : "导入...")) {
                importModel(kind, *model);
            }
        }
        const bool removable = installed && !downloading && (!model->bundled || imported);
        if (removable) {
            ImGui::SameLine();
            if (ImGui::Button(imported ? "移除导入" : "删除")) {
                if (imported) {
                    manager_.clearCustomPath(selected);
                    state_.draft.model_choices.custom_paths.remove(selected);
                } else {
                    manager_.remove(*model);
                }
                saveChoices();
            }
            if (!imported) {
                ImGui::SameLine();
                ImGui::TextColored(theme::TextMuted, "删除后需重新下载");
            }
        }
        if (downloading) {
            const int percent = progress_.value(selected, 0);
            ImGui::ProgressBar(percent / 100.0F, ImVec2(-FLT_MIN, 6.0F), "");
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::Spacing();
}

void ModelsUi::draw() {
    drawDirectoryCard();
    drawModelCard(ModelKind::Recognizer, "语音识别",
                  "SenseVoice 支持中、英、粤、日、韩；其他语言需要 Whisper。");
    drawModelCard(ModelKind::Translator, "翻译",
                  "更大的模型对不常见的语言翻译得更好，但更慢、占用更多内存。");
    drawModelCard(ModelKind::Voice, "朗读声音",
                  "Kokoro 说中文和英文；其他语言用 Piper 声音，每种语言单独下载。");
    if (!message_.empty()) {
        ImGui::TextColored(theme::Danger, "%s", message_.c_str());
        if (ImGui::Button("清除提示")) message_.clear();
    }
}

} // namespace sa::imgui_ui
