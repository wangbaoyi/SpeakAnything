# 设置界面设计系统

适用于 `InputSettingsDialog` 与 `ModelsPage`。Windows 使用固定色板与 px 字号；macOS 沿用系统控件、字体和深浅色，只共享间距、字号层级与焦点行为。

## Token

颜色定义在 `src/ui/design_tokens.cpp`，通过 `sa::ui::color(ColorRole)` 读取。

| Token | 值 | 用途 |
| --- | --- | --- |
| `Background` | `#F5F7FB` | 对话框背景 |
| `Sidebar` | `#EEF3FA` | 侧栏背景 |
| `Surface` | `#FFFFFF` | 卡片与选中项背景 |
| `SurfaceSubtle` | `#F8FAFD` | 次级表面 |
| `TextPrimary` | `#1F2937` | 标题与正文 |
| `TextSecondary` | `#4B5563` | 行标签 |
| `TextMuted` | `#5B6472` | 说明、路径、状态 |
| `Border` | `#DDE3EC` | 卡片和输入框描边 |
| `Focus` | `#1D4ED8` | 键盘焦点环 |
| `Selection` | `#EAF1FF` | 选中背景 |
| `Accent` | `#1D4ED8` | 主按钮与滑杆 |
| `AccentHover` | `#1E40AF` | 主按钮悬停 |
| `SliderFill` | `#1D4ED8` | 滑杆已选轨道 |
| `StatusSuccess` | `#15803D` | 成功状态 |
| `StatusWarning` | `#B45309` | 警告状态 |
| `StatusDanger` | `#DC2626` | 错误状态 |
| `DisabledSurface` | `#F3F4F6` | 禁用背景 |
| `DisabledText` | `#9CA3AF` | 禁用文字 |

尺寸 token：`page_title_size=24`、`card_title_size=15`、`body_size=13`、`hint_size=12`、`row_min_height=44`、`nav_item_height=44`。间距使用 4/8/12/16/24/32，圆角使用 6/8/12。

## 壳层与导航

- 侧栏宽 184 px；窗口默认 880×740，最小 760×540。
- 侧栏按任务分组：语音输入（输入、输出、VAD）、语音能力（翻译、朗读）、资源（热词、模型）、个性化（外观）。
- 导航项最小高度 44 px；选中项左侧 3 px 强调条，背景 `Surface`，文字 `TextPrimary` 加粗。
- 侧栏可聚焦；Up/Down 在页面项之间移动并跳过分组标题，Home/End 跳到首尾页面项；Tab 进入内容区。聚焦时侧栏显示 2 px `Focus` 描边。
- 页面顺序固定：输入 0、输出 1、外观 2、翻译 3、朗读 4、热词 5、模型 6、VAD 7。

## 行与变体

| 变体 | 结构 | 示例 |
| --- | --- | --- |
| Select | 44 px 行：标签 + 控件 | 计算设备、语言、触发方式 |
| Toggle | 44 px 行：标题 + 右侧复选框 + 可选说明 | 常驻显示、开启翻译 |
| Slider | 44 px 行：标签 + 滑杆 + 当前值 | 停留时长 |
| SliderBlock | 标签/值、滑杆、范围与说明 | VAD 四项 |
| Custom | 44 px 行内多控件 | 颜色色块 + 十六进制值、字体 + 字号 |
| Action | 44 px 行：一至三个可见按钮，隐藏项不占位 | 模型下载/导入/删除 |
| Workbench | 卡片内表格 + 空状态 + 底部操作 | 热词表 |
| Path/Status | 路径或状态说明，可换行、可选择文本 | 模型目录、模型详细状态 |

卡片统一使用 `QFrame#card`，标题 `QLabel#cardTitle`（15 px、600），说明 `QLabel#cardHint`（12 px、`TextMuted`），行标签 `QLabel#rowLabel`（13 px、`TextSecondary`）。模型卡使用短状态标记（自动/需下载/已下载/已内置/已导入/下载中）加详细状态行。

## 页面与对象名

页面锚点：`page_input`、`page_output`、`page_appearance`、`page_translation`、`page_speech`、`page_hotwords`、`page_models`、`page_vad`。

| 页面 | 稳定对象名 |
| --- | --- |
| 输入 | `input_compute`、`input_shortcut_preset`、`input_shortcut_edit`、`input_trigger`、`input_mute`、`input_startup` |
| 输出 | `output_mode_raw`、`output_mode_clean`、`output_destination`、`output_popup_content`、`output_clipboard_content`、`output_clipboard_ttl`、`output_paste_delay` |
| 外观 | `appearance_theme_preset`、`appearance_color_background`、`appearance_color_text`、`appearance_color_accent`、`appearance_font_family`、`appearance_font_size`、`appearance_linger`、`appearance_pinned` |
| 翻译 | `translation_enabled`、`translation_source`、`translation_target`、`translation_device`、`translation_separator` |
| 朗读 | `speech_enabled`、`speech_live`、`speech_device`、`speech_monitor`、`speech_voice_en`、`speech_voice_zh`、`speech_speed` |
| 热词 | `hotwords_table`、`hotwords_add`、`hotwords_remove`、`hotwords_empty` |
| 模型 | `models_directory_label`、`models_directory_choose`、`models_directory_reset`；`models_<kind>_{combo,state,status,download,browser,import,remove,progress}`，`kind` 为 `recognizer`、`translator`、`voice` |
| VAD | `vad_endpoint`、`vad_threshold`、`vad_minimum_db`、`vad_snr` |

清单文件 `tests/fixtures/settings_inventory.json` 是页面、控件 ID、类型与默认值的只读基线；新增或改名控件时必须同步更新，并通过 `settings-ui-test` 校验。

## 提交语义

- 外观修改即时保存并作用于浮窗；页内明确标注“即时生效”，取消不回滚。
- 模型操作即时生效；删除不可恢复，删除按钮旁提示“删除后需重新下载”。
- 其余设置在“应用”后写入；页脚“恢复表单默认值”覆盖 VAD、快捷键、输入模式、结果去向、内容、TTL、触发方式、静音、粘贴延迟、翻译和朗读。

## 验收

```powershell
cmake -S C:\sa\SpeakAnything -B C:\sa\SpeakAnything\build-verify
cmake --build C:\sa\SpeakAnything\build-verify --config Release --target settings-ui-test sensevoice-ui
$env:PATH = "E:\Programming environment\Qt\6.8.3\msvc2022_64\bin;" + $env:PATH
ctest --test-dir C:\sa\SpeakAnything\build-verify -C Release
```

必须从 ASCII junction 构建：`lrelease` 无法在中文路径下创建 `.qm`。`ctest` 包含 13 项，其中 `translations-check` 校验单一上下文、零 unfinished、零 vanished 和 `.qm` 非空。

确定性预览：

```powershell
$env:QT_QPA_PLATFORM = "offscreen"
Start-Process .\build-verify\Release\sensevoice-ui.exe `
  -ArgumentList @('--preview','--preview-settings', "E:\myworkwindows\语音识别项目\.codex-temp\ui-preview") `
  -Wait -WindowStyle Hidden
```

`settings-ui-test` 断言八页可渲染、760 宽无横向溢出、稳定对象名与 inventory 一致、44 px 行高、草稿全字段往返、热词添加/删除与空状态、侧栏键盘导航。截图哈希基线记录在 `docs/designs/speakanything-ui-refresh/baseline-9a66093.md`。
