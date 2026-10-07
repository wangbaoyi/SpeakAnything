#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

// SenseVoiceSmall encoder compiled for Core ML, pinned to the CPU + Neural
// Engine (the fp16 encoder produces NaN on the GPU path). The package has one
// function per padded feature length; each is loaded the first time it fits.
class CoreMLSenseVoice final {
public:
    CoreMLSenseVoice();
    ~CoreMLSenseVoice();

    CoreMLSenseVoice(const CoreMLSenseVoice&) = delete;
    CoreMLSenseVoice& operator=(const CoreMLSenseVoice&) = delete;

    // package is sensevoice.mlpackage; it is compiled once and cached under
    // ~/Library/Caches/SpeakAnything.
    bool load(const std::filesystem::path& package, std::string& error);

    // features: frames x 560 CMVN-normalized LFR features (frames <= 500).
    // logits receives (frames + 4) x vocabulary values, query frames first.
    bool run(std::span<const float> features, int frames, int language_id, int style_id,
             std::vector<float>& logits, int& vocabulary, std::string& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
