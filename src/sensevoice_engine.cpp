#include "sensevoice_engine.h"

#include "fbank.h"
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
#include <fstream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr float layer_norm_epsilon = 1e-5F;
constexpr int sample_rate = 16'000;
// The shared fbank module produces 80 mel bins; SenseVoice stacks 7 of them
// every 6 frames, which is its 560-wide model input.
constexpr int mel_bins = 80;
constexpr int lfr_window = 7;
constexpr int lfr_stride = 6;
constexpr int feature_dimension = 560;
static_assert(feature_dimension == lfr_window * mel_bins);

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
    ComputePolicy policy = ComputePolicy::Auto;
    GpuCapability gpu;
    // Copies of the weights on the CPU and, when selected, on the GPU. The
    // GPU copy is a buffer allocated by the backend, so the same code works
    // for discrete memory (CUDA) and unified memory (Metal). Compute falls
    // back to the CPU backend when the backend is unavailable or the graph
    // uses an op it lacks.
    ggml_backend_t gpu_backend = nullptr;
    ggml_backend_buffer_t gpu_weights = nullptr;
    ggml_backend_buffer_t weights = nullptr;
    bool use_gpu = false;
    // GGUF tensor layout, kept so the CPU weights can be rebuilt from the file
    // if a GPU graph turns out to be unsupported mid-run.
    struct WeightChunk {
        ggml_tensor* tensor = nullptr;
        std::uint64_t offset = 0;
        std::size_t size = 0;
    };
    std::filesystem::path model_path;
    std::vector<WeightChunk> weight_chunks;
    // Query-token rows copied out of embed.weight before a GPU upload. The
    // query embedding is built on the host, and a device tensor's data pointer
    // is not readable there.
    std::unordered_map<int, std::vector<float>> query_embeddings;
    std::string device_label = "CPU";
    std::string gpu_unsupported_op;
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

    // Allocates a buffer for every weight tensor on `backend` and uploads the
    // tensor bytes from the GGUF file. The GGUF is opened with no_alloc, so
    // tensor data has no host allocation to reuse or invalidate.
    bool upload_weights(
        ggml_backend_t backend, ggml_backend_buffer_t& output, std::string& error) {
        for (ggml_tensor* tensor = ggml_get_first_tensor(model.weights_context);
             tensor != nullptr; tensor = ggml_get_next_tensor(model.weights_context, tensor)) {
            if (tensor->view_src == nullptr) {
                tensor->data = nullptr;
                tensor->buffer = nullptr;
            }
        }
        ggml_backend_buffer_t buffer =
            ggml_backend_alloc_ctx_tensors(model.weights_context, backend);
        if (buffer == nullptr) {
            error = "failed to allocate model weights on " +
                std::string(ggml_backend_name(backend));
            return false;
        }
        std::ifstream file(model_path, std::ios::binary);
        if (!file) {
            ggml_backend_buffer_free(buffer);
            error = "failed to reopen " + model_path.string();
            return false;
        }
        std::vector<char> bytes;
        for (const WeightChunk& chunk : weight_chunks) {
            bytes.resize(chunk.size);
            file.seekg(static_cast<std::streamoff>(chunk.offset));
            if (!file.read(bytes.data(), static_cast<std::streamsize>(chunk.size))) {
                ggml_backend_buffer_free(buffer);
                error = "truncated GGUF tensor data";
                return false;
            }
            ggml_backend_tensor_set(chunk.tensor, bytes.data(), 0, chunk.size);
        }
        output = buffer;
        return true;
    }

    bool enable_gpu(std::string& error) {
        if (gpu_disabled()) {
            error = "GPU use is disabled by SENSEVOICE_NO_GPU";
            return false;
        }
        gpu = detect_gpu_capability();
        if (!gpu.available) {
            error = "no ggml GPU backend/device is available";
            return false;
        }
        auto* device = static_cast<ggml_backend_dev_t>(gpu.device_handle);
        gpu_backend = ggml_backend_dev_init(device, nullptr);
        if (gpu_backend == nullptr) {
            error = "failed to initialize the " + gpu.backend + " backend";
            return false;
        }
        if (!upload_weights(gpu_backend, gpu_weights, error)) {
            ggml_backend_free(gpu_backend);
            gpu_backend = nullptr;
            return false;
        }
        use_gpu = true;
        device_label = compute_device_label(ComputeDevice::Gpu, gpu);
        return true;
    }

    // Releases the GPU resources and rebuilds the weights on the CPU backend.
    // The caller is responsible for rebuilding any graph that views weights.
    void disable_gpu() {
        if (gpu_weights != nullptr) {
            ggml_backend_buffer_free(gpu_weights);
            gpu_weights = nullptr;
        }
        if (gpu_backend != nullptr) {
            ggml_backend_free(gpu_backend);
            gpu_backend = nullptr;
        }
        use_gpu = false;
        device_label = "CPU";
        std::string error;
        if (weights == nullptr && !upload_weights(backend, weights, error)) {
            throw std::runtime_error("failed to restore CPU weights: " + error);
        }
    }

    // Copies the rows of embed.weight that set_language can select. Runs after
    // the weights exist on whichever backend owns them; ggml_backend_tensor_get
    // copies from device memory when needed.
    void cache_query_embeddings() {
        ggml_tensor* embedding = model.get("embed.weight");
        if (embedding->type != GGML_TYPE_F32 || embedding->data == nullptr) {
            throw std::runtime_error("SenseVoice query embedding must be resident F32 data");
        }
        const int dimension = feature_dimension;
        const std::size_t element_count = ggml_nelements(embedding);
        std::vector<int> ids = query_tokens;
        // Every language id set_language can write into query_tokens[0].
        for (const int id : {0, 3, 4, 7, 11, 12}) ids.push_back(id);
        for (const int token_id : ids) {
            if (token_id < 0 || token_id >= model.config.vocabulary_size) continue;
            if (static_cast<std::size_t>(token_id + 1) * dimension > element_count) continue;
            if (query_embeddings.contains(token_id)) continue;
            std::vector<float> row(static_cast<std::size_t>(dimension));
            ggml_backend_tensor_get(
                embedding, row.data(),
                static_cast<std::size_t>(token_id) * dimension * sizeof(float),
                row.size() * sizeof(float));
            query_embeddings.emplace(token_id, std::move(row));
        }
    }

    const std::vector<float>& query_embedding(int token_id) const {
        const auto iterator = query_embeddings.find(token_id);
        if (iterator == query_embeddings.end()) {
            throw std::runtime_error("missing SenseVoice query embedding row");
        }
        return iterator->second;
    }

    static bool gpu_disabled() {
        const char* value = std::getenv("SENSEVOICE_NO_GPU");
        return value != nullptr && value[0] == '1';
    }

    ~Impl() {
        // The weight buffer and backend must outlive no tensor, but the buffer
        // has to be released before the backend that owns it.
        if (gpu_weights != nullptr) {
            ggml_backend_buffer_free(gpu_weights);
        }
        if (weights != nullptr) {
            ggml_backend_buffer_free(weights);
        }
        if (gpu_backend != nullptr) {
            ggml_backend_free(gpu_backend);
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
    std::string& error,
    ComputePolicy policy) {
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

    // Metadata only: ggml_backend_alloc_ctx_tensors owns the tensor memory on
    // whichever backend runs the model, and the bytes are streamed from the
    // file afterwards. This also keeps a CPU fallback independent of any GPU
    // allocation that was already tried.
    gguf_init_params parameters{true, &impl_->model.weights_context};
    impl_->model_path = model_path;
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
    const std::uint64_t data_offset = gguf_get_data_offset(gguf);
    impl_->weight_chunks.clear();
    impl_->weight_chunks.reserve(static_cast<std::size_t>(tensor_count));
    for (int index = 0; index < tensor_count; ++index) {
        const char* name = gguf_get_tensor_name(gguf, index);
        ggml_tensor* tensor = ggml_get_tensor(impl_->model.weights_context, name);
        impl_->model.tensors[name] = tensor;
        impl_->weight_chunks.push_back({
            tensor,
            data_offset + gguf_get_tensor_offset(gguf, index),
            ggml_nbytes(tensor),
        });
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
    impl_->policy = policy;

    const ComputeResolution resolution =
        resolve_compute(policy, detect_gpu_capability());
    if (!resolution.ok()) {
        error = resolution.error;
        return false;
    }
    if (resolution.device == ComputeDevice::Gpu) {
        std::string gpu_error;
        if (!impl_->enable_gpu(gpu_error)) {
            if (policy == ComputePolicy::Gpu) {
                error = gpu_error;
                return false;
            }
            // Auto falls back to the CPU backend.
            impl_->device_label = "CPU";
        }
    }
    if (!impl_->use_gpu) {
        std::string weights_error;
        if (!impl_->upload_weights(impl_->backend, impl_->weights, weights_error)) {
            error = weights_error;
            return false;
        }
    }
    try {
        impl_->cache_query_embeddings();
    } catch (const std::exception& exception) {
        error = exception.what();
        return false;
    }

    impl_->is_loaded = true;
    if (impl_->use_gpu) {
        // Settles GPU-vs-CPU (op support) and compiles the GPU kernels now,
        // so the first dictation isn't the slow one, and so device() reports
        // what recognition will actually use.
        const std::vector<float> silence(sample_rate, 0.0F);
        try {
            recognize(silence);
        } catch (const std::exception& exception) {
            if (policy == ComputePolicy::Gpu) {
                error = std::string("GPU inference is unavailable: ") + exception.what();
                impl_->disable_gpu();
                impl_->is_loaded = false;
                return false;
            }
            impl_->disable_gpu();
        }
        if (!impl_->use_gpu && policy == ComputePolicy::Gpu) {
            error = "GPU inference is unavailable: " +
                (impl_->gpu_unsupported_op.empty()
                     ? std::string("the GPU backend rejected the graph")
                     : "the GPU backend does not support " + impl_->gpu_unsupported_op);
            impl_->is_loaded = false;
            return false;
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
    return impl_->device_label.c_str();
}

SenseVoiceResult SenseVoiceEngine::recognize(
    std::span<const float> pcm_16khz_mono,
    std::span<const HotwordBoostPhrase> hotwords) {
    if (!impl_->is_loaded) {
        throw std::runtime_error("SenseVoice model is not loaded");
    }

    SenseVoiceResult result;
    result.audio_ms = static_cast<int>(pcm_16khz_mono.size() * 1000 / sample_rate);
    // Raw log-mel frames are computed in parallel (up to 4 workers, bounded by
    // the engine's thread count) and then stacked on this thread. Frames are
    // independent, so the result is identical to the serial path;
    // SENSEVOICE_SERIAL_FBANK=1 forces that path for benchmarks.
    int raw_frames = 0;
    const fbank::Config fbank_config;
    const std::vector<float> log_mel = fbank::compute_log_mel(
        pcm_16khz_mono, fbank_config, raw_frames, impl_->threads);
    int feature_frames = 0;
    std::vector<float> features = fbank::stack_lfr(
        log_mel,
        raw_frames,
        fbank_config.mel_bins,
        lfr_window,
        lfr_stride,
        feature_frames);
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
    for (int index = 0; index < query_count; ++index) {
        const std::vector<float>& row = impl_->query_embedding(impl_->query_tokens[index]);
        std::memcpy(
            &input[static_cast<std::size_t>(index) * feature_dimension],
            row.data(),
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

    ggml_context* context = nullptr;
    ggml_gallocr_t allocator = nullptr;
    bool computed = false;
    // Two attempts at most: one on the GPU, then one rebuild on the CPU when
    // an Auto policy discovers the GPU cannot run the graph. The graph must be
    // rebuilt for a fallback because its views captured the weight pointers.
    for (int attempt = 0; attempt < 2 && !computed; ++attempt) {
        context = ggml_init(context_parameters);
        if (context == nullptr) {
            throw std::runtime_error("failed to allocate ggml graph context");
        }
        allocator = nullptr;
        bool retry = false;
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
                    ggml_tensor* node = ggml_graph_node(graph, index);
                    if (ggml_backend_supports_op(impl_->gpu_backend, node)) continue;
                    impl_->gpu_unsupported_op =
                        std::string(ggml_op_desc(node)) + " (" + ggml_type_name(node->type) + ")";
                    if (std::getenv("SENSEVOICE_DEBUG_GPU") != nullptr) {
                        std::fprintf(stderr, "%s lacks %s (%s) type %s src0 %s\n",
                            impl_->gpu.backend.c_str(), ggml_op_desc(node), node->name,
                            ggml_type_name(node->type),
                            node->src[0] ? ggml_type_name(node->src[0]->type) : "-");
                    }
                    if (impl_->policy == ComputePolicy::Gpu) {
                        throw std::runtime_error("the " + impl_->gpu.backend +
                            " backend does not support " + impl_->gpu_unsupported_op);
                    }
                    impl_->disable_gpu();
                    retry = true;
                    break;
                }
            }
            if (!retry) {
                ggml_backend_t compute_backend =
                    impl_->use_gpu ? impl_->gpu_backend : impl_->backend;
                allocator = ggml_gallocr_new(
                    ggml_backend_get_default_buffer_type(compute_backend));
                if (allocator == nullptr || !ggml_gallocr_alloc_graph(allocator, graph)) {
                    if (impl_->use_gpu && impl_->policy != ComputePolicy::Gpu) {
                        impl_->gpu_unsupported_op = "graph allocation";
                        impl_->disable_gpu();
                        retry = true;
                    } else {
                        throw std::runtime_error("failed to allocate SenseVoice compute graph");
                    }
                }
            }
            if (!retry) {
                ggml_backend_t compute_backend =
                    impl_->use_gpu ? impl_->gpu_backend : impl_->backend;
                ggml_backend_tensor_set(
                    graph_input, input.data(), 0, ggml_nbytes(graph_input));
                if (ggml_backend_graph_compute(compute_backend, graph) != GGML_STATUS_SUCCESS) {
                    if (impl_->use_gpu && impl_->policy != ComputePolicy::Gpu) {
                        impl_->gpu_unsupported_op = "graph execution";
                        impl_->disable_gpu();
                        retry = true;
                    } else {
                        throw std::runtime_error("SenseVoice graph computation failed");
                    }
                }
            }
            if (!retry) {
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
                    detokenize(decoded.token_ids, impl_->vocabulary),
                    decoded.token_ids,
                    impl_->vocabulary);
                result.hotword_bias_applied = decoded.used_hotword_bias;
                computed = true;
            }
        } catch (...) {
            if (allocator != nullptr) {
                ggml_gallocr_free(allocator);
            }
            ggml_free(context);
            throw;
        }
        if (retry) {
            if (allocator != nullptr) {
                ggml_gallocr_free(allocator);
                allocator = nullptr;
            }
            ggml_free(context);
            context = nullptr;
        }
    }
    if (!computed || context == nullptr) {
        throw std::runtime_error("SenseVoice graph computation failed");
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
