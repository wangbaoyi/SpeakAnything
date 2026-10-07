#pragma once

#include "translation_session.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

// Extra guidance a translator may use; Opus-MT ignores it.
struct TranslationHints {
    // Terms to copy through untranslated (product names, people, "Claude Code").
    std::vector<std::string> keep_terms;
    // Earlier sentences of the same recording, original and translation, so a
    // sentence split by a pause is translated in context.
    std::vector<std::pair<std::string, std::string>> previous;
};

class Translator {
public:
    virtual ~Translator() = default;
    virtual std::optional<std::string> translate(std::string_view text, std::string& error) = 0;
    virtual std::optional<std::string> translate(
        std::string_view text, const TranslationHints& hints, std::string& error) {
        (void)hints;
        return translate(text, error);
    }
};

using TranslatorFactory = std::function<std::unique_ptr<Translator>(
    TranslationDirection direction, std::string& error)>;

// Opus-MT through CTranslate2. models_directory holds opus-mt-zh-en-ct2/ and
// opus-mt-en-zh-ct2/. Fails with an error when built without translation support.
TranslatorFactory make_opus_mt_factory(std::filesystem::path models_directory, int threads);
std::filesystem::path opus_mt_model_directory(
    const std::filesystem::path& models_directory, const TranslationDirection& direction);

// A small instruction-tuned LLM (GGUF) through llama.cpp; on macOS it runs on
// the GPU through Metal. One model file serves both directions.
TranslatorFactory make_llm_translation_factory(std::filesystem::path model_path, int threads);
inline constexpr const char* llm_translation_model_file = "qwen3-1.7b-q4_k_m.gguf";

// The backend this build ships: the LLM where it was compiled in (macOS),
// otherwise Opus-MT.
inline TranslatorFactory make_default_translation_factory(
    const std::filesystem::path& models_directory, int threads) {
#ifdef SENSEVOICE_WITH_LLM_TRANSLATION
    return make_llm_translation_factory(models_directory / llm_translation_model_file, threads);
#else
    return make_opus_mt_factory(models_directory, threads);
#endif
}

// Owns at most one loaded Translator and runs all model work on one background
// thread, so loading never blocks the caller. Callbacks run on that thread.
class TranslationWorker {
public:
    using LoadCallback = std::function<void(TranslationDirection, bool ok, const std::string& error)>;
    using ResultCallback = std::function<void(
        std::uint64_t ticket, std::optional<std::string> translation, const std::string& error)>;

    TranslationWorker(TranslatorFactory factory, LoadCallback on_loaded, ResultCallback on_result);
    ~TranslationWorker();

    TranslationWorker(const TranslationWorker&) = delete;
    TranslationWorker& operator=(const TranslationWorker&) = delete;

    // Replaces whatever model is loaded; a no-op when that direction is already loaded.
    void load(TranslationDirection direction);
    void unload();
    // Queued after any pending load. Without a loaded model the result is an error.
    void translate(std::uint64_t ticket, std::string text, TranslationHints hints = {});

private:
    struct Command {
        enum class Kind { Load, Unload, Translate } kind = Kind::Translate;
        TranslationDirection direction = TranslationDirection::ZhToEn;
        std::uint64_t ticket = 0;
        std::string text;
        TranslationHints hints;
    };

    void push(Command command);
    void run();

    TranslatorFactory factory_;
    LoadCallback on_loaded_;
    ResultCallback on_result_;

    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<Command> commands_;
    bool stopping_ = false;
    std::thread thread_;

    std::unique_ptr<Translator> translator_;
    std::optional<TranslationDirection> loaded_direction_;
};
