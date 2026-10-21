#include "model_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <iostream>

namespace {

bool touch(const QString& path) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    return file.write("x") == 1;
}

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __LINE__)

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    QTemporaryDir bundled;
    QTemporaryDir custom;
    QTemporaryDir other;
    CHECK(bundled.isValid() && custom.isValid() && other.isValid());

    ModelManager manager(bundled.path());

    // The categorised layout: modelspt/recognizer/....
    const CatalogModel* sensevoice = find_model("sensevoice-gguf");
    CHECK(sensevoice != nullptr);
    if (sensevoice != nullptr) {
        const QString categorised =
            bundled.filePath(QStringLiteral("recognizer/sensevoice-small-q8.gguf"));
        CHECK(touch(categorised));
        CHECK(manager.installed(*sensevoice));
        CHECK(manager.locate(*sensevoice) == categorised);
    }

    // Flat downloads: Whisper keeps only the file name at the root.
    const CatalogModel* turbo = find_model("whisper-turbo");
    CHECK(turbo != nullptr);
    if (turbo != nullptr) {
        CHECK(touch(bundled.filePath(QStringLiteral("ggml-large-v3-turbo-q5_0.bin"))));
        CHECK(manager.installed(*turbo));
        CHECK(manager.locate(*turbo) ==
              bundled.filePath(QStringLiteral("ggml-large-v3-turbo-q5_0.bin")));
    }

    // A user-selected directory is searched after modelspt.
    const CatalogModel* small = find_model("whisper-small");
    CHECK(small != nullptr);
    const QString small_in_custom =
        custom.filePath(QStringLiteral("whisper/ggml-small-q5_1.bin"));
    if (small != nullptr) {
        CHECK(touch(small_in_custom));
        manager.setCustomDirectory(custom.path());
        CHECK(manager.locate(*small) == small_in_custom);
    }

    // A per-model import overrides every search root.
    const QString imported = other.filePath(QStringLiteral("my-whisper.bin"));
    CHECK(touch(imported));
    if (small != nullptr) {
        manager.setCustomPath(QStringLiteral("whisper-small"), imported);
        CHECK(manager.locate(*small) == imported);
        CHECK(manager.customPaths().value(QStringLiteral("whisper-small")) == imported);

        manager.clearCustomPath(QStringLiteral("whisper-small"));
        CHECK(manager.locate(*small) == small_in_custom);

        // Removing an import unregisters it and never deletes the user's file.
        manager.setCustomPath(QStringLiteral("whisper-small"), imported);
        CHECK(manager.remove(*small));
        CHECK(QFileInfo::exists(imported));
        CHECK(!manager.customPaths().contains(QStringLiteral("whisper-small")));
    }

    // Directory models resolve to their folder, including imported folders.
    const CatalogModel* kokoro = find_model("kokoro-v1.1");
    CHECK(kokoro != nullptr);
    if (kokoro != nullptr) {
        CHECK(touch(bundled.filePath(QStringLiteral("voice/kokoro-multi-lang-v1_1/model.onnx"))));
        CHECK(manager.locateDirectory(*kokoro) ==
              bundled.filePath(QStringLiteral("voice/kokoro-multi-lang-v1_1")));

        const QString kokoro_import = custom.filePath(QStringLiteral("my-kokoro"));
        CHECK(touch(QDir(kokoro_import).filePath(QStringLiteral("model.onnx"))));
        manager.setCustomPath(QStringLiteral("kokoro-v1.1"), kokoro_import);
        CHECK(manager.installed(*kokoro));
        CHECK(manager.locateDirectory(*kokoro) == kokoro_import);
    }

    if (failures == 0) {
        std::cout << "model-manager-test passed\n";
        return 0;
    }
    std::cerr << failures << " checks failed\n";
    return 1;
}
