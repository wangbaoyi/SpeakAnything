#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Source and target language codes (see languages.h), e.g. {"zh", "en"}.
struct TranslationDirection {
    std::string source = "zh";
    std::string target = "en";

    static const TranslationDirection ZhToEn;
    static const TranslationDirection EnToZh;

    [[nodiscard]] TranslationDirection reversed() const { return {target, source}; }
    bool operator==(const TranslationDirection&) const = default;
};

inline const TranslationDirection TranslationDirection::ZhToEn{"zh", "en"};
inline const TranslationDirection TranslationDirection::EnToZh{"en", "zh"};

enum class TextLanguage {
    Unknown,
    Chinese,
    English,
};

// Counts CJK characters against Latin words; Unknown when neither appears.
TextLanguage detect_text_language(std::string_view text);
TextLanguage source_language(const TranslationDirection& direction);
// "zh-en"; parse accepts any pair of known language codes.
std::string direction_code(const TranslationDirection& direction);
std::optional<TranslationDirection> parse_direction_code(std::string_view code);

enum class SentenceTranslationState {
    Pending,
    Translated,
    Failed,
    LanguageMismatch,
};

enum class FallbackReason {
    None,
    LanguageMismatch,
    TranslationFailed,
    Timeout,
};

struct SessionInjection {
    std::string text;
    FallbackReason fallback = FallbackReason::None;
};

// Collects the Final results of one recording session and decides what gets
// injected on release: bilingual text, or the original alone when any sentence
// could not be translated.
class TranslationSession {
public:
    explicit TranslationSession(TranslationDirection direction);
    TranslationSession(const TranslationSession&) = default;
    TranslationSession& operator=(const TranslationSession&) = default;

    [[nodiscard]] const TranslationDirection& direction() const;

    // Returns the sentence index, or nullopt for blank text. A sentence whose
    // language contradicts the direction is settled as LanguageMismatch at once.
    std::optional<std::size_t> add_sentence(std::string original);
    void set_translation(std::size_t index, std::string translation);
    void set_failed(std::size_t index);

    [[nodiscard]] bool empty() const;
    [[nodiscard]] bool has_pending() const;
    [[nodiscard]] bool is_pending(std::size_t index) const;
    [[nodiscard]] std::string original_text() const;
    [[nodiscard]] std::string translated_text() const;
    [[nodiscard]] std::size_t size() const { return sentences_.size(); }
    [[nodiscard]] SentenceTranslationState state(std::size_t index) const { return sentences_[index].state; }
    [[nodiscard]] const std::string& translation(std::size_t index) const {
        return sentences_[index].translation;
    }
    // Sentences translated so far, original first, in order.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> translated_pairs() const;

    // timed_out: the caller stopped waiting; pending sentences count as Timeout.
    [[nodiscard]] SessionInjection compose(std::string_view separator, bool timed_out) const;

private:
    struct Sentence {
        std::string original;
        std::string translation;
        SentenceTranslationState state = SentenceTranslationState::Pending;
    };

    TranslationDirection direction_;
    std::vector<Sentence> sentences_;
};

std::string join_sentences(const std::vector<std::string>& sentences);
