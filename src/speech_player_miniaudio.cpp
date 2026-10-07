// SpeechPlayer for platforms without WASAPI. miniaudio's implementation is
// compiled once in audio_io.cpp; this file only uses its API.

#include "speech_player.h"

#include "miniaudio.h"

#include <QString>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

namespace {

std::wstring device_key(const ma_device_info& info) {
#if defined(__APPLE__)
    return QString::fromUtf8(info.id.coreaudio).toStdWString();
#else
    return QString::fromUtf8(info.name).toStdWString();
#endif
}

class Context {
public:
    Context() { ok_ = ma_context_init(nullptr, 0, nullptr, &context_) == MA_SUCCESS; }
    ~Context() {
        if (ok_) ma_context_uninit(&context_);
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    [[nodiscard]] bool ok() const { return ok_; }
    ma_context* get() { return &context_; }

    std::optional<ma_device_id> find_playback(const std::wstring& key) {
        ma_device_info* playback = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(&context_, &playback, &count, nullptr, nullptr) != MA_SUCCESS) {
            return std::nullopt;
        }
        for (ma_uint32 i = 0; i < count; ++i) {
            if (device_key(playback[i]) == key) return playback[i].id;
        }
        return std::nullopt;
    }

private:
    ma_context context_{};
    bool ok_ = false;
};

} // namespace

std::vector<AudioOutputDevice> list_audio_output_devices() {
    std::vector<AudioOutputDevice> devices;
    Context context;
    if (!context.ok()) return devices;
    ma_device_info* playback = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(context.get(), &playback, &count, nullptr, nullptr) != MA_SUCCESS) {
        return devices;
    }
    for (ma_uint32 i = 0; i < count; ++i) {
        devices.push_back({
            .id = device_key(playback[i]),
            .name = QString::fromUtf8(playback[i].name).toStdWString(),
        });
    }
    return devices;
}

struct SpeechPlayer::Impl {
    struct Clip {
        std::vector<float> samples;
        int sample_rate = 0;
        std::uint64_t generation = 0;
    };

    std::wstring device_id;
    ClipCallback on_clip_done;
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<Clip> clips;
    std::uint64_t generation = 0;
    bool stopping = false;
    std::thread thread;

    // Shared with the audio callback while a clip plays.
    const float* playing = nullptr;
    std::size_t playing_count = 0;
    std::atomic<std::size_t> cursor{0};
    std::atomic<bool> drained{false};

    bool cancelled(std::uint64_t clip_generation) {
        std::lock_guard lock(mutex);
        return stopping || clip_generation != generation;
    }

    static void data_callback(ma_device* device, void* output, const void*, ma_uint32 frames) {
        auto* self = static_cast<Impl*>(device->pUserData);
        auto* out = static_cast<float*>(output);
        const std::size_t position = self->cursor.load(std::memory_order_relaxed);
        const std::size_t available = self->playing_count - std::min(position, self->playing_count);
        const std::size_t copied = std::min<std::size_t>(available, frames);
        std::memcpy(out, self->playing + position, copied * sizeof(float));
        std::memset(out + copied, 0, (frames - copied) * sizeof(float));
        self->cursor.store(position + copied, std::memory_order_relaxed);
        if (copied < frames) self->drained.store(true, std::memory_order_release);
    }

    void run() {
        for (;;) {
            Clip clip;
            {
                std::unique_lock lock(mutex);
                condition.wait(lock, [this] { return stopping || !clips.empty(); });
                if (stopping) return;
                clip = std::move(clips.front());
                clips.pop_front();
            }
            std::string error;
            const bool ok = playClip(clip, error);
            if (!cancelled(clip.generation) && on_clip_done) on_clip_done(ok, error);
        }
    }

    bool playClip(const Clip& clip, std::string& error) {
        if (clip.samples.empty() || clip.sample_rate <= 0) return true;
        Context context;
        if (!context.ok()) {
            error = "failed to initialize audio output";
            return false;
        }
        std::optional<ma_device_id> id;
        if (!device_id.empty()) {
            id = context.find_playback(device_id);
            if (!id) {
                error = "playback device is not available";
                return false;
            }
        }
        playing = clip.samples.data();
        playing_count = clip.samples.size();
        cursor.store(0, std::memory_order_relaxed);
        drained.store(false, std::memory_order_relaxed);

        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.pDeviceID = id ? &*id : nullptr;
        config.playback.format = ma_format_f32;
        config.playback.channels = 1;
        config.sampleRate = static_cast<ma_uint32>(clip.sample_rate);
        config.dataCallback = data_callback;
        config.pUserData = this;
        ma_device device{};
        if (ma_device_init(context.get(), &config, &device) != MA_SUCCESS) {
            error = "failed to open playback device";
            return false;
        }
        if (ma_device_start(&device) != MA_SUCCESS) {
            ma_device_uninit(&device);
            error = "failed to start playback";
            return false;
        }
        while (!drained.load(std::memory_order_acquire) && !cancelled(clip.generation)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        // Let the device flush its last period before closing it.
        if (!cancelled(clip.generation)) std::this_thread::sleep_for(std::chrono::milliseconds(60));
        ma_device_uninit(&device);
        playing = nullptr;
        playing_count = 0;
        return true;
    }
};

SpeechPlayer::SpeechPlayer(std::wstring device_id, ClipCallback on_clip_done)
    : impl_(std::make_unique<Impl>()) {
    impl_->device_id = std::move(device_id);
    impl_->on_clip_done = std::move(on_clip_done);
    impl_->thread = std::thread([impl = impl_.get()] { impl->run(); });
}

SpeechPlayer::~SpeechPlayer() {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
        impl_->clips.clear();
    }
    impl_->condition.notify_all();
    impl_->thread.join();
}

void SpeechPlayer::play(std::vector<float> samples, int sample_rate) {
    {
        std::lock_guard lock(impl_->mutex);
        impl_->clips.push_back(Impl::Clip{std::move(samples), sample_rate, impl_->generation});
    }
    impl_->condition.notify_one();
}

void SpeechPlayer::clear() {
    std::lock_guard lock(impl_->mutex);
    ++impl_->generation;
    impl_->clips.clear();
}
