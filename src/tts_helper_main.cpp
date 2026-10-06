// sensevoice-tts.exe: Kokoro speech synthesis in its own process, so the GPL
// espeak-ng it pulls in stays outside the main program (docs/adr/0002).
//
// Protocol (binary stdin/stdout, one request at a time):
//   startup  -> "READY <sample_rate>\n"            or "ERROR <message>\n" and exit
//   request  <- "<sid>\t<speed>\t<utf-8 text>\n"
//   response -> "AUDIO <sample_count>\n" + float32 samples, or "ERROR <message>\n"
// EOF on stdin ends the process.

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "sherpa-onnx/c-api/c-api.h"

namespace {

void reply_error(const std::string& message) {
    std::string line = "ERROR " + message;
    for (char& c : line) {
        if (c == '\n' || c == '\r') c = ' ';
    }
    line += '\n';
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    if (argc < 2) {
        reply_error("usage: sensevoice-tts <kokoro model directory> [threads]");
        return 2;
    }
    const std::filesystem::path model_directory(argv[1]);
    const int threads = argc >= 3 ? std::max(1, _wtoi(argv[2])) : 4;
    // sherpa-onnx and espeak-ng open files through narrow paths, which break
    // under non-ASCII install directories; work from inside the model directory
    // so every path handed to them is plain ASCII.
    if (!std::filesystem::is_regular_file(model_directory / L"model.onnx") ||
        !SetCurrentDirectoryW(model_directory.c_str())) {
        reply_error("missing Kokoro model");
        return 1;
    }

    const std::string model = "model.onnx";
    const std::string voices = "voices.bin";
    const std::string tokens = "tokens.txt";
    const std::string data_dir = "espeak-ng-data";
    const std::string lexicon = "lexicon-us-en.txt,lexicon-zh.txt";
    const std::string rule_fsts = "phone-zh.fst,date-zh.fst,number-zh.fst";

    SherpaOnnxOfflineTtsConfig config;
    std::memset(&config, 0, sizeof(config));
    config.model.kokoro.model = model.c_str();
    config.model.kokoro.voices = voices.c_str();
    config.model.kokoro.tokens = tokens.c_str();
    config.model.kokoro.data_dir = data_dir.c_str();
    config.model.kokoro.lexicon = lexicon.c_str();
    config.model.kokoro.length_scale = 1.0F;
    config.model.num_threads = threads;
    config.model.provider = "cpu";
    config.rule_fsts = rule_fsts.c_str();
    config.max_num_sentences = 1;

    const SherpaOnnxOfflineTts* tts = SherpaOnnxCreateOfflineTts(&config);
    if (tts == nullptr) {
        reply_error("failed to load Kokoro model");
        return 1;
    }
    const std::string ready = "READY " + std::to_string(SherpaOnnxOfflineTtsSampleRate(tts)) + "\n";
    std::fwrite(ready.data(), 1, ready.size(), stdout);
    std::fflush(stdout);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::size_t first = line.find('\t');
        const std::size_t second = first == std::string::npos ? first : line.find('\t', first + 1);
        if (second == std::string::npos) {
            reply_error("malformed request");
            continue;
        }
        SherpaOnnxGenerationConfig generation;
        std::memset(&generation, 0, sizeof(generation));
        generation.sid = std::atoi(line.substr(0, first).c_str());
        generation.speed = static_cast<float>(std::atof(line.substr(first + 1, second - first - 1).c_str()));
        if (generation.speed <= 0.0F) generation.speed = 1.0F;
        generation.silence_scale = 0.2F;
        const std::string text = line.substr(second + 1);

        const SherpaOnnxGeneratedAudio* audio =
            SherpaOnnxOfflineTtsGenerateWithConfig(tts, text.c_str(), &generation, nullptr, nullptr);
        if (audio == nullptr) {
            reply_error("synthesis failed");
            continue;
        }
        const std::string header = "AUDIO " + std::to_string(audio->n) + "\n";
        std::fwrite(header.data(), 1, header.size(), stdout);
        std::fwrite(audio->samples, sizeof(float), static_cast<std::size_t>(audio->n), stdout);
        std::fflush(stdout);
        SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
    }
    SherpaOnnxDestroyOfflineTts(tts);
    return 0;
}
