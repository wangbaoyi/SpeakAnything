#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Every model the app can use: what it does, which languages it covers, where
// it lives under a models/ directory and how to fetch it when it isn't bundled.

enum class ModelKind {
    Recognizer,
    Translator,
    Voice,
};

enum class ModelEngine {
    SenseVoice,       // GGUF, CPU or GPU (Metal)
    SenseVoiceCoreML, // Neural Engine, macOS 15+
    Whisper,          // whisper.cpp; GPU, Neural Engine encoder when present
    Llm,              // GGUF chat model through llama.cpp
    Kokoro,           // sherpa-onnx Kokoro
    Piper,            // sherpa-onnx VITS (Piper)
    SystemVoice,      // the operating system's voices (macOS AVSpeechSynthesizer)
};

enum class ArchiveKind {
    File,   // saved as is at `destination`
    TarBz2, // extracted into `destination` (a directory)
    Zip,
};

struct ModelPart {
    std::string url;
    ArchiveKind archive = ArchiveKind::File;
    std::string destination; // relative to the models directory
    std::uint64_t bytes = 0;
    bool apple_only = false; // e.g. Core ML encoders
};

struct CatalogModel {
    std::string id;
    ModelKind kind = ModelKind::Recognizer;
    ModelEngine engine = ModelEngine::SenseVoice;
    std::string name;
    // Language codes it handles; empty means every language.
    std::vector<std::string> languages;
    // Relative path that exists once it is installed.
    std::string path;
    std::vector<ModelPart> parts;
    bool bundled = false; // ships inside the app
    bool apple_only = false;
    int speaker = 0;      // default speaker for multi-speaker voices
    // A raw Piper voice (.onnx + .onnx.json from rhasspy/piper-voices) that
    // needs tokens.txt and sherpa-onnx metadata generated after download.
    bool import_piper = false;
    // Higher is preferred when the user leaves the choice on "automatic".
    int rank = 0;

    [[nodiscard]] bool covers(std::string_view language) const;
    [[nodiscard]] std::uint64_t download_bytes() const;
};

std::span<const CatalogModel> model_catalog();
const CatalogModel* find_model(std::string_view id);
// Models of one kind usable for a language on this platform, best first.
std::vector<const CatalogModel*> models_for(ModelKind kind, std::string_view language);
