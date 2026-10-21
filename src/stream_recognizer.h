#pragma once

#include "sensevoice_engine.h"
#include "stability_tracker.h"
#include "fsmn_vad_engine.h"
#include "text_processor.h"

#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

enum class RecognitionEventKind {
    Partial,
    Final,
    Error,
};

struct RecognitionEvent {
    RecognitionEventKind kind = RecognitionEventKind::Partial;
    std::uint64_t sequence = 0;
    int audio_ms = 0;
    int vad_ms = 0;
    int inference_ms = 0;
    // Milliseconds from the most recent accept_pcm to this final event. Zero
    // for partials and for finals emitted without any accepted audio.
    int final_latency_ms = 0;
    float vad_current_db = -100.0F;
    float vad_peak_db = -100.0F;
    float vad_noise_floor_db = -70.0F;
    float vad_required_db = -45.0F;
    float vad_speech_probability = 0.0F;
    VadActivity vad_activity = VadActivity::Silence;
    std::string text;
    std::string stable;
    std::string unstable;
    std::string error;
    std::vector<std::string> matched_hotwords;
    std::vector<std::string> tokens;
    bool stability_conflict = false;
    bool forced_split = false;
    bool hotword_bias_applied = false;
};

struct StreamRecognizerConfig {
    int partial_interval_ms = 450;
    int minimum_audio_ms = 600;
    int minimum_new_audio_ms = 240;
    int endpoint_silence_ms = 700;
    int silence_preroll_ms = 1'000;
    int maximum_utterance_ms = 15'000;
    int memory_limit_mb = 300;
    float vad_speech_threshold = 0.55F;
    float vad_minimum_db = -60.0F;
    float vad_minimum_snr_db = 3.0F;
    int vad_minimum_speech_ms = 200;
    float hotword_boost = 3.0F;
    std::size_t stability_history = 3;
    std::size_t stability_holdback_chars = 3;
};

class StreamRecognizer {
public:
    using EventHandler = std::function<void(const RecognitionEvent&)>;

    StreamRecognizer(
        SpeechEngine& engine,
        FsmnVadEngine* vad,
        StreamRecognizerConfig config,
        EventHandler handler,
        TextProcessor* text_processor = nullptr);
    ~StreamRecognizer();

    StreamRecognizer(const StreamRecognizer&) = delete;
    StreamRecognizer& operator=(const StreamRecognizer&) = delete;

    void start();
    void accept_pcm(std::span<const float> samples);
    void finish();
    void cancel();
    [[nodiscard]] VadResult vad_telemetry() const;

private:
    // Immutable output of one VAD pass. `audio_samples` is the prefix length
    // of the audio buffer at the moment the pass started. `generation` changes
    // whenever consume_audio_prefix shifts the buffer, so a result from an
    // older generation can never be applied to newer audio.
    struct VadSnapshot {
        std::uint64_t generation = 0;
        std::uint64_t revision = 0;
        std::size_t audio_samples = 0;
        VadResult result;
    };

    void run();
    void run_serial();
    void run_overlapped();
    void run_vad_worker();
    VadDetectionOptions vad_options() const;
    bool fresh_vad_available() const;
    SenseVoiceResult recognize_snapshot(
        std::span<const float> snapshot,
        const VadResult* detected_vad = nullptr,
        const SpeechSegment* only_segment = nullptr);
    SenseVoiceResult recognize_audio(std::span<const float> audio);
    void consume_audio_prefix(std::size_t samples, bool reprocess_remaining);
    void remember_result(const SenseVoiceResult& result, std::size_t decoded_samples);
    void emit_result(const SenseVoiceResult& result, bool final_result);
    void emit_error(const std::string& error);

    SpeechEngine& engine_;
    FsmnVadEngine* vad_;
    StreamRecognizerConfig config_;
    EventHandler handler_;
    TextProcessor* text_processor_ = nullptr;
    StabilityTracker stability_;

    std::mutex mutex_;
    // Serializes start/finish/cancel so two callers cannot join the same
    // std::thread concurrently.
    mutable std::mutex lifecycle_mutex_;
    std::condition_variable condition_;
    std::condition_variable vad_condition_;
    std::vector<float> audio_;
    std::thread worker_;
    std::thread vad_worker_;
    bool started_ = false;
    bool dirty_ = false;
    bool finish_requested_ = false;
    bool cancel_requested_ = false;
    bool serial_vad_ = false;
    // Set by the recognizer worker as it returns. The VAD worker stays alive
    // until then so a trim during the last partial can still be served.
    bool asr_done_ = false;
    // Monotonic revision of the audio buffer, bumped on every append and every
    // consume. VAD results record the revision they were computed from.
    std::uint64_t audio_revision_ = 0;
    // Bumped whenever consume_audio_prefix shifts the buffer. Segment times are
    // relative to the buffer start, so results from an older generation are
    // discarded rather than reinterpreted.
    std::uint64_t audio_generation_ = 1;
    std::uint64_t finish_revision_ = 0;
    bool vad_requested_ = false;
    // Buffer length when the newest VAD pass started. accept_pcm only asks for
    // another pass once enough new audio has arrived, so the worker does not
    // scan every capture chunk of a growing utterance.
    std::size_t last_vad_pass_samples_ = 0;
    std::optional<VadSnapshot> vad_result_;
    std::uint64_t last_vad_revision_consumed_ = 0;
    std::string vad_error_;
    std::atomic<std::int64_t> last_accept_ns_{0};
    std::size_t last_decoded_samples_ = 0;
    std::string last_text_;
    int last_vad_ms_ = 0;
    int last_inference_ms_ = 0;
    bool last_hotword_bias_applied_ = false;
    VadResult last_vad_telemetry_;
    bool emitted_utterance_ = false;
    bool forced_split_ = false;
    std::size_t baseline_working_set_bytes_ = 0;
    std::uint64_t sequence_ = 0;
    std::atomic<float> vad_current_db_{-100.0F};
    std::atomic<float> vad_peak_db_{-100.0F};
    std::atomic<float> vad_noise_floor_db_{-70.0F};
    std::atomic<float> vad_required_db_{-45.0F};
    std::atomic<float> vad_speech_probability_{0.0F};
    std::atomic<int> vad_activity_{static_cast<int>(VadActivity::Silence)};
};
