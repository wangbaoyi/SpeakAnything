# SpeakAnything

[中文](README.md) | **English**

![Windows](https://img.shields.io/badge/platform-Windows-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/platform-macOS%20(preview)-000000?logo=apple&logoColor=white)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt 6](https://img.shields.io/badge/UI-Qt%206-41CD52?logo=qt&logoColor=white)
![Offline](https://img.shields.io/badge/inference-offline-2E7D32)
![License](https://img.shields.io/badge/license-AGPL--3.0-blue)

A lightweight, offline voice input and live translation tool. Hold a shortcut and speak; the text is typed at your cursor. With translation turned on, the translation can also be synthesized and sent into a virtual microphone, so the other side of a call hears it directly.

All recognition, translation and speech synthesis run locally on the CPU; no cloud API is involved.

- Repository: <https://github.com/wangbaoyi/SpeakAnything>
- Downloads: <https://github.com/wangbaoyi/SpeakAnything/releases> (portable ZIP with all models)

The UI follows the system language (Chinese or English). Override it from the tray menu → **Language / 语言**; the change applies after a restart.

---

## Contents

1. [Features](#features)
2. [Quick start](#quick-start)
3. [Speaking to the other side of a call (virtual microphone)](#speaking-to-the-other-side-of-a-call-virtual-microphone)
4. [macOS (preview)](#macos-preview)
5. [Models and download links](#models-and-download-links)
6. [Architecture](#architecture)
7. [Source layout](#source-layout)
8. [Settings](#settings)
9. [Command-line tool](#command-line-tool)
10. [Building](#building)
11. [Packaging and releases](#packaging-and-releases)
12. [Tests](#tests)
13. [Performance](#performance)
14. [Design documents](#design-documents)
15. [Third-party components and licenses](#third-party-components-and-licenses)

---

## Features

| Capability | Description |
| --- | --- |
| Offline speech recognition | SenseVoiceSmall Q8, mixed Chinese/English, FSMN-VAD for real-time segmentation, partial results previewed in the popup |
| Direct input | Writes at the cursor through Windows TSF and UI Automation (macOS: Accessibility); when unsupported, waits for the "paste delay" and falls back to Ctrl+V (⌘V) |
| Text cleanup | Original mode / Clean mode (drops filler words, normalizes punctuation, merges repeated sentences), hotword correction and boosting |
| Offline translation | Opus-MT Chinese→English and English→Chinese, run as CTranslate2 int8; original and translation are typed at the cursor together |
| Speech | The translation is synthesized by Kokoro and played to a device of your choice (e.g. VB-Cable, or SpeakAnything Virtual Mic on macOS), so the other side of a call hears it |
| Original fallback | If translation fails, times out (5 s) or the spoken language doesn't match the direction, only the original is typed and the popup says so |
| Triggers | "Hold to talk" or "press to toggle" (toggle mode records for at most 5 minutes) |
| Appearance | Capsule / panel / ring popup styles, color themes, linger time, always-visible mode |
| Audio protection | Mutes the default playback device while recording and restores it afterwards |
| Memory protection | 15 s maximum per utterance, 300 MiB working-set guard, long speech split at the nearest VAD boundary |

Domain terms (recording session, original, translation, speech queue and so on) are defined in [GLOSSARY.md](GLOSSARY.md).

## Quick start

1. Download `SpeakAnything-*-windows-x64.zip` from [Releases](https://github.com/wangbaoyi/SpeakAnything/releases) (about 740 MB, all models included).
2. Unzip anywhere and run `sensevoice-ui.exe`. It lives in the system tray.
3. Once the models are loaded, hold **Ctrl+Alt+Space** in any text field and speak; on release the text is typed where the cursor was when recording began. The program never presses Enter, so it can't send a message by accident.
4. Tray menu → **Settings** changes the shortcut, trigger, output, appearance, translation, speech, hotwords and VAD parameters.

Optional: run `install.cmd` to install into the current user's profile and register it to start at login (no administrator rights needed); `uninstall.ps1` removes it.

## Speaking to the other side of a call (virtual microphone)

```
You speak → recognition → original → translation → Kokoro speech → CABLE Input ══ virtual cable ══ CABLE Output → call app microphone → other side
```

1. Install the free [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) and reboot. (macOS: install SpeakAnything Virtual Mic instead, see [below](#virtual-microphone-on-macos).)
2. Tray menu → Settings → **Translation** tab: turn on translation and pick a direction.
3. **Speech** tab: turn on speech, choose **CABLE Input** as the speech device, pick a voice and speed, click Apply.
4. In your call app (WeChat, QQ, Zoom, Teams…) choose **CABLE Output** as the microphone.
5. Hold the shortcut and speak; after release the other side hears the whole translation. They don't hear your own voice.

Rules:

- Only the **translation** is spoken. If translation fails nothing is spoken (the original never is); the popup shows "nothing spoken".
- Several translations go into the **speech queue** and are spoken in order without interrupting each other. Tray menu → "Stop speaking" clears the queue.
- If the speech device is unavailable you get an error; it never falls back to another device (that would play through your own speakers and the other side would hear nothing).
- Optional **monitor**: also play on your own default output device; it is muted while recording so it isn't picked up again.
- Chinese→English uses an English voice; English→Chinese uses a Chinese voice.

## macOS (preview)

The macOS port builds from the same CMake project and is a work in progress. What works today:

| Area | macOS implementation |
| --- | --- |
| Speech recognition | Fastest available, picked at startup (shown in the tray tooltip): the Core ML build on the **Neural Engine** (macOS 15+), else the same Q8 GGUF as Windows on the **GPU through Metal**, else the CPU. `SPEAKANYTHING_ASR=gguf` skips the Neural Engine; `SENSEVOICE_NO_GPU=1` forces the CPU |
| Muting while recording | Off by default on macOS; tray menu → "Mute system sound while recording" |
| Sentence-end silence | 1200 ms on macOS (700 ms on Windows), so natural pauses don't split a sentence |
| VAD | FSMN-VAD on the CPU (1.7 MB model; dispatching it to the NPU would cost more than it runs) |
| Translation | **Qwen3-1.7B (Q4_K_M) on the GPU** through llama.cpp's Metal backend, one model for both directions |
| Speech synthesis | Kokoro through sherpa-onnx on the CPU (Accelerate); ONNX Runtime's Core ML provider measured slower |
| Global shortcut | Carbon `RegisterEventHotKey` (no Input Monitoring permission needed); default **⌃⌥Space** because ⌘⌥Space is Finder search |
| Typing at the cursor | Accessibility API (`kAXSelectedTextAttribute`), with ⌘V paste as the fallback |
| Mute while recording | CoreAudio default-output mute |
| Speech playback | miniaudio on CoreAudio; devices identified by CoreAudio UID |
| Launch at login | `SMAppService` (macOS 13+) |
| Virtual microphone | Own CoreAudio HAL plug-in, **SpeakAnything Virtual Mic**, replacing VB-Cable |
| App bundle | `SpeakAnything.app`, menu-bar only (`LSUIElement`); models go in `Contents/Resources/models` |

Permissions: on first launch macOS asks for **Microphone** access, and SpeakAnything asks for **Accessibility** access (System Settings → Privacy & Security → Accessibility) so it can type into other apps. Without Accessibility the text is still copied to the clipboard.

Measured on an M4 (macOS 15.7), warm:

| Step | Time |
| --- | --- |
| Recognition of a 4–7 s sentence on the Neural Engine | 14–33 ms (first launch compiles the model once, about 15 s) |
| Same, GGUF on the GPU (Metal) / on the CPU | 57–83 ms / 110–180 ms |
| Translation of one sentence on the GPU | 0.3–0.6 s |
| Kokoro synthesis (CPU, 4 threads) | RTF 0.23–0.26 |

The Neural Engine model needs **macOS 15**. Not done yet: notarization (needs a Developer ID), Kokoro on the Neural Engine (only a Swift Core ML port exists today). See [Building on macOS](#building-on-macos).

### Virtual microphone on macOS

`packaging/macos/virtual-mic` builds `SpeakAnythingMic.driver`, a user-space AudioServerPlugIn (no kernel extension). It publishes one 48 kHz stereo device: whatever is played to its output comes back out of its input, so:

1. Build it (it's part of the macOS build) and install it. This needs an administrator password and briefly restarts `coreaudiod`:

   ```bash
   packaging/macos/install-virtual-mic.sh
   ```

2. In SpeakAnything → Settings → **Speech**, choose **SpeakAnything Virtual Mic** as the speech device.
3. In your call app choose **SpeakAnything Virtual Mic** as the microphone.

Remove it with `packaging/macos/install-virtual-mic.sh --uninstall`. The device never becomes the system default output on its own.

## Languages, live speaking and cancelling

- **Languages.** Settings → Translation → *I speak* / *Translate into* takes any pair of 28 languages (Chinese, English, Cantonese, Japanese, Korean, Spanish, German, French, Italian, Portuguese, Dutch, Polish, Czech, Romanian, Hungarian, Swedish, Finnish, Turkish, Vietnamese, Indonesian, Bulgarian, Russian, Ukrainian, Serbian, Greek, Arabic, Hindi, Thai). Translation uses the Qwen3 LLM backend (macOS); the Opus-MT backend on Windows still covers only Chinese ↔ English.
- **Recognizers.** SenseVoice covers zh/en/yue/ja/ko. Other spoken languages need Whisper (Small, or Large v3 Turbo for best quality), downloaded from Settings → **Models**. On macOS Whisper Small's encoder runs on the Neural Engine and the decoder on the GPU.
- **Voices.** Kokoro speaks Chinese and English; 21 other languages have a Piper voice to download (no Bulgarian voice exists yet).
- **Models tab.** Pick the recognizer, translator and voice for the current languages, or leave *Automatic* (fastest installed). Only small models ship in the app; the rest download to `~/Library/Application Support/SpeakAnything/models` (`%LOCALAPPDATA%\SpeakAnything\models` on Windows).
- **Speak while I talk** (Settings → Speech): each sentence is translated and spoken as soon as you pause, instead of after you release the key. Earlier sentences are passed to the translator as context, so a split sentence still reads naturally.
- **Esc** while holding the shortcut (or while the take is finishing) discards it: nothing is typed, copied or spoken, and queued speech is dropped.
- **Laughter.** A laugh SenseVoice tags as an event is kept as "haha" / "哈哈", and the translator keeps interjections, so they get spoken.
- **Don't translate.** Tick *Don't translate* on a hotword (Settings → Hotwords) to keep it verbatim, e.g. "Claude Code" with the alias `cloud code` so it is also recognized correctly.

## Models and download links

Models are not stored in git. The release ZIP contains all of them; when building from source put them in `models/` (the packaging script checks for and copies them).

| Purpose | Path under `models/` | Size | Source | License |
| --- | --- | ---: | --- | --- |
| Speech recognition | `sensevoice-small-q8.gguf` | 243 MB | [FunAudioLLM/SenseVoiceSmall-GGUF](https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF) (file `sensevoice-small-q8.gguf`) | Apache-2.0 |
| Voice activity detection | `fsmn-vad.gguf` | 1.7 MB | [FunAudioLLM/fsmn-vad-GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF) (file `fsmn-vad.gguf`) | Apache-2.0 |
| Translation ZH→EN | `opus-mt-zh-en-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en), converted to CTranslate2 int8 with `tools/convert_opus_mt.py` | CC-BY-4.0 |
| Translation EN→ZH | `opus-mt-en-zh-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh), converted the same way | Apache-2.0 |
| Speech synthesis | `kokoro-multi-lang-v1_1/` | 401 MB | [`kokoro-multi-lang-v1_1.tar.bz2`](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2) from [sherpa-onnx tts-models](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models); original model [hexgrad/Kokoro-82M-v1.1-zh](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh) | Apache-2.0 |
| Chinese segmentation dictionary | `dict/` (next to the program) | | `jieba.dict.utf8` and `user.dict.utf8` from [cppjieba/dict](https://github.com/yanyiwu/cppjieba/tree/master/dict) | MIT |

macOS ships the same SenseVoice GGUF and adds:

| Purpose | Path under `models/` | Size | Source | License |
| --- | --- | ---: | --- | --- |
| Speech recognition (Neural Engine) | `sensevoice-coreml/` (`sensevoice.mlpackage`, `am.mvn`) plus `chn_jpn_yue_eng_ko_spectok.bpe.model` | 244 MB | [hugging-mac/sensevoice-small-coreml](https://huggingface.co/hugging-mac/sensevoice-small-coreml); tokenizer from [FunAudioLLM/SenseVoiceSmall](https://huggingface.co/FunAudioLLM/SenseVoiceSmall) | FunASR Model License 1.1 |
| Translation (GPU), both directions | `qwen3-1.7b-q4_k_m.gguf` | 1.1 GB | [unsloth/Qwen3-1.7B-GGUF](https://huggingface.co/unsloth/Qwen3-1.7B-GGUF) (file `Qwen3-1.7B-Q4_K_M.gguf`); original [Qwen/Qwen3-1.7B](https://huggingface.co/Qwen/Qwen3-1.7B) | Apache-2.0 |
| VAD, speech synthesis | `fsmn-vad.gguf`, `kokoro-multi-lang-v1_1/` | | Same as above | |

Upstream: the original SenseVoice model is [FunAudioLLM/SenseVoiceSmall](https://huggingface.co/FunAudioLLM/SenseVoiceSmall); GGUF conversion and runtime come from [FunASR runtime/llama.cpp](https://github.com/modelscope/FunASR/tree/main/runtime/llama.cpp).

### Getting the models

Simplest: download the release ZIP and copy its `models` folder into the repository root.

Or fetch them one by one:

```powershell
# Recognition and VAD (needs pip install -U huggingface_hub)
huggingface-cli download FunAudioLLM/SenseVoiceSmall-GGUF sensevoice-small-q8.gguf --local-dir models
huggingface-cli download FunAudioLLM/fsmn-vad-GGUF fsmn-vad.gguf --local-dir models

# Translation: download and convert to CTranslate2 int8 (once, on a development machine)
python -m pip install ctranslate2 transformers sentencepiece torch sacremoses
python tools\convert_opus_mt.py   # produces models\opus-mt-zh-en-ct2 and models\opus-mt-en-zh-ct2

# Speech synthesis
curl -L -o kokoro.tar.bz2 https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2
tar xjf kokoro.tar.bz2 -C models
```

> Note: Kokoro must be the **fp32 build** (`kokoro-multi-lang-v1_1`), not `kokoro-int8-multi-lang-v1_1`. On a Ryzen 5 5600 the int8 build measured slower than real time (RTF 1.35 to 2.2); fp32 with 4 threads runs at RTF 0.34 to 0.43. See [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md).

### Kokoro voice IDs

`voices.bin` holds 103 voices; the Settings dialog selects them by ID:

| ID | Voice |
| --- | --- |
| 0 | American female af_maple (English default) |
| 1 | American female af_sol |
| 2 | British female bf_vale |
| 3 to 57 | Chinese female zf_001 to zf_099 (55 voices; 3 = zf_001 is the Chinese default) |
| 58 to 102 | Chinese male zm_009 to zm_100 (45 voices) |

## Architecture

### Recognition and input flow

```
Microphone (WASAPI / CoreAudio) → level shaping → FSMN-VAD → utterance snapshots → SenseVoiceSmall → Partial / Final results
                                                                                        │
                                                                    Text cleanup (Clean mode, hotword correction)
                                                                                        │
                                                                                    Original
                                                                                        │
                                                          With translation on: Opus-MT (CTranslate2) → Translation
                                                                                        │
                                                On shortcut release: type at cursor / copy to clipboard / speak to device
```

SenseVoiceSmall is not a native streaming model; the project gets a real-time feel from utterance snapshots. Partial results keep updating the popup preview; once VAD confirms the end of a sentence the Final result is committed and its audio released. When the recognition thread is busy only the newest snapshot is kept, so stale results never queue up. Partial results are never translated.

### Processes and threads

| Component | Runs in | Notes |
| --- | --- | --- |
| `sensevoice-ui.exe` / `SpeakAnything.app` | Main process | Qt UI, shortcut, recognition, translation, injection, speech queue |
| Recognition | Main process, background thread | ggml CPU backend, no OpenMP |
| Translation | Main process, background thread (`TranslationWorker`) | Only one direction's model is loaded at a time; unloaded when translation is turned off |
| Speech synthesis | **Separate process** `sensevoice-tts(.exe)` | Talks over stdin/stdout pipes; a crash doesn't affect recognition or input |
| Playback | Main process, one thread per output device (`SpeechPlayer`) | Windows: WASAPI shared mode with automatic resampling. macOS: miniaudio on CoreAudio |

### Speech synthesis process protocol

`sensevoice-tts <model directory> [threads]`; stdin/stdout are binary and handle one request at a time:

| Direction | Content |
| --- | --- |
| On startup | `READY <sample rate>\n`, or `ERROR <message>\n` and exit |
| Request (stdin) | `<voice id>\t<speed>\t<UTF-8 text>\n` |
| Success | `AUDIO <sample count>\n` followed by that many float32 samples (mono, 24 kHz) |
| Failure | `ERROR <message>\n` |

The process exits when stdin closes. It first changes into the model directory and loads files by relative path, because sherpa-onnx and espeak-ng open files through narrow-character paths, which fail under non-ASCII install directories.

Why a separate process: when Kokoro speaks English it relies on espeak-ng (GPL-3.0) for phonemization. A separate process draws a clear license boundary and isolates crashes. See [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md).

## Source layout

```
src/
  qt_ui_main.cpp            Qt main program: popup, tray, settings dialog, recording sessions, translation and speech scheduling
  stream_recognizer.*       Streaming recognition: VAD segmentation, Partial/Final results, memory guard
  sensevoice_engine.*       SenseVoiceSmall GGUF inference
  fsmn_vad_engine.*         FSMN-VAD GGUF inference
  audio_io.*                Microphone capture and audio file decoding (miniaudio; Media Foundation fallback on Windows)
  audio_conditioner.*       Recording level shaping (attenuates only, never amplifies noise)
  stability_tracker.*       Partial result stability tracking
  text_processor.*          Text cleanup, hotwords, correction rules, jieba segmentation
  hotword_boost.*           CTC hotword boosting
  translator.h              Translation interface and TranslationWorker
  opus_mt_translator.cpp    Opus-MT via CTranslate2
  translation_session.*     Per-utterance translation, language check and original fallback within a session
  translation_worker.cpp    Translation background thread
  speech_queue.*            Speech queue: manages sensevoice-tts, queues requests, dispatches audio
  speech_player.*           WASAPI playback and output device enumeration (Windows)
  speech_player_miniaudio.cpp  miniaudio playback and device enumeration (macOS)
  tts_helper_main.cpp       sensevoice-tts entry point
  text_injector.h           Platform-neutral names for text injection
  windows_text_injector.*   TSF / UI Automation injection and Ctrl+V paste fallback
  macos/                    macOS: Carbon hotkey, Accessibility injection, login item
  system_audio_mute.*       Mutes the default playback device while recording (WASAPI / CoreAudio)
  main.cpp                  Command-line tool sensevoice-stream
tests/                      Unit tests and the Windows injection test target
tools/                      Packaging, installer, model conversion, icon generation, popup geometry checks
packaging/windows/          Inno Setup definition and portable install / uninstall scripts
packaging/macos/            App Info.plist, SpeakAnything Virtual Mic HAL plug-in and its installer
resources/                  Icons, Qt resources and UI translations (resources/i18n)
docs/adr/                   Architecture decision records
third_party/                Third-party dependencies (see below)
models/                     Local models (not in git)
```

## Settings

Settings are stored through QSettings: on Windows in the registry under `HKCU\Software\SenseVoice\LocalDictation`, on macOS in `~/Library/Preferences/com.sensevoice.LocalDictation.plist`.

| Key | Meaning | Default |
| --- | --- | --- |
| `hotkey/shortcut` | Shortcut | `Ctrl+Alt+Space` (macOS: `Meta+Alt+Space`, i.e. ⌃⌥Space) |
| `hotkey/trigger` | `hold` hold to talk / `toggle` press to toggle | `hold` |
| `text/mode` | 0 original / 1 clean | 1 |
| `output/destination` | `insert` type at cursor / `copy` copy only | `insert` |
| `output/content` | Popup content: original / translation / both | original |
| `output/clipboard_content` | Clipboard content (also what is typed at the cursor) | original |
| `output/paste_delay_ms` | Wait before the paste fallback | 150 |
| `translation/enabled` | Translation mode | false |
| `translation/direction` | `zh-en` / `en-zh` | `zh-en` |
| `translation/separator` | Separator between original and translation | ` / ` |
| `speech/enabled` | Speech (needs translation mode) | false |
| `speech/device_id` | Speech device: Windows endpoint ID or CoreAudio device UID | empty |
| `speech/monitor` | Monitor | false |
| `speech/voice_en`, `speech/voice_zh` | English and Chinese voice IDs | 0, 3 |
| `speech/speed_percent` | Speed (50 to 200) | 100 |
| `vad/*` | End-of-sentence silence, model threshold, minimum loudness, noise margin | 700 ms, 0.55, -60 dBFS, 3 dB |
| `appearance/*` | Color theme, font, linger time, always-visible mode | |
| `ui/language` | `auto` / `zh` / `en` (the `SPEAKANYTHING_LANG` environment variable overrides it) | `auto` |

Hotwords are saved in `hotwords.tsv` in the program's data directory.

## Command-line tool

`sensevoice-stream` is for debugging recognition and translation; it prints JSON events.

```powershell
# Microphone
.\sensevoice-stream.exe --model .\models\sensevoice-small-q8.gguf --vad .\models\fsmn-vad.gguf --mic

# Audio file, translated to English
.\sensevoice-stream.exe --model .\models\sensevoice-small-q8.gguf --vad .\models\fsmn-vad.gguf --audio C:\path\to\recording.m4a --translate zh-en
```

| Option | Default | Effect |
| --- | ---: | --- |
| `--partial-ms` | 450 | Target interval between Partial results |
| `--endpoint-silence-ms` | 700 | Silence needed to end a sentence |
| `--maximum-utterance-ms` | 15000 | Longest audio per utterance |
| `--memory-limit-mb` | 300 | Working-set guard |
| `--vad-speech-threshold` | preset | FSMN speech confidence threshold |
| `--vad-min-db` | preset | Minimum input level |
| `--vad-min-snr-db` | preset | Minimum signal-to-noise margin |
| `--hotwords FILE` | optional | UTF-8 tab-separated hotword file |
| `--corrections FILE` | optional | Deterministic correction rules |
| `--translate zh-en\|en-zh` | off | Translate every Final result; emits `translation` and `injection` events |
| `--translation-models DIR` | `models` next to the program | Directory containing `opus-mt-*-ct2` (or, with the LLM backend, `qwen3-1.7b-q4_k_m.gguf`) |

## Building

### Building on Windows

Requirements:

- Windows 10 or later, x64;
- Visual Studio 2022 ("Desktop development with C++") or Build Tools 2022, CMake 3.20+;
- Qt 6.8 Widgets (MSVC 2022 64-bit), including Qt Linguist tools for the English UI;
- Git (for submodules).

```powershell
git clone https://github.com/wangbaoyi/SpeakAnything.git
cd SpeakAnything
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="E:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --target sensevoice-ui sensevoice-stream sensevoice-tts --parallel 8
```

Output goes to `build\Release\`. Before running, put the models in `build\Release\models\` and the segmentation dictionary in `build\Release\dict\`.

Notes:

- The CPU backend is used by default; release builds target CPUs with AVX2/FMA/F16C.
- ggml doesn't use OpenMP; CTranslate2 uses MSVC OpenMP, so the release must ship `vcomp140.dll` (without OpenMP, CTranslate2 hangs at process exit on Windows).
- CTranslate2's matrix backend is Ruy, with no MKL or oneDNN. Ruy needs the nested cpuinfo submodule, so clone with `--recursive`.
- SentencePiece is pinned to v0.2.0 and built with `UNICODE`; otherwise loading a model from a Chinese path fails silently.
- `-DSENSEVOICE_WITH_TRANSLATION=OFF` builds without translation.
- sherpa-onnx v1.13.8 (win-x64, shared, MD, Release) headers and libraries are vendored in `third_party/sherpa-onnx`; the build copies the DLLs to the output directory.

### Building on macOS

Requirements: macOS 13+, Xcode command-line tools, CMake 3.20+, Ninja and Qt 6.8+ (e.g. `brew install cmake ninja qt`).

```bash
git submodule update --init --recursive
cmake -S . -B build-mac -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build-mac --target sensevoice-ui sensevoice-stream SpeakAnythingMic
```

This produces `build-mac/SpeakAnything.app`, `build-mac/sensevoice-stream` and `build-mac/packaging/macos/virtual-mic/SpeakAnythingMic.driver`. Copy `models/` and `dict/` into `SpeakAnything.app/Contents/Resources/`.

- CTranslate2 uses Apple Accelerate on macOS (no Ruy, no OpenMP).
- For Kokoro speech, unpack [sherpa-onnx v1.13.8 osx-universal2 shared](https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.13.8/sherpa-onnx-v1.13.8-osx-universal2-shared.tar.bz2) into `third_party/sherpa-onnx-macos` (or pass `-DSHERPA_ONNX_MACOS_DIR=…`). Without it, everything except speech works.
- `SENSEVOICE_WITH_COREML` (ON) runs SenseVoice on the Neural Engine when the model path is a `sensevoice-coreml/` directory; a `.gguf` path still uses the CPU engine.
- `SENSEVOICE_TRANSLATION_BACKEND` is `llm` on macOS (Qwen3 through llama.cpp, Metal) and `opus-mt` elsewhere; either can be chosen on any platform.

To build the distributable app with every model inside, plus a DMG containing the virtual microphone:

```bash
tools/package_macos.sh 0.3.0
```

It writes `out/SpeakAnything-0.3.0-macos-arm64.dmg` (about 1.7 GB). The app is signed ad hoc unless `CODESIGN_IDENTITY` names a Developer ID certificate, in which case it is signed with the hardened runtime, ready for notarization. An ad hoc build must be opened with right-click → Open the first time.

## Packaging and releases

```powershell
.\tools\package_windows.ps1 -Version 0.2.0      # portable ZIP
.\tools\create_installer.ps1 -Version 0.2.0     # Inno Setup installer (needs Inno Setup 6/7)
```

Output:

```
out\SpeakAnything-0.2.0-windows-x64.zip
out\SpeakAnything-0.2.0-Setup.exe
```

`package_windows.ps1` builds the three programs, gathers the Qt runtime with `windeployqt`, copies `vcomp140.dll`, the sherpa-onnx and ONNX Runtime DLLs, the segmentation dictionary, every model under `models/`, LICENSE and the third-party notices, then zips them. It fails if any model is missing.

Release layout:

```
sensevoice-ui.exe          Main program
sensevoice-tts.exe         Speech synthesis process
sensevoice-stream.exe      Command-line tool
sherpa-onnx-c-api.dll, onnxruntime*.dll, vcomp140.dll, Qt6*.dll
dict\                      Segmentation dictionary
models\                    All models
install.cmd / install.ps1 / uninstall.ps1 / README.txt / LICENSE / THIRD_PARTY_NOTICES.md
```

For macOS see `tools/package_macos.sh` under [Building on macOS](#building-on-macos).

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

| Test | Covers |
| --- | --- |
| `stability-tracker-test` | Partial result stability |
| `text-processor-test` | Text cleanup, hotwords, correction rules |
| `audio-conditioner-test` | Audio level shaping |
| `hotword-boost-test` | CTC hotword boosting |
| `translator-test` | Translation sessions and original fallback; also runs the real models when `models/opus-mt-*-ct2` exist |
| `windows-text-injector-test` | Injects text into the test window `windows-text-target.exe` (Windows only) |

Manual checks:

- `speech-queue-check "CABLE Input"`: speaks one English and one Chinese sentence to the given device through the speech queue; record from CABLE Output (or SpeakAnything Virtual Mic) to verify.
- `sensevoice-ui --preview --preview-settings <dir>`: saves a screenshot of every Settings tab as `settings-N.png`. Prefix with `SPEAKANYTHING_LANG=en` for the English UI.

## Performance

20-logical-core AVX2 machine, Q8 recognition model:

- Recognition model load about 80 to 140 ms; first non-empty Partial about 700 ms after you start speaking;
- In the Final stage, VAD takes about 30 to 40 ms and SenseVoice inference about 370 to 400 ms;
- Total CPU during real-time playback about 13%, peak working set about 289 MiB.

Ryzen 5 5600 (6 cores, 12 threads):

- Translating a Chinese sentence of about 12 seconds takes about 340 ms;
- Kokoro fp32 synthesis with 4 threads runs at RTF 0.34 to 0.43 (4 s of speech is synthesized in 1.5 to 1.9 s); the speech model's first load takes about 3 s.

These numbers depend on hardware and are only engineering references.

## Design documents

- [GLOSSARY.md](GLOSSARY.md): domain terms (recording session, Final result, original, translation, original fallback, result destination, speech, speech device, speech queue, monitor…).
- [ADR 0001](docs/adr/0001-ctranslate2-for-opus-mt.md): why Opus-MT runs on CTranslate2.
- [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md): why Kokoro fp32, and why in a separate process.

## Third-party components and licenses

This project is released under the [GNU AGPL-3.0](LICENSE): the Windows input method component is based on OpenLess (AGPL-3.0) and speech synthesis uses espeak-ng (GPL-3.0), so the whole project is AGPL-3.0.

| Component | Use | License |
| --- | --- | --- |
| [llama.cpp / ggml](https://github.com/ggml-org/llama.cpp) | Recognition and VAD inference runtime (vendored in `third_party/llama.cpp`) | MIT |
| [FunASR](https://github.com/modelscope/FunASR) | SenseVoice / VAD runtime helpers | MIT |
| [CTranslate2](https://github.com/OpenNMT/CTranslate2) | Translation inference | MIT |
| [SentencePiece](https://github.com/google/sentencepiece) | Translation tokenization | Apache-2.0 |
| [cppjieba](https://github.com/yanyiwu/cppjieba) | Chinese segmentation and dictionary | MIT |
| [OpenLess](https://github.com/Open-Less/openless) | Windows TSF input method (`OpenLessIme.dll`) | AGPL-3.0 |
| [Qwen3-1.7B](https://huggingface.co/Qwen/Qwen3-1.7B) | macOS translation model | Apache-2.0 |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | Kokoro speech synthesis (prebuilt library) | Apache-2.0 |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Shipped with sherpa-onnx | MIT |
| [espeak-ng](https://github.com/espeak-ng/espeak-ng) | Kokoro English phonemes (inside `sherpa-onnx-c-api`, data in the model's `espeak-ng-data`) | GPL-3.0 |
| [Qt 6](https://www.qt.io/) | UI, dynamically linked | LGPL-3.0 |
| [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) | Virtual audio cable on Windows (installed by the user, not distributed) | Donationware |

Model sources and licenses are listed under [Models and download links](#models-and-download-links); the full list is in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Feedback

Issues and pull requests are welcome. When reporting a recognition or injection problem, include your OS version, CPU model, the shortcut you use, the target app and any relevant logs. Please don't attach private recordings or any credentials.
