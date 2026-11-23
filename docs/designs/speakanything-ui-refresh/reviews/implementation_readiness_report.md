# Goldfish Review Report — Implementation Readiness

**Target:** `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` (DetailedRange 385-529; context 1-384)
**Mode:** REVIEW — Stage 9 Implementation Readiness Test
**Date:** 2026-11-22

Three fresh-context reviewers ran in REVIEW mode against the detailed plan. Each read its persona, the Implementation Readiness Standard where supplied, the full detailed range, and enough of the feature range to understand the work. Reviewers read repository sources to verify claims but modified nothing; no fixes were applied to the target. One reviewer instance ran per lens; one launch was deferred until a concurrency slot freed and then completed normally. No reviewer failed or timed out.

## Reviewer Status

| Reviewer | Status |
| --- | --- |
| `egm-implementation-readiness` | completed |
| `egm-codebase-details` | completed |
| `egm-style` | completed |

## Prioritized Findings (Blockers -> Major -> Nits)

### 1. The configure command points at a Qt prefix that does not exist - Severity: Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 468-472 (configure at 469); `build-ui`, every downstream command
* **Discrepancy Description**: `-DCMAKE_PREFIX_PATH="E:\Qt\6.8.3\msvc2022_64"` returns `False` from `Test-Path`. The working installation is `E:\Programming environment\Qt\6.8.3\msvc2022_64`, as recorded in `build-local/CMakeCache.txt:215`. Configure fails before any target exists, so every verification command that consumes `build-ui` is unexecutable as written.
* **Rationale & Recommendation**: A plan whose first command fails cannot be executed by a fresh session. Use the real path (quoted, it contains a space) or `-DCMAKE_PREFIX_PATH="$env:QTDIR"`, and add a `Test-Path` preflight. If the prefix is intentionally machine-specific, state it as a prerequisite instead of presenting it as an executable command.

### 2. The English UI and translation gate cannot be produced in this environment - Severity: Blocker
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 452, 455, 487-488, 496; `CMakeLists.txt:561-574`; `src/qt_ui_main.cpp:565-575`
* **Discrepancy Description**: `Qt6LinguistTools_DIR` is `NOTFOUND` in `build-local/CMakeCache.txt`, no `lrelease.exe` exists in the Qt `bin` directory, and no `.qm` file exists anywhere in the project. `qt_add_translations` is therefore skipped, `installUiTranslator()` silently falls back when `:/i18n/speakanything_en.qm` is absent, and the Chinese/English screenshot commands cannot produce a real English UI. The static `.ts` check can pass while the runtime UI stays Chinese.
* **Rationale & Recommendation**: Make LinguistTools/`lrelease` a hard configure requirement or an explicit prerequisite, and verify the compiled `.qm` exists and is loaded. Add a runtime assertion that `--preview-language en` actually changes visible labels; do not rely on `.ts` contents alone.

### 3. Preview mutates the real `QSettings` before it screenshots - Severity: Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 389, 415, 491; `src/qt_ui_main.cpp:2767`, `:3230-3260`, `:5533-5558`
* **Discrepancy Description**: The plan requires preview to avoid `QSettings::setValue`/`sync()` and to leave `HKCU\Software\SenseVoice\LocalDictation` unchanged. But preview constructs a `VoiceInputWindow` before opening the settings dialog, and its constructor unconditionally calls `loadSettings()`, which always writes `vad/profile_version` and calls `sync()`, and may migrate four VAD keys. `saveAppearanceSettings()` and `saveTranslationSettings()` already guard `preview_mode_`; `loadSettings()` does not. A settings-only screenshot therefore still mutates the registry, and the stated before/after comparison would fail.
* **Rationale & Recommendation**: Split `loadSettings()` into a read-only load and a migration/write phase, gate every write on `!preview_mode_`, or point preview at a temporary INI/registry store. Specify the mechanism in the plan and make the before/after registry assertion an automated test, not a manual instruction.

### 4. `PreviewOptions` is under-specified and conflicts with the existing preview CLI - Severity: Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 402-404, 414, 487-488; `src/qt_ui_main.cpp:5442-5476`; `tools/verify_preview_geometry.ps1:32-33`
* **Discrepancy Description**: The plan names only `--preview-page`, `--preview-language`, and `--preview-manifest`, yet the commands also pass `--preview-settings`, which is absent from the list and is currently the screenshot output directory, not a settings source. No flag supplies the “forced theme” or model directory that lines 402 and 415 require. Existing flags that the verification workflow depends on — `--preview`, `--preview-all`, `--preview-active`, `--bubble-style`, `--preview-geometry-test`, `--preview-geometry-log`, `--preview-image`, `--preview-text`, `--preview-translation` — are not enumerated. Current parsing uses `raw_command_line.contains("--preview")`, so any `--preview-*` flag silently enables preview, directly contradicting the rule that `--preview-settings` without `--preview` must be an error. `--preview-page all` has no defined value domain, and no parser return/error contract is given.
* **Rationale & Recommendation**: Define the exact struct fields, every retained flag, the legal/forbidden combinations, exact-argument parsing, the canonical page identifiers including `all`, and the meaning of `--preview-settings` (or rename it). State precedence among `--preview-language`, `SPEAKANYTHING_LANG`, and `ui/language`. Also state where the type lives, since `src/ui` including `src/app/preview_options.h` inverts the intended dependency direction.

### 5. The new dialog constructor drops the appearance-changed callback - Severity: Blocker
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** line 402; existing constructor `src/qt_ui_main.cpp:1611-1632`; production call `:3538-3547`
* **Discrepancy Description**: The proposed `InputSettingsDialog(const SettingsSnapshot&, ModelManager*, const PreviewOptions&, QWidget*)` removes `std::function<void(const AppearanceSettings&)> on_appearance_changed` and `ModelChoices`. The callback is the live path that applies and persists appearance changes while the dialog is open; it is not snapshot data. `PageBundle` “local callbacks” do not define who receives the change. Without an explicit replacement, appearance edits either do nothing until Apply or require the implementer to invent a new ownership model, breaking CUJ 5 and the “取消不会回滚外观” semantics.
* **Rationale & Recommendation**: Add a named `SettingsCallbacks` struct or explicit constructor parameter for the appearance callback, define its lifetime/ownership and its preview-mode no-op, and state whether `ModelChoices` is embedded in `SettingsSnapshot` or remains a separate argument.

### 6. Three file-allowlist rows contradict the repository - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 458, 461, 462
* **Discrepancy Description**: The repository root is `SpeakAnything`, but the row says `SpeakAnything/AGENTS.md`, which resolves to a non-existent nested path; the real file is `AGENTS.md`. `docs/designs/README.md` already exists and already registers this design, so `创建` would overwrite it. `docs/designs/speakanything-ui-refresh/design_draft.md` does not exist, so `修改` cannot be applied; the actual document is `speakanything-ui-refresh.md`. This is also the only row that uses a different path base, so the allowlist does not enforce a single convention.
* **Rationale & Recommendation**: Change the rows to `AGENTS.md` / 修改, `docs/designs/README.md` / 修改, and either `design_draft.md` / 创建 or point the last row at the existing design document. Validate every table path with `Test-Path` before execution.

### 7. `SettingsSnapshot` and `SettingsDraft` have no field contract - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** lines 399-400, 429; existing constructor `src/qt_ui_main.cpp:1611-1632`; 16 getters at `:1792-1898`
* **Discrepancy Description**: Both types are described only as prose groupings. There are no field names, types, defaults, member order, or mapping from the 22 constructor arguments and 16 getters. It is unstated whether `ModelChoices` is embedded, how `QColor`/`QFont`/`std::vector<HotwordEntry>` and enums are represented, how `draft()` assembles values from eight page bundles, or how defaults are represented for reset. A fresh implementer must invent the public data contract.
* **Rationale & Recommendation**: Put the exact struct definitions in the plan, including types and defaults, plus a mapping table from each existing constructor argument/getter to a new field. Specify the signature and behavior of `SettingsDraft InputSettingsDialog::draft() const`.

### 8. `PageBundle`, page order, and cross-page coordination are undefined - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-style`
* **Affected Scope:** lines 401, 403, 420-429, 434; existing wiring `src/qt_ui_main.cpp:1708-1739`
* **Discrepancy Description**: `PageBundle` has no defined fields, callback signatures, ownership rules, or page order. Current behavior is not page-local: the translation toggle calls `updateContentHint()` and `updateSpeechControls()`, output combos call `updateContentHint()`, and `models_page_->setLanguages()` is driven by translation widgets. Independent pages that do not hold the dialog pointer cannot preserve those dependencies without a coordinator the plan never defines. The grouped sidebar also breaks the current 1:1 `currentRowChanged` -> `setCurrentIndex` mapping, because group headers occupy rows; the plan does not say whether headers are selectable, skippable, or excluded from `Home`/`End`. Stack indices used by `selectPage(int)` and `--preview-page` are unspecified, and the current stack order (输入/输出/外观/翻译/朗读/热词/模型/VAD) differs from the plan's migration order.
* **Rationale & Recommendation**: Define the `PageBundle` struct, a dialog-level coordinator with explicit callbacks (appearance, translation, output, reset, draft), sidebar-row-to-page-index mapping including group headers, and the canonical page order and index for all eight pages. State which object owns the widgets and signal connections.

### 9. Control object names and inventory schema are undefined - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 451-453, 482; current names `src/qt_ui_main.cpp:1702-1746`, `:2057-2090`, `:2264`; `src/models_page.cpp:88-98`
* **Discrepancy Description**: The tests must check “控件对象名” and “关键对象名”, but most settings controls have no `setObjectName()`. Only the sidebar, stack, page wrapper, cards, card titles/hints, row labels, mode segments, and a few model-page elements are named; these cannot distinguish dozens of controls. A baseline captured now would contain empty or repeated names, and a post-refactor comparison would be meaningless. No JSON schema, version, ordering, or key format is given for `settings_inventory.json` or `manifest.json`.
* **Rationale & Recommendation**: Add an object-name table covering every page and control (page, type, label, default, unique name), assign the names in `pages_*.cpp`, and define both JSON schemas including ordering and dynamic-control representation before capturing the baseline.

### 10. Inventory provenance and determinism are unspecified - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 389, 451-453, 477-482; `src/qt_ui_main.cpp:199-203`, `:394-396`, `:2143-2154`, `:2464-2468`; `src/model_manager.cpp:94-105`
* **Discrepancy Description**: Section 7 says the inventory records “默认值和当前值”, but the fixture row lists only defaults, and current values depend on `QSettings`, GPU detection, audio devices, installed models, startup state, and UI language. The plan never says to generate and commit the fixture from `9a66093` before the split, so the “prove no setting was lost” baseline has no defined origin. Offscreen mode removes none of those dependencies. The two commands are also disconnected: `--inventory` writes `build-ui\settings-inventory.json`; `--compare` names only `tests\fixtures\settings_inventory.json`, so it may regenerate the inventory or compare the fixture to itself.
* **Rationale & Recommendation**: Define the JSON schema and exact pass/fail exit behavior, add a baseline step that generates and commits the fixture from `9a66093`, and exclude or normalize platform/device/locale-dependent values. Split invariant structure (page, name, type, label, default) from runtime values; compare runtime values only within one process run. Make the CLI explicit (`--inventory <out>` then `--compare <generated> <fixture>`).
* **Counter-argument**: A single cross-platform fixture is reasonable if the comparison is structural only. If that is the intent, say so and remove “当前值” from the comparison rather than leaving the choice to the implementer.

### 11. Test registration, working directory, and runtime environment are unspecified - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 451-452, 470-482, 496; `CMakeLists.txt:451`, `:587-596`, `:679-692`
* **Discrepancy Description**: The plan creates `tests/settings_ui_test.cpp` and `tests/check_translations.cmake` but never specifies `add_test` registration, arguments, working directory, or environment. `ctest -R translations-check` cannot run unless a test with that exact name exists. The `settings-ui-test` commands omit `QT_QPA_PLATFORM=offscreen` even though the test is specified as offscreen. Relative fixture paths (`tests\fixtures\...`) resolve under the build directory in CTest; the existing suite solves this with `SENSEVOICE_TEST_SOURCE_DIR`. The dialog only adds the model page when `ModelManager != nullptr`, so without a non-null deterministic manager `pageCount()` is 7, not 8.
* **Rationale & Recommendation**: Add exact `add_test` entries with arguments, `WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}` or a compiled source-dir definition, and `set_tests_properties(... ENVIRONMENT "QT_QPA_PLATFORM=offscreen")`. Pass a temporary model directory, construct a real `ModelManager`, and assert eight pages. Define the exact translation string scope.

### 12. The hotword round-trip test cannot prove what it claims - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 427, 454, 479-482, 519-520; `src/text_processor.cpp:594-623`, `:635-651`
* **Discrepancy Description**: The UI column order is enabled=0, phrase=1, aliases=2, boost=3, hits=4, keep=5; the persistence order is phrase=0, aliases=1, enabled=2, hits=3, boost=4, keep=5. The plan says to replace numeric indices with named constants but never lists them, so swapping boost/hits or enabled/phrase is easy. Separately, `save_hotwords()` truncates and rewrites every row, always emits a fixed header and six LF-separated fields with default float formatting. A whole-file SHA-256 cannot be byte-identical for legacy data: four- and five-field rows expand to six, `3.0` becomes `3`, CRLF becomes LF, and comments/headers are replaced. The existing `build-final/hotwords.tsv` header is CRLF. Editing one row also rewrites all rows, so a canonical round-trip does not prove “未编辑行的字节内容不变”.
* **Rationale & Recommendation**: Specify the named constants and the exact UI-to-persistence mapping. Define a canonical fixture whose bytes are the exact output of `save_hotwords()`, and add a test that edits one row and asserts the untouched rows' bytes are unchanged. Add semantic tests for four-field, five-field, CRLF, and `3.0` inputs. Narrow the completion criterion if arbitrary formatting preservation is out of scope.
* **Counter-argument**: Preserving arbitrary source formatting would require changing `save_hotwords()`, which the non-goals forbid. If byte preservation of non-canonical input is required, it is a separate behavior change, not a UI refresh.

### 13. `SENSEVOICE_UI_SOURCES` must exclude the app entry point - Severity: Major
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 282, 450, 470; `CMakeLists.txt:328-331`, `:373-377`
* **Discrepancy Description**: `src/app/main.cpp` defines `main()`. If `settings-ui-test` compiles the same shared source list to obtain the UI code, the link fails with two `main()` definitions. The plan groups the entry point with UI sources and does not separate them.
* **Rationale & Recommendation**: Define `SENSEVOICE_UI_SOURCES` without `src/app/main.cpp`, and add the entry point only to the `sensevoice-ui` target (or a separate `SENSEVOICE_UI_APP_SOURCES`). State this for both the Windows and macOS targets.

### 14. `SENSEVOICE_NATIVE_INPUT` is a file-local macro - Severity: Major
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 391, 437-446; `src/qt_ui_main.cpp:18`, 16 use sites
* **Discrepancy Description**: `#define SENSEVOICE_NATIVE_INPUT 1` lives at the top of `qt_ui_main.cpp` and is used at 16 sites in hotkey, session, injection, and overlay code. Moving those functions into separate translation units compiles out the native-input branches unless the definition moves with them.
* **Rationale & Recommendation**: Move the macro to `target_compile_definitions(sensevoice-ui PRIVATE ...)` or a shared `ui_config.h`, and add a compile-time or test assertion that the native path is enabled on Windows and macOS.

### 15. Collapsing identity constants can move macOS user data - Severity: Major
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 296-297, 411; `src/qt_ui_main.cpp:5431-5432`, `:601`, `:559`, `:3123`
* **Discrepancy Description**: Current identity is not one org/app pair: `applicationName()` is `SenseVoice 语音输入`, `organizationName()` is `SenseVoice`, and `QSettings` uses `SenseVoice`/`LocalDictation`. On macOS, `hotwordsPath()` uses `QStandardPaths::AppDataLocation`, whose path derives from the application/organization name. Folding these into a single “org/app” constant can silently move hotwords or change the registry path.
* **Rationale & Recommendation**: Define separate constants for window title, application name, organization name, QSettings organization, QSettings application, mutex name, and tray text. Preserve the current values exactly and add a macOS path/migration test.

### 16. The geometry gate asserts less than the plan claims - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 497, 500; `tools/verify_preview_geometry.ps1:32-33`, `:76-77`, `:103-111`
* **Discrepancy Description**: The script only asserts that the center range and center deltas are `<= 1.1` px. It computes `Last15DistinctBottom` and distinct widths but never asserts a bottom-drift or width limit. It also hardcodes `--bubble-style ring`, and the single verification command does not loop over capsule/panel/ring. “保持 1.1 px 中心和底边漂移阈值” therefore names a check that does not exist and a comparator (`<=`, `==`, range) that is not defined.
* **Rationale & Recommendation**: Write the criterion as “中心漂移 ≤ 1.1 px；底边漂移 ≤ 1.1 px”, add the missing assertions, parameterize the bubble style, and run the script once per style or add an internal loop.

### 17. Screenshot baselines have no capture or comparison protocol - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 392, 491, 500, 518; `src/qt_ui_main.cpp:5518-5532`, `:5569-5576`
* **Discrepancy Description**: The plan requires screenshots to be “一致” or “可重复” but never defines the artifact location, capture command, fixed environment, page/language matrix, filenames, comparison metric, or tolerance. “中英文截图” could mean one image per language or eight pages in both. “截图与拆分前一致” could mean pixel-identical or visually similar. No command produces the three overlay-style screenshots: `--preview-all` shows three windows but never renders or saves them, `--preview-image` saves only one, and `preview_harness.cpp` is scoped to settings pages. The current code ignores `QImage::save()`'s return value, so “截图失败必须返回非零” is not guaranteed.
* **Rationale & Recommendation**: Define a baseline directory, a scripted capture with fixed scale/theme/language, all eight pages in `zh` and `en` at `880x740` and `760x540`, deterministic filenames, and an explicit comparison metric (PNG SHA-256 or documented pixel tolerance). Add an overlay capture mode for capsule/panel/ring with fixed names. Check every `QImage::save()` result and return non-zero on failure.
* **Counter-argument**: Exact pixel equality is brittle across Qt and OS versions. If cross-machine comparison is required, pin the Qt version and use a small perceptual threshold; either way the plan must choose.

### 18. Minimum window width is stated three incompatible ways - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`, `egm-style`
* **Affected Scope:** lines 393, 504; conflicting context at lines 43 and 177; current code `src/qt_ui_main.cpp:1640`
* **Discrepancy Description**: The completion criterion says `680x540`; the shell step, design section, and manual walkthrough say `760x540`; the automated test says only “最小尺寸”. Current code is `setMinimumSize(680, 540)`. At 680 px with a 184 px sidebar the content area is too narrow for the six-column hotword table and model action rows, so a verifier following line 43 would test the wrong size. The `150%` condition appears only in the manual step and is not in the completion criteria, and the scaling mechanism is unnamed.
* **Rationale & Recommendation**: State one canonical matrix: default `880x740`, minimum `760x540`, tested at 100% and 150% with a named mechanism (`QT_SCALE_FACTOR=1.5` or Windows display scaling). Update line 43, assert `minimumSize() == QSize(760, 540)` in the test, and make the manual walkthrough match.

### 19. Registry comparison and manifest failure handling have no executable form - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-style`
* **Affected Scope:** lines 415, 487-491
* **Discrepancy Description**: “预览前后导出并比较 `HKCU\Software\SenseVoice\LocalDictation`” gives no export command, normalization rule, or compare script, and no expected exit code. The JSON manifest has no schema or example, no output-directory creation rule, and no specified behavior when `mkpath` or the write fails.
* **Rationale & Recommendation**: Provide exact `reg export` (before/after) and comparison commands, a manifest schema/example, explicit `QDir().mkpath` behavior, fixed screenshot names, and required exit codes for every failure branch.

### 20. Cancel-time restoration of model state is undefined - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** lines 401, 403, 429; `src/qt_ui_main.cpp:3465-3481`, `:3608-3611`
* **Discrepancy Description**: The current dialog applies model locations to the manager immediately and explicitly restores saved model choices on Cancel, because imports and directory changes are immediate. The new snapshot/draft/page-bundle flow never states how model choices and manager locations are restored on reject. A mechanical refactor can leave an imported directory active after Cancel, violating “点击取消放弃未应用修改”.
* **Rationale & Recommendation**: Define an explicit reject/restore path (for example `restoreModelLocations(const SettingsSnapshot&)` or an `on_rejected` callback) and enumerate which model operations are immediate and irreversible versus draft-only.

### 21. The translator is installed before preview arguments are parsed - Severity: Major
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 404, 487-488; `src/qt_ui_main.cpp:5434-5436`, `:556-575`
* **Discrepancy Description**: `installUiTranslator(application)` runs before the argument list is read, and `uiLanguageSetting()` reads only `SPEAKANYTHING_LANG` or `QSettings`. A `--preview-language zh|en` flag parsed later cannot prevent the English translator from being installed, nor force Chinese on a non-Chinese locale.
* **Rationale & Recommendation**: Parse `PreviewOptions` immediately after `QApplication` construction, then install the translator with the resolved language. Define precedence among `--preview-language`, `SPEAKANYTHING_LANG`, and `ui/language`.

### 22. The token model is described three incompatible ways - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 392, 418, 459; context lines 137, 141, 159, 306
* **Discrepancy Description**: Section 2 defines three layers (base scale, semantic roles, surface adapters). Section 10 says the ADR records “两层 token”. The target table says `design_tokens.h/.cpp` defines “Windows/macOS QSS 适配器”, while sections 2 and 9 say macOS uses native controls and `QPalette`, not QSS. Step 4's “先让 token 值等于现有字面值” has no single referent, because each role currently maps to several literals; an implementer cannot tell which value seeds `shell.background` or `surface.card`.
* **Rationale & Recommendation**: Standardize on “三层设计 token：基础尺度、语义角色、表面适配器”; make the macOS adapter map roles to `QPalette` and explicitly not generate QSS; make ADR 0003 record the three layers. Add a seed table mapping each semantic role to its current literal(s), and state that step 4 only extracts tokens without changing any applied widget style.

### 23. “触点页面” is undefined, and the static check cannot prove runtime English - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 452, 496, 500; context line 49
* **Discrepancy Description**: The scope of the English translation check is unknown, and a `.ts` inspection can only prove that entries exist and are neither `unfinished` nor `vanished`. A hardcoded Chinese string or a translator that fails to load would pass the `.ts` check and still violate “触点页面在英文界面中不出现中文回退”. “英文目录” at line 395 is also ambiguous between the translation catalog and an English documentation index.
* **Rationale & Recommendation**: Define the exact page/string set. Keep the `.ts` check but add a runtime assertion: load the compiled `.qm`, construct each page with a fixed snapshot, collect visible widget texts, and assert no CJK outside an explicit allowlist for user hotwords and model names. Change line 395 to “英文翻译目录 `resources/i18n/speakanything_en.ts`”.
* **Counter-argument**: A blanket CJK scan false-positives on user data and model names. That requires a scoped allowlist, not reliance on the static check alone.

### 24. “每页单独验收” names no gate - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 394, 504; context line 157
* **Discrepancy Description**: The step names no checklist, command, artifact, or exit condition, so an implementer cannot tell whether a page passes after the inventory compare, the screenshots, the keyboard walkthrough, or all three. “焦点环始终可见” and “无文字裁切” have no measurement, even though the design already defines a 2 px focus ring with ≥3:1 contrast. “150% 缩放” does not identify the scaling mechanism.
* **Rationale & Recommendation**: Define the per-page gate: inventory compare passes; `zh`/`en` screenshots at both sizes; keyboard focus walkthrough; `QScrollArea` horizontal maximum 0; no elided page titles, row labels, or buttons; line count ≤ 600. Require focus contrast ≥ 3:1. Automate scroll and elision checks in `settings-ui-test` and record pass/fail per page.
* **Counter-argument**: Automated text-clipping checks can be brittle with platform fonts. If inspection stays manual, name the exact labels and viewports instead of “check everything”.

### 25. The appearance page's reset behavior contradicts “即时生效” - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** lines 393, 429; context lines 248-252; CUJ 5 at line 101
* **Discrepancy Description**: Section 6 says appearance changes apply and save immediately, but the footer reset “只重置当前表单字段” while “不改外观即时值”. On the appearance page those two rules conflict: resetting the appearance controls either applies the defaults immediately (changing the appearance, violating the second clause) or leaves the controls out of sync with the applied appearance. The detailed shell and footer steps never resolve this, and the Cancel exception is not carried into per-page migration.
* **Rationale & Recommendation**: Define reset per page. On the appearance page, either disable “恢复表单默认值” or make it restore and immediately apply the defaults, and state that `appearance/*` is written. “取消” must close without rolling back already-applied appearance. Add a test for the appearance reset and cancel path.
* **Counter-argument**: If reset must never touch immediate appearance values, the button must be disabled or scoped to non-applied fields. That is a product decision and should be stated.

### 26. The macOS release gate is uncalibrated - Severity: Major
* **Reviewer Lens:** `egm-style`
* **Affected Scope:** line 522; context lines 43-49, 295
* **Discrepancy Description**: “要求 macOS 构建或源码评审作为发布前门禁” treats a build and a source review as equivalent, but a source review cannot verify native palette behavior, focus, layout, or runtime translation. “记录未验证项” names no artifact, owner, or location, and the completion criteria carry no platform qualifier, so a reader can conclude macOS criteria passed when they were never executed.
* **Rationale & Recommendation**: State the exact gate: a successful macOS build, `settings-ui-test` pass, and a manual smoke of all eight pages and focus paths at `880x740` and `760x540`; otherwise mark macOS unsupported for this release. Record gate, owner, and evidence location in ADR 0003, and add a platform qualifier to the completion criteria.
* **Counter-argument**: A full macOS build may be unavailable here; label the fallback “source-review-only, not runtime-verified” rather than equivalent to a build gate.

### 27. The 600-line gate checks too little and has no per-file budgets - Severity: Major
* **Reviewer Lens:** `egm-implementation-readiness`, `egm-codebase-details`
* **Affected Scope:** lines 276-282, 509-514; context lines 38, 51
* **Discrepancy Description**: The command scans only `src` for `*.cpp` and `*.h`, so it misses `src/macos/*.mm`, `tests/settings_ui_test.cpp`, and any modified file outside `src`, even though the completion criterion covers all new or modified code files. The estimate table also gives no per-file numbers for `src/ui/overlay/*` (“按职责拆分”), `preview_harness.cpp`, `app_paths.*`, `overlay_helpers.*`, or `settings_types.cpp`, so it does not demonstrate that every resulting file fits under 600 lines. The overlay group is the largest risk: `VoiceInputWindow` alone is about 2,684 lines.
* **Rationale & Recommendation**: Add per-file estimates or hard caps for every new file, especially the overlay group, and either broaden the check to the changed-file set (`git diff --name-only`, plus `.mm` and `tests`) or explicitly limit the rule and document the exclusion.

### 28. `src/app/main.cpp` is not distinguished from the existing CLI entry point - Severity: Nit
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 282, 413; `CMakeLists.txt:328-331`; `src/main.cpp`
* **Discrepancy Description**: The repository already has `src/main.cpp`, the entry point for `sensevoice-stream`. The UI `main()` is at the bottom of `src/qt_ui_main.cpp`. A fresh implementer could overwrite the CLI entry point or wire the wrong source into `sensevoice-ui`.
* **Rationale & Recommendation**: State explicitly that the new file receives `main()` from `src/qt_ui_main.cpp:5424-5587`, and that `src/main.cpp` remains the CLI entry point and is out of scope.

### 29. `PageBundle` callbacks implicitly reintroduce the dialog pointer - Severity: Nit
* **Reviewer Lens:** `egm-codebase-details`
* **Affected Scope:** lines 401, 420, 424, 429
* **Discrepancy Description**: The plan says pages do not hold an `InputSettingsDialog` pointer, but current page builders are member functions that capture `this`. If the new callbacks capture the dialog, the bundle indirectly holds that pointer, contradicting the stated constraint.
* **Rationale & Recommendation**: Define callback capture rules: capture only the controls needed, use a `QObject`/`QPointer` context, or revise the claim to allow a documented non-owning pointer.

### 30. “八个页面构建器” does not match seven `pages_*.cpp` files - Severity: Nit
* **Reviewer Lens:** `egm-implementation-readiness`
* **Affected Scope:** lines 420-428, 434
* **Discrepancy Description**: `settings_pages.h` is said to declare eight page builders, but the table creates seven `pages_*.cpp` files. The eighth page is `ModelsPage`, a `QWidget` class with a different interface, listed separately.
* **Rationale & Recommendation**: Say “七个页面构建器加 `ModelsPage`”, or add a `pages_model.cpp` builder and its signature.

## What Survives Scrutiny

The reviewers verified these claims against the repository and found them accurate:

* `src/qt_ui_main.cpp` is 5,587 lines; `src/models_page.cpp` is 395 lines and `models_page.h` is 69. The split premise is real, and the proposed 600-line budget per file is the correct scale of work.
* The current constructor really has 22 parameters, and the dialog really exposes 16 getters. Replacing both with `SettingsSnapshot`/`SettingsDraft` is justified and reduces a named source of drift.
* The current minimum size is `680x540`, the sidebar is 168 px and `NoFocus`, and preview uses `findChild("settingsStack")` — so the three shell changes (184 px, focusable grouped sidebar, public page interface) target real defects.
* `QSettings` is `SenseVoice`/`LocalDictation`, and the “do not change key names/types/migration” constraint is the right compatibility boundary.
* The hotword UI column order and persistence order are genuinely different, so separating display order from persistence is necessary rather than cosmetic.
* There are exactly nine existing `add_test` registrations, and `resources/i18n/speakanything_en.ts` has a single `SpeakAnything` context with zero `unfinished` and zero `vanished` entries.
* `tools/verify_preview_geometry.ps1` exists and does enforce a 1.1 px center threshold, and `qt_add_translations` is already wired for `sensevoice-ui`.
* The phased order (baseline → verification layer → mechanical split → tokens → shell → page migration → docs) is sound and each phase has a coherent rollback story. The failure-handling list, where it is specific (registry, inventory, hotword order, translation), is the right shape.

This review is scoped to the detailed plan. It does not re-litigate the scope decisions already raised in the Stage 5-8 reports; those remain open independently.

## Verdict

**Not implementation-ready.**

Five blockers prevent a fresh implementer from executing the plan as written: the build command fails on the stated Qt prefix; the English UI and translation gate cannot be produced in this environment; preview mutates the real registry; `PreviewOptions` neither covers nor preserves the existing CLI contract; and the new dialog constructor removes the appearance-apply callback without a replacement. The Major findings are concentrated in the same three areas the plan promises to make deterministic — the settings inventory, the hotword round-trip, and the screenshot/geometry gates — plus the undefined `SettingsSnapshot`/`PageBundle` contracts and the mismatched file allowlist.

The plan does not need discarding. It needs one revision pass that (a) makes the verification commands runnable and its baselines reproducible, (b) writes the two data contracts and the page/coordinator wiring as code-level definitions, and (c) corrects the allowlist and the conflicting acceptance numbers. Once those are closed, the remaining findings are local wording and test-detail fixes.
