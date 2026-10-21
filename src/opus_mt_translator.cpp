#include "translator.h"

#include <utility>
#include <vector>

#ifdef SENSEVOICE_WITH_TRANSLATION
#include <ctranslate2/translator.h>
#include <sentencepiece_processor.h>

namespace {

// SentencePiece and CTranslate2 both decode narrow paths as UTF-8 on Windows;
// path::string() would hand them the ANSI code page instead.
std::string utf8_path(const std::filesystem::path& path) {
    const std::u8string utf8 = path.u8string();
    return std::string(utf8.begin(), utf8.end());
}

// Opus-MT is a Marian model: SentencePiece pieces in, an explicit </s> to end
// the source, pieces out through the target-side SentencePiece model.
class OpusMtTranslator final : public Translator {
public:
    OpusMtTranslator(
        std::unique_ptr<ctranslate2::Translator> model,
        std::unique_ptr<sentencepiece::SentencePieceProcessor> source,
        std::unique_ptr<sentencepiece::SentencePieceProcessor> target,
        bool cuda,
        std::string device_label)
        : model_(std::move(model)), source_(std::move(source)), target_(std::move(target)),
          cuda_(cuda), device_label_(std::move(device_label)) {}

    std::string_view backend_name() const override {
        // Name the compute type too: CUDA runs FLOAT32, the CPU path INT8.
        return cuda_ ? "Opus-MT (CTranslate2 CUDA FP32)"
                     : "Opus-MT (CTranslate2 CPU INT8)";
    }
    std::string device_label() const override { return device_label_; }

    std::optional<std::string> translate(std::string_view text, std::string& error) override {
        try {
            std::vector<std::string> pieces;
            const auto encoded = source_->Encode(text, &pieces);
            if (!encoded.ok()) {
                error = "source tokenization failed: " + encoded.ToString();
                return std::nullopt;
            }
            pieces.emplace_back("</s>");

            ctranslate2::TranslationOptions options;
            options.beam_size = 2;
            options.max_decoding_length = 256;
            const auto results = model_->translate_batch({pieces}, options);
            if (results.empty() || results.front().hypotheses.empty()) {
                error = "translation produced no hypothesis";
                return std::nullopt;
            }

            std::string output;
            const auto decoded = target_->Decode(results.front().output(), &output);
            if (!decoded.ok()) {
                error = "target detokenization failed: " + decoded.ToString();
                return std::nullopt;
            }
            return output;
        } catch (const std::exception& exception) {
            error = exception.what();
            return std::nullopt;
        }
    }

private:
    std::unique_ptr<ctranslate2::Translator> model_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> source_;
    std::unique_ptr<sentencepiece::SentencePieceProcessor> target_;
    bool cuda_ = false;
    std::string device_label_ = "CPU";
};

std::unique_ptr<sentencepiece::SentencePieceProcessor> load_spm(
    const std::filesystem::path& path, std::string& error) {
    auto processor = std::make_unique<sentencepiece::SentencePieceProcessor>();
    const auto status = processor->Load(utf8_path(path));
    if (!status.ok()) {
        error = "cannot load " + utf8_path(path) + ": " + status.ToString();
        return nullptr;
    }
    return processor;
}

}  // namespace

TranslatorFactory make_opus_mt_factory(
    std::filesystem::path models_directory,
    int threads,
    ComputeDevice device,
    std::string device_label) {
    return [models_directory = std::move(models_directory), threads, device,
            device_label = std::move(device_label)](
               TranslationDirection direction, std::string& error) -> std::unique_ptr<Translator> {
        const auto directory = opus_mt_model_directory(models_directory, direction);
        if (!std::filesystem::exists(directory / "model.bin")) {
            error = "translation model not found: " + utf8_path(directory);
            return nullptr;
        }
        const bool use_cuda = device == ComputeDevice::Gpu;
#ifndef SENSEVOICE_CT2_CUDA
        if (use_cuda) {
            // Never run the CPU model under a GPU label.
            error =
                "Opus-MT on the GPU needs a CTranslate2 CUDA build "
                "(configure with -DSENSEVOICE_CT2_CUDA=ON)";
            return nullptr;
        }
#endif
        auto source = load_spm(directory / "source.spm", error);
        if (!source) return nullptr;
        auto target = load_spm(directory / "target.spm", error);
        if (!target) return nullptr;
        try {
            ctranslate2::ReplicaPoolConfig config;
            config.num_threads_per_replica = static_cast<std::size_t>(threads > 0 ? threads : 0);
            std::unique_ptr<ctranslate2::Translator> model;
            if (use_cuda) {
#ifdef SENSEVOICE_CT2_CUDA
                // Pascal has no fast INT8 path in CTranslate2; FLOAT32 was the
                // fastest measured option on the GTX 1080 Ti.
                model = std::make_unique<ctranslate2::Translator>(
                    utf8_path(directory),
                    ctranslate2::Device::CUDA,
                    ctranslate2::ComputeType::FLOAT32,
                    std::vector<int>{0},
                    false,
                    config);
#endif
            } else {
                model = std::make_unique<ctranslate2::Translator>(
                    utf8_path(directory),
                    ctranslate2::Device::CPU,
                    ctranslate2::ComputeType::INT8,
                    std::vector<int>{0},
                    false,
                    config);
            }
            if (!model) {
                error = "Opus-MT CUDA support was not compiled into this build";
                return nullptr;
            }
            const std::string label = device_label.empty()
                ? (use_cuda ? std::string("GPU (CUDA)") : std::string("CPU"))
                : device_label;
            auto translator = std::make_unique<OpusMtTranslator>(
                std::move(model), std::move(source), std::move(target), use_cuda, label);
            // Warm up so inference buffers are resident before any recording
            // session samples its working-set baseline.
            std::string warmup_error;
            translator->translate(
                direction.source == "zh" ? "你好。" : "Hello.", warmup_error);
            return translator;
        } catch (const std::exception& exception) {
            error = exception.what();
            return nullptr;
        }
    };
}

#else

TranslatorFactory make_opus_mt_factory(
    std::filesystem::path, int, ComputeDevice, std::string) {
    return [](TranslationDirection, std::string& error) -> std::unique_ptr<Translator> {
        error = "this build was compiled without translation support";
        return nullptr;
    };
}

#endif
