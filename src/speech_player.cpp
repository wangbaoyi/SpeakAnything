#include "speech_player.h"

#include <windows.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <propkey.h>
#include <propvarutil.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>

namespace {

using Microsoft::WRL::ComPtr;

std::string hresult_message(const char* what, HRESULT result) {
    std::ostringstream message;
    message << what << " (HRESULT 0x" << std::hex << static_cast<unsigned long>(result) << ')';
    return message.str();
}

class ComApartment {
public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) CoUninitialize();
    }

private:
    HRESULT result_;
};

} // namespace

std::vector<AudioOutputDevice> list_audio_output_devices() {
    std::vector<AudioOutputDevice> devices;
    const ComApartment apartment;
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.GetAddressOf()))) ||
        FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, collection.GetAddressOf()))) {
        return devices;
    }
    UINT count = 0;
    collection->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IPropertyStore> properties;
        LPWSTR id = nullptr;
        if (FAILED(collection->Item(i, device.GetAddressOf())) || FAILED(device->GetId(&id))) continue;
        AudioOutputDevice entry{.id = id, .name = id};
        CoTaskMemFree(id);
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) && name.vt == VT_LPWSTR) {
                entry.name = name.pwszVal;
            }
            PropVariantClear(&name);
        }
        devices.push_back(std::move(entry));
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

    bool cancelled(std::uint64_t clip_generation) {
        std::lock_guard lock(mutex);
        return stopping || clip_generation != generation;
    }

    void run() {
        const ComApartment apartment;
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
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> render;
        HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                          IID_PPV_ARGS(enumerator.GetAddressOf()));
        if (SUCCEEDED(result)) {
            result = device_id.empty()
                ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.GetAddressOf())
                : enumerator->GetDevice(device_id.c_str(), device.GetAddressOf());
        }
        if (FAILED(result)) {
            error = hresult_message("playback device unavailable", result);
            return false;
        }
        DWORD state = 0;
        if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE) {
            error = "playback device unavailable";
            return false;
        }
        result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.GetAddressOf()));
        // Hand Windows the clip's own format; the shared-mode engine converts it.
        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 1;
        format.nSamplesPerSec = static_cast<DWORD>(clip.sample_rate);
        format.wBitsPerSample = 32;
        format.nBlockAlign = sizeof(float);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        constexpr REFERENCE_TIME buffer_duration = 2'000'000; // 200 ms
        if (SUCCEEDED(result)) {
            result = client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                buffer_duration, 0, &format, nullptr);
        }
        UINT32 buffer_frames = 0;
        if (SUCCEEDED(result)) result = client->GetBufferSize(&buffer_frames);
        if (SUCCEEDED(result)) {
            result = client->GetService(IID_PPV_ARGS(render.GetAddressOf()));
        }
        if (FAILED(result)) {
            error = hresult_message("cannot open playback device", result);
            return false;
        }

        std::size_t position = 0;
        bool started = false;
        while (!cancelled(clip.generation)) {
            UINT32 padding = 0;
            if (FAILED(result = client->GetCurrentPadding(&padding))) break;
            if (position >= clip.samples.size()) {
                if (padding == 0) break;
                Sleep(10);
                continue;
            }
            const UINT32 room = buffer_frames - padding;
            const auto frames = static_cast<UINT32>(
                std::min<std::size_t>(room, clip.samples.size() - position));
            if (frames > 0) {
                BYTE* data = nullptr;
                if (FAILED(result = render->GetBuffer(frames, &data))) break;
                std::memcpy(data, clip.samples.data() + position, frames * sizeof(float));
                if (FAILED(result = render->ReleaseBuffer(frames, 0))) break;
                position += frames;
            }
            if (!started) {
                if (FAILED(result = client->Start())) break;
                started = true;
            }
            Sleep(10);
        }
        if (started) client->Stop();
        if (FAILED(result)) {
            error = hresult_message("playback failed", result);
            return false;
        }
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
