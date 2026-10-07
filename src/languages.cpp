#include "languages.h"

#include <array>

namespace {

constexpr std::array<LanguageInfo, 28> languages = {{
    {"zh", "Chinese", "中文", Script::Han, true},
    {"en", "English", "English", Script::Latin, true},
    {"yue", "Cantonese", "粵語", Script::Han, true},
    {"ja", "Japanese", "日本語", Script::Japanese, true},
    {"ko", "Korean", "한국어", Script::Hangul, true},
    {"es", "Spanish", "Español", Script::Latin, false},
    {"de", "German", "Deutsch", Script::Latin, false},
    {"fr", "French", "Français", Script::Latin, false},
    {"it", "Italian", "Italiano", Script::Latin, false},
    {"pt", "Portuguese", "Português", Script::Latin, false},
    {"nl", "Dutch", "Nederlands", Script::Latin, false},
    {"pl", "Polish", "Polski", Script::Latin, false},
    {"cs", "Czech", "Čeština", Script::Latin, false},
    {"ro", "Romanian", "Română", Script::Latin, false},
    {"hu", "Hungarian", "Magyar", Script::Latin, false},
    {"sv", "Swedish", "Svenska", Script::Latin, false},
    {"fi", "Finnish", "Suomi", Script::Latin, false},
    {"tr", "Turkish", "Türkçe", Script::Latin, false},
    {"vi", "Vietnamese", "Tiếng Việt", Script::Latin, false},
    {"id", "Indonesian", "Bahasa Indonesia", Script::Latin, false},
    {"bg", "Bulgarian", "Български", Script::Cyrillic, false},
    {"ru", "Russian", "Русский", Script::Cyrillic, false},
    {"uk", "Ukrainian", "Українська", Script::Cyrillic, false},
    {"sr", "Serbian", "Српски", Script::Cyrillic, false},
    {"el", "Greek", "Ελληνικά", Script::Greek, false},
    {"ar", "Arabic", "العربية", Script::Arabic, false},
    {"hi", "Hindi", "हिन्दी", Script::Devanagari, false},
    {"th", "Thai", "ไทย", Script::Other, false},
}};

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

Script script_of(char32_t c) {
    if ((c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || (c >= 0xC0 && c <= 0x24F) ||
        (c >= 0x1E00 && c <= 0x1EFF)) {
        return Script::Latin;
    }
    if (c >= 0x0400 && c <= 0x04FF) return Script::Cyrillic;
    if (c >= 0x0370 && c <= 0x03FF) return Script::Greek;
    if (c >= 0x0600 && c <= 0x06FF) return Script::Arabic;
    if (c >= 0x0900 && c <= 0x097F) return Script::Devanagari;
    if (c >= 0xAC00 && c <= 0xD7AF) return Script::Hangul;
    if (c >= 0x3040 && c <= 0x30FF) return Script::Japanese;
    if ((c >= 0x4E00 && c <= 0x9FFF) || (c >= 0x3400 && c <= 0x4DBF) ||
        (c >= 0xF900 && c <= 0xFAFF) || (c >= 0x20000 && c <= 0x2FA1F)) {
        return Script::Han;
    }
    return Script::Other;
}

} // namespace

std::span<const LanguageInfo> supported_languages() {
    return languages;
}

const LanguageInfo* find_language(std::string_view code) {
    for (const LanguageInfo& language : languages) {
        if (language.code == code) return &language;
    }
    return nullptr;
}

std::string_view language_english_name(std::string_view code) {
    const LanguageInfo* language = find_language(code);
    return language == nullptr ? code : language->english_name;
}

Script detect_script(std::string_view text) {
    std::array<int, 9> counts{};
    std::size_t offset = 0;
    bool in_latin_word = false;
    while (offset < text.size()) {
        const Script script = script_of(next_code_point(text, offset));
        // Latin counts per word, the others per character, so a single
        // English brand name doesn't outvote a Chinese sentence.
        if (script == Script::Latin) {
            if (!in_latin_word) ++counts[static_cast<int>(script)];
            in_latin_word = true;
            continue;
        }
        in_latin_word = false;
        if (script != Script::Other) ++counts[static_cast<int>(script)];
    }
    // Kana marks Japanese even when kanji outnumber it.
    if (counts[static_cast<int>(Script::Japanese)] > 0) {
        counts[static_cast<int>(Script::Japanese)] += counts[static_cast<int>(Script::Han)];
        counts[static_cast<int>(Script::Han)] = 0;
    }
    int best = static_cast<int>(Script::Other);
    int best_count = 0;
    for (int index = 0; index < static_cast<int>(counts.size()); ++index) {
        if (counts[index] > best_count) {
            best = index;
            best_count = counts[index];
        }
    }
    return static_cast<Script>(best);
}

bool text_matches_language(std::string_view text, std::string_view language_code) {
    const LanguageInfo* language = find_language(language_code);
    const Script script = detect_script(text);
    if (language == nullptr || script == Script::Other || language->script == Script::Other) {
        return true;
    }
    if (language->script == Script::Japanese) {
        return script == Script::Japanese || script == Script::Han;
    }
    return script == language->script;
}
