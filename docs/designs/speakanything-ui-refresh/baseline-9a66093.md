# Baseline 9a66093

**Date:** 2026-11-22
**Scope:** 设置界面重构前的只读基线；不创建提交、标签或源码改动。

## 环境

| 项 | 值 |
| --- | --- |
| Commit | `9a66093ccab8df73048e2b98ed74214deef928b3` (`main`, 2026-10-21 19:57:22 +0800) |
| Build | `build-cpu-verify`，Release，MSVC 2022 x64，`SENSEVOICE_WITH_CUDA=OFF` |
| Qt | 6.8.3 (`E:\Programming environment\Qt\6.8.3\msvc2022_64`) |
| CMake | 4.4.2 |
| Run path | `E:\sa-src` junction（ASCII；build 里记录的是该绝对路径） |

## 测试

```
ctest --test-dir E:\sa-src\build-cpu-verify -C Release
```

9/9 通过，耗时 2.76 s：`windows-text-injector`、`stability-tracker`、`text-processor`、`audio-conditioner`、`compute-policy`、`fbank`、`hotword-boost`、`translator`、`model-manager`。

**注意：** 该构建的测试目标在 Release 下定义了 `NDEBUG`，`assert()` 被编译掉，因此这 9 个测试的结果不代表断言通过。验证层已加 `/UNDEBUG`；修复后的 10/10（含 `settings-types-test`）是真实验证。

## 用户状态

| 项 | 值 |
| --- | --- |
| 注册表键 | `HKCU\Software\SenseVoice\LocalDictation` |
| 预览前/后 | 内容完全一致；导出 SHA-256 `CC5A7CA5CC7CF909468BA5CE4F83A31AC720C1708B2B50D756FB1FA577F3E46F` |
| `hotwords.tsv` | 不存在于 `build-cpu-verify\Release`，无哈希可记录 |

预览启动会无条件写回同值 `vad/profile_version=5`，键内容不变；`modelspt` 目录可能被创建。

## 八页截图

命令：`QT_QPA_PLATFORM=offscreen sensevoice-ui.exe --preview --preview-settings <dir>`。当前预览忽略 `--preview-page`，一次抓取全部 8 页，尺寸均为 880×740。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-0.png` | 10015 | `5B9D2001234B48B53141076A7289650BAE06429F540F88ACB043638BC893A708` |
| `settings-1.png` | 10687 | `A80DBF0FFF48F4A779BB0527552315D374B2A3D3A6CABCC8A303A38154B54DAC` |
| `settings-2.png` | 9440 | `F8CEC4594F0577D03CE79AD52F87D8EAB6008241E248DA2C904DDCB17DFA924C` |
| `settings-3.png` | 8300 | `CBEC6291E542795062CA9F1C5DC17A5FD21773A06ED962C028E1ED3C96478291` |
| `settings-4.png` | 9273 | `4902B4015B4197E737DE0459A9B6A92D1ACC74042D439AE69502F9E100C6F4F8` |
| `settings-5.png` | 7224 | `541FB15B005AACB7247CB7252E436D761F402C529B063722EFC9265894A23509` |
| `settings-6.png` | 9447 | `AC834D9CB57E334DD5F66D647AA5F8B8E4E89635FF54A7FFA92BEFA2988DDA12` |
| `settings-7.png` | 8793 | `6A19F2B32D2AE8745D6D8AA62BD69B6916A67B318C55B4709DC126B3B422979E` |

产物与 `ctest.log`、注册表导出位于 `.codex-temp/ui-baseline-9a66093/`。

## 确定性预览（SettingsEnvironment 注入后）

`InputSettingsDialog` 改从 `SettingsEnvironment` 读取 GPU/音频清单：生产入口用 `settingsEnvironmentFromSystem()`，设置页预览用空环境。连续两次预览的 8 页哈希完全一致。

| 文件 | SHA-256 |
| --- | --- |
| `settings-0.png` | `5B9D2001234B48B53141076A7289650BAE06429F540F88ACB043638BC893A708` |
| `settings-1.png` | `66B9E85658933EFDFB092564B10510880657C19D43D343086052ECF510A7489F` |
| `settings-2.png` | `3CC72262D7E98E5D2CD16FD0A993E5F088C69C3492CC1B05EC2BBED778D4C762` |
| `settings-3.png` | `440969C08CBCB5B1B33B2BE1568AD42556ED67A7ED2558C74D5E131F9DB126EE` |
| `settings-4.png` | `2B9967B404393CB0FB646E9BE039970977D073AA6E65C61743134CD9427BE5A3` |
| `settings-5.png` | `AF2E758929B9541145A55D85736214E0F3EC8E8F3EE168C7523FD7A6AA909B65` |
| `settings-6.png` | `A62C3FD26A828A63FA2FE41F8D2C666A7BB853C84B3F2BC354E0571AE9E47F29` |
| `settings-7.png` | `8C630740E4C4DB76FF863B88DE11BA5F9105D5C5635F054820AD0D33821761CB` |

与原始基线相比：page 0 相同（本机无 GPU），page 4 变化（不再列出真实音频设备，符合确定性目标），page 6 仍随构建目录路径文本变化。预览改用 `pageCount()`/`selectPage()` 后，page 1-7 的侧栏选中态随页面移动（旧实现每页都高亮“输入”）；差异仅位于侧栏区域（page 1 的 diff bbox 为 `(34,64)-(182,112)`）。产物位于 `.codex-temp/ui-baseline-deterministic/`。

## Token retarget 基线

Windows QSS 改为读取 `sa::ui::color()`，尺寸不变；8 页全部出现预期颜色差异。抽查 page 0 的背景为 `#F5F7FB`，尺寸仍为 880×740。

| 文件 | SHA-256 |
| --- | --- |
| `settings-0.png` | `42E620C7770FD5640BF7F4F6637A3F1D14E6B2AE4E19AD7A2B286B51736B085D` |
| `settings-1.png` | `9CB3B7DC054744BEE05F34A1AAEFDDFE4AEDABBFFCD21B863E4F89D5EACF9B4C` |
| `settings-2.png` | `220487B691C5FC9CCEDB17FE136308D80FB0F72F46AB8F756D538E84EFD0D81A` |
| `settings-3.png` | `5065415604E4471D4AEB395818BB7A9DC754551AF82607021E130FD21DFED222` |
| `settings-4.png` | `810D1267DF6BC04726F97D572EB9AC8DF3AFB7AB445D438F1A8014DEAA927CF8` |
| `settings-5.png` | `616AD8F1D339704D5FE5DCED2FE8A3C4BF29B7B5F1EDC2DAC7FB41A66094B25A` |
| `settings-6.png` | `492D01A4F7962CA56FB1477E30B578FE9DA3F7F70F9B1500B16B3622AEA0D3DB` |
| `settings-7.png` | `34571EBBB0C284D23C3408ADF4FB2467EF1D2FF99C76BCD2A0455F5FC287C95C` |

产物位于 `.codex-temp/ui-baseline-tokens/`；该轮 `ctest` 12/12 通过。

## Shell 重构基线

侧栏改为 184 px 分组导航（语音输入/语音能力/资源/个性化），页面标题独立显示，侧栏行到页面索引使用显式映射。模型页状态标签改为换行、下拉框不再按最长模型名撑宽；`settings-ui-test` 增加“窗口可缩到 760”断言。

| 文件 | SHA-256 |
| --- | --- |
| `settings-0.png` | `37B132139E1B81E6093CD7812BDBD060644F0E2170F5F2CBB2438745F96CB371` |
| `settings-1.png` | `FF965985FA8C4745DFD344F92A530C20C968394AA16B81873FD44692E6CDC6E4` |
| `settings-2.png` | `1C9B5C70CAB17B6BECB51BB76F05FC6FBE7BD52DE1A936E80ABA546391D4D892` |
| `settings-3.png` | `A03C08367E1FABF122AE75C93F65A536AA637434FE843535E73D4626B2DAF3A0` |
| `settings-4.png` | `75E44935A6D23B4BF0DFB767F3A56773FCDF4C6C860373E2A52586A62082A79C` |
| `settings-5.png` | `D176411155084863BA2C2B6B805C4637EB9FB3A7B74E44CAD3694C4B457B3F22` |
| `settings-6.png` | `AB67D9A89F141275E9836E946C7CDD6C111FB62DC7D63DDBF0CBBE6A2E42297A` |
| `settings-7.png` | `C6FF0435DA3A90C5E9EA53DAB3023363FC1905C98D4ADC3DBDBA1D79D8E2B896` |

产物位于 `.codex-temp/ui-baseline-shell/`；该轮 `ctest` 12/12 通过。

## 行高与卡片排版

`addCard` 标题改为 15 px/600、说明 12 px；`addRow` 行高至少 44 px，行标签 13 px。输入、输出、朗读三页出现预期排版差异，其余页面不变。

| 文件 | SHA-256 |
| --- | --- |
| `settings-0.png` | `18DA9818CD373FCBCE8EE855D78E26C9E3B352EFDC0723721AE1D3DC362D00D0` |
| `settings-1.png` | `47852E721DCE4A7314D428648354A0052EC7D8BD809559513AECD67319F5450B` |
| `settings-4.png` | `7BFBA13FC3DCA986FD0F2E93A17AFB0263C14CCC1B4778712C324616592DF13C` |

产物位于 `.codex-temp/ui-baseline-rows/`；该轮 `ctest` 12/12 通过。

## 外观与翻译页布局迁移

外观页拆为“配色主题 / 颜色 / 字体 / 浮窗显示”四张卡，颜色、字体、字号、停留时长和常驻显示统一走 44 px 行结构；颜色行在色块旁显示十六进制值。翻译页拆为“翻译 / 语言 / 翻译设备 / 分隔符”四张卡，开启翻译改为行内 Toggle，语言、设备、分隔符使用行结构，示例与失败提示留在分隔符卡内。

稳定对象名：

* 外观：`appearance_theme_preset`、`appearance_color_background`、`appearance_color_text`、`appearance_color_accent`、`appearance_font_family`、`appearance_font_size`、`appearance_linger`、`appearance_pinned`。
* 翻译：`translation_enabled`、`translation_source`、`translation_target`、`translation_device`、`translation_separator`。

`settings-ui-test` 增加上述 13 个对象名的存在性检查，并断言两页所有 `row` 容器最小高度不小于 44 px。`ctest` 12/12 通过，耗时 1.51 s。

与 `ui-baseline-rows` 对比：page 0、1、4、5、7 零差异；page 2、page 3 为预期布局变化（bbox 均为 `(240,89)-(855,671)`）；page 6 仅有 `(465,177)-(785,200)` 内 576 像素差异，来自模型状态文本，连续两次预览结果一致，与本次改动无关。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-2.png` | 10720 | `6F3F2BBAF73D49A22893568A6CDF3F16CA71ED6FEBA00AFE43F5C186CEAD8524` |
| `settings-3.png` | 10340 | `240608E96AE49EF327AA687C696B512E6996AB60E1A25C494D3533C684A81B3E` |

产物位于 `.codex-temp/ui-baseline-appearance-translation/`；该轮 `ctest` 12/12 通过。

## 热词页 Workbench 迁移

热词页改为“热词表”卡片：保留六列表格，卡片内依次是说明、表格、空状态提示和底部操作行；底部“添加热词”为主按钮，“删除所选”为次要按钮。空表时显示“还没有热词”的可操作提示。

稳定对象名：`hotwords_table`、`hotwords_add`、`hotwords_remove`、`hotwords_empty`。

`settings-ui-test` 增加对象名存在性、六列结构、添加/删除一行以及空状态自动切换的检查。`ctest` 12/12 通过，耗时 1.31 s。

与 `ui-baseline-appearance-translation` 对比：page 0-4、6、7 零差异，仅 page 5 为预期变化。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-5.png` | 7615 | `CD50B014F0C6AB48DADD065F3AC556C29C7C2B4F34E096AD4C84A0B3E33E4687` |

产物位于 `.codex-temp/ui-baseline-hotwords/`；该轮 `ctest` 12/12 通过。

## VAD 页分组迁移

VAD 页拆为“句尾判定”和“噪声判定”两张卡：句尾静音、模型阈值在前，最低响度、底噪余量在后。每个滑杆显示名称、当前值、范围含义和一行解释；页脚仍统一恢复这四个字段，页面不单独提供恢复默认。

稳定对象名：`vad_endpoint`、`vad_threshold`、`vad_minimum_db`、`vad_snr`。

`settings-ui-test` 增加两个分组卡片、四个滑杆对象名及取值范围的检查。`ctest` 12/12 通过，耗时 1.33 s。

与 `ui-baseline-hotwords` 对比：page 0-6 零差异，仅 page 7 为预期变化。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-7.png` | 10040 | `5E1326011F5526471445D9E918957CA2098780E61D6B44E6B59922C78708227D` |

产物位于 `.codex-temp/ui-baseline-vad/`；该轮 `ctest` 12/12 通过。

## 模型页行结构收敛

模型目录卡补齐 15 px 标题、12 px 说明、44 px 路径行和 44 px 操作行；三张模型卡统一为“标题 + 短状态标记 / 说明 / 详细状态 / 模型下拉 / 操作行 / 进度条”。状态拆成短标记（自动、需下载、已下载、已内置、已导入、下载中）和详细说明，长路径不再挤压标题行。操作按钮全部隐藏时整行收起；删除按钮旁显示“删除后需重新下载”。

稳定对象名：`models_directory_label`、`models_directory_choose`、`models_directory_reset`；每种模型使用 `models_<kind>_combo`、`_state`、`_status`、`_download`、`_browser`、`_import`、`_remove`、`_progress`（`kind` 为 `recognizer`、`translator`、`voice`）。

`settings-ui-test` 增加目录控件与 24 个模型控件对象名检查，并把模型页纳入 44 px 行高断言。`ctest` 12/12 通过，耗时 1.47 s。

与 `ui-baseline-vad` 对比：page 0-5、7 零差异，仅 page 6 为预期变化。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-6.png` | 9980 | `FE71047C7A0E4EB1E489405B02B954558AB63648F4C703D1A8DC559A561F5550` |

产物位于 `.codex-temp/ui-baseline-models/`；该轮 `ctest` 12/12 通过。

## 收尾：键盘、对象名与 inventory

- 侧栏改为可聚焦：Up/Down 在页面项之间移动并跳过四个分组标题，Home/End 跳到首尾页面项，选中项左侧显示 3 px 强调条；聚焦时显示 2 px `Focus` 描边。窗口打开时焦点落在当前页第一个可交互控件，Tab 可从内容区回到侧栏。
- 补齐输入、输出、朗读页的稳定控件对象名；`modeSegment` 与主按钮改用动态属性选择器，保留原有样式。
- 浮窗纯图标按钮补 `accessibleName`：取消、开始/完成、翻译模式、翻译方向、关闭浮窗。
- 新增 `tests/fixtures/settings_inventory.json`（版本 1，8 页，68 个控件）与一致性测试，校验页面、控件 ID 和类型。
- 新增 `docs/ui-design-system.md`，记录 token、壳层、行变体、页面对象名和验收命令。

`settings-ui-test` 增加侧栏焦点/键盘、inventory 和控件类型断言。`ctest` 12/12 通过，耗时 1.28 s。

与 `ui-baseline-models` 对比：page 1-7 仅选中项左侧 3 px 强调条区域变化（`x=33-38`）；page 0 另有当前页首个下拉框的焦点描边。页面布局本身无变化。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-0.png` | 10561 | `BAF38EF234DD7520B8FF69A2FCFED43EDEC40DA1F89B8F73638138A4B80EF49B` |
| `settings-1.png` | 10863 | `7A1C4728A38F69D526942999EE589786D897779674B1B0591A6F699E719A9107` |
| `settings-2.png` | 10834 | `809B43A38C9588E90DBB9394660E116789FA8BDFF71B53508575AF8522CEBF58` |
| `settings-3.png` | 10475 | `D491EC310736304363D7850D4F13BA50078DC4791E7E28D3D5774809C9E2C3ED` |
| `settings-4.png` | 10774 | `4D8E32D55AD0999620FEFD3E46356E4D001B16B75B48009B2AB6A8D52C582624` |
| `settings-5.png` | 7739 | `87A9560899BD4C869D37DB89A96F4652D18F2A279A3004FBF8781E2FC3C2F80A` |
| `settings-6.png` | 10113 | `B533D989405C2836DC46072939015700B40EEB9A36C72955D4A1AC68F472FB6A` |
| `settings-7.png` | 10176 | `180D218F6FAAD549EC3F55CAEB7D32864343CB9F65959BD443ECAEF5F16CD825` |

产物位于 `.codex-temp/ui-baseline-finish/`；该轮 `ctest` 12/12 通过。

## 侧栏扁平导航

移除侧栏的四个分组标题（语音输入、语音能力、资源、个性化），只保留 8 个可点击页面项：输入、输出、VAD、翻译、朗读、热词、模型、外观。键盘 Up/Down 仍按页面顺序移动，Home/End 仍跳到首尾；页面映射和选中强调条不变。

`settings-ui-test` 的侧栏键盘断言同步更新。`ctest` 13/13 通过，耗时 1.59 s。

与 `ui-baseline-finish` 对比：变化集中在侧栏区域；page 0 另有首个下拉框焦点描边的生成时机差异，page 6 为环境相关模型状态。

| 文件 | 字节 | SHA-256 |
| --- | --- | --- |
| `settings-0.png` | 10329 | `2C02BB097575E976A0A2653C62342A418A05D1FBCB620A84EC6482CED5C0DF30` |
| `settings-1.png` | 10695 | `D11307CAC14D9A84617819E487DE92AEDA71A8F2DD09AE9EE3ABD3DD7EAB31FB` |
| `settings-2.png` | 10726 | `903861C8F1F2B02DD90AADBE20A931DE14EC6692AF5D1D394FDF3CCFFBD5AA22` |
| `settings-3.png` | 10212 | `919B5E6A223D5199EDF872B4A3654A6C3E61D7365E658978868D169BB97346FC` |
| `settings-4.png` | 10581 | `262D1AC55BED852B772EA97DB797475B23AEFD14BBE0665ADF3982FF004AA8B0` |
| `settings-5.png` | 7533 | `CD746B5A2ECF78EFF94ED023728F6FAD7740A3B3A842A42342EA642E68883297` |
| `settings-6.png` | 9930 | `F6E74EE9A08A1A914010E74FB47BCDDDCFB323C0B39DEAE629C7A774DDD3E2D6` |
| `settings-7.png` | 9939 | `F28F9E518B7DB67D30714E8F0955B3E8543224FE3A002C7A273B6BCCDB3815AA` |

产物位于 `.codex-temp/ui-baseline-flat-nav/`；该轮 `ctest` 13/13 通过。

## 翻译目录与检查

`lupdate` 刷新后新增 79 条字符串、移除 7 条 vanished；补齐英文后目录为 299 条消息、单一 `SpeakAnything` 上下文、0 unfinished、0 vanished。`lrelease` 生成 299 条已完成翻译的 `speakanything_en.qm`。

新增 `tests/check_translations.cmake` 并注册 `translations-check`：单一上下文、零 unfinished、零 vanished、`.qm` 非空。`settings-ui-test` 增加英文运行样本（加载 `.qm`，断言窗口标题、侧栏、启动项和热词按钮为英文）。

`ctest` 13/13 通过，耗时 1.48 s。中文界面截图与 `ui-baseline-finish` 对比除 page 6 的环境相关模型状态外完全一致。

**构建路径要求**：`lrelease` 无法在中文路径下创建 `.qm`，必须从 ASCII junction 构建：

```powershell
cmake -S C:\sa\SpeakAnything -B C:\sa\SpeakAnything\build-verify
cmake --build C:\sa\SpeakAnything\build-verify --config Release --target sensevoice-ui settings-ui-test
```
