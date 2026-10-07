#include "macos/macos_system_voice.h"

#import <AVFoundation/AVFoundation.h>

#include <algorithm>
#include <memory>
#include <mutex>

namespace {

// Best installed voice for a language code such as "bg": premium, then
// enhanced, then default quality.
AVSpeechSynthesisVoice* voice_for(const std::string& language) {
    NSString* prefix = [NSString stringWithUTF8String:language.c_str()];
    AVSpeechSynthesisVoice* best = nil;
    for (AVSpeechSynthesisVoice* voice in AVSpeechSynthesisVoice.speechVoices) {
        NSString* code = voice.language; // "bg-BG"
        if (![code isEqualToString:prefix] && ![code hasPrefix:[prefix stringByAppendingString:@"-"]]) continue;
        if (best == nil || voice.quality > best.quality) best = voice;
    }
    return best;
}

// Synthesizers must outlive their asynchronous write.
NSMutableSet<AVSpeechSynthesizer*>* active_synthesizers() {
    static NSMutableSet<AVSpeechSynthesizer*>* set = [NSMutableSet set];
    return set;
}

std::mutex& active_mutex() {
    static std::mutex mutex;
    return mutex;
}

} // namespace

bool macos_system_voice_available(const std::string& language) {
    @autoreleasepool {
        return voice_for(language) != nil;
    }
}

void macos_system_voice_synthesize(
    const std::string& text,
    const std::string& language,
    double speed,
    std::function<void(std::vector<float> samples, int sample_rate, bool ok)> done) {
    @autoreleasepool {
        AVSpeechSynthesisVoice* voice = voice_for(language);
        if (voice == nil) {
            done({}, 0, false);
            return;
        }
        AVSpeechUtterance* utterance =
            [AVSpeechUtterance speechUtteranceWithString:[NSString stringWithUTF8String:text.c_str()]];
        utterance.voice = voice;
        const float rate = AVSpeechUtteranceDefaultSpeechRate * static_cast<float>(speed);
        utterance.rate = std::clamp(rate, AVSpeechUtteranceMinimumSpeechRate, AVSpeechUtteranceMaximumSpeechRate);

        AVSpeechSynthesizer* synthesizer = [[AVSpeechSynthesizer alloc] init];
        {
            std::lock_guard lock(active_mutex());
            [active_synthesizers() addObject:synthesizer];
        }
        auto samples = std::make_shared<std::vector<float>>();
        auto sample_rate = std::make_shared<int>(0);
        auto callback = std::make_shared<decltype(done)>(std::move(done));
        __weak AVSpeechSynthesizer* weak = synthesizer;
        [synthesizer writeUtterance:utterance toBufferCallback:^(AVAudioBuffer* buffer) {
            auto* pcm = [buffer isKindOfClass:AVAudioPCMBuffer.class] ? (AVAudioPCMBuffer*)buffer : nil;
            // A zero-length buffer marks the end of the utterance.
            if (pcm == nil || pcm.frameLength == 0) {
                // Decide success before the samples are moved out.
                const bool ok = !samples->empty();
                (*callback)(std::move(*samples), *sample_rate, ok);
                std::lock_guard lock(active_mutex());
                if (AVSpeechSynthesizer* strong = weak) [active_synthesizers() removeObject:strong];
                return;
            }
            *sample_rate = static_cast<int>(pcm.format.sampleRate);
            const AVAudioFrameCount frames = pcm.frameLength;
            if (pcm.floatChannelData != nullptr) {
                const float* channel = pcm.floatChannelData[0];
                samples->insert(samples->end(), channel, channel + frames);
            } else if (pcm.int16ChannelData != nullptr) {
                const int16_t* channel = pcm.int16ChannelData[0];
                for (AVAudioFrameCount index = 0; index < frames; ++index) {
                    samples->push_back(static_cast<float>(channel[index]) / 32768.0F);
                }
            }
        }];
    }
}
