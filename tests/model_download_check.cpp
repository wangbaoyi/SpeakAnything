// Manual check: downloads one catalogue model into the per-user models
// directory and reports where it landed.
//   model-download-check piper-ro

#include "model_manager.h"

#include <QCoreApplication>
#include <QDir>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: model-download-check <model id>\n");
        return 2;
    }
    const CatalogModel* model = find_model(argv[1]);
    if (model == nullptr) {
        std::fprintf(stderr, "unknown model %s\n", argv[1]);
        return 2;
    }
    // Run from the repository root so the bundled models are found in models/.
    ModelManager manager(QDir::current().filePath(QStringLiteral("models")));
    manager.addProgressHandler([](const QString&, qint64 received, qint64 total) {
        std::fprintf(stderr, "\r%lld / %lld MB", received / 1'000'000, total / 1'000'000);
    });
    int status = 1;
    manager.addFinishedHandler([&](const QString&, bool ok, const QString& error) {
        std::fprintf(stderr, "\n%s %s\n", ok ? "ok" : "failed:", qPrintable(error));
        if (ok) std::printf("%s\n", qPrintable(manager.locate(*model)));
        status = ok && manager.installed(*model) ? 0 : 1;
        application.quit();
    });
    manager.download(QString::fromUtf8(argv[1]));
    application.exec();
    return status;
}
