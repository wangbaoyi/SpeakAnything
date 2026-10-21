// ctest runs the Release config; keep the assertions live.
#undef NDEBUG

#include "fbank.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

namespace {

constexpr double pi = 3.14159265358979323846;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
    }
}

// 5 s of deterministic speech-like audio: two tones plus a seeded LCG noise
// floor. The same generator feeds the benchmark, so timings and equality are
// reproducible.
std::vector<float> deterministic_signal(int sample_count) {
    std::vector<float> audio(static_cast<std::size_t>(sample_count));
    std::uint32_t state = 0x12345678U;
    for (int index = 0; index < sample_count; ++index) {
        state = state * 1664525U + 1013904223U;
        const float noise =
            (static_cast<float>((state >> 8) & 0xFFFFU) / 32768.0F - 1.0F) * 0.02F;
        const double time = static_cast<double>(index) / 16'000.0;
        const float tone =
            0.3F * static_cast<float>(std::sin(2.0 * pi * 220.0 * time)) +
            0.15F * static_cast<float>(std::sin(2.0 * pi * 880.0 * time));
        audio[static_cast<std::size_t>(index)] = tone + noise;
    }
    return audio;
}

bool same_values(std::span<const float> left, std::span<const float> right) {
    return left.size() == right.size() &&
           (left.empty() ||
            std::memcmp(left.data(), right.data(), left.size() * sizeof(float)) == 0);
}

}  // namespace

int main() {
    const fbank::Config config;
    const bool forced_serial = fbank::serial_forced();

    // Thread policy: never more than 4, never more than the engine asks for,
    // serial below the short-input threshold, and serial when forced.
    check(fbank::resolve_threads(8, 500) == (forced_serial ? 1 : 4),
          "8 engine threads cap at 4 fbank threads");
    check(fbank::resolve_threads(16, 500) == (forced_serial ? 1 : 4),
          "large engine thread counts cap at 4");
    check(fbank::resolve_threads(2, 500) == (forced_serial ? 1 : 2),
          "fewer engine threads than 4 are respected");
    check(fbank::resolve_threads(1, 500) == 1, "one engine thread stays serial");
    check(fbank::resolve_threads(8, 10) == 1, "short input stays serial");

    // 5 s at 16 kHz: (80000 - 400) / 160 + 1.
    const std::vector<float> audio = deterministic_signal(5 * 16'000);
    int serial_frames = 0;
    int parallel_frames = 0;
    const std::vector<float> serial =
        fbank::compute_log_mel(audio, config, serial_frames, 1);
    const std::vector<float> parallel =
        fbank::compute_log_mel(audio, config, parallel_frames, 4);

    check(serial_frames == 498, "5 s gives 498 raw frames");
    check(parallel_frames == serial_frames, "parallel frame count matches serial");
    check(serial.size() == static_cast<std::size_t>(serial_frames) * 80,
          "raw features are frames x 80");
    check(same_values(serial, parallel),
          "parallel raw log-mel is bit-identical to serial");

    // Independent calls reuse the cached filter tables and stay identical.
    int repeat_frames = 0;
    const std::vector<float> repeated =
        fbank::compute_log_mel(audio, config, repeat_frames, 4);
    check(repeat_frames == serial_frames && same_values(serial, repeated),
          "repeated parallel calls are deterministic");

    // SenseVoice LFR: window 7, stride 6, 560-wide rows.
    int sensevoice_serial_frames = 0;
    int sensevoice_parallel_frames = 0;
    const std::vector<float> sensevoice_serial = fbank::stack_lfr(
        serial, serial_frames, 80, 7, 6, sensevoice_serial_frames);
    const std::vector<float> sensevoice_parallel = fbank::stack_lfr(
        parallel, parallel_frames, 80, 7, 6, sensevoice_parallel_frames);
    check(sensevoice_serial_frames == 83, "498 raw frames stack to 83 LFR frames");
    check(sensevoice_parallel_frames == sensevoice_serial_frames,
          "LFR frame count matches between paths");
    check(sensevoice_serial.size() ==
              static_cast<std::size_t>(sensevoice_serial_frames) * 560,
          "SenseVoice features are frames x 560");
    check(same_values(sensevoice_serial, sensevoice_parallel),
          "parallel LFR stack matches serial");

    // FSMN-VAD LFR: the model declares window 5, stride 1, 400-wide rows.
    int vad_frames = 0;
    const std::vector<float> vad =
        fbank::stack_lfr(serial, serial_frames, 80, 5, 1, vad_frames);
    check(vad_frames == 498, "stride 1 keeps the raw frame count");
    check(vad.size() == static_cast<std::size_t>(vad_frames) * 400,
          "FSMN-VAD features are frames x 400");

    // Padding semantics on a tiny synthetic input: first row repeated
    // (window - 1) / 2 times in front, last row repeated at the end.
    std::vector<float> rows(8 * 2);
    for (int frame = 0; frame < 8; ++frame) {
        rows[static_cast<std::size_t>(frame) * 2] = static_cast<float>(frame);
        rows[static_cast<std::size_t>(frame) * 2 + 1] = static_cast<float>(frame);
    }
    int tiny_frames = 0;
    const std::vector<float> tiny =
        fbank::stack_lfr(rows, 8, 2, 5, 3, tiny_frames);
    const std::vector<float> expected = {
        0, 0, 0, 0, 0, 0, 1, 1, 2, 2,
        1, 1, 2, 2, 3, 3, 4, 4, 5, 5,
        4, 4, 5, 5, 6, 6, 7, 7, 7, 7,
    };
    check(tiny_frames == 3, "8 rows with stride 3 stack to 3 frames");
    check(same_values(tiny, expected), "LFR padding repeats the first and last row");

    // Shorter than one window and empty inputs produce nothing, frames = 0.
    int short_frames = -1;
    const std::vector<float> short_audio(
        static_cast<std::size_t>(config.window_length) - 1, 0.5F);
    check(fbank::compute_log_mel(short_audio, config, short_frames, 4).empty(),
          "audio shorter than one window yields no frames");
    check(short_frames == 0, "short input reports zero frames");
    int empty_frames = -1;
    check(fbank::stack_lfr({}, 0, 80, 7, 6, empty_frames).empty(),
          "empty raw features stack to nothing");
    check(empty_frames == 0, "empty stack reports zero frames");

    if (failures == 0) {
        std::printf(
            "fbank: serial and parallel paths match (%d raw frames, "
            "83 SenseVoice frames, 498 VAD frames)\n",
            serial_frames);
        return 0;
    }
    std::fprintf(stderr, "fbank: %d check(s) failed\n", failures);
    return 1;
}
