#pragma once

#include "sensevoice_engine.h"

#include <filesystem>
#include <memory>
#include <string>

// Whisper (whisper.cpp) for languages SenseVoice doesn't cover, such as
// Bulgarian or German. On macOS the decoder runs on the GPU through Metal and,
// when <model>-encoder.mlmodelc sits next to the model, the encoder runs on
// the Neural Engine through Core ML.
class WhisperEngine final : public SpeechEngine {
public:
    WhisperEngine();
    ~WhisperEngine() override;

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    // language: ISO 639-1 code ("bg", "de"...), or "auto".
    bool load(const std::filesystem::path& model_path, std::string language, int threads,
              std::string& error);
    [[nodiscard]] bool loaded() const override;
    using SpeechEngine::recognize;
    // Hotwords bias Whisper through its initial prompt.
    SenseVoiceResult recognize(
        std::span<const float> pcm_16khz_mono,
        std::span<const HotwordBoostPhrase> hotwords) override;
    [[nodiscard]] const char* device() const override;
    void set_language(const std::string& language) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
