#include "translator.h"

#include <utility>

std::filesystem::path opus_mt_model_directory(
    const std::filesystem::path& models_directory, TranslationDirection direction) {
    return models_directory /
        (direction == TranslationDirection::ZhToEn ? "opus-mt-zh-en-ct2" : "opus-mt-en-zh-ct2");
}

TranslationWorker::TranslationWorker(
    TranslatorFactory factory, LoadCallback on_loaded, ResultCallback on_result)
    : factory_(std::move(factory)),
      on_loaded_(std::move(on_loaded)),
      on_result_(std::move(on_result)),
      thread_([this] { run(); }) {}

TranslationWorker::~TranslationWorker() {
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        commands_.clear();
    }
    condition_.notify_all();
    thread_.join();
}

void TranslationWorker::load(TranslationDirection direction) {
    Command command;
    command.kind = Command::Kind::Load;
    command.direction = direction;
    push(std::move(command));
}

void TranslationWorker::unload() {
    Command command;
    command.kind = Command::Kind::Unload;
    push(std::move(command));
}

void TranslationWorker::translate(std::uint64_t ticket, std::string text) {
    Command command;
    command.kind = Command::Kind::Translate;
    command.ticket = ticket;
    command.text = std::move(text);
    push(std::move(command));
}

void TranslationWorker::push(Command command) {
    {
        std::lock_guard lock(mutex_);
        commands_.push_back(std::move(command));
    }
    condition_.notify_one();
}

void TranslationWorker::run() {
    for (;;) {
        Command command;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
            if (stopping_) break;
            command = std::move(commands_.front());
            commands_.pop_front();
        }

        switch (command.kind) {
        case Command::Kind::Load: {
            if (translator_ && loaded_direction_ == command.direction) {
                if (on_loaded_) on_loaded_(command.direction, true, {});
                break;
            }
            // Free the old model first so two directions are never resident together.
            translator_.reset();
            loaded_direction_.reset();
            std::string error;
            translator_ = factory_ ? factory_(command.direction, error) : nullptr;
            if (translator_) {
                loaded_direction_ = command.direction;
            } else if (error.empty()) {
                error = "translator factory returned no model";
            }
            if (on_loaded_) on_loaded_(command.direction, translator_ != nullptr, error);
            break;
        }
        case Command::Kind::Unload:
            translator_.reset();
            loaded_direction_.reset();
            break;
        case Command::Kind::Translate: {
            std::string error;
            std::optional<std::string> translation;
            if (translator_) {
                translation = translator_->translate(command.text, error);
            } else {
                error = "translation model is not loaded";
            }
            if (on_result_) on_result_(command.ticket, std::move(translation), error);
            break;
        }
        }
    }
    translator_.reset();
}
