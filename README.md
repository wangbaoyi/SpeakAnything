# SpeakAnything

![Windows](https://img.shields.io/badge/platform-Windows-0078D4?logo=windows&logoColor=white)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt 6](https://img.shields.io/badge/UI-Qt%206-41CD52?logo=qt&logoColor=white)
![Offline](https://img.shields.io/badge/inference-offline-2E7D32)

轻量、离线、原生 Windows 语音输入与同声翻译工具：说话写进光标，也可以把翻译念给通话对方听。

SpeakAnything 使用 SenseVoiceSmall Q8 GGUF 作为底层识别模型，配合 FSMN-VAD 实现实时分句、临时结果更新和最终结果提交。麦克风音频和识别结果都留在本机，不依赖云端 API。

## Highlights

- **离线优先**：默认模型和所有处理都在本机运行。
- **轻量模型**：SenseVoiceSmall Q8 约 242 MiB，适合桌面常驻。
- **实时体验**：VAD 负责断句，识别线程只处理最新快照，避免结果堆积。
- **直接输入**：通过 Windows TSF 和 UI Automation 写入当前光标，不依赖剪贴板粘贴。
- **可调参数**：快捷键、VAD 门限、句尾静音、最低响度、SNR、热词和文本整理模式均可配置。
- **低打扰浮窗**：Qt 6 无边框界面显示文字、响度、VAD 状态、声纹和计时。
- **内存保护**：单句长度和工作集都有上限，长内容在最近的 VAD 边界自动分句。
- **音频保护**：录音期间可暂时静音默认播放设备，结束后恢复原状态。
- **离线翻译**：Opus-MT 中↔英翻译，原文和译文一起写入光标。
- **朗读给对方**：把译文用 Kokoro 合成语音，送进虚拟麦克风，通话对方直接听到翻译。

## Quick Start

### Portable ZIP

从 [Releases](../../releases) 下载 `SpeakAnything-*-windows-x64.zip`。压缩包已包含全部模型，解压后运行：

~~~powershell
.\\sensevoice-ui.exe
~~~

模型加载完成后，在任意 Windows 文本输入框中按住默认快捷键 Ctrl+Alt+Space，说话后松开即可注入到录音开始前的光标位置。程序不会自动按 Enter，避免误发送消息。

默认快捷键可以在托盘菜单的“设置”中改为 Ctrl+Win、Ctrl+Shift+Space、F8 或自定义组合键；同一页面也可以关闭“开机自动启动”。

### 朗读给通话对方（虚拟麦克风）

1. 安装免费的 [VB-Audio Virtual Cable](https://vb-audio.com/Cable/)，装完重启。
2. 托盘菜单 → 设置 → 「翻译」页开启翻译模式；「朗读」页开启朗读，朗读设备选 **CABLE Input**。
3. 在通话软件（微信、QQ、Zoom、Teams 等）里把麦克风选成 **CABLE Output**。
4. 按住快捷键说话，松开后对方会听到整段译文的语音。对方听不到你的原声。

多段译文按顺序排队朗读；托盘菜单「停止朗读」可清空队列。翻译失败时不朗读，只在浮窗提示。可选「监听」在自己的默认播放设备上同时播放。

## How It Works

SenseVoiceSmall 不是原生流式模型，项目通过短句快照实现实时体验：

~~~text
麦克风 → 音频整形 → FSMN-VAD → 短句快照 → SenseVoiceSmall → partial/final
~~~

partial 会持续更新当前预览，VAD 确认句尾后输出 final，已提交音频随即释放，下一句从新的短窗口开始。识别线程忙时只保留最新快照，不排队过时结果。

## Features

### Text processing

- 原文模式：保留模型输出；
- 精简模式：删除常见口水词、统一中文标点、合并相邻重复句；
- 热词表：标准词、别名、启用状态、命中计数和增强强度；
- 词典：支持 dict\\user.dict.utf8 补充专有名词；
- 纠正规则：支持确定性字面替换和单个 {num} 数字占位符。

### Translation

- 翻译模式：浮窗上的“译”开关、托盘菜单或设置页“翻译”均可开启，状态和方向重启后保留；
- 方向由用户选择：中→英 或 英→中，点击浮窗上的方向标签即可切换；
- 双语注入：松开快捷键后，整段原文和整段译文写在同一行，默认用 ` / ` 分隔（可在设置中修改，不会写入换行）；
- 先整理、后翻译：精简模式和热词纠正在翻译之前完成，partial 不翻译；
- 原文降级：任何一句语种与方向不符、翻译失败或超过 5 秒未完成时，整段只输入原文，并在浮窗中提示；
- 模型：Opus-MT（`opus-mt-zh-en` / `opus-mt-en-zh`）int8 量化，经 CTranslate2 离线运行。打开翻译模式时在后台加载当前方向，只保留一个方向，关闭时卸载。

### Speech (朗读)

- 模型：Kokoro 多语言 v1.1（fp32），经 sherpa-onnx 在 CPU 上离线运行；一个模型覆盖中英两种声音；
- 合成在独立进程 `sensevoice-tts.exe` 中进行，崩溃不影响识别和输入；
- 播放：WASAPI 共享模式，输出到用户选择的设备；
- 声音和语速可在设置中调整，中→英用英文声音，英→中用中文声音。

### Audio and memory controls

- 录音采集层只做衰减，不放大背景噪声；
- 目标语音 RMS 约为 -20 dBFS，峰值保护线为 -4 dBFS；
- 默认单句最长 15 秒，达到内存保护线时在最近 VAD 边界分句；
- 默认运行时内存保护线为 300 MiB；
- 录音结束、取消、失败和程序退出都会恢复播放设备原来的静音状态。

## Command Line

### Microphone

~~~powershell
.\\sensevoice-stream.exe --model .\\models\\sensevoice-small-q8.gguf --vad .\\models\\fsmn-vad.gguf --mic
~~~

### Audio file

~~~powershell
.\\sensevoice-stream.exe --model .\\models\\sensevoice-small-q8.gguf --vad .\\models\\fsmn-vad.gguf --audio 'C:\\path\\to\\recording.m4a'
~~~

Useful parameters:

| Parameter | Default | Purpose |
| --- | ---: | --- |
| --partial-ms | 450 | Target interval between partial results |
| --endpoint-silence-ms | 700 | Silence required to finalize a sentence |
| --maximum-utterance-ms | 15000 | Maximum audio budget for one utterance |
| --memory-limit-mb | 300 | Working-set protection line |
| --vad-speech-threshold | profile | FSMN speech confidence threshold |
| --vad-min-db | profile | Absolute minimum input level |
| --vad-min-snr-db | profile | Minimum signal-to-noise margin |
| --hotwords FILE | optional | UTF-8 tab-separated hotword file |
| --corrections FILE | optional | Deterministic correction rules |
| --translate zh-en\|en-zh | off | Translate each final with Opus-MT; prints `translation` and `injection` events |
| --translation-models DIR | models next to the exe | Directory holding `opus-mt-*-ct2` |

## Build

Requirements:

- Windows 10 or newer;
- Visual Studio 2022 with Desktop C++ and CMake;
- Qt 6.8 Widgets built for the same MSVC architecture;
- Git submodules initialized.

~~~powershell
git submodule update --init --recursive
cmake -S . -B build -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH='D:\\Qt\\6.8.3\\msvc2022_64'
cmake --build build --config Release --target sensevoice-ui sensevoice-stream sensevoice-tts --parallel 8
ctest --test-dir build -C Release --output-on-failure
~~~

The default build uses the CPU backend; the release path is optimized for AVX2/FMA/F16C CPUs. ggml runs without OpenMP. CTranslate2 (translation) uses MSVC OpenMP, so `vcomp140.dll` ships with the package.

Translation models are converted once on a developer machine (Python is not needed by end users):

~~~powershell
python -m pip install ctranslate2 transformers sentencepiece torch sacremoses
python tools\convert_opus_mt.py   # writes models\opus-mt-zh-en-ct2 and models\opus-mt-en-zh-ct2
~~~

sherpa-onnx v1.13.8 (win-x64, shared, MD, Release) headers and libraries are vendored under `third_party/sherpa-onnx`.

### Models

Models are not stored in git. Put them under `models/` (the packaging script copies them into the ZIP; the Release ZIP already contains them):

| Path under `models/` | Source |
| --- | --- |
| `sensevoice-small-q8.gguf` | SenseVoiceSmall (FunAudioLLM), Q8 GGUF conversion |
| `fsmn-vad.gguf` | FSMN-VAD (FunASR), GGUF conversion |
| `opus-mt-zh-en-ct2/`, `opus-mt-en-zh-ct2/` | Helsinki-NLP Opus-MT, converted with `tools/convert_opus_mt.py` |
| `kokoro-multi-lang-v1_1/` | [sherpa-onnx tts-models release](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models), `kokoro-multi-lang-v1_1.tar.bz2` |

The simplest way to get every model: download the Release ZIP and copy its `models` folder.

`translator-test` runs a real-model smoke test whenever those directories exist. Configure with `-DSENSEVOICE_WITH_TRANSLATION=OFF` to build without CTranslate2.

## Packaging

~~~powershell
.\tools\package_windows.ps1 -Version 0.2.0
.\tools\create_installer.ps1 -Version 0.2.0
~~~

Outputs:

~~~text
out\\SpeakAnything-0.2.0-windows-x64.zip
out\\SpeakAnything-0.2.0-Setup.exe
~~~

The installer is a standard Inno Setup wizard with welcome, destination, Start Menu, startup-task, progress, and finish pages. It installs per-user without administrator rights, registers an Apps & Features uninstall entry, and creates a native `unins000.exe` uninstaller. The portable ZIP keeps the PowerShell install/uninstall scripts separately.

To build the standard installer, install Inno Setup 6/7 so `ISCC.exe` is available, set `INNO_SETUP_HOME`, or pass `-CompilerPath` to `create_installer.ps1`.

## Performance Snapshot

Measured on a 20-logical-core AVX2 Windows machine with the Q8 model:

- model files: about 244 MiB;
- model load: about 80-140 ms;
- first non-empty partial: about 700 ms after speech starts;
- final VAD: about 30-40 ms;
- final SenseVoice inference: about 370-400 ms;
- real-time replay: about 13% total CPU;
- peak working set: about 289 MiB in the current lightweight text-processing profile.

These numbers are hardware-dependent engineering references, not benchmark claims.

## Project Layout

~~~text
src/                         C++20 recognition, VAD, audio and Qt UI
resources/                   Qt resources, ICO and Windows version resources
packaging/windows/            Inno Setup definition and portable scripts
tools/                       Packaging, icon generation and geometry checks
third_party/                 llama.cpp (vendored), sherpa-onnx (prebuilt), and submodules:
                             CTranslate2, SentencePiece, FunASR, cppjieba, OpenLess
docs/adr/                    Architecture decisions
GLOSSARY.md                  Domain terms (录音会话, 译文, 朗读 ...)
tests/                       Recognition, text processing and Windows injection tests
~~~

## Model and Third-Party Credits

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for every bundled component, model, and its license.

## Roadmap

- [x] Offline SenseVoiceSmall Q8 inference
- [x] FSMN-VAD endpointing and configurable thresholds
- [x] Hotword management and local text cleanup
- [x] Windows TSF/UI Automation injection
- [x] Qt floating UI and standard Inno Setup installer
- [x] Offline zh/en translation (Opus-MT)
- [x] Speak translations into a virtual microphone (Kokoro)
- [ ] macOS audio capture and text insertion
- [ ] Optional FP16 performance profile for supported hardware

## Contributing

Issues and pull requests are welcome. Please include the Windows version, CPU architecture, model quantization, audio characteristics, and relevant logs when reporting recognition or injection problems. Do not attach private recordings or credentials.

## License

[GNU AGPL-3.0](LICENSE). The Windows input method builds on OpenLess (AGPL-3.0) and speech synthesis uses espeak-ng (GPL-3.0), so the project as a whole is distributed under AGPL-3.0.
