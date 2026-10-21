// ctest runs the Release config; keep the assertions live.
#undef NDEBUG

#include "translator.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <tuple>

namespace {

class FakeTranslator final : public Translator {
public:
    explicit FakeTranslator(std::map<std::string, std::string> table) : table_(std::move(table)) {}

    std::optional<std::string> translate(std::string_view text, std::string& error) override {
        const auto found = table_.find(std::string(text));
        if (found == table_.end()) {
            error = "unknown sentence";
            return std::nullopt;
        }
        return found->second;
    }

private:
    std::map<std::string, std::string> table_;
};

void test_language_detection() {
    assert(detect_text_language("今天开会") == TextLanguage::Chinese);
    assert(detect_text_language("We have a meeting today") == TextLanguage::English);
    assert(detect_text_language("这个 bug 要 fix") == TextLanguage::Chinese);
    assert(detect_text_language("open the README file 吧") == TextLanguage::English);
    assert(detect_text_language("123，456。") == TextLanguage::Unknown);
    assert(parse_direction_code("zh-en") == TranslationDirection::ZhToEn);
    assert(parse_direction_code("en-zh") == TranslationDirection::EnToZh);
    assert(parse_direction_code("bg-de") == (TranslationDirection{"bg", "de"}));
    assert(!parse_direction_code("xx-en"));
    assert(!parse_direction_code("en-en"));
}

void test_bilingual_session() {
    TranslationSession session(TranslationDirection::ZhToEn);
    assert(session.empty());
    assert(!session.add_sentence("   "));
    const auto first = session.add_sentence("今天开会。");
    const auto second = session.add_sentence("明天放假。");
    assert(first && second);
    assert(session.has_pending());
    assert(session.is_pending(*first) && !session.is_pending(99));
    session.set_translation(*first, "We have a meeting today.");
    session.set_translation(*second, " Tomorrow is a holiday. ");
    assert(!session.has_pending());
    assert(session.original(*first) == "今天开会。");
    // add_sentence trims, so the original is not the raw input.
    assert(session.original(*second) == "明天放假。");

    const SessionInjection injection = session.compose(" / ", false);
    assert(injection.fallback == FallbackReason::None);
    assert(injection.text ==
        "今天开会。明天放假。 / We have a meeting today. Tomorrow is a holiday.");
    assert(injection.text.find('\n') == std::string::npos);

    assert(session.compose(" | ", false).text ==
        "今天开会。明天放假。 | We have a meeting today. Tomorrow is a holiday.");
}

void test_en_to_zh_joins_without_spaces() {
    TranslationSession session(TranslationDirection::EnToZh);
    const auto first = session.add_sentence("Hello.");
    const auto second = session.add_sentence("Good night.");
    session.set_translation(*first, "你好。");
    session.set_translation(*second, "晚安。");
    assert(session.compose(" / ", false).text == "Hello. Good night. / 你好。晚安。");
}

void test_language_mismatch_downgrades_whole_session() {
    TranslationSession session(TranslationDirection::ZhToEn);
    const auto first = session.add_sentence("今天开会。");
    const auto second = session.add_sentence("See you there.");
    session.set_translation(*first, "We have a meeting today.");
    // The mismatched sentence is settled at once and ignores late translations.
    session.set_translation(*second, "到时见。");
    assert(!session.has_pending());
    const SessionInjection injection = session.compose(" / ", false);
    assert(injection.fallback == FallbackReason::LanguageMismatch);
    assert(injection.text == "今天开会。See you there.");

    // Chinese spoken under en-zh: the original keeps Chinese spacing.
    TranslationSession reversed(TranslationDirection::EnToZh);
    reversed.add_sentence("今天天气很好。");
    reversed.add_sentence("我们下午3点开会。");
    const SessionInjection chinese = reversed.compose(" / ", false);
    assert(chinese.fallback == FallbackReason::LanguageMismatch);
    assert(chinese.text == "今天天气很好。我们下午3点开会。");
    assert(join_sentences({"Hello.", "3 apples", "你好。", "OK"}) == "Hello. 3 apples你好。OK");
}

void test_failure_and_timeout_downgrade_whole_session() {
    TranslationSession failed(TranslationDirection::ZhToEn);
    const auto ok = failed.add_sentence("第一句。");
    const auto bad = failed.add_sentence("第二句。");
    failed.set_translation(*ok, "First.");
    failed.set_failed(*bad);
    assert(failed.compose(" / ", false).fallback == FallbackReason::TranslationFailed);
    assert(failed.compose(" / ", false).text == "第一句。第二句。");

    TranslationSession empty_output(TranslationDirection::ZhToEn);
    const auto blank = empty_output.add_sentence("第一句。");
    empty_output.set_translation(*blank, "   ");
    assert(empty_output.compose(" / ", false).fallback == FallbackReason::TranslationFailed);

    TranslationSession slow(TranslationDirection::ZhToEn);
    const auto done = slow.add_sentence("第一句。");
    slow.add_sentence("第二句。");
    slow.set_translation(*done, "First.");
    const SessionInjection injection = slow.compose(" / ", true);
    assert(injection.fallback == FallbackReason::Timeout);
    assert(injection.text == "第一句。第二句。");
}

void test_worker_loads_one_direction_and_translates() {
    std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::pair<TranslationDirection, bool>> loads;
    std::map<std::uint64_t, std::optional<std::string>> results;
    int factory_calls = 0;

    TranslatorFactory factory = [&](TranslationDirection direction, std::string& error)
        -> std::unique_ptr<Translator> {
        ++factory_calls;
        if (direction == TranslationDirection::EnToZh) {
            error = "missing model";
            return nullptr;
        }
        return std::make_unique<FakeTranslator>(
            std::map<std::string, std::string>{{"你好", "Hello"}});
    };

    {
        TranslationWorker worker(
            factory,
            [&](TranslationDirection direction, bool ok, const std::string&,
                const TranslationModelInfo& info) {
                std::lock_guard lock(mutex);
                loads.emplace_back(direction, ok);
                if (ok) {
                    assert(!info.backend.empty());
                    assert(!info.device.empty());
                }
                condition.notify_all();
            },
            [&](std::uint64_t ticket, std::optional<std::string> translation, const std::string&) {
                std::lock_guard lock(mutex);
                results[ticket] = std::move(translation);
                condition.notify_all();
            });

        worker.translate(1, "你好");
        worker.load(TranslationDirection::ZhToEn);
        worker.load(TranslationDirection::ZhToEn);
        worker.translate(2, "你好");
        worker.translate(3, "再见");
        worker.load(TranslationDirection::EnToZh);
        worker.translate(4, "你好");
        worker.load(TranslationDirection::ZhToEn);
        worker.unload();
        worker.translate(5, "你好");

        std::unique_lock lock(mutex);
        const bool finished = condition.wait_for(lock, std::chrono::seconds(5), [&] {
            return results.size() == 5 && loads.size() == 4;
        });
        assert(finished);
        (void)finished;
    }

    assert(!results[1]);
    assert(results[2] == std::optional<std::string>("Hello"));
    assert(!results[3]);
    assert(!results[4]);
    assert(!results[5]);
    assert(loads[0] == std::make_pair(TranslationDirection::ZhToEn, true));
    assert(loads[1] == std::make_pair(TranslationDirection::ZhToEn, true));
    assert(loads[2] == std::make_pair(TranslationDirection::EnToZh, false));
    assert(loads[3] == std::make_pair(TranslationDirection::ZhToEn, true));
    // The repeated load of an already-resident direction does not reload.
    assert(factory_calls == 3);
}

GpuCapability test_gpu(bool available) {
    GpuCapability capability;
    capability.available = available;
    if (available) {
        capability.backend = "CUDA";
        capability.name = "CUDA0";
        capability.description = "Test GPU";
        capability.device_handle = reinterpret_cast<void*>(0x1);
    }
    return capability;
}

// The backend decision is pure, so no GPU or model file is needed here.
// Directions outside zh-en/en-zh must always use Qwen: Opus-MT has no model
// for them, and a GPU policy must never quietly pick the CPU.
void test_translation_backend_selection() {
    const std::filesystem::path qwen = "/models/translator/Qwen3-4B-Q4_K_M.gguf";
    const std::optional<std::filesystem::path> qwen_model = qwen;
    const std::optional<std::filesystem::path> no_model;
    const TranslationDirection zh_en = TranslationDirection::ZhToEn;
    const TranslationDirection en_zh = TranslationDirection::EnToZh;
    const TranslationDirection zh_de{"zh", "de"};
    const GpuCapability gpu = test_gpu(true);
    const GpuCapability no_gpu = test_gpu(false);

    const auto choose = [&](ComputePolicy policy, const GpuCapability& capability,
                            const std::optional<std::filesystem::path>& model,
                            bool opus, bool llm, TranslationDirection direction,
                            bool opus_cuda) {
        return choose_translation_backend(
            policy, capability, model, opus, llm, direction, opus_cuda);
    };

    // CPU: Opus-MT for zh-en/en-zh, Qwen for everything else.
    for (const bool available : {false, true}) {
        for (const TranslationDirection& direction : {zh_en, en_zh}) {
            const auto choice = choose(
                ComputePolicy::Cpu, test_gpu(available), qwen_model, true, true,
                direction, true);
            assert(choice.ok());
            assert(choice.backend == TranslationBackend::OpusMt);
            assert(choice.device == ComputeDevice::Cpu);
            assert(choice.model_path.empty());
        }
    }
    // A missing Qwen file never breaks the Opus CPU path.
    {
        const auto choice = choose(
            ComputePolicy::Cpu, gpu, no_model, true, true, zh_en, true);
        assert(choice.ok());
        assert(choice.backend == TranslationBackend::OpusMt);
        assert(choice.device == ComputeDevice::Cpu);
    }
    // Non-zh-en directions use Qwen even when Opus-MT is compiled.
    {
        const auto choice = choose(
            ComputePolicy::Cpu, gpu, qwen_model, true, true, zh_de, false);
        assert(choice.ok());
        assert(choice.backend == TranslationBackend::Llm);
        assert(choice.device == ComputeDevice::Cpu);
        assert(choice.model_path == qwen);

        const auto no_file = choose(
            ComputePolicy::Cpu, gpu, no_model, true, true, zh_de, false);
        assert(!no_file.ok());
        assert(no_file.error.find("Qwen") != std::string::npos);
    }
    // A macOS-style build without Opus-MT runs Qwen on the CPU and says CPU.
    {
        const auto choice = choose(
            ComputePolicy::Cpu, gpu, qwen_model, false, true, zh_en, false);
        assert(choice.ok());
        assert(choice.backend == TranslationBackend::Llm);
        assert(choice.device == ComputeDevice::Cpu);
        assert(choice.model_path == qwen);

        const auto none = choose(
            ComputePolicy::Cpu, gpu, qwen_model, false, false, zh_en, false);
        assert(!none.ok());
        assert(none.error.find("Opus-MT") != std::string::npos);
    }

    // GPU zh-en/en-zh: Opus-MT CUDA when compiled, Qwen otherwise.
    for (const TranslationDirection& direction : {zh_en, en_zh}) {
        const auto opus_cuda = choose(
            ComputePolicy::Gpu, gpu, no_model, true, true, direction, true);
        assert(opus_cuda.ok());
        assert(opus_cuda.backend == TranslationBackend::OpusMt);
        assert(opus_cuda.device == ComputeDevice::Gpu);
        assert(opus_cuda.model_path.empty());

        // Opus CUDA needs neither llama.cpp nor a Qwen file.
        const auto opus_only = choose(
            ComputePolicy::Gpu, gpu, no_model, true, false, direction, true);
        assert(opus_only.ok());
        assert(opus_only.backend == TranslationBackend::OpusMt);
        assert(opus_only.device == ComputeDevice::Gpu);

        const auto qwen_fallback = choose(
            ComputePolicy::Gpu, gpu, qwen_model, true, true, direction, false);
        assert(qwen_fallback.ok());
        assert(qwen_fallback.backend == TranslationBackend::Llm);
        assert(qwen_fallback.device == ComputeDevice::Gpu);
        assert(qwen_fallback.model_path == qwen);
    }
    {
        // No Opus CUDA and no Qwen: an explicit GPU request must fail, never
        // fall back to the CPU translator.
        const auto missing = choose(
            ComputePolicy::Gpu, gpu, no_model, true, true, zh_en, false);
        assert(!missing.ok());
        assert(missing.error.find("Opus-MT") != std::string::npos);

        const auto no_device = choose(
            ComputePolicy::Gpu, no_gpu, no_model, true, true, zh_en, true);
        assert(!no_device.ok());
        assert(no_device.error.find("GPU") != std::string::npos);
    }
    // Non-zh-en GPU always uses Qwen, even with Opus CUDA compiled.
    {
        const auto choice = choose(
            ComputePolicy::Gpu, gpu, qwen_model, true, true, zh_de, true);
        assert(choice.ok());
        assert(choice.backend == TranslationBackend::Llm);
        assert(choice.device == ComputeDevice::Gpu);
        assert(choice.model_path == qwen);

        const auto no_file = choose(
            ComputePolicy::Gpu, gpu, no_model, true, true, zh_de, true);
        assert(!no_file.ok());
        assert(no_file.error.find("Qwen") != std::string::npos);

        const auto no_support = choose(
            ComputePolicy::Gpu, gpu, qwen_model, true, false, zh_de, false);
        assert(!no_support.ok());
        assert(no_support.error.find("llama.cpp") != std::string::npos);
    }

    // Auto prefers the GPU Opus path, then GPU Qwen, then a CPU path.
    {
        const auto opus_gpu = choose(
            ComputePolicy::Auto, gpu, qwen_model, true, true, zh_en, true);
        assert(opus_gpu.ok());
        assert(opus_gpu.backend == TranslationBackend::OpusMt);
        assert(opus_gpu.device == ComputeDevice::Gpu);

        const auto qwen_gpu = choose(
            ComputePolicy::Auto, gpu, qwen_model, true, true, zh_en, false);
        assert(qwen_gpu.ok());
        assert(qwen_gpu.backend == TranslationBackend::Llm);
        assert(qwen_gpu.device == ComputeDevice::Gpu);

        const auto cpu_when_no_gpu = choose(
            ComputePolicy::Auto, no_gpu, qwen_model, true, true, zh_en, true);
        assert(cpu_when_no_gpu.ok());
        assert(cpu_when_no_gpu.backend == TranslationBackend::OpusMt);
        assert(cpu_when_no_gpu.device == ComputeDevice::Cpu);

        // No GPU Qwen path: Auto quietly falls back to Opus on the CPU.
        const auto cpu_fallback = choose(
            ComputePolicy::Auto, gpu, no_model, true, true, zh_en, false);
        assert(cpu_fallback.ok());
        assert(cpu_fallback.backend == TranslationBackend::OpusMt);
        assert(cpu_fallback.device == ComputeDevice::Cpu);

        const auto llm_cpu = choose(
            ComputePolicy::Auto, no_gpu, qwen_model, false, true, zh_en, false);
        assert(llm_cpu.ok());
        assert(llm_cpu.backend == TranslationBackend::Llm);
        assert(llm_cpu.device == ComputeDevice::Cpu);

        const auto none = choose(
            ComputePolicy::Auto, no_gpu, no_model, false, true, zh_en, false);
        assert(!none.ok());
    }
    // Auto non-zh-en: Qwen on the GPU when possible, otherwise Qwen on the CPU.
    {
        const auto gpu_choice = choose(
            ComputePolicy::Auto, gpu, qwen_model, true, true, zh_de, true);
        assert(gpu_choice.ok());
        assert(gpu_choice.backend == TranslationBackend::Llm);
        assert(gpu_choice.device == ComputeDevice::Gpu);

        const auto cpu_choice = choose(
            ComputePolicy::Auto, no_gpu, qwen_model, true, true, zh_de, true);
        assert(cpu_choice.ok());
        assert(cpu_choice.backend == TranslationBackend::Llm);
        assert(cpu_choice.device == ComputeDevice::Cpu);

        const auto no_file = choose(
            ComputePolicy::Auto, gpu, no_model, true, true, zh_de, true);
        assert(!no_file.ok());
        assert(no_file.error.find("Qwen") != std::string::npos);
    }
}

void write_probe_file(const std::filesystem::path& path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary);
    file << "x";
}

// Discovery must be case-insensitive and layout-independent, so the installed
// Qwen3-4B-Q4_K_M.gguf is found whether the catalog stored a lower-case path
// or the file sits directly under modelspt/translator.
void test_translation_model_discovery() {
    const std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("speakanything-translation-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(root);

    assert(!find_qwen_translation_model(root).has_value());

    const auto small = root / "translator" / "qwen3-1.7b-q4_k_m.gguf";
    write_probe_file(small);
    assert(find_qwen_translation_model(root) == small);

    // 4B wins over 1.7B, and matching is case-insensitive.
    const auto large = root / "translator" / "Qwen3-4B-Q4_K_M.gguf";
    write_probe_file(large);
    assert(find_qwen_translation_model(root) == large);

    // A lower-case catalog path still resolves to the mixed-case file.
    const auto preferred = root / "translator" / "qwen3-4b-q4_k_m.gguf";
    // Windows resolves the lower-case path straight to the existing file;
    // case-sensitive systems find it by the case-insensitive scan. Both must
    // end at the same file.
    const auto resolved = resolve_translation_model_file(preferred, root);
    assert(resolved.has_value());
    assert(std::filesystem::equivalent(*resolved, large));
    assert(resolve_translation_model_file(large, root) == large);

    // The flat layout under modelspt/ is searched too.
    const auto flat = root / "Qwen3-4B-Q4_K_M.gguf";
    std::filesystem::create_directories(root);
    write_probe_file(flat);
    std::filesystem::remove_all(root / "translator");
    assert(find_qwen_translation_model(root) == flat);

    // A selection that truly is missing stays missing: never substitute.
    assert(!resolve_translation_model_file(
        root / "translator" / "qwen3-8b-q4_k_m.gguf", root).has_value());

    std::filesystem::remove_all(root);
}

// Runs only when the converted models exist next to the source tree.
void smoke_test_real_model() {
    // The source tree may sit under a non-ASCII path; read the macro as UTF-8.
    const std::filesystem::path directory(u8"" SENSEVOICE_TEST_MODELS_DIR);
    if (!std::filesystem::exists(
            opus_mt_model_directory(directory, TranslationDirection::ZhToEn) / "model.bin")) {
        std::fprintf(stderr, "skipped real-model smoke test: run tools/convert_opus_mt.py\n");
        return;
    }
    std::string error;
    auto translator = make_opus_mt_factory(directory, 2)(TranslationDirection::ZhToEn, error);
    if (!translator) std::fprintf(stderr, "%s\n", error.c_str());
    assert(translator);
    const auto output = translator->translate("今天天气很好。", error);
    assert(output && !output->empty());
    assert(detect_text_language(*output) == TextLanguage::English);
}

}  // namespace

int main() {
    test_language_detection();
    test_bilingual_session();
    test_en_to_zh_joins_without_spaces();
    test_language_mismatch_downgrades_whole_session();
    test_failure_and_timeout_downgrade_whole_session();
    test_worker_loads_one_direction_and_translates();
    test_translation_backend_selection();
    test_translation_model_discovery();
    smoke_test_real_model();
    return 0;
}
