#pragma once

// Platform-neutral names for "remember where the cursor was, then type there".
// Each platform keeps its own implementation; the UI only uses these names.

#include <cstdint>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "windows_text_injector.h"

using TextInputTarget = WindowsTextInputTarget;

inline std::uint32_t current_process_id() {
    return static_cast<std::uint32_t>(GetCurrentProcessId());
}
inline TextInputTarget capture_text_input_target(std::uint32_t excluded_process_id) {
    return capture_windows_text_input_target(excluded_process_id);
}
inline bool inject_text_into_text_input(
    TextInputTarget target, std::wstring_view text,
    std::uint32_t excluded_process_id, bool* paste_safe) {
    return inject_text_into_windows_text_input(
        std::move(target), text, excluded_process_id, paste_safe);
}
inline bool paste_clipboard_into_text_input(
    TextInputTarget target, std::uint32_t excluded_process_id) {
    return paste_clipboard_into_windows_text_input(std::move(target), excluded_process_id);
}
#elif defined(__APPLE__)
#include "macos/macos_text_injector.h"
#endif
