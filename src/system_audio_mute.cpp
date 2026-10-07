#include "system_audio_mute.h"

#ifdef _WIN32

#include <windows.h>

#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <objbase.h>
#include <wrl/client.h>

#include <sstream>

namespace {

using Microsoft::WRL::ComPtr;

std::string hresult_message(HRESULT result) {
    std::ostringstream message;
    message << "Windows audio mute failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(result) << ')';
    return message.str();
}

} // namespace

struct SystemAudioMute::Impl {
    ComPtr<IAudioEndpointVolume> endpoint;
    bool was_muted = false;
    bool changed = false;
    bool apartment_owned = false;
};

SystemAudioMute::SystemAudioMute() : impl_(std::make_unique<Impl>()) {}

SystemAudioMute::~SystemAudioMute() {
    restore();
}

bool SystemAudioMute::mute(std::string& error) {
    restore();

    const HRESULT initialize = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = initialize == S_OK || initialize == S_FALSE;
    if (FAILED(initialize) && initialize != RPC_E_CHANGED_MODE) {
        error = hresult_message(initialize);
        return false;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    HRESULT result = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(enumerator.ReleaseAndGetAddressOf()));
    if (SUCCEEDED(result)) {
        result = enumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, device.ReleaseAndGetAddressOf());
    }
    if (SUCCEEDED(result)) {
        result = device->Activate(
            __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(impl_->endpoint.ReleaseAndGetAddressOf()));
    }
    if (FAILED(result)) {
        if (uninitialize) CoUninitialize();
        error = hresult_message(result);
        return false;
    }

    BOOL current_mute = FALSE;
    result = impl_->endpoint->GetMute(&current_mute);
    if (SUCCEEDED(result)) {
        impl_->was_muted = current_mute != FALSE;
        if (!impl_->was_muted) {
            result = impl_->endpoint->SetMute(TRUE, nullptr);
            impl_->changed = SUCCEEDED(result);
        }
    }
    if (FAILED(result)) {
        impl_->endpoint.Reset();
        if (uninitialize) CoUninitialize();
        error = hresult_message(result);
        return false;
    }
    impl_->apartment_owned = uninitialize;
    return true;
}

void SystemAudioMute::restore() {
    if (impl_ == nullptr) return;
    if (impl_->endpoint != nullptr && impl_->changed) {
        impl_->endpoint->SetMute(impl_->was_muted ? TRUE : FALSE, nullptr);
    }
    impl_->endpoint.Reset();
    impl_->changed = false;
    impl_->was_muted = false;
    if (impl_->apartment_owned) {
        CoUninitialize();
        impl_->apartment_owned = false;
    }
}

bool SystemAudioMute::active() const {
    return impl_ != nullptr && impl_->changed;
}

#elif defined(__APPLE__)

#include <CoreAudio/CoreAudio.h>

namespace {

AudioObjectPropertyAddress mute_address() {
    return {kAudioDevicePropertyMute, kAudioDevicePropertyScopeOutput,
            kAudioObjectPropertyElementMain};
}

} // namespace

struct SystemAudioMute::Impl {
    AudioObjectID device = kAudioObjectUnknown;
    bool changed = false;
};

SystemAudioMute::SystemAudioMute() : impl_(std::make_unique<Impl>()) {}

SystemAudioMute::~SystemAudioMute() {
    restore();
}

bool SystemAudioMute::mute(std::string& error) {
    restore();
    AudioObjectPropertyAddress default_output{
        kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain};
    AudioObjectID device = kAudioObjectUnknown;
    UInt32 size = sizeof(device);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &default_output, 0, nullptr,
                                   &size, &device) != noErr ||
        device == kAudioObjectUnknown) {
        error = "no default output device";
        return false;
    }
    const AudioObjectPropertyAddress address = mute_address();
    Boolean settable = false;
    if (!AudioObjectHasProperty(device, &address) ||
        AudioObjectIsPropertySettable(device, &address, &settable) != noErr || !settable) {
        error = "the default output device cannot be muted";
        return false;
    }
    UInt32 muted = 0;
    size = sizeof(muted);
    if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &muted) != noErr) {
        error = "failed to read output mute state";
        return false;
    }
    if (muted != 0) return true;
    muted = 1;
    if (AudioObjectSetPropertyData(device, &address, 0, nullptr, sizeof(muted), &muted) != noErr) {
        error = "failed to mute output";
        return false;
    }
    impl_->device = device;
    impl_->changed = true;
    return true;
}

void SystemAudioMute::restore() {
    if (impl_ == nullptr || !impl_->changed) return;
    const AudioObjectPropertyAddress address = mute_address();
    UInt32 muted = 0;
    AudioObjectSetPropertyData(impl_->device, &address, 0, nullptr, sizeof(muted), &muted);
    impl_->changed = false;
    impl_->device = kAudioObjectUnknown;
}

bool SystemAudioMute::active() const {
    return impl_ != nullptr && impl_->changed;
}

#else

struct SystemAudioMute::Impl {};
SystemAudioMute::SystemAudioMute() : impl_(std::make_unique<Impl>()) {}
SystemAudioMute::~SystemAudioMute() = default;
bool SystemAudioMute::mute(std::string& error) {
    error = "system playback mute is not supported on this platform";
    return false;
}
void SystemAudioMute::restore() {}
bool SystemAudioMute::active() const { return false; }

#endif
