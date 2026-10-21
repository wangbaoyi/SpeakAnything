// Runtime choice of translation backend, shared by the CLI and the UI.
//
// The policy is the same ComputePolicy that drives speech recognition:
//   Cpu  -> Opus-MT through CTranslate2 on the CPU.
//   Gpu  -> Qwen GGUF through llama.cpp with every layer offloaded; loading
//           fails instead of silently running on the CPU.
//   Auto -> Qwen on the GPU when both the GPU and a Qwen file exist, Opus-MT
//           on the CPU otherwise.
//
// This file is deliberately free of ggml and llama calls so the selection and
// the file discovery can be tested on a machine without a GPU.

#include "translator.h"

#include <algorithm>
#include <cctype>
#include <system_error>
#include <vector>

namespace {

std::string lower_ascii(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

std::string utf8_path(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

std::vector<std::filesystem::path> search_directories(
    const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> result;
    result.push_back(directory / "translator");
    result.push_back(directory);
    return result;
}

struct Candidate {
    std::filesystem::path path;
    int rank = 99;
    int root = 99;
};

// 0: exact Qwen3-4B Q4_K_M, 1: any Qwen3-4B GGUF, 2: exact 1.7B, 3: any
// Qwen GGUF. Other files are rejected.
std::optional<int> qwen_rank(const std::string& filename) {
    const std::string lower = lower_ascii(filename);
    if (lower.size() < 5 || lower.substr(lower.size() - 5) != ".gguf") {
        return std::nullopt;
    }
    if (lower == "qwen3-4b-q4_k_m.gguf") return 0;
    if (lower.rfind("qwen3-4b", 0) == 0) return 1;
    if (lower == "qwen3-1.7b-q4_k_m.gguf") return 2;
    if (lower.find("qwen") != std::string::npos) return 3;
    return std::nullopt;
}

// Opus-MT ships exactly two pairs; every other direction needs the LLM.
bool is_zh_en(const TranslationDirection& direction) {
    return (direction.source == "zh" && direction.target == "en") ||
           (direction.source == "en" && direction.target == "zh");
}

} // namespace

std::optional<std::filesystem::path> find_qwen_translation_model(
    const std::filesystem::path& directory) {
    std::vector<Candidate> candidates;
    int root_index = 0;
    for (const std::filesystem::path& root : search_directories(directory)) {
        std::error_code error;
        std::filesystem::directory_iterator iterator(root, error);
        if (error) {
            ++root_index;
            continue;
        }
        for (const std::filesystem::directory_entry& entry : iterator) {
            std::error_code status_error;
            if (!entry.is_regular_file(status_error) || status_error) continue;
            const std::string filename = utf8_path(entry.path().filename());
            const std::optional<int> rank = qwen_rank(filename);
            if (!rank.has_value()) continue;
            candidates.push_back({entry.path(), *rank, root_index});
        }
        ++root_index;
    }
    if (candidates.empty()) return std::nullopt;
    std::stable_sort(candidates.begin(), candidates.end(),
        [](const Candidate& left, const Candidate& right) {
            if (left.rank != right.rank) return left.rank < right.rank;
            return left.root < right.root;
        });
    return candidates.front().path;
}

std::optional<std::filesystem::path> resolve_translation_model_file(
    const std::filesystem::path& preferred,
    const std::filesystem::path& models_directory) {
    if (preferred.empty()) return std::nullopt;
    std::error_code error;
    if (std::filesystem::is_regular_file(preferred, error) && !error) {
        return preferred;
    }
    const std::string wanted = lower_ascii(utf8_path(preferred.filename()));
    if (wanted.empty()) return std::nullopt;
    for (const std::filesystem::path& root : search_directories(models_directory)) {
        std::filesystem::directory_iterator iterator(root, error);
        if (error) continue;
        for (const std::filesystem::directory_entry& entry : iterator) {
            std::error_code status_error;
            if (!entry.is_regular_file(status_error) || status_error) continue;
            if (lower_ascii(utf8_path(entry.path().filename())) == wanted) {
                return entry.path();
            }
        }
    }
    return std::nullopt;
}

TranslationBackendChoice choose_translation_backend(
    ComputePolicy policy,
    const GpuCapability& gpu,
    const std::optional<std::filesystem::path>& qwen_model,
    bool opus_available,
    bool llm_available,
    TranslationDirection direction,
    bool opus_cuda_available) {
    TranslationBackendChoice choice;
    const bool zh_en = is_zh_en(direction);
    const bool qwen_ready = llm_available && qwen_model.has_value();
    const bool gpu_opus_ready = gpu.available && opus_available && opus_cuda_available;
    const bool gpu_qwen_ready = gpu.available && qwen_ready;

    const auto choose_opus = [&choice](ComputeDevice device) {
        choice.backend = TranslationBackend::OpusMt;
        choice.device = device;
    };
    const auto choose_llm = [&choice, &qwen_model](ComputeDevice device) {
        choice.backend = TranslationBackend::Llm;
        choice.device = device;
        choice.model_path = *qwen_model;
    };
    // The CPU policy and Auto's fallback share these two paths.
    const auto choose_cpu = [&]() -> bool {
        if (zh_en && opus_available) {
            choose_opus(ComputeDevice::Cpu);
            return true;
        }
        // macOS builds ship the LLM without CTranslate2 (the original platform
        // default), and non-zh-en directions only exist in the LLM.
        if (qwen_ready) {
            choose_llm(ComputeDevice::Cpu);
            return true;
        }
        choice.device = ComputeDevice::Cpu;
        choice.backend = zh_en ? TranslationBackend::OpusMt : TranslationBackend::Llm;
        if (zh_en) {
            choice.error = llm_available
                ? "the CPU translation policy needs Opus-MT or a Qwen GGUF model, but this "
                  "build has no CTranslate2 support and no Qwen model file was found"
                : "the CPU translation policy needs Opus-MT, but this build was compiled "
                  "without CTranslate2 translation support";
        } else {
            choice.error = llm_available
                ? "translation for " + direction_code(direction) +
                      " needs a Qwen GGUF model, but none was found under "
                      "modelspt/translator or models/translator"
                : "translation for " + direction_code(direction) +
                      " needs Qwen support through llama.cpp, but this build was compiled "
                      "without SENSEVOICE_WITH_LLM_TRANSLATION";
        }
        return false;
    };

    if (policy == ComputePolicy::Cpu) {
        choose_cpu();
        return choice;
    }

    if (policy == ComputePolicy::Gpu) {
        if (!gpu.available) {
            choice.backend = TranslationBackend::Llm;
            choice.device = ComputeDevice::Cpu;
            choice.error = "GPU translation was requested, but no GPU backend is available. ";
            choice.error += compute_build_has_cuda()
                ? "CUDA is compiled in, but no usable device initialized (check the driver)."
                : "This build was compiled without CUDA.";
            return choice;
        }
        // zh-en/en-zh use the much faster Opus-MT CUDA path when this build
        // has it. If it does not, Qwen remains the GPU fallback.
        if (zh_en && opus_available && opus_cuda_available) {
            choose_opus(ComputeDevice::Gpu);
            return choice;
        }
        if (!llm_available) {
            choice.backend = TranslationBackend::Llm;
            choice.device = ComputeDevice::Cpu;
            choice.error =
                "GPU translation needs Qwen support through llama.cpp, but this build was "
                "compiled without SENSEVOICE_WITH_LLM_TRANSLATION.";
            return choice;
        }
        if (!qwen_model.has_value()) {
            choice.backend = TranslationBackend::Llm;
            choice.device = ComputeDevice::Cpu;
            choice.error = zh_en
                ? "GPU translation for zh-en needs Opus-MT with CTranslate2 CUDA "
                  "(-DSENSEVOICE_CT2_CUDA=ON) or a Qwen GGUF model, but neither is available."
                : "GPU translation needs a Qwen GGUF model, but none was found under "
                  "modelspt/translator or models/translator (expected Qwen3-4B-Q4_K_M.gguf).";
            return choice;
        }
        choose_llm(ComputeDevice::Gpu);
        return choice;
    }

    // Auto: the fastest compiled path, then a quiet CPU fallback.
    if (zh_en && gpu_opus_ready) {
        choose_opus(ComputeDevice::Gpu);
        return choice;
    }
    if (gpu_qwen_ready) {
        choose_llm(ComputeDevice::Gpu);
        return choice;
    }
    choose_cpu();
    return choice;
}

TranslatorFactory make_translation_factory(
    std::filesystem::path models_directory,
    TranslationModelPathProvider selected_llm_model,
    int threads,
    ComputePolicy policy,
    GpuCapability gpu) {
    return [models_directory = std::move(models_directory),
            selected_llm_model = std::move(selected_llm_model),
            threads, policy, gpu](TranslationDirection direction, std::string& error)
               -> std::unique_ptr<Translator> {
        const bool opus_available = translation_build_has_opus();
        const bool llm_available = translation_build_has_llm();
        const bool opus_cuda_available = translation_build_has_opus_cuda();
        const bool zh_en = is_zh_en(direction);
        // Opus-MT covers zh-en/en-zh only; the other directions always need
        // Qwen, and zh-en on the GPU needs Qwen only when the build has no
        // CTranslate2 CUDA path.
        const bool opus_path_available = zh_en && opus_available;
        const bool opus_gpu_path =
            opus_path_available && opus_cuda_available && gpu.available &&
            (policy == ComputePolicy::Gpu || policy == ComputePolicy::Auto);
        const bool need_qwen = !(opus_path_available &&
            (policy == ComputePolicy::Cpu || opus_gpu_path));

        // Read the user's current selection on the worker thread. A path that
        // cannot pick Qwen skips discovery entirely, so it never fails on a
        // stale selection.
        std::optional<std::filesystem::path> qwen_model;
        const std::filesystem::path selected =
            selected_llm_model ? selected_llm_model() : std::filesystem::path();
        bool selected_missing = false;
        if (need_qwen) {
            if (!selected.empty()) {
                qwen_model = resolve_translation_model_file(selected, models_directory);
                selected_missing = !qwen_model.has_value();
            } else {
                qwen_model = find_qwen_translation_model(models_directory);
            }
        }

        const TranslationBackendChoice choice = choose_translation_backend(
            policy, gpu, qwen_model, opus_available, llm_available, direction,
            opus_cuda_available);
        if (!choice.ok()) {
            // Make a stale user selection explicit instead of blaming the
            // GPU or the build, but only when the chosen path needed Qwen.
            const bool llm_path_wanted =
                need_qwen && (policy != ComputePolicy::Gpu || gpu.available);
            error = selected_missing && llm_path_wanted
                ? "selected translation model not found: " + utf8_path(selected)
                : choice.error;
            return nullptr;
        }
        if (choice.backend == TranslationBackend::Llm) {
#ifdef SENSEVOICE_WITH_LLM_TRANSLATION
            return make_llm_translation_factory(
                choice.model_path, threads, choice.device,
                compute_device_label(choice.device, gpu))(direction, error);
#else
            error = "this build was compiled without Qwen translation support";
            return nullptr;
#endif
        }
        return make_opus_mt_factory(
            models_directory, threads, choice.device,
            compute_device_label(choice.device, gpu))(direction, error);
    };
}
