#pragma once

#include <cstdint>

// Keeps a floating Qt window visible across Spaces, full-screen apps and
// display changes: tool windows otherwise hide when the app is inactive,
// which a menu-bar app always is.
void macos_make_overlay_window(std::uintptr_t native_view);
