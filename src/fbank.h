#pragma once

#include <cstddef>
#include <span>
#include <vector>

// Shared log-mel front end for SenseVoice and FSMN-VAD.
//
// Both models were trained on the same 80-bin filter bank: 16 kHz mono,
// 25 ms Hamming windows every 10 ms, a 512-point FFT, 20 Hz to 8 kHz, natural
// log of the mel energy with a 1.19e-7 floor, then LFR stacking. SenseVoice and
// FSMN-VAD used to carry byte-identical copies of this code; this module is the
// single copy, so the two features cannot drift apart.
namespace fbank {

struct Config {
    int sample_rate = 16'000;
    int window_length = 400;   // 25 ms
    int frame_shift = 160;     // 10 ms
    int fft_size = 512;
    int mel_bins = 80;
    float preemphasis = 0.97F;
    float low_frequency = 20.0F;
    float high_frequency = 8'000.0F;
};

// Fewer frames than this run on the calling thread: creating and joining
// workers costs more than the frame work itself. 32 frames is 320 ms.
inline constexpr int minimum_parallel_frames = 32;

// Independent frames never use more workers than this, whatever the engine's
// ggml thread count is, so an fbank pass cannot starve an overlapping
// FSMN-VAD pass or the ggml pool on a small CPU.
inline constexpr int maximum_threads = 4;

// True when SENSEVOICE_SERIAL_FBANK=1 (or any value other than empty and "0")
// is in the environment. Read once per process, like SENSEVOICE_SERIAL_VAD.
[[nodiscard]] bool serial_forced();

// Worker count for one call. engine_threads is the owning engine's ggml thread
// count. Returns 1 when SENSEVOICE_SERIAL_FBANK is set or frame_count is below
// minimum_parallel_frames, otherwise min(maximum_threads, engine_threads),
// never less than 1.
[[nodiscard]] int resolve_threads(int engine_threads, int frame_count);

// Log-mel frames for the whole input, row-major: frames x config.mel_bins.
// frames is 0 and the result empty when audio is shorter than one window.
//
// The per-frame math is unchanged from the two copies this module replaces.
// Frames are independent, so splitting them across threads produces the same
// bytes as the serial path; only the wall clock changes. engine_threads follows
// resolve_threads: pass 1 to force serial work in a test or benchmark.
[[nodiscard]] std::vector<float> compute_log_mel(
    std::span<const float> audio,
    const Config& config,
    int& frames,
    int engine_threads);

// LFR stacking. Output row `time` concatenates input rows
// time*stride ... time*stride+window-1, with the first row repeated
// (window - 1) / 2 times in front and the last row repeated at the end.
// Output frame count is ceil(source_frames / stride).
//
// features is row-major source_frames x mel_bins; the result is row-major
// frames x (window * mel_bins). frames is 0 and the result empty when the
// input is empty.
[[nodiscard]] std::vector<float> stack_lfr(
    std::span<const float> features,
    int source_frames,
    int mel_bins,
    int window,
    int stride,
    int& frames);

}  // namespace fbank
