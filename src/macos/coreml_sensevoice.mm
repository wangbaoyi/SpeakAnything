#include "macos/coreml_sensevoice.h"

#import <CoreML/CoreML.h>
#import <Foundation/Foundation.h>

#include <array>
#include <cstring>
#include <map>

namespace {

constexpr int feature_dimension = 560;
constexpr int query_frames = 4;
constexpr std::array<int, 3> buckets = {100, 250, 500};

std::string describe(NSError* error) {
    return error == nil ? std::string("unknown Core ML error")
                        : std::string(error.localizedDescription.UTF8String);
}

NSURL* cache_directory() {
    NSURL* caches = [NSFileManager.defaultManager URLsForDirectory:NSCachesDirectory
                                                         inDomains:NSUserDomainMask].firstObject;
    return [caches URLByAppendingPathComponent:@"SpeakAnything" isDirectory:YES];
}

// Compiling the package takes seconds, so the .mlmodelc is kept between runs
// and rebuilt only when the package's weights change size or date.
NSURL* compiled_model(NSURL* package, std::string& error) {
    NSFileManager* files = NSFileManager.defaultManager;
    NSURL* weights = [package URLByAppendingPathComponent:@"Data/com.apple.CoreML/weights/weight.bin"];
    NSDictionary* attributes = [files attributesOfItemAtPath:weights.path error:nil];
    NSString* stamp = [NSString stringWithFormat:@"sensevoice-%llu-%.0f.mlmodelc",
        [attributes[NSFileSize] unsignedLongLongValue],
        [attributes[NSFileModificationDate] timeIntervalSince1970]];
    NSURL* directory = cache_directory();
    NSURL* cached = [directory URLByAppendingPathComponent:stamp isDirectory:YES];
    if ([files fileExistsAtPath:cached.path]) return cached;

    NSError* failure = nil;
    NSURL* compiled = [MLModel compileModelAtURL:package error:&failure];
    if (compiled == nil) {
        error = "failed to compile the Core ML model: " + describe(failure);
        return nil;
    }
    [files createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:nil];
    for (NSURL* old in [files contentsOfDirectoryAtURL:directory includingPropertiesForKeys:nil
                                               options:0 error:nil]) {
        if ([old.lastPathComponent hasPrefix:@"sensevoice-"]) [files removeItemAtURL:old error:nil];
    }
    if (![files moveItemAtURL:compiled toURL:cached error:&failure]) return compiled;
    return cached;
}

MLMultiArray* int_input(int value) {
    MLMultiArray* array = [[MLMultiArray alloc] initWithShape:@[@1]
                                                     dataType:MLMultiArrayDataTypeInt32
                                                        error:nil];
    array[0] = @(value);
    return array;
}

} // namespace

struct CoreMLSenseVoice::Impl {
    NSURL* compiled = nil;
    std::map<int, MLModel*> functions;

    MLModel* function(int bucket, std::string& error) {
        const auto found = functions.find(bucket);
        if (found != functions.end()) return found->second;
        MLModelConfiguration* configuration = [[MLModelConfiguration alloc] init];
        configuration.computeUnits = MLComputeUnitsCPUAndNeuralEngine;
        configuration.functionName = [NSString stringWithFormat:@"encoder_%d", bucket];
        NSError* failure = nil;
        MLModel* model = [MLModel modelWithContentsOfURL:compiled configuration:configuration
                                                   error:&failure];
        if (model == nil) {
            error = "failed to load Core ML encoder_" + std::to_string(bucket) + ": " +
                describe(failure);
            return nil;
        }
        functions.emplace(bucket, model);
        return model;
    }
};

CoreMLSenseVoice::CoreMLSenseVoice() : impl_(std::make_unique<Impl>()) {}
CoreMLSenseVoice::~CoreMLSenseVoice() = default;

bool CoreMLSenseVoice::load(const std::filesystem::path& package, std::string& error) {
    @autoreleasepool {
        if (@available(macOS 15.0, *)) {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:package.c_str()]];
            impl_->compiled = compiled_model(url, error);
            if (impl_->compiled == nil) return false;
            // Each function is prepared for the Neural Engine on first load,
            // which can take seconds; do all of them now, off the dictation path.
            for (const int bucket : buckets) {
                if (impl_->function(bucket, error) == nil) return false;
            }
            return true;
        }
        error = "the Neural Engine speech model needs macOS 15 or later";
        return false;
    }
}

bool CoreMLSenseVoice::run(std::span<const float> features, int frames, int language_id,
                           int style_id, std::vector<float>& logits, int& vocabulary,
                           std::string& error) {
    @autoreleasepool {
        int bucket = 0;
        for (const int candidate : buckets) {
            if (frames <= candidate) {
                bucket = candidate;
                break;
            }
        }
        if (bucket == 0 || frames <= 0) {
            error = "utterance is longer than the Core ML model accepts";
            return false;
        }
        MLModel* model = impl_->function(bucket, error);
        if (model == nil) return false;

        NSError* failure = nil;
        MLMultiArray* input = [[MLMultiArray alloc]
            initWithShape:@[@1, @(bucket), @(feature_dimension)]
                 dataType:MLMultiArrayDataTypeFloat32
                    error:&failure];
        if (input == nil) {
            error = describe(failure);
            return false;
        }
        [input getMutableBytesWithHandler:^(void* bytes, NSInteger size, NSArray<NSNumber*>*) {
            std::memset(bytes, 0, static_cast<std::size_t>(size));
            std::memcpy(bytes, features.data(),
                        static_cast<std::size_t>(frames) * feature_dimension * sizeof(float));
        }];
        MLDictionaryFeatureProvider* provider = [[MLDictionaryFeatureProvider alloc]
            initWithDictionary:@{
                @"features": input,
                @"lengths": int_input(frames),
                @"language_id": int_input(language_id),
                @"style_id": int_input(style_id),
            }
                         error:&failure];
        id<MLFeatureProvider> output = provider == nil ? nil
            : [model predictionFromFeatures:provider error:&failure];
        MLMultiArray* result = [output featureValueForName:@"logits"].multiArrayValue;
        if (result == nil || result.shape.count != 3) {
            error = "Core ML prediction failed: " + describe(failure);
            return false;
        }

        const int rows = frames + query_frames;
        vocabulary = result.shape[2].intValue;
        const NSInteger row_stride = result.strides[1].integerValue;
        const NSInteger column_stride = result.strides[2].integerValue;
        logits.assign(static_cast<std::size_t>(rows) * vocabulary, 0.0F);
        const MLMultiArrayDataType type = result.dataType;
        [result getBytesWithHandler:^(const void* bytes, NSInteger) {
            for (int row = 0; row < rows; ++row) {
                float* destination = &logits[static_cast<std::size_t>(row) * vocabulary];
                for (int column = 0; column < vocabulary; ++column) {
                    const NSInteger offset = row * row_stride + column * column_stride;
                    destination[column] = type == MLMultiArrayDataTypeFloat16
                        ? static_cast<float>(static_cast<const _Float16*>(bytes)[offset])
                        : static_cast<const float*>(bytes)[offset];
                }
            }
        }];
        return true;
    }
}
