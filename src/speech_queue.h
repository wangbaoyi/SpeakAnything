#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>

#include <deque>
#include <functional>
#include <memory>

#include "speech_player.h"

// The 朗读队列: translated text waiting to be spoken, in session order.
// Synthesis runs in sensevoice-tts.exe (docs/adr/0002); playback goes to the
// 朗读设备 and, when 监听 is on, also to the default playback device.
// Everything here lives on the UI thread.
class SpeechQueue final : public QObject {
public:
    enum class State { Off, Loading, Ready, Failed };
    using ErrorHandler = std::function<void(const QString& message)>;
    using StateHandler = std::function<void(State state, const QString& error)>;

    SpeechQueue(QString helper_path, QString model_directory,
                ErrorHandler on_error, StateHandler on_state, QObject* parent = nullptr);
    ~SpeechQueue() override;

    // Starts the helper (loading the model) or stops it.
    void setActive(bool active);
    // Empty device id = nothing selected; speaking then reports an error.
    void setOutput(const QString& device_id, bool monitor);
    void speak(const QString& text, int voice, double speed);
    // Empties the queue and stops what is playing.
    void clear();
    [[nodiscard]] State state() const { return state_; }

private:
    struct Request {
        QString text;
        int voice = 0;
        double speed = 1.0;
    };

    void setState(State state, const QString& error = {});
    void readOutput();
    void pump();
    void stopHelper();

    QString helper_path_;
    QString model_directory_;
    ErrorHandler on_error_;
    StateHandler on_state_;
    QProcess* helper_ = nullptr;
    QByteArray buffer_;
    State state_ = State::Off;
    int sample_rate_ = 24000;
    bool synthesizing_ = false;
    // Results of a request issued before clear() are dropped.
    bool discard_current_ = false;
    std::deque<Request> pending_;
    QString device_id_;
    bool monitor_ = false;
    std::unique_ptr<SpeechPlayer> device_player_;
    std::unique_ptr<SpeechPlayer> monitor_player_;
};
