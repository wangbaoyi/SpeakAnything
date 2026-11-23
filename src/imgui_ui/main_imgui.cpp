#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <shellapi.h>

#include <QCoreApplication>
#include <QEventLoop>
#include <QString>

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_ui/overlay_window.h"
#include "imgui_ui/models_ui.h"
#include "imgui_ui/recording_controller.h"
#include "imgui_ui/settings_io.h"
#include "imgui_ui/settings_ui.h"
#include "imgui_ui/theme.h"
#include "model_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cmath>
#include <algorithm>
#include <filesystem>
#include <string>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swap_chain = nullptr;
ID3D11RenderTargetView* g_render_target = nullptr;
HWND g_window = nullptr;
bool g_running = true;
ImGuiContext* g_imgui_context = nullptr;
sa::imgui_ui::OverlayWindow g_overlay;
sa::imgui_ui::RecordingController* g_controller = nullptr;
bool g_translating = false;

constexpr UINT kTrayCallback = WM_APP + 1;
constexpr UINT kHotkeyToggleOverlay = 1;
constexpr UINT kCommandSettings = 1001;
constexpr UINT kCommandOverlay = 1002;
constexpr UINT kCommandTranslate = 1003;
constexpr UINT kCommandExit = 1004;
NOTIFYICONDATAW g_tray = {};
bool g_tray_added = false;
void createDevice(HWND window) {
    DXGI_SWAP_CHAIN_DESC description = {};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
        &description, &g_swap_chain, &g_device, &level, &g_context);
    if (FAILED(result)) return;

    ID3D11Texture2D* back_buffer = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer != nullptr) {
        g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
        back_buffer->Release();
    }
}

void cleanupDevice() {
    if (g_render_target != nullptr) g_render_target->Release();
    if (g_swap_chain != nullptr) g_swap_chain->Release();
    if (g_context != nullptr) g_context->Release();
    if (g_device != nullptr) g_device->Release();
    g_render_target = nullptr;
    g_swap_chain = nullptr;
    g_context = nullptr;
    g_device = nullptr;
}

void createRenderTarget() {
    ID3D11Texture2D* back_buffer = nullptr;
    g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer != nullptr) {
        g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
        back_buffer->Release();
    }
}

void cleanupRenderTarget() {
    if (g_render_target != nullptr) g_render_target->Release();
    g_render_target = nullptr;
}

void addTrayIcon() {
    g_tray = {};
    g_tray.cbSize = sizeof(g_tray);
    g_tray.hWnd = g_window;
    g_tray.uID = 1;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = kTrayCallback;
    g_tray.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_tray.szTip, L"SpeakAnything 语音输入", _TRUNCATE);
    g_tray_added = Shell_NotifyIconW(NIM_ADD, &g_tray) != FALSE;
}

void removeTrayIcon() {
    if (!g_tray_added) return;
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    g_tray_added = false;
}

void showTrayMenu() {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kCommandSettings, L"打开设置");
    AppendMenuW(menu, MF_STRING, kCommandOverlay, L"显示/隐藏浮窗");
    AppendMenuW(menu, MF_STRING | (g_translating ? MF_CHECKED : 0), kCommandTranslate,
                L"翻译模式");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"退出");
    POINT cursor = {};
    GetCursorPos(&cursor);
    SetForegroundWindow(g_window);
    const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, cursor.x, cursor.y, 0,
                                        g_window, nullptr);
    DestroyMenu(menu);
    switch (command) {
    case kCommandSettings:
        ShowWindow(g_window, SW_SHOW);
        SetForegroundWindow(g_window);
        break;
    case kCommandOverlay:
        g_overlay.toggle();
        break;
    case kCommandTranslate:
        g_translating = !g_translating;
        break;
    case kCommandExit:
        g_running = false;
        PostQuitMessage(0);
        break;
    default:
        break;
    }
}

LRESULT WINAPI windowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam)) return true;
    switch (message) {
    case WM_SIZE:
        if (wparam == SIZE_MINIMIZED) return 0;
        if (g_render_target != nullptr) {
            cleanupRenderTarget();
            g_swap_chain->ResizeBuffers(0, LOWORD(lparam), HIWORD(lparam),
                                        DXGI_FORMAT_UNKNOWN, 0);
            createRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_HOTKEY:
        if (wparam == kHotkeyToggleOverlay) {
            if (g_controller != nullptr && g_controller->listening()) {
                g_controller->stop();
            } else {
                g_overlay.show();
                if (g_controller != nullptr) g_controller->start();
            }
        }
        return 0;
    case kTrayCallback:
        if (lparam == WM_RBUTTONUP || lparam == WM_CONTEXTMENU) {
            showTrayMenu();
        } else if (lparam == WM_LBUTTONUP) {
            g_overlay.toggle();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {
        "C:\\Windows\\Fonts\\msyh.ttc",
        "C:\\Windows\\Fonts\\msyhbd.ttc",
        "C:\\Windows\\Fonts\\simhei.ttf",
    };
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        ImFontConfig config;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        if (io.Fonts->AddFontFromFileTTF(path, 17.0f, &config,
                                         io.Fonts->GetGlyphRangesChineseFull()) != nullptr) {
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

void applyStyle() {
    sa::imgui_ui::theme::applySettingsStyle();
}

std::filesystem::path findModelsDirectory(const std::filesystem::path& executable_dir) {
    const auto has_models = [](const std::filesystem::path& directory) {
        return std::filesystem::exists(directory / L"recognizer" / L"sensevoice-small-q8.gguf") ||
            std::filesystem::exists(directory / L"sensevoice-small-q8.gguf");
    };
    const std::filesystem::path candidates[] = {
        executable_dir / L"models",
        executable_dir / L"modelspt",
        executable_dir / L".." / L".." / L"models",
        executable_dir / L".." / L".." / L"modelspt",
        executable_dir / L".." / L".." / L".." / L"models",
        executable_dir / L".." / L".." / L".." / L"modelspt",
    };
    for (const auto& candidate : candidates) {
        if (has_models(candidate)) return candidate.lexically_normal();
    }
    const std::filesystem::path fresh = (executable_dir / L"models").lexically_normal();
    std::error_code error;
    std::filesystem::create_directories(fresh, error);
    return fresh;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int argc = 1;
    char application_name[] = "sensevoice-ui-imgui";
    char* argv[] = {application_name, nullptr};
    QCoreApplication qt_application(argc, argv);

    ImGui_ImplWin32_EnableDpiAwareness();

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = windowProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"SpeakAnythingImGui";
    RegisterClassExW(&window_class);

    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int window_width =
        std::min(900, static_cast<int>(work_area.right - work_area.left) - 80);
    const int window_height =
        std::min(760, static_cast<int>(work_area.bottom - work_area.top) - 80);
    const int window_x = work_area.left + (work_area.right - work_area.left - window_width) / 2;
    const int window_y = work_area.top + (work_area.bottom - work_area.top - window_height) / 2;
    g_window = CreateWindowW(window_class.lpszClassName, L"SpeakAnything 设置",
                             WS_OVERLAPPEDWINDOW, window_x, window_y, window_width, window_height,
                             nullptr, nullptr, instance, nullptr);
    if (g_window == nullptr) return 1;
    createDevice(g_window);
    if (g_device == nullptr) {
        DestroyWindow(g_window);
        UnregisterClassW(window_class.lpszClassName, instance);
        return 1;
    }
    ShowWindow(g_window, SW_SHOWDEFAULT);
    UpdateWindow(g_window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    loadFonts();
    applyStyle();
    ImGui_ImplWin32_Init(g_window);
    ImGui_ImplDX11_Init(g_device, g_context);
    g_imgui_context = ImGui::GetCurrentContext();

    g_overlay.create(instance, L"SpeakAnything 浮窗");
    addTrayIcon();
    RegisterHotKey(g_window, kHotkeyToggleOverlay, MOD_CONTROL | MOD_ALT, VK_SPACE);
    if (GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_SHOW_OVERLAY", nullptr, 0) > 0) {
        g_overlay.show();
    }

    sa::imgui_ui::SettingsState state;
    wchar_t executable[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, executable, MAX_PATH);
    const std::filesystem::path hotwords_path =
        std::filesystem::path(executable).parent_path() / L"hotwords.tsv";
    sa::imgui_ui::loadSettings(state, hotwords_path);
    sa::imgui_ui::initialize(state);
    wchar_t page_value[8] = {};
    if (GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_PAGE", page_value, 8) > 0) {
        state.page = std::clamp(_wtoi(page_value), 0, 7);
    }
    if (GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_TRANSLATE", nullptr, 0) > 0) {
        state.draft.translation.enabled = true;
        state.snapshot.translation.enabled = true;
    }
    if (GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_SPEAK", nullptr, 0) > 0) {
        state.draft.speech.enabled = true;
        state.snapshot.speech.enabled = true;
        if (state.draft.speech.device_id.isEmpty()) {
            const auto devices = list_audio_output_devices();
            if (!devices.empty()) {
                state.draft.speech.device_id = QString::fromStdWString(devices.front().id);
            }
        }
    }
    const std::filesystem::path models_directory =
        findModelsDirectory(std::filesystem::path(executable).parent_path());
    ModelManager model_manager(QString::fromStdWString(models_directory.wstring()));
    sa::imgui_ui::ModelsUi models_ui(model_manager, state);
    state.models_ui = &models_ui;

    const std::string recognition_language = state.language_codes.empty()
        ? std::string("zh")
        : state.language_codes[static_cast<std::size_t>(state.source_language)];
    const QString recognition_choice =
        state.draft.model_choices.recognizer.value(QString::fromStdString(recognition_language),
                                                   QStringLiteral("auto"));
    const CatalogModel* recognition_model = nullptr;
    if (recognition_choice != QStringLiteral("auto")) {
        recognition_model = find_model(recognition_choice.toStdString());
    }
    if (recognition_model == nullptr) {
        for (const CatalogModel* candidate : models_for(ModelKind::Recognizer,
                                                        recognition_language)) {
            if (model_manager.installed(*candidate)) {
                recognition_model = candidate;
                break;
            }
        }
    }
    sa::imgui_ui::ModelSelection selection;
    selection.language = recognition_language;
    if (recognition_model != nullptr) {
        const QString located = model_manager.locate(*recognition_model);
        if (!located.isEmpty()) {
            selection.recognizer = std::filesystem::path(located.toStdWString());
            selection.whisper = recognition_model->engine == ModelEngine::Whisper;
        }
    }
    const std::filesystem::path vad_modern = models_directory / L"recognizer" / L"fsmn-vad.gguf";
    selection.vad = std::filesystem::exists(vad_modern)
        ? vad_modern
        : models_directory / L"fsmn-vad.gguf";

    const std::string speech_language = state.language_codes.empty()
        ? std::string("en")
        : state.language_codes[static_cast<std::size_t>(state.target_language)];
    selection.speech_language = speech_language;
    const QString voice_choice =
        state.draft.model_choices.voice.value(QString::fromStdString(speech_language),
                                              QStringLiteral("auto"));
    const CatalogModel* voice_model = nullptr;
    if (voice_choice != QStringLiteral("auto")) {
        voice_model = find_model(voice_choice.toStdString());
    }
    if (voice_model == nullptr) {
        for (const CatalogModel* candidate : models_for(ModelKind::Voice, speech_language)) {
            if (model_manager.installed(*candidate)) {
                voice_model = candidate;
                break;
            }
        }
    }
    if (voice_model != nullptr) {
        const QString located = model_manager.locateDirectory(*voice_model);
        if (!located.isEmpty()) {
            selection.voice_model = std::filesystem::path(located.toStdWString());
            selection.voice_piper = voice_model->engine == ModelEngine::Piper;
        }
    }

    const auto llm_provider = [&model_manager, &state]() -> std::filesystem::path {
        const QString id = state.draft.model_choices.translator;
        if (id == QStringLiteral("auto")) return {};
        const CatalogModel* model = find_model(id.toStdString());
        if (model == nullptr || model->engine != ModelEngine::Llm) return {};
        const QString located = model_manager.locate(*model);
        return located.isEmpty() ? std::filesystem::path{}
                                 : std::filesystem::path(located.toStdWString());
    };
    sa::imgui_ui::RecordingController controller;
    g_controller = &controller;
    controller.initialize(state.draft, models_directory, selection, llm_provider);
    g_translating = state.draft.translation.enabled;
    g_overlay.setCallbacks(
        [&state, &controller] {
            g_translating = !g_translating;
            state.draft.translation.enabled = g_translating;
            state.snapshot.translation.enabled = g_translating;
            controller.updateSettings(state.draft);
            sa::imgui_ui::saveTranslationEnabled(g_translating);
        },
        [] {
            if (g_controller != nullptr) g_controller->stop();
            g_overlay.hide();
        });

    int smoke_frames = 0;
    wchar_t smoke_value[32] = {};
    if (GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_SMOKE", smoke_value,
                                32) > 0) {
        smoke_frames = _wtoi(smoke_value);
    }
    const bool auto_record =
        GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_AUTOSTART", nullptr, 0) > 0;
    wchar_t audio_path[512] = {};
    const bool auto_audio =
        GetEnvironmentVariableW(L"SPEAKANYTHING_IMGUI_AUDIO", audio_path, 512) > 0;
    bool auto_started = false;
    bool auto_stopped = false;
    const std::uint64_t app_start = GetTickCount64();

    int frames = 0;
    while (g_running) {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) g_running = false;
        }
        if (!g_running) break;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);

        if (IsWindowVisible(g_window)) {
            ImGui::SetCurrentContext(g_imgui_context);
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            sa::imgui_ui::draw(state);
            ImGui::Render();

            const float clear[4] = {0.96f, 0.97f, 0.98f, 1.0f};
            g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
            g_context->ClearRenderTargetView(g_render_target, clear);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            g_swap_chain->Present(1, 0);
        }

        controller.pumpInjection();
        g_translating = state.draft.translation.enabled;
        if (auto_record && !auto_started && GetTickCount64() - app_start > 2500) {
            auto_started = true;
            g_overlay.show();
            if (auto_audio) {
                std::string error;
                controller.startFile(audio_path, error);
            } else {
                controller.start();
            }
        }
        if (auto_record && auto_started && !auto_stopped &&
            GetTickCount64() - app_start > 7000) {
            auto_stopped = true;
            controller.stop();
        }
        const sa::imgui_ui::OverlaySnapshot snapshot = controller.snapshot();
        g_overlay.renderFrame(snapshot.level, snapshot.status, snapshot.listening, g_translating);

        if (state.close_requested) {
            state.close_requested = false;
            ShowWindow(g_window, SW_HIDE);
        }
        if (state.apply_requested) {
            state.apply_requested = false;
            std::string error;
            if (sa::imgui_ui::saveSettings(state, hotwords_path, &error)) {
                controller.updateSettings(state.draft);
                state.message = "已保存";
                ShowWindow(g_window, SW_HIDE);
            } else {
                state.message = "保存失败：" + error;
            }
        }
        if (smoke_frames > 0 && ++frames >= smoke_frames) g_running = false;
        if (!IsWindowVisible(g_window) && !g_overlay.visible() && smoke_frames == 0) {
            Sleep(10);
        }
    }

    UnregisterHotKey(g_window, kHotkeyToggleOverlay);
    g_controller = nullptr;
    controller.shutdown();
    removeTrayIcon();
    g_overlay.destroy();
    ImGui::SetCurrentContext(g_imgui_context);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupDevice();
    DestroyWindow(g_window);
    UnregisterClassW(window_class.lpszClassName, instance);
    return 0;
}
