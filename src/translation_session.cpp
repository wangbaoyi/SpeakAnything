#include "translation_session.h"

#include "languages.h"

#include <algorithm>

namespace {

bool is_cjk(char32_t code_point) {
    return (code_point >= 0x4E00 && code_point <= 0x9FFF) ||
        (code_point >= 0x3400 && code_point <= 0x4DBF) ||
        (code_point >= 0xF900 && code_point <= 0xFAFF) ||
        (code_point >= 0x20000 && code_point <= 0x2FA1F);
}

bool is_latin_letter(char32_t code_point) {
    return (code_point >= U'a' && code_point <= U'z') ||
        (code_point >= U'A' && code_point <= U'Z');
}

// Decodes one UTF-8 code point; malformed bytes decode as themselves.
char32_t next_code_point(std::string_view text, std::size_t& offset) {
    const auto lead = static_cast<unsigned char>(text[offset]);
    std::size_t length = 1;
    char32_t value = lead;
    if (lead >= 0xF0) {
        length = 4;
        value = lead & 0x07U;
    } else if (lead >= 0xE0) {
        length = 3;
        value = lead & 0x0FU;
    } else if (lead >= 0xC0) {
        length = 2;
        value = lead & 0x1FU;
    }
    if (offset + length > text.size()) {
        ++offset;
        return lead;
    }
    for (std::size_t i = 1; i < length; ++i) {
        value = (value << 6U) | (static_cast<unsigned char>(text[offset + i]) & 0x3FU);
    }
    offset += length;
    return value;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

}  // namespace

TextLanguage detect_text_language(std::string_view text) {
    std::size_t cjk = 0;
    std::size_t latin_words = 0;
    bool in_word = false;
    std::size_t offset = 0;
    while (offset < text.size()) {
        const char32_t code_point = next_code_point(text, offset);
        if (is_cjk(code_point)) ++cjk;
        const bool letter = is_latin_letter(code_point);
        if (letter && !in_word) ++latin_words;
        in_word = letter;
    }
    if (cjk == 0 && latin_words == 0) return TextLanguage::Unknown;
    return cjk >= latin_words ? TextLanguage::Chinese : TextLanguage::English;
}

TextLanguage source_language(const TranslationDirection& direction) {
    if (direction.source == "zh" || direction.source == "yue") return TextLanguage::Chinese;
    if (direction.source == "en") return TextLanguage::English;
    return TextLanguage::Unknown;
}

std::string direction_code(const TranslationDirection& direction) {
    return direction.source + "-" + direction.target;
}

std::optional<TranslationDirection> parse_direction_code(std::string_view code) {
    const std::size_t dash = code.find('-');
    if (dash == std::string_view::npos) return std::nullopt;
    const std::string_view source = code.substr(0, dash);
    const std::string_view target = code.substr(dash + 1);
    if (find_language(source) == nullptr || find_language(target) == nullptr || source == target) {
        return std::nullopt;
    }
    return TranslationDirection{std::string(source), std::string(target)};
}

std::string join_sentences(const std::vector<std::string>& sentences) {
    std::string joined;
    for (const auto& sentence : sentences) {
        if (sentence.empty()) continue;
        // Space only between two Latin-script edges; CJK text runs together.
        const auto is_ascii = [](char character) {
            return static_cast<unsigned char>(character) < 0x80;
        };
        if (!joined.empty() && is_ascii(joined.back()) && is_ascii(sentence.front())) {
            joined += ' ';
        }
        joined += sentence;
    }
    return joined;
}

TranslationSession::TranslationSession(TranslationDirection direction)
    : direction_(direction) {}

const TranslationDirection& TranslationSession::direction() const {
    return direction_;
}

std::optional<std::size_t> TranslationSession::add_sentence(std::string original) {
    original = trim(original);
    if (original.empty()) return std::nullopt;
    Sentence sentence;
    if (!text_matches_language(original, direction_.source)) {
        sentence.state = SentenceTranslationState::LanguageMismatch;
    }
    sentence.original = std::move(original);
    sentences_.push_back(std::move(sentence));
    return sentences_.size() - 1;
}

void TranslationSession::set_translation(std::size_t index, std::string translation) {
    if (index >= sentences_.size()) return;
    Sentence& sentence = sentences_[index];
    if (sentence.state != SentenceTranslationState::Pending) return;
    translation = trim(translation);
    if (translation.empty()) {
        sentence.state = SentenceTranslationState::Failed;
        return;
    }
    sentence.translation = std::move(translation);
    sentence.state = SentenceTranslationState::Translated;
}

void TranslationSession::set_failed(std::size_t index) {
    if (index >= sentences_.size()) return;
    if (sentences_[index].state == SentenceTranslationState::Pending) {
        sentences_[index].state = SentenceTranslationState::Failed;
    }
}

bool TranslationSession::empty() const {
    return sentences_.empty();
}

bool TranslationSession::has_pending() const {
    return std::any_of(sentences_.begin(), sentences_.end(), [](const Sentence& sentence) {
        return sentence.state == SentenceTranslationState::Pending;
    });
}

bool TranslationSession::is_pending(std::size_t index) const {
    return index < sentences_.size() &&
        sentences_[index].state == SentenceTranslationState::Pending;
}

std::string TranslationSession::original_text() const {
    std::vector<std::string> parts;
    for (const auto& sentence : sentences_) parts.push_back(sentence.original);
    return join_sentences(parts);
}

std::vector<std::pair<std::string, std::string>> TranslationSession::translated_pairs() const {
    std::vector<std::pair<std::string, std::string>> pairs;
    for (const Sentence& sentence : sentences_) {
        if (sentence.state == SentenceTranslationState::Translated) {
            pairs.emplace_back(sentence.original, sentence.translation);
        }
    }
    return pairs;
}

std::string TranslationSession::translated_text() const {
    std::vector<std::string> parts;
    for (const auto& sentence : sentences_) parts.push_back(sentence.translation);
    return join_sentences(parts);
}

SessionInjection TranslationSession::compose(std::string_view separator, bool timed_out) const {
    SessionInjection injection;
    injection.text = original_text();
    if (sentences_.empty()) return injection;

    // Mismatch outranks failure outranks timeout: the most actionable reason wins.
    bool mismatch = false;
    bool failed = false;
    bool pending = false;
    for (const auto& sentence : sentences_) {
        mismatch |= sentence.state == SentenceTranslationState::LanguageMismatch;
        failed |= sentence.state == SentenceTranslationState::Failed;
        pending |= sentence.state == SentenceTranslationState::Pending;
    }
    if (mismatch) {
        injection.fallback = FallbackReason::LanguageMismatch;
    } else if (failed) {
        injection.fallback = FallbackReason::TranslationFailed;
    } else if (pending) {
        injection.fallback = timed_out ? FallbackReason::Timeout : FallbackReason::TranslationFailed;
    } else {
        injection.text += separator;
        injection.text += translated_text();
    }
    return injection;
}
