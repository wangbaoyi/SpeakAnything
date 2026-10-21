#include "stream_recognizer.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#elif defined(__APPLE__)
#include <mach/mach.h>
#endif

namespace {

constexpr std::size_t samples_for_ms(int milliseconds) {
    return static_cast<std::size_t>(milliseconds) * 16;
}

std::size_t maximum_audio_samples(const StreamRecognizerConfig& config) {
    return samples_for_ms(std::clamp(config.maximum_utterance_ms, 1'000, 30'000));
}

std::int64_t steady_now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool serial_vad_from_environment() {
    const char* value = std::getenv("SENSEVOICE_SERIAL_VAD");
    return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

std::size_t process_working_set_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory))) {
        return 0;
    }
    return static_cast<std::size_t>(memory.WorkingSetSize);
#elif defined(__APPLE__)
    // phys_footprint is what Activity Monitor reports as "Memory".
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return 0;
    }
    return static_cast<std::size_t>(info.phys_footprint);
#else
    return 0;
#endif
}

bool process_memory_over_limit(int limit_mb, std::size_t baseline_working_set_bytes) {
    if (limit_mb <= 0) {
        return false;
    }
    const std::size_t current_working_set_bytes = process_working_set_bytes();
    if (current_working_set_bytes == 0) {
        return false;
    }

    // The model and dictionaries are resident before the stream starts. Keep the
    // configured line meaningful without allowing that fixed cost to split every
    // utterance immediately on machines whose baseline is already above the line.
    constexpr std::size_t minimum_runtime_headroom_bytes = 32ULL * 1024ULL * 1024ULL;
    const std::size_t configured_limit_bytes =
        static_cast<std::size_t>(limit_mb) * 1024ULL * 1024ULL;
    const std::size_t baseline_aware_limit_bytes =
        baseline_working_set_bytes > 0
        ? baseline_working_set_bytes + minimum_runtime_headroom_bytes
        : configured_limit_bytes;
    const std::size_t effective_limit_bytes =
        std::max(configured_limit_bytes, baseline_aware_limit_bytes);
    return current_working_set_bytes >= effective_limit_bytes;
}

} // namespace

StreamRecognizer::StreamRecognizer(
    SpeechEngine& engine,
    FsmnVadEngine* vad,
    StreamRecognizerConfig config,
    EventHandler handler,
    TextProcessor* text_processor)
    : engine_(engine),
      vad_(vad),
      config_(config),
      handler_(std::move(handler)),
      text_processor_(text_processor),
      serial_vad_(serial_vad_from_environment()),
      stability_(config.stability_history, config.stability_holdback_chars) {}

StreamRecognizer::~StreamRecognizer() {
    cancel();
}

VadResult StreamRecognizer::vad_telemetry() const {
    VadResult result;
    result.current_db = vad_current_db_.load(std::memory_order_relaxed);
    result.peak_db = vad_peak_db_.load(std::memory_order_relaxed);
    result.noise_floor_db = vad_noise_floor_db_.load(std::memory_order_relaxed);
    result.required_db = vad_required_db_.load(std::memory_order_relaxed);
    result.speech_probability = vad_speech_probability_.load(std::memory_order_relaxed);
    result.activity = static_cast<VadActivity>(vad_activity_.load(std::memory_order_relaxed));
    return result;
}

void publish_vad_telemetry(
    const VadResult& result,
    std::atomic<float>& current_db,
    std::atomic<float>& peak_db,
    std::atomic<float>& noise_floor_db,
    std::atomic<float>& required_db,
    std::atomic<float>& speech_probability,
    std::atomic<int>& activity) {
    current_db.store(result.current_db, std::memory_order_relaxed);
    peak_db.store(result.peak_db, std::memory_order_relaxed);
    noise_floor_db.store(result.noise_floor_db, std::memory_order_relaxed);
    required_db.store(result.required_db, std::memory_order_relaxed);
    speech_probability.store(result.speech_probability, std::memory_order_relaxed);
    activity.store(static_cast<int>(result.activity), std::memory_order_relaxed);
}

void StreamRecognizer::start() {
    std::scoped_lock lifecycle(lifecycle_mutex_);
    std::scoped_lock lock(mutex_);
    if (started_) {
        return;
    }
    baseline_working_set_bytes_ = process_working_set_bytes();
    started_ = true;
    worker_ = std::thread(&StreamRecognizer::run, this);
    if (!serial_vad_ && vad_ != nullptr) {
        vad_worker_ = std::thread(&StreamRecognizer::run_vad_worker, this);
    }
}

void StreamRecognizer::accept_pcm(std::span<const float> samples) {
    if (samples.empty()) {
        return;
    }
    {
        std::scoped_lock lock(mutex_);
        if (!started_ || finish_requested_ || cancel_requested_) {
            return;
        }
        audio_.insert(audio_.end(), samples.begin(), samples.end());
        ++audio_revision_;
        dirty_ = true;
        // Coalescing plus throttling: intermediate revisions are skipped, and
        // a new pass is only requested once at least 80 ms of audio has
        // arrived since the last pass started. VAD therefore samples the
        // growing buffer at a bounded rate instead of chasing every chunk.
        constexpr std::size_t minimum_new_vad_samples = 80 * 16;
        const std::size_t covered = std::min(audio_.size(), last_vad_pass_samples_);
        if (audio_.size() - covered >= minimum_new_vad_samples) {
            vad_requested_ = true;
        }
        last_accept_ns_.store(steady_now_ns(), std::memory_order_release);
    }
    condition_.notify_one();
    vad_condition_.notify_one();
}

void StreamRecognizer::finish() {
    std::scoped_lock lifecycle(lifecycle_mutex_);
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            return;
        }
        finish_requested_ = true;
        finish_revision_ = audio_revision_;
        // If a published pass already covers everything accepted so far the
        // final can reuse it; otherwise ask for one pass over the final audio.
        const bool covered = vad_result_.has_value() &&
            vad_result_->generation == audio_generation_ &&
            vad_result_->revision >= finish_revision_ &&
            vad_result_->audio_samples >= audio_.size();
        if (!covered) {
            vad_requested_ = true;
        }
    }
    condition_.notify_all();
    vad_condition_.notify_all();
    // The VAD worker never waits on the ASR worker, so joining it first is
    // safe. It exits when the recognizer signals that it is done, after any
    // final VAD pass has been served.
    if (vad_worker_.joinable()) {
        vad_worker_.join();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::scoped_lock lock(mutex_);
    started_ = false;
}

void StreamRecognizer::cancel() {
    std::scoped_lock lifecycle(lifecycle_mutex_);
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            return;
        }
        cancel_requested_ = true;
    }
    condition_.notify_all();
    vad_condition_.notify_all();
    if (vad_worker_.joinable()) {
        vad_worker_.join();
    }
    if (worker_.joinable()) {
        worker_.join();
    }
    std::scoped_lock lock(mutex_);
    started_ = false;
}

void StreamRecognizer::run() {
    struct CompletionGuard {
        StreamRecognizer* self;
        ~CompletionGuard() {
            std::scoped_lock lock(self->mutex_);
            self->asr_done_ = true;
            self->vad_condition_.notify_all();
        }
    } guard{this};
    if (serial_vad_ || vad_ == nullptr) {
        run_serial();
    } else {
        run_overlapped();
    }
}

VadDetectionOptions StreamRecognizer::vad_options() const {
    return VadDetectionOptions{
        .maximum_segment_ms = std::max(config_.maximum_utterance_ms, 1'000),
        .endpoint_silence_ms = config_.endpoint_silence_ms,
        .speech_noise_threshold = config_.vad_speech_threshold,
        .minimum_db = config_.vad_minimum_db,
        .minimum_snr_db = config_.vad_minimum_snr_db,
        .minimum_speech_ms = config_.vad_minimum_speech_ms,
    };
}

bool StreamRecognizer::fresh_vad_available() const {
    return vad_result_.has_value() &&
        vad_result_->generation == audio_generation_ &&
        vad_result_->revision > last_vad_revision_consumed_ &&
        vad_result_->audio_samples > last_decoded_samples_;
}

void StreamRecognizer::run_serial() {
    using clock = std::chrono::steady_clock;
    auto next_partial = clock::now();

    for (;;) {
        std::vector<float> snapshot;
        std::size_t snapshot_audio_samples = 0;
        bool final_result = false;
        {
            std::unique_lock lock(mutex_);
            condition_.wait_until(lock, next_partial, [&] {
                return cancel_requested_ || finish_requested_ ||
                    (dirty_ && clock::now() >= next_partial);
            });
            if (cancel_requested_) {
                return;
            }
            if (finish_requested_) {
                final_result = true;
                snapshot = audio_;
                snapshot_audio_samples = audio_.size();
            } else {
                const auto minimum_samples = samples_for_ms(config_.minimum_audio_ms);
                const auto new_samples = audio_.size() - last_decoded_samples_;
                if (!dirty_ || audio_.size() < minimum_samples ||
                    new_samples < samples_for_ms(config_.minimum_new_audio_ms)) {
                    next_partial = clock::now() + std::chrono::milliseconds(20);
                    continue;
                }
                snapshot = audio_;
                snapshot_audio_samples = audio_.size();
                dirty_ = false;
                next_partial = clock::now() +
                    std::chrono::milliseconds(std::max(50, config_.partial_interval_ms));
            }
        }

        if (snapshot.empty()) {
            if (final_result) {
                if (!emitted_utterance_) {
                    emit_result(SenseVoiceResult{}, true);
                }
                return;
            }
            continue;
        }

        try {
            VadResult detected_vad;
            const VadResult* vad_result = nullptr;
            if (vad_ != nullptr) {
                detected_vad = vad_->detect(
                    snapshot,
                    VadDetectionOptions{
                        .maximum_segment_ms = std::max(config_.maximum_utterance_ms, 1'000),
                        .endpoint_silence_ms = config_.endpoint_silence_ms,
                        .speech_noise_threshold = config_.vad_speech_threshold,
                        .minimum_db = config_.vad_minimum_db,
                        .minimum_snr_db = config_.vad_minimum_snr_db,
                        .minimum_speech_ms = config_.vad_minimum_speech_ms,
                    });
                vad_result = &detected_vad;
                last_vad_telemetry_ = detected_vad;
                publish_vad_telemetry(
                    detected_vad,
                    vad_current_db_,
                    vad_peak_db_,
                    vad_noise_floor_db_,
                    vad_required_db_,
                    vad_speech_probability_,
                    vad_activity_);
            }

            const std::size_t budget_samples = maximum_audio_samples(config_);
            const bool over_budget = snapshot.size() > budget_samples;
            const bool over_memory = process_memory_over_limit(
                config_.memory_limit_mb,
                baseline_working_set_bytes_);
            if (over_budget || (!final_result && over_memory)) {
                std::size_t split_samples = std::min(budget_samples, snapshot.size());
                if (vad_result != nullptr) {
                    std::size_t nearest_silence = 0;
                    for (const SpeechSegment& segment : vad_result->segments) {
                        const std::size_t end = std::min(
                            snapshot.size(),
                            samples_for_ms(std::max(0, segment.end_ms)));
                        if (end > 0 && end <= budget_samples) {
                            nearest_silence = std::max(nearest_silence, end);
                        }
                    }
                    if (nearest_silence >= samples_for_ms(1'000)) {
                        split_samples = nearest_silence;
                    }
                }
                split_samples = std::min(split_samples, snapshot.size());
                const std::size_t minimum_split_samples = over_memory && !over_budget
                    ? samples_for_ms(3'000)
                    : samples_for_ms(1'000);
                if (split_samples >= minimum_split_samples) {
                    SenseVoiceResult result = recognize_audio(
                        std::span<const float>(snapshot.data(), split_samples));
                    result.audio_ms = static_cast<int>(split_samples / 16);
                    remember_result(result, split_samples);
                    forced_split_ = true;
                    emit_result(result, true);
                    emitted_utterance_ = true;
                    stability_.reset();
                    consume_audio_prefix(split_samples, true);
                    next_partial = clock::now();
                    continue;
                }
            }

            if (!final_result && vad_result != nullptr) {
                const auto endpoint = std::find_if(
                    vad_result->segments.begin(),
                    vad_result->segments.end(),
                    [](const SpeechSegment& segment) { return segment.finalized; });
                if (endpoint != vad_result->segments.end()) {
                    SenseVoiceResult result = recognize_snapshot(snapshot, vad_result, &*endpoint);
                    result.audio_ms = static_cast<int>(snapshot_audio_samples / 16);
                    remember_result(result, snapshot.size());
                    emit_result(result, true);
                    emitted_utterance_ = true;
                    stability_.reset();
                    const auto next_segment = std::next(endpoint);
                    const std::size_t consumed_samples = next_segment == vad_result->segments.end()
                        ? snapshot.size()
                        : samples_for_ms(next_segment->start_ms);
                    consume_audio_prefix(
                        consumed_samples,
                        next_segment != vad_result->segments.end());
                    next_partial = clock::now();
                    continue;
                }
            }

            SenseVoiceResult result;
            if (final_result && snapshot.size() == last_decoded_samples_) {
                result.text = last_text_;
                result.audio_ms = static_cast<int>(snapshot.size() / 16);
                result.vad_ms = last_vad_ms_;
                result.inference_ms = last_inference_ms_;
                result.hotword_bias_applied = last_hotword_bias_applied_;
            } else if (final_result && vad_result == nullptr) {
                // Bounded-wait fallback: decode the whole buffer. Calling
                // recognize_snapshot here would run a second VAD pass on the
                // recognizer thread while the VAD worker may still own the
                // engine.
                result = recognize_audio(snapshot);
            } else {
                result = recognize_snapshot(snapshot, vad_result);
            }
            result.audio_ms = static_cast<int>(snapshot_audio_samples / 16);
            remember_result(result, snapshot.size());
            if (!final_result || !result.text.empty() || !emitted_utterance_) {
                emit_result(result, final_result);
            }
            if (!final_result && vad_result != nullptr &&
                vad_result->segments.empty() &&
                snapshot.size() > samples_for_ms(config_.silence_preroll_ms)) {
                consume_audio_prefix(
                    snapshot.size() - samples_for_ms(config_.silence_preroll_ms),
                    false);
            }
        } catch (const std::exception& exception) {
            emit_error(exception.what());
            return;
        }

        if (final_result) {
            return;
        }
    }
}

void StreamRecognizer::run_vad_worker() {
    for (;;) {
        std::vector<float> snapshot;
        std::uint64_t revision = 0;
        std::uint64_t generation = 0;
        {
            std::unique_lock lock(mutex_);
            vad_condition_.wait(lock, [&] {
                return cancel_requested_ || vad_requested_ || asr_done_;
            });
            // The recognizer only stops while no final is pending, so once it
            // is done this worker has nothing left to serve.
            if (cancel_requested_ || asr_done_) {
                return;
            }
            // Coalesce: take the newest audio, not a backlog of revisions.
            revision = audio_revision_;
            generation = audio_generation_;
            snapshot = audio_;
            last_vad_pass_samples_ = snapshot.size();
            vad_requested_ = false;
            if (snapshot.empty()) {
                if (finish_requested_) {
                    return;
                }
                continue;
            }
        }

        VadResult result;
        try {
            result = vad_->detect(snapshot, vad_options());
        } catch (const std::exception& exception) {
            std::scoped_lock lock(mutex_);
            if (!cancel_requested_ && vad_error_.empty()) {
                vad_error_ = exception.what();
            }
            condition_.notify_all();
            return;
        } catch (...) {
            std::scoped_lock lock(mutex_);
            if (!cancel_requested_ && vad_error_.empty()) {
                vad_error_ = "FSMN-VAD worker failed";
            }
            condition_.notify_all();
            return;
        }

        publish_vad_telemetry(
            result,
            vad_current_db_,
            vad_peak_db_,
            vad_noise_floor_db_,
            vad_required_db_,
            vad_speech_probability_,
            vad_activity_);

        {
            std::scoped_lock lock(mutex_);
            if (cancel_requested_) {
                return;
            }
            // A trim happened while this pass was running: the segment times
            // no longer match the buffer, so drop the result. The trim already
            // requested a fresh pass for the new generation.
            if (generation == audio_generation_) {
                vad_result_ = VadSnapshot{
                    generation,
                    revision,
                    snapshot.size(),
                    std::move(result),
                };
                // This pass already covers everything the final needs. Drop
                // any request queued before it started so teardown does not
                // pay for one redundant scan.
                if (finish_requested_ && revision >= finish_revision_) {
                    vad_requested_ = false;
                }
            }
        }
        condition_.notify_all();
    }
}

void StreamRecognizer::run_overlapped() {
    using clock = std::chrono::steady_clock;
    auto next_partial = clock::now();

    for (;;) {
        std::vector<float> snapshot;
        std::size_t snapshot_audio_samples = 0;
        std::optional<VadSnapshot> vad_work;
        bool final_result = false;
        {
            std::unique_lock lock(mutex_);
            const std::size_t minimum_samples = samples_for_ms(config_.minimum_audio_ms);
            const std::size_t minimum_new_samples =
                samples_for_ms(config_.minimum_new_audio_ms);
            auto ready = [&] {
                if (cancel_requested_ || !vad_error_.empty()) {
                    return true;
                }
                if (finish_requested_) {
                    return true;
                }
                if (clock::now() < next_partial) {
                    return false;
                }
                // Gate on what the VAD pass actually covered, not on the raw
                // buffer: an older result may predate the minimum utterance.
                if (!fresh_vad_available() ||
                    vad_result_->audio_samples < minimum_samples) {
                    return false;
                }
                return vad_result_->audio_samples - last_decoded_samples_ >=
                    minimum_new_samples;
            };
            condition_.wait_until(lock, next_partial, ready);
            if (cancel_requested_) {
                return;
            }

            if (finish_requested_) {
                // A final must see a VAD pass over the current audio, so wait
                // for a result from this generation covering the whole buffer.
                // The wait is bounded: a stuck VAD pass must not swallow the
                // final, so fall back to a plain decode without segments.
                const bool got_vad = condition_.wait_for(
                    lock,
                    std::chrono::seconds(5),
                    [&] {
                        return cancel_requested_ || !vad_error_.empty() || audio_.empty() ||
                            (vad_result_.has_value() &&
                             vad_result_->generation == audio_generation_ &&
                             vad_result_->revision >= finish_revision_ &&
                             vad_result_->audio_samples >= audio_.size());
                    });
                if (cancel_requested_) {
                    return;
                }
                if (!vad_error_.empty()) {
                    // The VAD worker failed: still deliver the final rather
                    // than dropping it. Without segments this is one decode
                    // over the whole remaining buffer, exactly the old
                    // no-VAD path.
                    std::cerr << "warning: VAD worker failed (" << vad_error_
                              << "); decoding final without segment boundaries\n";
                }
                if (audio_.empty()) {
                    lock.unlock();
                    if (!emitted_utterance_) {
                        emit_result(SenseVoiceResult{}, true);
                    }
                    return;
                }
                final_result = true;
                snapshot_audio_samples = audio_.size();
                snapshot = audio_;
                if (got_vad && vad_result_.has_value() &&
                    vad_result_->generation == audio_generation_) {
                    last_vad_revision_consumed_ = vad_result_->revision;
                    vad_work = *vad_result_;
                } else if (!got_vad && vad_error_.empty()) {
                    std::cerr << "warning: final VAD pass timed out; "
                                 "decoding without segment boundaries\n";
                }
            } else {
                if (!vad_error_.empty()) {
                    // During a live session there is no owner left for
                    // segmentation, so surface the failure instead of
                    // silently degrading endpoint detection.
                    const std::string error = vad_error_;
                    lock.unlock();
                    emit_error(error);
                    return;
                }
                // wait_until can return on a notify while the predicate is
                // still false (fresh VAD not published yet, or the pass saw
                // too little audio). Re-check before decoding.
                if (!fresh_vad_available() ||
                    vad_result_->audio_samples < minimum_samples ||
                    vad_result_->audio_samples - last_decoded_samples_ <
                        minimum_new_samples) {
                    next_partial = clock::now() + std::chrono::milliseconds(20);
                    continue;
                }
                // Decode exactly the prefix the VAD pass saw. This keeps
                // segment times and the endpoint decision consistent, the
                // same pairing the serial path gets from one shared snapshot.
                snapshot_audio_samples =
                    std::min(audio_.size(), vad_result_->audio_samples);
                snapshot.assign(
                    audio_.begin(), audio_.begin() + snapshot_audio_samples);
                last_vad_revision_consumed_ = vad_result_->revision;
                vad_work = *vad_result_;
                next_partial = clock::now() +
                    std::chrono::milliseconds(std::max(50, config_.partial_interval_ms));
            }
        }

        if (snapshot.empty()) {
            if (final_result) {
                if (!emitted_utterance_) {
                    emit_result(SenseVoiceResult{}, true);
                }
                return;
            }
            continue;
        }

        try {
            const VadResult* vad_result = nullptr;
            if (vad_work.has_value()) {
                last_vad_telemetry_ = vad_work->result;
                vad_result = &vad_work->result;
            }

            const std::size_t budget_samples = maximum_audio_samples(config_);
            const bool over_budget = snapshot.size() > budget_samples;
            const bool over_memory = process_memory_over_limit(
                config_.memory_limit_mb,
                baseline_working_set_bytes_);
            if (over_budget || (!final_result && over_memory)) {
                std::size_t split_samples = std::min(budget_samples, snapshot.size());
                if (vad_result != nullptr) {
                    std::size_t nearest_silence = 0;
                    for (const SpeechSegment& segment : vad_result->segments) {
                        const std::size_t end = std::min(
                            snapshot.size(),
                            samples_for_ms(std::max(0, segment.end_ms)));
                        if (end > 0 && end <= budget_samples) {
                            nearest_silence = std::max(nearest_silence, end);
                        }
                    }
                    if (nearest_silence >= samples_for_ms(1'000)) {
                        split_samples = nearest_silence;
                    }
                }
                split_samples = std::min(split_samples, snapshot.size());
                const std::size_t minimum_split_samples = over_memory && !over_budget
                    ? samples_for_ms(3'000)
                    : samples_for_ms(1'000);
                if (split_samples >= minimum_split_samples) {
                    SenseVoiceResult result = recognize_audio(
                        std::span<const float>(snapshot.data(), split_samples));
                    result.audio_ms = static_cast<int>(split_samples / 16);
                    remember_result(result, split_samples);
                    forced_split_ = true;
                    emit_result(result, true);
                    emitted_utterance_ = true;
                    stability_.reset();
                    consume_audio_prefix(split_samples, true);
                    next_partial = clock::now();
                    continue;
                }
            }

            if (!final_result && vad_result != nullptr) {
                const auto endpoint = std::find_if(
                    vad_result->segments.begin(),
                    vad_result->segments.end(),
                    [](const SpeechSegment& segment) { return segment.finalized; });
                if (endpoint != vad_result->segments.end()) {
                    SenseVoiceResult result = recognize_snapshot(snapshot, vad_result, &*endpoint);
                    result.audio_ms = static_cast<int>(snapshot_audio_samples / 16);
                    remember_result(result, snapshot.size());
                    emit_result(result, true);
                    emitted_utterance_ = true;
                    stability_.reset();
                    const auto next_segment = std::next(endpoint);
                    const std::size_t consumed_samples = next_segment == vad_result->segments.end()
                        ? snapshot.size()
                        : samples_for_ms(next_segment->start_ms);
                    consume_audio_prefix(
                        consumed_samples,
                        next_segment != vad_result->segments.end());
                    next_partial = clock::now();
                    continue;
                }
            }

            SenseVoiceResult result;
            if (final_result && snapshot.size() == last_decoded_samples_) {
                result.text = last_text_;
                result.audio_ms = static_cast<int>(snapshot.size() / 16);
                result.vad_ms = last_vad_ms_;
                result.inference_ms = last_inference_ms_;
                result.hotword_bias_applied = last_hotword_bias_applied_;
            } else if (final_result && vad_result == nullptr) {
                // Bounded-wait fallback: decode the whole buffer. Calling
                // recognize_snapshot here would run a second VAD pass on the
                // recognizer thread while the VAD worker may still own the
                // engine.
                result = recognize_audio(snapshot);
            } else {
                result = recognize_snapshot(snapshot, vad_result);
            }
            result.audio_ms = static_cast<int>(snapshot_audio_samples / 16);
            remember_result(result, snapshot.size());
            if (!final_result || !result.text.empty() || !emitted_utterance_) {
                emit_result(result, final_result);
            }
            if (!final_result && vad_result != nullptr &&
                vad_result->segments.empty() &&
                snapshot.size() > samples_for_ms(config_.silence_preroll_ms)) {
                consume_audio_prefix(
                    snapshot.size() - samples_for_ms(config_.silence_preroll_ms),
                    false);
            }
        } catch (const std::exception& exception) {
            emit_error(exception.what());
            return;
        }

        if (final_result) {
            return;
        }
    }
}

SenseVoiceResult StreamRecognizer::recognize_snapshot(
    std::span<const float> snapshot,
    const VadResult* detected_vad,
    const SpeechSegment* only_segment) {
    if (vad_ == nullptr) {
        return recognize_audio(snapshot);
    }

    SenseVoiceResult combined;
    combined.audio_ms = static_cast<int>(snapshot.size() / 16);
    VadResult local_vad;
    if (detected_vad == nullptr) {
        local_vad = vad_->detect(
            snapshot,
            VadDetectionOptions{
                .maximum_segment_ms = std::max(config_.maximum_utterance_ms, 1'000),
                .endpoint_silence_ms = config_.endpoint_silence_ms,
                .speech_noise_threshold = config_.vad_speech_threshold,
                .minimum_db = config_.vad_minimum_db,
                .minimum_snr_db = config_.vad_minimum_snr_db,
                .minimum_speech_ms = config_.vad_minimum_speech_ms,
            });
        detected_vad = &local_vad;
    }
    combined.vad_ms = detected_vad->inference_ms;
    const std::span<const SpeechSegment> segments = only_segment != nullptr
        ? std::span<const SpeechSegment>(only_segment, 1)
        : std::span<const SpeechSegment>(detected_vad->segments);
    for (const SpeechSegment& segment : segments) {
        const std::size_t start = std::min(
            snapshot.size(), static_cast<std::size_t>(std::max(0, segment.start_ms)) * 16);
        const std::size_t end = std::min(
            snapshot.size(), static_cast<std::size_t>(std::max(0, segment.end_ms)) * 16);
        if (end <= start || end - start < 400) {
            continue;
        }
        SenseVoiceResult current = recognize_audio(snapshot.subspan(start, end - start));
        if (!combined.text.empty() && !current.text.empty()) {
            const unsigned char left = static_cast<unsigned char>(combined.text.back());
            const unsigned char right = static_cast<unsigned char>(current.text.front());
            if (std::isalnum(left) != 0 && std::isalnum(right) != 0) {
                combined.text.push_back(' ');
            }
        }
        combined.text += current.text;
        combined.inference_ms += current.inference_ms;
        combined.hotword_bias_applied = combined.hotword_bias_applied || current.hotword_bias_applied;
    }
    return combined;
}

SenseVoiceResult StreamRecognizer::recognize_audio(std::span<const float> audio) {
    if (text_processor_ == nullptr || config_.hotword_boost <= 0.0F) {
        return engine_.recognize(audio);
    }
    std::vector<HotwordBoostPhrase> hotwords;
    for (const HotwordEntry& entry : text_processor_->hotwords()) {
        if (entry.enabled && entry.boost > 0.0F) {
            hotwords.push_back({
                entry.phrase,
                std::clamp(entry.boost * config_.hotword_boost / 3.0F, 0.0F, 12.0F),
            });
        }
    }
    return engine_.recognize(audio, hotwords);
}

void StreamRecognizer::consume_audio_prefix(
    std::size_t samples,
    bool reprocess_remaining) {
    {
        std::scoped_lock lock(mutex_);
        const bool has_unprocessed_audio = audio_.size() > last_decoded_samples_;
        samples = std::min(samples, audio_.size());
        audio_.erase(audio_.begin(), audio_.begin() + samples);
        // Segment times are relative to the buffer start, so the next VAD pass
        // must start from a new generation. Any in-flight result is discarded
        // when it arrives and the worker re-evaluates the trimmed buffer.
        ++audio_revision_;
        ++audio_generation_;
        vad_result_.reset();
        vad_requested_ = true;
        dirty_ = has_unprocessed_audio || reprocess_remaining;
        last_decoded_samples_ = 0;
        last_text_.clear();
        last_vad_ms_ = 0;
        last_inference_ms_ = 0;
        last_hotword_bias_applied_ = false;
    }
    vad_condition_.notify_one();
}

void StreamRecognizer::remember_result(
    const SenseVoiceResult& result,
    std::size_t decoded_samples) {
    last_decoded_samples_ = decoded_samples;
    last_text_ = result.text;
    last_vad_ms_ = result.vad_ms;
    last_inference_ms_ = result.inference_ms;
    last_hotword_bias_applied_ = result.hotword_bias_applied;
}

void StreamRecognizer::emit_result(const SenseVoiceResult& result, bool final_result) {
    TextProcessResult processed;
    const std::string* display_text = &result.text;
    if (text_processor_ != nullptr) {
        processed = text_processor_->process(result.text, final_result);
        display_text = &processed.text;
    }
    const StableText text = final_result
        ? stability_.finalize(*display_text)
        : stability_.update(*display_text);
    RecognitionEvent event;
    event.kind = final_result ? RecognitionEventKind::Final : RecognitionEventKind::Partial;
    event.sequence = ++sequence_;
    event.audio_ms = result.audio_ms;
    event.vad_ms = result.vad_ms;
    event.inference_ms = result.inference_ms;
    if (final_result) {
        const std::int64_t accepted = last_accept_ns_.load(std::memory_order_acquire);
        if (accepted != 0) {
            const std::int64_t latency_ns = steady_now_ns() - accepted;
            event.final_latency_ms = static_cast<int>(
                std::max<std::int64_t>(0, latency_ns / 1'000'000));
        }
    }
    event.vad_current_db = last_vad_telemetry_.current_db;
    event.vad_peak_db = last_vad_telemetry_.peak_db;
    event.vad_noise_floor_db = last_vad_telemetry_.noise_floor_db;
    event.vad_required_db = last_vad_telemetry_.required_db;
    event.vad_speech_probability = last_vad_telemetry_.speech_probability;
    event.vad_activity = last_vad_telemetry_.activity;
    event.text = *display_text;
    event.stable = text.stable;
    event.unstable = text.unstable;
    event.stability_conflict = text.conflict;
    event.forced_split = final_result && forced_split_;
    event.hotword_bias_applied = result.hotword_bias_applied;
    if (final_result) {
        forced_split_ = false;
    }
    if (text_processor_ != nullptr) {
        event.matched_hotwords = std::move(processed.matched_hotwords);
        event.tokens = std::move(processed.tokens);
    }
    handler_(event);
}

void StreamRecognizer::emit_error(const std::string& error) {
    RecognitionEvent event;
    event.kind = RecognitionEventKind::Error;
    event.sequence = ++sequence_;
    event.error = error;
    handler_(event);
}
