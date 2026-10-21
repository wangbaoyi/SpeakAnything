# 翻译 GPU 延迟调研

调研日期：2026-10-20  
目标机器：NVIDIA GeForce GTX 1080 Ti（Compute 6.1，11 GB），Windows，CUDA 12.6.3  
目标模型：现有 Opus-MT zh-en/en-zh、Qwen3-4B GGUF，以及 Qwen3-1.7B/0.6B 候选模型

## 结论

对当前项目，最快的 GPU 翻译路线不是 Qwen3-4B，而是
**CTranslate2 CUDA FP32 跑 Opus-MT**：

- 同一句中文，CTranslate2 CUDA FP32 中位约 **79–83 ms**；
- 当前项目里的 Qwen3-4B CUDA 约 **417–491 ms**；
- Qwen3-1.7B CUDA 约 **257–282 ms**；
- Qwen3-0.6B CUDA 约 **248–277 ms**；
- CTranslate2 CUDA INT8 约 **96–110 ms**，在 Pascal 上反而慢于 FP32。

因此建议新增一个真正的 GPU 快速翻译后端：

1. 中文↔英文使用 Opus-MT + CTranslate2 CUDA FP32；
2. 其他语言或高质量模式继续使用 Qwen3（优先 1.7B，4B 作为高质量选项）；
3. 识别和翻译分开选择设备，避免“GPU 识别被迫搭配慢速 GPU 翻译”。

## 实测数据

测试文本：

```text
今天天气很好，我们一起去公园散步。
请把会议改到明天下午三点。
这个方案还需要再讨论一下。
```

### CTranslate2 / Opus-MT

使用隔离的 Python 环境加载现有 `modelspt/translator/opus-mt-zh-en-ct2`，
beam size 2，预热后取 10 次中位数。单句和 3 句批量结果如下。

| 路径 | 单句中位 | 3 句批量中位 | 说明 |
| --- | ---: | ---: | --- |
| CPU INT8（Python wheel） | 80.9 ms | 108.8 ms | wheel 通常启用 MKL/oneDNN |
| CUDA INT8 | 109.8 ms | 109.8 ms | 1080 Ti 支持 INT8（CC 6.1），但这里不如 FP32 |
| CUDA FP32 | **79.4 ms** | **82.5 ms** | 当前最快 GPU 路径 |

CPU beam size 1 的单句中位约 **71.4 ms**，CTranslate2 官方也建议在可接受质量下使用
`beam_size=1` 降低延迟。当前项目 C++ 使用 beam size 2。

项目当前 C++ 的 Opus-MT CPU 路径实测约 **169–175 ms**。这很可能是因为项目构建
关闭了 MKL、使用 Ruy，而 Python wheel 使用 MKL/oneDNN；该差异需要在 C++ 中
实际构建后验证，不能直接假设 Python wheel 的数字会原样出现。

### Qwen GGUF

通过当前 `sensevoice-stream` 的 GPU 翻译路径测试，3 次运行：

| 模型 | 翻译耗时 | 加载时间 | 备注 |
| --- | ---: | ---: | --- |
| Qwen3-4B Q4_K_M | 417–491 ms | 2.3–3.1 s | 现有默认高质量模型 |
| Qwen3-1.7B Q4_K_M | 257–282 ms | 1.31 s | 速度/质量较均衡 |
| Qwen3-0.6B Q4_K_M | 248–277 ms | 0.79 s | 比 1.7B 只快一点，质量风险更高 |

候选模型文件：

- [Qwen3-1.7B-GGUF](https://huggingface.co/unsloth/Qwen3-1.7B-GGUF)
  `Qwen3-1.7B-Q4_K_M.gguf`，约 1.1 GB；
- [Qwen3-0.6B-GGUF](https://huggingface.co/unsloth/Qwen3-0.6B-GGUF)
  `Qwen3-0.6B-Q4_K_M.gguf`，约 0.4 GB。

1.7B 的译文质量明显好于 0.6B，而两者延迟接近，因此若要在 Qwen 路线里选一个
快速档，1.7B 更值得作为默认候选。4B 仍然适合质量优先或语言覆盖面更广的场景。

## 为什么 CTranslate2 CUDA 可行

项目自带的 CTranslate2 4.8.2 文档和源码给出了直接依据：

- [hardware_support.md](../third_party/CTranslate2/docs/hardware_support.md)
  说明 CUDA 支持 Compute Capability >= 3.5；
- [installation.md](../third_party/CTranslate2/docs/installation.md)
  列出 `WITH_CUDA=ON`、`CUDA_ARCH_LIST`、`CUDA_DYNAMIC_LOADING` 等构建选项；
- [parallel.md](../third_party/CTranslate2/docs/parallel.md)
  说明 GPU 使用 CUDA stream，`inter_threads` 主要提升吞吐，不一定降低单句延迟；
- `src/types.cc` 中 `gpu_supports_int8()` 对 Compute 6.1 返回 true；
- [performance.md](../third_party/CTranslate2/docs/performance.md)
  建议 GPU 尽量使用更大 batch；翻译可考虑 `beam_size=1`，并建议在模型有固定
  system prompt 时使用缓存。

Opus-MT 是纯 Transformer，不需要 cuDNN 的卷积路径；`WITH_CUDNN=ON` 不是必需项。
建议的 Windows 构建选项：

```text
-DWITH_CUDA=ON
-DCUDA_ARCH_LIST=6.1
-DCUDA_DYNAMIC_LOADING=ON   # 可选，取决于打包方式
```

集成点在 `src/opus_mt_translator.cpp`：该文件当前硬编码：

```text
ctranslate2::Device::CPU
ctranslate2::ComputeType::INT8
```

测试结果表明 GPU 应优先尝试 `Device::CUDA + ComputeType::FLOAT32`；INT8 仅作为
兼容路径保留，不应默认使用。

## Qwen KV Cache 的判断

当前 `src/llm_translator.cpp` 每次翻译都会：

1. 拼接 system prompt 和最近 3 轮上下文；
2. 清空 llama memory；
3. 对完整 prompt 重新 prefill；
4. 逐 token 生成译文。

复用 KV Cache 可以减少重复 prompt 的 prefill，尤其是多句连续翻译时。但从这次
单句实测看，1.7B 仍有约 250 ms，说明 decode 和模型规模仍是主要成本。KV Cache
应作为 Qwen 路线的第二层优化，优先级低于：

- 用 CTranslate2 CUDA FP32 处理 zh-en/en-zh；
- 选择 1.7B 而不是 4B 作为快速 Qwen 档；
- 把识别和翻译设备拆开。

## 推荐实施方案

### 阶段 A：最低风险的 GPU 翻译快速路径

- 新增 CTranslate2 CUDA 构建开关，并在 Opus-MT 翻译器中按设备选择
  `CUDA + FLOAT32` 或 `CPU + INT8`；
- 中文↔英文优先使用这条路径；
- 保留 Qwen3-4B 作为高质量/非中英方向模型；
- 为低延迟模式使用 `beam_size=1` 做可配置项，默认仍使用 beam 2 以保持质量。

预期：中英单句翻译从当前 Qwen4B 的约 0.42–0.49 s 降到约 **0.08–0.10 s**，
但需要实际 C++ 构建确认。

### 阶段 B：Qwen 快速档

- 将 Qwen3-1.7B 加入模型目录和 UI 选择；
- 增加“质量优先/低延迟/仅 CPU”三类预设；
- 对连续句子实现 session KV Cache 或至少缓存固定 system prompt。

### 阶段 C：不建议优先投入的方向

- 在当前 Pascal GPU 上继续追求 Qwen 4B 的 FP16/Tensor Core 优化；
- 把 VAD 或 Kokoro TTS 放到 GPU：VAD 模型太小，TTS 当前 CPU RTF 已经远快于实时；
- 为单句翻译堆多个 CUDA stream：官方文档说明这主要影响吞吐，单句延迟收益有限。

## 可复现实验说明

Python 基准使用独立虚拟环境加载现有 CTranslate2 模型，不修改项目依赖；
Qwen 基准使用当前项目的 `sensevoice-stream`、CUDA 构建和 `--translation-model`
指定临时模型文件。Python wheel 与项目 C++ 的构建后端不同，因此这些数字用于
路线筛选，最终结论仍需 C++ 集成后的端到端测试。

## C++ 集成验证（2026-10-20）

阶段 A 已落地：`SENSEVOICE_CT2_CUDA` 配置 vendored CTranslate2 的
`WITH_CUDA=ON`、`CUDA_ARCH_LIST=6.1`、`CUDA_DYNAMIC_LOADING=ON`，Opus-MT
工厂按设备选择 `CUDA + FLOAT32` 或 `CPU + INT8`。翻译设备从识别设备中拆开：
设置里是「翻译设备」，CLI 是 `--translation-device`（缺省跟随 `--device`）。

同一段 `speech_zh.wav`（识别结果 `今天天气很好，我们一起去公园散步。`），
CUDA 构建、识别留在 CPU、只切换 `--translation-device`：

| 设备 | 翻译后端 | 报告设备 | 加载 | translation_ms | 译文 |
| --- | --- | --- | ---: | ---: | --- |
| CPU | Opus-MT (CTranslate2 CPU INT8) | CPU | 322 ms | 177 ms | It's a nice day. We'll go for a walk in the park. |
| GPU | Opus-MT (CTranslate2 CUDA FP32) | GPU (CUDA: NVIDIA GeForce GTX 1080 Ti) | 493-510 ms | 80-103 ms | 同上 |
| GPU, `zh-de` 基线 | Qwen3 GGUF (llama.cpp) | GPU (CUDA: NVIDIA GeForce GTX 1080 Ti) | 2180-2622 ms | 423-424 ms | Heute ist das Wetter sehr gut, wir gehen gemeinsam ins Park spazieren. |

`auto` 在 CUDA 构建上为 `zh-en` 选择 Opus-MT CUDA（100 ms）；CPU 构建上的
`zh-de` 选择 Qwen CPU（4.2 s），说明非中英方向仍走 Qwen。显式 `gpu` 在没有
CUDA 的构建中会失败，不会用 CPU 冒充 GPU。

构建注意：CMake 4 需要 `CMP0146=OLD` 才能提供 CTranslate2 4.8 使用的
`FindCUDA`；`CUDA_USE_STATIC_CUDA_RUNTIME=OFF` 避免与 ggml 的共享 cudart
重复符号；CTranslate2 静态 MSVC 构建会把 CUDA 源文件固定为 `/MT`，因此
CUDA 翻译构建改用共享的 `ctranslate2.dll`（`/MD`），打包脚本会一并复制。
这台机器的 CUDA 12.6 缺少 cuRAND 组件，验证时从 NVIDIA `libcurand`
redistributable 补装了头文件和 `curand.lib`。
