#pragma once

#include <functional>
#include <string>
#include <vector>

// macOS built-in voices (AVSpeechSynthesizer), for languages without a
// Kokoro or Piper voice, such as Bulgarian. Speech is rendered to samples, not
// played, so it can go to the selected speech device like any other voice.

bool macos_system_voice_available(const std::string& language);

// done runs on an arbitrary thread with mono float samples, or ok = false.
void macos_system_voice_synthesize(
    const std::string& text,
    const std::string& language,
    double speed,
    std::function<void(std::vector<float> samples, int sample_rate, bool ok)> done);
