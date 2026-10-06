// ctest runs the Release config; keep the assertions live.
#undef NDEBUG

#include "translator.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>

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
    assert(!parse_direction_code("fr-en"));
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
            [&](TranslationDirection direction, bool ok, const std::string&) {
                std::lock_guard lock(mutex);
                loads.emplace_back(direction, ok);
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
    smoke_test_real_model();
    return 0;
}
