# SpeakAnything Virtual Mic: CoreAudio HAL plug-in bundle. Install with
# packaging/macos/install-virtual-mic.sh (needs admin, restarts coreaudiod).
add_library(SpeakAnythingMic MODULE SpeakAnythingMic.cpp)
set_target_properties(SpeakAnythingMic PROPERTIES
    BUNDLE ON
    BUNDLE_EXTENSION driver
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    CXX_VISIBILITY_PRESET hidden
)
target_link_libraries(SpeakAnythingMic PRIVATE
    "-framework CoreAudio"
    "-framework CoreFoundation"
)
