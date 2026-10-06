# Third-party notices

SpeakAnything is distributed under the GNU AGPL-3.0 (see `LICENSE`). It includes or links the components below; each keeps its own license, found in its directory under `third_party/` or at the listed source.

## Code

| Component | Use | License |
| --- | --- | --- |
| [llama.cpp / ggml](https://github.com/ggml-org/llama.cpp) | Inference runtime for SenseVoice and FSMN-VAD (vendored in `third_party/llama.cpp`) | MIT |
| [FunASR](https://github.com/modelscope/FunASR) | SenseVoice / VAD runtime helpers | MIT |
| [CTranslate2](https://github.com/OpenNMT/CTranslate2) | Opus-MT translation runtime | MIT |
| [SentencePiece](https://github.com/google/sentencepiece) | Translation tokenizer | Apache-2.0 |
| [cppjieba](https://github.com/yanyiwu/cppjieba) | Chinese segmentation and dictionaries | MIT |
| [OpenLess](https://github.com/Open-Less/openless) | Windows TSF input method (`OpenLessIme.dll`) | AGPL-3.0 |
| [sherpa-onnx](https://github.com/k2-fsa/sherpa-onnx) | Kokoro speech synthesis (prebuilt in `third_party/sherpa-onnx`) | Apache-2.0 |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | Shipped with sherpa-onnx | MIT |
| [espeak-ng](https://github.com/espeak-ng/espeak-ng) | English phonemes for Kokoro, inside `sherpa-onnx-c-api.dll`; data in `models/kokoro-multi-lang-v1_1/espeak-ng-data` | GPL-3.0 |
| [Qt 6](https://www.qt.io/) | UI, linked dynamically | LGPL-3.0 |

## Models (Release ZIP only, not in git)

| Model | Source | License |
| --- | --- | --- |
| SenseVoiceSmall (Q8 GGUF) | [FunAudioLLM/SenseVoiceSmall-GGUF](https://huggingface.co/FunAudioLLM/SenseVoiceSmall-GGUF) | Apache-2.0 |
| FSMN-VAD (GGUF) | [FunAudioLLM/fsmn-vad-GGUF](https://huggingface.co/FunAudioLLM/fsmn-vad-GGUF) | Apache-2.0 |
| Opus-MT zh-en (CTranslate2 int8) | [Helsinki-NLP/opus-mt-zh-en](https://huggingface.co/Helsinki-NLP/opus-mt-zh-en) | CC-BY-4.0 |
| Opus-MT en-zh (CTranslate2 int8) | [Helsinki-NLP/opus-mt-en-zh](https://huggingface.co/Helsinki-NLP/opus-mt-en-zh) | Apache-2.0 |
| Kokoro-82M v1.1 multi-lang | [hexgrad/Kokoro-82M-v1.1-zh](https://huggingface.co/hexgrad/Kokoro-82M-v1.1-zh), packaged by sherpa-onnx | Apache-2.0 |
