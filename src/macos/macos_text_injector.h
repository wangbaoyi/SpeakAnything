#pragma once

#include <cstdint>
#include <memory>
#include <string_view>

struct MacTextInputTargetState;

// The focused application and, when Accessibility is granted, its focused
// UI element, captured before our own popup can take focus.
struct TextInputTarget {
    std::int32_t process_id = 0;
    std::shared_ptr<MacTextInputTargetState> state;

    [[nodiscard]] bool valid() const {
        return process_id > 0;
    }
};

std::uint32_t current_process_id();

// True when this process may read focus and post keystrokes. With prompt set,
// macOS shows its Accessibility consent dialog once.
bool macos_accessibility_trusted(bool prompt);

TextInputTarget capture_text_input_target(std::uint32_t excluded_process_id);
// True when the captured application is still running and is not us.
bool text_input_target_alive(const TextInputTarget& target, std::uint32_t excluded_process_id);

// Writes text as the focused element's selected text through Accessibility.
// paste_safe is set when nothing was written, so Cmd+V cannot duplicate it.
bool inject_text_into_text_input(
    TextInputTarget target,
    std::wstring_view text,
    std::uint32_t excluded_process_id,
    bool* paste_safe = nullptr);

// Activates the target application and sends Cmd+V; the clipboard must
// already hold the text.
bool paste_clipboard_into_text_input(
    TextInputTarget target,
    std::uint32_t excluded_process_id);
