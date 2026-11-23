# SpeakAnything UI Refresh: Settings Shell, Design Tokens, and Overlay

* **Author:** wangbaoyi
* **Date:** 2026-11-22
* **Status:** Brainstorm (pre-draft), prepared for `/egm-draft`
* **Mode:** BRAINSTORM
* **Project root:** `E:\myworkwindows\语音识别项目\SpeakAnything`
* **Baseline:** `main` at `9a66093` (`v0.2.0-3-g9a66093`), clean worktree, remote `origin`

## Reviewer Status

| Lens | Persona file | Status | Main contribution |
| --- | --- | --- | --- |
| Design critic | `agents/design_critic.md` | completed | Four rendering substrates, layered tokens, dialog ownership |
| Codebase details | `agents/codebase_details.md` | completed | Surface/settings inventory, hidden couplings, preview defects |
| Zoom out | `agents/zoom_out.md` | completed | Minimum viable restyle; existing card vocabulary |
| Comprehension | `agents/comprehension.md` | completed | Per-page semantics, commit-model confusion, stale i18n |
| Implementation readiness | `agents/implementation_readiness.md` | completed | Staged rollout, persistence contracts, build matrix |
| Structural | `agents/structural.md` | completed | Decomposition map, translation context, CMake duplication |
| Inventive catalyst | `agents/inventive_catalyst.md` | completed | Contradiction analysis, commit-policy and row variants |

All seven reviewers returned reports. None timed out or failed. Reviews were read-only, source-first, and performed without compiling or launching the application.

## Overview

SpeakAnything is a tray-resident, offline voice-input utility: a frameless floating overlay shows recognition state, and a modal settings dialog holds eight pages of configuration. The request is to make that settings experience look modern and scan more clearly, using a referenced remote-desktop application as the visual model: light blue-gray background, white cards with thin borders, grouped left navigation, large page titles, and one strong accent.

The proposed concept goes further than a restyle. It rebuilds the UI shell, introduces one design-token system across the settings dialog, models page, hotword table, and floating overlay, and splits the 5,587-line `qt_ui_main.cpp` into focused files to satisfy the project's 600-line-per-file rule. It also promises to preserve every setting and behavior while keeping Windows and macOS support, keyboard access, visible focus, long Chinese/English labels, and high-DPI behavior.

The reviewers found the visual direction workable but the concept internally over-committed. Three claims do not survive contact with the code: there is no application shell to rebuild, "one token system" cannot be a single palette across four rendering substrates plus user-owned overlay themes, and the four-file split cannot satisfy the 600-line rule because `VoiceInputWindow` alone is about 2,684 lines of controller behavior. Two further claims describe properties that do not exist today: keyboard navigation and visible focus.

The defensible version is narrower and staged: treat the settings dialog as the shell, build two token layers with per-surface adapters, keep dense controls as explicit row variants, preserve the three commit semantics instead of hiding them behind one Apply/Cancel promise, and ship a pixel-identical mechanical split before any visual change. Verification must become deterministic before it can prove that no setting was lost.

## Verified Baseline

These facts come from source reading at `9a66093`. They are not recommendations.

* There is no `QMainWindow`. The primary surface is a frameless, always-on-top, no-focus `Qt::Tool` overlay (`src/qt_ui_main.cpp:2737-2752`). Settings is a modal `QDialog` opened from the tray menu (`src/qt_ui_main.cpp:3059-3148`, `3532-3548`).
* `qt_ui_main.cpp` is 5,587 lines with CRLF endings and no BOM. `models_page.cpp` is 395 lines with LF endings and no BOM. MSVC is built with `/utf-8` (`CMakeLists.txt:390`).
* `InputSettingsDialog` spans `1609-2735` (about 1,127 lines). `VoiceInputWindow` spans `2737-5420` (about 2,684 lines). Overlay widgets span `625-1608` (about 984 lines).
* The dialog is fixed at `880x740` with a `680x540` minimum (`1639-1640`). The sidebar is `168 px`, `Qt::NoFocus`, and its QSS sets `outline: 0` (`1703-1704`, `1646`, `1658`).
* The eight pages are input, output, appearance, translation, speech, hotwords, models, and VAD (`1712-1726`). Models is added only when `model_manager_` is non-null (`1721-1725`); production always passes one (`2763`, `3546`).
* The reference vocabulary already exists: background `#F7F8FA`, white `QFrame#card` with `#E3E6EB` border and 8 px radius, `#7A808A` helper text (`1655-1685`).
* There is no single accent. Blue focus `#356AE6` (`1670`), amber slider `#E6A52C` (`1673`), near-black primary `#202329` (`1677`), red error `#C43B3B` (`2187`), amber warnings `#C77A14` (`2309`, `2453`), teal overlay `#45B89E` / `#63D9B5` (`1324`, `1305`), green menu selection `#168E68` (`5512`).
* Overlay appearance is user data: five presets, custom colors, font family/size, linger time, pinned mode (`268-283`, `1913-2015`). Changes apply and save immediately; Cancel does not revert them (`1982`, `2031-2034`, `3541-3545`).
* The overlay has three painted styles, but normal launch constructs `BubbleStyle::Ring` (`5454`, `2739`). Capsule and Panel are reachable through `--bubble-style` or `--preview-all` (`5445-5483`, `5518-5532`).
* The English catalogue is stale. It has 227 messages, 0 marked unfinished, 0 marked vanished, and one `SpeakAnything` context, but current strings such as `模型目录`, `导入...`, `浏览器下载`, `音频`, `录音时静音系统播放` are absent, and at least one removed string remains at `resources/i18n/speakanything_en.ts:210`.
* The preview is not offscreen. `--preview-settings` calls `dialog.show()` then `dialog.grab()` at 880x740 (`5559-5565`), switches only the stack index (`5561-5563`), ignores `QImage::save()` failures, and requires `--preview` (`5550`).
* `--preview` constructs a real `VoiceInputWindow`, whose constructor calls `loadSettings()`. That function writes `vad/profile_version` and calls `sync()` (`2767`, `3243-3260`). Preview can therefore migrate the developer's registry.
* Settings live in `HKCU\Software\SenseVoice\LocalDictation` through an explicit `QSettings("SenseVoice", "LocalDictation")` (`2741`, `559`). There are 35 literal `settings_` keys, plus `ui/language`, `compute/device`, `translation/device`, and dynamic model groups (`models/recognizer/<lang>`, `models/voice/<lang>`, `models/custom_paths/<id>`).
* Hotwords persist to `hotwords.tsv` in the order phrase, aliases, enabled, hits, boost, keep (`src/text_processor.cpp:641-651`). The UI column order is enabled, phrase, aliases, boost, hits, keep (`2504-2521`).
* Model deletion is immediate and irreversible on Cancel: `ModelsPage` calls `manager_->remove()` (`src/models_page.cpp:274`), which deletes downloaded files (`src/model_manager.cpp:238-267`). Cancel restores only model locations/paths (`3608-3612`).
* There is no UI test target. The nine `add_test` entries cover recognition, text, audio, compute, translation, injection, and model management (`CMakeLists.txt:579-693`). There is no AUTOMOC and no `Q_OBJECT` in the UI sources.
* The Windows and macOS UI source lists are duplicated (`CMakeLists.txt:373-384`, `476-490`). `qt_add_translations` scans `sensevoice-ui` (`566-571`).
* `resources/sensevoice.qrc` embeds only `sensevoice.ico`. `build-final/imageformats` contains `qgif`, `qico`, and `qjpeg`, but no `qsvg`; `Qt6::Svg` is not linked.
* `SpeakAnything/AGENTS.md` is 9 bytes containing the text `CLAUDE.md`, and no `CLAUDE.md` exists. No root `AGENTS.md` exists on disk; the project rules used here were supplied in the task text.
* `GLOSSARY.md` exists and is linked from both READMEs. `docs/adr/` has two ADRs. There is no docs index or `docs/designs/` convention yet.
* The installer mutex is `Local\SenseVoiceLocalDictation` (`packaging/windows/SenseVoice.iss:34`); uninstall removes `{app}` recursively (`:86`), which includes `hotwords.tsv` and models imported into the app directory.

### Concept Claims Checked Against Code

| Claim | Verdict | Evidence |
| --- | --- | --- |
| Main UI is `qt_ui_main.cpp`, 5,587 lines | TRUE | Line count; `VoiceInputWindow` alone is ~2,684 lines |
| 168 px sidebar plus 8 pages | TRUE | 168 px; 8 pages only when a model manager exists, always true in production |
| Sidebar has `Qt::NoFocus` | TRUE | `1704` |
| Model page uses four same-weight cards | PARTIAL | One directory card plus three model cards; "same weight" is visual judgement |
| Overlay has three styles and a separate language | TRUE | Three QSS tables and custom paint; only Ring is used in normal launch |
| Windows light, macOS palette/native | TRUE | `1655-1685` versus `1641-1653` |
| Preview produces offscreen screenshots | FALSE | On-screen `show()` + `grab()`; not offscreen |
| One Apply/Cancel model exists today | FALSE | Three commit models: live, live-with-rollback, deferred |
| Keyboard access and visible focus exist | FALSE | NoFocus sidebar, `outline: 0`, no focus rules for buttons/tables/sliders |
| Four-file split satisfies 600 lines | FALSE | Dialog ~1,127 and window ~2,684 lines remain |
| English translations are current | FALSE | Catalogue misses current Models/Input strings and keeps a removed string |
| Tree is under git | TRUE | `main` at `9a66093`, tag `v0.2.0`; the parent directory is not a repo |

## Analysis

### 1. "Shell" means the settings dialog, not an application window

The product is a tray icon, a floating overlay, and a modal dialog. The reference's grouped sidebar belongs to a full application window. Adding one would introduce window lifecycle, focus, tray behavior, and single-instance changes, which the concept lists as out of scope. If "shell" means a new main window, this is a new product surface, not a visual refresh. The brief should state that the shell is the settings dialog and that no main window is introduced unless the owner explicitly expands scope.

### 2. One token system must be two layers plus adapters

There are four rendering paths: Windows QSS, the macOS palette/native branch, the overlay's QSS builders, and direct `QPainter` colors. A single flat palette cannot serve all four. Worse, the overlay palette is persisted user data, not a design constant; a process-global "current theme" would break `--preview-all`, which creates three overlay styles in one process, and would override high-contrast/custom choices.

The workable structure is three layers: primitive scales (spacing, radius, type, neutral ramp), semantic roles (surface, text, muted, border, focus, selection, danger, warning, success, overlay identity), and per-surface adapters (shell QSS generator, macOS palette mapping, overlay theme resolver). One authored source, many adapters. User appearance settings always win for the overlay; the shell does not follow them.

### 3. "One blue accent" conflicts with semantics and identity

Blue is currently a focus color, not the product accent. The primary button is near-black, sliders are amber, errors red, warnings amber, VAD red/amber/green, the overlay teal, and the menu selection green. Collapsing these into one blue would erase state meaning and change the overlay's shipped identity. The defensible reading is one chrome/interaction accent (blue), separate semantic roles, and an explicit overlay identity token that user themes may replace. Whether the overlay default becomes blue is an owner decision, not an implementation detail.

### 4. The reference row grammar needs explicit variants

"Icon, title, helper text, right-aligned toggle or action" describes booleans. The dialog also contains segmented buttons, combo boxes, key-sequence editors, sliders with value labels, color pickers, font pickers, file-path actions, a six-column editable table, progress bars, and model rows with a combo plus four conditional buttons. Forcing these into one right-aligned toggle either loses controls or creates six undocumented layouts. Define a row shell with variants: toggle, select, slider/numeric, action cluster, table workbench, and custom. Reference fidelity comes from spacing, typography, borders, and hierarchy, not from one silhouette.

### 5. "Preserve every behavior" collides with "consistent action placement"

The dialog has three commit models. Appearance applies and saves immediately and is explicitly not reverted by Cancel. Model directory/import/delete changes mutate immediately; Cancel restores only locations, not deleted files. Everything else is read after `Accepted`. A uniform footer teaches "nothing happens until Apply", which is false today and becomes more dangerous as the UI looks more coherent. The redesign must either preserve and disclose the mixed semantics per section or make unifying them an approved behavior change.

The reset action is also partial: it resets VAD, shortcut, mode, destination/content/TTL, trigger, mute, paste delay, translation, and speech, but not appearance, startup, compute device, hotwords, or model choices (`1755-1780`). A footer-level "Restore defaults" will be read as global.

### 6. The 600-line split is a controller refactor

Extracting theme, dialog, models page, and overlay leaves the window controller well over budget. Reaching 600 lines requires splitting recording-session, translation, speech, hotkey, injection, tray, and model-loading logic. That contradicts "UI-only refactoring" and is not covered by any existing test. Five non-UI files already exceed 600 lines (`main.cpp` 686, `sensevoice_engine.cpp` 1,109, `stream_recognizer.cpp` 917, `text_processor.cpp` 820, `windows_text_injector.cpp` 772), so the rule is not currently enforced repo-wide either. The plan needs an explicit scope decision and, if needed, a documented waiver for the mechanical phase.

### 7. macOS fidelity cannot be the same as Windows fidelity

The macOS branch deliberately keeps native controls, palette roles, and dark mode; the Windows branch hard-codes light colors. Emitting Windows hex QSS on macOS would remove that behavior. Shared hierarchy, spacing, typography, and focus rules are compatible; identical surface colors are not. The project needs an explicit platform policy, not an `#ifdef` decided during implementation.

### 8. Verification is blind, and the supplied baseline is stale

The existing preview is a manual screenshot loop: on-screen, fixed-size, one `processEvents()`, no assertions, no failure on missing stack or failed save. It changes only the stack index, so every screenshot after the first shows the first navigation item as selected. It depends on the developer's saved appearance, GPU, audio devices, and models. The bundled baseline PNGs predate HEAD and `settings-0.png` lacks the current `计算设备` card (`2138-2157`). No UI test exists. Before a restyle, "preserve every setting" is an unverifiable claim.

### 9. Internationalization is already out of sync

The English catalogue is missing strings that the current UI displays, so parts of the English UI already fall back to Chinese. It also retains a string removed from the code. Splitting files is safe only if every new translation unit keeps the `SpeakAnything` context; introducing class-specific `tr()` contexts would silently orphan all existing translations. New helper text, page subtitles, and row labels need translations before the visual work is accepted.

### 10. Inventive resolution

The core contradictions are resolvable without accepting a false compromise:

* **Segmentation** separates chrome accent, status roles, and overlay identity, and splits the monolith by mechanical seams before visual edits.
* **Extraction** moves overlay painting, page builders, and session controllers behind narrow interfaces; the visual layer stops owning recording logic.
* **Intermediary** provides one `SettingsSnapshot`/`SettingsDraft` contract, one commit-policy coordinator, and one `SettingRow` shell with variants.
* **Dynamics** handles platform profiles (Windows light, macOS palette/dark) and row overflow instead of forcing one fixed layout.
* **Beforehand Cushioning** provides deterministic preview, golden layout data, and rollback tags before pixels change.

The Ideal Final Result is one authored semantic token source; thin adapters for QSS, painter, and macOS palette; a settings dialog that truthfully declares commit behavior; dense pages expressed as explicit row variants; and a file layout where no UI file exceeds the agreed budget without a recorded waiver.

## Findings by Severity

Tags: `[VERIFIED]` means checked in source; `[RECOMMENDATION]` means a proposed action, not a current fact.

### Blocker

**B1. There is no application shell, and the concept does not say what "shell" means.**
Lens: design critic, zoom out, codebase details.
[VERIFIED] The only navigable surface is the modal settings dialog (`1609-2735`); the main surface is a frameless overlay (`2737-2752`).
[RECOMMENDATION] State that the settings dialog is the shell. If a main window is wanted, split it into a separate product decision with its own lifecycle, entry point, and scope.

**B2. A single token system cannot cover QSS, QPainter, macOS native controls, and user-owned overlay themes.**
Lens: design critic, structural, inventive catalyst.
[VERIFIED] Four styling paths exist (`1655-1685`, `1641-1653`, `1262-1344`, `1512-1580`); overlay colors are persisted user data (`268-283`, `3322-3351`).
[RECOMMENDATION] Adopt primitives + semantic roles + per-surface adapters. Keep overlay presets authoritative and process-local. No global mutable theme.

**B3. The proposed four-file split cannot satisfy the 600-line rule.**
Lens: design critic, codebase details, structural, readiness.
[VERIFIED] Dialog ~1,127 lines, window ~2,684 lines, overlay widgets ~984 lines, models page 395 lines.
[RECOMMENDATION] Plan a ~20-file UI decomposition or record a waiver. Separate the mechanical split from controller extraction, and do not claim compliance at the end of the mechanical phase.

**B4. One Apply/Cancel footer would misrepresent three commit models and one irreversible action.**
Lens: comprehension, inventive catalyst, codebase details.
[VERIFIED] Appearance saves immediately (`2031-2034`, `3541-3545`); models mutate immediately (`models_page.cpp:139-210`) and delete files (`274`; `model_manager.cpp:238-267`); Cancel restores only locations (`3608-3612`).
[RECOMMENDATION] Define commit policy per section and render it beside the control. Keep the current semantics unless a behavior change is approved; confirm destructive deletes and never imply Cancel restores files or stops downloads.

**B5. "Keep keyboard access and visible focus" describes behavior that does not exist.**
Lens: comprehension, readiness, structural.
[VERIFIED] Sidebar is `Qt::NoFocus` with `outline: 0` (`1704`, `1646`, `1658`); no `:focus` rules for buttons, checkboxes, sliders, or table cells (`1675-1684`); color swatches have tooltip-only names (`2046-2049`).
[RECOMMENDATION] Treat keyboard navigation and focus rings as deliverables: focusable sidebar, arrow/Home/End page changes, explicit tab order, tokenized focus state, accessible names for icon-only controls.

**B6. Verification cannot detect a lost setting, page, or translation.**
Lens: all.
[VERIFIED] No UI test; preview is non-deterministic, on-screen, fixed-size, ignores save failures, and does not sync the sidebar; bundled baselines predate HEAD.
[RECOMMENDATION] Build a settings inventory and round-trip test, a deterministic preview mode, a translation gate, and failure exit codes before restyling.

**B7. The English catalogue is stale, so parts of the English UI are already Chinese.**
Lens: comprehension, codebase details, structural.
[VERIFIED] Current strings are missing; a removed string remains at `.ts:210`; 250 `Ui::tr` calls versus 227 messages.
[RECOMMENDATION] Regenerate and translate before the visual pass, keep the `SpeakAnything` context in every new file, and gate on zero unfinished/vanished entries.

**B8. There is no clean rollback baseline for this change.**
Lens: readiness.
[VERIFIED] Binaries predate HEAD; some build directories point at other checkouts; the installer's uninstall removes `{app}` including hotwords and imported models (`SenseVoice.iss:86`).
[RECOMMENDATION] Tag a baseline, build HEAD in a fresh tree-local directory, retain the previous Setup/ZIP, and document rollback as install-over-old, never uninstall-first.

### Major

**M1. The reference vocabulary already exists; the real defects are elsewhere.**
Lens: zoom out.
[VERIFIED] White bordered cards, light background, helper text, and right-aligned fields are already implemented (`1655-1685`, `2057-2117`). What feels mechanical is competing accents, a nearly flat type scale, inconsistent page composition (multi-card versus single-card), and a selected nav state that reads like an input field.
[RECOMMENDATION] Fix accent roles, typography, spacing rhythm, and page composition inside the existing helpers before replacing the shell.

**M2. A 220 px sidebar costs 52 px of the densest pages.**
Lens: zoom out, comprehension, codebase details.
[VERIFIED] Content width is about 648 px today at 880 px; at the 680 px minimum it is about 448 px. A 220 px sidebar leaves about 596 px and 396 px respectively (`1639-1640`, `1703`, `1689-1700`). The hotword table has six columns; the model row has a combo and four buttons with a 76 px minimum.
[RECOMMENDATION] Keep 168-184 px, or raise the minimum width to preserve today's content width (~732 px for 220 px navigation). Define responsive row behavior and validate English at 150-200% scaling.

**M3. One row template cannot express the eight pages.**
Lens: comprehension, inventive catalyst.
[VERIFIED] Controls include segmented buttons (`2258-2272`), sliders (`2614`), color/font pickers (`1938-1970`), a six-column table (`2503-2521`), and model rows with four actions plus progress (`models_page.cpp:228-249`).
[RECOMMENDATION] Define explicit row variants and a documented rule for which page uses which. Keep the hotword table as a table and the model row as an action cluster.

**M4. Reordering hotword columns would corrupt persisted data.**
Lens: readiness, codebase details.
[VERIFIED] UI order is enabled, phrase, aliases, boost, hits, keep (`2504-2521`); TSV order is phrase, aliases, enabled, hits, boost, keep (`text_processor.cpp:641-651`).
[RECOMMENDATION] Replace numeric column indices with named constants while preserving the persisted mapping. Require a byte-identical TSV round-trip on a fixture with CJK phrases, aliases, non-default boost, and non-zero hits.

**M5. Unifying the overlay with the light shell risks legibility and the geometry contract.**
Lens: design critic, zoom out, comprehension.
[VERIFIED] Overlay defaults are dark for arbitrary desktops (`1523-1537`); window positioning is tested by `tools/verify_preview_geometry.ps1` with 1.1 px center-drift limits.
[RECOMMENDATION] Share typography and semantic roles, not surface colors. Keep overlay-specific dark/light/high-contrast surfaces and treat the existing geometry script as an acceptance gate.

**M6. The macOS policy is undefined.**
Lens: design critic, readiness, inventive catalyst.
[VERIFIED] macOS uses palette roles and native controls; the Windows branch is fixed light (`1641-1685`); app-level QSS is skipped on macOS (`5504-5513`).
[RECOMMENDATION] Choose native macOS with semantic palette tokens, or branded Qt-drawn controls with an explicit dark-mode trade-off. Record it as an ADR; do not let `#ifdef` decide.

**M7. `ModelsPage` is styled only by its parent dialog's object-name QSS.**
Lens: design critic, structural, codebase details.
[VERIFIED] It creates `QFrame#card` and `#cardTitle`/`#cardHint` (`models_page.cpp:86-121`, `214-250`) but has no stylesheet; the rules live in `InputSettingsDialog` (`1664-1667`).
[RECOMMENDATION] Make pages self-styling from injected tokens or shared object-name constants. Keep `settingsStack` and page object names stable for preview.

**M8. The dialog is a god object, and a naive page split multiplies its coupling.**
Lens: design critic, structural.
[VERIFIED] 22-argument constructor (`1611-1632`), 40+ widget members (`2670-2734`), about 16 getters, cross-page rules for content hints and speech gating (`1903-1911`, `2429-2435`).
[RECOMMENDATION] Introduce `SettingsSnapshot`/`SettingsDraft` and page bundles. Pages receive their slice plus callbacks and never hold the dialog. Wire pages after construction rather than relying on creation order.

**M9. "Restore defaults" is a partial reset presented as a global action.**
Lens: comprehension, structural.
[VERIFIED] The reset lambda covers a subset and omits appearance, startup, compute, hotwords, and models (`1755-1780`).
[RECOMMENDATION] Either complete it with confirmation or move reset per card and rename the footer action. Document the scope.

**M10. The proposed icon rows have no asset or deployment path.**
Lens: readiness, zoom out, structural.
[VERIFIED] `sensevoice.qrc` contains only the app icon; `build-final/imageformats` has no `qsvg`; `Qt6::Svg` is not linked.
[RECOMMENDATION] Use raster or `QStyle` icons, or explicitly approve `Qt6::Svg` and add the plugin to packaging. Do not assume SVG works because Qt supports it in development.

**M11. Documentation and governance are incomplete.**
Lens: structural, readiness.
[VERIFIED] `AGENTS.md` points to a missing `CLAUDE.md`; the READMEs' source-structure sections omit `models_page`, `model_manager`, and `model_catalog`; the settings table omits several live keys; there is no docs index; the 600-line rule is not recorded in the repo.
[RECOMMENDATION] Fix the instruction pointer, update both READMEs, add an ADR and a living UI document, and decide where move/deletion records live before renaming files.

**M12. The build matrix makes a missed file or context invisible.**
Lens: readiness, structural.
[VERIFIED] Duplicated Windows/macOS source lists; no CI; no macOS build here; no AUTOMOC; `qt_add_translations` only scans target sources.
[RECOMMENDATION] Use one shared source variable plus platform extras, add a parity check, keep new UI files in the existing target, and add a macOS compile-only job or an explicit reviewer sign-off.

**M13. Preview and model lifetimes can mislead or crash after extraction.**
Lens: readiness, structural.
[VERIFIED] Preview runs real settings and a real `ModelManager`; `ModelsPage` removes `handlers_` and `handlers_ + 1` assuming adjacency (`models_page.cpp:81-84`); the manager is owned by the window.
[RECOMMENDATION] Make preview side-effect-free with injected state, store both handler tokens explicitly, and define manager lifetime/ownership before moving the page.

**M14. Failures are silent.**
Lens: codebase details.
[VERIFIED] No `qDebug`/`qWarning` in the UI files; `saveSettings` never checks `QSettings::status()`; the UI still shows "已应用"; translator-load failure is silent (`565-576`).
[RECOMMENDATION] Add a UI logging category, check settings status, and surface non-blocking errors. Log page counts and missing controls in preview.

**M15. A global stylesheet would leak into the overlay and tray.**
Lens: readiness.
[VERIFIED] Dialog QSS uses unscoped `QLabel`/`QPushButton` selectors (`1656`); the app-level sheet is scoped to tooltip/menu (`5508-5513`).
[RECOMMENDATION] Scope token QSS to a shell root object name; keep overlay and tray styling separate.

### Nit

**N1. The window title is load-bearing for single-instance activation.**
[VERIFIED] `FindWindowW(nullptr, L"SenseVoice 语音输入")` matches the title literal (`5490`, `2743`). [RECOMMENDATION] Freeze the title or replace the lookup with an IPC handle; add a launch-twice test.

**N2. UTF-8 conversion helpers are duplicated three times.**
[VERIFIED] `to_qstring` appears in `qt_ui_main.cpp:187`, `models_page.cpp:22`, and `model_manager.cpp:18`. [RECOMMENDATION] One inline header; do not create a general-purpose `ui_common.h`.

**N3. Page construction order is an undocumented dependency.**
[VERIFIED] Translation page is built before Models, with a null guard and a later `setLanguages` call (`1718`, `1721-1725`, `2382-2388`). [RECOMMENDATION] Add an explicit wiring step after all pages exist.

**N4. New files have no stated newline/encoding convention.**
[VERIFIED] `qt_ui_main.cpp` is CRLF; `models_page.cpp` is LF; both are UTF-8 without BOM. [RECOMMENDATION] Pick one convention for new UI files and record it in the ADR.

**N5. The fixed label-width heuristic is translation-dependent.**
[VERIFIED] `addRow` picks 76 or 124 px by comparing `Ui::tr("组合键")` to the Chinese literal (`2093`). [RECOMMENDATION] Use font metrics or a grid; never branch layout on translated text equality.

**N6. The README calls the preview a screenshot tool without documenting its limits.**
[VERIFIED] `README.md:434` and `README.en.md:486` describe page screenshots; neither mentions the selected-row desync, fixed size, or registry writes. [RECOMMENDATION] Update both after the preview contract is fixed.

## Design Contradictions

| Contradiction | Verdict | Resolution |
| --- | --- | --- |
| "Rebuild the shell" versus no application shell | Genuine scope ambiguity | Define shell as the settings dialog; no new main window |
| One token system versus four renderers | Genuine at implementation level | Primitives + semantic roles + adapters |
| One blue accent versus status colors and teal identity | False if accent means chrome only | Blue chrome accent; separate status and overlay identity roles |
| Preserve behavior versus one Apply/Cancel model | Genuine and dangerous | Explicit per-section commit policy |
| Full-row toggle rows versus dense controls | False if row variants are allowed | Six row variants; table stays a table |
| 600-line rule versus UI-only refactor | Genuine | Mechanical split with waiver, then controller extraction |
| Reference fidelity versus native macOS | False for hierarchy; genuine for colors | Shared layout/typography; platform-specific surfaces |
| Keyboard access versus current NoFocus | Genuine functional gap | Make keyboard navigation a deliverable |
| Behavior-preserving split versus visual redesign | Genuine verification conflict | Two change sets with separate baselines |
| Offscreen preview versus `show()` + `grab()` | False claim | Fix determinism or stop calling it offscreen |

## Goals vs. Non-Goals

### In-Scope Goals

* A clearer settings hierarchy: page title, grouped navigation, consistent card/row composition, controlled spacing, and a readable type scale.
* One authored token source with semantic roles and per-surface adapters for the dialog, models page, hotword table, and overlay.
* All eight settings pages restyled without losing a setting, page, control, warning, or commit behavior.
* Model page and hotword table hierarchy improved while preserving their dense, multi-action nature.
* Overlay visual alignment limited to typography, semantic roles, and identity tokens; user themes remain authoritative.
* Keyboard navigation, visible focus, and accessible names for icon-only controls.
* Deterministic verification: settings inventory/round-trip, hotword round-trip, translation gate, preview manifest, and existing geometry checks.
* Documentation and i18n synchronization: both READMEs, an ADR, a living UI document, the instruction pointer, and the English catalogue.
* A file decomposition that meets the agreed line budget or records explicit waivers.

### Out-of-Scope Non-Goals

* A new main window, dashboard, or persistent application shell.
* New product features, settings, or workflow changes.
* Recognition, translation, TTS, audio routing, injection, downloading, or persistence-schema changes.
* Unifying the three commit models, unless explicitly approved as a behavior change.
* Replacing Qt Widgets or adding a new runtime dependency (including `Qt6::Svg`) without approval.
* Rebranding the application, window title, mutex, or QSettings scope.
* Claims of accessibility compliance or measured performance; only specific behaviors may be verified.
* A branded, non-native macOS shell unless an ADR selects that trade-off.

## Alternatives Weighed

| Alternative | Why rejected |
| --- | --- |
| New main window with grouped navigation | Product/behavior change; adds lifecycle, focus, tray, and single-instance risk |
| Literal single palette across all surfaces | Overrides user overlay themes, breaks high contrast and macOS dark mode, flattens semantic colors |
| Four-file split as proposed | Leaves ~1,127-line dialog and ~2,684-line window; does not satisfy 600 lines |
| Convert every setting to one row with a right toggle | Cannot express tables, sliders, color pickers, or four-button model rows |
| Ship an icon set in the first pass | No assets, no qsvg plugin, no evidence icons improve scanning; adds QA surface |
| Apply tokens via `qApp->setStyleSheet` | Leaks into overlay, tray, tooltips, and message boxes; breaks macOS native behavior |
| Branded QSS on macOS | Removes native controls and dark-mode behavior; contradicts the preview platform's intent |
| Keep the settings dialog alive as a persistent window | Changes lifecycle and focus behavior; raw `ModelManager` lifetime becomes unsafe |
| Add a "legacy shell" runtime flag | Doubles UI, focus, and test paths; rollback should use the previous build |
| Big-bang refactor plus restyle in one change set | Makes regressions unattributable; pixel parity cannot be proven |
| Keep the 220 px sidebar without raising the minimum width | Costs 52 px on the densest pages and clips English labels |
| Unify the accent by changing the overlay default to blue | Changes shipped visual identity; must be an explicit owner decision |

## Key Decisions & Staging for Next Steps

### Decisions Required Before Drafting

| ID | Decision | Recommendation |
| --- | --- | --- |
| D1 | Is a new main window in scope? | No. Shell = settings dialog. |
| D2 | Token architecture | Primitives + semantic roles + per-surface adapters; one authored source. |
| D3 | Accent policy | Blue for chrome/focus; keep semantic status colors; keep teal as an explicit overlay identity token. |
| D4 | Navigation width and minimum size | Keep 168-184 px, or use 220 px and raise the minimum width by ~52 px; define responsive rows. |
| D5 | Row grammar | Six explicit variants; table and model action cluster remain distinct. |
| D6 | Commit semantics | Preserve the three current models and disclose them per section; do not silently unify. |
| D7 | Reset scope | Either complete it with confirmation or move reset per card and rename the footer action. |
| D8 | 600-line scope | UI-only with a recorded waiver for Phase A; Phase B extracts controllers. |
| D9 | macOS policy | Native controls with palette-derived semantic tokens; shared layout/typography. |
| D10 | Icons | Raster or `QStyle` now; `Qt6::Svg` only with explicit dependency and packaging approval. |
| D11 | Test dependency | Approve a test-only Qt Test target, or implement an in-app self-check mode. |
| D12 | i18n scope | Regenerate and translate missing strings as part of this work; keep the `SpeakAnything` context. |
| D13 | Documentation home | `docs/adr/0003-...` plus a living `docs/ui-design-system.md`; update both READMEs and the instruction pointer. |
| D14 | Identity freeze | Freeze window title, app/org names, mutex, and QSettings scope. |

### Staged Plan

**Phase 0 — Baseline freeze.** Tag `9a66093`; configure a fresh tree-local build; run the nine existing tests; capture eight settings pages in Chinese and English at 100% and one scaled DPI; capture three overlay styles; export the registry key set; hash `hotwords.tsv` and the `.ts`; record line counts and both CMake source lists.

**Phase 1 — Mechanical split, zero visual change.** Extract overlay widgets, page builders, dialog shell, and app entry; split `VoiceInputWindow` by method group without editing logic. Do not change construction order, object names, strings, or settings keys. Exit on pixel parity, identical registry key set, identical hotword hash, and unchanged `.ts`.

**Phase 2 — Token plumbing with identical values.** Introduce primitive and semantic tokens whose values equal current literals; keep per-style overlay defaults; generate QSS from tokens. Exit on pixel parity and no new keys/assets.

**Phase 3 — Shell and navigation.** Implement the page title, grouped navigation, card/row shell, and keyboard/focus model. Keep page content unchanged. Exit on eight pages in order, keyboard walkthrough, and zh/en captures.

**Phase 4 — Page-by-page rows.** Restyle pages in disjoint commits: input/output/VAD; appearance/translation/speech; hotwords; models. Exit per page on zh/en captures, no clipped labels, model state matrix, and byte-identical hotword round-trip.

**Phase 5 — Overlay alignment.** Apply shared typography and identity tokens to the three painted styles without changing user presets. Exit on three style captures, the existing geometry script, and the preset/custom matrix.

**Phase 6 — Docs, i18n, packaging, release.** Update both READMEs, add the ADR and living UI document, finish translations, fix the instruction pointer, build the installer/ZIP, and smoke-test a clean profile.

### Acceptance Gates

* `git status` is clean at the baseline tag before Phase 1.
* Nine existing `ctest` targets pass on HEAD, in a build directory that belongs to this tree.
* Every new or moved file is listed in both platform source lists; no file exceeds the agreed budget or its waiver.
* Settings inventory round-trip is equal: page, control object name, type, label, value.
* `hotwords.tsv` round-trip is byte-identical (SHA-256) for CJK phrases, aliases, boost, hits, and keep flags.
* The QSettings key set and value types are unchanged; legacy fixtures (`output/destination=copy-both`, `vad/profile_version=0`) still migrate.
* The `.ts` has one context, zero unfinished entries, and every UI string present; English preview shows no Chinese fallback in restyled pages.
* Preview exits non-zero on missing stack, wrong page count, invalid directory, or failed save; it does not write user settings.
* Keyboard walkthrough passes: tab reaches the sidebar, arrows/Home/End change pages, every control shows a focus ring, icon-only controls have names.
* `tools/verify_preview_geometry.ps1` passes for all three overlay styles.
* No recognition, translation, TTS, injection, download, or persistence behavior changes.

## Strongest Objections

1. **The visual promise is false without a commit policy.** A user can delete a downloaded model, press Cancel, and still lose the files; appearance changes are already saved before Cancel. A cleaner footer would make the dialog look more trustworthy while making its contract less truthful.
2. **"One token system" is a re-architecture disguised as styling.** The overlay's palette is user data with a high-contrast preset and per-style defaults. Treating it as one flat token set either overrides that feature or becomes a fourth unused layer.
3. **The 600-line claim cannot be met by a UI-only move.** The window controller owns recording, translation, speech, hotkeys, tray, and model loading. Satisfying the rule requires behavior extraction that the scope forbids and that no test currently covers.
4. **The reference is being copied at the wrong altitude.** The current UI already has white cards and thin borders. Its mechanical feel comes from competing accents, flat type hierarchy, inconsistent page composition, and a nav selected state that reads like an input field. A new shell adds chrome without fixing those.
5. **Verification is the actual blocker.** Without an inventory and a deterministic preview, neither "every setting preserved" nor "only visuals changed" can be demonstrated.

## Edge Cases

* **Minimum window and scaling:** `680x540` at 150-200% leaves little height on 768 px laptops; a 220 px sidebar leaves ~396 px of content.
* **English expansion:** long checkbox labels (for example the live-speech option), long model hints, and fixed 76/124 px label columns clip or force width; horizontal scrolling is disabled.
* **Disabled states:** speech is disabled until translation is on; paste delay is disabled for copy-only; the current QSS has no disabled styling for several controls.
* **Empty/first-run:** the models page hides download actions under "Automatic"; hotwords has no empty state.
* **Errors:** download failure and hotword save failure are modal; shortcut validation is inline; the redesigned card rhythm must decide where each lives.
* **Destructive actions:** model removal deletes files immediately; Cancel cannot restore them.
* **Old registry:** `output/destination=copy-both`, missing `output/content`, and `vad/profile_version=0` must still load.
* **Hotword data:** CJK phrases, aliases with `|`, non-default boost, non-zero hits, and empty rows.
* **Translation load failure:** the translator is silently dropped; English silently falls back to Chinese.
* **macOS dark mode:** fixed light tokens would override palette roles.
* **Windows dark OS mode:** hardcoded light QSS mixed with `palette(placeholder-text)` can produce unreadable combinations.
* **Multi-monitor:** the dialog is parented to the overlay and may open on a monitor that was removed.
* **Settings while recording:** opening settings silently returns while listening and shows only a transient status while loading.
* **Preview:** `--preview-settings` is ignored without `--preview`; `--preview-all` creates three windows, three managers, and three settings objects in one process.
* **Single instance:** a rebranded title breaks the `FindWindowW` path while the mutex still blocks a second launch.

## Open Questions for the Owner

1. Is the settings dialog the entire scope, or is a new main window requested?
2. Is the 600-line rule UI-only or repo-wide? If repo-wide, which existing files are grandfathered?
3. Should the overlay default accent become blue, or remain teal as product identity?
4. Is a 220 px sidebar required, and may the minimum window width rise to preserve content width?
5. Is unifying Apply/Cancel a desired behavior change, or must the three commit models be preserved?
6. Are icons required in the first release, and may `Qt6::Svg` be added?
7. Is a test-only Qt Test target acceptable, or should verification stay inside the app?
8. Is completing "Restore defaults" in scope as a behavior change?
9. Is translating the missing English strings part of this task or a prerequisite?
10. Should macOS keep native controls in dark mode even if it looks less like the Windows reference?

## Method & Limitations

* Seven Goldfish lenses ran in fresh contexts with no parent transcript. Each read its persona file and the project sources before reporting.
* Reviews were source-first. No compilation, test execution, or application launch was performed; all build and runtime steps above are proposed, not executed.
* No performance, accessibility-compliance, or user-research measurements were taken. Statements about clipping, width, and scaling are arithmetic or code-derived, not rendered measurements.
* Baseline screenshots are stale relative to HEAD and were used only as visual context, not as proof of current layout.
* macOS was reviewed from source only; no macOS build is available here.
* The parent directory is not a git repository; `SpeakAnything` is. The initial environment check looked at the parent and produced a false "not a repo" result, corrected during review.
* Environment integrity: during the run, the tracked file `GLOSSARY.md` was deleted (cause not attributable from the available logs) and a transient git lock appeared. The file was restored from the Recycle Bin, repaired to its original first byte, and verified byte-identical to HEAD (`git diff` empty, clean worktree at `9a66093`). No source changes from this review remain.

## Appendix A: Settings Surface Inventory

| Page | Current controls | Behavior that must survive |
| --- | --- | --- |
| Input | Recognition device combo, detected-GPU status, shortcut preset plus custom editor and error, trigger combo, mute, startup | Shortcut validation; custom editor reveal; startup reflects the registry; device change reloads recognition |
| Output | Raw/clean segmented control, destination, popup content, clipboard content, TTL, paste delay, dependency hint | Text mode mapping; destination migration; hint depends on translation; delay disabled for copy-only |
| Appearance | Five overlay presets, three color pickers, font family/size, linger, pinned, instant-apply note | Live apply and save; Cancel does not revert; editing forks to Custom; affects overlay only |
| Translation | Enable, source/target languages, device, separator with live example, note | Separator sanitization; language changes update models page; independent device policy |
| Speech | Enable/live, translation dependency warning, device, monitor, two voices, speed | Disabled until translation; stale device retained; voice IDs; live mode |
| Hotwords | Six-column table: enable, phrase, aliases, boost, hits, keep; add/remove | Empty rows skipped; aliases split on `|`; hits preserved; TSV save |
| Models | Directory card; three model rows with combo, download, browser, import, remove, status, progress | Auto resolution; language refill; import validation; immediate mutations; Cancel restores locations only |
| VAD | Four sliders with live values | Keys, profile migrations, macOS default, reset values |

## Appendix B: Companion Surfaces

| Surface | What it owns | Risk if treated as part of the shell |
| --- | --- | --- |
| Tray menu | Settings, recent inputs, translation, speech, mute, language, exit | Duplicate state and stale checkmarks; language requires restart |
| Overlay context menu | Settings, cancel, copy, recent, exit | Settings reachable without a main window |
| Floating overlay | Transcript, transport controls, drag, pin, theme | Legibility over arbitrary desktops; geometry anti-jitter; user themes |
| Preview harness | `--preview`, `--preview-image`, `--preview-settings`, `--preview-all`, geometry test | Hidden contract on `settingsStack`, filenames, and page order; writes real settings |
| Packaging | Installer, mutex, uninstall cleanup | Uninstall removes hotwords and imported models; rollback must install over old |
