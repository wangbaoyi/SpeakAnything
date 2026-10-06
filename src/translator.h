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

class Translator {
public:
    virtual ~Translator() = default;
    virtual std::optional<std::string> translate(std::string_view text, std::string& error) = 0;
};

using TranslatorFactory = std::function<std::unique_ptr<Translator>(
    TranslationDirection direction, std::string& error)>;

// Opus-MT through CTranslate2. models_directory holds opus-mt-zh-en-ct2/ and
// opus-mt-en-zh-ct2/. Fails with an error when built without translation support.
TranslatorFactory make_opus_mt_factory(std::filesystem::path models_directory, int threads);
std::filesystem::path opus_mt_model_directory(
    const std::filesystem::path& models_directory, TranslationDirection direction);

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
    void translate(std::uint64_t ticket, std::string text);

private:
    struct Command {
        enum class Kind { Load, Unload, Translate } kind = Kind::Translate;
        TranslationDirection direction = TranslationDirection::ZhToEn;
        std::uint64_t ticket = 0;
        std::string text;
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
