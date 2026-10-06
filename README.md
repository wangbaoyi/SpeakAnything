# SpeakAnything

![Windows](https://img.shields.io/badge/platform-Windows-0078D4?logo=windows&logoColor=white)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Qt 6](https://img.shields.io/badge/UI-Qt%206-41CD52?logo=qt&logoColor=white)
![Offline](https://img.shields.io/badge/inference-offline-2E7D32)
![License](https://img.shields.io/badge/license-AGPL--3.0-blue)

轻量、离线、原生 Windows 语音输入与同声翻译工具：按住快捷键说话，文字写进当前光标；打开翻译后，译文还可以合成语音送进虚拟麦克风，让通话对方直接听到。

所有识别、翻译和语音合成都在本机 CPU 上完成，不依赖任何云端 API。

- 仓库：<https://github.com/wangbaoyi/SpeakAnything>
- 下载：<https://github.com/wangbaoyi/SpeakAnything/releases>（便携 ZIP，已包含全部模型）

---

## 目录

1. [功能概览](#功能概览)
2. [快速开始](#快速开始)
3. [朗读给通话对方（虚拟麦克风）](#朗读给通话对方虚拟麦克风)
4. [模型与下载链接](#模型与下载链接)
5. [系统架构](#系统架构)
6. [源码结构](#源码结构)
7. [设置项](#设置项)
8. [命令行工具](#命令行工具)
9. [编译](#编译)
10. [打包与发布](#打包与发布)
11. [测试](#测试)
12. [性能参考](#性能参考)
13. [设计文档](#设计文档)
14. [第三方组件与许可证](#第三方组件与许可证)

---

## 功能概览

| 能力 | 说明 |
| --- | --- |
| 离线语音识别 | SenseVoiceSmall Q8，中英混合，FSMN-VAD 实时断句，临时结果（Partial）在浮窗预览 |
| 直接输入 | 通过 Windows TSF 和 UI Automation 写入光标；不支持时等待"粘贴延迟"后用 Ctrl+V 兜底 |
| 文本整理 | 原文模式 / 精简模式（去口水词、统一标点、合并重复句），热词纠正与增强 |
| 离线翻译 | Opus-MT 中→英、英→中，经 CTranslate2 int8 运行；原文和译文一起写入光标 |
| 朗读 | 译文经 Kokoro 合成语音，播放到用户选择的设备（如 VB-Cable），通话对方直接听到 |
| 原文降级 | 翻译失败、超时（5 秒）或语种与方向不符时只输入原文，并在浮窗提示 |
| 触发方式 | "按住说话"或"按一下切换"（切换模式最长 5 分钟） |
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

## 模型与下载链接

模型不存放在 git 仓库中。Release ZIP 已包含全部模型；从源码编译时，把它们放到 `models/` 目录（打包脚本会检查并复制）。

| 用途 | `models/` 下的路径 | 大小 | 来源 | 许可证 |
| --- | --- | ---: | --- | --- |
| 语音识别 | `sensevoice-small-q8.gguf` | 243 MB | [FunAudioLLM/SenseVoiceSmall-GGUF](https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF)（文件 `sensevoice-small-q8.gguf`） | Apache-2.0 |
| 语音端点检测 | `fsmn-vad.gguf` | 1.7 MB | [FunAudioLLM/fsmn-vad-GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF)（文件 `fsmn-vad.gguf`） | Apache-2.0 |
| 翻译 中→英 | `opus-mt-zh-en-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en)，用 `tools/convert_opus_mt.py` 转换为 CTranslate2 int8 | CC-BY-4.0 |
| 翻译 英→中 | `opus-mt-en-zh-ct2/` | 79 MB | [Helsinki-NLP/opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh)，同上转换 | Apache-2.0 |
| 语音合成 | `kokoro-multi-lang-v1_1/` | 401 MB | [sherpa-onnx tts-models](https://github.com/k2-fsa/sherpa-onnx/releases/tag/tts-models) 中的 [`kokoro-multi-lang-v1_1.tar.bz2`](https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2)；原始模型 [hexgrad/Kokoro-82M-v1.1-zh](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh) | Apache-2.0 |
| 中文分词词典 | `dict/`（程序目录下） | | [cppjieba/dict](https://github.com/yanyiwu/cppjieba/tree/master/dict) 的 `jieba.dict.utf8`、`user.dict.utf8` | MIT |

上游参考：SenseVoice 原始模型 [FunAudioLLM/SenseVoiceSmall](https://huggingface.co/FunAudioLLM/SenseVoiceSmall)，GGUF 转换与运行时 [FunASR runtime/llama.cpp](https://github.com/modelscope/FunASR/tree/main/runtime/llama.cpp)。

### 获取模型

最简单：下载 Release ZIP，把其中的 `models` 文件夹复制到仓库根目录。

或者逐个获取：

```powershell
# 识别与 VAD（需要 pip install -U huggingface_hub）
huggingface-cli download FunAudioLLM/SenseVoiceSmall-GGUF sensevoice-small-q8.gguf --local-dir models
huggingface-cli download FunAudioLLM/fsmn-vad-GGUF fsmn-vad.gguf --local-dir models

# 翻译：下载并转换为 CTranslate2 int8（只在开发机上做一次）
python -m pip install ctranslate2 transformers sentencepiece torch sacremoses
python tools\convert_opus_mt.py   # 生成 models\opus-mt-zh-en-ct2 和 models\opus-mt-en-zh-ct2

# 语音合成
curl -L -o kokoro.tar.bz2 https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/kokoro-multi-lang-v1_1.tar.bz2
tar xjf kokoro.tar.bz2 -C models
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
麦克风 (WASAPI) → 音频整形 → FSMN-VAD → 短句快照 → SenseVoiceSmall → Partial / Final 结果
                                                                       │
                                                         文本整理（精简模式、热词纠正）
                                                                       │
                                                                     原文
                                                                       │
                                                  翻译模式开启时：Opus-MT (CTranslate2) → 译文
                                                                       │
                                     松开快捷键：整段注入光标 / 复制到剪贴板 / 朗读到设备
```

SenseVoiceSmall 不是原生流式模型，项目用短句快照实现实时体验：Partial 结果持续更新浮窗预览；VAD 确认句尾后提交 Final 结果，已提交的音频随即释放。识别线程忙时只保留最新快照，不排队过时结果。Partial 结果永远不会被翻译。

### 进程与线程

| 组件 | 运行位置 | 说明 |
| --- | --- | --- |
| `sensevoice-ui.exe` | 主进程 | Qt 界面、快捷键、识别、翻译、注入、朗读队列 |
| 识别 | 主进程后台线程 | ggml CPU 后端，不使用 OpenMP |
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
models/                     本地模型（不进 git）
```

## 设置项

设置保存在注册表 `HKCU\Software\SenseVoice\LocalDictation`（QSettings）。

| 键 | 含义 | 默认值 |
| --- | --- | --- |
| `hotkey/shortcut` | 快捷键 | `Ctrl+Alt+Space` |
| `hotkey/trigger` | `hold` 按住说话 / `toggle` 按一下切换 | `hold` |
| `text/mode` | 0 原文 / 1 精简 | 1 |
| `output/destination` | `insert` 输入到光标 / `copy` 只复制 | `insert` |
| `output/content` | 浮窗内容：原文 / 译文 / 两者 | 原文 |
| `output/clipboard_content` | 剪贴板内容（也是输入到光标的内容） | 原文 |
| `output/paste_delay_ms` | Ctrl+V 兜底前的等待 | 150 |
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
.\sensevoice-stream.exe --model .\models\sensevoice-small-q8.gguf --vad .\models\fsmn-vad.gguf --mic

# 音频文件，并翻译成英文
.\sensevoice-stream.exe --model .\models\sensevoice-small-q8.gguf --vad .\models\fsmn-vad.gguf --audio C:\path\to\recording.m4a --translate zh-en
```

| 参数 | 默认值 | 作用 |
| --- | ---: | --- |
| `--partial-ms` | 450 | Partial 结果的目标间隔 |
| `--endpoint-silence-ms` | 700 | 判定句尾所需的静音 |
| `--maximum-utterance-ms` | 15000 | 单句最长音频 |
| `--memory-limit-mb` | 300 | 工作集保护线 |
| `--vad-speech-threshold` | 预设 | FSMN 语音置信度阈值 |
| `--vad-min-db` | 预设 | 最低输入电平 |
| `--vad-min-snr-db` | 预设 | 最低信噪比余量 |
| `--hotwords FILE` | 可选 | UTF-8 制表符分隔的热词文件 |
| `--corrections FILE` | 可选 | 确定性纠正规则 |
| `--translate zh-en\|en-zh` | 关闭 | 翻译每条 Final 结果，输出 `translation` 和 `injection` 事件 |
| `--translation-models DIR` | 程序旁的 `models` | `opus-mt-*-ct2` 所在目录 |

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

产物在 `build\Release\`。运行前把模型放到 `build\Release\models\`，分词词典放到 `build\Release\dict\`。

编译说明：

- 默认使用 CPU 后端，发布构建针对支持 AVX2/FMA/F16C 的 CPU 优化。
- ggml 不使用 OpenMP；CTranslate2 使用 MSVC OpenMP，发布包需要附带 `vcomp140.dll`（不带 OpenMP 的 CTranslate2 在 Windows 上会在进程退出时卡死）。
- CTranslate2 的矩阵后端是 Ruy，不依赖 MKL 或 oneDNN。Ruy 需要嵌套子模块 cpuinfo，所以克隆时必须加 `--recursive`。
- SentencePiece 固定在 v0.2.0，并以 `UNICODE` 编译，否则中文路径下加载模型会静默失败。
- 加 `-DSENSEVOICE_WITH_TRANSLATION=OFF` 可以不编译翻译功能。
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

`package_windows.ps1` 会：编译三个程序，用 `windeployqt` 收集 Qt 运行库，复制 `vcomp140.dll`、sherpa-onnx 与 ONNX Runtime 的 DLL、分词词典、`models/` 下的全部模型、LICENSE 和第三方声明，然后压缩。缺少任何模型时直接报错。

发布包目录结构：

```
sensevoice-ui.exe          主程序
sensevoice-tts.exe         语音合成进程
sensevoice-stream.exe      命令行工具
sherpa-onnx-c-api.dll, onnxruntime*.dll, vcomp140.dll, Qt6*.dll
dict\                      分词词典
models\                    全部模型
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
| `translator-test` | 翻译会话与原文降级；`models/opus-mt-*-ct2` 存在时额外跑真实模型 |
| `windows-text-injector-test` | 向测试窗口 `windows-text-target.exe` 注入文字 |

手动检查工具：

- `speech-queue-check.exe "CABLE Input"`：通过朗读队列把一句英文、一句中文念到指定设备，可配合录音软件从 CABLE Output 验证。
- `sensevoice-ui.exe --preview --preview-settings <目录>`：把设置对话框的每一页截图保存为 `settings-N.png`。

## 性能参考

20 逻辑核 AVX2 机器，Q8 识别模型：

- 识别模型加载约 80 到 140 ms；首个非空 Partial 约在开口后 700 ms；
- Final 阶段 VAD 约 30 到 40 ms，SenseVoice 推理约 370 到 400 ms；
- 实时回放总 CPU 约 13%，峰值工作集约 289 MiB。

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
