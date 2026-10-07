#include "macos/macos_overlay.h"

#import <AppKit/AppKit.h>

void macos_make_overlay_window(std::uintptr_t native_view) {
    NSView* view = (__bridge NSView*)reinterpret_cast<void*>(native_view);
    NSWindow* window = view.window;
    if (window == nil) return;
    window.hidesOnDeactivate = NO;
    window.level = NSStatusWindowLevel;
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorFullScreenAuxiliary |
        NSWindowCollectionBehaviorStationary |
        NSWindowCollectionBehaviorIgnoresCycle;
}
