#!/bin/sh
# Builds SpeakAnything.app with every model inside and wraps it in a DMG.
#
#   tools/package_macos.sh [version]
#
# Needs: Qt 6.8+ (macdeployqt), CMake, Ninja, the models listed in
# README.en.md under models/, and sherpa-onnx in third_party/sherpa-onnx-macos.
# Signs ad hoc unless CODESIGN_IDENTITY names a Developer ID certificate.
set -eu

version="${1:-0.3.0}"
root="$(cd "$(dirname "$0")/.." && pwd)"
build="$root/build-mac-release"
out="$root/out"
qt_prefix="${QT_PREFIX:-$(brew --prefix qt 2>/dev/null || true)}"
identity="${CODESIGN_IDENTITY:--}"

require() {
    [ -e "$root/$1" ] || { echo "missing $1 (see README.en.md, Models)" >&2; exit 1; }
}
require models/sensevoice-small-q8.gguf
require models/fsmn-vad.gguf
require models/qwen3-1.7b-q4_k_m.gguf
require models/kokoro-multi-lang-v1_1/model.onnx
require third_party/sherpa-onnx-macos/lib/libsherpa-onnx-c-api.dylib

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$qt_prefix" -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
cmake --build "$build" --target sensevoice-ui sensevoice-tts sensevoice-stream SpeakAnythingMic

app="$build/SpeakAnything.app"
contents="$app/Contents"
resources="$contents/Resources"
frameworks="$contents/Frameworks"
# Start from an empty models folder so nothing from an earlier build lingers.
rm -rf "$resources/models"
mkdir -p "$resources/models" "$resources/dict" "$frameworks"

echo "Copying models..."
# Only the basic models ship; the rest download from Settings > Models.
for item in sensevoice-small-q8.gguf fsmn-vad.gguf qwen3-1.7b-q4_k_m.gguf kokoro-multi-lang-v1_1; do
    rsync -a --delete "$root/models/$item" "$resources/models/"
done
cp "$root/third_party/cppjieba/dict/jieba.dict.utf8" "$root/third_party/cppjieba/dict/user.dict.utf8" \
    "$resources/dict/"
cp "$root/LICENSE" "$root/THIRD_PARTY_NOTICES.md" "$resources/"

echo "Bundling sherpa-onnx..."
cp "$root/third_party/sherpa-onnx-macos/lib/libsherpa-onnx-c-api.dylib" \
    "$root/third_party/sherpa-onnx-macos/lib/libonnxruntime.dylib" "$frameworks/"
helper="$contents/MacOS/sensevoice-tts"
install_name_tool -add_rpath "@executable_path/../Frameworks" "$helper" 2>/dev/null || true

echo "App icon..."
iconset="$build/SpeakAnything.iconset"
rm -rf "$iconset" && mkdir -p "$iconset"
sips -s format png "$root/resources/sensevoice.ico" --out "$build/icon-source.png" >/dev/null
for size in 16 32 128 256 512; do
    sips -z $size $size "$build/icon-source.png" --out "$iconset/icon_${size}x${size}.png" >/dev/null
    double=$((size * 2))
    sips -z $double $double "$build/icon-source.png" --out "$iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$iconset" -o "$resources/SpeakAnything.icns"
/usr/libexec/PlistBuddy -c "Delete :CFBundleIconFile" "$contents/Info.plist" 2>/dev/null || true
/usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string SpeakAnything" "$contents/Info.plist"
/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $version" "$contents/Info.plist"

echo "Deploying Qt..."
"$qt_prefix/bin/macdeployqt" "$app" -executable="$helper" -always-overwrite

echo "Signing ($identity)..."
# Hardened runtime (needed for notarization) only with a real identity: ad hoc
# signatures carry no Team ID, so library validation would reject the bundled
# Qt and sherpa-onnx libraries.
if [ "$identity" = "-" ]; then
    codesign --force --deep --sign - "$app"
else
    codesign --force --deep --options runtime --timestamp --sign "$identity" \
        --entitlements "$root/packaging/macos/SpeakAnything.entitlements" "$app"
fi
driver="$build/packaging/macos/virtual-mic/SpeakAnythingMic.driver"
codesign --force --sign "$identity" "$driver"

echo "Creating DMG..."
staging="$build/dmg"
rm -rf "$staging" && mkdir -p "$staging/Virtual Mic"
cp -R "$app" "$staging/"
ln -s /Applications "$staging/Applications"
cp -R "$driver" "$staging/Virtual Mic/"
cp "$root/packaging/macos/install-virtual-mic.sh" "$staging/Virtual Mic/"
mkdir -p "$out"
dmg="$out/SpeakAnything-$version-macos-arm64.dmg"
rm -f "$dmg"
hdiutil create -volname "SpeakAnything $version" -srcfolder "$staging" -ov -format UDZO "$dmg" >/dev/null
echo "$dmg"
