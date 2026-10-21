# SpeakAnything

**中文** | [English](README.en.md)

![Windows](https://img.shields.io/badge/platform-Windows-0078D4?logo=windows&logoColor=white)
![macOS](https://img.shields.io/badge/platform-macOS%20(preview)-000000?logo=apple&logoColor=white)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt 6](https://img.shields.io/badge/UI-Qt%206-41CD52?logo=qt&logoColor=white)
![Offline](https://img.shields.io/badge/inference-offline-2E7D32)
![License](https://img.shields.io/badge/license-AGPL--3.0-blue)

轻量、离线、原生 Windows 语音输入与同声翻译工具：按住快捷键说话，文字写进当前光标；打开翻译后，译文还可以合成语音送进虚拟麦克风，让通话对方直接听到。

所有识别、翻译和语音合成都在本机完成，不依赖任何云端 API；识别和翻译可在设置中选择 CPU 或 GPU，语音合成在 CPU 上运行。

- 仓库：<https://github.com/wangbaoyi/SpeakAnything>
- 下载：<https://github.com/wangbaoyi/SpeakAnything/releases>（便携 ZIP，已包含全部模型）

界面语言跟随系统（中文或英文），可在托盘菜单 → **Language / 语言** 中切换，重启后生效。

---

## 目录

1. [功能概览](#功能概览)
2. [快速开始](#快速开始)
3. [朗读给通话对方（虚拟麦克风）](#朗读给通话对方虚拟麦克风)
4. [macOS（预览）](#macos预览)
5. [模型与下载链接](#模型与下载链接)
6. [系统架构](#系统架构)
7. [源码结构](#源码结构)
8. [设置项](#设置项)
9. [命令行工具](#命令行工具)
10. [编译](#编译)
11. [打包与发布](#打包与发布)
12. [测试](#测试)
13. [性能参考](#性能参考)
14. [设计文档](#设计文档)
15. [第三方组件与许可证](#第三方组件与许可证)

---

## 功能概览

| 能力 | 说明 |
| --- | --- |
| 离线语音识别 | SenseVoiceSmall Q8，中英混合，FSMN-VAD 实时断句，临时结果（Partial）在浮窗预览 |
| 直接输入 | 通过 Windows TSF 和 UI Automation 写入光标；不支持时等待"粘贴延迟"后用 Ctrl+V 兜底 |
| 文本整理 | 原文模式 / 精简模式（去口水词、统一标点、合并重复句），热词纠正与增强 |
| 离线翻译 | 中英互译在 CPU 上用 Opus-MT（CTranslate2 int8），在 GPU 上用 Opus-MT CUDA（CTranslate2 FP32，约 80 ms/句）；其他语言用 Qwen GGUF（llama.cpp，所有层卸载到显卡）；原文和译文一起写入光标 |
| 朗读 | 译文经 Kokoro 合成语音，播放到用户选择的设备（如 VB-Cable），通话对方直接听到 |
| 原文降级 | 翻译失败、超时（5 秒）或语种与方向不符时只输入原文，并在浮窗提示 |
| 触发方式 | "按住说话"、"按一下切换"（切换模式最长 5 分钟）或"实时模式"（按一下开始，边说边输出，再按一下结束，直到你主动关闭） |
| 外观 | 胶囊 / 面板 / 圆环三种浮窗样式，多种配色主题，停留时长，常驻模式 |
| 音频保护 | 录音期间暂时静音默认播放设备，结束后恢复原状态 |
| 内存保护 | 单句最长 15 秒，工作集保护线 300 MiB，长内容在最近的 VAD 边界自动分句 |

领域术语（录音会话、原文、译文、朗读队列等）的准确定义见 [GLOSSARY.md](GLOSSARY.md)。

## 快速开始

1. 从 [Releases](https://github.com/wangbaoyi/SpeakAnything/releases) 下载 `SpeakAnything-*-windows-x64.zip`（约 740 MB，已含全部模型）。
2. 解压到任意目录，运行 `sensevoice-ui.exe`。程序以托盘图标常驻。
3. 模型加载完成后，在任意文本输入框中按住 **Ctrl+Alt+Space** 说话，松开后文字写入录音开始前的光标位置。程序不会自动按 Enter，避免误发送消息。
4. 托盘菜单 → **设置** 可修改快捷键、触发方式、输出、外观、翻译、朗读、热词和 VAD 参数。

可选：运行 `install.cmd` 安装到当前用户目录并注册开机启动（不需要管理员权限），`uninstall.ps1` 卸载。

### 实时模式（边说边输出）

在 设置 → 输入 → 触发方式 中选择「实时模式：按一下开始，边说边输出，再按一下结束」。

- 按一下快捷键开始持续监听，不需要一直按住。说完一句后停顿约 1 秒，这句话就会立即整理、翻译（开启翻译时）并投递，然后浮窗和本句记录清空，你继续说下一句。
- Partial 结果只在浮窗预览，不会被投递。
- 再按一下快捷键结束，或按 Esc 取消后续投递。
- 结果去向为「输入到光标」时，每句依次输入到会话开始时记录的光标位置；为「只复制」时，剪贴板在每句结束后更新为累计文本。
- 开启翻译时，每句按「剪贴板内容」设置投递：选「原文」时投原文；选「译文」时投译文，某一句翻译失败时这一句投原文；选「两者」时投原文、配置的翻译分隔符和译文。相邻两句只有两端都是 ASCII 字符时才用空格连接，否则直接相连。实时模式下剪贴板始终保存最近一句的译文（该句没有译文时保存原文），每句覆盖上一句，不再累计整场会话。
- 开启朗读且已安装声音模型时，每句译文在稳定后立即朗读，不需要另外打开「边说边朗读」。
- 实时模式会一直保持麦克风开启，直到你再次按快捷键或按 Esc。按住和切换模式的行为不变。
- 实时模式不会自动静音扬声器；按住和切换模式可以在 设置 → 输入 → 音频 中选择「录音时静音系统播放」。
- 剪贴板保留时间在 设置 → 输出 → 剪贴板保留 中设置，0 表示不自动清除；实时模式每收到一句新译文都会重新计时。

## 朗读给通话对方（虚拟麦克风）

```
你说话 → 识别 → 原文 → 翻译 → 译文 → Kokoro 合成语音 → CABLE Input ══ 虚拟声卡 ══ CABLE Output → 通话软件的麦克风 → 对方
```

1. 安装免费的 [VB-Audio Virtual Cable](https://vb-audio.com/Cable/)，装完重启电脑。
2. 托盘菜单 → 设置 → **翻译** 页：开启翻译模式，选择翻译方向。
3. **朗读** 页：开启朗读，朗读设备选 **CABLE Input**，按需选择声音和语速，点应用。
4. 在通话软件（微信、QQ、Zoom、Teams 等）里把麦克风选成 **CABLE Output**。
5. 按住快捷键说话，松开后对方会听到整段译文的语音。对方听不到你的原声。

行为规则：

- 朗读只念 **译文**，翻译失败时不念（绝不念原文），只在浮窗提示"未朗读"。
- 多段译文进入 **朗读队列**，按顺序念完，不互相打断。托盘菜单"停止朗读"可清空队列。
- 朗读设备不可用时提示错误，不会改用其他设备（否则声音会从自己的扬声器外放，对方反而听不到）。
- 可选 **监听**：同时在自己的默认播放设备上播放；录音期间自动静音，避免被重新录进去。
- 中→英时用英文声音，英→中时用中文声音。

## macOS（预览）

macOS 版与 Windows 版共用同一个 CMake 工程，仍在移植中。目前已完成：

| 部分 | macOS 实现 |
| --- | --- |
| 语音识别 | 启动时自动选最快的（托盘提示中显示）：macOS 15+ 用 Core ML 版本跑在 **神经网络引擎** 上；否则用与 Windows 相同的 Q8 GGUF，经 Metal 跑在 **GPU** 上；都不行才用 CPU |
| 录音时静音 | macOS 上默认关闭；托盘菜单 →「录音时静音系统声音」 |
| 句尾静音 | macOS 上为 1200 ms（Windows 700 ms），自然停顿不会把一句话拆开 |
| VAD | FSMN-VAD 仍在 CPU 上（模型只有 1.7 MB，交给 NPU 的调度开销比计算本身还大） |
| 翻译 | **Qwen3-1.7B（Q4_K_M）通过 llama.cpp Metal 后端在 GPU 上运行**，一个模型兼顾两个方向 |
| 语音合成 | Kokoro 经 sherpa-onnx 在 CPU 上运行；实测 ONNX Runtime 的 Core ML 后端反而更慢 |
| 全局快捷键 | Carbon `RegisterEventHotKey`（不需要"输入监控"权限）；默认 **⌃⌥Space**，因为 ⌘⌥Space 是访达搜索 |
| 输入到光标 | 辅助功能 API（`kAXSelectedTextAttribute`），失败时用 ⌘V 粘贴兜底 |
| 录音期间静音 | CoreAudio 默认输出静音 |
| 朗读播放 | miniaudio（CoreAudio），设备以 CoreAudio UID 标识 |
| 登录时启动 | `SMAppService`（macOS 13+） |
| 虚拟麦克风 | 自带 CoreAudio HAL 插件 **SpeakAnything Virtual Mic**，取代 VB-Cable |
| 应用包 | `SpeakAnything.app`，只显示在菜单栏（`LSUIElement`）；模型放在 `Contents/Resources/models` |

权限：首次启动时 macOS 会请求 **麦克风** 权限，SpeakAnything 还会请求 **辅助功能** 权限（系统设置 → 隐私与安全性 → 辅助功能），用于向其他程序输入文字。未授权时文字仍会复制到剪贴板。

M4（macOS 15.7）实测（热启动）：识别模型加载 0.36 秒（首次启动需编译一次，约 15 秒）；一句 4 到 7 秒的语音 NPU 识别 14 到 33 ms，GPU 57 到 83 ms，CPU 110 到 180 ms；GPU 翻译一句 0.3 到 0.6 秒；Kokoro 合成实时率 0.23 到 0.26。

NPU 识别模型需要 **macOS 15**。打包：`tools/package_macos.sh 0.3.0` 生成包含全部模型和虚拟麦克风的 DMG（约 1.7 GB）。macOS 模型来源与编译方法见 [英文文档](README.en.md#building-on-macos)。尚未完成：公证（需要 Developer ID）、Kokoro 上 NPU。

### macOS 虚拟麦克风

`packaging/macos/virtual-mic` 编译出 `SpeakAnythingMic.driver`，这是一个用户态 AudioServerPlugIn（不是内核扩展）。它提供一个 48 kHz 立体声设备：播放到它输出端的声音会从它的输入端出来。

1. 编译（macOS 构建会一起编译）后安装，需要管理员密码，并会短暂重启 `coreaudiod`：`packaging/macos/install-virtual-mic.sh`
2. SpeakAnything → 设置 → **朗读**：朗读设备选 **SpeakAnything Virtual Mic**。
3. 通话软件里把麦克风也选成 **SpeakAnything Virtual Mic**。

卸载：`packaging/macos/install-virtual-mic.sh --uninstall`。该设备不会自动成为系统默认输出。

## 多语言、边说边朗读与取消

- **语言**：设置 → 翻译 → 「我说的语言」「翻译成」可选 28 种语言（含西班牙语、德语、法语、保加利亚语、俄语等）。中英互译在 GPU 上优先使用 Opus-MT CUDA，其他语言使用 Qwen3（macOS Metal / Windows CUDA）；CPU 上的 Opus-MT 仍只支持中英互译，其他方向在 CPU 上回退到 Qwen。
- **设备**：识别和翻译设备在设置里分开选择，互不影响。翻译默认「自动」：中英互译优先 Opus-MT CUDA，其他语言优先 GPU Qwen；显式选择「仅 GPU」但不可用时加载失败，不会悄悄改用 CPU。
- **识别**：SenseVoice 支持中、英、粤、日、韩；其他语言需要在 设置 → **模型** 中下载 Whisper（Small 或 Large v3 Turbo）。macOS 上 Whisper Small 的编码器运行在神经网络引擎上。
- **声音**：Kokoro 说中英文；其他 21 种语言可下载 Piper 声音（暂无保加利亚语声音）。
- **模型页**：为当前语言选择识别、翻译和朗读模型，或选「自动」。应用只内置小模型，其余下载到 `~/Library/Application Support/SpeakAnything/models`。
- **边说边朗读**（设置 → 朗读）：每次停顿后，前面的句子立即翻译并念出，不等松开快捷键；前文作为上下文传给翻译模型。
- **Esc**：按住快捷键时（或收尾时）按 Esc 放弃本次输入，不输入、不复制、不朗读，并清空朗读队列。
- **笑声**：SenseVoice 识别为事件的笑声会保留为「哈哈 / haha」，翻译时也保留，朗读能念出来。
- **不翻译**：在热词表中勾选「不翻译」即可原样保留，例如「Claude Code」，别名填 `cloud code` 还能纠正识别。

## 模型与下载链接

模型不存放在 git 仓库中。Release ZIP 已包含全部模型；从源码编译时，把它们放到可执行文件旁边的 `modelspt/` 目录（打包脚本会检查并复制）。程序仍会兼容旧的 `models/` 目录。

已经下载到其他位置的模型不需要复制：在 设置 → 模型 里可以：

- 点「选择目录...」指定一个额外的模型目录；程序会在 `modelspt`、自定义目录和下载目录中按顺序查找。
- 在某一类模型上点「导入...」，直接选择单个模型文件（`.gguf` / `.bin`）或模型文件夹（Kokoro、Piper）。导入记录会保存在设置里，点「移除导入」只是停止使用，不会删除你的文件。

`modelspt` 可写时，应用内下载的模型也直接保存到这里；如果程序装在只读目录（例如 Program Files），下载会自动回退到用户目录 `%LOCALAPPDATA%\SpeakAnything\models`。

推荐按用途分类存放，程序会自动识别这些子目录（旧的平铺结构仍然兼容）：

```
modelspt/
  recognizer/   SenseVoice、Whisper、FSMN-VAD
  translator/   Opus-MT、Qwen GGUF
  voice/        Kokoro、Piper
```

| 用途 | `modelspt/` 下的路径 | 大小 | 来源 | 许可证 |
| --- | --- | ---: | --- | --- |
| 语音识别 | `recognizer/sensevoice-small-q8.gguf` | 243 MB | [FunAudioLLM/SenseVoiceSmall-GGUF](https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF)（文件 `sensevoice-small-q8.gguf`） | Apache-2.0 |
| 语音端点检测 | `recognizer/fsmn-vad.gguf` | 1.7 MB | [FunAudioLLM/fsmn-vad-GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF)（文件 `fsmn-vad.gguf`） | Apache-2.0 |
| 翻译 中→英 | `translator/opus-mt-zh-en-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en)，用 `tools/convert_opus_mt.py` 转换为 CTranslate2 int8 | CC-BY-4.0 |
| 翻译 英→中 | `translator/opus-mt-en-zh-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)，同上转换 | Apache-2.0 |
| 语音合成 | `voice/kokoro-multi-lang-v1_1/` | 401 MB | [sherpa-onnx tts-models](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) 中的 [`kokoro-multi-lang-v1_1.tar.bz2`](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2)；原始模型 [hexgrad/Kokoro-82M-v1.1-zh](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh) | Apache-2.0 |
| 中文分词词典 | `dict/`（程序目录下） | | [cppjieba/dict](https://github.com/yanyiwu/cppjieba/tree/master/dict) 的 `jieba.dict.utf8`、`user.dict.utf8` | MIT |

上游参考：SenseVoice 原始模型 [FunAudioLLM/SenseVoiceSmall](https://huggingface.co/FunAudioLLM/SenseVoiceSmall)，GGUF 转换与运行时 [FunASR runtime/llama.cpp](https://github.com/modelscope/FunASR/tree/main/runtime/llama.cpp)。

### 获取模型

最简单：下载 Release ZIP，把其中的 `modelspt` 文件夹复制到仓库根目录。

或者逐个获取：

```powershell
# 识别与 VAD（需要 pip install -U huggingface_hub）
huggingface-cli download FunAudioLLM/SenseVoiceSmall-GGUF sensevoice-small-q8.gguf --local-dir modelspt
huggingface-cli download FunAudioLLM/fsmn-vad-GGUF fsmn-vad.gguf --local-dir modelspt

# 翻译：下载并转换为 CTranslate2 int8（只在开发机上做一次）
python -m pip install ctranslate2 transformers sentencepiece torch sacremoses
python tools\convert_opus_mt.py   # 生成 modelspt\translator\opus-mt-zh-en-ct2 和 modelspt\translator\opus-mt-en-zh-ct2

# 语音合成
curl -L -o kokoro.tar.bz2 https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2
tar xjf kokoro.tar.bz2 -C modelspt
```

> 注意：Kokoro 必须用 **fp32 版本**（`kokoro-multi-lang-v1_1`），不要用 `kokoro-int8-multi-lang-v1_1`。在 Ryzen 5 5600 上实测 int8 版本比实时还慢（实时率 1.35 到 2.2），fp32 版本 4 线程实时率约 0.34 到 0.43。详见 [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md)。

### Kokoro 声音编号

`voices.bin` 中共 103 个声音，设置页按编号选择：

| 编号 | 声音 |
| --- | --- |
| 0 | 美式女声 af_maple（英文默认） |
| 1 | 美式女声 af_sol |
| 2 | 英式女声 bf_vale |
| 3 到 57 | 中文女声 zf_001 到 zf_099（共 55 个，3 = zf_001 为中文默认） |
| 58 到 102 | 中文男声 zm_009 到 zm_100（共 45 个） |

## 系统架构

### 识别与输入流程

```
麦克风 (WASAPI) → 音频整形 → 音频快照 ─┬→ FSMN-VAD（独立线程，取最新快照）
                                       └→ SenseVoiceSmall → Partial / Final 结果
                                                                       │
                                                         文本整理（精简模式、热词纠正）
                                                                       │
                                                                     原文
                                                                       │
                                                  翻译模式开启时：Opus-MT (CTranslate2) → 译文
                                                                       │
                          按住/切换模式：松开时整段输出；实时模式：每条 Final 结果立即输出
```

SenseVoiceSmall 不是原生流式模型，项目用短句快照实现实时体验：Partial 结果持续更新浮窗预览；VAD 确认句尾后提交 Final 结果，已提交的音频随即释放。FSMN-VAD 在独立线程上评估最新音频快照，与识别推理重叠进行；识别线程只消费最新的完整 VAD 结果，并用音频版本号丢弃裁剪前的过期结果。识别线程忙时只保留最新快照，不排队过时结果。设置环境变量 `SENSEVOICE_SERIAL_VAD=1` 可退回 VAD 先于识别的串行路径，用于对比基线和排查问题。Partial 结果永远不会被翻译。按住和切换模式在松开时整段投递；实时模式下每条 Final 结果一稳定就立即整理、翻译并投递，不再等待快捷键松开。

### 进程与线程

| 组件 | 运行位置 | 说明 |
| --- | --- | --- |
| `sensevoice-ui.exe` | 主进程 | Qt 界面、快捷键、识别、翻译、注入、朗读队列 |
| 识别 | 主进程后台线程 | ggml CPU 或 CUDA 后端，由计算设备设置选择，不使用 OpenMP |
| VAD | 主进程后台线程 | FSMN-VAD 独占实例，默认与识别推理重叠，4 线程 |
| 翻译 | 主进程后台线程（`TranslationWorker`） | 同时只加载一个方向的模型，关闭翻译模式时卸载 |
| 语音合成 | **独立进程** `sensevoice-tts.exe` | 通过 stdin/stdout 管道通信；崩溃不影响识别和输入 |
| 播放 | 主进程，每个输出设备一个线程（`SpeechPlayer`） | WASAPI 共享模式，由 Windows 自动做采样率转换 |

### 语音合成进程协议

`sensevoice-tts.exe <模型目录> [线程数]`，stdin/stdout 为二进制模式，一次处理一个请求：

| 方向 | 内容 |
| --- | --- |
| 启动后输出 | `READY <采样率>\n`，或 `ERROR <信息>\n` 后退出 |
| 请求（stdin） | `<声音编号>\t<语速>\t<UTF-8 文本>\n` |
| 成功响应 | `AUDIO <采样数>\n` 后跟采样数个 float32 采样（单声道，24 kHz） |
| 失败响应 | `ERROR <信息>\n` |

stdin 关闭时进程退出。进程会先切换到模型目录，再用相对路径加载文件：sherpa-onnx 和 espeak-ng 用窄字符路径打开文件，安装在中文目录下时会失败。

为什么放在独立进程：Kokoro 念英文时依赖 espeak-ng（GPL-3.0）做音素转换，独立进程划清了许可边界，同时隔离崩溃。详见 [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md)。

## 源码结构

```
src/
  qt_ui_main.cpp            Qt 主程序：浮窗、托盘、设置对话框、录音会话、翻译与朗读的调度
  stream_recognizer.*       流式识别：VAD 断句、Partial/Final 结果、内存保护
  sensevoice_engine.*       SenseVoiceSmall GGUF 推理
  fsmn_vad_engine.*         FSMN-VAD GGUF 推理
  audio_io.*                麦克风采集与音频文件读取（Media Foundation）
  audio_conditioner.*       录音电平整形（只衰减，不放大噪声）
  stability_tracker.*       Partial 结果稳定性跟踪
  text_processor.*          文本整理、热词、纠正规则、jieba 分词
  hotword_boost.*           CTC 热词增强
  translator.h              翻译接口与 TranslationWorker
  opus_mt_translator.cpp    Opus-MT 经 CTranslate2 的实现
  translation_session.*     一次录音会话内的逐句翻译、语种判断与原文降级
  translation_worker.cpp    翻译后台线程
  speech_queue.*            朗读队列：管理 sensevoice-tts.exe、排队、分发音频
  speech_player.*           WASAPI 播放与输出设备枚举
  tts_helper_main.cpp       sensevoice-tts.exe 入口
  windows_text_injector.*   TSF / UI Automation 注入与 Ctrl+V 粘贴兜底
  system_audio_mute.*       录音期间静音默认播放设备
  main.cpp                  命令行工具 sensevoice-stream.exe
tests/                      单元测试与 Windows 注入测试目标程序
tools/                      打包、安装包、模型转换、图标生成、浮窗几何检查
packaging/windows/          Inno Setup 定义与便携版安装 / 卸载脚本
resources/                  图标与 Qt 资源
docs/adr/                   架构决策记录
third_party/                第三方依赖（见下文）
modelspt/                   本地模型（不进 git）
```

## 设置项

设置保存在注册表 `HKCU\Software\SenseVoice\LocalDictation`（QSettings）。

| 键 | 含义 | 默认值 |
| --- | --- | --- |
| `hotkey/shortcut` | 快捷键 | `Ctrl+Alt+Space` |
| `hotkey/trigger` | `hold` 按住说话 / `toggle` 按一下切换 / `realtime` 实时模式（边说边输出） | `hold` |
| `text/mode` | 0 原文 / 1 精简 | 1 |
| `output/destination` | `insert` 输入到光标 / `copy` 只复制 | `insert` |
| `output/content` | 浮窗内容：原文 / 译文 / 两者 | 原文 |
| `output/clipboard_content` | 剪贴板内容（也是输入到光标的内容） | 原文 |
| `output/paste_delay_ms` | Ctrl+V 兜底前的等待 | 150 |
| `output/clipboard_ttl_seconds` | 写入剪贴板后多久自动清除，0 表示不清除 | 0 |
| `models/custom_directory` | 额外的模型搜索目录（为空时只用 modelspt） | 空 |
| `models/custom_paths/<模型 id>` | 单个模型导入的文件或文件夹路径 | 空 |
| `translation/enabled` | 翻译模式 | false |
| `translation/direction` | `zh-en` / `en-zh` | `zh-en` |
| `translation/separator` | 原文与译文之间的分隔符 | ` / ` |
| `speech/enabled` | 朗读（需要翻译模式） | false |
| `speech/device_id` | 朗读设备的 Windows 端点 ID | 空 |
| `speech/monitor` | 监听 | false |
| `speech/voice_en`、`speech/voice_zh` | 英文、中文声音编号 | 0、3 |
| `speech/speed_percent` | 语速（50 到 200） | 100 |
| `vad/*` | 句尾静音、模型阈值、最低响度、底噪余量 | 700 ms、0.55、-60 dBFS、3 dB |
| `appearance/*` | 配色主题、字体、停留时长、常驻模式 | |

热词保存在程序目录下的 `hotwords.tsv`。

## 命令行工具

`sensevoice-stream.exe` 用于调试识别和翻译，输出 JSON 事件。

```powershell
# 麦克风
.\sensevoice-stream.exe --model .\modelspt\sensevoice-small-q8.gguf --vad .\modelspt\fsmn-vad.gguf --mic

# 音频文件，并翻译成英文
.\sensevoice-stream.exe --model .\modelspt\sensevoice-small-q8.gguf --vad .\modelspt\fsmn-vad.gguf --audio C:\path\to\recording.m4a --translate zh-en
```

| 参数 | 默认值 | 作用 |
| --- | ---: | --- |
| `--partial-ms` | 450 | Partial 结果的目标间隔 |
| `--endpoint-silence-ms` | 700 | 判定句尾所需的静音 |
| `--maximum-utterance-ms` | 15000 | 单句最长音频 |
| `--memory-limit-mb` | 300 | 工作集保护线 |
| `--vad-threads` | `min(4, --threads)` | FSMN-VAD 的 CPU 线程数；与识别重叠时保持低于识别线程数 |
| `--threads` | 8 | 识别所用的 ggml CPU 线程数 |
| `--vad-speech-threshold` | 预设 | FSMN 语音置信度阈值 |
| `--vad-min-db` | 预设 | 最低输入电平 |
| `--vad-min-snr-db` | 预设 | 最低信噪比余量 |
| `--hotwords FILE` | 可选 | UTF-8 制表符分隔的热词文件 |
| `--corrections FILE` | 可选 | 确定性纠正规则 |
| `--translate zh-en\|en-zh` | 关闭 | 翻译每条 Final 结果，输出 `translation` 和 `injection` 事件 |
| `--translation-device auto\|cpu\|gpu` | 跟随 `--device` | 翻译使用的设备策略，与识别设备分开 |
| `--translation-models DIR` | 程序旁的 `models` | 含 `translator/`（Opus-MT 目录、Qwen GGUF）的模型根目录 |
| `--translation-model FILE` | 自动查找 | 指定 Qwen GGUF；缺省时在 `translator/` 中不区分大小写查找 |

每条 `partial` 和 `final` 事件都带 `vad_ms`、`inference_ms` 和 `final_latency_ms`。最后一项表示从最近一次 `accept_pcm` 到 Final 事件的耗时。设置 `SENSEVOICE_SERIAL_VAD=1` 可强制退回 VAD 先于识别的串行路径；默认让两个线程重叠。

SenseVoice 和 FSMN-VAD 共用同一份 80 维 fbank 前端，独立音频帧默认分给 `min(4, 引擎线程数)` 个线程计算，少于 32 帧（320 ms）时改为串行。设置 `SENSEVOICE_SERIAL_FBANK=1` 可强制串行路径，用于和默认并行路径对比；命令行启动时会打印 `fbank: serial|parallel feature extraction`。

## 编译

环境要求：

- Windows 10 或更新版本，x64；
- Visual Studio 2022（"使用 C++ 的桌面开发"）或 Build Tools 2022，CMake 3.20+；
- Qt 6.8 Widgets（MSVC 2022 64 位）；
- Git（用于子模块）。

```powershell
git clone https://github.com/wangbaoyi/SpeakAnything.git
cd SpeakAnything
git submodule update --init --recursive
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="E:\Qt\6.8.3\msvc2022_64"
cmake --build build --config Release --target sensevoice-ui sensevoice-stream sensevoice-tts --parallel 8
```

产物在 `build\Release\`。运行前把模型放到 `build\Release\modelspt\`，分词词典放到 `build\Release\dict\`。

编译说明：

- 默认使用 CPU 后端，发布构建针对支持 AVX2/FMA/F16C 的 CPU 优化。
- ggml 不使用 OpenMP；CTranslate2 使用 MSVC OpenMP，发布包需要附带 `vcomp140.dll`（不带 OpenMP 的 CTranslate2 在 Windows 上会在进程退出时卡死）。
- CTranslate2 的矩阵后端是 Ruy，不依赖 MKL 或 oneDNN。Ruy 需要嵌套子模块 cpuinfo，所以克隆时必须加 `--recursive`。
- SentencePiece 固定在 v0.2.0，并以 `UNICODE` 编译，否则中文路径下加载模型会静默失败。
- 翻译有独立的设备策略（设置里的「翻译设备」或 `--translation-device`，默认 `auto`）。中英互译：`cpu` 用 Opus-MT CTranslate2 INT8，`gpu` 用 Opus-MT CTranslate2 CUDA FP32（需 `-DSENSEVOICE_CT2_CUDA=ON`），`auto` 在 CUDA 可用时选 GPU。其他语言始终用 Qwen GGUF：`gpu` 卸载所有层，`cpu` 用 CPU 推理。显式 `gpu` 不可用时加载失败，不会回退到 CPU。
- `SENSEVOICE_CT2_CUDA` 默认在检测到 CUDA toolkit 时开启，使用 `CUDA_ARCH_LIST=6.1` 和 `CUDA_DYNAMIC_LOADING=ON`。CTranslate2 4.8 仍依赖 CMake 的 `FindCUDA`；配置时会把 `CMP0146` 设为 `OLD`，因此 CMake 4.4 也能构建 CUDA 翻译路径。
- `-DSENSEVOICE_WITH_TRANSLATION=OFF` 关闭 Opus-MT；`-DSENSEVOICE_WITH_LLM_TRANSLATION=OFF` 关闭 Qwen。两者可同时编译进同一个程序。
- sherpa-onnx v1.13.8（win-x64、shared、MD、Release）的头文件和库已预置在 `third_party/sherpa-onnx`，编译时自动复制 DLL 到输出目录。

## 打包与发布

```powershell
.\tools\package_windows.ps1 -Version 0.2.0      # 便携 ZIP
.\tools\create_installer.ps1 -Version 0.2.0     # Inno Setup 安装包（需要 Inno Setup 6/7）
```

输出：

```
out\SpeakAnything-0.2.0-windows-x64.zip
out\SpeakAnything-0.2.0-Setup.exe
```

`package_windows.ps1` 会：编译三个程序，用 `windeployqt` 收集 Qt 运行库，复制 `vcomp140.dll`、sherpa-onnx 与 ONNX Runtime 的 DLL、分词词典、`modelspt/` 下的全部模型、LICENSE 和第三方声明，然后压缩。缺少任何模型时直接报错。

发布包目录结构：

```
sensevoice-ui.exe          主程序
sensevoice-tts.exe         语音合成进程
sensevoice-stream.exe      命令行工具
sherpa-onnx-c-api.dll, onnxruntime*.dll, vcomp140.dll, Qt6*.dll
dict\                      分词词典
modelspt\                   全部模型
install.cmd / install.ps1 / uninstall.ps1 / README.txt / LICENSE / THIRD_PARTY_NOTICES.md
```

## 测试

```powershell
ctest --test-dir build -C Release --output-on-failure
```

| 测试 | 内容 |
| --- | --- |
| `stability-tracker-test` | Partial 结果稳定性 |
| `text-processor-test` | 文本整理、热词、纠正规则 |
| `audio-conditioner-test` | 音频电平整形 |
| `hotword-boost-test` | CTC 热词增强 |
| `fbank-test` | 串行与并行 fbank 输出逐字节一致、LFR 形状与填充规则、线程策略 |
| `translator-test` | 翻译会话与原文降级；`modelspt/opus-mt-*-ct2` 存在时额外跑真实模型 |
| `windows-text-injector-test` | 向测试窗口 `windows-text-target.exe` 注入文字 |

手动检查工具：

- `fbank-benchmark.exe [迭代次数]`：对确定性 5 秒信号分别用 1 线程和 4 线程计算 fbank，打印耗时和输出是否一致。
- `speech-queue-check.exe "CABLE Input"`：通过朗读队列把一句英文、一句中文念到指定设备，可配合录音软件从 CABLE Output 验证。
- `sensevoice-ui.exe --preview --preview-settings <目录>`：把设置对话框的每一页截图保存为 `settings-N.png`。

## 性能参考

20 逻辑核 AVX2 机器，Q8 识别模型：

- 识别模型加载约 80 到 140 ms；首个非空 Partial 约在开口后 700 ms；
- Final 阶段 VAD 约 30 到 40 ms，SenseVoice 推理约 370 到 400 ms；
- 实时回放总 CPU 约 13%，峰值工作集约 289 MiB。

共享 fbank 前端（`fbank-benchmark`，确定性 5 秒信号，498 个原始帧，30 次取最优）：

- 单线程约 12.6 到 13.0 ms，4 线程约 3.6 到 3.9 ms，约 3.3 到 3.5 倍加速，两种路径输出逐字节一致；
- 端到端 4.692 秒中文片段（每路径 3 次，默认 VAD 重叠）：`vad_ms` 中位数 CPU 61 → 44 ms，CUDA 62 → 45 ms；`final_latency_ms` 中位数 CPU 365 → 330 ms，CUDA 215 → 179 ms；`inference_ms` 中位数基本不变（CPU 245 → 242 ms，CUDA 92 → 91 ms）。12 次运行的识别文本完全相同。

Ryzen 5 5600（6 核 12 线程）：

- 一句约 12 秒的中文，翻译约 340 ms；
- Kokoro fp32 合成，4 线程，实时率约 0.34 到 0.43（4 秒的语音约 1.5 到 1.9 秒合成完）；朗读模型首次加载约 3 秒。

这些数字依赖硬件，只作工程参考。

## 设计文档

- [GLOSSARY.md](GLOSSARY.md)：领域术语（录音会话、Final 结果、原文、译文、原文降级、结果去向、朗读、朗读设备、朗读队列、监听等）。
- [ADR 0001](docs/adr/0001-ctranslate2-for-opus-mt.md)：为什么用 CTranslate2 运行 Opus-MT。
- [ADR 0002](docs/adr/0002-kokoro-tts-in-separate-process.md)：为什么选 Kokoro fp32，为什么放在独立进程。

## 第三方组件与许可证

本项目以 [GNU AGPL-3.0](LICENSE) 发布：Windows 输入法组件基于 OpenLess（AGPL-3.0），语音合成用到 espeak-ng（GPL-3.0），因此整个项目采用 AGPL-3.0。

| 组件 | 用途 | 许可证 |
| --- | --- | --- |
| [llama.cpp / ggml](https://github.com/ggml-org/llama.cpp) | 识别与 VAD 推理运行时（预置在 `third_party/llama.cpp`） | MIT |
| [FunASR](https://github.com/modelscope/FunASR) | SenseVoice / VAD 运行时辅助代码 | MIT |
| [CTranslate2](https://github.com/OpenNMT/CTranslate2) | 翻译推理 | MIT |
| [SentencePiece](https://github.com/google/sentencepiece) | 翻译分词 | Apache-2.0 |
| [cppjieba](https://github.com/yanyiwu/cppjieba) | 中文分词与词典 | MIT |
| [OpenLess](https://github.com/Open-Less/openless) | Windows TSF 输入法（`OpenLessIme.dll`） | AGPL-3.0 |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | Kokoro 语音合成（预编译库） | Apache-2.0 |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | 随 sherpa-onnx 分发 | MIT |
| [espeak-ng](https://github.com/espeak-ng/espeak-ng) | Kokoro 英文音素（在 `sherpa-onnx-c-api.dll` 内，数据在模型目录 `espeak-ng-data`） | GPL-3.0 |
| [Qt 6](https://www.qt.io/) | 界面，动态链接 | LGPL-3.0 |
| [VB-Audio Virtual Cable](https://vb-audio.com/Cable/) | 虚拟声卡（用户自行安装，不随项目分发） | 捐赠软件 |

模型的来源与许可证见上文 [模型与下载链接](#模型与下载链接)，完整清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 反馈

欢迎提交 Issue 和 Pull Request。报告识别或注入问题时，请附上 Windows 版本、CPU 型号、所用快捷键和目标程序，以及相关日志。请不要附上私人录音或任何凭据。
