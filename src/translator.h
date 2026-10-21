#pragma once

#include "compute_policy.h"
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
    // What actually runs: "Opus-MT (CTranslate2)" or "Qwen3 GGUF (llama.cpp)".
    virtual std::string_view backend_name() const { return "unknown"; }
    // Where it runs, using the same labels as the speech engines:
    // "CPU" or "GPU (CUDA: NVIDIA GeForce GTX 1080 Ti)".
    virtual std::string device_label() const { return "CPU"; }
    virtual std::optional<std::string> translate(std::string_view text, std::string& error) = 0;
    virtual std::optional<std::string> translate(
        std::string_view text, const TranslationHints& hints, std::string& error) {
        (void)hints;
        return translate(text, error);
    }
};

using TranslatorFactory = std::function<std::unique_ptr<Translator>(
    TranslationDirection direction, std::string& error)>;

// Reported once a translation model finishes loading, for the CLI and for the
// tray/status display.
struct TranslationModelInfo {
    std::string backend;
    std::string device;
};

// What the runtime picked for one translation load.
enum class TranslationBackend {
    OpusMt,
    Llm,
};

struct TranslationBackendChoice {
    TranslationBackend backend = TranslationBackend::OpusMt;
    ComputeDevice device = ComputeDevice::Cpu;
    // LLM model file; empty for Opus-MT.
    std::filesystem::path model_path;
    // Empty on success.
    std::string error;

    [[nodiscard]] bool ok() const { return error.empty(); }
};

// Whether this build compiled Opus-MT / Qwen support.
inline constexpr bool translation_build_has_opus() {
#ifdef SENSEVOICE_WITH_TRANSLATION
    return true;
#else
    return false;
#endif
}
inline constexpr bool translation_build_has_llm() {
#ifdef SENSEVOICE_WITH_LLM_TRANSLATION
    return true;
#else
    return false;
#endif
}
// CTranslate2 itself can use CUDA for Opus-MT, independently of the ggml
// backend that runs speech recognition.
inline constexpr bool translation_build_has_opus_cuda() {
#ifdef SENSEVOICE_CT2_CUDA
    return true;
#else
    return false;
#endif
}

// Pure backend decision, so it can be tested without a GPU or a model file.
//   Cpu:  Opus-MT on the CPU for zh-en/en-zh; Qwen on the CPU for the other
//         directions (Opus-MT only ships those two pairs).
//   Gpu:  Opus-MT through CTranslate2 CUDA for zh-en/en-zh when that backend
//         was compiled; otherwise Qwen on the GPU. An error when the GPU, the
//         needed model, or the compiled backend is missing. Never CPU.
//   Auto: the best compiled path above, falling back to a CPU translator
//         without an error (falling back quietly is the point of Auto).
// `opus_cuda_available` lets tests cover both builds; callers pass
// translation_build_has_opus_cuda().
TranslationBackendChoice choose_translation_backend(
    ComputePolicy policy,
    const GpuCapability& gpu,
    const std::optional<std::filesystem::path>& qwen_model,
    bool opus_available,
    bool llm_available,
    TranslationDirection direction,
    bool opus_cuda_available);

// Finds a Qwen GGUF in `<directory>/translator` then `<directory>`, matching
// case-insensitively. Qwen3-4B-Q4_K_M wins over 1.7B, then any other Qwen GGUF.
std::optional<std::filesystem::path> find_qwen_translation_model(
    const std::filesystem::path& directory);

// Resolves a user-selected model file. An existing path wins; otherwise the
// same directories are searched for the file name, ignoring case.
std::optional<std::filesystem::path> resolve_translation_model_file(
    const std::filesystem::path& preferred,
    const std::filesystem::path& models_directory);

// Opus-MT through CTranslate2. models_directory holds opus-mt-zh-en-ct2/ and
// opus-mt-en-zh-ct2/. A GPU device selects CUDA + FLOAT32 and requires
// SENSEVOICE_CT2_CUDA; the CPU device keeps INT8. Fails with an error instead
// of silently using the CPU when a GPU was requested.
TranslatorFactory make_opus_mt_factory(
    std::filesystem::path models_directory,
    int threads,
    ComputeDevice device = ComputeDevice::Cpu,
    std::string device_label = {});
std::filesystem::path opus_mt_model_directory(
    const std::filesystem::path& models_directory, const TranslationDirection& direction);

// A small instruction-tuned LLM (GGUF) through llama.cpp. `device` decides
// whether every layer is offloaded; the label is reported verbatim afterwards.
TranslatorFactory make_llm_translation_factory(
    std::filesystem::path model_path,
    int threads,
    ComputeDevice device = ComputeDevice::Gpu,
    std::string device_label = {});
inline constexpr const char* llm_translation_model_file = "qwen3-1.7b-q4_k_m.gguf";

// The policy-driven factory shared by the CLI and the UI. `selected_llm_model`
// runs on the worker thread at load time, so it can read the user's current
// model choice. The GPU capability is sampled before the worker starts.
using TranslationModelPathProvider = std::function<std::filesystem::path()>;
TranslatorFactory make_translation_factory(
    std::filesystem::path models_directory,
    TranslationModelPathProvider selected_llm_model,
    int threads,
    ComputePolicy policy,
    GpuCapability gpu);

// Owns at most one loaded Translator and runs all model work on one background
// thread, so loading never blocks the caller. Callbacks run on that thread.
class TranslationWorker {
public:
    using LoadCallback = std::function<void(
        TranslationDirection, bool ok, const std::string& error, const TranslationModelInfo& info)>;
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
