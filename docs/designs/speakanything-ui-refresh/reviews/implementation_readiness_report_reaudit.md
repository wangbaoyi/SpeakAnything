# Goldfish Review Report

**Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (`DetailedRange` 360-571; `FeatureRange` 1-359 for context)

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-implementation-readiness` | completed |
| `egm-codebase-details` | completed |
| `egm-style` | completed |

## Verdict

**not implementation-ready**

The revision resolves the controller-split scope question and materially improves the hotword acceptance wording, the preview write guard, the Qt path, the file allowlist, and the visual token table. It does not yet close the data/page contract, the hotword mapping, the preview determinism contract, the reset/apply semantics, the inventory baseline, or the missing LinguistTools toolchain.

**Severity count:** 4 Blockers, 12 Majors, 5 Nits.

### Resolution Status

| Prior finding | Status after revision | Basis |
| --- | --- | --- |
| C-B1 overlay controller split removed from scope | **Resolved for the controller split** | `src/ui/overlay/*` is gone; `VoiceInputWindow` and controllers remain in `qt_ui_main.cpp` (design 14, 57, 479, 497). A residual `overlay_theme` extraction is still a scope-wording Major. |
| C-B2 600-line gate changed-file + grandfather | **Partially resolved** | Changed-file and `qt_ui_main.cpp` grandfather semantics are stated, but the command is not baseline-relative and false-passes after staging/committing. |
| C-B3 preview does not write QSettings; preview contract complete | **Partially resolved** | The read-only load/migration split and write guard are specified. GPU/audio/model reads still make screenshots machine-dependent, and the CLI/manifest contract is incomplete. |
| C-B4 snapshot/draft/callbacks/PageBundle and appearance callback | **Partially resolved** | The structs and appearance callback now exist and match the current callback role, but the loader cannot populate all fields, builders have no construction contract, and reset/apply ownership is unresolved. |
| C-B5 Qt path and LinguistTools/lrelease executable | **Not executable here** | The Qt path is now correct, but `lrelease.exe`, `Qt6LinguistToolsConfig.cmake`, and any `.qm` artifact are absent. |
| Hotword canonical no-edit + per-row test | **Criterion improved; mapping is wrong** | The narrowed byte guarantee is achievable, but the normative UI-to-TSV mapping corrupts fields if implemented literally. |
| Visual token exactness | **Partially resolved** | Exact hex values exist; contrast, typography units, focus-on-accent, and the amber slider are unresolved. |
| Translation runtime gate | **Partially resolved** | Touchpoint pages are defined, but the runtime check has no executable owner or exact sample. |
| Inventory schema | **Partially resolved** | Dynamic values are excluded and the two CLI steps are explicit, but schema, stable IDs, and baseline origin are missing. |
| File allowlist actions | **Resolved for paths/actions** | `AGENTS.md`, `docs/designs/README.md`, the real design file, and deleted model-page files all have valid paths/actions. Source-composition gaps remain. |
| Reset scope | **Not resolved** | “All Apply-bound fields” contradicts the explicit exclusion list and `SettingsDraft` membership. |
| Risks | **Partially resolved** | A risk table exists, but it has no trigger/detection/verification/owner and overstates the automated 150% mitigation. |

## Prioritized Findings

### Hotword UI-to-TSV mapping is inverted - Severity: Blocker

* **Reviewer Lens:** `egm-style`, `egm-implementation-readiness`
* **Affected Scope:** design line 251; `src/qt_ui_main.cpp:2505-2511, 2551-2583`; `src/text_processor.cpp:641-651`
* **Discrepancy Description:** The design states the UI columns are `启用, 标准词, 别名, 增强, 命中, 不翻译` and then says `UI 0/1/2/3/4/5 -> TSV phrase/aliases/enabled/hits/boost/keep`. The actual UI order is enabled, phrase, aliases, boost, hits, keep; the actual TSV order is phrase, aliases, enabled, hits, boost, keep. The stated mapping sends UI column 0 (the enabled checkbox) to the TSV phrase string, column 3 (boost) to hits, and column 4 (hits) to boost. Line 331 also claims the UI and persistence orders are separated by a fixed mapping, but the only fixed mapping written is wrong.
* **Rationale & Recommendation:** This is a direct data-corruption path for every edited hotword row. Replace line 251 with an explicit index table: UI `0 -> enabled (TSV 2)`, `1 -> phrase (0)`, `2 -> aliases (1)`, `3 -> boost (4)`, `4 -> hits (3)`, `5 -> keep (5)`. Add a round-trip test that writes one row through the UI, parses it with `TextProcessor::load_hotwords`, and asserts each field independently; a whole-file hash alone can accidentally pass if the same wrong mapping is used on both sides.
* **Counter-argument:** The preceding UI-column list makes the intended order recoverable, and a type-aware implementer might not write a checkbox into a text field. That is not sufficient for a normative mapping: the test plan does not isolate column identity, and the literal instruction is destructive.

### SettingsSnapshot cannot be populated by its only loader - Severity: Blocker

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** design lines 377-405, 448-452; `src/qt_ui_main.cpp:447-455, 2742, 3295, 4154-4156`
* **Discrepancy Description:** `SettingsSnapshot` contains `hotwords`, `startup_enabled`, and `bubble_style`, but the only loader is `loadSettingsSnapshot(const QSettings&)`. Hotwords are loaded from `hotwords.tsv` during model loading, startup state comes from `platformStartupEnabled()` (Windows Run key or macOS login item), and `bubble_style` is a `VoiceInputWindow` constructor argument, not a QSettings value. The promised single read path therefore cannot return the structure it declares. `SettingsLoadResult` also has only a boolean `needs_migration`, which cannot identify which of the required legacy migrations (`copy-both`, missing `output/content`, `vad/profile_version=0`) applies.
* **Rationale & Recommendation:** Split the contract into QSettings-backed fields and explicit runtime context, for example `loadSettingsSnapshot(const QSettings&, StartupState, BubbleStyle)` or a separate `SettingsRuntimeState`. Define whether `snapshot` is pre- or post-migration, and make `migrateSettings` receive explicit migration facts or persist exactly the effective values returned by the loader. Add one fixture per migration with expected post-migration keys.
* **Counter-argument:** The caller can patch the fields after loading. That makes the function not a complete snapshot and forces every test and preview to reimplement the same patching; it also leaves the migration facts undefined.

### PageBundle and page builders still lack a construction contract - Severity: Blocker

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 432-438, 443-444, 468; design lines 201-208 and CUJ 4 at line 112
* **Discrepancy Description:** `PageBundle` exposes `root`, `content`, `refresh_dependencies`, `reset_draft`, and `read_into_draft`; it has no initial-value entry point, no builder signatures, no dependency parameters, and no ownership rule for `root` versus `content`. A fresh implementer must invent how a page receives the snapshot, device lists, shortcut presets, appearance values, and callbacks. Separately, the sidebar visual order is `输入, 输出, VAD, 翻译, 朗读, 热词, 模型, 外观` (lines 202-205), while the page index order is `输入 0, 输出 1, 外观 2, 翻译 3, 朗读 4, 热词 5, 模型 6, VAD 7` (line 206). `selectPage(2)` therefore means appearance while sidebar row 2 is VAD; CUJ 4 says page order and visual order match, and group headers have no row mapping.
* **Rationale & Recommendation:** Define each builder signature, for example `PageBundle buildInputPage(const SettingsSnapshot&, const SettingsCallbacks&, PageDeps&)`, and state whether `root` is the stack widget and `content` the scrollable inner page. Define callback capture/lifetime and fixed `refresh_dependencies` order. Add a sidebar row table (row, group header or page, page index) and specify arrow/Home/End behavior over page rows only.
* **Counter-argument:** A builder can take the snapshot by convention and the dialog can keep a row-to-page map. That is exactly the contract a fresh implementer should not have to invent, especially because two stated acceptance sentences currently disagree.

### Qt LinguistTools/lrelease is still absent - Severity: Blocker

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`; `egm-style` treated the prerequisite as documented rather than installed
* **Affected Scope:** design lines 504-511, 563; `CMakeLists.txt:561-574`; `E:\Programming environment\Qt\6.8.3\msvc2022_64`
* **Discrepancy Description:** The Qt prefix is now correct, but `Test-Path "$env:QTDIR\bin\lrelease.exe"` returns false. No `lrelease.exe` exists under the Qt installation, `Qt6LinguistTools_DIR` is `NOTFOUND`, no `Qt6LinguistToolsConfig.cmake` exists in the kit, and no `.qm` exists in the project. The new preflight therefore throws before configure, and every downstream build/preview/translation command is unexecutable in this environment. `CMakeLists.txt` still uses `find_package(Qt6 6.8 QUIET COMPONENTS LinguistTools)` with a warning fallback; the design says to make it hard-fail but gives no installation step or version.
* **Rationale & Recommendation:** Add an explicit prerequisite/install step (for example the Qt Maintenance Tool component `Qt 6.8.3 > Qt LinguistTools`), verify both `lrelease.exe` and the CMake package, and record the exact `.qm` artifact path. Change `QUIET` to `REQUIRED` and remove the warning fallback only after the prerequisite is satisfiable.
* **Counter-argument:** A build prerequisite is legitimate and a plan need not install Qt itself. It does need a reproducible way to satisfy the prerequisite; otherwise the first verification gate is guaranteed to fail in the only repository environment.

### 600-line gate can false-pass - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 38, 51, 364, 550-556
* **Discrepancy Description:** `git diff --name-only --diff-filter=ACMR` compares the working tree to the index, not to the recorded baseline `9a66093`. Staged changes disappear from this diff and are not returned by `git ls-files --others --exclude-standard`; a staged over-600 file can therefore produce empty output. The gate also does not use the frozen baseline and the rule’s provenance/scope is still unrecorded. Goal 7 says “UI files,” criterion 51 says “all new or modified files,” and the command scans only changed `.cpp/.h/.mm`.
* **Rationale & Recommendation:** Use `git diff --name-only 9a66093 --` plus untracked files, or union unstaged and `--cached` changes. Record the rule’s source and exact scope in `AGENTS.md` or ADR 0003, and make all three statements use the same file set. Give `tests/settings_ui_test.cpp` an explicit budget or split it into multiple allowlisted test files; the existing 723-line injector test shows this scope does not naturally fit under 600.
* **Counter-argument:** The process says “no commits or tags,” so an uncommitted worktree is the intended state. Staging is normal and not prohibited; an acceptance gate should not silently pass because a file was staged.

### Preview is not deterministic on real hardware/model state - Severity: Major

* **Reviewer Lens:** `egm-codebase-details`, `egm-implementation-readiness`
* **Affected Scope:** design lines 46, 82, 288, 532; `src/qt_ui_main.cpp:2143, 2464`; `src/model_manager.cpp:94-120`
* **Discrepancy Description:** A fixed `SettingsSnapshot` does not isolate the page builders from the machine. The input page calls `detect_gpu_capability()` and renders a GPU/CPU hint; the speech page calls `list_audio_output_devices()` and can add a “previously selected device unavailable” row. `ModelManager` also includes real `QStandardPaths` and legacy model roots in `searchRoots()`, so passing a temporary bundled directory does not fully isolate model status. Two machines can produce different screenshots, inventory labels, and manifest `elided` results.
* **Rationale & Recommendation:** Inject deterministic GPU/audio/device inventories and an isolated model manager whose search roots contain only the fixture directory. Record those fixture inputs in the manifest. Do not treat `QT_QPA_PLATFORM=offscreen` as a substitute for deterministic device/model data.
* **Counter-argument:** Repeated runs on one machine may be visually stable. The design claims repeatable screenshots and uses them as acceptance evidence; machine-dependent labels make that evidence non-portable.

### Preview CLI and manifest contract is incomplete - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 46, 288, 454-455, 527-532; `src/qt_ui_main.cpp:5442-5476, 5564, 5575`
* **Discrepancy Description:** The design names `--preview-settings`, `--preview-page`, and `--preview-manifest` but does not enumerate retained flags (`--preview`, `--preview-all`, `--preview-active`, `--bubble-style`, `--preview-geometry-test`, `--preview-geometry-log`, `--preview-image`, `--preview-text`, `--preview-translation`), legal/forbidden combinations, precedence, or whether `--preview-settings` suppresses the overlay window. Manifest fields are “at least” names without JSON types, the screenshot hash algorithm is undefined, output directories are not created, and current `QImage::save()` results are ignored. The absolute claim “preview does not read QSettings” also conflicts with the documented `ui/language` fallback when `SPEAKANYTHING_LANG` is absent.
* **Rationale & Recommendation:** Publish an exact token parser and a manifest schema with example JSON. Define output-directory creation with `mkpath`, check every save/open result, and define nonzero exit codes. State that settings preview uses the direct dialog path and does not construct `VoiceInputWindow`. Change the QSettings claim to “no writes” or require `SPEAKANYTHING_LANG` and skip `ui/language`.
* **Counter-argument:** Existing flags can be retained implicitly and the sample commands set `SPEAKANYTHING_LANG`. The document is replacing the preview contract used by the verification commands; implicit flags and an absolute no-read claim still make the contract ambiguous.

### Reset scope contradicts SettingsDraft and PageBundle - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 282, 407-425, 432-438, 445, 521; `src/qt_ui_main.cpp:1755-1780`
* **Discrepancy Description:** The footer says it resets “所有 Apply-bound 字段” and then explicitly excludes appearance, startup, compute device, hotwords, and model selection/download. `SettingsDraft` is labeled “Apply-bound fields” and includes `compute`, `translation_compute`, `startup_enabled`, `hotwords`, and `model_choices`. A whole-page `reset_draft` cannot express “reset shortcut and mute but preserve startup and compute” on the input page. Speech reset also does not say whether `device_id` and `live` are reset, and appearance reset does not say whether the button is disabled or resets-and-applies.
* **Rationale & Recommendation:** Publish a field-by-field reset matrix: field, page, draft membership, reset behavior, persistence timing. Use a reset mask or per-page allowlist, and state exact defaults. Add a test that snapshots every draft field before and after reset and asserts the exact changed subset.
* **Counter-argument:** “Apply-bound” can mean “read when Apply is clicked,” which includes startup/compute, while reset is intentionally narrower. Then line 282 must not say “所有 Apply-bound 字段,” and the page callback must carry the reset mask.

### Inventory schema, stable IDs, and baseline origin remain undefined - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 290, 364-365, 486, 516-521, 561
* **Discrepancy Description:** The inventory records page, stable control ID, type, label, and default, but no JSON schema/version, ordering, key format, dynamic-control representation, or baseline origin is defined. Most settings controls have no unique object name. The fixture is created in step 2, while `9a66093` baseline freeze records registry keys, hotword hash, and screenshots, not the inventory. Labels are intentionally changed (`恢复默认` to `恢复表单默认值`), yet line 561 forbids baseline updates when a mismatch occurs. The gate therefore either fails a correct relabel or passes without proving that no setting was lost.
* **Rationale & Recommendation:** Define a versioned schema keyed by stable setting ID or QSettings key, with type/default and an optional label. Generate and commit the baseline from `9a66093` before visual work. Compare stable IDs/types/defaults; treat labels as a separately reviewed diff or exclude them from equality. Define exit codes for `--inventory` and `--compare`.
* **Counter-argument:** A post-change fixture can record the new labels. It then proves current completeness, not preservation from the pre-refresh design, which is the stated safety goal.

### Settings defaults and field mapping are still incomplete - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** design lines 377-446, 521; `src/qt_ui_main.cpp:1792-1898, 2141, 2711`
* **Discrepancy Description:** No scalar field has a default initializer. `SettingsSnapshot{}` would produce `TextMode::Raw` (current default `Clean`), `paste_delay_ms == 0` (current default 150), `bubble_style == Capsule` (current default `Ring`), and `startup_enabled == false` regardless of platform state. There is no mapping table from the 22 constructor arguments/getters to fields. A concrete current trap: `computePolicy()` returns `compute_policy_` and never reads the `compute_` combo created at line 2141; a mechanical `read_into_draft` that copies existing getters will fail the full-field roundtrip as soon as the recognition device changes.
* **Rationale & Recommendation:** Add default member initializers or `defaultSettingsSnapshot()`/`defaultSettingsDraft()`, and a field-to-page/widget mapping. Require `read_into_draft` to read `compute_->currentData()`, not the stale member. Add per-field roundtrip tests for compute, mode, destination, trigger, startup, and hotwords.
* **Counter-argument:** A competent implementer can derive defaults from current getters. The full-field roundtrip and preview snapshot need exact values, and the existing getter is already wrong for compute.

### translation_compute duplicates translation.device - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 380, 392, 410, 422; `src/qt_ui_main.cpp:1613, 1634, 1849`
* **Discrepancy Description:** `SettingsSnapshot`/`SettingsDraft` contain both `ComputePolicy translation_compute` and `TranslationSettings translation`, whose `device` member is the same setting. The current constructor accepts both and stores `translation_compute_policy_`, but the getter and translation page use `translation.device`; the member is dead. A draft can contain two different translation-device values with no precedence.
* **Rationale & Recommendation:** Remove `translation_compute` and use `translation.device`, or define one as a synchronized input alias and test equality after every page read. Do not preserve a dead duplicate merely because the old constructor had 22 parameters.
* **Counter-argument:** Keeping the old shape minimizes call-site churn. A new “only data contract” should not preserve contradictory state.

### Apply/persist ownership is undefined - Severity: Major

* **Reviewer Lens:** `egm-style`, `egm-implementation-readiness`
* **Affected Scope:** design lines 280, 320, 374, 404-405, 440-445, 452
* **Discrepancy Description:** The dialog exposes `draft()` but has no `apply()` method or `apply_draft` callback, and `migrateSettings` is explicitly migration-only. The design says non-appearance settings take effect after Apply, but never says who writes the draft to QSettings, when `sync()` occurs, or how Cancel avoids writes. Current code reads getters and calls `saveSettings()` after `exec()`; the revision replaces the getters with `draft()` but does not name the replacement save path.
* **Rationale & Recommendation:** Add `apply(SettingsDraft)` or `std::function<void(const SettingsDraft&)> apply_draft` to `SettingsCallbacks`, and state ownership, sync point, and cancel behavior. Add a test that Apply writes exactly the draft and Cancel writes nothing.
* **Counter-argument:** “Draft as output” may mean the caller applies it and existing `saveSettings()` stays. Then state that signature and mapping; “output” alone is not a contract.

### Visual token exactness is incomplete - Severity: Major

* **Reviewer Lens:** `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 146-182, 219, 239, 367; `src/qt_ui_main.cpp:1655-1685`
* **Discrepancy Description:** The hex table is exact, but several stated colors fail the design’s own 4.5:1 text rule when used as text: `#6B7280` on `#EEF3FA` is about 4.34:1; `#16A34A` on white is 3.30:1; `#D97706` on white is 3.19:1; `#2F6BFF` on `#F5F7FB` is 4.19:1. The document does not say status/accent are fill-only. There is no font family/fallback, px-versus-point policy, Windows high-contrast policy, or focus-on-accent rule; `settings.focus` equals `settings.accent`, so a focus ring on an accent-filled button disappears. The existing amber slider `#E6A52C` is neither replaced nor given a token, contradicting “single blue accent.” Step 4 also says to write new exact values and keep screenshots unchanged, which is impossible because the values differ from current QSS.
* **Rationale & Recommendation:** Add a surface/token contrast matrix and separate text-safe status/accent tokens, or state that those colors are non-text indicators. Define font family/fallback and px/pt policy. Add a focus ring that works on accent surfaces. Decide whether the slider is blue or amber and add it to the role table. Split token extraction (old literals, screenshots unchanged) from visual retarget (new tokens, expected diff).
* **Counter-argument:** Status colors may be intended only for dots/icons, and Qt may normalize px/pt at current DPI. The document does not state those constraints; the current status/error labels are text.

### Translation runtime gate has no executable owner - Severity: Major

* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 49, 292, 484-485, 521, 541; `resources/i18n/speakanything_en.ts`; `src/models_page.cpp:92, 156, 231-232`
* **Discrepancy Description:** `tests/check_translations.cmake` is assigned the “English runtime sample,” but a CMake script cannot construct Qt widgets, install a `QTranslator`, or inspect visible labels. The C++ test is separately said to check “English sample” without a mode, page set, widget enumeration, or exit contract. The gate still lists zero `unfinished`/`vanished`, which can be green while source strings were never extracted; the current `.ts` has 227 messages, one context, zero counters, and is missing strings such as `模型目录`, `导入模型`, and `浏览器下载`.
* **Rationale & Recommendation:** Put the runtime assertion in `settings-ui-test`: load the compiled `.qm`, construct all eight pages with the fixed snapshot under `SPEAKANYTHING_LANG=en`, enumerate visible static labels, and fail on CJK outside an explicit user-hotword/model-name/dynamic-value allowlist. Keep `check_translations.cmake` for extraction counters and `.qm` presence, and name the exact artifact path and build dependency.
* **Counter-argument:** A CMake wrapper can invoke a test binary. Then name the binary, flags, page set, and exit code; the current document names none of them.

### SENSEVOICE_UI_SOURCES can duplicate main() in the test target - Severity: Major

* **Reviewer Lens:** `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 323, 479, 483-484, 507; `src/qt_ui_main.cpp:5424`; `CMakeLists.txt:373-378`
* **Discrepancy Description:** `qt_ui_main.cpp` defines `main()` and is said to retain the entry point. The design says Windows/macOS share `SENSEVOICE_UI_SOURCES` and adds `settings-ui-test`, but never defines the variable’s contents or says the test target excludes `qt_ui_main.cpp`. If included, the test gets two `main()` definitions and all overlay/tray/hotkey dependencies; if excluded, the test still needs `TextProcessor`, languages/compute/audio, and model-manager sources, which are not listed.
* **Rationale & Recommendation:** Define `SENSEVOICE_UI_SOURCES` without `qt_ui_main.cpp`, add the entry point only to `sensevoice-ui` via a separate app-source list, and specify the test target’s source/library composition. Add a configure/link check.
* **Counter-argument:** The implementer could infer the exclusion. This is exactly the build hazard the file allowlist is meant to pre-decide.

### overlay_theme extraction contradicts “only settings code removed” - Severity: Major

* **Reviewer Lens:** `egm-codebase-details`, `egm-style`
* **Affected Scope:** design lines 14, 298, 366, 467, 479, 497; `src/qt_ui_main.cpp:268-362`
* **Discrepancy Description:** The design correctly removes the `VoiceInputWindow` controller split, but still creates `src/ui/overlay_theme.h/.cpp` by migrating overlay theme presets, color mixing, readable-color logic, and the three style default colors. Line 14 says the overlay source “only deletes moved settings code,” and line 479’s removal list for `qt_ui_main.cpp` omits `overlay_theme`. The risk table itself says settings tests cannot detect overlay regressions.
* **Rationale & Recommendation:** Either keep the theme helpers in `qt_ui_main.cpp` this round, or explicitly declare `overlay_theme` a verbatim settings-adjacent move and add a behavior test for overlay colors, `themeFont()`, and all three style defaults. Update lines 14 and 479 so the scope statement matches the file table.
* **Counter-argument:** Theme helpers are not the overlay controller, so the C-B1 controller reduction still holds. The current wording “only settings code” is still false, and the moved code has no characterization test.

### Risks & Limits is not yet an actionable register - Severity: Major

* **Reviewer Lens:** `egm-style`, `egm-codebase-details`, `egm-implementation-readiness`
* **Affected Scope:** design lines 75-86, 83, 268, 344-358
* **Discrepancy Description:** The table has risk, impact, and mitigation but no trigger/failure mode, detection method, verification artifact, or owner. It does not cover the snapshot-source problem, preview hardware reads, inventory label conflict, hotword mapping, or translation-gate false pass. The 150% risk says an offscreen size assertion mitigates it, but 150% appears only in the manual walkthrough. Model deletion is still framed as “label near the button” rather than an explicit accepted risk with follow-up/owner, and the alternatives still lack a cheap in-place restyle plus narrow extraction baseline.
* **Rationale & Recommendation:** Add trigger/detection/verification/owner columns or fold the table into ADR 0003. Add an offscreen `QT_SCALE_FACTOR=1.5` run. Record model deletion as accepted risk with owner/follow-up or add confirmation/quarantine. Add the in-place baseline alternative with effort, risk, test surface, and rollback.
* **Counter-argument:** The non-goal forbids model behavior changes, so accepted risk may be correct. Then say “accepted risk” explicitly and name the owner; a label is notification, not mitigation.

## Nits

### `pages_*.cpp` is an unnecessary hand-written glob - Severity: Nit

* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** design line 311; explicit files at 470-476
* **Discrepancy Description:** The seven page files are already known and listed individually; a glob weakens the Phase 5 write allowlist and can match future unintended files.
* **Rationale & Recommendation:** Delete the glob row and keep the explicit file rows.

### `pageCount()` is always 8 only if a nullable pointer is non-null - Severity: Nit

* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** design lines 321, 442; `src/qt_ui_main.cpp:1721-1725`
* **Discrepancy Description:** The constructor takes `ModelManager*`, and the design says it must be non-null, but nothing enforces it; a null manager yields seven pages while `pageCount()` is documented as always eight.
* **Rationale & Recommendation:** Use a reference, `Q_ASSERT`, or an explicit invalid-argument path.

### PageBundle ownership is duplicated between settings_types and settings_pages - Severity: Nit

* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** design lines 306, 310, 465, 468
* **Discrepancy Description:** `settings_types` is said to carry “页面包,” while `settings_pages.h` is also said to declare `PageBundle`.
* **Rationale & Recommendation:** Assign `PageBundle` to `settings_pages.h` only.

### Open Questions says there are none while contracts remain open - Severity: Nit

* **Reviewer Lens:** `egm-style`, `egm-implementation-readiness`
* **Affected Scope:** design lines 66-73
* **Discrepancy Description:** The section says “无未决问题” and then lists four settled scope decisions; preview manifest, migration semantics, snapshot defaults, and reset/apply ownership are unresolved but unlisted.
* **Rationale & Recommendation:** Rename to “Scope Decisions” or list the remaining open items with owners.

### Manifest and migration fixtures lack executable field definitions - Severity: Nit

* **Reviewer Lens:** `egm-style`, `egm-codebase-details`
* **Affected Scope:** design lines 292, 486, 532
* **Discrepancy Description:** `horizontal_scroll` and `elided` have no types or measurement rules. The three legacy fixtures are named but no expected migrated values are given, so the test cannot assert a migration result.
* **Rationale & Recommendation:** Define manifest field types and measurement rules, and add fixture-to-expected-snapshot assertions for `copy-both`, missing `output/content`, and `vad/profile_version=0`.

## What Survives Scrutiny

* **C-B1 controller scope is genuinely reduced.** The overlay controller split is gone; `VoiceInputWindow`, tray, hotkeys, sessions, translation workers, speech queues, and injection remain in `qt_ui_main.cpp`. The residual concern is only the `overlay_theme` helper extraction, not a controller rewrite.
* **C-B2’s gate shape is materially better.** The command now targets changed `.cpp/.h/.mm` files, includes new tests, excludes the registered `qt_ui_main.cpp` grandfather, and no longer asks for a repository-wide refactor of untouched offenders.
* **C-B3’s write model is directionally correct.** Settings preview is specified to construct `InputSettingsDialog` directly, and the production path splits `loadSettingsSnapshot` from `migrateSettings`; the `preview_mode_` guard for migration and `sync()` is the right mechanism.
* **C-B4 now has real value types.** `SettingsSnapshot`, `SettingsDraft`, `SettingsCallbacks`, `SettingsLoadResult`, and `PageBundle` exist; the appearance callback signature is restored; the page count, index list, and draft output are named.
* **C-B5’s path is corrected.** `$env:QTDIR` points at the real spaced Qt installation, the `lrelease` preflight is explicit, and the language precedence (`SPEAKANYTHING_LANG > ui/language > system`) and translator-install order are stated.
* **The hotword guarantee is correctly narrowed.** “Current saver no-edit round trip plus untouched row byte slices” is achievable; arbitrary legacy byte preservation is explicitly disclaimed.
* **The visual target is now concrete.** The exact Windows token table, default 880x740/minimum 760x540 matrix, 44 px navigation/row baseline, QStyle-only icon policy, and single `SpeakAnything` translation context are strong improvements.
* **The file allowlist paths and actions are corrected.** Root `AGENTS.md` is “modify,” `docs/designs/README.md` is “modify,” the actual `speakanything-ui-refresh.md` is “modify,” and the old `src/models_page.*` files are “delete.”

--------------------------------------------------------------------------------

*Consolidated with EGM*
