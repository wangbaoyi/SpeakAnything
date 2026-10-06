#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

enum class TranslationDirection {
    ZhToEn,
    EnToZh,
};

enum class TextLanguage {
    Unknown,
    Chinese,
    English,
};

// Counts CJK characters against Latin words; Unknown when neither appears.
TextLanguage detect_text_language(std::string_view text);
TextLanguage source_language(TranslationDirection direction);
const char* direction_code(TranslationDirection direction);
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

    [[nodiscard]] TranslationDirection direction() const;

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
