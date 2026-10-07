#include "whisper_engine.h"

#include "whisper.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <string_view>
#include <utility>

namespace {

void silence_logs() {
    static std::once_flag once;
    std::call_once(once, [] { whisper_log_set([](ggml_log_level, const char*, void*) {}, nullptr); });
}

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

} // namespace

struct WhisperEngine::Impl {
    whisper_context* context = nullptr;
    std::string language = "auto";
    int threads = 4;
    bool gpu = false;
    bool neural_engine = false;

    ~Impl() {
        if (context != nullptr) whisper_free(context);
    }
};

WhisperEngine::WhisperEngine() : impl_(std::make_unique<Impl>()) {}
WhisperEngine::~WhisperEngine() = default;

bool WhisperEngine::load(const std::filesystem::path& model_path, std::string language,
                         int threads, std::string& error) {
    silence_logs();
    if (!std::filesystem::is_regular_file(model_path)) {
        error = "missing Whisper model " + model_path.string();
        return false;
    }
    whisper_context_params params = whisper_context_default_params();
    params.use_gpu = true;
    params.flash_attn = true;
    impl_->context = whisper_init_from_file_with_params(model_path.string().c_str(), params);
    if (impl_->context == nullptr) {
        error = "failed to load Whisper model";
        return false;
    }
    impl_->language = language.empty() ? "auto" : std::move(language);
    impl_->threads = std::max(1, threads);
#ifdef __APPLE__
    impl_->gpu = true;
    // whisper.cpp picks up the Core ML encoder from this exact name.
    std::filesystem::path encoder = model_path;
    encoder.replace_extension();
    std::string stem = encoder.string();
    for (const char* suffix : {"-q5_0", "-q5_1", "-q8_0"}) {
        if (stem.ends_with(suffix)) stem.erase(stem.size() - std::string(suffix).size());
    }
    impl_->neural_engine = std::filesystem::exists(stem + "-encoder.mlmodelc");
#endif
    return true;
}

bool WhisperEngine::loaded() const {
    return impl_->context != nullptr;
}

const char* WhisperEngine::device() const {
    if (impl_->neural_engine) return "Neural Engine + GPU (Whisper)";
    return impl_->gpu ? "GPU (Metal, Whisper)" : "CPU (Whisper)";
}

void WhisperEngine::set_language(const std::string& language) {
    impl_->language = language.empty() ? "auto" : language;
}

SenseVoiceResult WhisperEngine::recognize(
    std::span<const float> pcm_16khz_mono,
    std::span<const HotwordBoostPhrase> hotwords) {
    SenseVoiceResult result;
    result.audio_ms = static_cast<int>(pcm_16khz_mono.size() / 16);
    // Whisper pads everything to 30 s; very short snippets mostly hallucinate.
    if (impl_->context == nullptr || pcm_16khz_mono.size() < 16'000 / 4) return result;

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.language = impl_->language.c_str();
    params.n_threads = impl_->threads;
    params.no_context = true;
    params.no_timestamps = true;
    params.single_segment = true;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_special = false;
    params.print_timestamps = false;
    params.suppress_nst = true;
    params.temperature_inc = 0.0F;
    // params.language pins the language token. A primer sentence in the
    // language was tried for Bulgarian and made Whisper Small worse, so the
    // prompt carries only the hotwords.
    std::string prompt;
    for (const HotwordBoostPhrase& hotword : hotwords) {
        if (!prompt.empty()) prompt += ", ";
        prompt += hotword.phrase;
    }
    if (!prompt.empty()) params.initial_prompt = prompt.c_str();
    result.hotword_bias_applied = !hotwords.empty();

    const auto started = std::chrono::steady_clock::now();
    if (whisper_full(impl_->context, params, pcm_16khz_mono.data(),
                     static_cast<int>(pcm_16khz_mono.size())) != 0) {
        throw std::runtime_error("Whisper inference failed");
    }
    std::string text;
    for (int segment = 0; segment < whisper_full_n_segments(impl_->context); ++segment) {
        text += whisper_full_get_segment_text(impl_->context, segment);
    }
    result.text = trim(std::move(text));
    result.inference_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started).count());
    return result;
}
