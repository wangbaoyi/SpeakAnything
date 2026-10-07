#include "macos/macos_hotkey.h"

#include <Carbon/Carbon.h>

#include <utility>

namespace {

constexpr OSType hotkey_signature = 'SpkA';

// Virtual key codes are positional (ANSI layout), matching Carbon's kVK_* set.
UInt32 virtual_key_for_qt_key(Qt::Key key) {
    static const UInt32 letters[] = {
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E, kVK_ANSI_F, kVK_ANSI_G,
        kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J, kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N,
        kVK_ANSI_O, kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T, kVK_ANSI_U,
        kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y, kVK_ANSI_Z,
    };
    static const UInt32 digits[] = {
        kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9,
    };
    static const UInt32 functions[] = {
        kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6, kVK_F7, kVK_F8, kVK_F9, kVK_F10,
        kVK_F11, kVK_F12, kVK_F13, kVK_F14, kVK_F15, kVK_F16, kVK_F17, kVK_F18, kVK_F19, kVK_F20,
    };
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return letters[key - Qt::Key_A];
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return digits[key - Qt::Key_0];
    if (key >= Qt::Key_F1 && key <= Qt::Key_F20) return functions[key - Qt::Key_F1];
    switch (key) {
    case Qt::Key_Space: return kVK_Space;
    case Qt::Key_Return:
    case Qt::Key_Enter: return kVK_Return;
    case Qt::Key_Tab: return kVK_Tab;
    case Qt::Key_Backspace: return kVK_Delete;
    case Qt::Key_Delete: return kVK_ForwardDelete;
    case Qt::Key_Escape: return kVK_Escape;
    case Qt::Key_Left: return kVK_LeftArrow;
    case Qt::Key_Right: return kVK_RightArrow;
    case Qt::Key_Up: return kVK_UpArrow;
    case Qt::Key_Down: return kVK_DownArrow;
    case Qt::Key_Home: return kVK_Home;
    case Qt::Key_End: return kVK_End;
    case Qt::Key_PageUp: return kVK_PageUp;
    case Qt::Key_PageDown: return kVK_PageDown;
    case Qt::Key_Comma: return kVK_ANSI_Comma;
    case Qt::Key_Period: return kVK_ANSI_Period;
    case Qt::Key_Slash: return kVK_ANSI_Slash;
    case Qt::Key_Semicolon: return kVK_ANSI_Semicolon;
    case Qt::Key_Minus: return kVK_ANSI_Minus;
    case Qt::Key_Equal: return kVK_ANSI_Equal;
    case Qt::Key_BracketLeft: return kVK_ANSI_LeftBracket;
    case Qt::Key_BracketRight: return kVK_ANSI_RightBracket;
    case Qt::Key_Backslash: return kVK_ANSI_Backslash;
    case Qt::Key_Apostrophe: return kVK_ANSI_Quote;
    case Qt::Key_QuoteLeft: return kVK_ANSI_Grave;
    default: return UINT32_MAX;
    }
}

} // namespace

struct MacGlobalHotkey::Impl {
    EventHotKeyRef hotkey = nullptr;
    EventHandlerRef handler = nullptr;
    std::function<void()> on_pressed;
    UInt32 virtual_key = UINT32_MAX;
    CGEventFlags required_flags = 0;
    // Each instance (the dictation key, Esc to cancel) has its own id; every
    // instance's handler sees every hot key and passes on the others.
    UInt32 number = next_number();

    static UInt32 next_number() {
        static UInt32 counter = 0;
        return ++counter;
    }

    static OSStatus handle(EventHandlerCallRef, EventRef event, void* user_data) {
        auto* self = static_cast<Impl*>(user_data);
        EventHotKeyID id{};
        GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                          sizeof(id), nullptr, &id);
        if (id.signature != hotkey_signature || id.id != self->number) return eventNotHandledErr;
        if (self->on_pressed) self->on_pressed();
        return noErr;
    }
};

MacGlobalHotkey::MacGlobalHotkey() : impl_(new Impl) {}

MacGlobalHotkey::~MacGlobalHotkey() {
    clear();
    if (impl_->handler != nullptr) RemoveEventHandler(impl_->handler);
    delete impl_;
}

bool MacGlobalHotkey::set_shortcut(const QKeySequence& sequence, std::function<void()> on_pressed) {
    clear();
    if (sequence.isEmpty() || sequence.count() != 1) return false;
    const QKeyCombination combination = sequence[0];
    const UInt32 key = virtual_key_for_qt_key(combination.key());
    if (key == UINT32_MAX) return false;

    // Qt on macOS maps Cmd to ControlModifier and Ctrl to MetaModifier.
    const Qt::KeyboardModifiers modifiers = combination.keyboardModifiers();
    UInt32 carbon = 0;
    CGEventFlags flags = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) { carbon |= cmdKey; flags |= kCGEventFlagMaskCommand; }
    if (modifiers.testFlag(Qt::MetaModifier)) { carbon |= controlKey; flags |= kCGEventFlagMaskControl; }
    if (modifiers.testFlag(Qt::AltModifier)) { carbon |= optionKey; flags |= kCGEventFlagMaskAlternate; }
    if (modifiers.testFlag(Qt::ShiftModifier)) { carbon |= shiftKey; flags |= kCGEventFlagMaskShift; }

    if (impl_->handler == nullptr) {
        const EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        InstallApplicationEventHandler(&Impl::handle, 1, &spec, impl_, &impl_->handler);
    }
    const EventHotKeyID id{hotkey_signature, impl_->number};
    if (RegisterEventHotKey(key, carbon, id, GetApplicationEventTarget(), 0, &impl_->hotkey) != noErr) {
        impl_->hotkey = nullptr;
        return false;
    }
    impl_->on_pressed = std::move(on_pressed);
    impl_->virtual_key = key;
    impl_->required_flags = flags;
    return true;
}

void MacGlobalHotkey::clear() {
    if (impl_->hotkey != nullptr) {
        UnregisterEventHotKey(impl_->hotkey);
        impl_->hotkey = nullptr;
    }
    impl_->on_pressed = nullptr;
    impl_->virtual_key = UINT32_MAX;
    impl_->required_flags = 0;
}

bool MacGlobalHotkey::registered() const {
    return impl_->hotkey != nullptr;
}

bool MacGlobalHotkey::keys_down() const {
    if (impl_->virtual_key == UINT32_MAX) return false;
    const CGEventFlags flags = CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState);
    if ((flags & impl_->required_flags) != impl_->required_flags) return false;
    return CGEventSourceKeyState(
        kCGEventSourceStateCombinedSessionState, static_cast<CGKeyCode>(impl_->virtual_key));
}
