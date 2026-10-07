// SpeakAnything Virtual Mic: a CoreAudio AudioServerPlugIn that publishes one
// loopback device. Audio written to its output stream comes back out of its
// input stream, so call apps can pick it as a microphone while SpeakAnything
// plays synthesized speech into it. It replaces VB-Cable on macOS.
//
// The structure follows Apple's NullAudio sample driver: one plug-in object,
// one device, one input stream and one output stream, no controls. Runs inside
// coreaudiod's sandboxed driver host, so it touches nothing but its own memory.

#include <CoreAudio/AudioServerPlugIn.h>
#include <mach/mach_time.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {

enum : AudioObjectID {
    kObjectID_PlugIn = kAudioObjectPlugInObject,
    kObjectID_Device = 2,
    kObjectID_Stream_Input = 3,
    kObjectID_Stream_Output = 4,
};

constexpr Float64 kSampleRate = 48000.0;
constexpr UInt32 kChannels = 2;
constexpr UInt32 kBytesPerFrame = kChannels * sizeof(Float32);
// Zero time stamps advance once per ring cycle; it also bounds the latency
// between what is written and what can still be read back.
constexpr UInt32 kRingFrames = 16384;
constexpr UInt32 kLatencyFrames = 0;
constexpr UInt32 kSafetyOffsetFrames = 0;

constexpr const char* kDeviceUID = "org.speakanything.virtualmic.device";
constexpr const char* kModelUID = "org.speakanything.virtualmic.model";
constexpr const char* kDeviceName = "SpeakAnything Virtual Mic";
constexpr const char* kManufacturer = "SpeakAnything";

AudioServerPlugInHostRef g_host = nullptr;
std::atomic<UInt32> g_ref_count{0};
std::mutex g_state_mutex;
std::mutex g_io_mutex;
UInt64 g_io_clients = 0;
Float64 g_host_ticks_per_frame = 0.0;
UInt64 g_anchor_host_time = 0;
UInt64 g_number_time_stamps = 0;
Float32 g_ring[kRingFrames * kChannels];

CFStringRef cfstring(const char* text) {
    return CFStringCreateWithCString(nullptr, text, kCFStringEncodingUTF8);
}

AudioStreamBasicDescription stream_format() {
    AudioStreamBasicDescription format{};
    format.mSampleRate = kSampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagsNativeEndian |
        kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = kBytesPerFrame;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = kBytesPerFrame;
    format.mChannelsPerFrame = kChannels;
    format.mBitsPerChannel = 32;
    return format;
}

bool is_stream(AudioObjectID object) {
    return object == kObjectID_Stream_Input || object == kObjectID_Stream_Output;
}

template <typename T>
OSStatus write_value(const T& value, UInt32 in_size, UInt32* out_size, void* out_data) {
    if (in_size < sizeof(T)) return kAudioHardwareBadPropertySizeError;
    *static_cast<T*>(out_data) = value;
    *out_size = sizeof(T);
    return kAudioHardwareNoError;
}

// ---- COM-style plumbing -------------------------------------------------

HRESULT QueryInterface(void* driver, REFIID iid, LPVOID* out);
ULONG AddRef(void* driver);
ULONG Release(void* driver);
OSStatus Initialize(AudioServerPlugInDriverRef, AudioServerPlugInHostRef host);
OSStatus CreateDevice(AudioServerPlugInDriverRef, CFDictionaryRef, const AudioServerPlugInClientInfo*, AudioObjectID*) {
    return kAudioHardwareUnsupportedOperationError;
}
OSStatus DestroyDevice(AudioServerPlugInDriverRef, AudioObjectID) {
    return kAudioHardwareUnsupportedOperationError;
}
OSStatus AddDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*) {
    return kAudioHardwareNoError;
}
OSStatus RemoveDeviceClient(AudioServerPlugInDriverRef, AudioObjectID, const AudioServerPlugInClientInfo*) {
    return kAudioHardwareNoError;
}
OSStatus PerformDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*) {
    return kAudioHardwareNoError;
}
OSStatus AbortDeviceConfigurationChange(AudioServerPlugInDriverRef, AudioObjectID, UInt64, void*) {
    return kAudioHardwareNoError;
}
Boolean HasProperty(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*);
OSStatus IsPropertySettable(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, Boolean*);
OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32*);
OSStatus GetPropertyData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, UInt32*, void*);
OSStatus SetPropertyData(AudioServerPlugInDriverRef, AudioObjectID, pid_t, const AudioObjectPropertyAddress*, UInt32, const void*, UInt32, const void*) {
    return kAudioHardwareUnsupportedOperationError;
}
OSStatus StartIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
OSStatus StopIO(AudioServerPlugInDriverRef, AudioObjectID, UInt32);
OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef, AudioObjectID, UInt32, Float64*, UInt64*, UInt64*);
OSStatus WillDoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, Boolean*, Boolean*);
OSStatus BeginIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*) {
    return kAudioHardwareNoError;
}
OSStatus DoIOOperation(AudioServerPlugInDriverRef, AudioObjectID, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*, void*, void*);
OSStatus EndIOOperation(AudioServerPlugInDriverRef, AudioObjectID, UInt32, UInt32, UInt32, const AudioServerPlugInIOCycleInfo*) {
    return kAudioHardwareNoError;
}

AudioServerPlugInDriverInterface g_interface = {
    nullptr,
    QueryInterface,
    AddRef,
    Release,
    Initialize,
    CreateDevice,
    DestroyDevice,
    AddDeviceClient,
    RemoveDeviceClient,
    PerformDeviceConfigurationChange,
    AbortDeviceConfigurationChange,
    HasProperty,
    IsPropertySettable,
    GetPropertyDataSize,
    GetPropertyData,
    SetPropertyData,
    StartIO,
    StopIO,
    GetZeroTimeStamp,
    WillDoIOOperation,
    BeginIOOperation,
    DoIOOperation,
    EndIOOperation,
};
AudioServerPlugInDriverInterface* g_interface_ptr = &g_interface;
AudioServerPlugInDriverRef g_driver = &g_interface_ptr;

HRESULT QueryInterface(void* driver, REFIID iid, LPVOID* out) {
    if (driver != g_driver || out == nullptr) return kAudioHardwareBadObjectError;
    CFUUIDRef requested = CFUUIDCreateFromUUIDBytes(nullptr, iid);
    const bool supported = CFEqual(requested, IUnknownUUID) ||
        CFEqual(requested, kAudioServerPlugInDriverInterfaceUUID);
    CFRelease(requested);
    if (!supported) return E_NOINTERFACE;
    g_ref_count.fetch_add(1);
    *out = g_driver;
    return S_OK;
}

ULONG AddRef(void* driver) {
    if (driver != g_driver) return 0;
    return g_ref_count.fetch_add(1) + 1;
}

ULONG Release(void* driver) {
    if (driver != g_driver) return 0;
    const UInt32 current = g_ref_count.load();
    if (current == 0) return 0;
    return g_ref_count.fetch_sub(1) - 1;
}

OSStatus Initialize(AudioServerPlugInDriverRef driver, AudioServerPlugInHostRef host) {
    if (driver != g_driver) return kAudioHardwareBadObjectError;
    g_host = host;
    mach_timebase_info_data_t timebase{};
    mach_timebase_info(&timebase);
    const Float64 host_clock_frequency =
        static_cast<Float64>(timebase.denom) / timebase.numer * 1'000'000'000.0;
    g_host_ticks_per_frame = host_clock_frequency / kSampleRate;
    return kAudioHardwareNoError;
}

// ---- Properties ---------------------------------------------------------

Boolean HasProperty(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t,
                    const AudioObjectPropertyAddress* address) {
    if (driver != g_driver || address == nullptr) return false;
    UInt32 size = 0;
    return GetPropertyDataSize(driver, object, 0, address, 0, nullptr, &size) ==
        kAudioHardwareNoError;
}

OSStatus IsPropertySettable(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t,
                            const AudioObjectPropertyAddress* address, Boolean* settable) {
    if (driver != g_driver || address == nullptr || settable == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    if (!HasProperty(driver, object, 0, address)) return kAudioHardwareUnknownPropertyError;
    *settable = false;
    return kAudioHardwareNoError;
}

OSStatus GetPropertyDataSize(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t,
                             const AudioObjectPropertyAddress* address, UInt32, const void*,
                             UInt32* out_size) {
    if (driver != g_driver || address == nullptr || out_size == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    const AudioObjectPropertySelector selector = address->mSelector;
    if (object == kObjectID_PlugIn) {
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass: *out_size = sizeof(AudioClassID); return 0;
        case kAudioObjectPropertyOwner: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioObjectPropertyManufacturer: *out_size = sizeof(CFStringRef); return 0;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioPlugInPropertyTranslateUIDToDevice: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioPlugInPropertyResourceBundle: *out_size = sizeof(CFStringRef); return 0;
        default: return kAudioHardwareUnknownPropertyError;
        }
    }
    if (object == kObjectID_Device) {
        const bool input = address->mScope == kAudioObjectPropertyScopeInput;
        const bool output = address->mScope == kAudioObjectPropertyScopeOutput;
        const UInt32 streams = (input || output) ? 1 : 2;
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass: *out_size = sizeof(AudioClassID); return 0;
        case kAudioObjectPropertyOwner: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioObjectPropertyName:
        case kAudioObjectPropertyManufacturer:
        case kAudioDevicePropertyDeviceUID:
        case kAudioDevicePropertyModelUID: *out_size = sizeof(CFStringRef); return 0;
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: *out_size = streams * sizeof(AudioObjectID); return 0;
        case kAudioObjectPropertyControlList: *out_size = 0; return 0;
        case kAudioDevicePropertyTransportType:
        case kAudioDevicePropertyClockDomain:
        case kAudioDevicePropertyDeviceIsAlive:
        case kAudioDevicePropertyDeviceIsRunning:
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
        case kAudioDevicePropertyLatency:
        case kAudioDevicePropertySafetyOffset:
        case kAudioDevicePropertyZeroTimeStampPeriod:
        case kAudioDevicePropertyIsHidden: *out_size = sizeof(UInt32); return 0;
        case kAudioDevicePropertyRelatedDevices: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioDevicePropertyNominalSampleRate: *out_size = sizeof(Float64); return 0;
        case kAudioDevicePropertyAvailableNominalSampleRates: *out_size = sizeof(AudioValueRange); return 0;
        case kAudioDevicePropertyPreferredChannelsForStereo: *out_size = 2 * sizeof(UInt32); return 0;
        default: return kAudioHardwareUnknownPropertyError;
        }
    }
    if (is_stream(object)) {
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
        case kAudioObjectPropertyClass: *out_size = sizeof(AudioClassID); return 0;
        case kAudioObjectPropertyOwner: *out_size = sizeof(AudioObjectID); return 0;
        case kAudioObjectPropertyOwnedObjects: *out_size = 0; return 0;
        case kAudioStreamPropertyIsActive:
        case kAudioStreamPropertyDirection:
        case kAudioStreamPropertyTerminalType:
        case kAudioStreamPropertyStartingChannel:
        case kAudioStreamPropertyLatency: *out_size = sizeof(UInt32); return 0;
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat: *out_size = sizeof(AudioStreamBasicDescription); return 0;
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats:
            *out_size = sizeof(AudioStreamRangedDescription);
            return 0;
        default: return kAudioHardwareUnknownPropertyError;
        }
    }
    return kAudioHardwareBadObjectError;
}

OSStatus GetPropertyData(AudioServerPlugInDriverRef driver, AudioObjectID object, pid_t,
                         const AudioObjectPropertyAddress* address, UInt32 qualifier_size,
                         const void* qualifier, UInt32 in_size, UInt32* out_size, void* out_data) {
    if (driver != g_driver || address == nullptr || out_size == nullptr || out_data == nullptr) {
        return kAudioHardwareIllegalOperationError;
    }
    const AudioObjectPropertySelector selector = address->mSelector;

    if (object == kObjectID_PlugIn) {
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
            return write_value<AudioClassID>(kAudioObjectClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyClass:
            return write_value<AudioClassID>(kAudioPlugInClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyOwner:
            return write_value<AudioObjectID>(kAudioObjectUnknown, in_size, out_size, out_data);
        case kAudioObjectPropertyManufacturer:
            return write_value<CFStringRef>(cfstring(kManufacturer), in_size, out_size, out_data);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioPlugInPropertyDeviceList:
            if (in_size < sizeof(AudioObjectID)) { *out_size = 0; return 0; }
            return write_value<AudioObjectID>(kObjectID_Device, in_size, out_size, out_data);
        case kAudioPlugInPropertyTranslateUIDToDevice: {
            AudioObjectID result = kAudioObjectUnknown;
            if (qualifier_size == sizeof(CFStringRef) && qualifier != nullptr) {
                CFStringRef uid = *static_cast<const CFStringRef*>(qualifier);
                CFStringRef ours = cfstring(kDeviceUID);
                if (uid != nullptr && CFStringCompare(uid, ours, 0) == kCFCompareEqualTo) {
                    result = kObjectID_Device;
                }
                CFRelease(ours);
            }
            return write_value<AudioObjectID>(result, in_size, out_size, out_data);
        }
        case kAudioPlugInPropertyResourceBundle:
            return write_value<CFStringRef>(CFSTR(""), in_size, out_size, out_data);
        default: return kAudioHardwareUnknownPropertyError;
        }
    }

    if (object == kObjectID_Device) {
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
            return write_value<AudioClassID>(kAudioObjectClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyClass:
            return write_value<AudioClassID>(kAudioDeviceClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyOwner:
            return write_value<AudioObjectID>(kObjectID_PlugIn, in_size, out_size, out_data);
        case kAudioObjectPropertyName:
            return write_value<CFStringRef>(cfstring(kDeviceName), in_size, out_size, out_data);
        case kAudioObjectPropertyManufacturer:
            return write_value<CFStringRef>(cfstring(kManufacturer), in_size, out_size, out_data);
        case kAudioDevicePropertyDeviceUID:
            return write_value<CFStringRef>(cfstring(kDeviceUID), in_size, out_size, out_data);
        case kAudioDevicePropertyModelUID:
            return write_value<CFStringRef>(cfstring(kModelUID), in_size, out_size, out_data);
        case kAudioObjectPropertyOwnedObjects:
        case kAudioDevicePropertyStreams: {
            AudioObjectID ids[2];
            UInt32 count = 0;
            if (address->mScope != kAudioObjectPropertyScopeOutput) ids[count++] = kObjectID_Stream_Input;
            if (address->mScope != kAudioObjectPropertyScopeInput) ids[count++] = kObjectID_Stream_Output;
            count = std::min<UInt32>(count, in_size / sizeof(AudioObjectID));
            std::memcpy(out_data, ids, count * sizeof(AudioObjectID));
            *out_size = count * sizeof(AudioObjectID);
            return 0;
        }
        case kAudioObjectPropertyControlList:
            *out_size = 0;
            return 0;
        case kAudioDevicePropertyTransportType:
            return write_value<UInt32>(kAudioDeviceTransportTypeVirtual, in_size, out_size, out_data);
        case kAudioDevicePropertyRelatedDevices:
            return write_value<AudioObjectID>(kObjectID_Device, in_size, out_size, out_data);
        case kAudioDevicePropertyClockDomain:
            return write_value<UInt32>(0, in_size, out_size, out_data);
        case kAudioDevicePropertyDeviceIsAlive:
            return write_value<UInt32>(1, in_size, out_size, out_data);
        case kAudioDevicePropertyDeviceIsRunning: {
            std::lock_guard lock(g_state_mutex);
            return write_value<UInt32>(g_io_clients > 0 ? 1 : 0, in_size, out_size, out_data);
        }
        // Never become the system default: only apps that ask for it get it.
        case kAudioDevicePropertyDeviceCanBeDefaultDevice:
            return write_value<UInt32>(address->mScope == kAudioObjectPropertyScopeInput ? 1 : 0,
                                       in_size, out_size, out_data);
        case kAudioDevicePropertyDeviceCanBeDefaultSystemDevice:
            return write_value<UInt32>(0, in_size, out_size, out_data);
        case kAudioDevicePropertyLatency:
            return write_value<UInt32>(kLatencyFrames, in_size, out_size, out_data);
        case kAudioDevicePropertySafetyOffset:
            return write_value<UInt32>(kSafetyOffsetFrames, in_size, out_size, out_data);
        case kAudioDevicePropertyZeroTimeStampPeriod:
            return write_value<UInt32>(kRingFrames, in_size, out_size, out_data);
        case kAudioDevicePropertyIsHidden:
            return write_value<UInt32>(0, in_size, out_size, out_data);
        case kAudioDevicePropertyNominalSampleRate:
            return write_value<Float64>(kSampleRate, in_size, out_size, out_data);
        case kAudioDevicePropertyAvailableNominalSampleRates:
            return write_value<AudioValueRange>({kSampleRate, kSampleRate}, in_size, out_size, out_data);
        case kAudioDevicePropertyPreferredChannelsForStereo: {
            if (in_size < 2 * sizeof(UInt32)) return kAudioHardwareBadPropertySizeError;
            auto* channels = static_cast<UInt32*>(out_data);
            channels[0] = 1;
            channels[1] = 2;
            *out_size = 2 * sizeof(UInt32);
            return 0;
        }
        default: return kAudioHardwareUnknownPropertyError;
        }
    }

    if (is_stream(object)) {
        const bool input = object == kObjectID_Stream_Input;
        switch (selector) {
        case kAudioObjectPropertyBaseClass:
            return write_value<AudioClassID>(kAudioObjectClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyClass:
            return write_value<AudioClassID>(kAudioStreamClassID, in_size, out_size, out_data);
        case kAudioObjectPropertyOwner:
            return write_value<AudioObjectID>(kObjectID_Device, in_size, out_size, out_data);
        case kAudioObjectPropertyOwnedObjects:
            *out_size = 0;
            return 0;
        case kAudioStreamPropertyIsActive:
            return write_value<UInt32>(1, in_size, out_size, out_data);
        case kAudioStreamPropertyDirection:
            return write_value<UInt32>(input ? 1 : 0, in_size, out_size, out_data);
        case kAudioStreamPropertyTerminalType:
            return write_value<UInt32>(input ? kAudioStreamTerminalTypeMicrophone
                                             : kAudioStreamTerminalTypeSpeaker,
                                       in_size, out_size, out_data);
        case kAudioStreamPropertyStartingChannel:
            return write_value<UInt32>(1, in_size, out_size, out_data);
        case kAudioStreamPropertyLatency:
            return write_value<UInt32>(kLatencyFrames, in_size, out_size, out_data);
        case kAudioStreamPropertyVirtualFormat:
        case kAudioStreamPropertyPhysicalFormat:
            return write_value<AudioStreamBasicDescription>(stream_format(), in_size, out_size, out_data);
        case kAudioStreamPropertyAvailableVirtualFormats:
        case kAudioStreamPropertyAvailablePhysicalFormats: {
            AudioStreamRangedDescription ranged{};
            ranged.mFormat = stream_format();
            ranged.mSampleRateRange = {kSampleRate, kSampleRate};
            if (in_size < sizeof(ranged)) { *out_size = 0; return 0; }
            return write_value<AudioStreamRangedDescription>(ranged, in_size, out_size, out_data);
        }
        default: return kAudioHardwareUnknownPropertyError;
        }
    }
    return kAudioHardwareBadObjectError;
}

// ---- IO -----------------------------------------------------------------

OSStatus StartIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32) {
    if (driver != g_driver || device != kObjectID_Device) return kAudioHardwareBadObjectError;
    std::lock_guard lock(g_state_mutex);
    if (g_io_clients++ == 0) {
        g_number_time_stamps = 0;
        g_anchor_host_time = mach_absolute_time();
        std::lock_guard io_lock(g_io_mutex);
        std::memset(g_ring, 0, sizeof(g_ring));
    }
    return kAudioHardwareNoError;
}

OSStatus StopIO(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32) {
    if (driver != g_driver || device != kObjectID_Device) return kAudioHardwareBadObjectError;
    std::lock_guard lock(g_state_mutex);
    if (g_io_clients == 0) return kAudioHardwareIllegalOperationError;
    --g_io_clients;
    return kAudioHardwareNoError;
}

OSStatus GetZeroTimeStamp(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32,
                          Float64* sample_time, UInt64* host_time, UInt64* seed) {
    if (driver != g_driver || device != kObjectID_Device) return kAudioHardwareBadObjectError;
    std::lock_guard lock(g_state_mutex);
    const UInt64 now = mach_absolute_time();
    const Float64 ticks_per_ring = g_host_ticks_per_frame * kRingFrames;
    UInt64 next = g_anchor_host_time +
        static_cast<UInt64>((g_number_time_stamps + 1) * ticks_per_ring);
    if (next <= now) ++g_number_time_stamps;
    *sample_time = static_cast<Float64>(g_number_time_stamps) * kRingFrames;
    *host_time = g_anchor_host_time + static_cast<UInt64>(g_number_time_stamps * ticks_per_ring);
    *seed = 1;
    return kAudioHardwareNoError;
}

OSStatus WillDoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, UInt32,
                           UInt32 operation, Boolean* will_do, Boolean* in_place) {
    if (driver != g_driver || device != kObjectID_Device) return kAudioHardwareBadObjectError;
    const bool handled = operation == kAudioServerPlugInIOOperationReadInput ||
        operation == kAudioServerPlugInIOOperationWriteMix;
    if (will_do != nullptr) *will_do = handled;
    if (in_place != nullptr) *in_place = true;
    return kAudioHardwareNoError;
}

OSStatus DoIOOperation(AudioServerPlugInDriverRef driver, AudioObjectID device, AudioObjectID stream,
                       UInt32, UInt32 operation, UInt32 frames,
                       const AudioServerPlugInIOCycleInfo* cycle, void* main_buffer, void*) {
    if (driver != g_driver || device != kObjectID_Device) return kAudioHardwareBadObjectError;
    if (cycle == nullptr || main_buffer == nullptr || frames > kRingFrames) {
        return kAudioHardwareIllegalOperationError;
    }
    auto* buffer = static_cast<Float32*>(main_buffer);
    std::lock_guard lock(g_io_mutex);

    if (operation == kAudioServerPlugInIOOperationWriteMix && stream == kObjectID_Stream_Output) {
        const UInt64 start = static_cast<UInt64>(cycle->mOutputTime.mSampleTime) % kRingFrames;
        const UInt32 first = std::min<UInt32>(frames, kRingFrames - static_cast<UInt32>(start));
        std::memcpy(&g_ring[start * kChannels], buffer, first * kBytesPerFrame);
        std::memcpy(g_ring, buffer + first * kChannels, (frames - first) * kBytesPerFrame);
        return kAudioHardwareNoError;
    }
    if (operation == kAudioServerPlugInIOOperationReadInput && stream == kObjectID_Stream_Input) {
        const UInt64 start = static_cast<UInt64>(cycle->mInputTime.mSampleTime) % kRingFrames;
        const UInt32 first = std::min<UInt32>(frames, kRingFrames - static_cast<UInt32>(start));
        std::memcpy(buffer, &g_ring[start * kChannels], first * kBytesPerFrame);
        std::memcpy(buffer + first * kChannels, g_ring, (frames - first) * kBytesPerFrame);
        // Consumed audio is cleared so a stopped player leaves silence, not a loop.
        std::memset(&g_ring[start * kChannels], 0, first * kBytesPerFrame);
        std::memset(g_ring, 0, (frames - first) * kBytesPerFrame);
        return kAudioHardwareNoError;
    }
    return kAudioHardwareNoError;
}

} // namespace

extern "C" __attribute__((visibility("default")))
void* SpeakAnythingMicFactory(CFAllocatorRef, CFUUIDRef requested_type) {
    if (!CFEqual(requested_type, kAudioServerPlugInTypeUUID)) return nullptr;
    g_ref_count.fetch_add(1);
    return g_driver;
}
