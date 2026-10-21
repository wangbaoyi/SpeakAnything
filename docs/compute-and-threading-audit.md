# Compute and threading audit

Scope: the shared checkout after the compute-policy, translation, VAD-overlap
and shared-fbank phases. This document maps where work runs, which stages are
serialized, and which threading changes are worth doing next. Overlapping VAD
with recognition and parallel feature extraction are implemented; the sections
[Overlapped VAD pipeline](#overlapped-vad-pipeline-implemented) and
[Shared fbank front end](#shared-fbank-front-end-implemented) describe the
invariants and the remaining limits.

Related: [compute policy and GPU offload](#compute-policy-and-gpu-offload),
[ADR 0002 Kokoro in a separate process](adr/0002-kokoro-tts-in-separate-process.md).

## Processes

| Process | Role | Lifetime |
| --- | --- | --- |
| `sensevoice-ui.exe` | Qt window, hotkeys, capture, recognition, translation, text delivery, speech queue control | User session |
| `sensevoice-tts.exe` | sherpa-onnx Kokoro/Piper synthesis; reads request lines on stdin and writes `READY`, `AUDIO` and `ERROR` lines on stdout | Started lazily by `SpeechQueue::setActive`, restarted when the voice changes |
| `tar.exe` (Windows) / `/usr/bin/ditto` (macOS) | Archive extraction for model downloads | One process per downloaded archive |
| `sensevoice-stream.exe` | Diagnostic CLI: file replay or microphone, same recognizer and VAD as the UI | Command invocation |

The TTS helper exists so a crash or a long synthesis cannot take down the UI
process. It also means speech never shares a thread pool with recognition.

## Threads in `sensevoice-ui.exe`

| Thread | Created by | Work |
| --- | --- | --- |
| Qt GUI thread | `QApplication` | Widgets, tray, hotkey polling timers, `handleRecognition`, translation routing, text injection, `SpeechQueue` and `QProcess` events, model downloads (`QNetworkAccessManager`) |
| miniaudio capture callback | Audio device backend | Copies input frames into the 30 s lock-free ring in `audio_io.cpp`; no allocation, no processing |
| `MicrophoneCapture::Impl::worker` | `MicrophoneCapture::start` | Drains the ring in 800-sample chunks, runs `AudioConditioner::process`, publishes level meters, calls `StreamRecognizer::accept_pcm` |
| `StreamRecognizer::worker` | `StreamRecognizer::start` | Waits for a fresh VAD result and the partial cadence, snapshots the prefix the VAD pass covered, runs SenseVoice/Whisper inference, stability tracking and text processing, emits partial/final events |
| `StreamRecognizer::vad_worker` | `StreamRecognizer::start` | Owns the only `FsmnVadEngine` in overlapped mode. Waits for the newest audio revision, runs FSMN-VAD on an immutable snapshot, publishes one result at a time. Not started when `SENSEVOICE_SERIAL_VAD=1` or no VAD model is configured |
| `VoiceInputWindow::loader_` | `beginLoadModels` | Loads the recognizer (and, on first load, VAD, hotwords, corrections and the jieba dictionary), then hands the result back with `QMetaObject::invokeMethod` |
| `VoiceInputWindow::stopper_` | `stopSession` | Calls `StreamRecognizer::finish` or `cancel` so a long final decode does not block the GUI thread |
| `TranslationWorker` thread | `TranslationWorker` constructor | Loads CTranslate2 or llama.cpp translation models and translates one queued sentence at a time |
| `SpeechPlayer` worker | `SpeechPlayer` constructor | Feeds WASAPI/miniaudio playback buffers for the speech device and the monitor device |
| Qt network thread | `QNetworkAccessManager` | Download I/O; completion slots are delivered to the GUI thread |

`ggml` also creates its own OpenMP/thread pool for CPU matmul (`GGML_OPENMP=OFF`,
so ggml's own thread pool). ASR uses `n_threads=8`; FSMN-VAD has a separate
CPU backend with `n_threads=4` so the two pools do not oversubscribe the cores
while they overlap. CUDA work is submitted from the calling thread and
synchronized inside `ggml_backend_graph_compute`.

Feature extraction creates at most 3 extra workers for the duration of one
pass (the calling thread handles the first frame range): `min(4, engine
threads)`, and none at all below 32 frames. They are joined before the ggml
graph runs, so they never coexist with the inference pool.

## Threads in `sensevoice-stream.exe`

The CLI is deliberately simpler:

| Thread | Work |
| --- | --- |
| Main thread | Replays a WAV in real time (`run_audio_file`) or waits for Enter (`run_microphone`), prints JSON events |
| `MicrophoneCapture::Impl::worker` | Same conditioner and ring as the UI |
| `StreamRecognizer::worker` | Recognition and endpoint decisions |
| `StreamRecognizer::vad_worker` | FSMN-VAD, overlapped with recognition by default |
| Translation | Runs synchronously on the recognizer worker inside `translate_final`, so a final event waits for translation |

## Queues and buffers

| Queue or buffer | Producer | Consumer | Bound |
| --- | --- | --- | --- |
| Miniaudio ring, 480,000 samples | Device callback | Capture worker | Fixed 30 s, drops newest samples and increments `dropped_samples` |
| `StreamRecognizer::audio_` | Capture worker via `accept_pcm` | Recognizer worker and VAD worker (copy) | Grows with the utterance; trimmed by `consume_audio_prefix` and the memory budget |
| `StreamRecognizer::vad_result_` | VAD worker | Recognizer worker | One immutable snapshot. Newer passes replace it; intermediate revisions coalesce and are never queued |
| `TranslationWorker::commands_` | GUI thread | Translation thread | Unbounded deque, one command at a time |
| `SpeechQueue::pending_` | GUI thread | TTS helper process | Unbounded list, one synthesis in flight |
| TTS stdout buffer | Helper process | GUI thread (`readOutput`) | Grows until a complete newline header plus payload arrives |
| Qt event queue | Worker threads via `invokeMethod` | GUI thread | One queued event per partial/final result |

## The serialized dictation path

In the UI the happy path is serial from microphone to delivery. Every stage
waits for the previous one, with three exceptions that are already concurrent:
audio capture and conditioning, model loading, and translation after a final
result.

1. **Capture.** The device callback writes PCM into the ring. The capture
   worker drains it, applies automatic attenuation in `AudioConditioner`, and
   forwards the chunk to `StreamRecognizer::accept_pcm`.
2. **Segmentation.** In the default pipeline a separate VAD worker copies the
   accumulated utterance and runs `FsmnVadEngine::detect` over that immutable
   snapshot while the recognizer worker is decoding a previous one. The
   recognizer waits for the newest published result, uses exactly the audio
   prefix that result covered, and takes the speech segments, endpoint
   decision and waveform telemetry from it. With `SENSEVOICE_SERIAL_VAD=1`
   the recognizer worker runs VAD itself, exactly as before.
3. **Feature extraction.** `SenseVoiceEngine::recognize` (or
   `WhisperEngine::recognize`) computes the fbank features for the snapshot on
   the recognizer thread. SenseVoice and FSMN-VAD share `src/fbank.cpp`; the
   raw frame loop is split across at most 4 workers (see
   [Shared fbank front end](#shared-fbank-front-end-implemented)). Whisper
   keeps its own front end.
4. **Inference.** The ggml graph is built and executed on the CPU or the GPU
   selected by `ComputePolicy`. Both partials and finals re-run the model over
   the entire current utterance. There is no incremental decoder state.
5. **Post-processing.** `TextProcessor` applies hotwords and corrections,
   `StabilityTracker` splits the stable and unstable prefix, and
   `RecognitionEvent`s are posted to the GUI thread.
6. **GUI event.** `handleRecognition` updates the bubble and, for streaming
   output, appends text to `streaming_output_text_` and may deliver a fragment
   immediately.
7. **Translation.** A final result creates a ticket. `TranslationWorker`
   translates queued sentences in order. `completeTranslation` on the GUI
   thread waits up to `translation_deadline_`.
8. **Delivery.** The GUI thread composes the final text, copies it, and injects
   it with the clipboard plus Ctrl+V (Windows) or an accessibility insert. The
   send happens on the GUI thread, optionally after a paste-delay timer.
9. **Speech.** When 朗读 is enabled, the final or translated sentence is pushed
   to `SpeechQueue`, synthesized by the helper process, and played by a
   `SpeechPlayer` worker. Synthesis and playback of the next sentence only start
   after the previous AUDIO frame has been parsed.

## Shared fbank front end (implemented)

SenseVoice and FSMN-VAD carried byte-identical copies of the same 80-bin
log-mel front end. It now lives in `src/fbank.{h,cpp}`:

| Stage | API | Semantics (unchanged) |
| --- | --- | --- |
| Raw frames | `fbank::compute_log_mel` | 16 kHz mono, 400-sample Hamming window every 160 samples, 512-point FFT, 80 mel triangles from 20 Hz to 8 kHz, `log(max(energy, 1.1920929e-07))` |
| LFR | `fbank::stack_lfr` | Row `t` concatenates `window` raw rows starting at `t * stride`; `(window - 1) / 2` copies of the first row in front, the last row repeated at the end; `ceil(source / stride)` rows |

SenseVoice calls the pair with window 7 and stride 6, producing 560-wide rows;
`feature_dimension` is now a `static_assert` on `7 * 80`. FSMN-VAD passes the
`vad.lfr_m` / `vad.lfr_n` values read from its GGUF (5 and 1), producing its
400-wide input. CMVN, graph inputs, VAD segmentation and recognition output are
untouched.

### Threading

- Only raw frame extraction is parallel. Frames are independent, every worker
  writes a disjoint contiguous range of output rows, and the tables and waveform
  are read-only, so the result is byte-identical to the serial path. The unit
  test compares with `memcmp`, not a tolerance.
- Worker count is `min(4, engine_threads)`. SenseVoice passes its ggml thread
  count (8 by default) and FSMN-VAD its `--vad-threads` value (4 by default), so
  both use 4. Inputs below 32 frames (320 ms) run only on the calling thread,
  where thread start and join would cost more than the work. `engine_threads`
  is also the floor: a 2-thread engine uses 2.
- The Hamming window and mel filter bank are built once per distinct `Config`
  under a mutex and shared read-only; each worker owns its FFT scratch
  buffers. No mutable state is shared during a pass.
- `SENSEVOICE_SERIAL_FBANK=1` forces one thread for both engines and is read
  once per process. The CLI prints `fbank: serial|parallel feature extraction`
  after the VAD load line.

### Measured

`fbank-benchmark` on the deterministic 5 s signal (498 raw frames, best of 30):

| Build | 1 thread | 4 threads | Speedup | Output |
| --- | --- | --- | --- | --- |
| CPU-only | 12.7 to 13.0 ms | 3.6 to 3.8 ms | 3.4x to 3.5x | identical |
| CUDA build (host code is the same) | 12.6 to 12.9 ms | 3.7 to 3.9 ms | 3.3x to 3.5x | identical |

Each cell is the range over repeated `best of 30` runs; single runs vary by
about 0.3 ms, which is enough to move the ratio between roughly 3.3x and 3.5x.

End-to-end `E:/tmp-saytest/speech_zh.wav` (4.692 s, real-time replay), three
runs per path, default overlapped VAD, `--threads 8 --vad-threads 4`:

| Build | fbank | final_latency_ms | vad_ms | inference_ms |
| --- | --- | ---: | ---: | ---: |
| CPU | serial | 376, 363, 365 (median 365) | 62, 61, 61 (median 61) | 253, 241, 245 (median 245) |
| CPU | parallel | 326, 330, 332 (median 330) | 44, 46, 43 (median 44) | 240, 242, 247 (median 242) |
| CUDA | serial | 218, 215, 206 (median 215) | 65, 61, 62 (median 62) | 92, 96, 84 (median 92) |
| CUDA | parallel | 186, 179, 162 (median 179) | 45, 46, 44 (median 45) | 99, 91, 91 (median 91) |

All twelve runs produced exactly one final event and the identical text
"今天天气很好，我们一起去公园散步。"

Reading the numbers:

- `vad_ms` includes the VAD's own fbank pass, so it falls by about 17 ms in
  both builds: that is where the feature saving shows up most clearly.
- `inference_ms` starts after SenseVoice's fbank pass and excludes it, so it
  should not move. CPU medians differ by 3 ms and CUDA medians by 1 ms, well
  inside the run-to-run spread, which matches "no change".
- `final_latency_ms` is wall clock from the newest `accept_pcm`, so it does
  contain the feature saving. CPU drops from 365 to 330 ms (median) and CUDA
  from 215 to 179 ms.

The serial path remains the default fallback for short inputs and whenever
`SENSEVOICE_SERIAL_FBANK=1` is set.

## What must stay serial

These are correctness constraints, not accidents:

- **Microphone sample order.** VAD state and decoder input assume contiguous
  16 kHz mono frames. Any parallel split of the capture stream has to preserve
  order and the conditioning gain envelope.
- **VAD state and endpoint decisions.** The endpoint decides which samples are
  finalized and consumed. `FsmnVadEngine` has exactly one owner: the VAD worker
  in overlapped mode, the recognizer worker in serial mode. Two concurrent
  passes over overlapping ranges could emit a sentence twice or cut it short.
- **VAD result freshness.** A VAD result carries the audio generation and
  revision it was computed from. The recognizer only applies a result from the
  current generation, and `consume_audio_prefix` bumps the generation so a
  pass that started before a trim can never finalize the trimmed utterance.
- **Recognizer snapshot order.** Partials are prefix results. Two partials
  computed in parallel can finish out of order and make `StabilityTracker`
  regress.
- **Translation sentence order.** `TranslationSession` assigns indices and the
  injected text concatenates in that order. Out-of-order completion would
  reorder the transcript.
- **TTS playback order.** `SpeechQueue` is a queue for a reason. Parallel
  synthesis is possible, but playback order must remain the sentence order.
- **Text injection order.** Clipboard and keyboard injection are global UI
   state. Two injections racing would interleave into the target application.
- **Model swap versus active session.** `engine_`, `vad_` and the translation
  model owners are read by worker threads. Replacing them is only safe while
  no session is listening and no translation ticket is pending.

## Compute policy and GPU offload

`ComputePolicy` (Auto, Cpu, Gpu) is shared by SenseVoice, Whisper, the CLI and
the settings dialog. The runtime resolution reads the ggml backend registry, so
`Gpu` and `Auto` agree on what the build and driver actually expose. A build
without a CUDA toolkit still starts and reports `CPU`; selecting 仅 GPU on such a
build fails with a clear error at model load.

SenseVoice loads the GGUF metadata with `no_alloc`, allocates weight tensors on
the chosen backend, and streams tensor bytes from the file. On a GPU it copies
weights into device memory, checks every graph op with
`ggml_backend_supports_op`, and only then computes on the GPU. `Auto` rebuilds
the graph for the CPU when an op is unsupported or allocation fails. `Gpu`
reports the unsupported op and refuses to load rather than silently running on
the CPU. The graph doubles as the load-time warmup, so the reported device is
the device the first dictation uses.

Whisper follows the same policy. On Windows and Linux the reported backend
comes from the registry (`GPU (CUDA: ...)`), never a hard-coded Metal string.

### Translation has its own device policy

Translation no longer follows the recognition device. It has a separate
`ComputePolicy` stored under `translation/device` in the UI and selected by
`--translation-device` in the CLI (default `auto`; the CLI follows `--device`
when the flag is absent). This matters on a GPU machine: recognition can stay
on CUDA while translation takes the fastest path for its direction, or the
reverse.

Both backends can be compiled into one binary, and
`choose_translation_backend` is a pure function, so the whole matrix is tested
without a GPU. Opus-MT covers zh-en/en-zh only; every other direction uses
Qwen.

| Policy | Direction | Result |
| --- | --- | --- |
| `Cpu` | zh-en, en-zh | Opus-MT CTranslate2 INT8 on the CPU |
| `Cpu` | other | Qwen GGUF on the CPU (`n_gpu_layers = 0`) |
| `Gpu` | zh-en, en-zh, `SENSEVOICE_CT2_CUDA` build | Opus-MT CTranslate2 CUDA FLOAT32 on the GPU |
| `Gpu` | zh-en, en-zh, CPU-only CTranslate2 | Qwen GGUF on the GPU, the pre-CUDA fallback |
| `Gpu` | other | Qwen GGUF, every llama layer offloaded (`n_gpu_layers = -1`) |
| `Auto` | zh-en, en-zh | GPU Opus-MT CUDA when compiled, else GPU Qwen when ready, else CPU Opus-MT (CPU Qwen in LLM-only builds) |
| `Auto` | other | GPU Qwen when ready, else CPU Qwen |

An explicit `Gpu` never falls back to the CPU: a missing device, model or
compiled backend returns an error instead of a CPU translator labelled GPU.
`Auto` never reports a GPU it cannot use, but does fall back quietly to the
CPU. Opus-MT reports `Opus-MT (CTranslate2 CUDA FP32)` or
`Opus-MT (CTranslate2 CPU INT8)` plus the `GPU (CUDA: ...)` / `CPU` label, so
the selected compute type is visible in the CLI and the UI.

On the GTX 1080 Ti the CUDA FP32 path measured about 79-83 ms per sentence,
against 417-491 ms for Qwen3-4B; see
[translation-gpu-research.md](translation-gpu-research.md).

The macOS default still builds the LLM only and does not pull in CTranslate2.
There, a CPU policy runs the LLM with `n_gpu_layers = 0` and reports `CPU`.
Windows builds both backends, which is what the CUDA build needs.

Qwen discovery is case-insensitive and searches `<root>/translator` then
`<root>`, so `modelspt/translator/Qwen3-4B-Q4_K_M.gguf` is found even when the
catalog stores a lower-case file name. `Qwen3-4B` wins over `1.7B`, then any
other Qwen GGUF. An explicit user selection wins over discovery. Discovery is
skipped entirely on the zh-en/en-zh Opus paths, so a stale Qwen selection does
not break them; when the LLM is needed, a missing selection is reported by
name. Opus-MT directories are resolved under either `<root>` or
`<root>/translator`.

The loaded `Translator` reports `backend_name()` and `device_label()`. The CLI
prints both with the load time and adds `backend` and `device` to every
translation JSON event; the UI shows them in the tray tooltip and the status
line after a model loads. The worker still owns exactly one translator, so the
Opus and Qwen models are never resident together and queued sentences keep
their order.

Changing the translation device rebuilds only the `TranslationWorker`, with no
active session, after the settings dialog closes. Changing the recognition
device reloads only the speech engine (`beginLoadModels`); the two paths no
longer force each other. The settings dialog has separate recognition and
translation selectors, and `listen` sessions keep the translation device they
loaded.

On Windows the model path reaches llama.cpp as UTF-8 (`ggml_fopen` decodes it
and calls `_wfopen`), so a Qwen file under a non-ASCII checkout path loads.
The primary verification used the real Chinese checkout path rather than the
`E:\sa-src` junction, which is only needed for nvcc.

### Build and packaging

- `SENSEVOICE_WITH_CUDA` (default ON on Windows) runs `find_package(CUDAToolkit)`
  at configure time. A toolkit sets `GGML_CUDA=ON`; its absence produces a
  CPU-only build with a status message.
- `SENSEVOICE_CUDA_REQUIRED=ON` turns a missing toolkit into a configure error
  for reproducible GPU packaging.
- CUDA 12.6.x is the right toolkit for Pascal (sm_61, GeForce GTX 10 series).
  CUDA 13 drops device code for that architecture; the configure step warns.
- `SENSEVOICE_CT2_CUDA` (default ON when a toolkit is found) configures the
  vendored CTranslate2 with `WITH_CUDA=ON`, `CUDA_ARCH_LIST=6.1` and
  `CUDA_DYNAMIC_LOADING=ON`, and defines `SENSEVOICE_CT2_CUDA` on
  `sensevoice_translation`. Without a toolkit, or on macOS, the translation
  library stays CPU-only and the rest of the build is unchanged.
- CTranslate2 4.8 still uses CMake's `FindCUDA`, which CMake 3.27 and later
  hide unless policy `CMP0146` is `OLD`. The configure sets
  `CMAKE_POLICY_DEFAULT_CMP0146=OLD` before adding the vendored project, so
  CMake 4.4 configures the CUDA translation path as well. The CUDA builds in
  this audit used CMake 4.4.2 with `CUDA_PATH` pointing at CUDA 12.6.
- `CUDA_USE_STATIC_CUDA_RUNTIME=OFF` keeps FindCUDA on `cudart.dll`; ggml
  already links the shared runtime, and mixing both copies duplicates every
  `cuda*` symbol at link time.
- CTranslate2's static MSVC build forces `/MT` for its CUDA sources, which
  cannot link into this `/MD` application. Only the CUDA configuration builds
  CTranslate2 shared (`ctranslate2.dll`), which uses the project runtime; the
  DLL is copied next to `sensevoice-stream`, `sensevoice-ui` and
  `translator-test`, and packaging ships it.
- This machine's CUDA 12.6 installation lacked the cuRAND component, which
  CTranslate2's `random.cu` includes. The cuRAND 10.3.7.77 headers and
  `curand.lib` were installed into the toolkit from NVIDIA's `libcurand`
  redistributable; a complete CUDA toolkit already has them.
- `tools/package_windows.ps1` copies `cudart64_*`, `cublas64_*` and
  `cublasLt64_*` from the same toolkit when the cache says `GGML_CUDA=ON` or
  the CTranslate2 CUDA backend was configured
  (`SENSEVOICE_CT2_CUDA_AVAILABLE=ON`).
- `sensevoice-stream --device auto|cpu|gpu` selects the recognition policy and
  `--translation-device auto|cpu|gpu` the translation policy (default: follow
  `--device`). It prints `Speech engine loaded on <device> in <ms>` and
  `Translator <direction> (<backend>) loaded on <device> in <ms>` on stderr.
- `SENSEVOICE_WITH_TRANSLATION` builds Opus-MT (default ON except on macOS,
  matching the old platform default). `SENSEVOICE_WITH_LLM_TRANSLATION` builds
  the Qwen path (default ON). The deprecated
  `SENSEVOICE_TRANSLATION_BACKEND` cache entry is still accepted but no longer
  selects anything; the backend is a runtime decision.
- llama.cpp is pinned to C++17 because its `u8` string literals are
  `char8_t` under this project's C++20 and stop MSVC from compiling the LLM
  path on Windows.
- nvcc on Windows mishandles build paths that contain non-ASCII characters.
  If the checkout lives under such a path, configure and build through an
  ASCII directory junction, for example `E:\sa-src`. The produced binaries
  still load models from the real non-ASCII path, as verified below.

## Overlapped VAD pipeline (implemented)

`StreamRecognizer` now runs two workers when a VAD model is configured and
`SENSEVOICE_SERIAL_VAD` is not set:

| Worker | Owns | Wakes on |
| --- | --- | --- |
| recognizer | inference, endpoint decisions, prefix consumption, stability and text processing | a newer VAD result plus the partial cadence, `finish`, `cancel` |
| VAD | the single `FsmnVadEngine` | a pending audio revision, `finish`, `cancel`, recognizer exit |

### Revisions, generations and snapshots

- `audio_revision_` increments on every `accept_pcm` and every
  `consume_audio_prefix`.
- `audio_generation_` increments only when a prefix is erased. Segment times are
  relative to the buffer start, so results from an older generation are
  discarded instead of being reinterpreted against shifted audio.
- The VAD worker copies `audio_` under the mutex into an immutable snapshot and
  records `(generation, revision, sample_count)`. It publishes one
  `VadSnapshot` at a time. The recognizer consumes the newest published result
  and decodes exactly the prefix that pass covered, which reproduces the
  serial pairing of "one VAD result, one recognition snapshot".
- `consume_audio_prefix` drops the published result, bumps the generation and
  requests a fresh pass. A pass that finishes while the trim is in flight is
  dropped. An old revision can therefore never finalize a newer utterance.

### Coalescing and throttling

`accept_pcm` does not run VAD and does not queue work. It sets a single
`vad_requested_` flag at most once per 80 ms of new audio since the last pass
started. While a pass runs, further chunks only update the audio buffer. When
the pass ends, the worker re-reads the newest buffer if the flag is set. A
backlog of intermediate revisions cannot form, and a 15 s utterance does not
trigger a VAD scan on every 20 ms capture chunk.

### Final and teardown

- `finish` records the current revision. If the published VAD result already
  covers the whole buffer it is reused; otherwise the worker is asked for one
  pass over the final audio. The recognizer waits for a result from the current
  generation whose revision is at least the finish revision.
- The final wait is bounded at 5 seconds. If the VAD worker hangs, the final is
  decoded without segment boundaries rather than dropped, and a warning is
  written to stderr. If the worker throws, `vad_error_` is published; a live
  session surfaces an `error` event, while an in-progress final falls back to
  the same no-segment decode.
- `finish` and `cancel` set their flag, notify both condition variables, then
  join the VAD worker and the recognizer. The VAD worker never waits on the
  recognizer, and it exits only on `cancel` or after the recognizer sets
  `asr_done_` as it returns. This covers both the "final already covered" and
  "final not covered" cases without a join cycle, and it lets a trim performed
  during the last partial still be served.
- Finals are emitted by one recognizer thread, so ordering and the
  one-final-per-utterance rule are unchanged from the serial path.

### Metrics and configuration

- `RecognitionEvent::final_latency_ms` measures from the most recent
  `accept_pcm` to the final event. It is in every CLI partial/final JSON line
  (`0` for partials). `vad_ms` is the VAD pass time and `inference_ms` is the
  recognition time for the decoded segments.
- `--vad-threads` defaults to `min(4, --threads)` in the CLI, and the UI loads
  FSMN-VAD with 4 threads instead of 8. The model is small, and four threads
  leave the ASR pool room when both run at once.
- `SENSEVOICE_SERIAL_VAD=1` keeps the old serial path for a fair benchmark. It
  is read once per `StreamRecognizer`.

### Known limits

- VAD still recomputes the whole utterance per pass. With a 4.7 s clip a pass
  costs about 60 ms; the overlap hides part of that but the final path still
  pays one VAD pass whenever the newest pass predates the last accepted chunk.
  On the CPU build the final is dominated by SenseVoice inference, so the
  measured final latency is close to the serial path. The structural win is
  that VAD time no longer has to precede every partial.
- An inference already running when `finish` arrives is not cancelled. Both
  pipelines pay that in-flight partial before the final decode.
- `FsmnVadEngine` has no abort hook, so a `cancel` during a long pass waits for
  that pass to return.

## Prioritized threading opportunities

Ranked by latency benefit against implementation risk.

### 1. Overlap VAD and feature extraction with inference, and reuse features

Status: VAD overlap and parallel fbank are implemented as described above.
Incremental fbank and feature reuse are not.

Today the recognizer worker still computes fbank and builds the graph for the
whole snapshot on every partial, and VAD recomputes features for the whole
snapshot too. With a 450 ms partial cadence and a growing snapshot, every
partial redoes work over audio that has not changed. The frame-parallel split
cut a 5 s pass from about 12.7 ms to 3.6 ms, but the pass still scales with the
snapshot.

- Remaining benefit: removes the remaining feature cost from the partial
  latency entirely rather than dividing it; on CPU builds this mostly helps
  short partials, where the fixed cost is a larger share. Also allows a partial
  cadence shorter than the current compute time.
- Approach: key feature buffers by sample range so a partial only computes
  features for new audio, and have the VAD worker publish the features it
  already computed.
- Risk: medium-high. Snapshot completion order still needs a generation
  counter, and memory grows if old feature buffers are retained.

### 2. Pipeline sentence translation and TTS synthesis ahead of playback

Translation is already asynchronous, but each streaming sentence waits for its
translation before delivery, and `SpeechQueue` synthesizes strictly one request
at a time with playback starting only after synthesis returns.

- Benefit: hides CTranslate2 or LLM latency behind the next partial, and hides
  half of the TTS model latency on the first sentence of a session.
- Approach: allow `SpeechQueue` to keep one synthesis request in flight while
  the current buffer plays, and let the translation worker accept the next
  sentence while the GUI waits for the previous ticket. Playback stays FIFO.
- Risk: medium. Cancellation on stop must discard synthesized audio in the
  right order. The helper protocol serializes requests, so the win is bounded
  by synthesis time rather than by wall-clock overlap of multiple syntheses.

### 3. Preload the TTS model and translation model before the first sentence

`SpeechQueue::setActive` starts the helper and the model load only when 朗读 is
first used; translation loads on the first request. Both happen while the user
is waiting for audio.

- Benefit: removes a multi-second first-use stall from the first spoken or
  translated sentence.
- Approach: warm both models when the window reaches `State::Ready` and the
  settings say they are enabled, on the existing loader thread. Keep a single
  owner per model and reuse the existing unload commands when settings change.
- Risk: low-medium. Adds steady-state memory and VRAM use even when the feature
  is idle, and the loader thread must not race a settings change. Gate it on
  available memory if the GPU copy is also warm.

### Smaller opportunities

- Move `TextProcessor` correction and jieba segmentation off the recognizer
  worker for final results. The stability split needs the text, but corrections
  are independent and could run with the GUI update.
- Compute the next partial's fbank while the GUI renders the previous one.
- Run clipboard composition and injection preparation before translation
  completes so only the final paste waits on the translation deadline.

## What was checked

### Overlapped VAD

- `cmake --build build-cpu-verify --config Release --target sensevoice-stream
  sensevoice-ui`: success. `ctest -C Release`: 8/8 passed.
- `cmake --build build-cuda-verify --config Release` with `CUDA_PATH` and
  `CUDA_PATH_V12_6` pointing at CUDA 12.6, through the `E:\sa-src` junction:
  success. `ctest -C Release`: 8/8 passed. Running the CUDA binary needs
  `...\CUDA\v12.6\bin` on `PATH` for `cublas64_12.dll`.
- Clip `E:/tmp-saytest/speech_zh.wav` (4.692 s, real-time replay,
  `models/sensevoice-small-q8.gguf`, `models/fsmn-vad.gguf`), three runs per
  path. `final_latency_ms` from the newest `accept_pcm` to the final event:

  | Build | Pipeline | final_latency_ms | vad_ms | inference_ms |
  | --- | --- | ---: | ---: | ---: |
  | CPU | serial (`SENSEVOICE_SERIAL_VAD=1`) | 375, 446, 472 | 66, 60, 63 | 251, 327, 329 |
  | CPU | overlap (default) | 441, 382, 383 | 63, 60, 60 | 319, 264, 263 |
  | CUDA | serial | 223, 211, 222 | 68, 61, 65 | 92, 92, 94 |
  | CUDA | overlap | 203, 193, 192 | 60, 60, 61 | 84, 90, 88 |

  Medians: CPU 446 ms serial versus 383 ms overlap, CUDA 222 ms serial versus
  193 ms overlap. The spread across runs is large, and the final still pays one
  VAD pass plus one full inference. The CUDA case is the clearer win because
  inference is short enough for the overlapped VAD pass to hide under it; on
  the CPU build SenseVoice inference dominates. All twelve runs produced
  exactly one final event and the same text:
  "今天天气很好，我们一起去公园散步。"
- A 3 s low-noise file exercised the no-speech path (repeated prefix trims and
  generation bumps) in both pipelines: exit 0, one final, empty text.
- An 18.8 s file made by repeating the clip four times, decoded with
  `--maximum-utterance-ms 5000` to force the budget split path, produced the
  same four finals in the same order and with the same 4240/4700/4680/5000 ms
  boundaries in both pipelines. No final was dropped or duplicated.
- Five fast replays (`--replay-speed 10`) finished with exit 0, one final and
  identical text each, which exercises `finish` racing the last partial and the
  VAD pass.
- The CLI prints the pipeline and VAD thread count:
  `FSMN-VAD loaded in 1 ms (4 threads, overlapped pipeline)`.
- `git diff --check` for whitespace errors.

### Shared fbank

- `cmake --build build-cpu-verify --config Release --target sensevoice-stream
  fbank-test fbank-benchmark`: success. `ctest -C Release`: 9/9 passed, with
  `fbank-test` added. `model-manager-test` still needs the Qt `bin` directory
  on `PATH`, as before.
- CUDA 12.6 configure and build through the `E:\sa-src` junction with
  `CUDA_PATH` and `CUDA_PATH_V12_6` set, targets `sensevoice-stream`,
  `fbank-test`, `fbank-benchmark`: success. `ctest -C Release`: 9/9 passed.
- `fbank-test` compares 1-thread and 4-thread raw features and LFR stacks with
  `memcmp` (no tolerance), checks the 498 / 83 / 498 frame shapes and the LFR
  padding rule, and checks the thread policy (4 cap, engine floor, 32-frame
  serial threshold, `SENSEVOICE_SERIAL_FBANK`).
- `fbank-benchmark 30` reported about 12.7 to 13.0 ms on one thread versus 3.6
  to 3.8 ms on four in the CPU build, and 12.6 to 12.9 ms versus 3.7 to 3.9 ms
  in the CUDA build, always with identical output.
  `SENSEVOICE_SERIAL_FBANK=1` made the 4-thread call take 12.77 ms, confirming
  the override.
- Twelve end-to-end runs (CPU and CUDA builds, serial and parallel fbank, three
  each) produced one final event and the same text per run; the timings are in
  the [Measured](#measured) table above.
- `git diff --check` reports no whitespace errors.

### Earlier compute and translation checks

- CPU configure, build and `ctest` in a dedicated `build-cpu-verify` directory:
  8/8 passed, including the GPU-independent backend and model-path tests.
- CUDA 12.6 configure, build and `ctest` through the ASCII junction: 8/8
  passed.
- `sensevoice-stream --audio E:/tmp-saytest/speech_zh.wav --translate zh-en`
  with `--device cpu`: Opus-MT (CTranslate2) on CPU, load 313 to 324 ms,
  translation 169 to 175 ms, output
  "It's a nice day. We'll go for a walk in the park."
- The same run with `--device gpu` on the CUDA build: Qwen3 GGUF on
  `GPU (CUDA: NVIDIA GeForce GTX 1080 Ti)`, load 2297 to 3123 ms, translation
  417 to 473 ms,
  output "The weather is really nice today, let's go for a walk in the park."
  The model path was the real `E:/myworkwindows/语音识别项目/SpeakAnything/modelspt`
  path, not the junction.
- `--device auto` on the CPU-only build used Opus-MT on CPU, as designed.
- The CPU-only build with `--device gpu` exited 1 with
  "GPU compute was requested, but no GPU backend is available. This build was
  compiled without CUDA."
