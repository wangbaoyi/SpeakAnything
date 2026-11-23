#include "imgui_ui/recording_controller.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace sa::imgui_ui {
namespace {

std::uint64_t nowTick() {
    return static_cast<std::uint64_t>(GetTickCount64());
}

std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0);
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(),
                        size);
    return result;
}

void setClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != nullptr) {
        void* destination = GlobalLock(memory);
        if (destination != nullptr) {
            std::memcpy(destination, text.c_str(), bytes);
            GlobalUnlock(memory);
            SetClipboardData(CF_UNICODETEXT, memory);
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void logTranscript(const char* tag, const std::string& text) {
    static const bool enabled =
        GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_TRANSCRIPT", nullptr, 0) > 0;
    if (!enabled) return;
    std::ofstream log("imgui-transcript.log", std::ios::out | std::ios::app);
    log << tag << ": " << text << '\n';
}

} // namespace

RecordingController::RecordingController() = default;

RecordingController::~RecordingController() {
    shutdown();
}

void RecordingController::initialize(const sa::settings::SettingsDraft& settings,
                                     const std::filesystem::path& models_directory,
                                     const ModelSelection& selection,
                                     std::function<std::filesystem::path()> llm_model_provider) {
    models_directory_ = models_directory;
    llm_model_provider_ = std::move(llm_model_provider);
    selection_ = selection;
    speech_piper_ = selection.voice_piper;
    speech_language_ = selection.speech_language;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings_ = settings;
    }
    init_state_ = InitState::Loading;
    loader_ = std::thread([this, selection] {
        if (selection.recognizer.empty() || selection.vad.empty()) {
            setStatus("未找到识别模型或 VAD 模型");
            init_state_ = InitState::Failed;
            return;
        }

        const unsigned int threads = std::max(2u, std::thread::hardware_concurrency() / 2);
        std::string error;
        vad_ = std::make_unique<FsmnVadEngine>();
        if (!vad_->load(selection.vad.string(), static_cast<int>(threads), error)) {
            setStatus("VAD 加载失败：" + error);
            init_state_ = InitState::Failed;
            return;
        }

        sa::settings::SettingsDraft snapshot;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            snapshot = settings_;
        }
        bool loaded = false;
        if (selection.whisper) {
            auto whisper = std::make_unique<WhisperEngine>();
            loaded = whisper->load(selection.recognizer.string(), selection.language,
                                   static_cast<int>(threads), error, snapshot.compute);
            if (loaded) engine_ = std::move(whisper);
        } else {
            auto sensevoice = std::make_unique<SenseVoiceEngine>();
            loaded = sensevoice->load(selection.recognizer.string(), static_cast<int>(threads),
                                      error, snapshot.compute);
            if (loaded) engine_ = std::move(sensevoice);
        }
        if (!loaded) {
            setStatus("识别模型加载失败：" + error);
            init_state_ = InitState::Failed;
            return;
        }
        text_processor_ = std::make_unique<TextProcessor>();
        text_processor_->set_hotwords(snapshot.hotwords);

        StreamRecognizerConfig config;
        config.endpoint_silence_ms = snapshot.vad.endpoint_ms;
        config.vad_speech_threshold = snapshot.vad.threshold_percent / 100.0F;
        config.vad_minimum_db = static_cast<float>(snapshot.vad.minimum_db);
        config.vad_minimum_snr_db = static_cast<float>(snapshot.vad.snr_db);
        recognizer_ = std::make_unique<StreamRecognizer>(
            *engine_, vad_.get(), config,
            [this](const RecognitionEvent& event) { handleEvent(event); }, text_processor_.get());
        setStatus("就绪 · Ctrl+Alt+Space 开始");
        init_state_ = InitState::Ready;
    });
}

void RecordingController::updateSettings(const sa::settings::SettingsDraft& settings) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = settings;
}

void RecordingController::start() {
    if (init_state_ != InitState::Ready || listening_.load() || busy_.load()) return;
    sa::settings::SettingsDraft settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
    }
    if (text_processor_ != nullptr) text_processor_->set_hotwords(settings.hotwords);
    if (engine_ != nullptr) engine_->set_language(settings.translation.direction.source);
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        translation_session_ = TranslationSession(settings.translation.direction);
        translation_failed_ = false;
        stop_requested_ = false;
        pending_translations_ = 0;
    }
    if (settings.translation.enabled) ensureTranslator();
    if (settings.speech.enabled) ensureSpeech();
    if (speech_queue_ != nullptr) speech_queue_->clear();
    target_ = capture_text_input_target(current_process_id());
    recognizer_->start();
    std::string error;
    if (!capture_.start([this](std::span<const float> samples) {
            if (recognizer_ != nullptr) recognizer_->accept_pcm(samples);
        },
                        error)) {
        setStatus("麦克风打开失败：" + error);
        recognizer_->cancel();
        return;
    }
    listening_ = true;
    busy_ = true;
    last_event_tick_ = nowTick();
    setStatus("正在聆听...");
}

bool RecordingController::startFile(const std::filesystem::path& audio_path, std::string& error) {
    if (init_state_ != InitState::Ready || busy_.load()) {
        error = "模型尚未就绪";
        return false;
    }
    sa::settings::SettingsDraft settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
    }
    if (text_processor_ != nullptr) text_processor_->set_hotwords(settings.hotwords);
    if (engine_ != nullptr) engine_->set_language(settings.translation.direction.source);
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        translation_session_ = TranslationSession(settings.translation.direction);
        translation_failed_ = false;
        stop_requested_ = false;
        pending_translations_ = 0;
    }
    if (settings.translation.enabled) ensureTranslator();
    target_ = capture_text_input_target(current_process_id());

    std::vector<float> samples;
    if (!load_audio_16khz_mono(audio_path, samples, error)) return false;
    recognizer_->start();
    constexpr std::size_t chunk = 1600;
    for (std::size_t offset = 0; offset < samples.size(); offset += chunk) {
        const std::size_t count = std::min(chunk, samples.size() - offset);
        recognizer_->accept_pcm(std::span<const float>(samples.data() + offset, count));
    }
    listening_ = false;
    busy_ = true;
    stop_tick_ = nowTick();
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        stop_requested_ = true;
    }
    setStatus("处理中...");
    recognizer_->finish();
    return true;
}

void RecordingController::stop() {
    if (!listening_.load()) return;
    capture_.stop();
    listening_ = false;
    stop_tick_ = nowTick();
    bool finish_now = false;
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        stop_requested_ = true;
        finish_now = pending_translations_ == 0;
    }
    setStatus("处理中...");
    last_event_tick_ = nowTick();
    if (recognizer_ != nullptr) recognizer_->finish();
    if (finish_now) finishTranslation(false);
}

void RecordingController::toggle() {
    if (listening_.load()) {
        stop();
    } else {
        start();
    }
}

void RecordingController::shutdown() {
    if (started_) return;
    started_ = true;
    capture_.stop();
    if (recognizer_ != nullptr) {
        recognizer_->cancel();
        recognizer_.reset();
    }
    translation_worker_.reset();
    speech_queue_.reset();
    if (loader_.joinable()) loader_.join();
    engine_.reset();
    vad_.reset();
}

bool RecordingController::listening() const {
    return listening_.load();
}

OverlaySnapshot RecordingController::snapshot() const {
    OverlaySnapshot result;
    result.listening = listening_.load();
    result.busy = busy_.load() || init_state_ == InitState::Loading;
    const AudioLevelMetrics metrics = capture_.level_metrics();
    result.level = std::clamp((metrics.input_rms_db + 60.0F) / 60.0F, 0.0F, 1.0F);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result.status = status_;
        result.last_text = last_text_;
    }
    if (!result.listening && !busy_.load() && init_state_ == InitState::Ready &&
        nowTick() - last_event_tick_.load() > 2500) {
        result.status = "就绪 · Ctrl+Alt+Space 开始";
    }
    return result;
}

void RecordingController::setStatus(const std::string& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_ = status;
}

void RecordingController::handleEvent(const RecognitionEvent& event) {
    last_event_tick_ = nowTick();
    switch (event.kind) {
    case RecognitionEventKind::Partial:
        if (!event.text.empty()) setStatus(event.text);
        break;
    case RecognitionEventKind::Final:
        busy_ = false;
        if (!event.text.empty()) {
            sa::settings::SettingsDraft settings;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                last_text_ = event.text;
                status_ = event.text;
                settings = settings_;
            }
            if (settings.translation.enabled && translation_worker_ != nullptr) {
                logTranscript("recognized", event.text);
                std::size_t index = 0;
                {
                    std::lock_guard<std::mutex> lock(translation_mutex_);
                    const auto added = translation_session_.add_sentence(event.text);
                    if (!added.has_value()) break;
                    index = *added;
                    ++pending_translations_;
                    setStatus("翻译中...");
                }
                translation_worker_->translate(index, event.text);
                break;
            }
            {
                logTranscript("recognized", event.text);
                std::lock_guard<std::mutex> lock(queue_mutex_);
                pending_text_.push_back(event.text);
            }
        }
        break;
    case RecognitionEventKind::Error:
        busy_ = false;
        listening_ = false;
        setStatus("识别失败：" + event.error);
        break;
    }
}

void RecordingController::pumpInjection() {
    bool timed_out = false;
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        timed_out = stop_requested_ && pending_translations_ > 0 &&
            nowTick() - stop_tick_.load() > 4000;
    }
    if (timed_out) finishTranslation(true);

    std::deque<std::string> texts;
    std::deque<std::string> speech;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        texts.swap(pending_text_);
        speech.swap(pending_speech_);
    }
    if (texts.empty() && speech.empty()) return;
    sa::settings::SettingsDraft settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
    }
    for (const std::string& text : texts) {
        std::string output = text;
        if (settings.mode == sa::settings::TextMode::Clean && text_processor_ != nullptr) {
            output = text_processor_->process(text).text;
        }
        if (!output.empty()) injectText(output);
    }
    last_inject_tick_ = nowTick();
    if (!speech.empty() && settings.speech.enabled) {
        ensureSpeech();
        if (speech_queue_ != nullptr) {
            const int voice = speech_piper_
                ? 0
                : (settings.translation.direction.target == "zh" ? settings.speech.voice_zh
                                                                 : settings.speech.voice_en);
            const double speed = settings.speech.speed_percent / 100.0;
            for (const std::string& text : speech) {
                speech_queue_->speak(QString::fromStdString(text), voice, speed);
            }
        }
    }
}

void RecordingController::ensureSpeech() {
    if (speech_queue_ != nullptr || selection_.voice_model.empty()) return;
    const std::filesystem::path helper = models_directory_.parent_path() / L"sensevoice-tts.exe";
    speech_queue_ = std::make_unique<SpeechQueue>(
        QString::fromStdWString(helper.wstring()),
        QString::fromStdWString(selection_.voice_model.wstring()),
        [this](const QString& message) { setStatus("朗读失败：" + message.toStdString()); },
        [this](SpeechQueue::State state, const QString& error) {
            if (state == SpeechQueue::State::Failed) {
                setStatus("朗读模型加载失败：" + error.toStdString());
            }
        });
    const sa::settings::SettingsDraft settings = [this] {
        std::lock_guard<std::mutex> lock(mutex_);
        return settings_;
    }();
    speech_queue_->setOutput(settings.speech.device_id, settings.speech.monitor);
    speech_queue_->setModel(
        QString::fromStdWString(selection_.voice_model.wstring()),
        speech_piper_ ? QString() : QString::fromStdString(speech_language_));
    speech_queue_->setActive(settings.speech.enabled);
}

void RecordingController::ensureTranslator() {
    sa::settings::SettingsDraft settings;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        settings = settings_;
    }
    if (translation_worker_ != nullptr) {
        translation_worker_->load(settings.translation.direction);
        return;
    }
    const unsigned int threads = std::max(2u, std::thread::hardware_concurrency() / 2);
    TranslatorFactory factory = make_translation_factory(
        models_directory_,
        llm_model_provider_ != nullptr ? llm_model_provider_
                                       : std::function<std::filesystem::path()>(
                                             [] { return std::filesystem::path{}; }),
        static_cast<int>(threads),
        settings.translation.device, detect_gpu_capability());
    translation_worker_ = std::make_unique<TranslationWorker>(
        std::move(factory),
        [this](TranslationDirection, bool ok, const std::string& error, const TranslationModelInfo&) {
            if (!ok) {
                {
                    std::lock_guard<std::mutex> lock(translation_mutex_);
                    translation_failed_ = true;
                }
                setStatus("翻译模型加载失败：" + error);
            }
        },
        [this](std::uint64_t ticket, std::optional<std::string> translation,
               const std::string& error) {
            bool finished = false;
            {
                std::lock_guard<std::mutex> lock(translation_mutex_);
                if (translation.has_value()) {
                    translation_session_.set_translation(static_cast<std::size_t>(ticket),
                                                         *translation);
                } else {
                    translation_session_.set_failed(static_cast<std::size_t>(ticket));
                    translation_failed_ = true;
                }
                if (pending_translations_ > 0) --pending_translations_;
                finished = stop_requested_ && pending_translations_ == 0;
            }
            if (!translation.has_value() && !error.empty()) {
                setStatus("翻译失败：" + error);
            }
            if (translation.has_value()) {
                bool live = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    live = settings_.speech.enabled && settings_.speech.live;
                }
                if (live) queueSpeech(*translation);
            }
            if (finished) finishTranslation(false);
        });
    translation_worker_->load(settings.translation.direction);
}

void RecordingController::finishTranslation(bool timed_out) {
    std::string separator;
    bool live_speech = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        separator = settings_.translation.separator.toStdString();
        live_speech = settings_.speech.enabled && settings_.speech.live;
    }
    std::string text;
    std::string spoken;
    {
        std::lock_guard<std::mutex> lock(translation_mutex_);
        if (!stop_requested_) return;
        text = translation_session_.compose(separator, timed_out).text;
        spoken = translation_session_.translated_text();
        pending_translations_ = 0;
    }
    if (text.empty()) return;
    logTranscript(timed_out ? "translated-timeout" : "translated", text);
    queueInjection(text);
    if (!spoken.empty() && !live_speech) queueSpeech(spoken);
    setStatus(text);
}

void RecordingController::queueInjection(const std::string& text) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_text_.push_back(text);
}

void RecordingController::queueSpeech(const std::string& text) {
    logTranscript("speak", text);
    std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_speech_.push_back(text);
}

void RecordingController::injectText(const std::string& text) {
    const std::wstring wide = toWide(text);
    if (wide.empty()) return;
    if (target_.valid()) {
        bool paste_safe = false;
        if (inject_text_into_text_input(target_, wide, current_process_id(), &paste_safe)) return;
        if (paste_safe) {
            setClipboardText(wide);
            if (paste_clipboard_into_text_input(target_, current_process_id())) return;
        }
    }
    setClipboardText(wide);
}

} // namespace sa::imgui_ui
