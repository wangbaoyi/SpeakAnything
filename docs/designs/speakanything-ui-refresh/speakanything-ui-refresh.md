# SpeakAnything 设置界面现代化与布局重构

* **Author:** wangbaoyi
* **Status:** Proposed (revised after Goldfish review)
* **Date:** 2026-11-22
* **Scope:** 设置对话框、模型页、热词页

--------------------------------------------------------------------------------

## Overview

本设计把 SpeakAnything 的设置对话框改造成现代桌面工具布局：浅灰蓝背景、分组左侧导航、大标题内容区、白色整行设置卡、细描边和单一蓝色交互强调色。模型页与热词页使用同一套层级和间距规则，但保留表格、组合框、滑杆和多按钮行等必要形态。

本轮不新增应用主窗口，不拆分或重绘录音浮窗，也不改识别、翻译、朗读、文本注入、模型下载或设置持久化行为。工作顺序固定为：先建立可重复的验证基线，再迁移设置相关代码，最后按页面应用视觉改动。录音浮窗继续留在 `qt_ui_main.cpp`；只移除设置代码，并把设置依赖的主题数学原样迁到 `overlay_theme`。

## The Problem

当前设置界面已经具备白色卡片、细边框和浅色背景，但整体仍显得机械，控件难以快速扫描。具体原因不是缺少装饰，而是以下问题叠加：

* 八个页面并列显示，导航没有分组，选中态更像输入框，用户无法快速判断当前任务位置。
* 页面内卡片、说明文字、状态和主操作使用接近的视觉权重，长说明与可操作控件混在一起。
* 控件尺寸、圆角、字号和间距存在多套硬编码规则，页面之间缺少统一节奏。
* 模型页的目录卡与识别、翻译、朗读卡视觉权重接近，长路径与模型状态抢占注意力。
* 热词页是六列表格，信息密度高，表头层级弱，也没有空状态和清楚的操作区。
* 侧栏被设为不可聚焦，按钮、表格、滑杆和复选框缺少一致的可见焦点状态。
* 现有预览依赖当前用户的主题和设备状态，会把设置写入真实注册表，无法证明“只改视觉、没有丢设置”。

参考图解决的问题是“让桌面工具像完整产品，而不是一组原生控件拼接”。设计采用它的信息层级和布局原则，不照搬品牌、主窗口或业务内容。参考截图包含设备标识，不写入仓库；精确颜色和尺寸以本文 token 为准。

## Goals

1. **建立清楚的设置层级**：每个页面都有明确标题、简短说明、统一卡片或整行设置结构，主操作和状态有固定位置。
2. **建立一套可维护的视觉规则**：用基础尺度与语义角色两层 token，替换设置窗内散落的颜色、圆角、字号和间距；Windows QSS 与 macOS 调色板继续各自渲染。
3. **完成八个设置页面的视觉重构**：输入、输出、外观、翻译、朗读、热词、模型、VAD 全部保留，控件和设置项不得丢失。
4. **改善模型页与热词页的扫描效率**：模型页突出当前模型与主要操作；热词表保留六列编辑能力，并强化表头、空状态和操作区。
5. **补齐键盘与焦点行为**：侧栏可用键盘进入和切换，所有交互控件有可见焦点，纯图标按钮有可读名称。
6. **先建立验证再改视觉**：设置页截图直接构造对话框和固定 `SettingsSnapshot`，不构造 `VoiceInputWindow`，不写真实 `QSettings`（语言键允许只读）；设置清单、热词往返、翻译和布局测试必须有可重复的通过条件。
7. **满足文件行数约束**：本轮新增或修改的 UI 源码文件（`.cpp/.h/.mm`）不超过 600 行，文档、fixtures 和资源不计入；`qt_ui_main.cpp` 是既有超限文件，本轮只移除设置相关代码，不拆分浮窗控制器，并在 ADR 记录 grandfather 清单。
8. **同步文档与翻译**：更新中英文 README、设计文档索引、ADR 和英文翻译目录，新增或移动的字符串不得静默回退中文。

### 可验证的完成标准

* 八个页面在 `880x740` 和最小 `760x540` 下可访问，不出现页面级横向滚动或文字裁切；热词表允许内部横向滚动。
* 设置静态清单一致，`SettingsDraft` 与改造前字段逐项一致；`QSettings` 键名、值类型和迁移入口不变。
* `hotwords.tsv` 的无编辑往返与当前保存器输出字节一致；编辑一行后，未编辑行的字节切片不变。
* 设置页预览不构造 `VoiceInputWindow`、不写真实 `QSettings`（语言键允许只读）；固定快照、环境、页面、语言和模型目录后截图可重复。
* 键盘可以进入侧栏并用方向键、Home、End 切换页面；所有可操作控件显示焦点，纯图标按钮有 `accessibleName`。
* 现有 `ctest` 全部通过；新增的 UI 验证目标通过。
* 本文定义的“触点页面”（八个设置页、侧栏、页脚）在英文界面中不出现中文回退。
* 浮窗绘制、控制器逻辑和几何不修改；现有几何脚本继续通过。
* 除登记的 grandfather `qt_ui_main.cpp` 外，本轮新增或修改的 `.cpp/.h/.mm` 文件不超过 600 行；文档、fixtures 和资源不计入。

## Non-Goals

* 不新增主窗口、仪表盘或常驻应用壳层。
* 不改变录音浮窗的默认深色/青绿色身份，也不把浮窗主题并入设置窗主题。
* 不拆分或重写 `VoiceInputWindow`、热键、托盘、会话、翻译 worker、朗读队列或文本注入控制器。
* 不新增产品功能、设置项或工作流。
* 不改变识别、翻译、TTS、音频路由、文本注入、模型下载和设置持久化逻辑。
* 不统一当前三套提交语义：外观即时保存、模型操作即时生效、其余设置在应用后生效。
* 不新增运行时依赖；本轮不使用 `Qt6::Svg`，如确需使用须另立 ADR 并记录打包与跨平台验证。
* 不做 macOS 品牌化自绘壳层；macOS 继续使用原生控件和系统调色板。
* 不追求与参考图逐像素一致，也不复制其品牌、图标或业务页面。
* 不承诺保留任意旧 `hotwords.tsv` 的原始格式；只保证当前保存器的无编辑往返和未编辑行稳定。

## Open Questions

无未决问题。评审后的范围是：

* **文件拆分**：只迁移设置类型、对话框、页面和模型页；浮窗控制器不拆分。新增/修改文件守 600 行，`qt_ui_main.cpp` 记录 grandfather。
* **浮窗范围**：不改浮窗绘制/控制器逻辑、视觉、三种样式或用户主题数据；只保留现有几何检查。
* **窗口尺寸**：默认 `880x740`，最小 `760x540`；`680x540` 不再作为验收尺寸。
* **预览**：设置页直接构造 `InputSettingsDialog`、固定快照和固定 `SettingsEnvironment`；浮窗预览仍走现有窗口，但 `loadSettings()` 的迁移写入必须被预览模式跳过。

## Risks & Limits

| 风险 | 影响 | 检测与验证 | 处置 |
| --- | --- | --- | --- |
| QSS 对表格、滑杆、复选框的焦点和禁用态覆盖不完整 | 键盘用户看不清焦点 | 离屏焦点链测试；手动 Tab 走查 | 逐控件焦点规则 |
| 184 px 侧栏在 `760x540` 或 150% 缩放下拥挤 | 导航滚动、文字裁切 | `QT_SCALE_FACTOR=1.5` 离屏尺寸断言；英文截图 | 侧栏纵向滚动 |
| macOS 原生调色板与 Windows token 不完全一致 | 平台视觉差异 | macOS 构建门禁；差异截图 | 共享尺度与角色；差异记录在 ADR |
| 预览读取真实设备、模型或用户状态 | 截图和断言不稳定 | manifest `environment` 字段；`searchRoots().size()==1` 断言 | 注入 `SettingsEnvironment`；隔离临时模型目录 |
| 模型删除仍即时且不可恢复 | 用户误删后需重新下载 | 手动走查删除路径 | 按钮旁显示不可恢复提示；accepted risk（owner: author），本轮不改行为 |
| 旧热词文件可能被保存器规范化 | 原有格式字节变化 | 无编辑 SHA-256；未编辑行字节切片 | 只承诺当前保存器往返；另立行为变更 |
| 英文翻译缺失或 `.qm` 未生成 | 英文界面回退中文 | `release_translations`、`translations-check` 和英文样本 | LinguistTools + ASCII 构建路径；不允许中文回退 |
| 浮窗回归无法由设置页测试发现 | 录音、翻译或注入失效 | 几何脚本；手动录制冒烟 | 不修改浮窗绘制/控制器逻辑 |

## Critical User Journeys

### CUJ 1: 首次配置快捷键

* **Given**: 用户已启动应用，模型可用，设置窗以默认尺寸打开。
* **When**: 用户从托盘打开设置，进入“输入”，选择推荐快捷键并点击“应用”。
* **Then**: 快捷键设置保存；重新打开设置后仍显示同一选择；无效自定义快捷键仍在当前页显示可读错误。

### CUJ 2: 配置翻译与模型

* **Given**: 用户需要中英互译，部分模型尚未下载。
* **When**: 用户在“翻译”页开启翻译、选择语言和计算设备，再进入“模型”页选择并下载所需模型。
* **Then**: 语言变化同步模型页；模型状态、下载进度和主操作清楚可见；关闭设置后模型选择和下载结果仍在。

### CUJ 3: 编辑热词

* **Given**: 用户已有包含中文短语、别名和“不翻译”标记的热词表。
* **When**: 用户新增、编辑或删除热词，并应用设置。
* **Then**: 表格保持可读；当前保存器的无编辑往返字节一致；编辑一行后，未编辑行的字节切片不变。

### CUJ 4: 只用键盘完成设置

* **Given**: 用户不使用鼠标，设置窗已打开。
* **When**: 用户按 Tab 进入侧栏，用方向键切换页面，再按 Tab 访问页面控件。
* **Then**: 当前页面和焦点始终可见；页面顺序与视觉顺序一致；纯图标按钮可由辅助技术读出名称。

### CUJ 5: 外观即时预览

* **Given**: 用户打开“外观”页。
* **When**: 用户调整浮窗主题、颜色、字体或停留时间。
* **Then**: 浮窗立即变化并保存；界面明确标注“即时生效”；点击“取消”不会让用户误以为外观会回滚。

## Technical Plan (Architecture)

### 1. 改造边界

本轮只改设置对话框、模型页、热词页及其设置类型和样式。录音浮窗、托盘、热键、会话、翻译 worker、朗读队列和文本注入控制器继续留在 `qt_ui_main.cpp`，不拆分、不重绘。设置窗使用自己的浅色蓝色视觉系统，不跟随浮窗主题，也不把设置窗主题写进 `appearance/*`。

改造按以下顺序执行：

```text
行为基线 → 验证夹具 → 迁移设置代码 → 两层 token
→ 设置壳层 → 逐页迁移 → 模型/热词 → 文档与测试
```

先补验证再改视觉的原因：现有设置预览依赖当前用户状态，浮窗构造又会写注册表；没有固定快照、设置清单和离屏布局测试，就无法证明设置项没有丢失，也无法证明视觉改动没有改变行为。

### 2. 视觉系统分层

设置窗使用两层 token，平台渲染保持现有分支：

1. **基础尺度**：间距、圆角、字号、字重、边框宽度和中性色阶。
2. **语义角色**：`settings.background`、`settings.sidebar`、`settings.surface`、`settings.text.primary`、`settings.text.secondary`、`settings.text.muted`、`settings.border`、`settings.focus`、`settings.selection`、`settings.accent`、`settings.status.*`。

Windows QSS 读取这些角色；macOS 继续使用原生控件并把角色映射到 `QPalette`，不生成 Windows QSS。浮窗不读取设置 token，继续使用 `AppearanceSettings`、`ThemeColors` 和三种样式的默认配色。用户自定义浮窗颜色优先级最高。

基础值：

| 角色 | 值 |
| --- | --- |
| 间距 | `4, 8, 12, 16, 24, 32` |
| 圆角 | `6, 8, 12` |
| 字号 | `11, 12, 13, 14, 15, 22, 24, 26` |
| 页面标题 | 24 px，600 字重 |
| 卡片标题 | 15 px，600 字重 |
| 正文/控件 | 13 px |
| 辅助说明 | 12 px |
| 设置行最小高度 | 44 px |
| 导航项高度 | 44 px |
| 边框 | 1 px |
| 焦点环 | 2 px，`settings.focus`，与背景至少 3:1 |

Windows 语义色（实现基准）：

| Token | 值 |
| --- | --- |
| `settings.background` | `#F5F7FB` |
| `settings.sidebar` | `#EEF3FA` |
| `settings.surface` | `#FFFFFF` |
| `settings.surface.subtle` | `#F8FAFD` |
| `settings.text.primary` | `#1F2937` |
| `settings.text.secondary` | `#4B5563` |
| `settings.text.muted` | `#5B6472` |
| `settings.border` | `#DDE3EC` |
| `settings.focus` | `#1D4ED8` |
| `settings.selection` | `#EAF1FF` |
| `settings.accent` | `#1D4ED8` |
| `settings.accent.hover` | `#1E40AF` |
| `settings.slider.fill` | `#1D4ED8` |
| `settings.status.success` | `#15803D` |
| `settings.status.warning` | `#B45309` |
| `settings.status.danger` | `#DC2626` |
| `settings.disabled.surface` | `#F3F4F6` |
| `settings.disabled.text` | `#9CA3AF` |

正文与背景对比度至少 4.5:1；状态色用于文字时也须达到 4.5:1，用于圆点/图标时可低于该值但不得只靠颜色表达。焦点环与相邻背景至少 3:1；强调色按钮上的焦点使用 2 px 白色内环 + 2 px 蓝色外环。Windows 字体为 `Microsoft YaHei UI`，回退 `Segoe UI`，字号单位为 px。参考截图包含设备标识，不落库；以上 token 是 Windows 实现的唯一视觉基准。macOS 共享尺度、字号、间距和焦点行为，不共享固定十六进制表面色。

### 3. 设置壳层

设置窗改为四个区域：

```text
+------------------------------------------------------+
| 设置                                                 |
| 左侧分组导航            | 页面标题 + 说明             |
|                         | 卡片/整行设置              |
|                         | 状态与提示                 |
|                         |                          |
+-------------------------+--------------------------+
| 恢复表单默认值                           取消  应用   |
+------------------------------------------------------+
```

* 侧栏宽度固定为 184 px；默认窗口 `880x740`，最小尺寸 `760x540`。`680x540` 不再作为验收尺寸。
* 侧栏按任务分组，而不是八个平级入口：
  * 语音输入：输入、输出、VAD
  * 语音能力：翻译、朗读
  * 资源：热词、模型
  * 个性化：外观
* 页面顺序固定：输入 0、输出 1、外观 2、翻译 3、朗读 4、热词 5、模型 6、VAD 7。`selectPage(int)` 和预览都使用这个顺序。
* 导航项高度 44 px，左侧选中条 3 px，选中背景使用 `settings.selection`；图标只用现有 `QStyle` 标准图标，并设置 `accessibleName`。
* 侧栏可聚焦。方向键移动当前项，Home/End 跳到首尾，Tab 进入内容区；高度不足时侧栏纵向滚动。
* 页面标题 24 px，说明文字 12 到 13 px；内容区只保留一个主标题，不再重复窗口标题。

### 4. 行结构与页面变体

参考图的“图标 + 标题 + 说明 + 右侧开关”只适合布尔设置。其余控件使用同一外壳下的明确变体：

| 变体 | 用途 | 示例 |
| --- | --- | --- |
| Toggle | 单个开/关 | 开机启动、录音静音、常驻显示 |
| Select | 单选下拉 | 计算设备（CPU/GPU）、触发方式、语言 |
| Slider | 连续数值 | 句尾静音、语速、停留时长 |
| Action | 一至三个可见按钮 | 选择目录、导入、下载 |
| Path/Status | 路径与状态文本 | 模型目录、当前模型、下载进度 |
| Workbench | 高密度表格 | 热词表 |
| Custom | 多控件组合 | 颜色选择、字体与字号 |

每个行壳负责标题、说明、状态位置和对齐；控件本身仍使用 Qt 原生交互。不能用统一样式把表格强行拆成卡片，也不能把下拉框伪装成开关。

### 5. 页面布局

#### 输入

保留当前计算设备、快捷键、触发方式、音频和启动设置。不新增“当前输入方式”摘要；快捷键推荐项沿用现有 preset，设备检测结果继续放在识别设备行下方。快捷键错误显示在快捷键卡片内。

#### 输出

原文/精简已经是分段按钮，本轮只重排样式和分组。结果去向、浮窗内容、剪贴板内容和保留时间使用行结构。粘贴延迟在“只复制”时禁用。依赖翻译的提示保留在对应行附近，不放到页面顶部。

#### 外观

主题、颜色、字体、停留时长和常驻显示按“浮窗外观”组织。页面顶部固定显示“即时生效，取消不会回滚”的说明。颜色按钮在色块旁显示十六进制值，并提供工具提示；不使用只靠颜色表达的按钮。页脚“恢复表单默认值”不改变外观即时值。

#### 翻译

开启翻译使用 Toggle 行；语言、计算设备、分隔符使用 Select/Custom 行。分隔符下方保留实时示例。翻译设备指 CPU/GPU 计算设备，与朗读的音频输出设备分开命名和说明。

#### 朗读

开启朗读、边说边朗读、音频输出设备、监听、声音和语速按依赖顺序排列。未开启翻译时，朗读控件保持禁用，并在原位置说明原因。音频输出设备不可用时仍显示旧设备项，并标记“当前不可用”。

#### 热词

热词表保留六列：启用、标准词、别名、增强、命中、不翻译。启用和不翻译是复选框；标准词、别名可编辑；增强是 `0.0` 到 `12.0`、一位小数的输入；命中是只读计数。UI 列和 TSV 字段逐项映射：UI 0 启用 -> TSV 2 `enabled`；UI 1 标准词 -> TSV 0 `phrase`；UI 2 别名 -> TSV 1 `aliases`；UI 3 增强 -> TSV 4 `boost`；UI 4 命中 -> TSV 3 `hits`；UI 5 不翻译 -> TSV 5 `keep`。页面底部增加“添加热词”主按钮和删除次要按钮；空表显示可操作的空状态。

#### 模型

模型目录作为页面顶部状态卡，显示当前目录和“选择目录/恢复默认”。识别、翻译、朗读各占一张模型卡：

* 第一行：模型名称、当前选择、状态标记。
* 第二行：模型下拉框。
* 第三行：状态相关的操作，最多三个可见按钮：
  * 自动：无操作。
  * 未安装：下载、浏览器下载、导入。
  * 下载中：取消、浏览器下载。
  * 已内置：导入（重新导入）。
  * 已下载：导入（重新导入）、删除。
  * 已导入：重新导入、移除导入。
* 下载中显示进度条和取消按钮；不可用操作隐藏而不是留下空位。

模型删除仍即时生效且不可恢复。界面在删除按钮附近显示“删除后需重新下载”，但不新增确认对话框，除非后续批准行为变更。

#### VAD

四个滑杆按“句尾判定”和“噪声判定”分组。每个滑杆显示名称、当前值、最小/最大含义和简短解释。VAD 页不单独提供恢复默认；页脚恢复包含这四个 VAD 字段。

### 6. 提交语义与页脚

保留现有三套语义，不做静默统一：

* 外观页修改立即应用并保存，页内标注“即时生效”。
* 模型目录、导入、删除和下载立即生效，卡片内标注状态。
* 其余设置点击“应用”后生效，点击“取消”放弃未应用修改。

页脚左侧改为“恢复表单默认值”，重置以下字段：VAD、快捷键、输入模式、结果去向、浮窗/剪贴板内容、剪贴板 TTL、触发方式、录音静音、粘贴延迟、翻译和朗读。重置范围由本列表定义，不由 `SettingsDraft` 成员推导；它不改变外观、开机启动、计算设备、热词和模型选择/下载。外观页显示“外观不参与恢复默认值”；模型页不显示该按钮。`SettingsDraft` 不包含外观字段，外观由即时回调单独提交。页脚右侧保持“取消/应用”。

### 7. 验证层

验证层先于视觉改动实现：

1. **确定性设置预览**：预览直接构造 `InputSettingsDialog`，传入固定 `SettingsSnapshot`、固定 `SettingsEnvironment`（GPU 不可用、空音频设备列表）、隔离的临时模型目录（`<temp>/models`，可写，使 `searchRoots()` 只剩该目录）和默认外观；不构造 `VoiceInputWindow`，不写真实 `QSettings`（语言解析允许只读 `ui/language`）。`--preview-settings` 仍是截图输出目录，`--preview-page` 使用本文固定页序，`--preview-manifest` 写 JSON manifest。
2. **页面选择接口**：设置窗暴露 `pageCount()`、`selectPage(int)` 和 `draft()`；预览和测试不再依赖 `findChild("settingsStack")` 的内部结构。
3. **静态清单与草稿往返**：清单 schema 版本为 1，字段为 `version:int`、`pages[]{id:string, controls[]{id:string, type:string, default:string, label:string, dynamic:bool}}`；`id` 是稳定设置 ID，不是 `QObject::objectName`。按 `{id,type,default,dynamic}` 比较，`label` 单独审阅，`dynamic:true` 的控件不参与相等比较；测试同时用 `draft()` 比较全部 Apply-bound 字段。设备、GPU、模型状态和命中数等动态值不写入静态基线。`--inventory` 和 `--compare` 成功返回 0，schema/读取/比较失败返回非零。
4. **热词组往返**：固定样例由当前 `save_hotwords()` 生成；无编辑往返比较整文件 SHA-256；编辑一行后比较未编辑行的字节切片。另测四字段、五字段、CRLF 和 `3.0` 输入的规范化行为，不承诺任意旧文件原样保留。
5. **翻译检查**：保持单一 `SpeakAnything` 上下文；要求 Qt LinguistTools、`lrelease` 和生成的 `.qm` 存在。`check_translations.cmake` 只检查上下文、零 `unfinished`、零 `vanished` 和 `.qm` 存在；`settings-ui-test --english-check` 加载 `build-ui/speakanything_en.qm`，以 `SPEAKANYTHING_LANG=en` 构造八页，枚举可见静态标签，出现 CJK 即失败（用户热词和模型名在允许列表内）。
6. **离屏布局检查**：使用 `QT_QPA_PLATFORM=offscreen` 构造八页，检查 760×540、焦点链、侧栏焦点、导航选中态、无页面级横向溢出和可见文字裁切；同一套断言在 `QT_SCALE_FACTOR=1.5` 下再跑一次。
7. **现有回归**：九项 `ctest` 和预览几何脚本继续通过；浮窗绘制/控制器逻辑不修改，因此不建立新的浮窗截图基线。

### 8. 文件拆分边界

`qt_ui_main.cpp` 目前 5,587 行。本轮只迁移设置对话框、模型页、热词页和它们依赖的设置类型/主题函数；`VoiceInputWindow` 及其控制器继续留在原文件。`qt_ui_main.cpp` 作为既有超限文件登记为 grandfather，本轮不得继续增加设置逻辑。

| 文件组 | 职责 | 预估行数 |
| --- | --- | ---: |
| `src/util/text_encoding.h` | UTF-8/QString 转换 | 30 |
| `src/common/app_identity.h` | 窗口标题、org/app、QSettings 作用域、互斥量和托盘文本常量 | 40 |
| `src/common/app_paths.h/.cpp` | 数据目录、模型目录、热词路径、启动项和翻译安装 | 220 |
| `src/ui/ui_strings.h` | 统一 `SpeakAnything` 翻译上下文 | 10 |
| `src/ui/settings_types.h/.cpp` | 设置值类型、枚举、默认值、名称编解码、快照/草稿/页面包 | 260 |
| `src/ui/overlay_theme.h/.cpp` | 浮窗主题、预设和颜色混合 | 250 |
| `src/ui/design_tokens.h/.cpp` | 基础尺度、语义角色、Windows QSS、macOS 角色映射 | 260 |
| `src/ui/settings/settings_layout.h/.cpp` | 卡片、行、滑杆、分段按钮等布局助手 | 220 |
| `src/ui/settings/settings_pages.h` | 页面构建器声明和 `PageBundle` | 80 |
| `src/ui/settings/pages_*.cpp` | 七个页面构建器，每个文件不超过 160 行；模型页单独实现 | 每个 ≤160 |
| `src/ui/settings/settings_dialog.h/.cpp` | 壳层、导航、页脚、快照/草稿和验证 | 380 |
| `src/ui/models_page.h/.cpp` | 模型目录和三类模型卡 | 420 |
| `src/qt_ui_main.cpp` | 保留浮窗、托盘、热键、会话、翻译、朗读、注入和入口 | grandfather |

拆分要求：

* 新文件继续使用 `Q_DECLARE_TR_FUNCTIONS(SpeakAnything)`，不创建新的翻译上下文。
* 不使用 `Q_OBJECT`；页面构建器返回 `PageBundle`，通过 `std::function` 回调通信，避免引入 AUTOMOC。
* `SettingsSnapshot` 作为输入，`SettingsDraft` 作为输出；`SettingsCallbacks` 保留外观即时回调。
* 模型页始终存在；构造 `InputSettingsDialog` 时必须传入非空 `ModelManager`，测试使用临时模型目录。
* `VoiceInputWindow`、`src/main.cpp` 和浮窗控件不拆分；`src/main.cpp` 继续是 `sensevoice-stream` 的入口。
* Windows 和 macOS 共享 `SENSEVOICE_UI_SOURCES` 变量，但该变量不包含 `qt_ui_main.cpp`；`sensevoice-ui` 单独追加入口文件，`settings-ui-test` 只链接设置相关源文件。平台文件在各自分支追加。
* `resources/sensevoice.qrc` 保持只包含图标；不新增 SVG 运行时依赖。

### 9. 平台与持久化约束

* Windows 使用本文 token 的蓝色浅色设计；macOS 使用原生控件和系统调色板，允许深色模式。
* 不修改 `QSettings("SenseVoice", "LocalDictation")` 的 org/app、键名、值类型和迁移逻辑。
* 不修改窗口标题、应用/组织名、互斥量名称或 `FindWindowW` 使用的标题；`app_identity.h` 分别保存这些常量。
* 不修改 `hotwords.tsv` 的持久化字段顺序；UI 列顺序与持久化顺序分离，按 §5 的固定映射实现。
* 不修改安装器、卸载器或模型目录布局。

### 10. 文档与索引

视觉改造完成后同步：

* `README.md` 与 `README.en.md` 的设置界面说明和源码结构。
* `docs/adr/0003-ui-design-system.md`：记录两层 token、平台分支、浮窗保持不变、grandfather 文件和拆分决策。
* `docs/ui-design-system.md`：记录 token、行变体、页面清单、对象名和验收命令。
* `docs/designs/README.md`：登记本设计文档和评审报告。
* `resources/i18n/speakanything_en.ts`：补齐触点页面英文翻译。

## Alternatives Considered

| 备选方案 | 优点 | 代价 | 决定 |
| --- | --- | --- | --- |
| 新增应用主窗口 | 最接近完整产品形态 | 新生命周期、焦点、托盘和单实例行为 | Rejected by author: 产品表面变更，超出本轮 |
| 一套全局调色板 | 规则最少 | 覆盖浮窗用户主题；破坏 macOS 深色模式 | Rejected by author: 设置窗与浮窗是两个表面 |
| 只拆四个文件 | 改动文件少 | 设置对话框仍超过 600 行 | Rejected by author: 不满足职责和行数目标 |
| 所有设置都用整行开关 | 最接近参考图 | 无法表达表格、滑杆、颜色和多操作行 | Rejected by author: 使用行变体 |
| 先改视觉，后补验证 | 更快看到变化 | 无法证明设置没有丢失，回归不可归因 | Rejected by author: 先建验证 |
| 在现有控件上直接换 QSS/调色板 | 风险最低，无文件迁移 | 保留 117 个十六进制字面量、24 个圆角和 22 参数构造；模型页与设置页继续各自维护卡片 | Rejected by author: 无法形成可测试的页面接口和统一规则 |
| 第一版加入 SVG 图标 | 更接近参考图 | 需要 `Qt6::Svg`、图像插件和打包验证 | Rejected by author: 本轮只用 `QStyle` 图标 |
| macOS 也画 Windows 浅色壳层 | 两平台外观一致 | 移除原生控件、系统调色板和深色模式 | Rejected by author: macOS 保留原生控件 |
| 220 px 侧栏但保持 680 宽 | 侧栏更宽 | 内容区约 396 px，热词表和模型行拥挤 | Rejected by author: 184 px + 760 最小宽度 |
| 把浮窗青色改成蓝色 | 视觉更统一 | 改变已发布的浮窗身份和用户预设 | Rejected by author: 浮窗身份色保留 |
| 本轮拆分浮窗控制器 | 可让 `qt_ui_main.cpp` 接近行数目标 | 2,684 行行为无测试覆盖，风险与设置改版无关 | Rejected by author: 另立 ADR 和测试 |

## Detailed Implementation Plan

### 实施顺序

1. **基线冻结**：记录 `9a66093`、九个 `ctest`、注册表键集、`hotwords.tsv` 哈希和当前八页截图；不创建提交或标签。
2. **验证层**：先给交互控件加稳定对象名，加入设置清单、草稿往返、热词组、翻译、离屏布局和预览 manifest。此阶段不改视觉。
3. **提取设置代码**：迁移 `settings_types`、`overlay_theme`、布局助手、七个页面构建器、设置对话框和模型页；`qt_ui_main.cpp` 保留浮窗和入口，只移除设置代码。
4. **Token 接入**：先写入本文精确值，生成 Windows QSS；macOS 映射到原生调色板；截图与提取前一致。
5. **设置壳层**：184 px 分组侧栏、页面标题、行结构、页脚和键盘焦点。
6. **逐页迁移**：输入/输出/VAD → 外观/翻译/朗读 → 热词 → 模型；每页独立验收。
7. **文档与翻译**：更新 README、ADR、设计索引和英文目录；最后跑完整验证。

### API 与数据边界

接口使用值类型和显式回调。以下字段是实现的唯一数据契约：

```cpp
struct SettingsSnapshot {
  VadSettings vad;
  ComputePolicy compute;
  QString shortcut;
  TextMode mode;
  HotkeyTrigger trigger;
  ResultDestination destination;
  ResultContent popup_content;
  ResultContent clipboard_content;
  int paste_delay_ms;
  int clipboard_ttl_seconds;
  bool startup_enabled;
  bool mute_while_recording;
  std::vector<HotwordEntry> hotwords;
  TranslationSettings translation;
  SpeechSettings speech;
  AppearanceSettings appearance;
  BubbleStyle bubble_style;
  ModelChoices model_choices;
};

struct SettingsLoadInputs {
  const QSettings& settings;
  const std::filesystem::path& hotwords_path;
  bool startup_enabled;
  BubbleStyle bubble_style;
};

enum class SettingsMigration { CopyBoth, MissingContent, VadProfile };
struct SettingsLoadResult {
  SettingsSnapshot snapshot;
  std::vector<SettingsMigration> migrations;
  std::string error;
};

SettingsLoadResult loadSettingsSnapshot(const SettingsLoadInputs&);
void migrateSettings(QSettings&, const SettingsLoadResult&); // production only

struct SettingsEnvironment {
  GpuCapability gpu;                            // production: detect_gpu_capability()
  std::vector<AudioOutputDevice> audio_outputs; // production: list_audio_output_devices()
};

struct SettingsDraft { // Apply-bound fields; appearance is excluded
  VadSettings vad; ComputePolicy compute; QString shortcut;
  TextMode mode; HotkeyTrigger trigger; ResultDestination destination;
  ResultContent popup_content; ResultContent clipboard_content;
  int paste_delay_ms; int clipboard_ttl_seconds;
  bool startup_enabled; bool mute_while_recording;
  std::vector<HotwordEntry> hotwords;
  TranslationSettings translation; SpeechSettings speech;
  ModelChoices model_choices;
};

struct SettingsCallbacks {
  std::function<void(const AppearanceSettings&)> appearance_changed;
  std::function<void()> restore_model_locations;
};

struct PageBundle {
  QWidget* root = nullptr;
  QWidget* content = nullptr;
  std::function<void()> refresh_dependencies;
  std::function<void()> reset_draft;
  std::function<void(SettingsDraft&)> read_into_draft;
};

struct SettingsPageDefinition {
  QString id; QString title; QString group;
  std::function<PageBundle(const SettingsSnapshot&, const SettingsCallbacks&,
                           const SettingsEnvironment&, ModelManager*)> build;
};
const std::array<SettingsPageDefinition, 8>& settingsPageDefinitions();

InputSettingsDialog(const SettingsSnapshot&, const SettingsCallbacks&,
                    const SettingsEnvironment&, ModelManager*, QWidget*);
int pageCount() const;       // always 8
void selectPage(int index);  // 0 input, 1 output, 2 appearance, 3 translation,
                             // 4 speech, 5 hotwords, 6 models, 7 VAD
SettingsDraft draft() const; // Apply-bound fields only
```

* `ModelManager*` 必须非空；预览和测试使用临时模型目录。
* `SettingsEnvironment` 由生产入口用 `detect_gpu_capability()` 和 `list_audio_output_devices()` 构造；预览和测试传入固定 fixture。页面构建器不得直接调用这两个函数。
* 预览和测试的 `ModelManager` 以 `<temp>/models` 作为 bundled 目录；该目录可写，`searchRoots()` 只返回它（legacy 目录与 bundled 同名）。测试断言 `searchRoots().size()==1`。
* 模型页由 `settings_dialog` 包装成 `PageBundle`；它只提供 `read_into_draft` 和依赖刷新，不提供页脚重置。
* `PageBundle::root` 加入 `QStackedWidget`，`content` 是滚动内层；两者由对话框和 Qt 父子关系持有，页面不删除。
* 侧栏行固定为：0 分组“语音输入”、1 输入、2 输出、3 VAD、4 分组“语音能力”、5 翻译、6 朗读、7 分组“资源”、8 热词、9 模型、10 分组“个性化”、11 外观。`selectPage()` 用 `pageToRow[0..7]` 同步 stack 和侧栏。
* `PageBundle::read_into_draft` 只读取页面控件；跨页依赖由 `InputSettingsDialog` 的协调函数统一调用，页面不持有对话框指针。
* 取消时调用 `restore_model_locations`，恢复模型目录/导入路径；删除的模型文件不恢复，保持现有语义。
* `loadSettingsSnapshot` 读取 QSettings、`hotwords.tsv`、启动状态和气泡样式；热词失败返回 `error`。迁移写只在生产路径调用 `migrateSettings`。
* 对话框不写非外观设置；调用方在 `Accepted` 后读取 `draft()` 并调用现有 `saveSettings()`。外观通过 `appearance_changed` 即时保存；`Cancel` 不写非外观设置。
* `SettingsDraft` 的成员不等于重置范围；页脚只按 §6 字段列表重置，启动、计算设备、热词和模型不重置。
* `SettingsSnapshot` 和 `SettingsDraft` 的标量成员必须有默认初始值，并提供 `defaultSettingsSnapshot()`/`defaultSettingsDraft()`；默认值与当前实现一致。字段到页面/控件的归属以 §5 各页控件列表为准；`read_into_draft` 读取控件的 `currentData()`/`text()`，不读取缓存成员，例如识别设备读 `compute_->currentData()`。
* 设置页预览直接构造 `InputSettingsDialog`，不构造 `VoiceInputWindow`。
* 浮窗预览仍使用现有 `VoiceInputWindow`，但只调用只读快照加载；迁移和 `sync()` 在 `preview_mode_` 下跳过。
* 预览参数按完整 token 解析，不使用 `contains("--preview")`；`--preview-settings` 缺少 `--preview` 或 `--preview-page` 不在 `0..7/all` 时返回非零；`--preview-settings` 与 `--preview-image`/`--preview-text`/`--preview-translation` 互斥。
* 预览输出目录用 `QDir().mkpath()` 创建；每个 `grab().save()` 和 manifest 写入都检查返回值，失败返回非零。
* 语言优先级为 `SPEAKANYTHING_LANG` > `ui/language` > 系统语言；解析 CLI 后再调用 `installUiTranslator`。

### 目标文件

| 文件 | 动作 | 变更与理由 |
| --- | --- | --- |
| `src/util/text_encoding.h` | 创建 | 统一 `QString`/UTF-8 转换，删除三个重复实现。 |
| `src/common/app_identity.h` | 创建 | 分别保存窗口标题、应用/组织名、QSettings org/app、互斥量和托盘文本；值保持不变。 |
| `src/common/app_paths.h/.cpp` | 创建 | 集中数据目录、模型目录、热词路径、启动项和翻译安装。 |
| `src/ui/ui_strings.h` | 创建 | 保留单一 `Q_DECLARE_TR_FUNCTIONS(SpeakAnything)` 上下文。 |
| `src/ui/settings_types.h/.cpp` | 创建 | 承载设置结构、默认值、快照读取/迁移、名称编解码和 `SettingsSnapshot/SettingsDraft/SettingsCallbacks`。 |
| `src/ui/design_tokens.h/.cpp` | 创建 | 定义基础尺度、语义角色、Windows QSS 和 macOS 调色板映射。 |
| `src/ui/overlay_theme.h/.cpp` | 创建 | 迁移浮窗主题预设、颜色混合、可读色和三种样式默认色。 |
| `src/ui/settings/settings_pages.h` | 创建 | 声明七个页面构建器和 `PageBundle`；模型页单独声明。 |
| `src/ui/settings/settings_layout.h/.cpp` | 创建 | 提供卡片、行、分组标题、滑杆、分段按钮和提示文本助手。 |
| `src/ui/settings/pages_input.cpp` | 创建 | 构建输入页，保留计算设备、快捷键、触发、音频和启动控件。 |
| `src/ui/settings/pages_output.cpp` | 创建 | 构建输出页，保留文本模式、去向、内容、TTL 和粘贴延迟。 |
| `src/ui/settings/pages_appearance.cpp` | 创建 | 构建外观页，保留即时生效提示、颜色、字体、停留和常驻。 |
| `src/ui/settings/pages_translation.cpp` | 创建 | 构建翻译页，保留语言、设备、分隔符和实时示例。 |
| `src/ui/settings/pages_speech.cpp` | 创建 | 构建朗读页，保留翻译依赖、设备、监听、声音和语速。 |
| `src/ui/settings/pages_hotwords.cpp` | 创建 | 构建热词工作台；列索引改为命名常量，持久化映射不变。 |
| `src/ui/settings/pages_vad.cpp` | 创建 | 构建 VAD 页，保留四个滑杆的键、范围和默认值。 |
| `src/ui/settings/settings_dialog.h/.cpp` | 创建 | 实现设置壳层、侧栏分组、页面装配、快照/草稿、校验和页脚。 |
| `src/ui/models_page.h/.cpp` | 创建 | 迁移模型页并改版；头文件前向声明 `ModelManager`，显式保存两个 handler token。 |
| `src/qt_ui_main.cpp` | 修改 | 移除设置对话框/模型页/设置助手；保留浮窗、托盘、热键、会话、翻译、朗读、注入和入口；加预览写保护；不拆分控制器；主题数学原样移出。 |
| `src/models_page.cpp` | 删除 | 迁移到 `src/ui/models_page.cpp`。 |
| `src/models_page.h` | 删除 | 迁移到 `src/ui/models_page.h`。 |
| `src/main.cpp` | 不变 | `sensevoice-stream` CLI 入口，不参与本轮。 |
| `CMakeLists.txt` | 修改 | 共享 `SENSEVOICE_UI_SOURCES`（不含入口）；新增测试目标和 allowlist；LinguistTools 改为 `REQUIRED`。 |
| `tests/settings_ui_test.cpp` | 创建 | 离屏构造设置窗，检查八页、静态清单、草稿往返、侧栏焦点、尺寸、溢出、热词组往返和三种浮窗样式主题值；预算 ≤600 行。 |
| `tests/check_translations.cmake` | 创建 | 检查单一上下文、零 `unfinished`、零 `vanished` 和 `.qm` 存在；英文运行样本由 `settings-ui-test` 负责。 |
| `tests/fixtures/settings_inventory.json` | 创建 | 设置页、稳定控件 ID、类型、标签和默认值基线；不含机器相关值。 |
| `tests/fixtures/hotwords_ui_roundtrip.tsv` | 创建 | 中文短语、别名、增强、命中和“不翻译”往返样例。 |
| `resources/i18n/speakanything_en.ts` | 修改 | 补齐输入、输出、热词、模型和导航新字符串；保持 `SpeakAnything` 上下文。 |
| `README.md` | 修改 | 更新设置界面、预览命令、源码结构和测试说明。 |
| `README.en.md` | 修改 | 同步英文说明和源码结构。 |
| `AGENTS.md` | 修改 | 把失效的 `CLAUDE.md` 指针改为实际规则入口；记录 600 行 grandfather。 |
| `docs/adr/0003-ui-design-system.md` | 创建 | 记录两层 token、平台策略、浮窗不变和文件拆分决策。 |
| `docs/ui-design-system.md` | 创建 | 记录 token 表、行变体、页面清单、对象名和验收命令。 |
| `docs/designs/README.md` | 修改 | 更新本文状态和评审报告入口；文件已存在。 |
| `docs/designs/speakanything-ui-refresh/speakanything-ui-refresh.md` | 修改 | 本文修订、矛盾核对和定稿。 |

不创建 `src/app/*`、`src/ui/overlay/*` 或 `src/ui/widgets/*`；浮窗控件和控制器继续留在 `qt_ui_main.cpp`。

### 验证计划

**构建**

```powershell
$env:QTDIR = 'E:\Programming environment\Qt\6.8.3\msvc2022_64'
# Prerequisite: Qt LinguistTools 6.8.3 (installed here with aqt qttools).
if (-not (Test-Path "$env:QTDIR\bin\lrelease.exe")) { throw 'Qt LinguistTools/lrelease is required' }
# lrelease and nvcc receive ANSI paths on Windows; build through an ASCII
# junction when the checkout path contains non-ASCII characters.
if (-not (Test-Path C:\sa)) { cmd /c mklink /J C:\sa "E:\myworkwindows\语音识别项目" }
cmake -S C:\sa\SpeakAnything -B C:\sa\SpeakAnything\build-ui -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$env:QTDIR"
cmake --build C:\sa\SpeakAnything\build-ui --config Release --target sensevoice-ui settings-ui-test release_translations --parallel 8
ctest --test-dir C:\sa\SpeakAnything\build-ui -C Release --output-on-failure
```

`settings-ui-test` 和 `translations-check` 必须在 CMake 中用 `add_test` 注册；测试工作目录设为 `${CMAKE_SOURCE_DIR}`，并设置 `QT_QPA_PLATFORM=offscreen`。Qt LinguistTools 缺失时 configure 失败，不允许静默回退中文。本机 ACP=1252，`lrelease` 会把绝对路径中的中文转成 `?` 并报 `Cannot open E:/myworkwindows/??????/...`；直接在中文路径 configure 会在 `release_translations` 失败。同一问题也会让 CUDA 编译器探测报 `c1xx C1083`，因此所有实现构建统一走 ASCII junction。经 junction 后 CMake 保留 `C:/sa/...` 路径，`lrelease` 和 nvcc 正常。2026-11-22 自验：Qt 6.8.3 `qttools`/`qttranslations` 已装入 `$env:QTDIR`，`sensevoice-ui` 的 `release_translations` 生成 `speakanything_en.qm`（227 条全部 finished、0 unfinished，单一 `SpeakAnything` 上下文）。

**设置清单、草稿与热词**

```powershell
build-ui\Release\settings-ui-test.exe --inventory build-ui\settings-inventory.json
build-ui\Release\settings-ui-test.exe --compare build-ui\settings-inventory.json tests\fixtures\settings_inventory.json
build-ui\Release\settings-ui-test.exe --hotword-roundtrip tests\fixtures\hotwords_ui_roundtrip.tsv
```

静态清单只含页面、稳定控件 ID、类型、标签和默认值，不含设备名、GPU、模型状态或命中数。测试检查八页、`SettingsDraft` 全字段往返、侧栏焦点、760×540 最小尺寸、`QScrollArea` 的 `viewport` 宽度与内容 `minimumSizeHint`、图标按钮 `accessibleName` 和英文样本。热词测试验证当前保存器的无编辑 SHA-256，以及编辑一行后未编辑行的字节切片。

**确定性预览**

```powershell
$env:SPEAKANYTHING_LANG='zh'
build-ui\Release\sensevoice-ui.exe --preview --preview-settings build-ui\ui-preview\zh --preview-page all --preview-manifest build-ui\ui-preview\zh\manifest.json
$env:SPEAKANYTHING_LANG='en'
build-ui\Release\sensevoice-ui.exe --preview --preview-settings build-ui\ui-preview\en --preview-page all --preview-manifest build-ui\ui-preview\en\manifest.json
```

设置页预览不构造 `VoiceInputWindow`，只使用固定 `SettingsSnapshot`、固定 `SettingsEnvironment`、默认外观和隔离的临时模型目录。manifest 至少含 `version:int`、`language:string`、`environment:string`（固定 `preview-fixture-v1`）、`pages[]`（`index:int`、`name:string`、`image:string`、`horizontal_scroll:int`、`elided:string[]`）和 SHA-256 截图哈希；manifest schema 版本为 1，基线从 `9a66093` 生成。输出目录不存在时先 `mkpath`；预览前后比较 `HKCU\Software\SenseVoice\LocalDictation`；截图失败、页数不符、manifest 写入失败或注册表变化必须返回非零。`loadSettingsSnapshot` 和 `migrateSettings` 用 legacy fixtures（`copy-both`、缺少 `output/content`、`vad/profile_version=0`）做迁移测试，每个 fixture 断言迁移后的具体键和值。

**翻译与几何**

```powershell
# Run from C:\sa\SpeakAnything (the ASCII junction configured above).
ctest --test-dir build-ui -C Release -R translations-check --output-on-failure
powershell -ExecutionPolicy Bypass -File tools\verify_preview_geometry.ps1 -Executable build-ui\Release\sensevoice-ui.exe
```

翻译检查必须确认 `build-ui\speakanything_en.qm` 存在；`settings-ui-test` 加载 `.qm`、以 `SPEAKANYTHING_LANG=en` 构造八页并检查可见标签无中文回退（用户热词和模型名除外）。`check_translations.cmake` 只检查上下文、计数和 `.qm` 存在。几何脚本只按现有实现检查中心漂移 ≤1.1 px；本设计不新增底边断言，也不声称已有截图比较工具。

**键盘与尺寸**

手动走查：Tab 进入侧栏，方向键/Home/End 切页，Tab 遍历当前页全部控件；焦点环可见且对比度 ≥3:1。检查 `880x740`、`760x540` 和 `QT_SCALE_FACTOR=1.5`，确认无页面级横向滚动和文字裁切；热词表允许内部横向滚动。

**文件行数**

```powershell
$changed = @(git diff --name-only --diff-filter=ACMR 9a66093 --) + @(git ls-files --others --exclude-standard)
$changed | Where-Object { $_ -match '\.(cpp|h|mm)$' -and $_ -ne 'src/qt_ui_main.cpp' } |
  ForEach-Object { [pscustomobject]@{ Lines=(Get-Content $_).Count; File=$_ } } |
  Where-Object Lines -gt 600
```

输出必须为空。`git diff 9a66093` 同时覆盖已暂存和未暂存改动，未跟踪文件由 `git ls-files --others` 补入；规则来源、扫描集合和 grandfather 清单记录在 ADR 0003。`src/qt_ui_main.cpp` 是登记的 grandfather；其他既有超限文件不在本轮修改范围，另立重构 ADR。

**失败处理**

* 设置代码迁移后截图或行为不一致：回退该文件的迁移，不继续视觉改动。
* 设置清单不一致：补齐控件或恢复原对象名，不允许更新基线掩盖缺失。
* 热词哈希不一致：检查 UI/Tsv 映射和保存器规范化，修复后再继续。
* 翻译检查失败：先确认构建路径为 ASCII（中文路径用 junction），再检查 LinguistTools 和 `release_translations`；补齐 `.ts` 和 `.qm`，不允许用中文回退通过。
* 无法在当前 Windows 环境验证 macOS：标记为未运行，不把源码评审当作运行时验证；发布前要求 macOS 构建和手动冒烟。

--------------------------------------------------------------------------------

*Drafted with EGM*



