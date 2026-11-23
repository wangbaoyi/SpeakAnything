# Goldfish Review Report

**Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (lines 1-384)

**Mode:** REVIEW. Scope ends at line 384; `## Detailed Implementation Plan` (line 385) and everything after were excluded.

**Reviewers:** `egm-design`, `egm-structural`, `egm-style` — three independent fresh-context lenses, each asked to verify claims against the repository. Repository line references below are evidence, not target-document locations; finding scopes use target-document lines.

## Reviewer Status

| Reviewer | Lens | Status |
| --- | --- | --- |
| `egm-design` | Design critic (standard: `feature-spec-standard`) | completed |
| `egm-structural` | Structural / template compliance (standard: `feature-spec-standard`) | completed |
| `egm-style` | Technical copy editor (no standard) | completed |

**Tally:** 1 Blocker, 12 Major, 13 Nits. 26 findings after deduplication from 37 raw findings.

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. "UI-only" scope conceals a 2,684-line controller extraction with no behavior tests - Severity: Blocker
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 6, 14, 107 against lines 268-291 and 328 (also the `src/ui/overlay/*` row at line 281)
* **Discrepancy Description**: The document asserts a UI-only boundary — "本轮只改设置对话框及其直接子页面" (line 107) and "不改识别、翻译、朗读、文本注入…" (line 14) — but its largest work item is splitting the 2,684-line `VoiceInputWindow` into `src/ui/overlay/*`, covering "浮窗控制器、托盘、快捷键、会话、翻译、朗读、注入" (line 281), plus the 5,587-line `qt_ui_main.cpp` (line 268). Verified: `VoiceInputWindow` spans `src/qt_ui_main.cpp:2737-5420` and owns `loadSettings()` (3230), hotkey registration (2800-2814), translation/speech setup (2785-2786), and tray construction (2769-2771). The only regression evidence is ctest plus overlay screenshots/geometry; `CMakeLists.txt:587-692` registers no tray, hotkey, session, translation-lifecycle, or injection test. The 600-line rule that forces the split is also unrecorded: the project `AGENTS.md` contains only `CLAUDE.md`, `CLAUDE.md` does not exist, and no 600-line rule appears in either README or `docs/`.
* **Rationale & Recommendation**: Moving controllers that own process-wide behavior is not a mechanical UI change, and "behavior unchanged" is currently an assertion rather than a tested property. Choose one path: (a) remove controller extraction from this design and make it a separate refactor with its own behavior tests and rollback plan, or (b) explicitly widen the scope and record a waiver, enumerate the extracted classes and ownership, and add pre/post smoke coverage for hotkey registration, tray actions, session start/stop, translation/speech lifecycle, and text injection. Record the 600-line rule's source (or a grandfather list) so the constraint is auditable.
* **Counter-argument**: The split is intended to be mechanical, the 600-line rule may be a known project convention not stored in the repo, and moving code now prevents the file from growing further. That is a legitimate planning choice — but without behavior tests and a recorded rule, neither "mechanical" nor "required" can be reviewed.

### 2. Minimum window width contradicts itself, and sidebar/table fit at the minimum is undefined - Severity: Major
* **Reviewer Lens:** `egm-design`, `egm-structural`, `egm-style`
* **Affected Scope:** line 43 against lines 177, 183 and 376-377; also the minimum-size assertion at line 263
* **Discrepancy Description**: The acceptance criterion requires eight pages to work at "最小 `680x540`" (line 43), while §3 raises the minimum width to `760` (line 177) and Alternative K explicitly rejects the 680 px option (lines 376-377). Current code is `resize(880, 740)` and `setMinimumSize(680, 540)` at `src/qt_ui_main.cpp:1639-1640`, so line 43 reads as a stale copy of today's behavior. Line 263 turns "最小尺寸" into a test assertion without naming a value, so a test author cannot tell whether 680 or 760 is the gate. Also unaddressed: at `540` height, four group headers plus eight items at "至少 40 px" (line 183) exceed the roughly 400 px body area, so the grouped sidebar must scroll or compact; line 43 forbids "横向滚动" without saying whether that applies to the page or to the six-column hotword table at `760` width.
* **Rationale & Recommendation**: The primary layout acceptance criterion and the architecture cannot both be satisfied. Use one minimum (`760x540`), update line 43, make line 263 assert that value, define sidebar behavior at minimum height, and state whether the hotword table may scroll internally. If `680x540` is a compatibility contract, say so and prove the six-column table fits; otherwise withdraw it.
* **Counter-argument**: A careful reader follows §3 and Alternative K and treats 680 as stale. That still leaves contradictory normative text in the first place QA reads, and `QListWidget`/`QTableWidget` auto-scrolling may make the layout functional without new rules — but the document must decide, not leave it implicit.

### 3. Preview is neither write-free nor deterministic under the described mechanism - Severity: Major
* **Reviewer Lens:** `egm-style`, `egm-design`
* **Affected Scope:** lines 26, 46, 133, 258-259; existing flag contract at line 258
* **Discrepancy Description**: The plan requires preview not to call `QSettings::setValue`/`sync()` (line 258) and to produce reproducible screenshots (line 46). But preview constructs `VoiceInputWindow` (`src/qt_ui_main.cpp:5516-5548`), whose constructor calls `loadSettings()` unconditionally (2767), and `loadSettings()` always writes `vad/profile_version` and calls `sync()` (3259-3260), plus VAD migrations (3244-3258). `PreviewOptions` forcing language/theme/page/model directory does not isolate the settings store, device enumeration (2463-2468), GPU detection (2143-2154), model status, or speech voices, so screenshots remain machine-dependent. The verification section only asserts absence of `setValue` in the preview branch; a behavior test that snapshots the store before and after preview is missing. Separately, `--preview-settings` already exists as the output directory for settings PNGs (`src/qt_ui_main.cpp:5466`, 5550-5566); line 258 gives it a new meaning without saying whether it is repurposed or extended.
* **Rationale & Recommendation**: The stated mechanism does not achieve the stated guarantee. Inject or redirect the settings store (temporary INI/registry path) before constructing any window, inject fixture device/model catalogs, and run preview through that store. Add two assertions: (1) the store snapshot before/after preview is identical; (2) after `selectPage(i)`, both the stack index and sidebar row equal `i` and the screenshot is stable. Define the `--preview-settings` contract, or rename the new flag to avoid colliding with the existing screenshot-directory flag. Reword line 46 to cover "Windows registry / macOS plist".
* **Counter-argument**: A no-write flag or skipping migrations in `loadSettings()` is a small fix and may be considered sufficient. It still leaves the real registry being read, so previews stay machine-dependent — the missing decision is store isolation, not one guard.

### 4. The standard's mandatory Risks & Limits section is absent - Severity: Major
* **Reviewer Lens:** `egm-design`, `egm-structural`
* **Affected Scope:** document structure between the Technical Plan (ends line 309) and `## Alternatives Considered` (line 311); the §9 constraints at lines 293-299 are not a risk analysis
* **Discrepancy Description**: The feature-spec standard requires the specification to identify failure modes, security implications, scalability limits, and mitigations. The document has constraints (§9) and scattered notes (lines 133, 238, 252), but no risk register: no trigger, impact, detection, mitigation, or owner. Uncovered failure modes include QSS/focus styling on `QTableWidget`/`QSlider`/`QCheckBox`; tab-order regressions from making the sidebar focusable (currently `Qt::NoFocus`, `src/qt_ui_main.cpp:1704`); macOS dark-mode contrast; 150% DPI and English-text clipping; destructive model deletion (line 238); preview data integrity; and controller-split regressions (Finding 1). The word 风险 appears once, inside Alternative E's rationale (line 341).
* **Rationale & Recommendation**: Add `## Risks & Mitigations` before Alternatives, as a table: Risk | Trigger/Failure mode | Impact | Mitigation | Verification | Owner. Cover at minimum the items above, and state explicitly that security/scalability exposure is N/A for a local Qt Widgets UI rather than omitting it silently.
* **Counter-argument**: Risk material is partly distributed across Non-Goals, §9, and each Alternative's "Cons", so a reader can reconstruct much of the surface. Distribution is not a register: nothing pairs a failure mode with a mitigation, which is what the standard asks for.

### 5. The visual target is not concrete enough to build or verify - Severity: Major
* **Reviewer Lens:** `egm-design`, `egm-style`
* **Affected Scope:** lines 12, 28, 137-159, 183; also lines 315, 333, 335, 345, 357, 375
* **Discrepancy Description**: The core visual deliverable has named roles (`shell.background`, `surface.card`, `text.primary`, `text.muted`, `border.default`, `focus.ring`, `selection.background`, `status.*`, line 140) but no concrete color values. "浅灰蓝" and "单一蓝色交互强调色" (lines 12, 159) are undefined, and no reference image is embedded or linked — `docs/designs/speakanything-ui-refresh/` contains only Markdown. There is no font family/fallback or pt-vs-px policy, although the app currently sets `QFont("Microsoft YaHei UI", 10)` (`src/qt_ui_main.cpp:5507`), and no Windows dark/high-contrast policy. "项目内栅格图标" (line 183) does not exist: `resources/` holds `sensevoice.ico`, an unused `sensevoice.svg`, and no raster icon set; `resources/sensevoice.qrc` embeds only the ICO. The 3:1 focus contrast at line 157 cannot be checked without an actual focus color. Existing QSS colors (`#F7F8FA`, `#FFFFFF`, `#E3E6EB`, `#356AE6`, `#EAF0FF` at `src/qt_ui_main.cpp:1656-1685`) could be reused, but the document does not say which are retained or replaced, and the current amber slider `#E6A52C` contradicts "single blue accent".
* **Rationale & Recommendation**: Add a token table with exact light-mode values for background, card, border, text, muted text, accent, focus ring, and status colors, with contrast ratios. State font family/fallback and px policy, define which existing colors are replaced, attach or link the reference image (or name the source application and revision), and decide the icon strategy: QStyle-only, or add a raster asset set. "Not pixel-perfect" still requires a shared target and concrete values.
* **Counter-argument**: The design explicitly says it does not chase pixel fidelity with the reference (line 62), and an implementer can lift colors from the existing QSS. That covers neither the new single-accent hierarchy nor contrast verification, and different implementers will still pick different blues.

### 6. The token model contradicts itself in namespaces, layer count, scale, and row height - Severity: Major
* **Reviewer Lens:** `egm-style`, `egm-structural`, `egm-design`
* **Affected Scope:** lines 140-143, 151-156, 183, 306
* **Discrepancy Description**: Four internal conflicts. (a) Line 140 defines `surface.card`, `text.primary`, `text.muted`, `border.default`, `focus.ring`, `selection.background`, then line 143 says "设置窗只读取 `shell.*` 和 `status.*`" — excluding exactly the roles cards, text, borders, and focus need. The reader cannot tell whether `shell.*` is an umbrella namespace or a single key. (b) Line 137 says "采用三层结构" while line 306 says the ADR records "两层 token"; whether the adapter is a token layer is left to inference. (c) The scale is `11, 12, 13, 15, 22, 26` (line 151) but roles use "页面标题 22 到 24 px" and "卡片标题 14 到 15 px" (lines 152-153); 14 and 24 are not steps, and the ranges imply 23. (d) "行高 | 最小 44 px" (line 156) is unscoped and conflicts with "导航项高度至少 40 px" (line 183); line 263 turns "行高" into an automated assertion, so a test written to 44 will flag the sidebar.
* **Rationale & Recommendation**: Make the contract closed and unambiguous. Either nest roles under `shell.*` (`shell.surface.card`, `shell.text.primary`, …) or list every namespace the settings window reads. Rewrite line 306 as "基础尺度与语义角色两层 token，加表面适配器". Add 14 and 24 to the scale or snap roles to existing steps (`15`, `22`, `26`), and replace ranges with single tokens. Rename 行高 to "设置行最小高度 44 px" and state that 40 px navigation rows are a separate density exception, or raise them to 44.
* **Counter-argument**: `shell.*` may be intended as shorthand for the whole settings palette, the table says "建议基础值" (guidance, not a closed set), and 40 px nav rows may be a deliberate density choice. If so, the document must say which values are binding and list the exceptions — otherwise the reader cannot tell guidance from rule.

### 7. The hotword byte-identical guarantee is contradicted by the current saver and by the proposed test - Severity: Major
* **Reviewer Lens:** `egm-design`, `egm-style`
* **Affected Scope:** lines 45, 89, 261, 298
* **Discrepancy Description**: Line 45 requires `hotwords.tsv` to stay "字节级一致" for a sample with Chinese, aliases, boost values, hit counts, and the "不翻译" flag; CUJ 3 (line 89) promises "未编辑行的字节内容不变"; line 261 verifies with a single whole-file SHA-256. But `TextProcessor::save_hotwords` (`src/text_processor.cpp:635-658`) opens with `std::ios::trunc`, rewrites the header comment, and streams every row; `boost` is a `float` (`src/text_processor.h:15`), so legacy `3.0` serializes as `3`; `load_hotwords` accepts 4/5-field rows (610-623) and expands them to 6 fields; CRLF becomes LF. A whole-file hash necessarily changes when any row is edited, so it cannot prove untouched rows stayed byte-identical, and a no-op round-trip passes without exercising edit/delete.
* **Rationale & Recommendation**: Split the criterion into (1) no-edit load-to-save produces a byte-identical file and matching SHA-256, and (2) after editing one row, untouched rows are byte-identical and the edited row matches expected TSV serialization. Compare row-level bytes or a fixture diff. Reword line 45 to "无编辑往返保持字节级一致；编辑后未编辑行字节不变". If real legacy files must round-trip byte-identically, that requires a line-preserving save path and is a separately approved behavior change, not a non-goal-compatible side effect.
* **Counter-argument**: The "固定样例" can be generated by the current saver, so SHA-256 equality can pass for that fixture. CUJ 3 nevertheless speaks about real legacy files, which the current saver rewrites.

### 8. The settings-inventory comparison cannot pass as specified - Severity: Major
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 44, 260-261
* **Discrepancy Description**: The manifest records "页面、控件对象名、控件类型、标签、默认值和当前值" and requires a "结构化比较" before/after (line 260). The redesign deliberately regroups pages (lines 178-182), renames/reparents controls, and changes composition (model cards 231-236, hotword header/empty state 227, output segments 211), so structural equality cannot hold by design. "当前值" is also non-deterministic: device combos enumerate real hardware (`src/qt_ui_main.cpp:2463-2468`), GPU detection is machine-dependent (2143-2154), model status comes from `src/models_page.cpp`, appearance from the user registry, and hotword `hits` changes with recognition. Most controls have no object name today — 17 `setObjectName` calls exist, mostly `card`, `cardTitle`, `cardHint`, `page`, `settingsSidebar`, `settingsStack`, `modeSegment`, `primary` — so the baseline manifest is not capturable without adding names, itself a change.
* **Rationale & Recommendation**: Key the inventory on stable setting IDs or `QSettings` keys, compare semantic type/default/value, treat layout, object names, and labels as non-normative, and exclude or fixture runtime-derived values. Check in a golden JSON with a diff command so "逐项一致" becomes executable.
* **Counter-argument**: A human can map renamed controls, and the intent is to catch lost settings rather than freeze layout. "结构化比较" and "逐项一致" imply machine equality, which the redesign makes impossible.

### 9. "恢复表单默认值" has no defined scope and may be a behavior change - Severity: Major
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** line 252; current behavior at `src/qt_ui_main.cpp:1755-1779`
* **Discrepancy Description**: The footer becomes "恢复表单默认值" and "只重置当前表单字段" (line 252). Current behavior resets fields across all pages: VAD sliders, shortcut, text mode, destination, content/clipboard content, trigger, mute, paste delay, clipboard TTL, translation enabled/direction/separator, and speech enabled/monitor/voices/speed (1755-1779); it does not reset the startup checkbox or hotwords. "当前表单" can mean the current page or the whole dialog draft. Page-scoped reset would be a behavior change, contradicting the Non-Goal; dialog-scoped reset still leaves startup, hotwords, model choices, and appearance unspecified (the document excludes only models/downloads/appearance).
* **Rationale & Recommendation**: Enumerate the reset set per page and state whether it is dialog-wide or page-local; explicitly include or exclude startup, hotwords, model choices, and appearance; add a test snapshotting all draft fields before and after reset.
* **Counter-argument**: "表单" naturally means the whole dialog draft, and the current reset is already dialog-wide, so perhaps only the label changes. "当前" plus the new label suggest a narrower scope, and the document claims no behavior change.

### 10. The `Action` variant's three-button cap conflicts with the model card - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** line 196 against lines 235 and 334
* **Discrepancy Description**: The variant table defines "Action | 一至三个按钮" (line 196), but the model card specifies "下载为主按钮；浏览器下载、导入、删除为次要按钮" (line 235) — four actions — and Alternative D refers to "带四个操作的模型行" (line 334). If all four can be visible, the taxonomy has no valid class for the model row; if they are mutually exclusive, the document never says so. Line 236's "不可用操作隐藏" is not enough to tell an implementer whether the visible count is capped at three.
* **Rationale & Recommendation**: Either extend Action to "一至四个按钮" with wrap/overflow behavior at narrow widths, or add a `ModelActions` variant and state which actions are visible in each state (not downloaded, downloading, installed, unavailable). If the visible count is always at most three because actions are state-dependent, give the state matrix explicitly.
* **Counter-argument**: If "下载" and "浏览器下载" are alternatives and unavailable actions hide, the visible count may never exceed three and the taxonomy is sound — but that inference must be written down.

### 11. "触点页面" and "中文回退" are undefined acceptance terms - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 49, 262, 309
* **Discrepancy Description**: "触点页面" appears in the acceptance criterion "触点页面在英文界面中不出现中文回退" (line 49), the translation check "确认触点页面字符串存在英文翻译" (line 262), and the documentation task "补齐触点页面英文翻译" (line 309), but is never defined. A fresh reader cannot tell whether it means all eight settings pages, only pages touched by this change, or another subset. "中文回退" is likewise ambiguous: untranslated strings falling back to Chinese, or hardcoded Chinese text. The i18n gate's scope is therefore unknown.
* **Rationale & Recommendation**: Define at first use — "本设计涉及的八个设置页面，下文称触点页面" — or replace the term with "本设计涉及的八个设置页面". Define "中文回退" as "未翻译字符串回退到中文源文", and use the same defined terms in all three places.
* **Counter-argument**: The term may be shorthand from the brief. The design document is supposed to stand alone, and its acceptance and verification sections depend on it.

### 12. Alternatives never evaluate the in-place restyle baseline - Severity: Major
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 311-383
* **Discrepancy Description**: Alternatives A-L cover broad product, platform, and sequencing choices, but none evaluates the obvious baseline for a UI-only refresh: restyle the existing widgets in place with QSS/palette overrides, no token layer, no roughly 3,600-line split, and no new preview API. Alternative C still splits four files; E is sequencing; G is an app-level stylesheet; J is one-shot. Without that baseline, the document does not prove that the token system plus full split is justified — especially since the 600-line rule that drives the split has no recorded source (Finding 1). The standard's Alternatives Rule requires concrete trade-offs for viable designs, including complexity and cost.
* **Rationale & Recommendation**: Add "Alternative M: in-place restyle" with effort, risk, expected visual outcome, and rollback, then compare it explicitly to the chosen path. If the split is mandatory, cite the rule's source or record a waiver in the same section.
* **Counter-argument**: Twelve alternatives is already more than the standard requires, and a no-split option may be out of scope if the 600-line rule is binding. The rule's provenance is unverified in the repo, and no-split is the natural cost baseline for a refresh described as UI-only.

### 13. The 600-line gate is inconsistent and its interfaces are undefined - Severity: Major
* **Reviewer Lens:** `egm-design`, `egm-structural`
* **Affected Scope:** lines 38, 51, 68, 268-289
* **Discrepancy Description**: Line 38 says "任何文件不超过 600 行"; line 51 narrows this to "所有新增或被修改的代码文件". Existing untouched files already exceed 600 — verified `src/sensevoice_engine.cpp` 1,108, `src/stream_recognizer.cpp` 916, `src/text_processor.cpp` 819, `src/windows_text_injector.cpp` 771, `src/main.cpp` 685 — so an "any file" gate fails before work starts. The file table (270-282) has no row for residual `qt_ui_main.cpp` (deleted or kept?), no budget for `src/ui/overlay/*` absorbing the 2,684-line `VoiceInputWindow`, and mixes per-file rows with per-glob rows (`widgets/*` 900, `pages_*.cpp` 700) plus one non-numeric cell ("按职责拆分"); the estimates total roughly 3,640 lines against a 5,587-line original. `PageBundle`, `SettingsSnapshot`, and `SettingsDraft` (287-288) are named but never defined — no fields, ownership, or callback signatures. Line 289's "按方法组拆分" also conflicts with line 281's component split.
* **Rationale & Recommendation**: Define the gate as "all new/modified files at most 600 lines, with an explicit grandfather list". Add the residual main-file decision and per-file overlay budgets. Provide a header sketch for `PageBundle`/`SettingsSnapshot`/`SettingsDraft` including callback ownership. Choose either method-group `.cpp` splitting or collaborator extraction, not both.
* **Counter-argument**: Estimates are rough and an engineer can keep splitting until files fit. Without a residual/overlay budget and interface contracts, neither feasibility nor "zero behavior change" is reviewable. This finding overlaps Finding 1; resolving the scope decision there may absorb most of it.

## Nits

### 14. `Open Questions` negates its own existence - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** lines 64-69
* **Rationale & Recommendation**: The section opens with "无未决问题" and then lists two items that must be confirmed before finalization, so readers scanning for unresolved decisions will skip normative content. Retitle to "已定稿决策" and phrase the bullets as settled decisions, or keep the heading and phrase them as questions with an owner and deadline.
* **Counter-argument**: Both items are restated in Goal 7 and Non-Goals, so nothing is lost except discoverability.

### 15. Required standard areas use non-standard headings - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** line 16 (`## The Problem`), line 103 (`## Technical Plan (Architecture)`)
* **Rationale & Recommendation**: The standard names "Problem Statement" and "Proposed Architecture". Both exist semantically but under different titles, so template compliance and the standard-to-document mapping are not self-evident. Rename, or add the standard term parenthetically; the body can stay unchanged.
* **Counter-argument**: The requirement is semantic ("must detail these areas"), not literal heading text, so this costs discoverability, not correctness.

### 16. Only one section uses numbered H3s - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** lines 105-301 (`### 1.` through `### 10.`) against `### CUJ 1..5` and `### Alternative A..L`
* **Rationale & Recommendation**: Two numbering conventions appear at the same heading depth, and inserting a section forces renumbering. Drop the numerals and use short topic headings; order plus cross-references already carry the sequence.
* **Counter-argument**: The numerals make §7 and §8 easy to cite, and the sequence is genuinely ordered.

### 17. Three different page orders coexist - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** lines 203-242 against the sidebar groups (178-182) and the migration order (line 127)
* **Rationale & Recommendation**: §5 lists 输入、输出、外观、翻译、朗读、热词、模型、VAD; the sidebar puts 外观 last; the migration batches 外观/翻译/朗读. A reader cross-checking a layout against its nav group must mentally reorder. Either order §5 to match the sidebar, or add one line stating §5's order is by implementation batch, not navigation.
* **Counter-argument**: §5 follows implementation dependencies, which is a legitimate reason to differ — it just needs to be stated.

### 18. "Rejected by author" boilerplate adds authority, not evidence - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** all `**Reason for Rejection**` bullets (lines 317, 323, 329, 335, 341, 347, 353, 359, 365, 371, 377, 383)
* **Rationale & Recommendation**: The standard forbids attributing a rejection to a maintainer without linking an issue/discussion/PR. Here the following rationale is concrete and self-contained, so substance passes, but the repeating prefix reads as decision-by-authority. Replace with `**Rationale**:` and state the deciding trade-off directly.
* **Counter-argument**: "author" is the document's own author, not an external maintainer cited as authority, so the rule's intent is not actually violated; purely a labeling cleanup.

### 19. The shell ASCII diagram does not line up - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** lines 165-175
* **Rationale & Recommendation**: The divider (25 + 26 columns) does not match the 54-column borders, and the CJK labels are double-width, so the right column edge misaligns in any monospaced renderer. Rebuild with fixed-width ASCII labels or as a Markdown table with two columns plus header/footer rows; keep the four-region semantics.
* **Counter-argument**: The bullet list below states the four regions in prose, so no information is lost.

### 20. The problem statement misdescribes three existing controls - Severity: Nit
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 24, 35, 211, 215
* **Rationale & Recommendation**: (a) Lines 24/35 say the hotword page needs a clear header, but `createHotwordPage` already sets six header labels at `src/qt_ui_main.cpp:2505-2512`; the work is restyling, not adding. (b) Line 211 says "原文/精简改为分段按钮", but they are already checkable `QPushButton#modeSegment` buttons at 2260-2270. (c) Line 215 says color buttons "保留可读颜色值", but they are 56x28 swatches with the hex only in a tooltip (2046-2049) — there is no visible value to preserve. Correct the baseline to "restyle the existing header/segments" and "add a visible color value"; this also keeps the before/after manifest honest.
* **Counter-argument**: "清楚的表头" and "改为" are qualitative, and the header may be visually weak; the color-value claim, however, is factually wrong.

### 21. `pageCount() == 8` is not guaranteed by the construction path - Severity: Nit
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 259, 263
* **Rationale & Recommendation**: The dialog adds the 模型 page only when `model_manager_ != nullptr` (`src/qt_ui_main.cpp:1721-1725`); otherwise there are seven pages. The new API and the offscreen test assume eight. Make the model page always present with a null-safe disabled state, or require a `ModelManager` fixture and assert eight; document the index order used by `selectPage()`.
* **Counter-argument**: Production always passes a manager, so eight is the real case; the public API should still state the invariant.

### 22. "Screenshots unchanged" has no baseline or diff tooling - Severity: Nit
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 50, 130, 264
* **Rationale & Recommendation**: The repo has `tools/verify_preview_geometry.ps1` (geometry only, Windows `user32`) but no capture/diff tool, checked-in baseline images, or tolerance. "保持通过" is therefore not reproducible. Add a capture command, checked-in baselines (or fixture-generated ones), and a pixel-diff threshold; or drop the screenshot criterion and rely on geometry/object-property assertions.
* **Counter-argument**: A manual visual check may be intended, and a human can compare screenshots; the rest of the verification layer is automated, so "保持通过" implies automation.

### 23. Assistive-technology names are promised but not verified - Severity: Nit
* **Reviewer Lens:** `egm-design`
* **Affected Scope:** lines 36, 95, 263
* **Rationale & Recommendation**: Goal 5 and CUJ 4 promise icon-only buttons have names readable by assistive technology, but the layout test checks only focus policy and "关键对象名". The remove button is icon-only with a tooltip (`src/qt_ui_main.cpp:2530-2532`), and there are no `setAccessibleName`/`QAccessible` calls under `src/`. Add `setAccessibleName`/`setAccessibleDescription` and an offscreen `QAccessible::queryAccessibleInterface` assertion.
* **Counter-argument**: Qt may fall back to tooltip text, and the add button has "+" text; the trash button is icon-only and the verification does not test accessible names.

### 24. Subjective framing and rhetorical filler - Severity: Nit
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 18, 28, 133
* **Rationale & Recommendation**: "整体仍显得机械" (line 18) and "让桌面工具像完整产品，而不是一组原生控件拼接" (line 28) are subjective judgments with no measurable defect; the bullets that follow carry the specifics. "先补验证再改视觉不是流程装饰" (line 133) defends the plan rather than stating a fact. Replace the summary with the measurable symptoms already listed ("导航无分组、视觉权重接近、硬编码值多套、热词表无表头/空状态、焦点不可见"), and start line 133 with the consequence.
* **Counter-argument**: The subjective framing communicates the user-perceived problem and motivates the refresh; it can stay if trimmed, but should not be the only statement of the problem.

### 25. The SVG exception has no approval gate - Severity: Nit
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** line 60
* **Rationale & Recommendation**: "不使用 `Qt6::Svg` 或新增图标插件，除非后续明确批准" names no approver, criteria, or decision record. Either drop the exception or replace it with a concrete gate: "如确需使用，须另立 ADR，记录批准人、打包影响和跨平台验证结果，不在本轮实施." Classification: CALIBRATE.
* **Counter-argument**: The exception preserves flexibility for a later approved change; without a named decision-maker it is unenforceable and does not belong in a scope-boundary list.

### 26. The documentation task says "update" two files that do not exist - Severity: Nit
* **Reviewer Lens:** `egm-structural`
* **Affected Scope:** lines 306-307
* **Rationale & Recommendation**: `docs/adr/0003-ui-design-system.md` and `docs/ui-design-system.md` do not exist in the repository (only ADR 0001 and 0002 are present). "更新" implies existing files; say "新增" for these two and keep "更新" for the READMEs and index. Otherwise the doc-sync step reads as a no-op.
* **Counter-argument**: "记录" in the same bullets already signals authoring, and an author may read "同步" as create-or-update; the verb choice is still imprecise.

## What Survives Scrutiny

The document's factual baseline is unusually solid for a design spec. These claims were checked against the repository and hold:

* `src/qt_ui_main.cpp` is **5,587 lines** (line 268), `InputSettingsDialog` is **1,128 lines** (about 1,127, line 328), and `VoiceInputWindow` is **2,684 lines** (line 328).
* There are exactly **nine** `add_test` entries (`CMakeLists.txt:451, 587-692`), matching "九项 `ctest`" (line 264).
* The constructor really has **22 parameters** (`src/qt_ui_main.cpp:1611-1632`), so the `SettingsSnapshot`/`SettingsDraft` motivation (line 288) rests on a real defect.
* `QSettings("SenseVoice", "LocalDictation")` and the `appearance/*`, `vad/*`, `translation/*` keys exist as described (lines 296, 143).
* The hotword table already has **six columns** (line 227) and `hotwords.tsv` is real; the persistence-order separation proposed at line 298 is the right shape.
* There is **one** translation context, `SpeakAnything` (`resources/i18n/speakanything_en.ts`), and `Q_DECLARE_TR_FUNCTIONS(SpeakAnything)` is used at `src/qt_ui_main.cpp:145`, so the single-context plan (lines 286, 309) is coherent.
* The UI file uses **no `Q_OBJECT`**, so the "avoid AUTOMOC" constraint (line 287) matches reality.
* `resources/sensevoice.qrc` embeds only the ICO and the project links **no `Qt6::Svg`**, so the no-SVG dependency constraint (lines 60, 291, 346) is accurate.
* The claim that current preview writes the real registry (lines 26, 133) is **true and load-bearing**: preview constructs `VoiceInputWindow`, whose constructor calls `loadSettings()`, which unconditionally writes `vad/profile_version` and calls `sync()` (`src/qt_ui_main.cpp:2767, 3259-3260`). The validation-first ordering is justified by a real defect, not by preference.
* The phased order — baseline, deterministic preview, mechanical split, tokens, shell, per-page migration (lines 109-131) — is defensible, and each phase has an independent acceptance signal.
* The Alternatives section is substantive: twelve options with concrete `Cons` and specific rejection rationales, which satisfies the Alternatives Rule in substance even though the cheapest baseline is missing (Finding 12).
* The Non-Goals are concrete and mostly enforceable (no main window, no overlay identity change, no new settings, no persistence changes, macOS native controls), and they correctly fence off the largest scope-creep risks.
