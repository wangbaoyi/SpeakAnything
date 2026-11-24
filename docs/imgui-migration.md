# ImGui Interface Migration

The interface is moving from Qt Widgets to Dear ImGui. The existing Qt target
stays available while the immediate-mode interface reaches feature parity.

Visual language follows WhaleTalk's dark UI: near-black canvas `#090B11`,
translucent panels `#171B26`, hairline borders, 14 px rounded cards, pink accent
`#FF6F9C`/`#FB4D86`, and two-column card grids for settings pages.

Themes: 默认 (WhaleTalk dark/pink), 亮色, 暗色 (blue accent), 高对比度 and
自定义. Selecting a theme in 外观 applies it immediately to both the settings
window and the overlay, and saves it to QSettings. 自定义 derives all surfaces
and borders from the user's background/text/accent colors.

## Current state

Implemented in `sensevoice-ui-imgui`:

- Win32 window with the DX11 backend and a Microsoft YaHei font atlas.
- Eight settings pages: 输入、输出、外观、翻译、朗读、热词、模型、VAD.
- Settings load/save through the existing `sa::settings` model and
  `hotwords.tsv`, so the ImGui app shares the same storage as the Qt app.
- Hotword table editing, appearance colors/font/linger, VAD sliders and the
  translation/speech controls.
- Frameless, transparent, always-on-top overlay rendered with a second ImGui
  context and a layered Win32 window (color-key transparency).
- Tray icon and native popup menu (打开设置 / 显示浮窗 / 翻译模式 / 退出).
- Global `Ctrl+Alt+Space` hotkey to show or hide the overlay.
- Recording controller: microphone capture, FSMN VAD, SenseVoice streaming
  recognition, text cleaning/hotwords and text injection through the existing
  Windows text injector. The overlay shows live level, partial text and final
  text; `Ctrl+Alt+Space` starts and stops recording.
- Translation runtime through `TranslationWorker` (Opus-MT for zh↔en, Qwen for
  other directions), with bilingual composition and original-text fallback on
  timeout or failure.
- Overlay actions: clicking the “翻译” chip toggles translation mode and saves
  the setting; clicking × stops the session and hides the overlay.
- Model page: directory picker / restore default, per-kind model selection,
  download/cancel with progress, browser download, import via native Windows
  file/folder dialogs, and remove/remove-import. Choices are written to
  `QSettings` immediately.
- Models live under `<executable>/models` (`recognizer/`, `translator/`,
  `voice/`). `ModelManager` also searches a sibling `modelspt/` so existing
  installs keep working without moving files.
- The selected recognizer (SenseVoice or Whisper) is resolved through
  `ModelManager` and loaded by the recording controller; the selected Qwen GGUF
  is passed to the translation factory.
- Speech playback uses the existing `SpeechQueue` + `sensevoice-tts.exe`: the
  selected Kokoro/Piper voice model, output device, monitor flag, voice and
  speed from the 朗读 page are applied. Live mode speaks each settled sentence;
  otherwise the final translation is spoken after injection.

Still Qt-only:

- Message boxes and the existing Qt translation catalog.

## Build and run

```powershell
cmake -S C:\sa\SpeakAnything -B C:\sa\SpeakAnything\build-verify
cmake --build C:\sa\SpeakAnything\build-verify --config Release --target sensevoice-ui-imgui
$env:PATH = "E:\Programming environment\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
& C:\sa\SpeakAnything\build-verify\Release\sensevoice-ui-imgui.exe
```

Smoke test (renders a fixed number of frames and exits):

```powershell
$env:SPEAKANYTHING_IMGUI_SMOKE = '120'
& C:\sa\SpeakAnything\build-verify\Release\sensevoice-ui-imgui.exe
```

Diagnostics:

| Variable | Effect |
| --- | --- |
| `SPEAKANYTHING_IMGUI_SHOW_OVERLAY=1` | Show the overlay at startup |
| `SPEAKANYTHING_IMGUI_AUTOSTART=1` | Start recording after 2.5 s |
| `SPEAKANYTHING_IMGUI_AUDIO=<wav>` | Feed a WAV file instead of the microphone |
| `SPEAKANYTHING_IMGUI_TRANSLATE=1` | Force translation mode on for this run |
| `SPEAKANYTHING_IMGUI_TRANSCRIPT=1` | Append recognized/translated text to `imgui-transcript.log` |
| `SPEAKANYTHING_IMGUI_PAGE=<0-7>` | Open the settings window on a specific page |
| `SPEAKANYTHING_IMGUI_SPEAK=1` | Force speech playback on for this run |
| `SPEAKANYTHING_IMGUI_THEME=light\|dark\|high\|custom` | Preview a theme for this run |

## Layout

| Path | Purpose |
| --- | --- |
| `third_party/imgui` | Dear ImGui 1.91.9b core and Win32/DX11 backends |
| `src/imgui_ui/main_imgui.cpp` | Win32 window, D3D11 device, font and frame loop |
| `src/imgui_ui/overlay_window.*` | Layered transparent overlay and its ImGui context |
| `src/imgui_ui/recording_controller.*` | Microphone, VAD, streaming ASR and text injection |
| `src/imgui_ui/settings_ui.*` | Sidebar and the eight settings pages |
| `src/imgui_ui/settings_io.*` | QSettings/hotwords load and save |

## Next steps

1. Move runtime translations to an ImGui string table, then retire Qt Widgets.
2. Add Qt-free message boxes and finish macOS parity.
