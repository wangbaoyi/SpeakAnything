// A small standalone benchmark for the shared fbank front end. It reports the
// same deterministic 5 s signal at 1 and 4 threads, and verifies that the two
// outputs are identical. Usage: fbank-benchmark [iterations] (default 20).
//
// This is a CPU measurement even in a CUDA build: feature extraction always
// runs on the host, before the model graph is submitted.

#include "fbank.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr int sample_rate = 16'000;
constexpr int seconds = 5;

std::vector<float> deterministic_signal() {
    std::vector<float> audio(static_cast<std::size_t>(sample_rate * seconds));
    std::uint32_t state = 0x12345678U;
    for (std::size_t index = 0; index < audio.size(); ++index) {
        state = state * 1664525U + 1013904223U;
        const float noise =
            (static_cast<float>((state >> 8) & 0xFFFFU) / 32768.0F - 1.0F) * 0.02F;
        const double time = static_cast<double>(index) / sample_rate;
        const float tone =
            0.3F * static_cast<float>(std::sin(2.0 * pi * 220.0 * time)) +
            0.15F * static_cast<float>(std::sin(2.0 * pi * 880.0 * time));
        audio[index] = tone + noise;
    }
    return audio;
}

double best_time_ms(
    std::span<const float> audio,
    const fbank::Config& config,
    int threads,
    int iterations,
    std::vector<float>& result,
    int& frames) {
    double best = 1e9;
    for (int iteration = 0; iteration < iterations; ++iteration) {
        const auto started = std::chrono::steady_clock::now();
        result = fbank::compute_log_mel(audio, config, frames, threads);
        const auto finished = std::chrono::steady_clock::now();
        best = std::min(
            best,
            std::chrono::duration<double, std::milli>(finished - started).count());
    }
    return best;
}

bool same_values(std::span<const float> left, std::span<const float> right) {
    return left.size() == right.size() &&
           (left.empty() ||
            std::memcmp(left.data(), right.data(), left.size() * sizeof(float)) == 0);
}

}  // namespace

int main(int argument_count, char** arguments) {
    int iterations = 20;
    if (argument_count > 1) {
        const int parsed = std::atoi(arguments[1]);
        if (parsed > 0) {
            iterations = parsed;
        }
    }

    const std::vector<float> audio = deterministic_signal();
    const fbank::Config config;

    // Warm the filter-table cache before timing so it is not charged to the
    // first run.
    int warm_frames = 0;
    const std::vector<float> warm =
        fbank::compute_log_mel(audio, config, warm_frames, 1);
    if (warm.empty()) {
        std::fprintf(stderr, "fbank benchmark: no frames produced\n");
        return 1;
    }

    std::vector<float> serial;
    std::vector<float> parallel;
    int serial_frames = 0;
    int parallel_frames = 0;
    const double serial_ms =
        best_time_ms(audio, config, 1, iterations, serial, serial_frames);
    const double parallel_ms =
        best_time_ms(audio, config, 4, iterations, parallel, parallel_frames);

    int sensevoice_frames = 0;
    const std::vector<float> sensevoice =
        fbank::stack_lfr(serial, serial_frames, config.mel_bins, 7, 6, sensevoice_frames);
    int vad_frames = 0;
    const std::vector<float> vad =
        fbank::stack_lfr(serial, serial_frames, config.mel_bins, 5, 1, vad_frames);

    const bool identical = same_values(serial, parallel) &&
                           serial_frames == parallel_frames;
    std::printf(
        "fbank benchmark: %d s deterministic signal, %d Hz, %d mel bins\n",
        seconds, sample_rate, config.mel_bins);
    std::printf("raw frames: %d, LFR 7/6 -> %d x 560, LFR 5/1 -> %d x 400\n",
                serial_frames, sensevoice_frames, vad_frames);
    std::printf("1 thread : %7.3f ms (best of %d)\n", serial_ms, iterations);
    std::printf("4 threads: %7.3f ms (best of %d)\n", parallel_ms, iterations);
    if (serial_ms > 0.0) {
        std::printf("speedup  : %7.2fx\n", serial_ms / parallel_ms);
    }
    std::printf("outputs  : %s\n", identical ? "identical" : "DIFFERENT");
    if (!identical) {
        return 1;
    }
    if (fbank::serial_forced()) {
        std::printf(
            "note: SENSEVOICE_SERIAL_FBANK is set, so the 4-thread call used "
            "the serial path too\n");
    }
    return 0;
}
