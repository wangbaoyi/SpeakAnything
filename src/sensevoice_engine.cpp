#include "sensevoice_engine.h"

#include "hotword_boost.h"

#include "ggml-alloc.h"
#include "ggml-backend.h"
#include "ggml-cpu.h"
#include "ggml.h"
#include "gguf.h"

#ifdef SENSEVOICE_WITH_COREML
#include "macos/coreml_sensevoice.h"

#include <sentencepiece_processor.h>

#include <fstream>
#include <sstream>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float layer_norm_epsilon = 1e-5F;
constexpr int sample_rate = 16'000;
constexpr int window_length = 400;
constexpr int frame_shift = 160;
constexpr int fft_size = 512;
constexpr int mel_bins = 80;
constexpr int lfr_window = 7;
constexpr int lfr_stride = 6;
constexpr float preemphasis = 0.97F;
constexpr float low_frequency = 20.0F;
constexpr float high_frequency = 8'000.0F;
constexpr int feature_dimension = 560;

float mel_frequency(float frequency) {
    return 1127.0F * std::log(1.0F + frequency / 700.0F);
}

void fft(std::vector<float>& real, std::vector<float>& imaginary, int size) {
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
        const double angle = -2.0 * 3.14159265358979323846 / length;
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

std::vector<float> compute_fbank(std::span<const float> audio, int& frames) {
    frames = 0;
    if (audio.size() < window_length) {
        return {};
    }

    std::vector<float> waveform(audio.begin(), audio.end());
    for (float& sample : waveform) {
        sample *= 32768.0F;
    }

    std::vector<float> window(window_length);
    for (int index = 0; index < window_length; ++index) {
        window[index] = 0.54F - 0.46F *
            std::cos(2.0F * 3.14159265358979323846F * index / (window_length - 1));
    }

    constexpr int spectrum_bins = fft_size / 2 + 1;
    const float bin_width = static_cast<float>(sample_rate) / fft_size;
    const float mel_low = mel_frequency(low_frequency);
    const float mel_high = mel_frequency(high_frequency);
    const float mel_step = (mel_high - mel_low) / (mel_bins + 1);
    std::vector<std::vector<float>> filters(
        mel_bins, std::vector<float>(spectrum_bins, 0.0F));
    for (int mel = 0; mel < mel_bins; ++mel) {
        const float left = mel_low + mel * mel_step;
        const float center = mel_low + (mel + 1) * mel_step;
        const float right = mel_low + (mel + 2) * mel_step;
        for (int bin = 0; bin < spectrum_bins; ++bin) {
            const float frequency = mel_frequency(bin_width * bin);
            if (frequency > left && frequency < right) {
                filters[mel][bin] = frequency <= center
                    ? (frequency - left) / (center - left)
                    : (right - frequency) / (right - center);
            }
        }
    }

    const int raw_frames =
        (static_cast<int>(waveform.size()) - window_length) / frame_shift + 1;
    std::vector<std::vector<float>> features(
        raw_frames, std::vector<float>(mel_bins));
    std::vector<float> real(fft_size);
    std::vector<float> imaginary(fft_size);
    std::vector<float> frame(window_length);
    constexpr float floor = 1.1920929e-07F;

    for (int time = 0; time < raw_frames; ++time) {
        const float* samples = waveform.data() + time * frame_shift;
        double mean = 0.0;
        for (int index = 0; index < window_length; ++index) {
            mean += samples[index];
        }
        mean /= window_length;
        for (int index = 0; index < window_length; ++index) {
            frame[index] = samples[index] - static_cast<float>(mean);
        }
        for (int index = window_length - 1; index > 0; --index) {
            frame[index] -= preemphasis * frame[index - 1];
        }
        frame[0] -= preemphasis * frame[0];
        for (int index = 0; index < fft_size; ++index) {
            real[index] = index < window_length ? frame[index] * window[index] : 0.0F;
            imaginary[index] = 0.0F;
        }
        fft(real, imaginary, fft_size);
        for (int mel = 0; mel < mel_bins; ++mel) {
            float energy = 0.0F;
            for (int bin = 0; bin < spectrum_bins; ++bin) {
                if (filters[mel][bin] > 0.0F) {
                    energy += filters[mel][bin] *
                        (real[bin] * real[bin] + imaginary[bin] * imaginary[bin]);
                }
            }
            features[time][mel] = std::log(std::max(energy, floor));
        }
    }

    constexpr int left_padding = (lfr_window - 1) / 2;
    frames = (raw_frames + lfr_stride - 1) / lfr_stride;
    std::vector<std::vector<float>> padded;
    padded.reserve(raw_frames + left_padding + lfr_window);
    for (int index = 0; index < left_padding; ++index) {
        padded.push_back(features.front());
    }
    padded.insert(padded.end(), features.begin(), features.end());
    while (static_cast<int>(padded.size()) <
           (frames - 1) * lfr_stride + lfr_window) {
        padded.push_back(features.back());
    }

    std::vector<float> output(static_cast<std::size_t>(frames) * feature_dimension);
    for (int time = 0; time < frames; ++time) {
        for (int offset = 0; offset < lfr_window; ++offset) {
            std::memcpy(
                &output[static_cast<std::size_t>(time) * feature_dimension +
                        offset * mel_bins],
                padded[time * lfr_stride + offset].data(),
                mel_bins * sizeof(float));
        }
    }
    return output;
}

struct ModelConfig {
    int model_dimension = 512;
    int attention_heads = 4;
    int encoder_blocks = 50;
    int tp_blocks = 20;
    int kernel_size = 11;
    int vocabulary_size = 25'055;
    int blank_id = 0;
};

struct Model {
    ModelConfig config;
    ggml_context* weights_context = nullptr;
    std::map<std::string, ggml_tensor*> tensors;

    ggml_tensor* get(const std::string& name) const {
        const auto iterator = tensors.find(name);
        if (iterator == tensors.end()) {
            throw std::runtime_error("missing model tensor: " + name);
        }
        return iterator->second;
    }
};

ggml_tensor* linear(
    ggml_context* context,
    ggml_tensor* weights,
    ggml_tensor* bias,
    ggml_tensor* input) {
    ggml_tensor* output = ggml_mul_mat(context, weights, input);
    return bias != nullptr ? ggml_add(context, output, bias) : output;
}

ggml_tensor* layer_norm(
    ggml_context* context,
    ggml_tensor* input,
    ggml_tensor* weights,
    ggml_tensor* bias) {
    return ggml_add(
        context,
        ggml_mul(context, ggml_norm(context, input, layer_norm_epsilon), weights),
        bias);
}

ggml_tensor* sanm_attention(
    ggml_context* context,
    const Model& model,
    const std::string& prefix,
    ggml_tensor* input,
    int frames) {
    const int dimension = model.config.model_dimension;
    const int heads = model.config.attention_heads;
    const int head_dimension = dimension / heads;
    const int kernel = model.config.kernel_size;
    ggml_tensor* query_key_value = linear(
        context,
        model.get(prefix + "linear_q_k_v.weight"),
        model.get(prefix + "linear_q_k_v.bias"),
        input);
    const std::size_t row_stride = query_key_value->nb[1];
    ggml_tensor* query = ggml_cont(
        context, ggml_view_2d(context, query_key_value, dimension, frames, row_stride, 0));
    ggml_tensor* key = ggml_cont(
        context,
        ggml_view_2d(
            context,
            query_key_value,
            dimension,
            frames,
            row_stride,
            static_cast<std::size_t>(dimension) * sizeof(float)));
    ggml_tensor* value = ggml_cont(
        context,
        ggml_view_2d(
            context,
            query_key_value,
            dimension,
            frames,
            row_stride,
            static_cast<std::size_t>(2 * dimension) * sizeof(float)));

    const int padding = (kernel - 1) / 2;
    ggml_tensor* fsmn_kernel = model.get(prefix + "fsmn_block.weight");
    // Zero-pad the frame axis on both sides. Metal only pads on the right, so
    // the left zeros are a scaled-by-zero slice concatenated in front; the
    // result is identical to ggml_pad_ext on every backend.
    ggml_tensor* padded_value = nullptr;
    if (frames >= padding) {
        ggml_tensor* left_zeros = ggml_scale(
            context, ggml_view_2d(context, value, dimension, padding, value->nb[1], 0), 0.0F);
        padded_value = ggml_concat(context, left_zeros, ggml_pad(context, value, 0, padding, 0, 0), 1);
    } else {
        padded_value = ggml_pad_ext(context, value, 0, 0, padding, padding, 0, 0, 0, 0);
    }
    ggml_tensor* fsmn = value;
    for (int tap = 0; tap < kernel; ++tap) {
        ggml_tensor* slice = ggml_view_2d(
            context,
            padded_value,
            dimension,
            frames,
            padded_value->nb[1],
            static_cast<std::size_t>(tap) * padded_value->nb[1]);
        ggml_tensor* tap_weights = ggml_view_1d(
            context,
            fsmn_kernel,
            dimension,
            static_cast<std::size_t>(tap) * fsmn_kernel->nb[1]);
        fsmn = ggml_add(
            context,
            fsmn,
            ggml_mul(context, ggml_cont(context, slice), tap_weights));
    }

    query = ggml_permute(
        context, ggml_reshape_3d(context, query, head_dimension, heads, frames), 0, 2, 1, 3);
    key = ggml_permute(
        context, ggml_reshape_3d(context, key, head_dimension, heads, frames), 0, 2, 1, 3);
    ggml_tensor* headed_value = ggml_cont(
        context,
        ggml_permute(
            context,
            ggml_reshape_3d(context, value, head_dimension, heads, frames),
            1,
            2,
            0,
            3));
    ggml_tensor* attention = ggml_soft_max(
        context,
        ggml_scale(
            context,
            ggml_mul_mat(context, key, query),
            1.0F / std::sqrt(static_cast<float>(head_dimension))));
    ggml_tensor* output = ggml_cont_2d(
        context,
        ggml_permute(context, ggml_mul_mat(context, headed_value, attention), 0, 2, 1, 3),
        dimension,
        frames);
    return ggml_add(
        context,
        linear(
            context,
            model.get(prefix + "linear_out.weight"),
            model.get(prefix + "linear_out.bias"),
            output),
        fsmn);
}

ggml_tensor* sanm_layer(
    ggml_context* context,
    const Model& model,
    const std::string& prefix,
    ggml_tensor* input,
    int frames,
    bool residual) {
    ggml_tensor* shortcut = input;
    ggml_tensor* hidden = layer_norm(
        context,
        input,
        model.get(prefix + "norm1.weight"),
        model.get(prefix + "norm1.bias"));
    ggml_tensor* attention = sanm_attention(
        context, model, prefix + "self_attn.", hidden, frames);
    input = residual ? ggml_add(context, shortcut, attention) : attention;
    shortcut = input;
    hidden = layer_norm(
        context,
        input,
        model.get(prefix + "norm2.weight"),
        model.get(prefix + "norm2.bias"));
    hidden = linear(
        context,
        model.get(prefix + "feed_forward.w_1.weight"),
        model.get(prefix + "feed_forward.w_1.bias"),
        hidden);
    hidden = ggml_relu(context, hidden);
    hidden = linear(
        context,
        model.get(prefix + "feed_forward.w_2.weight"),
        model.get(prefix + "feed_forward.w_2.bias"),
        hidden);
    return ggml_add(context, shortcut, hidden);
}

void add_position_encoding(std::vector<float>& input, int frames, int depth) {
    const double increment = std::log(10'000.0) / (depth / 2.0 - 1.0);
    for (int time = 0; time < frames; ++time) {
        const double position = time + 1;
        for (int index = 0; index < depth / 2; ++index) {
            const double inverse_scale = std::exp(index * -increment);
            const double value = position * inverse_scale;
            input[static_cast<std::size_t>(time) * depth + index] +=
                static_cast<float>(std::sin(value));
            input[static_cast<std::size_t>(time) * depth + depth / 2 + index] +=
                static_cast<float>(std::cos(value));
        }
    }
}

std::string trim_spaces(const std::string& input) {
    const std::size_t first = input.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = input.find_last_not_of(' ');
    return input.substr(first, last - first + 1);
}

std::string detokenize(
    const std::vector<int>& token_ids,
    const std::vector<std::string>& vocabulary) {
    std::string result;
    for (const int token_id : token_ids) {
        if (token_id < 0 || token_id >= static_cast<int>(vocabulary.size())) {
            continue;
        }
        const std::string& piece = vocabulary[token_id];
        if (piece.size() >= 2 && piece[0] == '<' && piece[1] == '|') {
            continue;
        }
        result += piece;
    }
    constexpr const char* sentencepiece_space = "\xe2\x96\x81";
    std::size_t position = 0;
    while ((position = result.find(sentencepiece_space)) != std::string::npos) {
        result.replace(position, 3, " ");
    }
    return trim_spaces(result);
}

// SenseVoice reports a laugh as a <|Laughter|> event, not as text, so a
// laugh would vanish from both the input and the spoken translation. Put it
// back as words the translator and the voice can carry.
std::string with_laughter(
    std::string text,
    const std::vector<int>& token_ids,
    const std::vector<std::string>& vocabulary) {
    bool laughed = false;
    for (const int token_id : token_ids) {
        if (token_id >= 0 && token_id < static_cast<int>(vocabulary.size()) &&
            vocabulary[token_id] == "<|Laughter|>") {
            laughed = true;
            break;
        }
    }
    if (!laughed) return text;
    for (const char* written : {"哈", "呵", "嘿", "ha", "Ha", "HA", "he", "He"}) {
        if (text.find(written) != std::string::npos) return text;
    }
    bool latin = false;
    for (const char character : text) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
            latin = true;
            break;
        }
    }
    if (text.empty()) return "哈哈";
    return latin ? "Haha, " + text : "哈哈，" + text;
}

} // namespace

struct SenseVoiceEngine::Impl {
    struct VocabularyPiece {
        std::string surface;
        int token_id = 0;
    };

    Model model;
    ggml_backend_t backend = nullptr;
    ggml_backend_buffer_type_t buffer_type = nullptr;
    std::vector<int> query_tokens;
    std::vector<std::string> vocabulary;
    std::array<std::vector<VocabularyPiece>, 256> vocabulary_pieces;
    std::unordered_map<std::string, std::vector<int>> hotword_token_cache;
    int threads = 8;
    bool is_loaded = false;
    // Metal (Apple GPU) view of the same weights; compute falls back to the
    // CPU backend when unavailable or when the graph uses an op it lacks.
    ggml_backend_t gpu_backend = nullptr;
    ggml_backend_buffer_t gpu_weights = nullptr;
    bool use_gpu = false;
    // SenseVoice language query id: 0 auto, 3 zh, 4 en, 7 yue, 11 ja, 12 ko.
    int language_id = 0;
    bool gpu_checked = false;
#ifdef SENSEVOICE_WITH_COREML
    std::unique_ptr<CoreMLSenseVoice> coreml;
    std::vector<float> cmvn_shift;
    std::vector<float> cmvn_scale;
#endif

    static bool matches_piece(std::string_view phrase, std::size_t position, std::string_view piece) {
        if (position + piece.size() > phrase.size()) {
            return false;
        }
        for (std::size_t index = 0; index < piece.size(); ++index) {
            const auto phrase_character = static_cast<unsigned char>(phrase[position + index]);
            const auto piece_character = static_cast<unsigned char>(piece[index]);
            if (phrase_character < 0x80U && piece_character < 0x80U) {
                if (std::tolower(phrase_character) != std::tolower(piece_character)) {
                    return false;
                }
            } else if (phrase_character != piece_character) {
                return false;
            }
        }
        return true;
    }

    void add_vocabulary_piece(std::string surface, int token_id) {
        if (surface.empty()) {
            return;
        }
        vocabulary_pieces[static_cast<unsigned char>(surface.front())].push_back(
            {std::move(surface), token_id});
    }

    void prepare_hotword_vocabulary() {
        constexpr std::string_view sentencepiece_space = "\xe2\x96\x81";
        const int count = std::min(
            static_cast<int>(vocabulary.size()), model.config.vocabulary_size);
        for (int token_id = 0; token_id < count; ++token_id) {
            const std::string& piece = vocabulary[token_id];
            if (piece.empty() || (piece.size() >= 2 && piece[0] == '<' && piece[1] == '|')) {
                continue;
            }
            std::string surface = piece;
            std::size_t position = 0;
            while ((position = surface.find(sentencepiece_space, position)) != std::string::npos) {
                surface.replace(position, sentencepiece_space.size(), " ");
                ++position;
            }
            add_vocabulary_piece(surface, token_id);
            if (surface.starts_with(' ') && surface.size() > 1) {
                add_vocabulary_piece(surface.substr(1), token_id);
            }
        }
    }

    std::vector<int> tokenize_hotword(std::string_view phrase) {
        const auto cached = hotword_token_cache.find(std::string(phrase));
        if (cached != hotword_token_cache.end()) {
            return cached->second;
        }
        std::vector<std::vector<int>> paths(phrase.size() + 1);
        std::vector<bool> reachable(phrase.size() + 1, false);
        reachable[0] = true;
        for (std::size_t position = 0; position < phrase.size(); ++position) {
            if (!reachable[position]) {
                continue;
            }
            const auto lead = static_cast<unsigned char>(phrase[position]);
            for (const VocabularyPiece& piece : vocabulary_pieces[lead]) {
                if (!matches_piece(phrase, position, piece.surface)) {
                    continue;
                }
                const std::size_t next = position + piece.surface.size();
                std::vector<int> candidate = paths[position];
                candidate.push_back(piece.token_id);
                if (!reachable[next] || candidate.size() < paths[next].size()) {
                    paths[next] = std::move(candidate);
                    reachable[next] = true;
                }
            }
        }
        std::vector<int> result = reachable.back() ? std::move(paths.back()) : std::vector<int>{};
        hotword_token_cache.emplace(std::string(phrase), result);
        return result;
    }

    // Unified memory: wrap the already-loaded weights in a GPU buffer in place.
    void try_enable_gpu() {
#ifdef __APPLE__
        if (gpu_disabled()) return;
        ggml_backend_dev_t device = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU);
        if (device == nullptr || model.weights_context == nullptr) return;
        ggml_backend_dev_props props;
        ggml_backend_dev_get_props(device, &props);
        if (!props.caps.buffer_from_host_ptr) return;
        size_t largest = 0;
        for (ggml_tensor* tensor = ggml_get_first_tensor(model.weights_context); tensor != nullptr;
             tensor = ggml_get_next_tensor(model.weights_context, tensor)) {
            largest = std::max(largest, ggml_nbytes(tensor));
        }
        gpu_weights = ggml_backend_dev_buffer_from_host_ptr(
            device, ggml_get_mem_buffer(model.weights_context),
            ggml_get_mem_size(model.weights_context), largest);
        if (gpu_weights == nullptr) return;
        gpu_backend = ggml_backend_dev_init(device, nullptr);
        if (gpu_backend == nullptr) {
            ggml_backend_buffer_free(gpu_weights);
            gpu_weights = nullptr;
            return;
        }
        for (ggml_tensor* tensor = ggml_get_first_tensor(model.weights_context); tensor != nullptr;
             tensor = ggml_get_next_tensor(model.weights_context, tensor)) {
            tensor->buffer = gpu_weights;
        }
        use_gpu = true;
#endif
    }

    static bool gpu_disabled() {
        const char* value = std::getenv("SENSEVOICE_NO_GPU");
        return value != nullptr && value[0] == '1';
    }

    ~Impl() {
        if (gpu_backend != nullptr) {
            ggml_backend_free(gpu_backend);
        }
        if (gpu_weights != nullptr) {
            ggml_backend_buffer_free(gpu_weights);
        }
        if (backend != nullptr) {
            ggml_backend_free(backend);
        }
        if (model.weights_context != nullptr) {
            ggml_free(model.weights_context);
        }
    }
};

SenseVoiceEngine::SenseVoiceEngine() : impl_(std::make_unique<Impl>()) {}

SenseVoiceEngine::~SenseVoiceEngine() = default;

bool SenseVoiceEngine::load(
    const std::filesystem::path& model_path,
    int threads,
    std::string& error) {
    if (impl_->is_loaded) {
        error = "SenseVoice model is already loaded";
        return false;
    }
#ifdef SENSEVOICE_WITH_COREML
    if (std::filesystem::is_directory(model_path)) {
        return load_coreml(model_path, error);
    }
#endif

    impl_->backend = ggml_backend_cpu_init();
    if (impl_->backend == nullptr) {
        error = "failed to initialize ggml CPU backend";
        return false;
    }
    impl_->buffer_type = ggml_backend_get_default_buffer_type(impl_->backend);
    if (impl_->buffer_type == nullptr) {
        error = "failed to initialize ggml CPU buffer type";
        return false;
    }

    gguf_init_params parameters{false, &impl_->model.weights_context};
    const auto path_u8 = model_path.u8string();
    const std::string path_utf8(
        reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
    gguf_context* gguf = gguf_init_from_file(path_utf8.c_str(), parameters);
    if (gguf == nullptr) {
        error = "failed to load GGUF model: " + model_path.string();
        return false;
    }

    auto read_u32 = [&](const char* key, int fallback) {
        const std::int64_t index = gguf_find_key(gguf, key);
        return index < 0 ? fallback : static_cast<int>(gguf_get_val_u32(gguf, index));
    };
    impl_->model.config.model_dimension = read_u32("sv.output_size", 512);
    impl_->model.config.attention_heads = read_u32("sv.attention_heads", 4);
    impl_->model.config.encoder_blocks = read_u32("sv.num_blocks", 50);
    impl_->model.config.tp_blocks = read_u32("sv.tp_blocks", 20);
    impl_->model.config.kernel_size = read_u32("sv.kernel_size", 11);
    impl_->model.config.vocabulary_size = read_u32("sv.vocab_size", 25'055);
    impl_->model.config.blank_id = read_u32("sv.blank_id", 0);

    const std::int64_t query_index = gguf_find_key(gguf, "sv.query_tokens");
    const int query_count = query_index < 0
        ? 0
        : static_cast<int>(gguf_get_arr_n(gguf, query_index));
    impl_->query_tokens.resize(query_count);
    if (query_count > 0) {
        const auto* data = static_cast<const std::int32_t*>(gguf_get_arr_data(gguf, query_index));
        std::copy(data, data + query_count, impl_->query_tokens.begin());
    }

    const std::int64_t vocabulary_index = gguf_find_key(gguf, "sv.vocab");
    if (vocabulary_index >= 0) {
        const int count = static_cast<int>(gguf_get_arr_n(gguf, vocabulary_index));
        impl_->vocabulary.resize(count);
        for (int index = 0; index < count; ++index) {
            const char* piece = gguf_get_arr_str(gguf, vocabulary_index, index);
            impl_->vocabulary[index] = piece != nullptr ? piece : "";
        }
    }
    impl_->prepare_hotword_vocabulary();

    const int tensor_count = static_cast<int>(gguf_get_n_tensors(gguf));
    for (int index = 0; index < tensor_count; ++index) {
        const char* name = gguf_get_tensor_name(gguf, index);
        impl_->model.tensors[name] = ggml_get_tensor(impl_->model.weights_context, name);
    }
    gguf_free(gguf);

    if (impl_->query_tokens.empty() || impl_->vocabulary.empty()) {
        error = "GGUF model is missing SenseVoice query tokens or vocabulary";
        return false;
    }
    try {
        impl_->model.get("embed.weight");
        impl_->model.get("ctc.ctc_lo.weight");
        impl_->model.get("ctc.ctc_lo.bias");
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    impl_->threads = std::max(1, threads);
    ggml_backend_cpu_set_n_threads(impl_->backend, impl_->threads);
    impl_->try_enable_gpu();
    impl_->is_loaded = true;
    if (impl_->use_gpu) {
        // Settles GPU-vs-CPU (op support) and compiles the Metal kernels now,
        // so the first dictation isn't the slow one.
        const std::vector<float> silence(sample_rate, 0.0F);
        try {
            recognize(silence);
        } catch (const std::exception&) {
            impl_->use_gpu = false;
        }
    }
    return true;
}

bool SenseVoiceEngine::loaded() const {
    return impl_->is_loaded;
}

void SenseVoiceEngine::set_language(const std::string& language) {
    static const std::pair<const char*, int> ids[] = {
        {"zh", 3}, {"en", 4}, {"yue", 7}, {"ja", 11}, {"ko", 12}};
    impl_->language_id = 0;
    for (const auto& [code, id] : ids) {
        if (language == code) impl_->language_id = id;
    }
    // GGUF builds read the query from query_tokens; its first slot is the language.
    if (!impl_->query_tokens.empty()) impl_->query_tokens[0] = impl_->language_id;
}

void SenseVoiceEngine::reset() {
    impl_ = std::make_unique<Impl>();
}

const char* SenseVoiceEngine::device() const {
#ifdef SENSEVOICE_WITH_COREML
    if (impl_->coreml != nullptr) return "Neural Engine";
#endif
    return impl_->use_gpu ? "GPU (Metal)" : "CPU";
}

SenseVoiceResult SenseVoiceEngine::recognize(
    std::span<const float> pcm_16khz_mono,
    std::span<const HotwordBoostPhrase> hotwords) {
    if (!impl_->is_loaded) {
        throw std::runtime_error("SenseVoice model is not loaded");
    }

    SenseVoiceResult result;
    result.audio_ms = static_cast<int>(pcm_16khz_mono.size() * 1000 / sample_rate);
    int feature_frames = 0;
    std::vector<float> features = compute_fbank(pcm_16khz_mono, feature_frames);
    if (features.empty()) {
        return result;
    }

#ifdef SENSEVOICE_WITH_COREML
    if (impl_->coreml != nullptr) {
        return recognize_coreml(std::move(features), feature_frames, hotwords, result);
    }
#endif
    const auto started = std::chrono::steady_clock::now();
    const int query_count = static_cast<int>(impl_->query_tokens.size());
    const int total_frames = query_count + feature_frames;
    std::vector<float> input(static_cast<std::size_t>(total_frames) * feature_dimension);
    ggml_tensor* embedding = impl_->model.get("embed.weight");
    if (embedding->type != GGML_TYPE_F32 || embedding->data == nullptr) {
        throw std::runtime_error("SenseVoice query embedding must be resident F32 data");
    }
    const auto* embedding_data = static_cast<const float*>(embedding->data);
    for (int index = 0; index < query_count; ++index) {
        std::memcpy(
            &input[static_cast<std::size_t>(index) * feature_dimension],
            &embedding_data[static_cast<std::size_t>(impl_->query_tokens[index]) * feature_dimension],
            feature_dimension * sizeof(float));
    }
    std::memcpy(
        &input[static_cast<std::size_t>(query_count) * feature_dimension],
        features.data(),
        features.size() * sizeof(float));
    const float input_scale = std::sqrt(static_cast<float>(impl_->model.config.model_dimension));
    for (float& value : input) {
        value *= input_scale;
    }
    add_position_encoding(input, total_frames, feature_dimension);

    ggml_init_params context_parameters{
        static_cast<std::size_t>(16) * 1024 * 1024,
        nullptr,
        true,
    };
    ggml_context* context = ggml_init(context_parameters);
    if (context == nullptr) {
        throw std::runtime_error("failed to allocate ggml graph context");
    }

    ggml_gallocr_t allocator = nullptr;
    try {
        ggml_tensor* graph_input = ggml_new_tensor_2d(
            context, GGML_TYPE_F32, feature_dimension, total_frames);
        ggml_set_input(graph_input);
        ggml_tensor* hidden = sanm_layer(
            context,
            impl_->model,
            "encoder.encoders0.0.",
            graph_input,
            total_frames,
            false);
        for (int index = 0; index < impl_->model.config.encoder_blocks - 1; ++index) {
            hidden = sanm_layer(
                context,
                impl_->model,
                "encoder.encoders." + std::to_string(index) + ".",
                hidden,
                total_frames,
                true);
        }
        hidden = layer_norm(
            context,
            hidden,
            impl_->model.get("encoder.after_norm.weight"),
            impl_->model.get("encoder.after_norm.bias"));
        for (int index = 0; index < impl_->model.config.tp_blocks; ++index) {
            hidden = sanm_layer(
                context,
                impl_->model,
                "encoder.tp_encoders." + std::to_string(index) + ".",
                hidden,
                total_frames,
                true);
        }
        hidden = layer_norm(
            context,
            hidden,
            impl_->model.get("encoder.tp_norm.weight"),
            impl_->model.get("encoder.tp_norm.bias"));
        ggml_tensor* logits = linear(
            context,
            impl_->model.get("ctc.ctc_lo.weight"),
            impl_->model.get("ctc.ctc_lo.bias"),
            hidden);
        ggml_set_output(logits);

        ggml_cgraph* graph = ggml_new_graph_custom(context, 32'768, false);
        ggml_build_forward_expand(graph, logits);
        if (impl_->use_gpu && !impl_->gpu_checked) {
            impl_->gpu_checked = true;
            for (int index = 0; index < ggml_graph_n_nodes(graph); ++index) {
                if (!ggml_backend_supports_op(impl_->gpu_backend, ggml_graph_node(graph, index))) {
                    if (std::getenv("SENSEVOICE_DEBUG_GPU") != nullptr) {
                        ggml_tensor* node = ggml_graph_node(graph, index);
                        std::fprintf(stderr, "Metal lacks %s (%s) type %s src0 %s\n", ggml_op_desc(node), node->name,
                            ggml_type_name(node->type), node->src[0] ? ggml_type_name(node->src[0]->type) : "-");
                        continue;
                    }
                    impl_->use_gpu = false;
                    break;
                }
            }
        }
        ggml_backend_t compute_backend = impl_->use_gpu ? impl_->gpu_backend : impl_->backend;
        allocator = ggml_gallocr_new(ggml_backend_get_default_buffer_type(compute_backend));
        if (!ggml_gallocr_alloc_graph(allocator, graph)) {
            throw std::runtime_error("failed to allocate SenseVoice compute graph");
        }
        ggml_backend_tensor_set(graph_input, input.data(), 0, ggml_nbytes(graph_input));
        if (ggml_backend_graph_compute(compute_backend, graph) != GGML_STATUS_SUCCESS) {
            throw std::runtime_error("SenseVoice graph computation failed");
        }

        const int vocabulary_size = impl_->model.config.vocabulary_size;
        std::vector<float> host_logits(
            static_cast<std::size_t>(vocabulary_size) * total_frames);
        ggml_backend_tensor_get(logits, host_logits.data(), 0, ggml_nbytes(logits));
        std::vector<CtcHotword> encoded_hotwords;
        encoded_hotwords.reserve(hotwords.size());
        for (const HotwordBoostPhrase& hotword : hotwords) {
            std::vector<int> token_ids = impl_->tokenize_hotword(hotword.phrase);
            if (token_ids.size() >= 2 && hotword.boost > 0.0F) {
                encoded_hotwords.push_back({std::move(token_ids), hotword.boost});
            }
        }
        const CtcHotwordDecodeResult decoded = decode_ctc_with_hotword_bias(
            host_logits,
            total_frames,
            vocabulary_size,
            impl_->model.config.blank_id,
            encoded_hotwords);
        result.text = with_laughter(
            detokenize(decoded.token_ids, impl_->vocabulary), decoded.token_ids, impl_->vocabulary);
        result.hotword_bias_applied = decoded.used_hotword_bias;
    } catch (...) {
        if (allocator != nullptr) {
            ggml_gallocr_free(allocator);
        }
        ggml_free(context);
        throw;
    }

    ggml_gallocr_free(allocator);
    ggml_free(context);
    result.inference_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started)
            .count());
    return result;
}

#ifdef SENSEVOICE_WITH_COREML

namespace {

// am.mvn is a Kaldi nnet text file; the vectors after <AddShift> and
// <Rescale> are the per-dimension shift and scale.
bool read_cmvn(const std::filesystem::path& path, std::vector<float>& shift,
               std::vector<float>& scale) {
    std::ifstream file(path);
    std::string line;
    std::vector<float>* pending = nullptr;
    while (std::getline(file, line)) {
        if (line.find("<AddShift>") != std::string::npos) pending = &shift;
        else if (line.find("<Rescale>") != std::string::npos) pending = &scale;
        else if (pending != nullptr && line.find("<LearnRateCoef>") != std::string::npos) {
            const std::size_t open = line.find('[');
            const std::size_t close = line.find(']');
            std::istringstream values(line.substr(open + 1, close - open - 1));
            float value = 0.0F;
            while (values >> value) pending->push_back(value);
            pending = nullptr;
        }
    }
    return !shift.empty() && shift.size() == scale.size();
}

} // namespace

// model_directory holds sensevoice.mlpackage, am.mvn and the SentencePiece
// model from FunAudioLLM/SenseVoiceSmall.
bool SenseVoiceEngine::load_coreml(const std::filesystem::path& model_directory,
                                   std::string& error) {
    if (!read_cmvn(model_directory / "am.mvn", impl_->cmvn_shift, impl_->cmvn_scale) ||
        impl_->cmvn_shift.size() < static_cast<std::size_t>(feature_dimension)) {
        error = "missing or invalid am.mvn in " + model_directory.string();
        return false;
    }
    sentencepiece::SentencePieceProcessor tokenizer;
    if (!tokenizer.Load((model_directory / "chn_jpn_yue_eng_ko_spectok.bpe.model").string()).ok()) {
        error = "failed to load the SenseVoice tokenizer";
        return false;
    }
    impl_->vocabulary.resize(static_cast<std::size_t>(tokenizer.GetPieceSize()));
    for (int id = 0; id < tokenizer.GetPieceSize(); ++id) {
        impl_->vocabulary[static_cast<std::size_t>(id)] = tokenizer.IdToPiece(id);
    }
    impl_->model.config.vocabulary_size = static_cast<int>(impl_->vocabulary.size());
    impl_->model.config.blank_id = 0;
    impl_->prepare_hotword_vocabulary();

    auto coreml = std::make_unique<CoreMLSenseVoice>();
    if (!coreml->load(model_directory / "sensevoice.mlpackage", error)) return false;
    impl_->coreml = std::move(coreml);
    impl_->is_loaded = true;
    return true;
}

SenseVoiceResult SenseVoiceEngine::recognize_coreml(
    std::vector<float> features,
    int frames,
    std::span<const HotwordBoostPhrase> hotwords,
    SenseVoiceResult result) {
    // Same queries as the GGUF build: the chosen language (auto unless
    // set_language picked one) and text normalization withitn (14).
    constexpr int style_with_itn = 14;
    const auto started = std::chrono::steady_clock::now();
    for (int time = 0; time < frames; ++time) {
        float* row = &features[static_cast<std::size_t>(time) * feature_dimension];
        for (int index = 0; index < feature_dimension; ++index) {
            row[index] = (row[index] + impl_->cmvn_shift[index]) * impl_->cmvn_scale[index];
        }
    }
    std::vector<float> logits;
    int vocabulary_size = 0;
    std::string error;
    if (!impl_->coreml->run(features, frames, impl_->language_id, style_with_itn, logits,
                            vocabulary_size, error)) {
        throw std::runtime_error(error);
    }
    std::vector<CtcHotword> encoded_hotwords;
    for (const HotwordBoostPhrase& hotword : hotwords) {
        std::vector<int> token_ids = impl_->tokenize_hotword(hotword.phrase);
        if (token_ids.size() >= 2 && hotword.boost > 0.0F) {
            encoded_hotwords.push_back({std::move(token_ids), hotword.boost});
        }
    }
    const CtcHotwordDecodeResult decoded = decode_ctc_with_hotword_bias(
        logits, frames + 4, vocabulary_size, 0, encoded_hotwords);
    result.text = with_laughter(
        detokenize(decoded.token_ids, impl_->vocabulary), decoded.token_ids, impl_->vocabulary);
    result.hotword_bias_applied = decoded.used_hotword_bias;
    result.inference_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    return result;
}

#endif
