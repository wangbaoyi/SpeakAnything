// Translation with a small instruction-tuned LLM (Qwen3-1.7B GGUF) through
// llama.cpp. On macOS every layer is offloaded to the GPU through Metal; on
// other platforms it falls back to whatever ggml backends are compiled in.

#include "translator.h"

#include "languages.h"

#include "llama.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr int context_tokens = 2048;

void ensure_backend() {
    static std::once_flag once;
    std::call_once(once, [] {
        llama_log_set([](ggml_log_level, const char*, void*) {}, nullptr);
        llama_backend_init();
    });
}

std::string system_prompt(const TranslationDirection& direction, const TranslationHints& hints) {
    const std::string source(language_english_name(direction.source));
    std::string target(language_english_name(direction.target));
    if (direction.target == "zh") target = "Simplified Chinese";
    std::string prompt =
        "You are a live speech interpreter. Translate the user's " + source +
        " speech into natural, fluent spoken " + target +
        ". It is dictated speech, so keep its tone. Keep laughter and interjections "
        "(haha, hehe, 哈哈, wow, hmm) as the natural equivalent in " + target +
        " instead of dropping them. Keep names and numbers. Output only the translation, with "
        "no notes, quotes or explanations.";
    if (!hints.keep_terms.empty()) {
        prompt += " Never translate these terms; copy them exactly as written:";
        for (std::size_t index = 0; index < hints.keep_terms.size(); ++index) {
            prompt += (index == 0 ? " " : ", ") + hints.keep_terms[index];
        }
        prompt += ".";
    }
    return prompt;
}

struct ModelDeleter {
    void operator()(llama_model* model) const { llama_model_free(model); }
};
struct ContextDeleter {
    void operator()(llama_context* context) const { llama_free(context); }
};
struct SamplerDeleter {
    void operator()(llama_sampler* sampler) const { llama_sampler_free(sampler); }
};

class LlmTranslator final : public Translator {
public:
    LlmTranslator(std::unique_ptr<llama_model, ModelDeleter> model,
                  std::unique_ptr<llama_context, ContextDeleter> context,
                  TranslationDirection direction)
        : model_(std::move(model)),
          context_(std::move(context)),
          sampler_(llama_sampler_init_greedy()),
          vocab_(llama_model_get_vocab(model_.get())),
          direction_(direction) {}

    std::optional<std::string> translate(std::string_view text, std::string& error) override {
        return translate(text, TranslationHints{}, error);
    }

    std::optional<std::string> translate(
        std::string_view text, const TranslationHints& hints, std::string& error) override {
        // Qwen3 chat format with the thinking block closed up front, so the
        // model answers directly instead of reasoning first. Earlier sentences
        // of the recording go in as prior turns, so a sentence cut by a pause
        // continues the same translation.
        std::string prompt = "<|im_start|>system\n" + system_prompt(direction_, hints) + "<|im_end|>\n";
        const std::size_t first_previous = hints.previous.size() > 3 ? hints.previous.size() - 3 : 0;
        for (std::size_t index = first_previous; index < hints.previous.size(); ++index) {
            prompt += "<|im_start|>user\n" + hints.previous[index].first +
                "<|im_end|>\n<|im_start|>assistant\n" + hints.previous[index].second + "<|im_end|>\n";
        }
        prompt += "<|im_start|>user\n" + std::string(text) +
            "<|im_end|>\n<|im_start|>assistant\n<think>\n\n</think>\n\n";

        std::vector<llama_token> tokens(prompt.size() + 16);
        const int count = llama_tokenize(vocab_, prompt.data(), static_cast<int32_t>(prompt.size()),
                                         tokens.data(), static_cast<int32_t>(tokens.size()),
                                         true, true);
        if (count < 0 || count >= context_tokens - 16) {
            error = "text is too long to translate";
            return std::nullopt;
        }
        tokens.resize(static_cast<std::size_t>(count));

        llama_memory_clear(llama_get_memory(context_.get()), true);
        llama_sampler_reset(sampler_.get());
        if (llama_decode(context_.get(), llama_batch_get_one(tokens.data(), count)) != 0) {
            error = "translation model failed to read the input";
            return std::nullopt;
        }

        // Translations rarely run longer than three tokens per source byte.
        const int budget = std::min<int>(context_tokens - count - 1,
                                         static_cast<int>(text.size()) * 3 + 32);
        std::string output;
        for (int step = 0; step < budget; ++step) {
            llama_token token = llama_sampler_sample(sampler_.get(), context_.get(), -1);
            if (llama_vocab_is_eog(vocab_, token)) break;
            char piece[256];
            const int length = llama_token_to_piece(vocab_, token, piece, sizeof(piece), 0, false);
            if (length > 0) output.append(piece, static_cast<std::size_t>(length));
            if (llama_decode(context_.get(), llama_batch_get_one(&token, 1)) != 0) {
                error = "translation model failed while generating";
                return std::nullopt;
            }
        }

        const auto first = output.find_first_not_of(" \n\r\t");
        if (first == std::string::npos) {
            error = "translation model returned nothing";
            return std::nullopt;
        }
        const auto last = output.find_last_not_of(" \n\r\t");
        return output.substr(first, last - first + 1);
    }

private:
    std::unique_ptr<llama_model, ModelDeleter> model_;
    std::unique_ptr<llama_context, ContextDeleter> context_;
    std::unique_ptr<llama_sampler, SamplerDeleter> sampler_;
    const llama_vocab* vocab_;
    TranslationDirection direction_;
};

} // namespace

TranslatorFactory make_llm_translation_factory(std::filesystem::path model_path, int threads) {
    return [model_path = std::move(model_path), threads](
               TranslationDirection direction, std::string& error) -> std::unique_ptr<Translator> {
        if (!std::filesystem::is_regular_file(model_path)) {
            error = "missing translation model " + model_path.string();
            return nullptr;
        }
        ensure_backend();
        llama_model_params model_params = llama_model_default_params();
        model_params.n_gpu_layers = -1;
        std::unique_ptr<llama_model, ModelDeleter> model(
            llama_model_load_from_file(model_path.string().c_str(), model_params));
        if (!model) {
            error = "failed to load translation model";
            return nullptr;
        }
        llama_context_params context_params = llama_context_default_params();
        context_params.n_ctx = context_tokens;
        context_params.n_batch = context_tokens;
        context_params.n_threads = threads;
        context_params.n_threads_batch = threads;
        context_params.no_perf = true;
        std::unique_ptr<llama_context, ContextDeleter> context(
            llama_init_from_model(model.get(), context_params));
        if (!context) {
            error = "failed to create translation context";
            return nullptr;
        }
        return std::make_unique<LlmTranslator>(std::move(model), std::move(context), direction);
    };
}
