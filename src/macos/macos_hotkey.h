#pragma once

#include <QKeySequence>

#include <functional>

// System-wide shortcut through Carbon RegisterEventHotKey, which needs no
// Accessibility or Input Monitoring permission. Releases are detected by
// polling keys_down(), the same way the Windows build polls GetAsyncKeyState.
class MacGlobalHotkey final {
public:
    MacGlobalHotkey();
    ~MacGlobalHotkey();

    MacGlobalHotkey(const MacGlobalHotkey&) = delete;
    MacGlobalHotkey& operator=(const MacGlobalHotkey&) = delete;

    // on_pressed runs on the main thread. Returns false if the key cannot be
    // mapped or another application already owns the combination.
    bool set_shortcut(const QKeySequence& sequence, std::function<void()> on_pressed);
    void clear();
    [[nodiscard]] bool registered() const;
    [[nodiscard]] bool keys_down() const;

private:
    struct Impl;
    Impl* impl_;
};
