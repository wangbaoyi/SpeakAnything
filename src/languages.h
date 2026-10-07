#pragma once

#include <span>
#include <string_view>

// Writing systems, used to tell whether a recognized sentence is in the
// language the user said they would speak.
enum class Script {
    Han,       // Chinese (and kanji)
    Japanese,  // kana, possibly mixed with kanji
    Hangul,
    Latin,
    Cyrillic,
    Greek,
    Arabic,
    Devanagari,
    Other,
};

struct LanguageInfo {
    std::string_view code;         // ISO 639-1, as used by Whisper and in settings
    std::string_view english_name; // also used in translation prompts
    std::string_view native_name;
    Script script;
    bool sensevoice;               // SenseVoiceSmall recognizes it
};

std::span<const LanguageInfo> supported_languages();
const LanguageInfo* find_language(std::string_view code);
std::string_view language_english_name(std::string_view code);

// Dominant script of the letters in text; Other when there are none.
Script detect_script(std::string_view text);
// False only when text is clearly written in a script the language never uses.
bool text_matches_language(std::string_view text, std::string_view language_code);
