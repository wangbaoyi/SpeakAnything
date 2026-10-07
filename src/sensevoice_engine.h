#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

struct HotwordBoostPhrase {
    std::string phrase;
    float boost = 3.0F;
};

struct SenseVoiceResult {
    std::string text;
    int audio_ms = 0;
    int vad_ms = 0;
    int inference_ms = 0;
    bool hotword_bias_applied = false;
};

// A speech recognizer the streaming pipeline can drive: SenseVoice for the
// languages it knows, Whisper for the rest.
class SpeechEngine {
public:
    virtual ~SpeechEngine() = default;
    [[nodiscard]] virtual bool loaded() const = 0;
    virtual SenseVoiceResult recognize(
        std::span<const float> pcm_16khz_mono,
        std::span<const HotwordBoostPhrase> hotwords) = 0;
    SenseVoiceResult recognize(std::span<const float> pcm_16khz_mono) {
        return recognize(pcm_16khz_mono, {});
    }
    // Where inference runs: "Neural Engine", "GPU (Metal)" or "CPU".
    [[nodiscard]] virtual const char* device() const = 0;
    // The language the user speaks (ISO 639-1); recognition is held to it
    // instead of guessing. Empty or "auto" lets the model detect it.
    virtual void set_language(const std::string& language) = 0;
};

class SenseVoiceEngine final : public SpeechEngine {
public:
    SenseVoiceEngine();
    ~SenseVoiceEngine() override;

    SenseVoiceEngine(const SenseVoiceEngine&) = delete;
    SenseVoiceEngine& operator=(const SenseVoiceEngine&) = delete;

    bool load(const std::filesystem::path& model_path, int threads, std::string& error);
    [[nodiscard]] bool loaded() const override;
    // Drops a loaded or half-loaded model so load() can try another one.
    void reset();
    [[nodiscard]] const char* device() const override;
    // SenseVoice knows zh, en, yue, ja and ko; anything else falls back to auto.
    void set_language(const std::string& language) override;
    using SpeechEngine::recognize;
    SenseVoiceResult recognize(
        std::span<const float> pcm_16khz_mono,
        std::span<const HotwordBoostPhrase> hotwords) override;

private:
#ifdef SENSEVOICE_WITH_COREML
    bool load_coreml(const std::filesystem::path& model_directory, std::string& error);
    SenseVoiceResult recognize_coreml(
        std::vector<float> features, int frames,
        std::span<const HotwordBoostPhrase> hotwords, SenseVoiceResult result);
#endif
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
