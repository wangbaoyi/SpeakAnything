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
        std::unique_ptr<sentencepiece::SentencePieceProcessor> target)
        : model_(std::move(model)), source_(std::move(source)), target_(std::move(target)) {}

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

TranslatorFactory make_opus_mt_factory(std::filesystem::path models_directory, int threads) {
    return [models_directory = std::move(models_directory), threads](
               TranslationDirection direction, std::string& error) -> std::unique_ptr<Translator> {
        const auto directory = opus_mt_model_directory(models_directory, direction);
        if (!std::filesystem::exists(directory / "model.bin")) {
            error = "translation model not found: " + utf8_path(directory);
            return nullptr;
        }
        auto source = load_spm(directory / "source.spm", error);
        if (!source) return nullptr;
        auto target = load_spm(directory / "target.spm", error);
        if (!target) return nullptr;
        try {
            ctranslate2::ReplicaPoolConfig config;
            config.num_threads_per_replica = static_cast<std::size_t>(threads > 0 ? threads : 0);
            auto model = std::make_unique<ctranslate2::Translator>(
                utf8_path(directory),
                ctranslate2::Device::CPU,
                ctranslate2::ComputeType::INT8,
                std::vector<int>{0},
                false,
                config);
            auto translator = std::make_unique<OpusMtTranslator>(
                std::move(model), std::move(source), std::move(target));
            // Warm up so inference buffers are resident before any recording
            // session samples its working-set baseline.
            std::string warmup_error;
            translator->translate(
                direction == TranslationDirection::ZhToEn ? "你好。" : "Hello.", warmup_error);
            return translator;
        } catch (const std::exception& exception) {
            error = exception.what();
            return nullptr;
        }
    };
}

#else

TranslatorFactory make_opus_mt_factory(std::filesystem::path, int) {
    return [](TranslationDirection, std::string& error) -> std::unique_ptr<Translator> {
        error = "this build was compiled without translation support";
        return nullptr;
    };
}

#endif
