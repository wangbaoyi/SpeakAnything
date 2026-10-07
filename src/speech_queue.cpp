#include "speech_queue.h"

#include <QMetaObject>
#include <QPointer>

#ifdef __APPLE__
#include "macos/macos_system_voice.h"
#endif

#include <cstring>
#include <utility>
#include <vector>

SpeechQueue::SpeechQueue(QString helper_path, QString model_directory,
                         ErrorHandler on_error, StateHandler on_state, QObject* parent)
    : QObject(parent),
      helper_path_(std::move(helper_path)),
      model_directory_(std::move(model_directory)),
      on_error_(std::move(on_error)),
      on_state_(std::move(on_state)) {}

SpeechQueue::~SpeechQueue() {
    device_player_.reset();
    monitor_player_.reset();
    stopHelper();
}

void SpeechQueue::setActive(bool active) {
    if (!active) {
        clear();
        stopHelper();
        setState(State::Off);
        return;
    }
    if (helper_ != nullptr || (systemVoice() && state_ == State::Ready)) return;
    if (systemVoice()) {
        // Synthesized in this process; nothing to load.
        setState(State::Ready);
        return;
    }
    buffer_.clear();
    synthesizing_ = false;
    discard_current_ = false;
    helper_ = new QProcess(this);
    helper_->setProcessChannelMode(QProcess::SeparateChannels);
    helper_->setStandardErrorFile(QProcess::nullDevice());
    QObject::connect(helper_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    QObject::connect(helper_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (helper_ == nullptr) return;
        const QString error = helper_->errorString();
        stopHelper();
        setState(State::Failed, error);
    });
    QObject::connect(helper_, &QProcess::finished, this, [this](int, QProcess::ExitStatus) {
        if (helper_ == nullptr) return;
        stopHelper();
        if (state_ != State::Failed) setState(State::Failed, QStringLiteral("朗读进程意外退出"));
    });
    setState(State::Loading);
    QStringList arguments = {model_directory_, QStringLiteral("4")};
    if (!language_.isEmpty()) arguments.append(language_);
    helper_->start(helper_path_, arguments);
}

void SpeechQueue::setModel(const QString& model_directory, const QString& language) {
    if (model_directory == model_directory_ && language == language_) return;
    const bool was_active = helper_ != nullptr || (systemVoice() && state_ == State::Ready);
    clear();
    stopHelper();
    model_directory_ = model_directory;
    language_ = language;
    if (!was_active) return;
    setState(State::Off);
    setActive(true);
}

void SpeechQueue::setOutput(const QString& device_id, bool monitor) {
    if (device_id == device_id_ && monitor == monitor_ && (device_player_ || device_id.isEmpty())) return;
    device_id_ = device_id;
    monitor_ = monitor;
    device_player_.reset();
    monitor_player_.reset();
    if (!device_id_.isEmpty()) {
        QPointer<SpeechQueue> self(this);
        device_player_ = std::make_unique<SpeechPlayer>(
            device_id_.toStdWString(), [self](bool ok, const std::string&) {
                if (ok) return;
                QMetaObject::invokeMethod(self, [self] {
                    if (self && self->on_error_) self->on_error_(QStringLiteral("朗读设备不可用"));
                }, Qt::QueuedConnection);
            });
    }
    // Default playback device; it is muted along with everything else while recording.
    if (monitor_) monitor_player_ = std::make_unique<SpeechPlayer>(std::wstring{}, nullptr);
}

void SpeechQueue::speak(const QString& text, int voice, double speed) {
    QString line = text.simplified();
    if (line.isEmpty()) return;
    if (device_player_ == nullptr) {
        if (on_error_) on_error_(QStringLiteral("未选择朗读设备"));
        return;
    }
    if (state_ != State::Loading && state_ != State::Ready) {
        if (on_error_) on_error_(QStringLiteral("朗读模型未就绪"));
        return;
    }
    pending_.push_back(Request{std::move(line), voice, speed});
    pump();
}

void SpeechQueue::clear() {
    pending_.clear();
    if (synthesizing_) discard_current_ = true;
    if (device_player_) device_player_->clear();
    if (monitor_player_) monitor_player_->clear();
}

void SpeechQueue::setState(State state, const QString& error) {
    state_ = state;
    if (state != State::Ready && state != State::Loading) pending_.clear();
    if (on_state_) on_state_(state, error);
}

void SpeechQueue::play(std::vector<float> samples, int sample_rate) {
    if (monitor_player_) monitor_player_->play(samples, sample_rate);
    if (device_player_) device_player_->play(std::move(samples), sample_rate);
}

void SpeechQueue::pump() {
    if (state_ != State::Ready || synthesizing_ || pending_.empty()) return;
    if (!systemVoice() && helper_ == nullptr) return;
    const Request request = std::move(pending_.front());
    pending_.pop_front();
    synthesizing_ = true;
    discard_current_ = false;
#ifdef __APPLE__
    if (systemVoice()) {
        QPointer<SpeechQueue> self(this);
        macos_system_voice_synthesize(
            request.text.toStdString(), language_.toStdString(), request.speed,
            [self](std::vector<float> samples, int sample_rate, bool ok) {
                QMetaObject::invokeMethod(self, [self, samples = std::move(samples), sample_rate, ok]() mutable {
                    if (!self) return;
                    self->synthesizing_ = false;
                    if (!self->discard_current_) {
                        if (ok) self->play(std::move(samples), sample_rate);
                        else if (self->on_error_) self->on_error_(QStringLiteral("朗读合成失败"));
                    }
                    self->pump();
                }, Qt::QueuedConnection);
            });
        return;
    }
#endif
    const QByteArray line = QByteArray::number(request.voice) + '\t' +
        QByteArray::number(request.speed, 'f', 2) + '\t' + request.text.toUtf8() + '\n';
    helper_->write(line);
}

void SpeechQueue::readOutput() {
    if (helper_ == nullptr) return;
    buffer_ += helper_->readAllStandardOutput();
    for (;;) {
        const qsizetype newline = buffer_.indexOf('\n');
        if (newline < 0) return;
        const QByteArray header = buffer_.left(newline);
        if (header.startsWith("READY ")) {
            buffer_.remove(0, newline + 1);
            sample_rate_ = header.mid(6).trimmed().toInt();
            if (sample_rate_ <= 0) sample_rate_ = 24000;
            setState(State::Ready);
            pump();
        } else if (header.startsWith("ERROR ")) {
            buffer_.remove(0, newline + 1);
            const QString message = QString::fromUtf8(header.mid(6));
            if (state_ == State::Loading) {
                stopHelper();
                setState(State::Failed, message);
                return;
            }
            synthesizing_ = false;
            if (!discard_current_ && on_error_) on_error_(QStringLiteral("朗读合成失败"));
            pump();
        } else if (header.startsWith("AUDIO ")) {
            const qsizetype count = header.mid(6).trimmed().toLongLong();
            const qsizetype bytes = count * static_cast<qsizetype>(sizeof(float));
            if (buffer_.size() < newline + 1 + bytes) return;
            std::vector<float> samples(static_cast<std::size_t>(count));
            std::memcpy(samples.data(), buffer_.constData() + newline + 1, static_cast<std::size_t>(bytes));
            buffer_.remove(0, newline + 1 + bytes);
            synthesizing_ = false;
            if (!discard_current_) play(std::move(samples), sample_rate_);
            pump();
        } else {
            buffer_.remove(0, newline + 1);
        }
    }
}

void SpeechQueue::stopHelper() {
    if (helper_ == nullptr) return;
    QProcess* helper = std::exchange(helper_, nullptr);
    helper->disconnect(this);
    helper->closeWriteChannel();
    if (!helper->waitForFinished(1500)) {
        helper->kill();
        helper->waitForFinished(1000);
    }
    helper->deleteLater();
    synthesizing_ = false;
    buffer_.clear();
}
