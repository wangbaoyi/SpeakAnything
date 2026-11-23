#pragma once

#include "ui/settings_types.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "audio_io.h"
#include "fsmn_vad_engine.h"
#include "sensevoice_engine.h"
#include "speech_queue.h"
#include "stream_recognizer.h"
#include "text_processor.h"
#include "text_injector.h"
#include "translation_session.h"
#include "translator.h"
#include "whisper_engine.h"

namespace sa::imgui_ui {

struct OverlaySnapshot {
    bool listening = false;
    bool busy = false;
    float level = 0.0F;
    std::string status;
    std::string last_text;
};

struct ModelSelection {
    std::filesystem::path recognizer;
    std::filesystem::path vad;
    std::filesystem::path voice_model;
    bool whisper = false;
    bool voice_piper = false;
    std::string language = "zh";
    std::string speech_language = "en";
};

class RecordingController {
public:
    RecordingController();
    ~RecordingController();

    RecordingController(const RecordingController&) = delete;
    RecordingController& operator=(const RecordingController&) = delete;

    void initialize(const sa::settings::SettingsDraft& settings,
                    const std::filesystem::path& models_directory,
                    const ModelSelection& selection,
                    std::function<std::filesystem::path()> llm_model_provider = {});
    void updateSettings(const sa::settings::SettingsDraft& settings);
    void start();
    bool startFile(const std::filesystem::path& audio_path, std::string& error);
    void stop();
    void toggle();
    void shutdown();

    [[nodiscard]] bool listening() const;
    [[nodiscard]] OverlaySnapshot snapshot() const;
    // Must be called on the UI thread; injects completed text at the target.
    void pumpInjection();

private:
    enum class InitState { Idle, Loading, Ready, Failed };
    void loadModels();
    void setStatus(const std::string& status);
    void handleEvent(const RecognitionEvent& event);
    void injectText(const std::string& text);
    void ensureTranslator();
    void ensureSpeech();
    void finishTranslation(bool timed_out);
    void queueInjection(const std::string& text);
    void queueSpeech(const std::string& text);

    mutable std::mutex mutex_;
    sa::settings::SettingsDraft settings_;
    std::string status_ = "正在加载模型...";
    std::string last_text_;
    std::atomic<InitState> init_state_{InitState::Idle};
    std::atomic<bool> listening_{false};
    std::atomic<bool> busy_{false};
    std::atomic<std::uint64_t> last_event_tick_{0};
    std::atomic<std::uint64_t> last_inject_tick_{0};

    std::unique_ptr<SpeechEngine> engine_;
    std::unique_ptr<FsmnVadEngine> vad_;
    std::unique_ptr<StreamRecognizer> recognizer_;
    std::unique_ptr<TextProcessor> text_processor_;
    MicrophoneCapture capture_;
    std::thread loader_;
    std::filesystem::path models_directory_;
    std::function<std::filesystem::path()> llm_model_provider_;
    std::unique_ptr<SpeechQueue> speech_queue_;
    bool speech_piper_ = false;
    std::string speech_language_ = "en";
    ModelSelection selection_;
    std::unique_ptr<TranslationWorker> translation_worker_;
    TranslationSession translation_session_{TranslationDirection::ZhToEn};
    std::mutex translation_mutex_;
    bool translation_failed_ = false;
    bool stop_requested_ = false;
    int pending_translations_ = 0;
    std::atomic<std::uint64_t> stop_tick_{0};
    WindowsTextInputTarget target_;
    std::mutex queue_mutex_;
    std::deque<std::string> pending_text_;
    std::deque<std::string> pending_speech_;
    bool started_ = false;
};

} // namespace sa::imgui_ui
