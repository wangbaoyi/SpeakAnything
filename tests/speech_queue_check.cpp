// Manual check: speaks two lines through SpeechQueue into the named playback
// device. Usage: speech-queue-check <device name substring>
#include "speech_player.h"
#include "speech_queue.h"

#include <QCoreApplication>
#include <QDir>
#include <QTimer>

#include <cstdio>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString wanted = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("CABLE Input");
    QString device_id;
    for (const AudioOutputDevice& device : list_audio_output_devices()) {
        if (QString::fromStdWString(device.name).contains(wanted)) device_id = QString::fromStdWString(device.id);
    }
    if (device_id.isEmpty()) {
        std::fprintf(stderr, "device not found\n");
        return 1;
    }
    const QString dir = QCoreApplication::applicationDirPath();
    SpeechQueue queue(
        QDir(dir).filePath(QStringLiteral("sensevoice-tts.exe")),
        QDir(dir).filePath(QStringLiteral("models/kokoro-multi-lang-v1_1")),
        [](const QString& message) { std::fprintf(stderr, "error: %s\n", message.toUtf8().constData()); },
        [](SpeechQueue::State state, const QString& error) {
            std::fprintf(stderr, "state %d %s\n", static_cast<int>(state), error.toUtf8().constData());
        });
    queue.setOutput(device_id, false);
    queue.setActive(true);
    queue.speak(QStringLiteral("We have a meeting at three tomorrow afternoon."), 0, 1.0);
    queue.speak(QStringLiteral("明天下午三点开会。"), 3, 1.0);
    QTimer::singleShot(14000, &app, &QCoreApplication::quit);
    return app.exec();
}
