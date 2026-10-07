#include "macos/macos_text_injector.h"

#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>
#include <unistd.h>

#include <chrono>
#include <thread>

struct MacTextInputTargetState {
    AXUIElementRef focused = nullptr;

    ~MacTextInputTargetState() {
        if (focused != nullptr) CFRelease(focused);
    }
};

namespace {

NSString* to_nsstring(std::wstring_view text) {
    // wchar_t is UTF-32 on macOS.
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size() * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding];
}

NSRunningApplication* running_target(const TextInputTarget& target) {
    if (!target.valid()) return nil;
    NSRunningApplication* app =
        [NSRunningApplication runningApplicationWithProcessIdentifier:target.process_id];
    return (app == nil || app.terminated) ? nil : app;
}

bool activate_and_wait(NSRunningApplication* app) {
    if (app.active) return true;
    [app activateWithOptions:0];
    for (int i = 0; i < 25; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier ==
            app.processIdentifier) {
            return true;
        }
    }
    return false;
}

} // namespace

std::uint32_t current_process_id() {
    return static_cast<std::uint32_t>(getpid());
}

bool macos_accessibility_trusted(bool prompt) {
    if (!prompt) return AXIsProcessTrusted();
    NSDictionary* options = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES};
    return AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)options);
}

TextInputTarget capture_text_input_target(std::uint32_t excluded_process_id) {
    @autoreleasepool {
        TextInputTarget target;
        NSRunningApplication* front = NSWorkspace.sharedWorkspace.frontmostApplication;
        if (front == nil || static_cast<std::uint32_t>(front.processIdentifier) == excluded_process_id) {
            return target;
        }
        target.process_id = front.processIdentifier;
        target.state = std::make_shared<MacTextInputTargetState>();
        if (AXIsProcessTrusted()) {
            AXUIElementRef app = AXUIElementCreateApplication(front.processIdentifier);
            CFTypeRef focused = nullptr;
            if (AXUIElementCopyAttributeValue(app, kAXFocusedUIElementAttribute, &focused) ==
                    kAXErrorSuccess &&
                focused != nullptr) {
                target.state->focused = static_cast<AXUIElementRef>(focused);
            }
            CFRelease(app);
        }
        return target;
    }
}

bool text_input_target_alive(const TextInputTarget& target, std::uint32_t excluded_process_id) {
    @autoreleasepool {
        return running_target(target) != nil &&
            static_cast<std::uint32_t>(target.process_id) != excluded_process_id;
    }
}

bool inject_text_into_text_input(
    TextInputTarget target,
    std::wstring_view text,
    std::uint32_t excluded_process_id,
    bool* paste_safe) {
    if (paste_safe != nullptr) *paste_safe = true;
    @autoreleasepool {
        NSRunningApplication* app = running_target(target);
        if (app == nil || static_cast<std::uint32_t>(target.process_id) == excluded_process_id ||
            target.state == nullptr || target.state->focused == nullptr) {
            return false;
        }
        Boolean settable = false;
        if (AXUIElementIsAttributeSettable(
                target.state->focused, kAXSelectedTextAttribute, &settable) != kAXErrorSuccess ||
            !settable) {
            return false;
        }
        if (!activate_and_wait(app)) return false;
        NSString* string = to_nsstring(text);
        if (string == nil) return false;
        // Many Electron/web text fields accept the call but ignore it; read the
        // value back so a silent no-op still falls through to Cmd+V.
        CFTypeRef before = nullptr;
        AXUIElementCopyAttributeValue(target.state->focused, kAXValueAttribute, &before);
        const AXError result = AXUIElementSetAttributeValue(
            target.state->focused, kAXSelectedTextAttribute, (__bridge CFStringRef)string);
        CFTypeRef after = nullptr;
        AXUIElementCopyAttributeValue(target.state->focused, kAXValueAttribute, &after);
        bool changed = result == kAXErrorSuccess;
        if (changed && before != nullptr && after != nullptr) {
            changed = !CFEqual(before, after);
        }
        if (before != nullptr) CFRelease(before);
        if (after != nullptr) CFRelease(after);
        if (paste_safe != nullptr) *paste_safe = !changed;
        return changed;
    }
}

bool paste_clipboard_into_text_input(
    TextInputTarget target,
    std::uint32_t excluded_process_id) {
    @autoreleasepool {
        NSRunningApplication* app = running_target(target);
        if (app == nil || static_cast<std::uint32_t>(target.process_id) == excluded_process_id ||
            !AXIsProcessTrusted() || !activate_and_wait(app)) {
            return false;
        }
        CGEventSourceRef source = CGEventSourceCreate(kCGEventSourceStateCombinedSessionState);
        CGEventRef down = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, true);
        CGEventRef up = CGEventCreateKeyboardEvent(source, kVK_ANSI_V, false);
        CGEventSetFlags(down, kCGEventFlagMaskCommand);
        CGEventSetFlags(up, kCGEventFlagMaskCommand);
        CGEventPost(kCGAnnotatedSessionEventTap, down);
        CGEventPost(kCGAnnotatedSessionEventTap, up);
        CFRelease(down);
        CFRelease(up);
        if (source != nullptr) CFRelease(source);
        return true;
    }
}
