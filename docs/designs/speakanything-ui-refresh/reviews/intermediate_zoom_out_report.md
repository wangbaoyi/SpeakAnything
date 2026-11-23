# Goldfish Review Report — Intermediate Zoom Out

**Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (lines 1-529; FeatureRange 1-384, DetailedRange 385-529)
**Mode:** REVIEW — Stage 8 Intermediate Zoom Out
**Date:** 2026-11-22

The target has not been visibly revised since the Stage 5-7 reports: its mtime is 20:39, while the prior reports are 20:55-21:14, and the same normative text is still present at the same line numbers. No prior finding is resolved in the current text; the `pageCount()` / `selectPage()` page interface is a sound idea, but it does not address the scope, gate, or determinism findings. This review therefore separates what is genuinely sound from what is still unresolved, and asks whether the remaining program is proportionate to a settings-dialog refresh.

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-zoom-out` | completed |

One fresh-context instance read the persona, the Zoom-Out Standard, the full target, and all three prior reports. No reviewer failure or retry occurred.

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. A settings refresh still carries a UI-controller/build refactor - Severity: Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 6, 14, 38, 51, 68, 107, 268-289, 328, 436-446
* **Discrepancy Description**: The document states “本轮只改设置对话框及其直接子页面” (line 107), then makes its largest work item the extraction of the 2,684-line `VoiceInputWindow` into roughly eleven `src/ui/overlay/*` translation units (lines 436-446) alongside splitting the 5,587-line `qt_ui_main.cpp`. That controller owns `loadSettings()`, hotkey registration, tray construction, recording sessions, translation workers, speech queues, and injection state. No existing test exercises those controller paths; the nine `ctest` targets cover lower-level modules, and the overlay screenshot/geometry script cannot detect changed hotkey, tray, or session wiring. Line 289 says “按方法组拆分，不改成员语义”, but a method-group split leaves member state in the class and only reduces file size; it does not achieve the decoupling claimed at line 268.
* **Rationale & Recommendation**: The root problem is settings scanability (lines 20-26). The overlay split delivers no settings-dialog improvement and is the highest-risk work in the plan. Remove `src/ui/overlay/*` from this design, keep `qt_ui_main.cpp` as the controller this round, and move the split to a separate ADR with characterization tests for hotkeys, tray actions, session start/stop, translation/speech lifecycle, and injection. If it must stay, separate “verbatim move” from “state/interface extraction” and gate the latter on behavior tests, not screenshots. Add explicit per-file budgets for the overlay files; line 281 currently has none.

### 2. The 600-line gate still cannot pass, and the rule has no recorded source or scope - Severity: Blocker
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 38, 51, 68, 268-289, 328, 458, 509-514
* **Discrepancy Description**: Goal 7 says “任何文件不超过 600 行” (line 38), while criterion 51 says “所有新增或被修改的代码文件” (line 51), and the verification command scans every `.cpp`/`.h` under `src` and requires empty output (lines 509-514). Run verbatim, the gate reports six files: `src/qt_ui_main.cpp` 5,587; `src/sensevoice_engine.cpp` 1,108; `src/stream_recognizer.cpp` 916; `src/text_processor.cpp` 819; `src/windows_text_injector.cpp` 771; `src/main.cpp` 685. Five are untouched by this design. The rule is also unrecorded: `AGENTS.md` is a 9-byte line containing `CLAUDE.md`, no `CLAUDE.md` exists, and neither README nor `docs/` states a 600-line rule. The target table's `SpeakAnything/AGENTS.md` row (line 458) also uses the wrong path base; the file is at the repository root.
* **Rationale & Recommendation**: A visual refresh cannot depend on an unplanned refactor of recognition and text-processing code. Resolve the rule's provenance and scope before further drafting. If it is binding, record it, add a grandfather list for the five pre-existing files, and change the gate to the changed-file set (for example `git diff --name-only`). If it applies only to touched UI files, say so and make Goal 7, criterion 51, and lines 509-514 consistent. Do not let a settings restyle silently carry five unrelated refactors.

### 3. The obvious cheap baseline — in-place restyle with narrow extraction — is still absent - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 311-383 (Alternatives A-L), especially C (325-329) and J (367-371); lines 66-69
* **Discrepancy Description**: The alternatives evaluate a four-file split, a one-shot change, a global stylesheet, and sequencing, but none evaluates the natural baseline: restyle the existing widgets in place, extract only the settings dialog and models page, leave the overlay controller where it is, reuse the existing card/row helpers, and fix the one known preview write with a guard. Without that baseline, the roughly 40-file program has no cost/benefit comparison; its only driver is the unrecorded 600-line rule.
* **Rationale & Recommendation**: Add “Alternative M: in-place restyle plus narrow extraction” with effort, risk, expected visual outcome, test surface, and rollback. Compare it directly against the chosen path. Unless the 600-line rule is recorded as binding, Alternative M is the defensible default and should replace the current full-split plan.

### 4. The visual deliverable is still the least specified part of the design - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 12, 137-159, 183, 418
* **Discrepancy Description**: The design names semantic roles but supplies no hexadecimal values for background, card, border, text, muted text, accent, or focus. The “参考图” is neither embedded nor linked, and the design directory contains only Markdown. The existing QSS already uses `#F7F8FA`, `#FFFFFF`, `#E3E6EB`, `#356AE6`, `#EAF0FF`, plus an amber slider (`#E6A52C`) and near-black text (`#202329`); the document does not say which survive. “单一蓝色交互强调色” therefore conflicts with the amber slider, and the 3:1 focus contrast claim cannot be checked without an actual focus color. Line 183 also offers “项目内栅格图标”, but `resources/sensevoice.qrc` embeds only `sensevoice.ico`, and there is no raster icon set.
* **Rationale & Recommendation**: Add an exact light-mode token table with contrast ratios, link or attach the reference, and state which existing colors are retained or replaced, including whether status colors and the amber slider remain. If the intent is QSS-only restyling, say so and reuse the existing palette rather than inventing a parallel token vocabulary. Change the icon option to “QStyle standard icons only” or add the intended assets to the target-file table.

### 5. The preview harness over-scopes production code and still under-guarantees no-write - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 258, 402-404, 414-415, 487-491; repository `src/qt_ui_main.cpp:2767`, `:3259-3260`, `:5466`, `:5550-5566`; `README.md:434`
* **Discrepancy Description**: `loadSettings()` unconditionally writes `vad/profile_version` and calls `sync()` (lines 3259-3260), and preview constructs `VoiceInputWindow` (line 5534), whose constructor calls `loadSettings()` (line 2767). The design says “预览不调用 `QSettings::setValue` 或 `sync()`” (line 258) but adds neither a guard nor store isolation. It also threads `PreviewOptions` into the production dialog constructor (line 402). Separately, `--preview-settings` already means the screenshot output directory (lines 5466, 5550-5566), while the design's parse list omits it, `--preview-page all` is undefined, and there is no theme or model-directory flag despite the stated forcing. Manifest contents are never specified.
* **Rationale & Recommendation**: Add an `if (!preview_mode_)` guard around the known writes, or redirect `QSettings` to a temporary INI and assert it is unchanged. Use the existing `SPEAKANYTHING_LANG` instead of `--preview-language`, keep `selectPage()` as the page interface, and either define `--preview-settings` as the output directory or rename the new option. Remove `PreviewOptions` from the production constructor. Specify the manifest fields and keep the registry diff as defense in depth.

### 6. The settings inventory is circular and non-deterministic - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 44, 260, 453, 477-482, 519
* **Discrepancy Description**: The inventory records page, object name, type, label, default, and current value. `qt_ui_main.cpp` has only 17 `setObjectName` calls, mostly generic styling hooks (`card`, `cardTitle`, `cardHint`, `page`, `settingsSidebar`, `settingsStack`), plus six in `models_page.cpp`; most interactive controls have no stable name. The redesign intentionally changes labels and composition (lines 185, 227, 231-236), so a label comparison must fail, yet line 519 forbids updating the baseline to explain a legitimate relabel. Current values also depend on runtime state such as enumerated audio devices (lines 2463-2468), so the comparison is not reproducible.
* **Rationale & Recommendation**: Inventory by stable setting ID or `QSettings` key plus declared type/default, not by label. Add a step 2a that assigns stable object names to interactive controls only. Compare the accepted `SettingsDraft` against a checked-in fixture, exclude runtime-derived current values, and allow baseline diffs when they are explained in the change description. Keep per-page screenshots for layout regressions instead of using labels as the behavioral contract.

### 7. The translation gate still checks a property that is already true, and its scope term is undefined - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 39, 49, 262, 309, 452, 496, 500
* **Discrepancy Description**: The gate checks zero `unfinished` and zero `vanished`, but strings that were never extracted are not marked `unfinished`. The current catalogue has 227 messages, zero `unfinished`, zero `vanished`, and one `SpeakAnything` context, yet it contains zero occurrences of `模型目录`, `导入模型`, and `浏览器下载`, all of which exist in `src/models_page.cpp` (lines 92, 156, 231-232). “触点页面” is used as the coverage scope (lines 49, 262, 309) but never defined.
* **Rationale & Recommendation**: Define the touched page set explicitly — preferably the eight settings pages. Run an `lupdate`-equivalent pass, or extract every `tr()` / `translate("SpeakAnything", ...)` literal from the touched sources and assert each has a non-empty translation. Fail on missing strings rather than on already-zero counters, and keep the single `SpeakAnything` context.

### 8. `settings_layout` would create a third copy of helpers that already exist - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 275-278, 421; repository `src/qt_ui_main.cpp:2057`, `:2067`, `:2086`, `:2100`, `:2112`, `:2614`; `src/models_page.cpp:86-119`, `:214-250`
* **Discrepancy Description**: The plan says to “创建” `settings_layout` and “提供卡片、行、分组标题、滑杆、分段按钮和提示文本助手” (line 421), but those helpers already exist as `scrollable`, `addCard`, `addRow`, `carded`, `pageLayout`, and `addSlider`. `ModelsPage` also has its own near-identical card builder. Creating a new module without retiring the old ones produces a third copy of the same pattern, working against the stated consistency goal.
* **Rationale & Recommendation**: Define `settings_layout` as a migration of the existing helpers, list which functions move and which are deleted, and convert `ModelsPage` to use the shared module. If that migration is not worth the churn this round, drop the new module and reuse the existing helpers in place.

### 9. Acceptance-critical contradictions and undefined terms remain - Severity: Major
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 43 / 177 / 504; 137-143 / 151-153 / 156 / 183 / 306; 196 / 235 / 334; 45 / 89 / 261; 242 / 252; 50 / 69 / 107; 194 / 207 / 219 / 223 / 399; 141 / 159 / 418
* **Discrepancy Description**: The document still contains several normative conflicts that a fresh implementer cannot resolve. They are not cosmetic because they determine acceptance tests or behavior.

| Issue | Current conflict | Recommended resolution |
| --- | --- | --- |
| Minimum width | Criterion 43 says `680x540`; §3 line 177 raises the minimum to `760`; walkthrough line 504 checks `760x540`; current code is `680x540` (`src/qt_ui_main.cpp:1640`) | Pick one number. If `760x540`, update line 43 and line 263 and define sidebar scrolling at 540 height. If `680x540`, the 184 px sidebar must adapt. |
| Token model | `shell.*` only (line 143) versus flat roles (line 140); three layers (line 137) versus “两层 token” (line 306); 14/24 outside scale 11/12/13/15/22/26 (lines 151-153); row height 44 versus nav 40 (lines 156, 183) | Nest all roles under `shell.*` or list every namespace read; close the type scale; rename row height to “settings row minimum” and state the 40 px navigation exception. |
| Action variant | “一至三个按钮” (line 196) versus four model actions (lines 235, 334); current code has four buttons (`src/models_page.cpp:230-240`) | Extend to one-to-four with overflow behavior, or add a `ModelActions` variant and a state matrix. |
| Hotword bytes | Whole-file SHA-256 (line 261) versus untouched-row promise (line 89); boost is a `float` (`src/text_processor.cpp:651`) edited through a one-decimal spin box (`src/qt_ui_main.cpp:2569`) | Require byte-identical no-edit round-trip on a fixture generated by the current saver; after an edit, compare untouched rows byte-for-byte and the edited row field-by-field. Do not promise arbitrary legacy bytes. |
| Reset scope | “当前表单字段” (line 252) versus VAD-only reset (line 242); current reset is dialog-wide over Apply-bound fields (lines 1755-1780) | Enumerate the reset set. State whether it is dialog-wide or page-local, and explicitly exclude appearance, models, startup, and hotwords. |
| Overlay vocabulary | “三种样式” (lines 50, 69, 107), `AppearanceSettings`, and `overlay.identity` are used but never defined | Name the three styles and describe `AppearanceSettings` and `overlay.identity`, or replace the references with the existing code terms. |
| Device overload | “设备” means compute device, audio output device, and device detection in different sections (lines 194, 207, 219, 223, 399) | Split the terms: compute policy, audio output device, device capability. |
| macOS adapter | “Windows/macOS QSS 适配器” (line 418) versus native palette/native controls (lines 141, 159) | Rewrite line 418 as “Windows QSS adapter + macOS palette role mapping” and add a cheap parity artifact. |

* **Rationale & Recommendation**: Close these in one revision and give each fact a single owner. The minimum-width and hotword-byte items directly determine whether the acceptance tests can pass; the rest determine whether two implementers build the same UI.

### 10. Target table and factual counts still contain errors - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 183, 400, 413, 447, 458, 461-462
* **Discrepancy Description**: Line 400 says “16 个分散 getter”; the accessor block at `src/qt_ui_main.cpp:1792-1898` has 14. Line 183 offers “项目内栅格图标”, but no raster icon set exists. `docs/designs/README.md` already exists and already registers this design, so its action should be “修改”, not “创建”. `design_draft.md` does not exist. `SpeakAnything/AGENTS.md` uses the wrong path base. The existing `src/main.cpp` CLI entry for `sensevoice-stream` is unaccounted for beside the new `src/app/main.cpp`.
* **Rationale & Recommendation**: Correct the count and the target actions, change the AGENTS row to the repository-root path, and add an explicit “unchanged, CLI entry” row for `src/main.cpp` or rename the new file `ui_main.cpp`.

### 11. Baseline descriptions overstate what must be added - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 24, 35, 211, 215
* **Discrepancy Description**: The hotword page already sets six header labels (`src/qt_ui_main.cpp:2505-2512`), so the work is restyling, not adding a header. The raw/clean controls are already checkable segmented buttons (`:2260-2270`), so “改为分段按钮” is also a restyle. Color buttons show the hex value only in a tooltip (`:2046-2049`), so “保留可读颜色值” should be “add a visible value”.
* **Rationale & Recommendation**: Reword these as restyle/add operations. This keeps the before/after inventory honest and avoids claiming credit for existing behavior.

### 12. `Open Questions` still negates itself - Severity: Nit
* **Reviewer Lens:** `egm-zoom-out`
* **Affected Scope:** lines 64-69
* **Discrepancy Description**: The section opens with “无未决问题” and then lists two items that must be confirmed before finalization. Readers scanning for unresolved decisions will skip binding scope content.
* **Rationale & Recommendation**: Retitle the section “已定稿决策” and phrase the bullets as settled decisions, or present them as real questions with an owner and deadline.

## What Survives Scrutiny

* **The settings-shell diagnosis is code-accurate.** The sidebar is flat, non-focusable (`src/qt_ui_main.cpp:1704`), 168 px wide (`:1703`); the dialog uses `setMinimumSize(680, 540)` (`:1640`); all eight pages exist (`:1712-1726`); and preview really does write the registry (`:5534` → `:2767` → `:3259-3260`). The motivation is not speculative.
* **The quantitative baseline is exact.** `qt_ui_main.cpp` is 5,587 lines, `InputSettingsDialog` about 1,127, `VoiceInputWindow` 2,684; there are exactly nine `add_test` entries; the dialog constructor takes 22 parameters.
* **Surface separation is sound.** Settings-shell styling, overlay identity, user-owned `appearance/*` colors, and the no-SVG constraint are correctly separated. Alternatives B, H, and L are argued correctly.
* **The settings-dialog extraction is defensible.** `SettingsSnapshot` / `SettingsDraft` / `PageBundle` address a real 22-parameter constructor and 14 scattered getters. The `to_qstring` deduplication, shared `SENSEVOICE_UI_SOURCES`, and no-`Q_OBJECT`/AUTOMOC constraint all match the codebase.
* **Verify-first sequencing is justified.** The current preview cannot prove a read-only run, so building the instrument before measuring is correct. The defect is the instrument's guarantee, not the order.
* **Reuse instincts are mostly right.** The existing card/row/slider helpers and the `ModelsPage` card builder are genuine reuse candidates; the plan should migrate them rather than clone them.
* **Not verifiable in this environment:** macOS runtime behavior and screenshots; whether `760x540` with a 184 px sidebar avoids clipping in the six-column hotword table at 150% scaling; and the provenance of the 600-line rule outside the repository.

## Overall Verdict

**Return to drafting — targeted revision with scope reduction, not a restart.** The settings-dialog refresh is grounded and worth doing, but the current document is two projects fused together: a defensible visual refresh and an unjustified controller/build refactor. Before implementation:

1. Resolve the 600-line rule's source and scope; change the gate to changed files plus a grandfather list.
2. Remove the `VoiceInputWindow` split from this design or move it to a separate ADR with behavior tests.
3. Define the exact visual tokens, reference target, and which existing colors survive.
4. Rewrite the preview, inventory, and translation gates so they test the properties they claim, and drop production-only ceremony.
5. Close the contradictions in Finding 9 in the same revision.

With those changes, the remaining settings-dialog refresh is a smaller, defensible project. As written, it should not proceed as-is.
