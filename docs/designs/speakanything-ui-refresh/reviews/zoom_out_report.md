# Goldfish Review Report

**Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (lines 1-529; FeatureRange 1-384, DetailedRange 385-529)
**Mode:** REVIEW — Stage 5 Initial Zoom Out Review
**Date:** 2026-11-22

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-zoom-out` | completed |
| `egm-inventive-catalyst` | completed |

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. The 600-line acceptance gate cannot pass, and the rule behind it has no documented source - Severity: Blocker
* **Reviewer Lens:** `egm-zoom-out`, `egm-inventive-catalyst`
* **Affected Scope:** Goals 7 (line 38), completion criteria (line 51), Open Questions (line 68), §8 (lines 268-289), Alternative C (line 328), verification plan (lines 506-514)
* **Discrepancy Description**: The design declares a repo-wide ceiling (“任何文件不超过 600 行”, line 38) and then defines a gate that scans every `.cpp`/`.h` under `src` and requires empty output (lines 509-514). Run verbatim against the current tree, that gate reports six files, five of which this design never touches. The document therefore cannot reach its own “done” state without an unplanned refactor of recognition and text-processing code, or without silently weakening the gate. Compounding this, no file in the repository records the 600-line rule: `SpeakAnything/AGENTS.md` is a 9-byte pointer containing only `CLAUDE.md`, and `CLAUDE.md` does not exist; `README.md`, `README.en.md`, and `docs/*.md` contain no such rule.
* **Evidence**:
  * “满足文件行数约束：把 UI 代码拆成职责清楚的文件，任何文件不超过 600 行；拆分阶段不得改变行为。”（line 38）
  * “输出必须为空。机械拆分阶段若暂时出现超限，必须在继续视觉改动前解决，不能把超限文件带入完成状态。”（line 514）
  * “四个文件仍超过 600 行，无法满足项目规则。”（line 328）
  * Verified with the document's own metric (`Get-Content ... .Count`): `src/qt_ui_main.cpp` 5,587; `src/sensevoice_engine.cpp` 1,108; `src/stream_recognizer.cpp` 916; `src/text_processor.cpp` 819; `src/windows_text_injector.cpp` 771; `src/main.cpp` 685.
  * `SpeakAnything/AGENTS.md` = `CLAUDE.md` (one line, 9 bytes); no `CLAUDE.md` at the project root.
* **Rationale & Recommendation**: Either the rule is project-wide — in which case the design's scope is insufficient and five non-UI files must be refactored — or it is scoped to touched UI files, in which case the verification command is wrong. Pick one and make it explicit. Recommended edit: state the rule's source and scope in Goal 7 (“owner-supplied constraint, applies to files created or modified by this design”), change the check to the changed-file set (e.g. `git diff --name-only -- 'src/*.cpp' 'src/*.h'`), or add an explicit exclusion list for the five pre-existing offenders. Then restate line 51 as the authoritative criterion and make lines 509-514 consistent with it.

### 2. The `VoiceInputWindow` “mechanical split” is a controller refactor bundled into a UI-only change - Severity: Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §1 改造边界 (line 107), Non-Goals (line 58), Open Questions (line 68), §8 (lines 281, 289), target files (lines 436-446)
* **Discrepancy Description**: §1 states the round only changes the settings dialog, yet the plan moves a 2,684-line controller into eleven new overlay translation units and simultaneously introduces `SettingsSnapshot` / `SettingsDraft` / `PageBundle` as new data-flow contracts. Nothing in The Problem (lines 18-26) mentions overlay controller size. No existing test exercises that controller: the nine `ctest` targets cover text, audio, compute, injector helpers and model management, not `VoiceInputWindow`. Gating this work on “screenshots identical” cannot validate it, because screenshots exercise only a few visual paths and cannot detect changed wiring of translation workers, speech queues, hotkeys, tray actions or injection state.
* **Evidence**:
  * “本轮只改设置对话框及其直接子页面。”（line 107）
  * “不改变识别、翻译、TTS、音频路由、文本注入、模型下载和设置持久化逻辑。”（line 58）
  * “本轮完成 `VoiceInputWindow` 的机械拆分，不记录 Phase A waiver；拆分阶段不改变行为。”（line 68）
  * “`VoiceInputWindow` 按方法组拆分，不改成员语义和执行顺序；拆分阶段不做逻辑重写。”（line 289）
  * Verified: `VoiceInputWindow` spans `src/qt_ui_main.cpp:2737-5420` (2,684 lines) and owns `translation_worker_`, `speech_queue_`, hotkey state, tray actions and injection state. “不改成员语义” is not achievable when member state must move with extracted method groups.
* **Rationale & Recommendation**: This is the highest-risk part of the change and delivers zero user-visible improvement to the settings dialog. Move `src/ui/overlay/*` out of this design into a follow-up ADR with characterization tests, and keep `qt_ui_main.cpp` as the overlay controller this round. If the split must stay, split step 3 into 3a “verbatim class moves, screenshots identical” and 3b “snapshot/draft/bundle extraction”, and gate 3b on the settings round-trip test rather than on screenshots. Alternatively, add one sentence to §1 that explicitly scopes the visual boundary and carves out overlay code migration as a separate, risk-accepted workstream.

### 3. Preview determinism is over-built for the problem and under-guaranteed where it matters - Severity: Major
* **Reviewer Lens:** `egm-inventive-catalyst`, `egm-zoom-out`
* **Affected Scope:** §7 验证层 (line 258), completion criteria (line 46), API 边界 (line 402), target files (lines 414-415), verification plan (line 491)
* **Discrepancy Description**: The design guarantees “no writes” by convention — “预览不调用 `QSettings::setValue` 或 `sync()`” — across a file that contains dozens of write sites, and then checks it after the fact with a registry export diff. At the same time it threads a preview-only `PreviewOptions` parameter into the production dialog constructor and adds two new modules. The existing code already offers cheaper determinism: `SPEAKANYTHING_LANG` already forces the UI language (`src/qt_ui_main.cpp:556-558`), the preview already passes an empty `AppearanceSettings{}` so the theme is deterministic (`:5556`), and `ModelManager*` is already injectable (`:1630`). The one real leak is `loadSettings()`, called unconditionally from the constructor (`:2767`), which writes `vad/profile_version` and calls `sync()` (`:3243-3260`) even when `preview_mode_` is true.
* **Evidence**:
  * “新增 `PreviewOptions`，强制指定语言、主题、页面和模型目录；预览不调用 `QSettings::setValue` 或 `sync()`；`--preview-settings` 缺少 `--preview` 时直接报错。”（line 258）
  * “`InputSettingsDialog(const SettingsSnapshot&, ModelManager*, const PreviewOptions&, QWidget*)`：生产路径传默认 `PreviewOptions`；预览路径强制指定页面、语言和主题。”（line 402）
  * “预览模式不写注册表，页面、导航选中态和截图输出可重复。”（line 46）
  * Verified write path: `src/qt_ui_main.cpp:5534` constructs `VoiceInputWindow(preview_mode=true)`; `:2767` calls `loadSettings()`; `:3259-3260` executes `settings_.setValue("vad/profile_version", 5); settings_.sync();` without a preview guard.
* **Rationale & Recommendation**: A per-site “do not call the writer” contract is unenforceable as the file is split, and it does not cover the geometry script, which launches plain `--preview`. A single guard around the migration writes protects every preview path. Recommended: add `if (!preview_mode_)` around the writes in `loadSettings()`; keep `--preview-page` and `--preview-manifest`; use `SPEAKANYTHING_LANG` instead of `--preview-language`; drop `PreviewOptions` from the production constructor and let the harness call `selectPage()`. For a hard guarantee, redirect the default backend (`QSettings::setDefaultFormat(QSettings::IniFormat)` + `setPath`) and assert the file is unchanged; keep the HKCU diff as defense in depth and extend it to the geometry script.

### 4. Minimum window width is simultaneously 680 and 760 - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`, `egm-inventive-catalyst`
* **Affected Scope:** completion criteria (line 43), §3 设置壳层 (line 177), Alternative K (line 377), manual walkthrough (line 504)
* **Discrepancy Description**: The acceptance criterion still promises 680×540, while the shell section raises the minimum width to 760 and the manual check restates 760. One of the two acceptance tests must fail, and a reader cannot tell which number is the contract. The current code confirms today's floor is 680 (`src/qt_ui_main.cpp:1640`).
* **Evidence**:
  * “八个页面在 `880x740` 和最小 `680x540` 窗口下可访问，不出现横向滚动或文字裁切。”（line 43）
  * “默认窗口保持 `880x740`，最小宽度提高到 `760`，最小高度保持 `540`。”（line 177）
  * “采用 184 px 侧栏并把最小宽度提高到 760 px，优先保证设置内容可读。”（line 377）
  * `src/qt_ui_main.cpp:1640`: `setMinimumSize(680, 540);`
* **Rationale & Recommendation**: If 760 is the intended floor because a 184 px sidebar leaves too little content at 680, then line 43 is false and must be rewritten. Recommended: replace `680x540` with `760x540` in line 43, make §3 the single place that defines the number, and have the verification section reference “the minimum from §3” instead of restating it. If a 680 floor is a product requirement, the sidebar must adapt (e.g. collapse to an icon rail) — a larger change that should be stated explicitly.

### 5. “恢复表单默认值” has no defined scope and conflicts with live-applied appearance - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`, `egm-inventive-catalyst`
* **Affected Scope:** §5 外观 (line 215), §5 VAD (line 242), §6 提交语义与页脚 (line 252), API 边界 (lines 399-400)
* **Discrepancy Description**: The footer reset is described as resetting “当前表单字段” but never says whether that means the current page or the whole dialog. Today's implementation resets every deferred field across all pages and already excludes appearance, startup, compute, hotwords and model choices (`src/qt_ui_main.cpp:1755-1780`). The VAD section separately says reset only affects existing VAD fields, which implies per-page behavior — a change to current behavior and therefore a contradiction with the design's “no behavior change” claim. The same ambiguity appears in the API: `SettingsDraft` is described as reading back “同一组字段”, which per line 399 includes appearance, even though appearance is applied and saved immediately through a callback (`:3541-3545`) and is not consumed on accept. A caller treating the draft as complete post-accept state could revert appearance and break CUJ 5.
* **Evidence**:
  * “页脚左侧改为‘恢复表单默认值’，只重置当前表单字段，不删除模型、不关闭下载、不改外观即时值。”（line 252）
  * “恢复默认只影响当前表达式中已有的 VAD 字段。”（line 242）
  * “外观页修改立即应用并保存，页内标注‘即时生效’。”（line 248）
  * “页面顶部固定显示‘即时生效，取消不会回滚’的说明。”（line 215）
* **Rationale & Recommendation**: State explicitly that the footer resets every Apply-bound field across pages (today's behavior), and that appearance, model, startup, compute and hotwords are untouched; delete or relocate the page-specific reset sentence at line 242. In §API, state that `SettingsDraft` excludes live-applied appearance (or marks it as already committed) so the three commit semantics remain representable. If per-page reset is actually wanted, call it out as an approved behavior change with its own CUJ.

### 6. The settings inventory is circular and its “never update the baseline” rule forbids intentional relabeling - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §7 验证层 (line 260), failure handling (line 519), §3 页面标题 (line 185), §5 模型 (lines 232-235)
* **Discrepancy Description**: The inventory records page, object name, type, label, default and current value, but the codebase has only 17 `setObjectName` calls in 5,587 lines, and the dialog-internal ones are mostly styling hooks with duplicates (`card`, `cardTitle`, `cardHint` repeat). To inventory by object name, step 2 must first add stable names to roughly a hundred controls — a code change that is neither “verification only” nor listed in the implementation steps. Meanwhile the design intentionally rewrites labels and hierarchy (single page title, redefined model rows), so a label-and-default comparison will legitimately differ — yet line 519 forbids updating the baseline to explain the diff.
* **Evidence**:
  * “记录页面、控件对象名、控件类型、标签、默认值和当前值；改造前后做结构化比较。”（line 260）
  * “设置清单不一致：补齐控件或恢复原对象名，不允许更新基线掩盖缺失。”（line 519）
  * “内容区只保留一个主标题，不再重复窗口标题。”（line 185）
  * Verified: 17 `setObjectName` calls; styling hooks at `src/qt_ui_main.cpp:1702, 1707, 1746, 2058, 2069, 2074, 2078, 2090, 2104, 2155, 2264`.
* **Rationale & Recommendation**: A widget-level inventory genuinely catches a dropped control that a value-only comparison would miss, so keep the idea but make it buildable. Recommended: add step 2a “assign stable `objectName`s to interactive controls only”; define the fixture as page → control ID → type → current value (no labels, no defaults); compare the accepted `SettingsDraft` per page for behavior; allow baseline updates when the diff is explained in the change description; keep per-page screenshots for layout regressions.

### 7. The translation gate passes today while the defect it cites is present - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §7 验证层 (line 262), failure handling (line 521), Goal 8 (line 39), target file (line 452)
* **Discrepancy Description**: The gate checks “zero `unfinished`, zero `vanished`”, but strings that were never extracted are not marked `unfinished`; they only become unfinished after an `lupdate` pass. The current catalogue is already green on those two counters while current Models-page strings are absent, so the English UI already falls back to Chinese — exactly the defect the design promises to prevent.
* **Evidence**:
  * “保持单一 `SpeakAnything` 上下文；检查零 `unfinished`、零 `vanished`，并确认触点页面字符串存在英文翻译。”（line 262）
  * Verified: `resources/i18n/speakanything_en.ts` has 227 `<message>` entries, one `SpeakAnything` context, 0 `unfinished`, 0 `vanished` — and zero occurrences of `模型目录`, `导入模型`, `浏览器下载`, or `语音输入设置`, all of which exist as `Ui::tr(...)` source strings (`src/models_page.cpp:92, 156, 231-232`; `src/qt_ui_main.cpp:1637`).
* **Rationale & Recommendation**: The gate validates a property that is already true rather than the property that matters (every source string has an English translation). Recommended: require an `lupdate`-equivalent pass before the check, or write `check_translations.cmake` to extract every `tr(` / `translate("SpeakAnything", ...)` literal from the touched pages and assert each has a non-empty translation. Add an explicit allowlist of new navigation/page strings so “zero missing” is meaningful.

### 8. Irreversible model deletion is mitigated only by a text label - Severity: Major
* **Reviewer Lens:** `egm-inventive-catalyst`
* **Affected Scope:** §5 模型 (line 238), §6 提交语义 (line 249)
* **Discrepancy Description**: The design keeps deletion instant and irreversible and treats “删除后需重新下载” as sufficient mitigation. That is passive notification standing in for active mitigation: a mis-click destroys a multi-GB download with no recovery. The current code calls `manager_->remove(*model)` directly in the click handler with no confirmation or recovery step.
* **Evidence**:
  * “模型删除仍即时生效且不可恢复。界面在删除按钮附近显示‘删除后需重新下载’，但不新增确认对话框，除非后续批准行为变更。”（line 238）
  * “模型目录、导入、删除和下载立即生效，卡片内标注状态。”（line 249）
  * `src/models_page.cpp:274-279`: click handler calls `manager_->remove(*model);` with no confirmation.
* **Rationale & Recommendation**: If no behavior change is allowed this round, say so as an accepted risk rather than presenting the label as mitigation, and schedule a follow-up. If a low-friction fix is allowed, quarantine instead of delete: move the model directory instantly to `.trash/` on the same volume, hide the row immediately, show “已删除 · 撤销”, and purge on next launch or after N days. This preserves instant semantics and adds no dialog, but it does add model-manager state, a startup cleanup path, and tests — declare those costs explicitly.

### 9. The macOS adapter contract contradicts itself and has no parity artifact - Severity: Major
* **Reviewer Lens:** `egm-inventive-catalyst`
* **Affected Scope:** §2 视觉系统分层 (lines 141, 159), target file (line 418), failure handling (line 522)
* **Discrepancy Description**: The architecture says the macOS adapter is a palette mapping with native controls, but the implementation table says `design_tokens` provides a “Windows/macOS QSS adapter” — a QSS adapter for macOS contradicts the palette/native-control decision. Beyond the wording, the design asserts that layout, type, spacing and focus rules are shared across platforms but defines no check for that sharing; the only macOS mitigation is an unverifiable pre-release gate, and no completion criterion covers macOS parity.
* **Evidence**:
  * “表面适配器：Windows QSS、macOS 调色板、浮窗主题解析器。”（line 141）
  * “共享布局、字号、间距和焦点规则，不共享固定十六进制表面色。”（line 159）
  * “定义间距、圆角、字号、语义角色和 Windows/macOS QSS 适配器。”（line 418）
  * “无法在当前 Windows 环境验证 macOS：记录未验证项，要求 macOS 构建或源码评审作为发布前门禁。”（line 522）
* **Rationale & Recommendation**: Fix line 418 to “Windows QSS adapter + macOS palette role mapping” so the document stops promising a macOS QSS path. Add a cheap headless parity artifact: `design_tokens::resolve(role, Platform)` resolved for both platforms against golden manifests, with any macOS divergence (surface colors, system accent) in an explicit allowlist. Add a macOS row to the completion criteria so “record unverified” is a tracked gate rather than a footnote.

### 10. The three-layer token system is one layer too many, and `settings_layout` re-authors helpers that already exist - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §2 视觉系统分层 (lines 137-143), §8 table (lines 275-277), target file (line 421)
* **Discrepancy Description**: The dialog has exactly two platform branches: six macOS rules (`src/qt_ui_main.cpp:1644-1653`) and one Windows QSS block (`:1655-1685`). An “adapter” interface over two `#ifdef` branches is speculative; a scale table plus a renderer function per surface is the whole requirement. Separately, the plan creates `settings_layout.h/.cpp` without mentioning the existing helpers that already implement the row/card vocabulary: `scrollable` (`:2057`), `addCard` (`:2067`), `addRow` (`:2086`), `carded` (`:2100`), `pageLayout` (`:2112`), `addSlider` (`:2614`). `ModelsPage` already duplicates a near-identical card builder at `src/models_page.cpp:214-235`. Creating a new module while leaving the duplicate produces a third copy of the same pattern.
* **Evidence**:
  * “不使用一个全局调色板覆盖所有表面。采用三层结构… 3. **表面适配器**：Windows QSS、macOS 调色板、浮窗主题解析器。”（lines 137-141）
  * “`src/ui/settings/settings_layout.h/.cpp` | 创建 | 提供卡片、行、分组标题、滑杆、分段按钮和提示文本助手。”（line 421）
* **Rationale & Recommendation**: Collapse layer 3 into “the existing per-surface renderers (Windows QSS function, macOS palette function, overlay theme resolver)” and keep the token layer to scale + semantic roles. State that `settings_layout` is a migration of `addCard`/`addRow`/`carded`/`pageLayout`/`addSlider`, not new code, and include `ModelsPage::addRow` in that migration. Without this, the consistency goal adds a copy rather than removing one.

### 11. Byte-identical hotword round-trip is fragile through the UI's spin box - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** completion criteria (line 45), §7 验证层 (line 261)
* **Discrepancy Description**: The UI parses boost through `QDoubleSpinBox` with `setDecimals(1)` (`src/qt_ui_main.cpp:2567-2571`), and the serializer writes `entry.boost` with default stream formatting (`src/text_processor.cpp:650-651`). A fixture containing `12.0` is rewritten as `12`, and `1.25` rounds to `1.3`, so SHA-256 changes for reasons unrelated to the UI. The test conflates semantic round-trip with byte formatting and can fail on a correct implementation.
* **Evidence**:
  * “`hotwords.tsv` 对包含中文、别名、增强值、命中数和‘不翻译’标记的样例保持字节级一致。”（line 45）
* **Rationale & Recommendation**: Add one sentence: the fixture is generated by the current save path, and boosts are limited to one decimal. Alternatively compare untouched rows byte-for-byte and edited rows field-by-field.

### 12. “项目内栅格图标” does not exist - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §3 设置壳层 (line 183)
* **Discrepancy Description**: The design offers existing `QStyle` icons or “项目内栅格图标”, but `resources/sensevoice.qrc` contains exactly one file, `sensevoice.ico`; there are no PNG/raster icon assets in `resources/`, and the target-files table adds none. The option is unavailable and invites either unplanned asset work or ad-hoc icons.
* **Evidence**:
  * “图标使用现有 `QStyle` 标准图标或项目内栅格图标，不新增 SVG 依赖。”（line 183）
* **Rationale & Recommendation**: Change to “QStyle standard icons only for this round”, or add the intended raster assets to the target-files table explicitly.

### 13. `src/main.cpp` already exists, but the target table does not account for it - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** target files (line 413)
* **Discrepancy Description**: The plan creates `src/app/main.cpp` for the UI entry point, while `src/main.cpp` already exists (685 lines) as the `sensevoice-stream` CLI entry (`CMakeLists.txt:328-331`). Two `main.cpp` files are legal, but the table reads as if the UI entry point is the only `main`, which is a review and build-debugging hazard.
* **Evidence**:
  * “`src/app/main.cpp` | 创建 | 接收原 `main()`、参数解析、单实例和预览分派；保持入口行为不变。”（line 413）
* **Rationale & Recommendation**: Add a row for `src/main.cpp` — “unchanged, CLI entry for `sensevoice-stream`” — and consider naming the new file `src/app/ui_main.cpp` to make the distinction obvious.

### 14. §7 验证层 duplicates the Detailed Plan at implementation altitude - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** §7 验证层 (lines 254-264) versus lines 390, 397-404, 464-522
* **Discrepancy Description**: `PreviewOptions` and its flags appear at both line 258 and line 404; `pageCount()`/`selectPage(int)` at both line 259 and line 403; the inventory at line 260 and again at line 390 with commands at 477-482; the regression list at line 264 and again at line 500. The architecture section names concrete types and CLI flags before the plan section that should own them, and any later change will silently desynchronize one copy.
* **Evidence**:
  * “新增 `PreviewOptions`，强制指定语言、主题、页面和模型目录…”（line 258） versus “`PreviewOptions`：解析 `--preview-page`、`--preview-language`、`--preview-manifest`…”（line 404）
* **Rationale & Recommendation**: Reduce §7 to five outcome statements (no registry writes; deterministic page/language/theme; inventory gate; translation gate; offscreen layout test) and move all type/flag names and commands into the plan — one owner per fact.

### 15. Target-file table has stale entries - Severity: Nit
* **Reviewer Lens:** `synthesizer` (source verification)
* **Affected Scope:** target files (lines 461-462)
* **Discrepancy Description**: The table says to create `docs/designs/README.md`, but that file already exists and already registers this design (status Proposed) — following the table literally risks overwriting it. The next row says to modify `docs/designs/speakanything-ui-refresh/design_draft.md`, but no such file exists; the directory contains only `concepts_brief.md` and this design document.
* **Evidence**:
  * “`docs/designs/README.md` | 创建 | 登记本设计文档和后续评审入口。”（line 461）
  * “`docs/designs/speakanything-ui-refresh/design_draft.md` | 修改 | 完成最终校对、矛盾核对和设计定稿。”（line 462）
* **Rationale & Recommendation**: Change line 461 to `修改` and describe the actual edit (the entry already exists). Replace the `design_draft.md` row with the real artifact it means — this file, or `concepts_brief.md` — and delete the row if no further edit is intended.

### 16. “16 个分散 getter” is actually 14 - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out` (verified by synthesizer)
* **Affected Scope:** API 边界 (line 400)
* **Discrepancy Description**: The document claims the draft replaces “16 个分散 getter”. The `InputSettingsDialog` accessor block at `src/qt_ui_main.cpp:1792-1898` contains exactly 14 `... () const` getters (`values`, `shortcut`, `mode`, `pasteDelayMs`, `clipboardTtlSeconds`, `trigger`, `inputContent`, `clipboardContent`, `destination`, `startupEnabled`, `muteWhileRecording`, `translation`, `speech`, `hotwords`).
* **Evidence**:
  * “`SettingsDraft`：从页面控件读回同一组字段；不再保留 16 个分散 getter。”（line 400）
* **Rationale & Recommendation**: Correct the number to 14 or drop the count; precise counts elsewhere in the document are accurate, so this one weakens trust unnecessarily.

## What Survives Scrutiny

* **The problem diagnosis is code-accurate.** The sidebar is non-focusable (`src/qt_ui_main.cpp:1704`) and 168 px wide (`:1703`); the dialog still uses `setMinimumSize(680, 540)` (`:1640`); all eight pages exist (`:1712-1726`); and preview really does write the registry today (`:5534` → `:2767` → `:3259-3260`). The motivation is not speculative.
* **The scale claims are exact.** `qt_ui_main.cpp` is 5,587 lines; `InputSettingsDialog` is 1,127 lines (1,609-2,735); `VoiceInputWindow` is 2,684 lines (2,737-5,420); there are exactly nine `add_test` entries; the dialog constructor takes 22 parameters. The 600-line problem is real even though the gate around it is broken.
* **Context sufficiency and root-problem focus pass.** Objective, motivation, non-goals, five CUJs and the visual diagnosis are unusually clear and grounded in the code. The failure is scope, not comprehension.
* **Verify-first sequencing is justified.** The current preview cannot prove a read-only run, so building the instrument before measuring is correct; Alternative E is genuinely worse. The defect is the instrument's guarantee (Finding 3), not the order.
* **Settings-surface vs overlay-identity separation is sound.** It is Extraction, not duplicated theming: overlay colors are user data under `appearance/*`, while the token layer carries dimensions and roles. Alternatives B, H, K and L are argued correctly.
* **Reuse instincts are mostly right.** Deduplicating `to_qstring` is justified — it exists three times (`src/qt_ui_main.cpp:187`, `src/models_page.cpp:22`, `src/model_manager.cpp:18`) — as is sharing `SENSEVOICE_UI_SOURCES` (the Windows/macOS source lists are duplicated at `CMakeLists.txt:373-384` and `476-490`). Avoiding `Q_OBJECT`/AUTOMOC for a same-class multi-TU split is a legitimate constraint, not a workaround.
* **No premature optimization.** There is no speculative caching, async queue, sharding or plugin architecture. The YAGNI exposure is confined to the preview type and the third token layer.
* **Not verifiable in this environment:** macOS runtime behavior and screenshots; whether 760×540 with a 184 px sidebar actually avoids clipping in the six-column hotword table at 150% scaling; and the provenance of the 600-line rule outside the repository.
