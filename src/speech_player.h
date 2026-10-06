#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

struct AudioOutputDevice {
    std::wstring id;
    std::wstring name;
};

// Active playback endpoints, in the order Windows reports them.
std::vector<AudioOutputDevice> list_audio_output_devices();

// Plays mono float clips one after another on a single output device, on its
// own thread. An empty device id means the current default playback device.
// Callbacks run on the playback thread.
class SpeechPlayer final {
public:
    using ClipCallback = std::function<void(bool ok, const std::string& error)>;

    SpeechPlayer(std::wstring device_id, ClipCallback on_clip_done);
    ~SpeechPlayer();

    SpeechPlayer(const SpeechPlayer&) = delete;
    SpeechPlayer& operator=(const SpeechPlayer&) = delete;

    void play(std::vector<float> samples, int sample_rate);
    // Drops queued clips and cuts the one playing short; no callbacks for them.
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
