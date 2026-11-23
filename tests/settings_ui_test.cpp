#include "ui/settings_test_hooks.h"
#include "model_manager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QWidget>

#include <array>
#include <cstdio>
#include <tuple>

using namespace sa::settings;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

bool widgetMatchesType(const QWidget* widget, const QString& type) {
    if (type == QStringLiteral("combobox")) return qobject_cast<const QComboBox*>(widget) != nullptr;
    if (type == QStringLiteral("checkbox")) return qobject_cast<const QCheckBox*>(widget) != nullptr;
    if (type == QStringLiteral("slider")) return qobject_cast<const QSlider*>(widget) != nullptr;
    if (type == QStringLiteral("spinbox")) return qobject_cast<const QSpinBox*>(widget) != nullptr;
    if (type == QStringLiteral("keysequence")) {
        return qobject_cast<const QKeySequenceEdit*>(widget) != nullptr;
    }
    if (type == QStringLiteral("lineedit")) return qobject_cast<const QLineEdit*>(widget) != nullptr;
    if (type == QStringLiteral("button")) return qobject_cast<const QPushButton*>(widget) != nullptr;
    if (type == QStringLiteral("table")) return qobject_cast<const QTableWidget*>(widget) != nullptr;
    if (type == QStringLiteral("label")) return qobject_cast<const QLabel*>(widget) != nullptr;
    if (type == QStringLiteral("progress")) {
        return qobject_cast<const QProgressBar*>(widget) != nullptr;
    }
    return false;
}

QString inventoryPath() {
    for (const char* candidate : {"tests/fixtures/settings_inventory.json",
                                  "../tests/fixtures/settings_inventory.json",
                                  "../../tests/fixtures/settings_inventory.json"}) {
        const QString path = QString::fromLatin1(candidate);
        if (QFileInfo::exists(path)) return path;
    }
    return {};
}

void testInventory(QWidget* dialog) {
    const QString path = inventoryPath();
    check(!path.isEmpty(), "settings inventory fixture found");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        check(false, "settings inventory is readable");
        return;
    }
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    check(error.error == QJsonParseError::NoError, "settings inventory parses");
    if (!document.isObject()) return;

    const QJsonObject root = document.object();
    check(root.value(QStringLiteral("version")).toInt() == 1, "settings inventory version is 1");
    const QJsonArray pages = root.value(QStringLiteral("pages")).toArray();
    check(pages.size() == 8, "settings inventory lists eight pages");
    for (const QJsonValue& page_value : pages) {
        const QJsonObject page = page_value.toObject();
        const QString page_id = page.value(QStringLiteral("id")).toString();
        if (dialog->findChild<QWidget*>(page_id) == nullptr) {
            std::printf("FAIL: inventory page %s is missing\n", page_id.toUtf8().constData());
            ++failures;
        }
        for (const QJsonValue& control_value : page.value(QStringLiteral("controls")).toArray()) {
            const QJsonObject control = control_value.toObject();
            const QString id = control.value(QStringLiteral("id")).toString();
            const QString type = control.value(QStringLiteral("type")).toString();
            QWidget* widget = dialog->findChild<QWidget*>(id);
            if (widget == nullptr) {
                std::printf("FAIL: inventory control %s is missing\n", id.toUtf8().constData());
                ++failures;
                continue;
            }
            if (!widgetMatchesType(widget, type)) {
                std::printf("FAIL: inventory control %s has wrong type %s\n",
                            id.toUtf8().constData(), type.toUtf8().constData());
                ++failures;
            }
        }
    }
}

void testDraftRoundtrip(ModelManager& model_manager) {
    SettingsSnapshot snapshot;
    snapshot.vad = VadSettings{900, 60, -55, 4};
    snapshot.compute = ComputePolicy::Gpu;
    snapshot.shortcut = QStringLiteral("F8");
    snapshot.mode = TextMode::Raw;
    snapshot.trigger = HotkeyTrigger::Toggle;
    snapshot.destination = ResultDestination::CopyOnly;
    snapshot.popup_content = ResultContent::Both;
    snapshot.clipboard_content = ResultContent::Translation;
    snapshot.paste_delay_ms = 250;
    snapshot.clipboard_ttl_seconds = 60;
    snapshot.startup_enabled = true;
    snapshot.mute_while_recording = true;
    snapshot.translation.enabled = true;
    snapshot.translation.direction = TranslationDirection::EnToZh;
    snapshot.translation.device = ComputePolicy::Cpu;
    snapshot.translation.separator = QStringLiteral(" | ");
    snapshot.speech.enabled = true;
    snapshot.speech.device_id = QStringLiteral("device-1");
    snapshot.speech.monitor = true;
    snapshot.speech.voice_zh = 5;
    snapshot.speech.voice_en = 2;
    snapshot.speech.speed_percent = 120;
    snapshot.speech.live = true;
    HotwordEntry hotword;
    hotword.phrase = "你好";
    hotword.aliases = {"你 好", "您好"};
    hotword.enabled = true;
    hotword.hits = 7;
    hotword.boost = 4.5F;
    hotword.keep_untranslated = true;
    snapshot.hotwords = {hotword};
    snapshot.model_choices.translator = QStringLiteral("auto");

    QWidget* dialog =
        createSettingsDialogForTest(snapshot, SettingsEnvironment{}, &model_manager);
    check(dialog != nullptr, "roundtrip dialog created");
    if (dialog == nullptr) return;

    const SettingsDraft draft = settingsDraftForTest(dialog);
    check(draft.vad.endpoint_ms == 900, "draft endpoint");
    check(draft.vad.threshold_percent == 60, "draft threshold");
    check(draft.vad.minimum_db == -55, "draft minimum db");
    check(draft.vad.snr_db == 4, "draft snr");
    check(draft.compute == ComputePolicy::Gpu, "draft compute policy");
    check(draft.shortcut == QStringLiteral("F8"), "draft shortcut");
    check(draft.mode == TextMode::Raw, "draft text mode");
    check(draft.trigger == HotkeyTrigger::Toggle, "draft trigger");
    check(draft.destination == ResultDestination::CopyOnly, "draft destination");
    check(draft.popup_content == ResultContent::Both, "draft popup content");
    check(draft.clipboard_content == ResultContent::Translation, "draft clipboard content");
    check(draft.paste_delay_ms == 250, "draft paste delay");
    check(draft.clipboard_ttl_seconds == 60, "draft clipboard ttl");
    check(draft.startup_enabled, "draft startup");
    check(draft.mute_while_recording, "draft mute");
    check(draft.translation.enabled, "draft translation enabled");
    check(draft.translation.direction.source == "en", "draft translation source");
    check(draft.translation.direction.target == "zh", "draft translation target");
    check(draft.translation.device == ComputePolicy::Cpu, "draft translation device");
    check(draft.translation.separator == QStringLiteral(" | "), "draft separator");
    check(draft.speech.enabled, "draft speech enabled");
    check(draft.speech.device_id == QStringLiteral("device-1"), "draft speech device");
    check(draft.speech.monitor, "draft speech monitor");
    check(draft.speech.voice_zh == 5, "draft voice zh");
    check(draft.speech.voice_en == 2, "draft voice en");
    check(draft.speech.speed_percent == 120, "draft speech speed");
    check(draft.speech.live, "draft live speech");
    check(draft.model_choices.translator == QStringLiteral("auto"), "draft translator");
    check(draft.hotwords.size() == 1, "draft hotword count");
    if (draft.hotwords.size() == 1) {
        check(draft.hotwords[0].phrase == "你好", "draft hotword phrase");
        check(draft.hotwords[0].aliases.size() == 2, "draft hotword aliases");
        if (draft.hotwords[0].aliases.size() == 2) {
            check(draft.hotwords[0].aliases[0] == "你 好", "draft first alias");
            check(draft.hotwords[0].aliases[1] == "您好", "draft second alias");
        }
        check(draft.hotwords[0].enabled, "draft hotword enabled");
        check(draft.hotwords[0].hits == 7, "draft hotword hits");
        check(draft.hotwords[0].boost == 4.5F, "draft hotword boost");
        check(draft.hotwords[0].keep_untranslated, "draft hotword keep");
    }
    delete dialog;
}

void testEnglishUi(ModelManager& model_manager, QApplication& application) {
    QString qm_path;
    for (const char* candidate : {"speakanything_en.qm",
                                  "../speakanything_en.qm",
                                  "../../speakanything_en.qm"}) {
        const QString path = QString::fromLatin1(candidate);
        if (QFileInfo::exists(path)) {
            qm_path = path;
            break;
        }
    }
    check(!qm_path.isEmpty(), "english translation catalogue found");
    if (qm_path.isEmpty()) return;

    QTranslator translator;
    const bool loaded = translator.load(qm_path);
    check(loaded, "english translation catalogue loads");
    if (!loaded) return;
    application.installTranslator(&translator);

    QWidget* dialog = createSettingsDialogForTest(
        defaultSettingsSnapshot(), SettingsEnvironment{}, &model_manager);
    check(dialog != nullptr, "english dialog created");
    if (dialog != nullptr) {
        check(dialog->windowTitle() == QStringLiteral("Voice Input Settings"),
              "english window title");
        QString input_title;
        auto* sidebar = dialog->findChild<QListWidget*>(QStringLiteral("settingsSidebar"));
        if (sidebar != nullptr) {
            for (int row = 0; row < sidebar->count(); ++row) {
                const QListWidgetItem* item = sidebar->item(row);
                if (item->data(Qt::UserRole).isValid() &&
                    item->data(Qt::UserRole).toInt() == 0) {
                    input_title = item->text();
                    break;
                }
            }
        }
        check(input_title == QStringLiteral("Input"), "english sidebar label");
        auto* startup = dialog->findChild<QCheckBox*>(QStringLiteral("input_startup"));
        check(startup != nullptr && startup->text() == QStringLiteral("Launch at login"),
              "english startup label");
        auto* add_hotword = dialog->findChild<QPushButton*>(QStringLiteral("hotwords_add"));
        check(add_hotword != nullptr && add_hotword->text() == QStringLiteral("Add hotword"),
              "english hotword button label");
        delete dialog;
    }

    application.removeTranslator(&translator);
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication application(argc, argv);

    QTemporaryDir directory;
    check(directory.isValid(), "temporary directory");
    if (!directory.isValid()) return 1;

    ModelManager model_manager(QDir(directory.path()).filePath(QStringLiteral("models")));
    QWidget* dialog = createSettingsDialogForTest(
        defaultSettingsSnapshot(), SettingsEnvironment{}, &model_manager);
    check(dialog != nullptr, "dialog created");
    if (dialog == nullptr) return 1;

    check(settingsPageCountForTest(dialog) == 8, "eight settings pages");
    if (settingsPageCountForTest(dialog) != 8) {
        delete dialog;
        return 1;
    }

    const std::array<const char*, 8> page_names = {
        "page_input", "page_output", "page_appearance", "page_translation",
        "page_speech", "page_hotwords", "page_models", "page_vad"};
    for (const char* name : page_names) {
        check(dialog->findChild<QWidget*>(QString::fromLatin1(name)) != nullptr,
              "page object name exists");
    }

    auto* sidebar = dialog->findChild<QListWidget*>(QStringLiteral("settingsSidebar"));
    check(sidebar != nullptr, "settings sidebar exists");
    if (sidebar != nullptr) {
        check((sidebar->focusPolicy() & Qt::TabFocus) != 0, "sidebar accepts tab focus");
        check(!sidebar->accessibleName().isEmpty(), "sidebar has an accessible name");
        settingsSelectPageForTest(dialog, 0);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(sidebar, &down);
        check(settingsCurrentPageForTest(dialog) == 1, "down moves to the next page");
        QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
        QApplication::sendEvent(sidebar, &end);
        check(settingsCurrentPageForTest(dialog) == 2, "end selects the last page");
        QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(sidebar, &up);
        check(settingsCurrentPageForTest(dialog) == 6, "up moves to the previous page");
        QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::sendEvent(sidebar, &home);
        check(settingsCurrentPageForTest(dialog) == 0, "home selects the first page");
    }
    testInventory(dialog);

    const std::array<const char*, 8> appearance_controls = {
        "appearance_theme_preset", "appearance_color_background",
        "appearance_color_text", "appearance_color_accent",
        "appearance_font_family", "appearance_font_size",
        "appearance_linger", "appearance_pinned"};
    for (const char* name : appearance_controls) {
        if (dialog->findChild<QWidget*>(QString::fromLatin1(name)) == nullptr) {
            std::printf("FAIL: missing appearance control %s\n", name);
            ++failures;
        }
    }

    const std::array<const char*, 5> translation_controls = {
        "translation_enabled", "translation_source", "translation_target",
        "translation_device", "translation_separator"};
    for (const char* name : translation_controls) {
        if (dialog->findChild<QWidget*>(QString::fromLatin1(name)) == nullptr) {
            std::printf("FAIL: missing translation control %s\n", name);
            ++failures;
        }
    }

    auto* hotword_table = dialog->findChild<QTableWidget*>(QStringLiteral("hotwords_table"));
    auto* hotword_add = dialog->findChild<QPushButton*>(QStringLiteral("hotwords_add"));
    auto* hotword_remove = dialog->findChild<QPushButton*>(QStringLiteral("hotwords_remove"));
    auto* hotword_empty = dialog->findChild<QLabel*>(QStringLiteral("hotwords_empty"));
    check(hotword_table != nullptr, "hotword table object name exists");
    check(hotword_add != nullptr, "hotword add button exists");
    check(hotword_remove != nullptr, "hotword remove button exists");
    check(hotword_empty != nullptr, "hotword empty state exists");
    if (hotword_table != nullptr) {
        check(hotword_table->columnCount() == 6, "hotword table has six columns");
    }
    if (hotword_table != nullptr && hotword_add != nullptr && hotword_remove != nullptr &&
        hotword_empty != nullptr) {
        check(!hotword_empty->isHidden(), "empty hotword state is visible");
        hotword_add->click();
        application.processEvents();
        check(hotword_table->rowCount() == 1, "add hotword appends a row");
        check(hotword_empty->isHidden(), "empty hotword state hides after add");
        hotword_remove->click();
        application.processEvents();
        check(hotword_table->rowCount() == 0, "remove hotword drops the row");
        check(!hotword_empty->isHidden(), "empty hotword state returns after remove");
    }

    QWidget* vad_page = dialog->findChild<QWidget*>(QStringLiteral("page_vad"));
    check(vad_page != nullptr, "vad page exists");
    if (vad_page != nullptr) {
        check(vad_page->findChildren<QWidget*>(QStringLiteral("card")).size() == 2,
              "vad page has sentence and noise cards");
        const std::array<std::tuple<const char*, int, int>, 4> vad_sliders = {{
            {"vad_endpoint", 300, 2000},
            {"vad_threshold", 5, 95},
            {"vad_minimum_db", -80, -20},
            {"vad_snr", 0, 20},
        }};
        for (const auto& [name, minimum, maximum] : vad_sliders) {
            QSlider* slider = dialog->findChild<QSlider*>(QString::fromLatin1(name));
            if (slider == nullptr) {
                std::printf("FAIL: missing vad slider %s\n", name);
                ++failures;
                continue;
            }
            if (slider->minimum() != minimum || slider->maximum() != maximum) {
                std::printf("FAIL: vad slider %s has unexpected range\n", name);
                ++failures;
            }
        }
    }

    for (const char* name : {"models_directory_label", "models_directory_choose",
                             "models_directory_reset"}) {
        if (dialog->findChild<QWidget*>(QString::fromLatin1(name)) == nullptr) {
            std::printf("FAIL: missing model directory control %s\n", name);
            ++failures;
        }
    }
    for (const char* kind : {"recognizer", "translator", "voice"}) {
        for (const char* suffix : {"_combo", "_state", "_status", "_download", "_browser",
                                   "_import", "_remove", "_progress"}) {
            const QString name = QStringLiteral("models_") + QString::fromLatin1(kind) +
                                 QString::fromLatin1(suffix);
            if (dialog->findChild<QWidget*>(name) == nullptr) {
                std::printf("FAIL: missing model control %s\n", name.toUtf8().constData());
                ++failures;
            }
        }
    }

    for (const char* page_name : {"page_appearance", "page_translation", "page_models"}) {
        QWidget* page = dialog->findChild<QWidget*>(QString::fromLatin1(page_name));
        if (page == nullptr) continue;
        const QList<QWidget*> rows = page->findChildren<QWidget*>(QStringLiteral("row"));
        check(!rows.isEmpty(), "migrated page has rows");
        for (QWidget* row : rows) {
            if (row->minimumHeight() < 44) {
                std::printf("FAIL: row in %s is shorter than 44 px\n", page_name);
                ++failures;
            }
        }
    }

    for (int page = 0; page < 8; ++page) {
        settingsSelectPageForTest(dialog, page);
        application.processEvents();
        const QPixmap shot = dialog->grab();
        check(!shot.isNull(), "page renders");
        check(shot.width() > 0 && shot.height() > 0, "page has a size");
    }

    dialog->resize(760, 540);
    application.processEvents();
    check(dialog->width() <= 760, "dialog shrinks to 760");
    for (int page = 0; page < 8; ++page) {
        settingsSelectPageForTest(dialog, page);
        application.processEvents();
        for (QScrollArea* area : dialog->findChildren<QScrollArea*>()) {
            check(area->horizontalScrollBar()->maximum() == 0,
                  "page has no horizontal overflow at 760 wide");
        }
    }

    delete dialog;
    testDraftRoundtrip(model_manager);
    testEnglishUi(model_manager, application);

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("settings-ui-test: all checks passed\n");
    return 0;
}
