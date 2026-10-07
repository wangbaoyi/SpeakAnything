#!/bin/sh
# Installs (or with --uninstall removes) the SpeakAnything Virtual Mic HAL
# plug-in. Needs admin rights; restarting coreaudiod briefly interrupts audio.
set -eu
target="/Library/Audio/Plug-Ins/HAL/SpeakAnythingMic.driver"
if [ "${1:-}" = "--uninstall" ]; then
    sudo rm -rf "$target"
else
    here="$(cd "$(dirname "$0")" && pwd)"
    if [ -n "${1:-}" ]; then
        source="$1"
    elif [ -d "$here/SpeakAnythingMic.driver" ]; then
        source="$here/SpeakAnythingMic.driver"      # next to the script in the DMG
    else
        source="$here/../../build-mac/packaging/macos/virtual-mic/SpeakAnythingMic.driver"
    fi
    [ -d "$source" ] || { echo "driver bundle not found: $source" >&2; exit 1; }
    codesign --force --sign "${CODESIGN_IDENTITY:--}" "$source"
    sudo rm -rf "$target"
    sudo cp -R "$source" "$target"
    sudo chown -R root:wheel "$target"
fi
sudo killall -9 coreaudiod
