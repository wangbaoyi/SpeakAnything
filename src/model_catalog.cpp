#include "model_catalog.h"

#include <algorithm>

namespace {

constexpr const char* huggingface = "https://huggingface.co/";
constexpr const char* tts_release = "https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/";

struct PiperVoice {
    const char* language;
    const char* voice; // vits-piper-<voice>
    std::uint64_t megabytes;
};

// One natural-sounding medium voice per language that Kokoro can't speak.
constexpr PiperVoice piper_voices[] = {
    {"de", "de_DE-thorsten-medium", 67},
    {"es", "es_ES-davefx-medium", 67},
    {"fr", "fr_FR-siwis-medium", 67},
    {"it", "it_IT-paola-medium", 67},
    {"pt", "pt_BR-faber-medium", 67},
    {"nl", "nl_NL-ronnie-medium", 67},
    {"pl", "pl_PL-darkman-medium", 67},
    {"cs", "cs_CZ-jirka-medium", 67},
    {"ro", "ro_RO-mihai-medium", 67},
    {"hu", "hu_HU-anna-medium", 67},
    {"sv", "sv_SE-nst-medium", 67},
    {"fi", "fi_FI-harri-medium", 67},
    {"tr", "tr_TR-dfki-medium", 67},
    {"vi", "vi_VN-vais1000-medium", 67},
    {"id", "id_ID-news_tts-medium", 67},
    {"ru", "ru_RU-irina-medium", 67},
    {"uk", "uk_UA-ukrainian_tts-medium", 80},
    {"sr", "sr_RS-serbski_institut-medium", 80},
    {"el", "el_GR-rapunzelina-low", 67},
    {"ar", "ar_JO-kareem-medium", 67},
    {"hi", "hi_IN-priyamvada-medium", 67},
};

std::vector<CatalogModel> build_catalog() {
    const std::string hf = huggingface;
    const std::string sensevoice_coreml =
        hf + "hugging-mac/sensevoice-small-coreml/resolve/main/sensevoice-small-coreml/";
    const std::string whisper = hf + "ggerganov/whisper.cpp/resolve/main/";
    const std::vector<std::string> sensevoice_languages = {"zh", "en", "yue", "ja", "ko"};

    std::vector<CatalogModel> catalog;

    // Speech recognition.
    catalog.push_back({
        .id = "sensevoice-coreml",
        .kind = ModelKind::Recognizer,
        .engine = ModelEngine::SenseVoiceCoreML,
        .name = "SenseVoice Small · Neural Engine",
        .languages = sensevoice_languages,
        .path = "sensevoice-coreml/sensevoice.mlpackage",
        .parts = {
            {sensevoice_coreml + "am.mvn", ArchiveKind::File, "sensevoice-coreml/am.mvn", 11'203},
            {sensevoice_coreml + "sensevoice.mlpackage/Manifest.json", ArchiveKind::File,
             "sensevoice-coreml/sensevoice.mlpackage/Manifest.json", 617},
            {sensevoice_coreml + "sensevoice.mlpackage/Data/com.apple.CoreML/model.mlmodel",
             ArchiveKind::File, "sensevoice-coreml/sensevoice.mlpackage/Data/com.apple.CoreML/model.mlmodel",
             2'813'640},
            {sensevoice_coreml + "sensevoice.mlpackage/Data/com.apple.CoreML/weights/weight.bin",
             ArchiveKind::File,
             "sensevoice-coreml/sensevoice.mlpackage/Data/com.apple.CoreML/weights/weight.bin",
             235'829'568},
            {hf + "FunAudioLLM/SenseVoiceSmall/resolve/main/chn_jpn_yue_eng_ko_spectok.bpe.model",
             ArchiveKind::File, "sensevoice-coreml/chn_jpn_yue_eng_ko_spectok.bpe.model", 377'341},
        },
        .apple_only = true,
        .rank = 30,
    });
    catalog.push_back({
        .id = "sensevoice-gguf",
        .kind = ModelKind::Recognizer,
        .engine = ModelEngine::SenseVoice,
        .name = "SenseVoice Small",
        .languages = sensevoice_languages,
        .path = "sensevoice-small-q8.gguf",
        .parts = {{hf + "FunAudioLLM/SenseVoiceSmall-GGUF/resolve/main/sensevoice-small-q8.gguf",
                   ArchiveKind::File, "sensevoice-small-q8.gguf", 254'208'320}},
        .bundled = true,
        .rank = 20,
    });
    catalog.push_back({
        .id = "whisper-small",
        .kind = ModelKind::Recognizer,
        .engine = ModelEngine::Whisper,
        .name = "Whisper Small",
        .path = "whisper/ggml-small-q5_1.bin",
        .parts = {
            {whisper + "ggml-small-q5_1.bin", ArchiveKind::File, "whisper/ggml-small-q5_1.bin", 190'085'487},
            {whisper + "ggml-small-encoder.mlmodelc.zip", ArchiveKind::Zip, "whisper", 163'083'239, true},
        },
        .rank = 10,
    });
    catalog.push_back({
        .id = "whisper-turbo",
        .kind = ModelKind::Recognizer,
        .engine = ModelEngine::Whisper,
        .name = "Whisper Large v3 Turbo",
        .path = "whisper/ggml-large-v3-turbo-q5_0.bin",
        .parts = {{whisper + "ggml-large-v3-turbo-q5_0.bin", ArchiveKind::File,
                   "whisper/ggml-large-v3-turbo-q5_0.bin", 574'041'195}},
        .rank = 15,
    });

    // Translation.
    catalog.push_back({
        .id = "qwen3-1.7b",
        .kind = ModelKind::Translator,
        .engine = ModelEngine::Llm,
        .name = "Qwen3 1.7B",
        .path = "qwen3-1.7b-q4_k_m.gguf",
        .parts = {{hf + "unsloth/Qwen3-1.7B-GGUF/resolve/main/Qwen3-1.7B-Q4_K_M.gguf",
                   ArchiveKind::File, "qwen3-1.7b-q4_k_m.gguf", 1'107'409'472}},
        .bundled = true,
        .rank = 10,
    });
    catalog.push_back({
        .id = "qwen3-4b",
        .kind = ModelKind::Translator,
        .engine = ModelEngine::Llm,
        .name = "Qwen3 4B · better for less common languages",
        .path = "qwen3-4b-q4_k_m.gguf",
        .parts = {{hf + "unsloth/Qwen3-4B-GGUF/resolve/main/Qwen3-4B-Q4_K_M.gguf",
                   ArchiveKind::File, "qwen3-4b-q4_k_m.gguf", 2'497'000'000}},
        .rank = 20,
    });

    // Voices.
    catalog.push_back({
        .id = "kokoro-v1.1",
        .kind = ModelKind::Voice,
        .engine = ModelEngine::Kokoro,
        .name = "Kokoro v1.1",
        .languages = {"en", "zh"},
        .path = "kokoro-multi-lang-v1_1/model.onnx",
        .parts = {{std::string(tts_release) + "kokoro-multi-lang-v1_1.tar.bz2", ArchiveKind::TarBz2, "",
                   364'000'000}},
        .bundled = true,
        .rank = 20,
    });
    for (const PiperVoice& voice : piper_voices) {
        const std::string folder = std::string("vits-piper-") + voice.voice;
        catalog.push_back({
            .id = std::string("piper-") + voice.language,
            .kind = ModelKind::Voice,
            .engine = ModelEngine::Piper,
            .name = std::string("Piper · ") + voice.voice,
            .languages = {voice.language},
            .path = folder + "/tokens.txt",
            .parts = {{std::string(tts_release) + folder + ".tar.bz2", ArchiveKind::TarBz2, "",
                       voice.megabytes * 1'000'000}},
            .rank = 10,
        });
    }
    // Piper voices sherpa-onnx never repackaged, straight from
    // rhasspy/piper-voices; converted for sherpa-onnx after download.
    // (Japanese Piper voices need OpenJTalk, which sherpa-onnx lacks.)
    struct RawPiperVoice {
        const char* language;
        const char* path; // under rhasspy/piper-voices, without extension
    };
    constexpr RawPiperVoice raw_piper_voices[] = {
        {"bg", "bg/bg_BG/dimitar/medium/bg_BG-dimitar-medium"},
        {"ko", "ko/ko_KR/kss/medium/ko_KR-kss-medium"},
        {"th", "th/th_TH/tsync2/medium/th_TH-tsync2-medium"},
    };
    for (const RawPiperVoice& voice : raw_piper_voices) {
        const std::string path = voice.path;
        const std::string name = path.substr(path.rfind('/') + 1);
        const std::string folder = "piper-" + name;
        const std::string url = hf + "rhasspy/piper-voices/resolve/main/" + path;
        catalog.push_back({
            .id = std::string("piper-") + voice.language,
            .kind = ModelKind::Voice,
            .engine = ModelEngine::Piper,
            .name = "Piper · " + name,
            .languages = {voice.language},
            .path = folder + "/tokens.txt",
            .parts = {
                {url + ".onnx", ArchiveKind::File, folder + "/" + name + ".onnx", 63'221'984},
                {url + ".onnx.json", ArchiveKind::File, folder + "/" + name + ".onnx.json", 5'200},
            },
            .import_piper = true,
            .rank = 10,
        });
    }

    // Built-in macOS voices cover languages no downloadable voice does, such
    // as Bulgarian; nothing to download, lowest rank so real models win.
    catalog.push_back({
        .id = "system-voice",
        .kind = ModelKind::Voice,
        .engine = ModelEngine::SystemVoice,
        .name = "macOS system voice",
        .bundled = true,
        .apple_only = true,
        .rank = 0,
    });
    return catalog;
}

} // namespace

bool CatalogModel::covers(std::string_view language) const {
    return languages.empty() ||
        std::find(languages.begin(), languages.end(), language) != languages.end();
}

std::uint64_t CatalogModel::download_bytes() const {
    std::uint64_t total = 0;
    for (const ModelPart& part : parts) {
#ifndef __APPLE__
        if (part.apple_only) continue;
#endif
        total += part.bytes;
    }
    return total;
}

std::span<const CatalogModel> model_catalog() {
    static const std::vector<CatalogModel> catalog = build_catalog();
    return catalog;
}

const CatalogModel* find_model(std::string_view id) {
    for (const CatalogModel& model : model_catalog()) {
        if (model.id == id) return &model;
    }
    return nullptr;
}

std::vector<const CatalogModel*> models_for(ModelKind kind, std::string_view language) {
    std::vector<const CatalogModel*> result;
    for (const CatalogModel& model : model_catalog()) {
#ifndef __APPLE__
        if (model.apple_only) continue;
#endif
        if (model.kind == kind && model.covers(language)) result.push_back(&model);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const CatalogModel* a, const CatalogModel* b) { return a->rank > b->rank; });
    return result;
}
