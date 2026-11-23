# Consolidated Review Report - SpeakAnything UI Refresh

- **Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (529 lines)
- **Review date:** 2026-11-22
- **Review mode:** Uninterrupted full flow (Stages 5-9, no interruption, no fixes applied to the target)
- **Inputs read in full:** `zoom_out_report.md`, `comprehension_report.md`, `critic_report.md`, `intermediate_zoom_out_report.md`, `implementation_readiness_report.md`
- **Status of this report:** Consolidated synthesis only. The target design and all source files are unchanged.

## Stage and Reviewer Status

| Stage | Report | Reviewer(s) | Status |
| --- | --- | --- | --- |
| 5 - Initial Zoom Out | `zoom_out_report.md` | `egm-zoom-out`, `egm-inventive-catalyst` | completed |
| 6 - Comprehension | `comprehension_report.md` | comprehension reviewer | completed |
| 7 - Design Critic | `critic_report.md` | `egm-design`, `egm-structural`, `egm-style` | completed |
| 8 - Intermediate Zoom Out | `intermediate_zoom_out_report.md` | `egm-zoom-out` | completed |
| 9 - Implementation Readiness | `implementation_readiness_report.md` | `egm-implementation-readiness`, `egm-codebase-details`, `egm-style` | completed |

All reviewers completed. No reviewer was skipped, failed, timed out, or left not run.

## Overall Verdict

**Not implementation-ready. Return to drafting for a targeted revision with scope reduction, not a rewrite.**

The settings-dialog diagnosis and the verify-first instinct are sound, and the code baseline is largely accurate. The document fails readiness for three reasons: it fuses a settings refresh with an unrequested controller/build refactor; several acceptance gates are either impossible to satisfy or prove a different property than the one claimed; and the contracts that an implementer must code against are still prose rather than definitions.

**Consolidated counts:** 24 findings - 9 Blockers, 10 Major, 5 Nits.

**Severity calibration used here:**

- **Blocker:** the design cannot be approved for implementation, or its done-state cannot be reached or trusted, until the item is resolved.
- **Major:** the plan can still proceed after a local correction, but the current text would cause rework, a false pass, or an unsafe/incorrect implementation.
- **Nit:** clarity, consistency, or trust issue with no direct behavioral consequence.

**Where the reports differed:**

- The 600-line gate was Major in Stage 7 but Blocker in Stages 5, 8, and 9. The verbatim command reports six offenders, five outside this design's scope, and the rule has no repository source. The stronger evidence supports Blocker.
- Hotword byte preservation was Nit in Stage 5, Major in Stages 7 and 9. Stage 5 focused on a canonical fixture passing; Stage 7 and Stage 9 inspected the saver and found that arbitrary legacy bytes cannot survive it. The stronger evidence supports Blocker for the stated guarantee, while the canonical no-edit case remains achievable.
- Stage 8 noted that "按方法组拆分，不改成员语义" cannot hold when member state moves with extracted method groups. That is the stronger interpretation and is reflected in C-B1.
- Earlier stages rated preview, translation, inventory, and the visual target as Major. They are consolidated as Blockers only where they are the sole evidence for a central claim ("no settings written", "no English fallback", "no settings lost", or "the visual target is met") and that evidence cannot execute as written.

## Consolidated Findings

### Blockers

#### C-B1 - A UI-only design carries a controller extraction with no behavior tests

- **Severity:** Blocker
- **Consolidated issue:** The document says this round changes only the settings dialog and its direct pages (line 107), then makes its largest work item the extraction of the 5,587-line `qt_ui_main.cpp` and the 2,684-line `VoiceInputWindow` into roughly eleven `src/ui/overlay/*` files, plus new snapshot/draft/page contracts. No existing test exercises hotkey registration, tray actions, session lifecycle, translation workers, speech queues, or injection. Screenshots and the geometry script cannot detect changed wiring in those paths. The claim "按方法组拆分，不改成员语义" (line 289) also conflicts with moving member state into extracted collaborator files.
- **Affected scope:** lines 6, 14, 58, 68, 107, 268-289, 328, 436-446.
- **Evidence:** `VoiceInputWindow` spans `src/qt_ui_main.cpp:2737-5420` and owns `loadSettings()`, hotkeys, tray, translation/speech setup and injection state. `CMakeLists.txt` registers nine tests, none covering those controller paths. `tools/verify_preview_geometry.ps1` measures only a single preview window and does not exercise tray, hotkey, or session behavior.
- **Why it matters:** This is the highest-risk work in the plan and delivers no settings-dialog improvement. A regression can compile, pass the proposed tests, and still break recording, translation, or text injection. That contradicts the "no behavior change" premise on which the mechanical split depends.
- **Required resolution:** Remove `src/ui/overlay/*` from this design and keep `qt_ui_main.cpp` as the overlay controller this round. Move the split to a separate ADR with characterization tests for hotkeys, tray actions, session start/stop, translation/speech lifecycle, and injection. If the split must stay, separate verbatim file moves from state/interface extraction, enumerate ownership, add the behavior tests, and record an explicit scope waiver.
- **Counter-argument / trade-off:** The split may be genuinely mechanical, and doing it now prevents further growth. That is a legitimate planning choice, but "mechanical" is not reviewable without tests, and a method-group split does not prove unchanged behavior when state crosses translation units.

#### C-B2 - The 600-line gate is unsatisfiable, unrecorded, and scans the wrong file set

- **Severity:** Blocker
- **Consolidated issue:** Three incompatible scopes coexist: "任何文件不超过 600 行" (line 38), "所有新增或被修改的代码文件" (line 51), and a command that scans every `.cpp`/`.h` under `src` and requires empty output (lines 509-514). Today that command reports six files, five of which this design does not touch. The rule has no recorded source: the root `AGENTS.md` is a 9-byte `CLAUDE.md` pointer, no `CLAUDE.md` exists, and neither README nor `docs/` states a 600-line rule. The command also misses `src/macos/*.mm`, `tests/settings_ui_test.cpp`, and modified files outside `src`, and the file table gives no per-file budget for `src/ui/overlay/*`.
- **Affected scope:** lines 38, 51, 68, 268-289, 328, 450, 509-514.
- **Evidence:** Measured with the document's own metric: `qt_ui_main.cpp` 5,587; `sensevoice_engine.cpp` 1,108; `stream_recognizer.cpp` 916; `text_processor.cpp` 819; `windows_text_injector.cpp` 771; `main.cpp` 685. The current gate includes all six. `AGENTS.md` contains only `CLAUDE.md`, and the root `CLAUDE.md` does not exist.
- **Why it matters:** The plan cannot reach its own done-state without an unplanned refactor of recognition and text-processing code, or without silently weakening the gate. A rule that drives a roughly 40-file split but has no provenance cannot justify that cost.
- **Required resolution:** Record the rule's source and scope. If it is project-wide, add an explicit grandfather list for the five pre-existing offenders. If it applies only to touched files, make Goal 7, criterion 51, and the command consistent, and switch the gate to the changed-file set (including `.mm` and `tests`). Add hard per-file budgets for every new file, especially the overlay group.
- **Counter-argument / trade-off:** The 600-line rule may be an external or conversational convention not stored in the repository. Even then, the design must state that provenance and scope; an unrecorded rule cannot drive acceptance.

#### C-B3 - Preview writes the real QSettings, and the preview option contract is incomplete

- **Severity:** Blocker
- **Consolidated issue:** The design requires preview not to call `QSettings::setValue` or `sync()` and to leave `HKCU\Software\SenseVoice\LocalDictation` unchanged (lines 46, 258, 391). But preview constructs a `VoiceInputWindow` before opening the settings dialog, and its constructor unconditionally calls `loadSettings()`, which writes `vad/profile_version` and calls `sync()`, and may migrate four VAD keys. No preview guard exists. The option contract is also incomplete: `PreviewOptions` is said to force language, theme, page, and model directory (lines 258, 402, 415), but the parse list names only `--preview-page`, `--preview-language`, and `--preview-manifest` (line 404). There is no theme or model-directory flag; `--preview-settings` already means the screenshot output directory; `--preview-page all` has no defined value domain; and the manifest schema and exit behavior are unspecified.
- **Affected scope:** lines 46, 258-259, 389, 402-404, 414-415, 487-491.
- **Evidence:** `src/qt_ui_main.cpp:2767` calls `loadSettings()`; `:3259-3260` executes `settings_.setValue("vad/profile_version", 5); settings_.sync();` without a preview check. Current preview creates the window around `:5533-5558`; `--preview-settings` is read as a directory at `:5466` and used to save `settings-%1.png` at `:5564`. Preview detection uses `raw_command_line.contains("--preview")` at `:5442-5444`, so any `--preview-*` flag silently enables preview and contradicts the proposed "missing --preview must error" rule.
- **Why it matters:** The central promise "preview does not write settings" is false before the first screenshot. The preview is also machine-dependent because it reads the user's store, device list, GPU capability, model status, and voices. The manifest and screenshot outputs are the only evidence for visual parity, so an under-specified contract makes the visual work unverifiable.
- **Required resolution:** Split `loadSettings()` into a read-only load and a migration/write phase, or point preview at a temporary INI/registry store and assert it is unchanged. Define the complete `PreviewOptions` schema, every retained and new flag, legal/forbidden combinations, page identifiers including `all`, theme/model-directory sources, manifest schema, directory creation, exit codes, and precedence among `--preview-language`, `SPEAKANYTHING_LANG`, and `ui/language`. Keep the registry diff as defense in depth and automate it.
- **Counter-argument / trade-off:** One `if (!preview_mode_)` guard may stop the known mutation. It does not make screenshots deterministic, because preview still reads real devices, models, and user state; store isolation or fixtures are still required.

#### C-B4 - SettingsSnapshot, SettingsDraft, and PageBundle have no code-level contract; the appearance callback is dropped

- **Severity:** Blocker
- **Consolidated issue:** `SettingsSnapshot`, `SettingsDraft`, and `PageBundle` are named but never defined: no fields, types, defaults, member order, callback signatures, ownership, or mapping from the current 22 constructor arguments and 14 getters. The proposed constructor `InputSettingsDialog(const SettingsSnapshot&, ModelManager*, const PreviewOptions&, QWidget*)` (line 402) removes the existing `std::function<void(const AppearanceSettings&)> on_appearance_changed` and `ModelChoices`. The callback is the live path that applies and persists appearance edits while the dialog is open; `PageBundle` "local callbacks" do not say who receives them. Page order, stack indices, group-header rows, cross-page dependencies (translation updates content/speech/model controls), cancel-time model restoration, and translator install order are also undefined.
- **Affected scope:** lines 287-288, 399-404, 420-434.
- **Evidence:** Current constructor: `src/qt_ui_main.cpp:1611-1632` (22 parameters, including the appearance callback and `ModelChoices`). Production call site: `:3538-3547`, which applies and saves appearance in the callback. Current getter block: `:1792-1898` (14 getters, not 16). `installUiTranslator()` runs before preview arguments are parsed (`:5434-5436`), and current cross-page wiring is not page-local.
- **Why it matters:** A fresh implementer must invent the public data model and the page coordinator. That is where CUJ 5 (appearance applies immediately), cancel semantics, model restoration, and English preview can silently break. The design's stated benefit - replacing a 22-parameter constructor - is not realizable until the replacement is specified.
- **Required resolution:** Put the exact struct definitions in the plan, including types, defaults, and a mapping table from every existing constructor argument/getter to a new field. Define a `SettingsCallbacks` struct (or explicit parameters) for appearance, translation, output, reset, draft, and reject/restore. Define `PageBundle`, page order, canonical stack indices, sidebar-row mapping (including group headers), callback capture/ownership rules, model reject/restore behavior, and translator parse order and language precedence.
- **Counter-argument / trade-off:** A design document need not reproduce every header verbatim. These types are the plan's central contract and are named as acceptance-critical, so prose groupings are insufficient; the absence of a callback replacement is a direct behavioral gap, not a code-style detail.

#### C-B5 - The verification environment is false: wrong Qt prefix and no LinguistTools/lrelease

- **Severity:** Blocker
- **Consolidated issue:** The configure command uses `E:\Qt\6.8.3\msvc2022_64` (line 469), which does not exist. The working prefix recorded in `build-local/CMakeCache.txt` is `E:/Programming environment/Qt/6.8.3/msvc2022_64`. Separately, `Qt6LinguistTools_DIR` is `NOTFOUND`, no `lrelease.exe` exists under the Qt installation, and no `.qm` file exists in the project. `qt_add_translations` is skipped, `installUiTranslator()` silently falls back when `:/i18n/speakanything_en.qm` is absent, and the English screenshot and translation commands cannot produce a real English UI.
- **Affected scope:** lines 452, 455, 468-472, 487-488, 496.
- **Evidence:** `Test-Path "E:\Qt\6.8.3\msvc2022_64"` returns `False`; `build-local/CMakeCache.txt` records the spaced path; `Qt6LinguistTools_DIR:PATH=Qt6LinguistTools_DIR-NOTFOUND`; a recursive search under the working Qt root finds only `mkspecs/features/lrelease.prf`, not `lrelease.exe`; no `.qm` files exist.
- **Why it matters:** The first command fails, so every downstream `build-ui` command is unexecutable. Even with a corrected prefix, the English UI and translation gate cannot be produced or proven in this environment.
- **Required resolution:** Use the real quoted path or `$env:QTDIR`, and add a `Test-Path` preflight. Make Qt LinguistTools/`lrelease` a hard configure requirement or an explicit prerequisite. Verify that a compiled `.qm` exists and is loaded, and add a runtime assertion that `en` preview actually changes visible labels.
- **Counter-argument / trade-off:** A machine-specific Qt prefix can legitimately be a prerequisite. If so, state it as a prerequisite and keep the executable commands generic; do not present a false path as runnable.

#### C-B6 - The hotword byte-identical guarantee is unachievable through the current saver

- **Severity:** Blocker
- **Consolidated issue:** The design promises `hotwords.tsv` remains byte-identical for Chinese, aliases, boost, hits, and "不翻译" samples (line 45), and that unedited rows keep their bytes after an edit (line 89), verified by one whole-file SHA-256 (line 261). `TextProcessor::save_hotwords()` opens with `std::ios::trunc`, rewrites a fixed header, and streams every row in six LF-separated fields. A `float` boost serializes with default formatting (`3.0` becomes `3`), four- and five-field legacy rows expand to six, CRLF becomes LF, and comments/headers are replaced. The UI also edits boost through a one-decimal `QDoubleSpinBox` (`1.25` becomes `1.3`). A whole-file hash cannot prove untouched rows stayed unchanged when any row is edited.
- **Affected scope:** lines 45, 89, 261, 427, 454, 479-482, 519-520.
- **Evidence:** `src/text_processor.cpp:635-651` (truncate, fixed header, six-field rewrite); `src/text_processor.h:15` (`float boost`); `src/text_processor.cpp:605-624` (4/5-field acceptance and expansion); `src/qt_ui_main.cpp:2567-2571` (one-decimal spin box). UI column order is enabled/phrase/aliases/boost/hits/keep, while persistence order is phrase/aliases/enabled/hits/boost/keep.
- **Why it matters:** The stated safety guarantee is false for arbitrary legacy data, and the proposed test can fail a correct implementation or pass a destructive one. This is the main promise that hotword editing is non-destructive.
- **Required resolution:** Split the criterion into (1) a canonical fixture generated by the current saver round-trips byte-identically with no edit, and (2) after editing one row, untouched rows are byte-identical and the edited row matches expected serialization. Compare row-level bytes or a fixture diff. Name the UI and persistence index constants. Add semantic tests for 4-field, 5-field, CRLF, and `3.0` inputs. If arbitrary legacy bytes must be preserved, treat that as a separately approved behavior change to the saver, not a UI-refresh side effect.
- **Counter-argument / trade-off:** A fixture generated by the current saver can satisfy SHA-256 equality, so the canonical no-edit case is not impossible. The document's wording covers "旧格式兼容" and unedited real rows, which the current saver cannot deliver; narrowing the promise is the only non-goal-compatible resolution.

#### C-B7 - The visual target has no concrete values, reference, font policy, or icon assets

- **Severity:** Blocker
- **Consolidated issue:** The core visual deliverable names semantic roles but supplies no hexadecimal values for background, card, border, text, muted text, accent, focus, or status colors. The "参考图" is neither embedded nor linked; the design directory contains only Markdown. There is no font family/fallback or px-vs-pt policy, and no Windows dark/high-contrast policy. "单一蓝色交互强调色" conflicts with the existing amber slider `#E6A52C`, and the 3:1 focus-contrast claim cannot be checked without a focus color. The icon option "项目内栅格图标" is unavailable: `resources/sensevoice.qrc` embeds only `sensevoice.ico`; `sensevoice.svg` is not in the resource file and no raster icon set exists.
- **Affected scope:** lines 12, 28, 137-159, 183, 315-375.
- **Evidence:** Current QSS uses `#F7F8FA`, `#FFFFFF`, `#E3E6EB`, `#356AE6`, `#EAF0FF`, `#E6A52C`, and `#202329` (`src/qt_ui_main.cpp:1656-1685`). The document does not say which survive. `resources/` contains `sensevoice.ico` and an unused `sensevoice.svg`; the `.qrc` embeds only the ICO.
- **Why it matters:** The visual refresh is the primary deliverable, yet two implementers can choose different palettes, fonts, and icon strategies and both claim compliance. The focus, contrast, and single-accent acceptance criteria are not objectively testable.
- **Required resolution:** Add an exact light-mode token table with values and contrast ratios for background, card, border, text, muted text, accent, focus ring, selection, and status colors. State which existing colors are retained or replaced, including whether the amber slider remains. Link or attach the reference image, or name its source and revision. Define font family/fallback and px policy, Windows dark/high-contrast behavior, and the icon strategy: QStyle-only, or add specific raster assets to the target-file table.
- **Counter-argument / trade-off:** "Not pixel-perfect" (line 62) reduces fidelity requirements but does not remove the need for a shared target. Reusing existing QSS values is a valid seed, but the document must say so and still resolve the single-accent and contrast questions.

#### C-B8 - The translation gate proves the wrong property and its scope term is undefined

- **Severity:** Blocker
- **Consolidated issue:** The gate checks zero `unfinished`, zero `vanished`, and existence of "触点页面" strings (line 262). Strings that were never extracted are not marked `unfinished`; they only become unfinished after an `lupdate` pass. The current catalogue is already green on those counters while current Models-page strings are absent, so the English UI already falls back to Chinese. A static `.ts` inspection also cannot detect hardcoded Chinese or a translator that fails to load. "触点页面" (lines 49, 262, 309) is never defined, so the coverage scope is unknown.
- **Affected scope:** lines 39, 49, 262, 309, 452, 496, 500.
- **Evidence:** `resources/i18n/speakanything_en.ts` has 227 messages, one `SpeakAnything` context, zero `unfinished`, zero `vanished`, and no occurrences of `模型目录`, `导入模型`, or `浏览器下载`, all of which exist as source strings in `src/models_page.cpp:92, 156, 231-232`. No `.qm` is built (see C-B5).
- **Why it matters:** The gate can pass while the exact defect it cites is present. That makes the English-UI acceptance criterion untrustworthy and lets a Chinese fallback ship.
- **Required resolution:** Define the exact page/string set, preferably the eight settings pages. Run an `lupdate`-equivalent pass, or extract every `tr()` and `translate("SpeakAnything", ...)` literal from touched sources and assert every one has a non-empty translation. Add a runtime check that loads the compiled `.qm`, constructs each page with a fixed snapshot, and asserts no unexpected CJK; allowlist user hotwords and model names explicitly. Fail on missing strings, not only on already-zero counters.
- **Counter-argument / trade-off:** A blanket CJK scan will false-positive on user data and model names, so it needs a scoped allowlist. That is a reason to scope the check, not a reason to rely on the static `.ts` gate alone.

#### C-B9 - The settings inventory is circular, non-deterministic, and has no schema or baseline origin

- **Severity:** Blocker
- **Consolidated issue:** The inventory records page, object name, type, label, default, and current value (line 260), then requires a structural before/after comparison. Most interactive controls have no stable object name; the codebase has only 17 `setObjectName` calls in `qt_ui_main.cpp` and 6 in `models_page.cpp`, many repeated styling hooks (`card`, `cardTitle`, `cardHint`). The redesign intentionally changes labels and composition, so label equality must fail, while line 519 forbids updating the baseline to explain a legitimate relabel. Current values depend on `QSettings`, GPU detection, audio devices, installed models, startup state, and language. No JSON schema, version, ordering, baseline origin, or working-directory/CTest registration is specified. The two commands are disconnected: `--inventory` writes `build-ui\settings-inventory.json`; `--compare` names only the fixture and may compare the fixture to itself.
- **Affected scope:** lines 44, 260-261, 451-453, 477-482, 519.
- **Evidence:** 17 `setObjectName` calls in `src/qt_ui_main.cpp`, 6 in `src/models_page.cpp`; device enumeration `:2463-2468`; GPU detection `:2143-2154`; model status from `src/models_page.cpp`; appearance from the user registry. The document does not state that the fixture is generated and committed from `9a66093` before the split.
- **Why it matters:** The instrument that is supposed to prove "no setting was lost" cannot pass as written and cannot distinguish a real loss from an intentional relabel or a machine difference. Without a baseline origin, it also cannot prove the split preserved behavior.
- **Required resolution:** Inventory by stable setting ID or `QSettings` key plus declared type/default, not by label. Add step 2a that assigns stable object names to interactive controls only. Define versioned schemas for `settings_inventory.json` and `manifest.json`, including ordering and dynamic-control representation. Generate and commit the baseline from `9a66093`; split invariant structure from runtime values; exclude or normalize device/GPU/model/locale values. Make the CLI explicit (`--inventory <out>` then `--compare <generated> <fixture>`), add exact `add_test` entries, `WORKING_DIRECTORY`, and `QT_QPA_PLATFORM=offscreen`. Allow baseline diffs only when explained in the change description.
- **Counter-argument / trade-off:** A widget-level inventory genuinely catches a dropped control that a value-only comparison would miss, so the concept should survive. A structural-only comparison is also reasonable; if that is the intent, remove "当前值" from the comparison instead of leaving the choice to the implementer.

### Major

#### C-M1 - Minimum width is stated as 680 and 760, and fit at the minimum is undefined

- **Severity:** Major
- **Consolidated issue:** The completion criterion promises 680x540 (line 43), while the shell raises the minimum to 760 and keeps height 540 (line 177), Alternative K repeats 760 (line 377), and the manual walkthrough checks 760x540 (line 504). The automated test says only "最小尺寸" (line 263), so a test author cannot tell which value to assert. With a fixed 184 px sidebar at 680, the content area is about 496 px before padding, which is too narrow for the six-column hotword table and model action rows. At 540 height, four group headers plus eight navigation items at 40 px exceed the body area unless the sidebar scrolls or compacts.
- **Affected scope:** lines 43, 177, 183, 263, 377, 393, 504.
- **Evidence:** Current code is `setMinimumSize(680, 540)` at `src/qt_ui_main.cpp:1640`. Alternative K itself says the 680 content area would be about 396 px with a 220 px sidebar and would crowd or clip the table and model rows.
- **Why it matters:** One acceptance test must fail, and a verifier following line 43 tests a size the architecture says is unsupported.
- **Required resolution:** Choose one canonical matrix: default 880x740, minimum 760x540, tested at 100% and 150% with a named scaling mechanism. Update line 43, assert `minimumSize() == QSize(760, 540)` in the automated test, and make the manual walkthrough match. Define sidebar scrolling/compaction at 540 height and whether the hotword table may scroll internally.
- **Counter-argument / trade-off:** If 680x540 is a product compatibility contract, the 184 px sidebar must adapt (for example, collapse to an icon rail), and that larger change must be stated. If it is not, withdraw the 680 promise.

#### C-M2 - "恢复表单默认值" is undefined on immediate-apply pages

- **Severity:** Major
- **Consolidated issue:** The footer action is described as resetting "当前表单字段" (line 252) but never says whether that means the current page or the whole dialog. Current behavior resets every Apply-bound field across all pages and excludes startup, hotwords, model choices, and appearance (`src/qt_ui_main.cpp:1755-1780`). The VAD section separately says reset affects only existing VAD fields (line 242), implying page-local behavior and therefore a behavior change. On the appearance page, all controls are immediate-apply and save; resetting them either changes the live overlay (violating "不改外观即时值") or leaves the controls out of sync with the applied appearance.
- **Affected scope:** lines 173, 215, 242, 248, 252, 393, 429.
- **Evidence:** Current reset block `src/qt_ui_main.cpp:1755-1780` covers VAD, shortcut, text mode, destination, content, trigger, mute, paste delay, clipboard TTL, translation and speech, but not startup or hotwords. Appearance is applied and saved through the callback at `:3541-3545`.
- **Why it matters:** A single label change can silently alter reset behavior across eight pages, and the appearance page can enter a state where widgets no longer match the live overlay.
- **Required resolution:** Enumerate the reset set per page and state whether it is dialog-wide or page-local. Explicitly include or exclude appearance, startup, hotwords, model choices, and downloads. On the appearance page, either disable reset or make it restore and immediately apply/save defaults, and state that `appearance/*` is written. Add a test that snapshots all draft fields before and after reset.
- **Counter-argument / trade-off:** "表单" may naturally mean the whole dialog draft, which matches current behavior. "当前" plus the new label suggests a narrower scope, and the document claims no behavior change, so it must choose.

#### C-M3 - The target-file table contains wrong paths and wrong file actions

- **Severity:** Major
- **Consolidated issue:** The table says `SpeakAnything/AGENTS.md` (line 458), which resolves to a non-existent nested path; the real file is `AGENTS.md` at the repository root. It says `docs/designs/README.md` is "创建" (line 461), but that file already exists and already registers this design; following the table would overwrite it. It says `docs/designs/speakanything-ui-refresh/design_draft.md` is "修改" (line 462), but no such file exists; the actual document is `speakanything-ui-refresh.md`. It creates `src/app/main.cpp` (line 413) without accounting for the existing `src/main.cpp` CLI entry for `sensevoice-stream`, and it deletes `src/qt_ui_main.cpp` without saying which `main()` moves.
- **Affected scope:** lines 306-308, 413, 447, 458, 461-462.
- **Evidence:** Root `AGENTS.md` is 9 bytes; `SpeakAnything/AGENTS.md` is absent. `docs/designs/README.md` exists and registers the design. `design_draft.md` is absent. `CMakeLists.txt:328-331` builds `sensevoice-stream` from `src/main.cpp`.
- **Why it matters:** Literal execution can overwrite the design index, create a nested AGENTS path, or wire the wrong entry point into `sensevoice-ui`. The table is the implementation allowlist, so path errors are execution errors.
- **Required resolution:** Correct the rows to `AGENTS.md` / 修改, `docs/designs/README.md` / 修改, and either `design_draft.md` / 创建 or the real design document / 修改. Add an explicit "unchanged, CLI entry" row for `src/main.cpp`, or rename the new UI entry to `src/app/ui_main.cpp`. Validate every path with `Test-Path` before execution. Make the ADR/documentation verbs match whether the files exist.
- **Counter-argument / trade-off:** A careful reader can infer the intended paths, and the table may be illustrative. It is also the only file allowlist in the plan; literal execution is the expected use.

#### C-M4 - The token model contradicts itself in namespaces, layer count, scale, row height, and macOS adapter

- **Severity:** Major
- **Consolidated issue:** The roles are listed flat (`shell.background`, `surface.card`, `text.primary`, etc., line 140), then the settings window is said to read only `shell.*` and `status.*` (line 143), which excludes the card, text, border, focus, and selection roles it needs. Section 2 says three layers (line 137); the ADR says "两层 token" (line 306). The type scale is `11, 12, 13, 15, 22, 26` (line 151), while page titles are 22-24 and card titles 14-15 (lines 152-153), so 14 and 24 are outside the scale. "行高最小 44 px" (line 156) conflicts with navigation items at least 40 px (line 183). The file table says `design_tokens` provides a "Windows/macOS QSS 适配器" (line 418), while the architecture says macOS uses native controls and `QPalette` (lines 141, 159). Step 4 says token values should equal existing literals, but no role-to-literal seed table exists, and each role maps to several current values.
- **Affected scope:** lines 137-159, 183, 306, 418.
- **Evidence:** Current QSS has multiple distinct values for background, card, border, text, accent, and slider; `shell.background` could seed from `#F7F8FA` or `#EEF0F3`, and `surface.card` from `#FFFFFF`. The document does not choose.
- **Why it matters:** The token layer is the mechanism for consistency, but an implementer cannot tell which namespaces are legal, how many layers to document, which scale steps exist, or whether macOS receives QSS. Tests written against one reading will fail another.
- **Required resolution:** Nest all roles under `shell.*` or list every namespace the settings window reads. Standardize on three layers: base scale, semantic roles, surface adapters. Close the type scale and replace ranges with single tokens, or add 14 and 24. Rename "行高" to "设置行最小高度" and state the 40 px navigation exception. Make the macOS adapter map roles to `QPalette` and explicitly not generate QSS. Add a seed table mapping each role to its current literal(s) and state that step 4 extracts tokens without changing applied styles.
- **Counter-argument / trade-off:** The value table is labeled "建议基础值" and may be guidance rather than a closed set. If so, the document must distinguish binding values from suggestions and list every exception; otherwise a test cannot know what is normative.

#### C-M5 - Component and vocabulary contracts are undefined or contradictory

- **Severity:** Major
- **Consolidated issue:** The `Action` variant allows one to three buttons (line 196), while the model card specifies download plus browser download, import, and delete (line 235) and Alternative D calls it a four-action model row (line 334). "设备" means compute device, audio output device, and device-capability detection in different sections (lines 194, 207, 219, 223, 399), with no types. The overlay's "三种样式", `AppearanceSettings`, and `overlay.identity` are used but never defined (lines 50, 69, 107, 143, 419). The hotword six columns are named but not defined: editability, types, ranges, whether "命中" is read-only, what "增强" changes, and how UI order maps to persistence are absent. New input-page elements ("推荐快捷键", "当前输入方式" summary, "设备检测结果") have no content definition.
- **Affected scope:** lines 50, 69, 107, 143, 194-196, 207-235, 334, 399, 419, 427.
- **Evidence:** `src/models_page.cpp:230-240` creates four action buttons. Translation compute, speech device, and device detection are distinct code paths. The hotword UI order (enabled, phrase, aliases, boost, hits, keep) differs from persistence order (phrase, aliases, enabled, hits, boost, keep).
- **Why it matters:** The model row cannot be classified, "设备" can bind the wrong control, and the hotword workbench can persist the wrong column. These are functional-bug risks, not vocabulary preferences.
- **Required resolution:** Define a state matrix for model actions (not downloaded, downloading, installed, unavailable) and extend `Action` to one-to-four or add a `ModelActions` variant. Replace "设备" with "计算策略", "音频输出设备", and "设备能力检测" consistently. Name the three overlay styles, define `AppearanceSettings` fields and ownership, and add `overlay.identity` to the role list. Define every hotword column's type, range, editability, and UI-to-persistence mapping. Define the new input-page summaries or remove them from this round.
- **Counter-argument / trade-off:** Existing code can be read to recover some meanings, but the design claims to preserve behavior and be self-contained; an implementer should not have to reverse-engineer column semantics or device bindings.

#### C-M6 - Creating `settings_layout` would add a third copy of existing helpers

- **Severity:** Major
- **Consolidated issue:** The plan says to create `src/ui/settings/settings_layout.h/.cpp` to provide card, row, group-title, slider, segmented-button, and hint helpers (lines 277, 421). Those helpers already exist as `scrollable`, `addCard`, `addRow`, `carded`, `pageLayout`, and `addSlider`; `ModelsPage` also has a near-identical card builder. Creating a new module without retiring the old ones produces a third copy of the same pattern, which works against the stated consistency goal.
- **Affected scope:** lines 275-278, 421.
- **Evidence:** `src/qt_ui_main.cpp:2057, 2067, 2086, 2100, 2112, 2614`; `src/models_page.cpp:86-119, 214-250`.
- **Why it matters:** The visual refresh would increase duplication while claiming to reduce it. Future changes would have to be made in three places.
- **Required resolution:** Define `settings_layout` as a migration of the existing helpers, list which functions move and which are deleted, and convert `ModelsPage` to the shared module. If that migration is not worth the churn this round, drop the new module and reuse the existing helpers in place.
- **Counter-argument / trade-off:** A new module may be cleaner if the old helpers are actually removed. The plan currently says "创建" and does not mention deletion, so the default outcome is duplication.

#### C-M7 - The build split omits three concrete hazards: duplicate main, file-local native macro, and identity constants

- **Severity:** Major
- **Consolidated issue:** `SENSEVOICE_UI_SOURCES` is shared with `settings-ui-test`, but the plan puts `src/app/main.cpp`, which defines `main()`, into the UI source group; linking the test would produce two `main()` definitions. `SENSEVOICE_NATIVE_INPUT` is defined at the top of `qt_ui_main.cpp` and used at 16 sites; moving hotkey/session/injection code into separate translation units compiles out native-input branches unless the definition moves with them. Collapsing identity into one "org/app" constant can move macOS user data, because the current application name, organization name, QSettings org/app, mutex name, and hotword path are not one pair.
- **Affected scope:** lines 282, 391, 411, 437-450.
- **Evidence:** `src/qt_ui_main.cpp:18` defines `SENSEVOICE_NATIVE_INPUT`; use sites include `:2800, 2830, 3363, 3570, 3896, 4227, 4297, 4326, 4379, 4498, 4505, 4513`. `application.setApplicationName("SenseVoice 语音输入")` and `setOrganizationName("SenseVoice")` at `:5431-5432`; `QSettings("SenseVoice", "LocalDictation")`; `hotwordsPath()` uses `QStandardPaths::AppDataLocation` at `:601`. `src/app/main.cpp` would define `main()`, and `settings-ui-test` also links UI sources.
- **Why it matters:** These are build/link failures and behavior losses that the "mechanical move" would introduce. The macOS path change could silently orphan hotwords or settings.
- **Required resolution:** Define `SENSEVOICE_UI_SOURCES` without the app entry point, and add the entry point only to `sensevoice-ui` (or use a separate `SENSEVOICE_UI_APP_SOURCES`) for both Windows and macOS. Move `SENSEVOICE_NATIVE_INPUT` to `target_compile_definitions` or a shared `ui_config.h`, and add a compile-time assertion. Define separate constants for window title, application name, organization name, QSettings organization/application, mutex, tray text, and hotword path; preserve current values exactly and add a macOS path/migration test.
- **Counter-argument / trade-off:** A mechanical file move still requires build-configuration changes; that does not make them optional. If the split is deferred (C-B1), most of this risk moves with it.

#### C-M8 - The screenshot and geometry gates claim checks the tooling does not implement

- **Severity:** Major
- **Consolidated issue:** The design requires "1.1 px 中心和底边漂移阈值" (line 500), but `verify_preview_geometry.ps1` asserts only that the center range and center deltas are `<= 1.1`; it computes `Last15DistinctBottom` and distinct widths but never asserts a bottom-drift or width limit. It hardcodes `--bubble-style ring` and runs once, so capsule and panel are not checked. The screenshot criteria have no artifact location, capture command, fixed environment, page/language matrix, filenames, comparison metric, or tolerance. `--preview-all` shows three windows but never saves them; `--preview-image` saves one; `preview_harness.cpp` is scoped to settings pages. `QImage::save()` results are ignored, so "screenshot failure must return non-zero" is not guaranteed. "每页单独验收" names no checklist or exit condition.
- **Affected scope:** lines 46, 50, 130, 264, 392, 491, 497-500, 504, 518.
- **Evidence:** `tools/verify_preview_geometry.ps1:31-33` hardcodes `ring`; `:77, 94` compute bottom/width values; `:103-111` only assert center deltas. `src/qt_ui_main.cpp:5564` calls `dialog.grab().save(...)` without checking the result.
- **Why it matters:** The only automated visual regression check can pass while the bottom edge drifts or two overlay styles regress. The screenshot claim is not reproducible, so "screenshots unchanged" is a manual impression rather than a gate.
- **Required resolution:** Define a baseline directory and a scripted capture matrix: eight pages in `zh` and `en` at 880x740 and 760x540, plus capsule/panel/ring overlay captures. Fix scale, theme, language, and filenames. Choose PNG hash or a documented pixel tolerance. Add the missing bottom-drift and width assertions, loop over all three bubble styles, and check every `QImage::save()` result. Define a per-page acceptance checklist with pass/fail evidence.
- **Counter-argument / trade-off:** Exact pixel equality is brittle across Qt and OS versions; a perceptual threshold or pinned environment is a valid alternative. Manual visual review is also valid, but then the document should not present the check as automated or reproducible.

#### C-M9 - The decision record is missing Risks & Limits and the in-place restyle baseline

- **Severity:** Major
- **Consolidated issue:** The specification has no `Risks & Limits` section: constraints (§9) and scattered notes are not a risk register with trigger, impact, detection, mitigation, verification, and owner. Uncovered failure modes include QSS/focus styling on tables and sliders, tab-order regressions from making the sidebar focusable, macOS dark-mode contrast, 150% DPI and English clipping, destructive model deletion, preview data integrity, and controller-split regressions. The Alternatives section (A-L) never evaluates the natural cheap baseline: restyle existing widgets in place, extract only the settings dialog and model page, leave the overlay controller, reuse existing helpers, and fix the one known preview write with a guard. Without that baseline, the roughly 40-file program has no cost/benefit comparison; its only driver is the unrecorded 600-line rule.
- **Affected scope:** lines 293-311 (constraints and missing risk section); lines 311-383 (Alternatives A-L).
- **Evidence:** The word "风险" appears only inside Alternative E's rationale (line 341). Alternative C splits four files; E changes sequencing; G is an app stylesheet; J is one-shot. None compares the no-split in-place restyle.
- **Why it matters:** Risk material is distributed but never paired with mitigations, so reviewers cannot see what could fail or how it would be detected. Without the in-place baseline, the document does not prove that the token layer plus full split is proportionate to a settings refresh.
- **Required resolution:** Add `## Risks & Limits` before Alternatives with a table: Risk, trigger/failure mode, impact, mitigation, verification, owner. State explicitly that security/scalability exposure is N/A for a local Qt Widgets UI. Add "Alternative M: in-place restyle plus narrow extraction" with effort, risk, expected visual outcome, test surface, and rollback, and compare it directly to the chosen path.
- **Counter-argument / trade-off:** Twelve alternatives already exceed the standard's minimum, and some Cons cover risk material. Quantity is not the issue; the cheapest viable baseline and an actionable risk register are absent.

#### C-M10 - Irreversible model deletion is mitigated only by a text label

- **Severity:** Major
- **Consolidated issue:** The design keeps model deletion instant and irreversible and treats "删除后需重新下载" as sufficient mitigation (line 238). That is passive notification standing in for active mitigation: a mis-click destroys a multi-GB download with no recovery. The current click handler calls `manager_->remove(*model)` directly, with no confirmation or recovery step.
- **Affected scope:** lines 238-239, 249.
- **Evidence:** `src/models_page.cpp:274-279` calls `manager_->remove(*model)` in the click handler.
- **Why it matters:** The user can lose a large download with one click, and the design records no accepted risk or follow-up.
- **Required resolution:** If no behavior change is allowed this round, state the irreversible deletion as an accepted risk with a follow-up item and owner, rather than presenting the label as mitigation. If a low-friction fix is allowed, quarantine the model directory to a `.trash/` path on the same volume, hide the row immediately, show "已删除 · 撤销", and purge on next launch or after N days.
- **Counter-argument / trade-off:** Quarantine adds model-manager state, startup cleanup, and tests, which is real scope. The non-goal forbids behavior changes, so an explicit accepted-risk record may be the correct minimal resolution; the current text does neither.

### Nits

#### C-N1 - Several baseline descriptions are factually wrong

- **Severity:** Nit
- **Consolidated issue:** The document says the hotword page needs a header added (lines 24, 35), but it already sets six header labels. It says "原文/精简改为分段按钮" (line 211), but those controls are already checkable `QPushButton#modeSegment` buttons. It says color buttons "保留可读颜色值" (line 215), but the hex value exists only in a tooltip. It says "16 个分散 getter" (line 400), but the accessor block has 14.
- **Affected scope:** lines 24, 35, 211, 215, 400.
- **Evidence:** `src/qt_ui_main.cpp:2505-2512` (hotword header labels), `:2260-2270` (segmented buttons), `:2046-2049` (color tooltip), `:1792-1898` (14 getters).
- **Why it matters:** The before/after inventory and problem statement should describe restyle/add work accurately. Wrong counts weaken trust in otherwise precise measurements.
- **Required resolution:** Reword as "restyle the existing header", "restyle the existing segments", and "add a visible color value". Correct the count to 14 or drop it.
- **Counter-argument / trade-off:** "清楚的表头" is qualitative, and the existing header may be visually weak. The color-value claim and the getter count are objectively wrong.

#### C-N2 - `Open Questions` negates itself and references undefined process artifacts

- **Severity:** Nit
- **Consolidated issue:** The section opens with "无未决问题" and then lists two items that must be confirmed before finalization (lines 64-69), so readers scanning for unresolved decisions skip binding scope content. It also refers to an "早期 brief" and "Phase A waiver" without linking the brief or defining the waiver.
- **Affected scope:** lines 64-69.
- **Evidence:** Both bullets restate normative scope decisions already in Goal 7 and Non-Goals, but the heading signals the opposite.
- **Why it matters:** Discoverability and auditability, not behavior. A future reviewer cannot tell what prior decision was waived.
- **Required resolution:** Retitle to "已定稿决策" and phrase the bullets as settled decisions, or present them as real questions with owner and deadline. Link the brief and define the waiver, or remove the references.
- **Counter-argument / trade-off:** Nothing is lost semantically because the items are restated elsewhere; the cost is reader confusion.

#### C-N3 - Editorial and template issues remain

- **Severity:** Nit
- **Consolidated issue:** Required standard areas use non-standard headings (`The Problem`, `Technical Plan (Architecture)` instead of `Problem Statement`, `Proposed Architecture`); numbered H3s coexist with unnumbered CUJs and alternatives; three page orders coexist (section 5 order, sidebar order, migration order); the ASCII shell diagram misaligns; every alternative uses "Rejected by author" boilerplate; subjective framing appears at lines 18, 28, and 133; the SVG exception names no approver or decision record.
- **Affected scope:** lines 16, 18, 28, 60, 103, 105-301, 165-175, 203-242, 317-383.
- **Evidence:** Structural review only; no behavioral claim.
- **Why it matters:** Template compliance and scanability. The content is present, but a reader cross-checking a layout against its navigation group must mentally reorder it.
- **Required resolution:** Add the standard term parenthetically or rename the headings; drop or standardize numbering; align section 5 to the sidebar or state that it follows implementation batches; rebuild the diagram as a table or fixed-width ASCII; replace "Rejected by author" with `Rationale`; trim subjective framing; give the SVG exception a concrete ADR/approval gate or remove it.
- **Counter-argument / trade-off:** These are labeling and presentation choices; none changes the implementation. They still cost discoverability in a document this long.

#### C-N4 - Residual test precision and terminology gaps

- **Severity:** Nit
- **Consolidated issue:** `pageCount() == 8` is not guaranteed by the construction path: the model page is added only when `ModelManager != nullptr`, otherwise there are seven pages. `settings_pages.h` is said to declare eight builders, but the table creates seven `pages_*.cpp` files plus a separate `ModelsPage`. Assistive-technology names are promised but not verified: the layout test checks focus policy and object names, not accessible names. "旧数据迁移规则" is asserted unchanged but never described.
- **Affected scope:** lines 36, 44, 95, 259, 263, 420-434.
- **Evidence:** Model page is conditionally added at `src/qt_ui_main.cpp:1721-1725`. The remove button is icon-only with a tooltip at `:2530-2532`; there are no `setAccessibleName` calls under `src/`. The migration behavior is not enumerated.
- **Why it matters:** Tests can assert seven pages in a null-manager fixture, accessibility can remain unverified, and "migration unchanged" cannot be checked from the document.
- **Required resolution:** Make the model page always present with a null-safe disabled state, or require a `ModelManager` fixture and assert eight. Say "seven page builders plus `ModelsPage`" or add a model-page builder. Add `setAccessibleName`/`setAccessibleDescription` and an offscreen `QAccessible` assertion. List the existing migrations and where they run.
- **Counter-argument / trade-off:** Production always passes a manager, so eight is the real case; the public API should still state the invariant. Qt may fall back to tooltip text, but the icon-only remove button is not covered by the current test.

#### C-N5 - Section 7 duplicates the Detailed Plan at implementation altitude

- **Severity:** Nit
- **Consolidated issue:** `PreviewOptions` and its flags appear at line 258 and again at line 404; `pageCount()`/`selectPage(int)` at 259 and 403; the inventory at 260 and again at 390 with commands at 477-482; the regression list at 264 and again at 500. The architecture section names concrete types and CLI flags before the plan section that should own them.
- **Affected scope:** lines 254-264 against 390-404 and 464-522.
- **Evidence:** Direct textual duplication; no behavioral claim.
- **Why it matters:** Any later change will silently desynchronize one copy, and reviewers will cite different lines for the same requirement.
- **Required resolution:** Reduce section 7 to five outcome statements (no registry writes; deterministic page/language/theme; inventory gate; translation gate; offscreen layout test) and move all type names, flags, and commands into the detailed plan. Give each fact one owner.
- **Counter-argument / trade-off:** Repetition aids top-down reading, but this document already has a detailed range and the copies are not marked as summaries.

## Traceability Matrix

Numbers are the finding numbers in each stage report. "—" means the stage did not raise that issue or did not review that range. C-B1 and C-M1/M2/M3 etc. merge duplicate findings; the strongest evidence is cited in the finding.

| Consolidated ID | Stage 5 `zoom_out` | Stage 6 `comprehension` | Stage 7 `critic` | Stage 8 `intermediate` | Stage 9 `readiness` |
| --- | --- | --- | --- | --- | --- |
| C-B1 | 2 (controller split) | — | 1 (UI-only scope) | 1 (settings refresh carries refactor) | — (scope not re-litigated) |
| C-B2 | 1 (600-line gate) | — | 13 (600-line gate) | 2 (600-line gate) | 27 (gate checks too little) |
| C-B3 | 3 (preview determinism) | 5 (preview interface) | 3 (preview not write-free) | 5 (preview over-scope) | 3, 4, 19, 21 (QSettings, options, registry, translator order) |
| C-B4 | — | 14 (snapshot fields) | 13 (undefined interfaces) | — | 5, 7, 8, 11, 20, 21, 29, 30 (callback, contracts, pages, tests, restore, callbacks, builders) |
| C-B5 | — | — | — | — | 1, 2 (Qt prefix, LinguistTools) |
| C-B6 | 11 (hotword bytes) | 7 (hotword columns) | 7 (hotword bytes) | 9 (hotword row) | 12 (round-trip test) |
| C-B7 | 10, 12 (tokens, icons) | 2 (visual target) | 5 (visual target) | 4 (visual deliverable) | — (token wording is C-M4) |
| C-B8 | 7 (translation gate) | 9 (触点页面) | 11 (触点/中文回退) | 7 (translation gate) | 23 (static check) |
| C-B9 | 6 (inventory) | — | 8 (inventory) | 6 (inventory) | 9, 10, 11 (names, schema, provenance, tests) |
| C-M1 | 4 (minimum width) | 1 (minimum width) | 2 (minimum width) | 9 (min-width row) | 18 (three widths) |
| C-M2 | 5 (reset scope) | 8 (reset) | 9 (reset) | 9 (reset row) | 25 (appearance reset) |
| C-M3 | 13, 15 (main, stale targets) | 10, 11 (file table, main) | 26 (docs update) | 10 (target table) | 6, 28 (allowlist, main) |
| C-M4 | 9, 10 (macOS, token layers) | 3, 12, 13 (namespaces, scale, layers) | 6 (token model) | 9 (token row, macOS row) | 22, 26 (token wording, macOS gate) |
| C-M5 | 8 (model delete) and 9 (macOS) | 4, 6, 7, 17 (overlay, device, hotwords, input) | 10 (Action variant) | 9 (Action, device, overlay rows) | 30 (builder count) |
| C-M6 | 10 (settings_layout) | — | — | 8 (third copy) | — |
| C-M7 | — | 11 (main ambiguity) | — | — | 13, 14, 15 (UI sources, macro, identity) |
| C-M8 | — | 16 (1.1 px) | 22 (screenshot baselines) | — | 16, 17, 24 (geometry, baselines, per-page gate) |
| C-M9 | — | — | 4, 12 (risks, in-place baseline) | 3 (cheap baseline) | — |
| C-M10 | 8 (model deletion) | — | — | — | — |
| C-N1 | 16 (getter count) | — | 20 (baseline controls) | 11 (baseline descriptions) | — |
| C-N2 | — | 18 (process refs) | 14 (Open Questions) | 12 (Open Questions) | — |
| C-N3 | 14 (section duplication) | — | 15, 16, 17, 18, 19, 24, 25 (editorial) | — | — |
| C-N4 | — | 15 (migration rules) | 21, 23 (pageCount, AT names) | — | 30 (builder count) |
| C-N5 | 14 (section 7 duplication) | — | — | — | — |

**Severity reconciliation notes:**

- C-B2 is marked Blocker because the only stated done-condition is a command that returns five out-of-scope files. Stage 7's Major rating understated the operational effect of running the command verbatim.
- C-B6 is marked Blocker for the stated "arbitrary legacy bytes" guarantee, but the canonical no-edit round-trip is achievable. This distinction is the main disagreement between Stage 5's Nit and Stages 7/9's Major; the consolidated finding preserves both truths.
- C-B9 and C-B8 are marked Blocker because they are the sole instruments for "no setting lost" and "no Chinese fallback". The underlying fixes are local, which is why earlier stages rated them Major; the severity reflects the consequence of trusting a false gate, not the size of the edit.

## What Survives Scrutiny

These facts and design elements were verified and do not need rework:

- **The problem diagnosis is code-accurate.** The sidebar is non-focusable (`src/qt_ui_main.cpp:1704`) and 168 px wide (`:1703`); the dialog uses `setMinimumSize(680, 540)` (`:1640`); all eight pages exist (`:1712-1726`); preview really does write the registry (`:5534` -> `:2767` -> `:3259-3260`). The motivation is grounded, not speculative.
- **The quantitative baseline is exact.** `qt_ui_main.cpp` is 5,587 lines; `InputSettingsDialog` is about 1,127 lines; `VoiceInputWindow` is 2,684 lines; there are exactly nine `add_test` entries; the dialog constructor takes 22 parameters; the accessor block has 14 getters. The 600-line problem is real even though the gate around it is broken.
- **The settings-dialog extraction is justified in principle.** Replacing a 22-parameter constructor and 14 scattered getters with a snapshot/draft is a sound response to a real defect. The failure is the missing contract (C-B4), not the motivation.
- **Surface separation is sound.** Settings-shell styling, overlay identity, user-owned `appearance/*` colors, the three overlay styles, and the no-SVG constraint are correctly separated. Alternatives B, H, and L argue this correctly. The overlay must retain its dark/teal identity and user theme data.
- **The verify-first sequence is correct.** The current preview cannot prove a read-only run, so building the instrument before measuring is justified. Alternative E is genuinely worse. The defect is the instrument's guarantee, not the order.
- **Reuse instincts are mostly right.** Deduplicating `to_qstring` (three copies), sharing `SENSEVOICE_UI_SOURCES`, avoiding `Q_OBJECT`/AUTOMOC for a same-class multi-TU split, and reusing the existing card/row/slider helpers are all grounded in the code. The `settings_layout` problem is that the plan creates rather than migrates (C-M6).
- **The hotword UI/persistence split is necessary.** The UI column order and persistence order genuinely differ, so separating display order from field order is not cosmetic. The current saver is the reason the byte guarantee needs narrowing (C-B6).
- **The single translation context is coherent.** `resources/i18n/speakanything_en.ts` has one `SpeakAnything` context, and `Q_DECLARE_TR_FUNCTIONS(SpeakAnything)` is used in the UI. The problem is coverage and runtime loading, not the context model.
- **The `pageCount()` / `selectPage(int)` idea is sound.** A public page interface is better than `findChild("settingsStack")`, but it does not address scope, determinism, or contract gaps.
- **The CUJs are clear and useful.** The five journeys state observable outcomes for shortcut setup, translation/model selection, hotword editing, keyboard-only use, and immediate appearance preview. They are the right acceptance anchors once the contradictions above are resolved.
- **The alternatives are substantive.** Twelve options with concrete Cons and specific rationales satisfy the Alternatives Rule in substance, even though the cheapest in-place baseline is missing (C-M9).

## Recommended Revision Sequence

The order below is dependency order. Do not start visual migration until steps 1-6 are complete. This is a revision of the existing document, not a restart.

1. **Decide scope and the 600-line rule.** Remove `src/ui/overlay/*` from this design, or explicitly widen scope and add controller characterization tests. Record the 600-line rule's source and scope, add a grandfather list, switch the gate to changed files including `.mm` and `tests`, and add per-file budgets. This unblocks C-B1, C-B2, C-M7, and the file table.
2. **Fix the execution environment.** Replace the Qt prefix with the real path, add a `Test-Path` preflight, make Qt LinguistTools/`lrelease` a hard prerequisite, build the `.qm`, and add a runtime English-label assertion. This unblocks C-B5 and makes C-B8 testable.
3. **Redefine preview as a deterministic instrument.** Split read-only loading from migration writes, guard or isolate the settings store, and specify the complete `PreviewOptions`/CLI/manifest contract, including theme, model directory, page identifiers, precedence, output paths, and exit codes. Automate the before/after store comparison. This closes C-B3.
4. **Write the data and page contracts.** Define `SettingsSnapshot`, `SettingsDraft`, `SettingsCallbacks`, and `PageBundle` with fields, types, defaults, ownership, page order, sidebar-row mapping, cancel/restore behavior, and translator parse order. Map every current constructor argument and getter. This closes C-B4 and unblocks page implementation.
5. **Fix the visual target and token model.** Add exact colors, contrast ratios, reference link/source, font/fallback/px policy, Windows dark/high-contrast behavior, and an icon decision. Resolve the `shell.*` namespace, layer count, scale gaps, row-height exception, macOS palette-vs-QSS wording, and role-to-literal seed table. This closes C-B7 and C-M4.
6. **Rewrite the four gates.** Define the inventory schema and baseline origin; narrow the hotword criterion to canonical no-edit plus row-level edit comparison; replace the static translation check with extraction plus runtime coverage; define the screenshot/geometry matrix, comparison metric, per-style loop, save-error handling, and per-page checklist. This closes C-B6, C-B8, C-B9, and C-M8.
7. **Resolve the acceptance and behavior contradictions.** Pick one minimum size and test it; enumerate reset scope per page and decide appearance reset semantics; define model-action states and device terminology; resolve irreversible deletion as accepted risk or quarantine; correct every file path/action and distinguish the two `main()` files. This closes C-M1, C-M2, C-M3, C-M5, and C-M10.
8. **Complete the decision record.** Add `Risks & Limits` with triggers, impacts, mitigations, verification, and owners. Add Alternative M (in-place restyle plus narrow extraction) with cost, risk, outcome, tests, and rollback. Correct the remaining factual counts and editorial issues. This closes C-M9 and the Nits.
9. **Run a fresh Stage 9 readiness pass.** Verify that every command executes on the stated environment, every gate has a pass/fail artifact, and every contract has a code-level definition. Only then declare implementation-ready.

## Unverified Limitations

The following items were not measured in this environment, and none of the recommendations above should be read as measured facts:

- **macOS runtime is unverified.** No macOS build, native palette behavior, dark mode, focus path, hotword storage path, or translation loading was executed. A source review cannot substitute for a macOS build and smoke test.
- **Pixel output is unverified.** No screenshot baseline, pixel diff, or cross-Qt/OS comparison was produced. The repository has no capture/diff protocol, and `verify_preview_geometry.ps1` measures one ring-style window only.
- **Actual fit at the minimum size is unverified.** Whether 760x540 with a 184 px sidebar avoids clipping in the six-column hotword table and model action rows at 150% scaling was not measured. The 680-versus-760 contradiction is textual evidence, not a layout measurement.
- **English runtime UI is unverified.** Qt LinguistTools is absent, no `lrelease` executable or `.qm` file exists, and the static `.ts` counters do not prove runtime English. The English screenshot commands cannot be executed as written.
- **Arbitrary legacy hotword byte preservation is unverified and appears unachievable.** The saver behavior was inspected; no exhaustive corpus of user hotword files was measured. The canonical no-edit case is the only byte-level case that can be made reliable without changing the saver.
- **The 600-line rule's provenance outside the repository is unknown.** The repository contains no such rule; it may be an external convention. Its binding status cannot be verified from the project alone.
- **The mechanical split's behavior preservation is unverified.** No controller tests exist, and no split was performed. The claim that hotkeys, tray, sessions, translation, speech, and injection remain unchanged is a plan assertion, not a measurement.
- **Model download/delete behavior and preview device enumeration were not exercised.** The review inspected code paths and acceptance text; it did not run the application against real models, devices, or user settings.

These limitations are why the verdict is "return to drafting", not "implement with caveats": the missing evidence is concentrated in the exact gates the design relies on.

--------------------------------------------------------------------------------

*Consolidated with EGM*
