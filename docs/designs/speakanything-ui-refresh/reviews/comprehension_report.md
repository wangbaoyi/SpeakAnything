## What this proposes

This document proposes a visual and structural refresh of the SpeakAnything settings dialog: eight existing pages are reorganized under a grouped left sidebar, with a page title, short description, white card/row settings, a light gray-blue background, thin borders, and one blue interaction accent. The model page is reworked around the current model and its primary actions, and the hotword page keeps its six-column editor while gaining a header, empty state, and action area. It also proposes a layered design-token system, keyboard/focus repairs, a deterministic preview and settings-inventory harness, and a behavior-preserving mechanical split of the 5,587-line `qt_ui_main.cpp` into files under 600 lines before any visual migration. The recording overlay keeps its existing dark/teal identity and is moved, not restyled.

## How the existing system works, per this document

The current settings UI is one dialog with eight pages exposed as flat peer navigation; the sidebar is explicitly non-focusable, the pages use white cards, thin borders, and a light background, and component sizes, radii, fonts, and spacing are hard-coded in several competing sets. The model page uses four equal-weight cards for the model directory, recognition, translation, and speech models, while the hotword page is a six-column table with no clear header, empty state, or action hierarchy. Preview currently depends on the current user's theme and device state and writes to the real `QSettings` store (`HKCU\Software\SenseVoice\LocalDictation`), so it cannot prove that settings were preserved. The recording overlay is a separate surface with a default dark/teal identity, three drawing styles, and user theme data under `appearance/*`; the baseline is nine `ctest` tests plus a preview-geometry script and three overlay screenshots.

## Findings

Reviewed lines 1–384 (FeatureRange) and 385–529 (DetailedRange), with the boundary confirmed at line 385 (`## Detailed Implementation Plan`). No complete comprehension failure was found; the document can be paraphrased end to end. The Major findings below are contradictions or undefined project terms that would force a fresh engineer to guess.

### 1. Minimum window size is specified two different ways

- **Severity:** Major
- **Affected Scope:** `Goals → 可验证的完成标准` line 43; `Technical Plan §3 设置壳层` line 177; `验证计划 → 键盘与尺寸` line 504
- **Discrepancy Description & Rationale:** The completion criterion says all eight pages must work at the minimum `680x540` window, but the shell plan raises the minimum width to `760` and the manual walkthrough checks `760x540`. A fresh engineer cannot tell whether `680x540` remains supported, whether the existing minimum must be retained or changed, or which size the acceptance test should enforce. With a fixed 184 px sidebar, the two targets produce materially different content widths and scroll behavior.

### 2. The visual target depends on an unlinked reference image and missing color values

- **Severity:** Major
- **Affected Scope:** `Overview` lines 12 and 28; `Technical Plan §2 视觉系统分层` lines 139–159; `Non-Goals` line 62; `Alternatives` lines 315–375
- **Discrepancy Description & Rationale:** The design repeatedly uses “参考图” as the authority for the light gray-blue background, white cards, blue accent, sidebar proportions, and the “icon + title + description + toggle” row. The document neither embeds nor links that image. The token table supplies spacing, radius, and font sizes, but no values for the neutral color ramp, surface colors, border widths, or the single blue accent; Windows colors are deferred to the missing image, while macOS intentionally uses native palette roles. A fresh engineer cannot choose the Windows palette or judge whether an implementation matches the intended visual target. The “not pixel-perfect” disclaimer reduces, but does not remove, the need for a concrete target.

### 3. `shell.*` contradicts the flat semantic-role list

- **Severity:** Major
- **Affected Scope:** `Technical Plan §2 视觉系统分层` lines 140–143
- **Discrepancy Description & Rationale:** The semantic roles are listed as flat names: `shell.background`, `surface.card`, `text.primary`, `text.muted`, `border.default`, `focus.ring`, `selection.background`, and `status.*`. The next paragraph says the settings window “only reads `shell.*` and `status.*`.” Taken literally, the settings window would not read card, text, border, focus, or selection roles, which contradicts the proposed card/row shell. If `shell.*` is meant as an umbrella namespace containing all of those roles, the document should say so and show the hierarchy. A fresh engineer cannot tell which roles the settings window is allowed to consume or how to organize the token files.

### 4. Overlay vocabulary is never defined

- **Severity:** Major
- **Affected Scope:** `Goals` line 50; `Open Questions` line 69; `Technical Plan §1` line 107; `Technical Plan §2` line 143; `目标文件` line 419
- **Discrepancy Description & Rationale:** The document requires preserving the overlay’s “三种样式”/“三种绘制样式,” says the overlay reads `AppearanceSettings` and “三种样式的默认配色,” and introduces `overlay.identity` and the `appearance/*` group. None of these are defined: the three styles are never named, `AppearanceSettings` is never described (fields, owner, persistence), and `overlay.identity` is not in the semantic-role list. A fresh engineer cannot know what “three styles” must be screenshotted, what data belongs to the user versus the default, or how to avoid overwriting user theme data. “Preserve existing” is not self-contained when the preserved object is unnamed.

### 5. The deterministic-preview interface is inconsistent and incomplete

- **Severity:** Major
- **Affected Scope:** `Technical Plan §7 验证层` lines 258–259; `API 与数据边界` lines 402–404; `验证计划 → 确定性预览` lines 484–491
- **Discrepancy Description & Rationale:** §7 says `PreviewOptions` must force language, theme, page, and model directory. The API section says `PreviewOptions` parses `--preview-page`, `--preview-language`, and `--preview-manifest`; there is no `--preview-theme` or `--preview-model-dir`. The verification commands also use `--preview` and `--preview-settings <dir>`, which are not in that parse list, and `--preview-page all` uses an undefined special value. “Theme” is overloaded (overlay preset, settings-shell appearance, or macOS dark mode), and the manifest’s required contents are never specified. A fresh engineer cannot implement the preview harness that gates all visual work. The current README documents the existing form as `--preview --preview-settings <目录>`; the new option contract does not reconcile that with `PreviewOptions`.

### 6. “设备” has at least three different meanings

- **Severity:** Major
- **Affected Scope:** `CUJ 2` line 82; `Technical Plan §4 行结构` line 194; `§5 输入` line 207; `§5 翻译` line 219; `§5 朗读` line 223; `API 与数据边界` line 399; `目标文件` lines 425–426
- **Discrepancy Description & Rationale:** The document uses “设备” for compute device (`计算设备`, `识别设备`, `翻译设备`, likely CPU/GPU selection), audio output device (`朗读设备`), and device-capability detection (`设备检测结果`). The `Select` row example simply says “设备,” and `SettingsSnapshot` says “识别/翻译设备” without types. A fresh engineer can easily bind the translation device control to the audio-device list, or treat “设备检测” as the same feature as “朗读设备不可用.” This is a functional-bug risk, not a wording preference.

### 7. Hotword six-column semantics are not defined

- **Severity:** Major
- **Affected Scope:** `Goals` line 45; `§5 热词` line 227; `§7 验证层` line 261; `目标文件` line 454
- **Discrepancy Description & Rationale:** The table is specified as six columns — 启用、标准词、别名、增强、命中、不翻译 — but the document never says what each column means, which are editable, what types or ranges they hold, whether “命中” is a read-only counter or user input, or what “增强” (boost) changes. It also says “列宽改用命名常量，禁止依赖数字索引保存” without explaining what is currently saved by index and how the UI column order maps to the persisted TSV fields. A fresh engineer cannot build the workbench editors or the byte-level round-trip test from this description. Naming the columns is not the same as defining them.

### 8. “恢复表单默认值” is undefined on immediate-apply pages

- **Severity:** Major
- **Affected Scope:** `§3 设置壳层` footer diagram line 173; `§6 提交语义与页脚` line 252
- **Discrepancy Description & Rationale:** The footer action is defined as “只重置当前表单字段，不删除模型、不关闭下载、不改外观即时值.” On the appearance page, all controls are immediate-apply and save, so it is unclear whether reset changes the live overlay to defaults, resets only the widget values without applying, or does nothing. On the model page, directory, import, delete, and download are also immediate, so it is unclear which of those the reset touches. The document does not give a per-page reset scope, even though it explicitly preserves three different commit semantics.

### 9. “触点页面” is undefined but determines translation coverage

- **Severity:** Major
- **Affected Scope:** `Goals → 可验证的完成标准` line 49; `§7 验证层` line 262; `§10 文档与索引` line 309; `目标文件` line 452; `验证计划` line 500
- **Discrepancy Description & Rationale:** “触点页面” is used as the scope for the English-translation requirement: “触点页面在英文界面中不出现中文回退,” “补齐触点页面英文翻译,” and “确认触点页面字符串存在英文翻译.” The document never defines the term or lists which pages count. A fresh engineer cannot tell whether it means all eight settings pages, only pages whose visible strings changed, or only the model/hotword pages named in Scope. An undefined scope can let Chinese fallbacks pass the translation check.

### 10. The target-file table contains a false action and an inconsistent path base

- **Severity:** Major
- **Affected Scope:** `§10 文档与索引` line 308; `目标文件` lines 458 and 461
- **Discrepancy Description & Rationale:** The table marks `docs/designs/README.md` as “创建” (create), but the repository already contains that file and it already registers this design document; §10 itself says “登记本设计文档,” which implies update. The correct action is “修改.” The row `SpeakAnything/AGENTS.md` also uses a different path base from every other repo-relative path in the table (`src/...`, `docs/...`, `tests/...`), while the actual `AGENTS.md` is at the repository root. A fresh engineer following the table could overwrite the design index or create a nested `SpeakAnything/SpeakAnything/AGENTS.md`. Repository check confirms both mismatches.

### 11. “原 `main()`” is ambiguous between two entry points

- **Severity:** Major
- **Affected Scope:** `§8 文件拆分边界` line 282; `目标文件` line 413; `目标文件` line 447
- **Discrepancy Description & Rationale:** The plan creates `src/app/main.cpp` to receive “原 `main()`” and deletes `src/qt_ui_main.cpp`, but the repository also contains `src/main.cpp`, which is the entry point for the separate `sensevoice-stream` target. The document never says which `main()` is being moved or what happens to the other one. A fresh engineer could move the CLI entry point into the UI application, or leave two `main()` functions competing in the build. This is an unstated workflow prerequisite.

### 12. Font-size ranges fall outside the declared type scale

- **Severity:** Nit
- **Affected Scope:** `§2 视觉系统分层` lines 151–153; `§3 设置壳层` line 185
- **Discrepancy Description & Rationale:** The font-size scale is `11, 12, 13, 15, 22, 26`, but page titles are specified as 22–24 px and card titles as 14–15 px. Values 14 and 24 are not in the scale, so a fresh engineer cannot tell whether to add them, snap to 15/22/26, or treat the ranges as independent of the scale. The “基础尺度” also names border width and a neutral color ramp without supplying their values. This weakens the claim that one maintainable scale replaces the hard-coded sets.

### 13. “两层 token” contradicts the stated three-layer model

- **Severity:** Nit
- **Affected Scope:** `§2 视觉系统分层` lines 137–141; `§10 文档与索引` line 306
- **Discrepancy Description & Rationale:** §2 says the visual system uses three layers: base scale, semantic roles, and surface adapters. The ADR entry says it records “两层 token.” The document never says which two layers are the “tokens” or whether the adapter layer is excluded. A fresh engineer writing the ADR or the token files cannot tell whether to document two layers or three.

### 14. `SettingsSnapshot` field names are undefined or duplicated

- **Severity:** Nit
- **Affected Scope:** `§5 输出` line 211; `API 与数据边界` line 399; `目标文件` line 423
- **Discrepancy Description & Rationale:** `SettingsSnapshot` lists `输入模式` and `触发方式` as separate fields, but neither is defined; `结果去向`, `浮窗/剪贴板内容`, `剪贴板保留`, `启动`, `录音静音`, and `气泡样式` are also only labels. The output page calls the same-looking setting `保留时间`, while the snapshot says `剪贴板保留` and the file table says `TTL`. A fresh engineer cannot map the snapshot fields to the page controls or tell whether these are duplicates. This is terminology, not code detail, but it affects the “preserve every setting” claim.

### 15. “旧数据迁移规则” is asserted but never described

- **Severity:** Nit
- **Affected Scope:** `Goals → 可验证的完成标准` line 44
- **Discrepancy Description & Rationale:** The criterion says `QSettings` key names, value types, and “旧数据迁移规则” must not change, but the document never says what migrations exist, where they run, or what old data shapes are supported. A fresh engineer cannot verify “unchanged” from the document; the inventory comparison covers current keys and values, not migration behavior. This is an unstated assumption behind a hard acceptance criterion.

### 16. The 1.1 px geometry threshold has no defined measurement

- **Severity:** Nit
- **Affected Scope:** `验证计划 → 翻译与几何` line 500
- **Discrepancy Description & Rationale:** The geometry check must “保持 1.1 px 中心和底边漂移阈值,” but the document does not say which geometry is measured (overlay, bubble, text block, or something else), what “中心” and “底边漂移” refer to, or whether 1.1 px is a maximum, a tolerance, or a target. A fresh engineer cannot interpret or reproduce the check.

### 17. New input-page elements have no content definition

- **Severity:** Nit
- **Affected Scope:** `CUJ 1` line 76; `§5 输入` line 207
- **Discrepancy Description & Rationale:** CUJ 1 asks the user to “选择推荐快捷键,” but the input-page spec only says to preserve the existing shortcut controls; it never defines what a “推荐快捷键” is (a preset list, a detected conflict-free combination, or an OS recommendation). Similarly, the “当前输入方式” summary and the “设备检测结果” are new UI elements with no stated fields, source, or display format. “内容只来自现有设置” does not say which settings, so two implementers will produce different summaries.

### 18. Internal process references are undefined

- **Severity:** Nit
- **Affected Scope:** `Open Questions` lines 66 and 68
- **Discrepancy Description & Rationale:** The document refers to an “早期 brief” and a “Phase A waiver” without linking the brief or defining the waiver. A fresh engineer cannot tell what prior decision was waived, what the brief said, or whether the difference matters. These are presented as binding scope clarifications, but the artifacts behind them are not identified.

