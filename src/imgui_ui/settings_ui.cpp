#include "imgui_ui/settings_ui.h"
#include "imgui_ui/theme.h"
#include "imgui_ui/models_ui.h"

#include "languages.h"

#include "imgui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cfloat>
#include <cstring>

using namespace sa::settings;

namespace sa::imgui_ui {
namespace {

std::string utf8FromWide(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                         static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), size, nullptr, nullptr);
    return result;
}

const char* kPageTitles[8] = {"输入", "输出", "外观", "翻译", "朗读", "热词", "模型", "VAD"};

bool inputText(const char* label, std::string& value, std::size_t capacity = 256) {
    char buffer[512] = {};
    const std::size_t size = std::min(value.size(), capacity - 1);
    std::memcpy(buffer, value.data(), size);
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        value.assign(buffer);
        return true;
    }
    return false;
}

bool comboStrings(const char* label, int* current, const std::vector<std::string>& items) {
    if (items.empty()) return false;
    *current = std::clamp(*current, 0, static_cast<int>(items.size()) - 1);
    bool changed = false;
    if (ImGui::BeginCombo(label, items[static_cast<std::size_t>(*current)].c_str())) {
        for (int index = 0; index < static_cast<int>(items.size()); ++index) {
            const bool selected = index == *current;
            if (ImGui::Selectable(items[static_cast<std::size_t>(index)].c_str(), selected)) {
                *current = index;
                changed = true;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool comboCompute(const char* label, ComputePolicy* policy) {
    static const char* items[] = {"自动（优先 GPU）", "仅 CPU", "仅 GPU"};
    int current = static_cast<int>(*policy);
    if (ImGui::Combo(label, &current, items, 3)) {
        *policy = static_cast<ComputePolicy>(current);
        return true;
    }
    return false;
}

constexpr float kLabelWidth = 130.0f;

std::string hiddenId(const char* label) {
    return std::string("##") + label;
}

void rowLabel(const char* label) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::SameLine(kLabelWidth);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

bool rowComboCompute(const char* label, ComputePolicy* policy) {
    rowLabel(label);
    const std::string id = hiddenId(label);
    return comboCompute(id.c_str(), policy);
}

bool rowComboStrings(const char* label, int* current, const std::vector<std::string>& items) {
    rowLabel(label);
    const std::string id = hiddenId(label);
    return comboStrings(id.c_str(), current, items);
}

bool rowCombo(const char* label, int* current, const char* const items[], int count) {
    rowLabel(label);
    const std::string id = hiddenId(label);
    return ImGui::Combo(id.c_str(), current, items, count);
}

bool rowSliderInt(const char* label, int* value, int minimum, int maximum) {
    rowLabel(label);
    const std::string id = hiddenId(label);
    return ImGui::SliderInt(id.c_str(), value, minimum, maximum);
}

bool rowInputText(const char* label, std::string& value, std::size_t capacity = 256) {
    rowLabel(label);
    const std::string id = hiddenId(label);
    return inputText(id.c_str(), value, capacity);
}

void cardTitle(const char* text) {
    ImGui::TextColored(theme::TextPrimary, "%s", text);
    ImGui::Separator();
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, theme::TextMuted);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void beginCard(const char* id) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Surface);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Border);
    ImGui::BeginChild(id, ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
}

void endCard() {
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);
    ImGui::Spacing();
}

void beginGrid(const char* id, int columns) { ImGui::BeginTable(id, columns, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings); ImGui::TableNextColumn(); }
#define nextCell() ImGui::TableNextColumn()
void endGrid() { ImGui::EndTable(); ImGui::Spacing(); }

void drawInputPage(SettingsState& state) {
    beginGrid("input_grid", 2);
    beginCard("input_compute_card");
    cardTitle("计算设备");
    hint("自动在 GPU 可用时使用 GPU，否则使用 CPU。更改设备后重新加载识别模型。");
    rowComboCompute("识别设备", &state.draft.compute);
    endCard();

    nextCell();
    beginCard("input_shortcut_card");
    cardTitle("快捷键");
    std::string shortcut = state.draft.shortcut.toStdString();
    if (rowInputText("组合键", shortcut, 64)) {
        state.draft.shortcut = QString::fromStdString(shortcut);
        state.dirty = true;
    }
    hint("示例：Ctrl+Alt+Space。录入后按“应用”生效。");
    endCard();

    nextCell();
    beginCard("input_trigger_card");
    cardTitle("触发方式");
    static const char* triggers[] = {"按住说话", "按一下切换", "实时模式"};
    int trigger = static_cast<int>(state.draft.trigger);
    if (rowCombo("触发方式", &trigger, triggers, 3)) {
        state.draft.trigger = static_cast<HotkeyTrigger>(trigger);
        state.dirty = true;
    }
    endCard();

    nextCell();
    beginCard("input_audio_card");
    cardTitle("音频与启动");
    if (ImGui::Checkbox("录音时静音系统播放", &state.draft.mute_while_recording)) {
        state.dirty = true;
    }
    if (ImGui::Checkbox("开机自动启动", &state.draft.startup_enabled)) {
        state.dirty = true;
    }
    endCard();
    endGrid();
}

void drawOutputPage(SettingsState& state) {
    beginCard("output_mode_card");
    cardTitle("文字整理");
    int mode = static_cast<int>(state.draft.mode);
    if (ImGui::RadioButton("原文", &mode, 0)) {
        state.draft.mode = static_cast<TextMode>(mode);
        state.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("精简", &mode, 1)) {
        state.draft.mode = static_cast<TextMode>(mode);
        state.dirty = true;
    }
    hint("精简会去掉口头语和重复，原文保留识别出的每个字。");
    endCard();

    beginCard("output_result_card");
    cardTitle("识别结果");
    static const char* destinations[] = {"输入到光标位置", "只复制到剪贴板"};
    int destination = static_cast<int>(state.draft.destination);
    if (rowCombo("去向", &destination, destinations, 2)) {
        state.draft.destination = static_cast<ResultDestination>(destination);
        state.dirty = true;
    }
    static const char* contents[] = {"原文", "译文", "原文 + 译文"};
    int popup = static_cast<int>(state.draft.popup_content);
    if (rowCombo("浮窗内容", &popup, contents, 3)) {
        state.draft.popup_content = static_cast<ResultContent>(popup);
        state.dirty = true;
    }
    int clipboard = static_cast<int>(state.draft.clipboard_content);
    if (rowCombo("剪贴板内容", &clipboard, contents, 3)) {
        state.draft.clipboard_content = static_cast<ResultContent>(clipboard);
        state.dirty = true;
    }
    if (rowSliderInt("剪贴板保留（秒）", &state.draft.clipboard_ttl_seconds, 0, 600)) {
        state.dirty = true;
    }
    endCard();

    beginCard("output_paste_card");
    cardTitle("粘贴兜底");
    if (rowSliderInt("粘贴延迟（ms）", &state.draft.paste_delay_ms, 0, maximum_paste_delay_ms)) {
        state.dirty = true;
    }
    hint("目标程序不支持直接输入时，等待这段时间后自动粘贴。");
    endCard();
}

void markAppearance(SettingsState& state) { state.dirty = true; if (state.appearance_changed) state.appearance_changed(); }

void drawAppearancePage(SettingsState& state) {
    hint("外观修改即时生效，关闭窗口不会撤回。");
    beginGrid("appearance_grid", 2);
    AppearanceSettings& appearance = state.snapshot.appearance;
    beginCard("appearance_theme_card");
    cardTitle("配色主题");
    static const char* themes[] = {"默认", "亮色", "暗色", "高对比度", "自定义"};
    int preset = static_cast<int>(appearance.preset);
    if (rowCombo("主题", &preset, themes, 5)) {
        appearance.preset = static_cast<ThemePreset>(preset);
        if (appearance.preset == ThemePreset::Custom) {
            appearance.custom.background = QColor::fromRgbF(theme::Background.x, theme::Background.y, theme::Background.z);
            appearance.custom.text = QColor::fromRgbF(theme::TextPrimary.x, theme::TextPrimary.y, theme::TextPrimary.z);
            appearance.custom.accent = QColor::fromRgbF(theme::Accent.x, theme::Accent.y, theme::Accent.z);
        }
        markAppearance(state);
    }
    endCard();

    nextCell();
    beginCard("appearance_color_card");
    cardTitle("自定义颜色");
    hint("仅在“自定义”主题下生效；选择预设主题时使用预设配色。");
    float background[3] = {appearance.custom.background.redF(), appearance.custom.background.greenF(),
                           appearance.custom.background.blueF()};
    float text[3] = {appearance.custom.text.redF(), appearance.custom.text.greenF(),
                     appearance.custom.text.blueF()};
    float accent[3] = {appearance.custom.accent.redF(), appearance.custom.accent.greenF(),
                       appearance.custom.accent.blueF()};
    bool colors_changed = false;
    rowLabel("背景");
    colors_changed |= ImGui::ColorEdit3("##背景", background);
    rowLabel("文字");
    colors_changed |= ImGui::ColorEdit3("##文字", text);
    rowLabel("强调色");
    colors_changed |= ImGui::ColorEdit3("##强调色", accent);
    if (colors_changed) {
        appearance.preset = ThemePreset::Custom;
        appearance.custom.background = QColor::fromRgbF(background[0], background[1], background[2]);
        appearance.custom.text = QColor::fromRgbF(text[0], text[1], text[2]);
        appearance.custom.accent = QColor::fromRgbF(accent[0], accent[1], accent[2]);
        markAppearance(state);
    }
    endCard();

    nextCell();
    beginCard("appearance_font_card");
    cardTitle("字体");
    std::string family = appearance.custom_font_family.toStdString();
    if (rowInputText("字体", family, 128)) {
        appearance.custom_font_family = QString::fromStdString(family);
        appearance.preset = ThemePreset::Custom;
        markAppearance(state);
    }
    if (rowSliderInt("字号", &appearance.custom_font_size, 8, 24)) {
        appearance.preset = ThemePreset::Custom;
        markAppearance(state);
    }
    endCard();

    nextCell();
    beginCard("appearance_display_card");
    cardTitle("浮窗显示");
    int linger = appearance.linger_ms;
    if (rowSliderInt("停留时长（ms）", &linger, 0, linger_maximum_ms)) {
        appearance.linger_ms = (linger / linger_step_ms) * linger_step_ms;
        markAppearance(state);
    }
    if (ImGui::Checkbox("常驻显示", &appearance.pinned)) {
        markAppearance(state);
    }
    hint("常驻显示时，唤出后不自动隐藏，点浮窗上的 × 关闭。");
    endCard();
    endGrid();
}

void drawTranslationPage(SettingsState& state) {
    TranslationSettings& translation = state.draft.translation;
    beginGrid("translation_grid", 2);
    beginCard("translation_mode_card");
    cardTitle("翻译");
    if (ImGui::Checkbox("开启翻译模式", &translation.enabled)) state.dirty = true;
    hint("开启后，原文和译文会一起写入光标位置。");
    endCard();

    nextCell();
    beginCard("translation_language_card");
    cardTitle("语言");
    rowComboStrings("我说的语言", &state.source_language, state.language_labels);
    rowComboStrings("翻译成", &state.target_language, state.language_labels);
    if (!state.language_codes.empty()) {
        translation.direction.source = state.language_codes[static_cast<std::size_t>(state.source_language)];
        translation.direction.target = state.language_codes[static_cast<std::size_t>(state.target_language)];
    }
    endCard();

    nextCell();
    beginCard("translation_device_card");
    cardTitle("翻译设备");
    rowComboCompute("计算设备", &translation.device);
    hint("中英互译在 GPU 上使用 Opus-MT CUDA，其他语言使用 Qwen。");
    endCard();

    nextCell();
    beginCard("translation_separator_card");
    cardTitle("分隔符");
    std::string separator = translation.separator.toStdString();
    if (rowInputText("分隔符", separator, 16)) {
        translation.separator = QString::fromStdString(separator);
        state.dirty = true;
    }
    ImGui::Text("示例：今天开会。%sWe have a meeting today.",
                translation.separator.toUtf8().constData());
    endCard();
    endGrid();
}

void drawSpeechPage(SettingsState& state) {
    SpeechSettings& speech = state.draft.speech;
    beginGrid("speech_grid", 2);
    beginCard("speech_mode_card");
    cardTitle("朗读");
    if (ImGui::Checkbox("开启朗读", &speech.enabled)) state.dirty = true;
    if (ImGui::Checkbox("边说边朗读", &speech.live)) state.dirty = true;
    endCard();

    nextCell();
    beginCard("speech_device_card");
    cardTitle("朗读设备");
    rowComboStrings("设备", &state.speech_device, state.speech_devices);
    if (ImGui::Checkbox("监听：同时在自己的默认播放设备上播放", &speech.monitor)) {
        state.dirty = true;
    }
    endCard();

    nextCell();
    beginCard("speech_voice_card");
    cardTitle("声音与语速");
    static const char* voices[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    int zh = std::clamp(speech.voice_zh, 0, 9);
    int en = std::clamp(speech.voice_en, 0, 9);
    if (rowCombo("中文声音", &zh, voices, 10)) {
        speech.voice_zh = zh;
        state.dirty = true;
    }
    if (rowCombo("英文声音", &en, voices, 10)) {
        speech.voice_en = en;
        state.dirty = true;
    }
    if (rowSliderInt("语速（%）", &speech.speed_percent, 50, 200)) state.dirty = true;
    endCard();
    endGrid();
}

void drawHotwordPage(SettingsState& state) {
    beginCard("hotword_table_card");
    cardTitle("热词表");
    hint("每行一个热词；别名用 | 分隔。");
    if (ImGui::BeginTable("hotwords", 6,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("启用");
        ImGui::TableSetupColumn("标准词");
        ImGui::TableSetupColumn("别名");
        ImGui::TableSetupColumn("增强");
        ImGui::TableSetupColumn("命中");
        ImGui::TableSetupColumn("不翻译");
        ImGui::TableHeadersRow();

        for (int row = 0; row < static_cast<int>(state.draft.hotwords.size()); ++row) {
            HotwordEntry& entry = state.draft.hotwords[static_cast<std::size_t>(row)];
            ImGui::PushID(row);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Checkbox("##enabled", &entry.enabled)) state.dirty = true;
            ImGui::TableSetColumnIndex(1);
            std::string phrase = entry.phrase;
            if (inputText("##phrase", phrase, 128)) {
                entry.phrase = phrase;
                state.dirty = true;
            }
            ImGui::TableSetColumnIndex(2);
            std::string aliases;
            for (std::size_t index = 0; index < entry.aliases.size(); ++index) {
                if (index != 0) aliases += '|';
                aliases += entry.aliases[index];
            }
            if (inputText("##aliases", aliases, 256)) {
                entry.aliases.clear();
                std::size_t start = 0;
                while (start <= aliases.size()) {
                    const std::size_t end = aliases.find('|', start);
                    const std::string part = aliases.substr(
                        start, end == std::string::npos ? std::string::npos : end - start);
                    if (!part.empty()) entry.aliases.push_back(part);
                    if (end == std::string::npos) break;
                    start = end + 1;
                }
                state.dirty = true;
            }
            ImGui::TableSetColumnIndex(3);
            if (ImGui::DragFloat("##boost", &entry.boost, 0.1f, 0.0f, 12.0f, "%.1f")) {
                state.dirty = true;
            }
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%llu", static_cast<unsigned long long>(entry.hits));
            ImGui::TableSetColumnIndex(5);
            if (ImGui::Checkbox("##keep", &entry.keep_untranslated)) state.dirty = true;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("添加热词")) {
        state.draft.hotwords.emplace_back();
        state.selected_hotword = static_cast<int>(state.draft.hotwords.size()) - 1;
        state.dirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("删除最后一行") && !state.draft.hotwords.empty()) {
        state.draft.hotwords.pop_back();
        state.dirty = true;
    }
    endCard();
}

void drawVadPage(SettingsState& state) {
    VadSettings& vad = state.draft.vad;
    beginGrid("vad_grid", 2);
    beginCard("vad_sentence_card");
    cardTitle("句尾判定");
    rowSliderInt("句尾静音（ms）", &vad.endpoint_ms, 300, 2000);
    hint("检测到多长的停顿后结束当前句子。");
    rowSliderInt("模型阈值（%）", &vad.threshold_percent, 5, 95);
    hint("越低越容易判定语音结束。");
    endCard();

    nextCell();
    beginCard("vad_noise_card");
    cardTitle("噪声判定");
    rowSliderInt("最低响度（dBFS）", &vad.minimum_db, -80, -20);
    hint("低于这个响度的声音按静音处理。");
    rowSliderInt("底噪余量（dB）", &vad.snr_db, 0, 20);
    hint("高于底噪多少分贝才算人声。");
    endCard();
    endGrid();
}

void drawPage(SettingsState& state) {
    switch (state.page) {
    case 0: drawInputPage(state); break;
    case 1: drawOutputPage(state); break;
    case 2: drawAppearancePage(state); break;
    case 3: drawTranslationPage(state); break;
    case 4: drawSpeechPage(state); break;
    case 5: drawHotwordPage(state); break;
    case 6:
        if (state.models_ui != nullptr) state.models_ui->draw();
        break;
    case 7: drawVadPage(state); break;
    default: break;
    }
}

} // namespace

void initialize(SettingsState& state) {
    if (state.draft.shortcut.isEmpty()) {
        state.snapshot = defaultSettingsSnapshot();
        state.draft = defaultSettingsDraft();
    }
    state.language_labels.clear();
    state.language_codes.clear();
    for (const LanguageInfo& language : supported_languages()) {
        std::string label(language.native_name);
        if (language.native_name != language.english_name) {
            label += " · ";
            label += language.english_name;
        }
        state.language_labels.push_back(std::move(label));
        state.language_codes.emplace_back(language.code);
    }
    state.source_language = 0;
    state.target_language = 0;
    for (int index = 0; index < static_cast<int>(state.language_codes.size()); ++index) {
        if (state.language_codes[static_cast<std::size_t>(index)] ==
            state.draft.translation.direction.source) {
            state.source_language = index;
        }
        if (state.language_codes[static_cast<std::size_t>(index)] ==
            state.draft.translation.direction.target) {
            state.target_language = index;
        }
    }
    state.speech_devices.clear();
    state.speech_devices.push_back("默认播放设备");
    for (const AudioOutputDevice& device : list_audio_output_devices()) {
        state.speech_devices.push_back(utf8FromWide(device.name));
    }
}

void draw(SettingsState& state) {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("SpeakAnything", nullptr, flags);

    ImGui::TextColored(theme::TextPrimary, "语音输入设置");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme::Sidebar);
    ImGui::PushStyleColor(ImGuiCol_Border, theme::Border);
    ImGui::BeginChild("nav", ImVec2(184, -96), ImGuiChildFlags_Borders);
    ImDrawList* nav_draw = ImGui::GetWindowDrawList();
    for (int index = 0; index < 8; ++index) {
        const bool selected = state.page == index;
        if (selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.816f, 0.871f, 1.0f));
        if (ImGui::Selectable(kPageTitles[index], selected)) {
            state.page = index;
        }
        if (selected) ImGui::PopStyleColor();
        if (selected) {
            const ImVec2 minimum = ImGui::GetItemRectMin();
            const ImVec2 maximum = ImGui::GetItemRectMax();
            nav_draw->AddRectFilled(ImVec2(minimum.x + 2.0f, minimum.y + 8.0f),
                                    ImVec2(minimum.x + 5.0f, maximum.y - 8.0f),
                                    theme::u32(theme::Accent), 2.0f);
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    ImGui::BeginChild("content", ImVec2(0, -96));
    ImGui::TextColored(theme::TextPrimary, "%s", kPageTitles[state.page]);
    ImGui::Separator();
    ImGui::BeginChild("scroll", ImVec2(0, -68));
    drawPage(state);
    ImGui::EndChild();
    ImGui::Separator();
    if (!state.message.empty()) {
        ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f), "%s", state.message.c_str());
    }
    ImGui::PushStyleColor(ImGuiCol_Button, theme::Accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme::AccentHover);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button("应用")) {
        state.dirty = false;
        state.apply_requested = true;
    }
    ImGui::PopStyleColor(4);
    ImGui::SameLine();
    if (ImGui::Button("取消")) {
        state.close_requested = true;
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace sa::imgui_ui
