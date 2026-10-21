#include "fbank.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fbank {
namespace {

constexpr double pi = 3.14159265358979323846;

// Everything that depends only on Config: the Hamming window and the mel
// filter bank. Built once per distinct Config, under a mutex, and shared
// read-only by every worker.
struct Tables {
    Config config;
    int spectrum_bins = 0;
    std::vector<float> window;
    // Row-major mel_bins x spectrum_bins. A zero weight is skipped when
    // accumulating energy, exactly as the original code did.
    std::vector<float> filters;
};

float mel_frequency(float frequency) {
    return 1127.0F * std::log(1.0F + frequency / 700.0F);
}

Tables build_tables(const Config& config) {
    Tables tables;
    tables.config = config;
    tables.spectrum_bins = config.fft_size / 2 + 1;

    tables.window.resize(static_cast<std::size_t>(config.window_length));
    for (int index = 0; index < config.window_length; ++index) {
        tables.window[static_cast<std::size_t>(index)] =
            0.54F - 0.46F * std::cos(
                2.0F * static_cast<float>(pi) * index / (config.window_length - 1));
    }

    const float bin_width =
        static_cast<float>(config.sample_rate) / config.fft_size;
    const float mel_low = mel_frequency(config.low_frequency);
    const float mel_high = mel_frequency(config.high_frequency);
    const float mel_step = (mel_high - mel_low) / (config.mel_bins + 1);
    tables.filters.assign(
        static_cast<std::size_t>(config.mel_bins) * tables.spectrum_bins, 0.0F);
    for (int mel = 0; mel < config.mel_bins; ++mel) {
        const float left = mel_low + mel * mel_step;
        const float center = mel_low + (mel + 1) * mel_step;
        const float right = mel_low + (mel + 2) * mel_step;
        for (int bin = 0; bin < tables.spectrum_bins; ++bin) {
            const float frequency = mel_frequency(bin_width * bin);
            if (frequency > left && frequency < right) {
                tables.filters[
                    static_cast<std::size_t>(mel) * tables.spectrum_bins + bin] =
                    frequency <= center
                        ? (frequency - left) / (center - left)
                        : (right - frequency) / (right - center);
            }
        }
    }
    return tables;
}

bool same_config(const Config& left, const Config& right) {
    return left.sample_rate == right.sample_rate &&
           left.window_length == right.window_length &&
           left.frame_shift == right.frame_shift &&
           left.fft_size == right.fft_size &&
           left.mel_bins == right.mel_bins &&
           left.preemphasis == right.preemphasis &&
           left.low_frequency == right.low_frequency &&
           left.high_frequency == right.high_frequency;
}

std::shared_ptr<const Tables> tables_for(const Config& config) {
    // A function-local static initializes on first use with the C++11
    // thread-safe guarantee; the mutex protects the small per-config cache.
    static std::mutex mutex;
    static std::vector<std::shared_ptr<const Tables>> cache;
    const std::lock_guard<std::mutex> lock(mutex);
    for (const auto& tables : cache) {
        if (same_config(tables->config, config)) {
            return tables;
        }
    }
    cache.push_back(std::make_shared<const Tables>(build_tables(config)));
    return cache.back();
}

// The original in-place radix-2 FFT, unchanged. Kept per worker so no scratch
// state is shared between threads.
void fft(float* real, float* imaginary, int size) {
    for (int index = 1, reverse = 0; index < size; ++index) {
        int bit = size >> 1;
        for (; reverse & bit; bit >>= 1) {
            reverse ^= bit;
        }
        reverse ^= bit;
        if (index < reverse) {
            std::swap(real[index], real[reverse]);
            std::swap(imaginary[index], imaginary[reverse]);
        }
    }

    for (int length = 2; length <= size; length <<= 1) {
        const double angle = -2.0 * pi / length;
        const float rotation_real = std::cos(static_cast<float>(angle));
        const float rotation_imaginary = std::sin(static_cast<float>(angle));
        for (int start = 0; start < size; start += length) {
            float current_real = 1.0F;
            float current_imaginary = 0.0F;
            for (int offset = 0; offset < length / 2; ++offset) {
                const float upper_real = real[start + offset];
                const float upper_imaginary = imaginary[start + offset];
                const float lower_real =
                    real[start + offset + length / 2] * current_real -
                    imaginary[start + offset + length / 2] * current_imaginary;
                const float lower_imaginary =
                    real[start + offset + length / 2] * current_imaginary +
                    imaginary[start + offset + length / 2] * current_real;
                real[start + offset] = upper_real + lower_real;
                imaginary[start + offset] = upper_imaginary + lower_imaginary;
                real[start + offset + length / 2] = upper_real - lower_real;
                imaginary[start + offset + length / 2] = upper_imaginary - lower_imaginary;
                const float next_real =
                    current_real * rotation_real - current_imaginary * rotation_imaginary;
                current_imaginary =
                    current_real * rotation_imaginary + current_imaginary * rotation_real;
                current_real = next_real;
            }
        }
    }
}

// Per-worker scratch. Sized once per thread and reused for its whole frame
// range, which is why a worker handles a contiguous run.
struct Scratch {
    std::vector<float> real;
    std::vector<float> imaginary;
    std::vector<float> frame;
};

// One 10 ms frame, copied operation for operation from the code this module
// replaces. Writes exactly config.mel_bins values to `output`.
void compute_frame(
    const float* samples,
    const Tables& tables,
    Scratch& scratch,
    float* output) {
    const Config& config = tables.config;
    const int window_length = config.window_length;
    const int fft_size = config.fft_size;
    const int mel_bins = config.mel_bins;
    const int spectrum_bins = tables.spectrum_bins;
    constexpr float floor = 1.1920929e-07F;

    double mean = 0.0;
    for (int index = 0; index < window_length; ++index) {
        mean += samples[index];
    }
    mean /= window_length;
    for (int index = 0; index < window_length; ++index) {
        scratch.frame[index] = samples[index] - static_cast<float>(mean);
    }
    for (int index = window_length - 1; index > 0; --index) {
        scratch.frame[index] -= config.preemphasis * scratch.frame[index - 1];
    }
    scratch.frame[0] -= config.preemphasis * scratch.frame[0];
    for (int index = 0; index < fft_size; ++index) {
        scratch.real[index] = index < window_length
            ? scratch.frame[index] * tables.window[index]
            : 0.0F;
        scratch.imaginary[index] = 0.0F;
    }
    fft(scratch.real.data(), scratch.imaginary.data(), fft_size);
    for (int mel = 0; mel < mel_bins; ++mel) {
        float energy = 0.0F;
        const std::size_t row = static_cast<std::size_t>(mel) * spectrum_bins;
        for (int bin = 0; bin < spectrum_bins; ++bin) {
            const float weight = tables.filters[row + bin];
            if (weight > 0.0F) {
                energy += weight * (
                    scratch.real[bin] * scratch.real[bin] +
                    scratch.imaginary[bin] * scratch.imaginary[bin]);
            }
        }
        output[mel] = std::log(std::max(energy, floor));
    }
}

void compute_frame_range(
    const float* waveform,
    int begin,
    int end,
    const Tables& tables,
    float* features) {
    Scratch scratch;
    scratch.real.resize(static_cast<std::size_t>(tables.config.fft_size));
    scratch.imaginary.resize(static_cast<std::size_t>(tables.config.fft_size));
    scratch.frame.resize(static_cast<std::size_t>(tables.config.window_length));
    const int mel_bins = tables.config.mel_bins;
    for (int time = begin; time < end; ++time) {
        compute_frame(
            waveform + static_cast<std::size_t>(time) * tables.config.frame_shift,
            tables,
            scratch,
            features + static_cast<std::size_t>(time) * mel_bins);
    }
}

std::string environment_string(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
#endif
}

}  // namespace

bool serial_forced() {
    static const bool forced = [] {
        const std::string value = environment_string("SENSEVOICE_SERIAL_FBANK");
        return !value.empty() && value != "0";
    }();
    return forced;
}

int resolve_threads(int engine_threads, int frame_count) {
    if (serial_forced() || frame_count < minimum_parallel_frames) {
        return 1;
    }
    return std::clamp(engine_threads, 1, maximum_threads);
}

std::vector<float> compute_log_mel(
    std::span<const float> audio,
    const Config& config,
    int& frames,
    int engine_threads) {
    frames = 0;
    if (audio.size() < static_cast<std::size_t>(config.window_length)) {
        return {};
    }

    // The scale the models were trained on. Kept as an explicit copy step so
    // the arithmetic is identical to the original serial implementation.
    std::vector<float> waveform(audio.begin(), audio.end());
    for (float& sample : waveform) {
        sample *= 32768.0F;
    }

    const int raw_frames = (
        static_cast<int>(waveform.size()) - config.window_length) /
        config.frame_shift + 1;
    const int thread_count = resolve_threads(engine_threads, raw_frames);
    const auto tables = tables_for(config);

    std::vector<float> features(
        static_cast<std::size_t>(raw_frames) * config.mel_bins);

    // Contiguous, disjoint row ranges; every worker owns its scratch buffers.
    // No worker reads a row that another writes, and the waveform and tables
    // are read-only, so the parallel result is byte-identical to the serial one.
    const int frames_per_thread =
        (raw_frames + thread_count - 1) / thread_count;
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(thread_count - 1));
    bool spawned = true;
    try {
        for (int index = 1; index < thread_count; ++index) {
            const int begin = index * frames_per_thread;
            const int end = std::min(raw_frames, begin + frames_per_thread);
            if (begin >= end) {
                break;
            }
            workers.emplace_back([&, begin, end] {
                compute_frame_range(
                    waveform.data(), begin, end, *tables, features.data());
            });
        }
    } catch (...) {
        // Thread creation can fail under resource pressure. Fall back to the
        // serial path instead of terminating; any worker that did start is
        // joined before the serial pass overwrites the same rows with the
        // same bytes.
        spawned = false;
    }
    if (spawned) {
        compute_frame_range(
            waveform.data(),
            0,
            std::min(raw_frames, frames_per_thread),
            *tables,
            features.data());
    }
    for (auto& worker : workers) {
        worker.join();
    }
    if (!spawned) {
        compute_frame_range(
            waveform.data(), 0, raw_frames, *tables, features.data());
    }

    frames = raw_frames;
    return features;
}

std::vector<float> stack_lfr(
    std::span<const float> features,
    int source_frames,
    int mel_bins,
    int window,
    int stride,
    int& frames) {
    frames = 0;
    if (source_frames <= 0 || features.empty()) {
        return {};
    }

    // Rows are referenced by index rather than copied; the original built a
    // vector of copied rows, which is the same data for this read-only pass.
    const int padding = (window - 1) / 2;
    frames = (source_frames + stride - 1) / stride;
    std::vector<int> padded;
    padded.reserve(static_cast<std::size_t>(source_frames + padding + window));
    for (int index = 0; index < padding; ++index) {
        padded.push_back(0);
    }
    for (int index = 0; index < source_frames; ++index) {
        padded.push_back(index);
    }
    while (static_cast<int>(padded.size()) < (frames - 1) * stride + window) {
        padded.push_back(source_frames - 1);
    }

    std::vector<float> output(
        static_cast<std::size_t>(frames) * window * mel_bins);
    const std::size_t row_bytes = static_cast<std::size_t>(mel_bins) * sizeof(float);
    for (int time = 0; time < frames; ++time) {
        for (int offset = 0; offset < window; ++offset) {
            const int source = padded[static_cast<std::size_t>(time) * stride + offset];
            std::memcpy(
                &output[(static_cast<std::size_t>(time) * window + offset) * mel_bins],
                features.data() + static_cast<std::size_t>(source) * mel_bins,
                row_bytes);
        }
    }
    return output;
}

}  // namespace fbank
