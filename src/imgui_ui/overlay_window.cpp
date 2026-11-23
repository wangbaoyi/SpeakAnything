#include "imgui_ui/overlay_window.h"

#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"
#include "imgui_ui/theme.h"

#include <d3d11.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam,
                                                             LPARAM lParam);

namespace sa::imgui_ui {
namespace {

constexpr COLORREF kKeyColor = RGB(1, 2, 3);
constexpr int kWidth = 560;
constexpr int kHeight = 132;

LRESULT WINAPI overlayProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self != nullptr && self->imguiContext() != nullptr) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(self->imguiContext());
        const bool handled = ImGui_ImplWin32_WndProcHandler(window, message, wparam, lparam);
        ImGui::SetCurrentContext(previous);
        if (handled) return true;
    }
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        ShowWindow(window, SW_HIDE);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

bool OverlayWindow::create(HINSTANCE instance, const wchar_t* title) {
    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.style = CS_CLASSDC;
    window_class.lpfnWndProc = overlayProc;
    window_class.hInstance = instance;
    window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
    window_class.lpszClassName = L"SpeakAnythingImGuiOverlay";
    RegisterClassExW(&window_class);

    window_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        window_class.lpszClassName, title, WS_POPUP, 0, 0, kWidth, kHeight, nullptr, nullptr,
        instance, nullptr);
    if (window_ == nullptr) return false;
    SetWindowLongPtrW(window_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    SetLayeredWindowAttributes(window_, kKeyColor, 255, LWA_COLORKEY);

    createDevice();
    if (device_ == nullptr) {
        DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    IMGUI_CHECKVERSION();
    imgui_context_ = ImGui::CreateContext();
    ImGui::SetCurrentContext(imgui_context_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    loadFonts();
    applyStyle();
    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_, context_);

    RECT work_area = {};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    const int x = work_area.left + (work_area.right - work_area.left - kWidth) / 2;
    const int y = work_area.bottom - kHeight - 48;
    SetWindowPos(window_, HWND_TOPMOST, x, y, kWidth, kHeight, SWP_NOACTIVATE);
    ShowWindow(window_, SW_HIDE);
    return true;
}

void OverlayWindow::destroy() {
    if (window_ == nullptr) return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(imgui_context_);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    imgui_context_ = nullptr;
    ImGui::SetCurrentContext(previous);
    cleanupRenderTarget();
    if (swap_chain_ != nullptr) swap_chain_->Release();
    if (context_ != nullptr) context_->Release();
    if (device_ != nullptr) device_->Release();
    swap_chain_ = nullptr;
    context_ = nullptr;
    device_ = nullptr;
    DestroyWindow(window_);
    window_ = nullptr;
}

void OverlayWindow::createDevice() {
    DXGI_SWAP_CHAIN_DESC description = {};
    description.BufferCount = 2;
    description.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.OutputWindow = window_;
    description.SampleDesc.Count = 1;
    description.Windowed = TRUE;
    description.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                             D3D11_SDK_VERSION, &description, &swap_chain_, &device_,
                                             &level, &context_))) {
        return;
    }
    createRenderTarget();
}

void OverlayWindow::createRenderTarget() {
    ID3D11Texture2D* back_buffer = nullptr;
    swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
    if (back_buffer != nullptr) {
        device_->CreateRenderTargetView(back_buffer, nullptr, &render_target_);
        back_buffer->Release();
    }
}

void OverlayWindow::cleanupRenderTarget() {
    if (render_target_ != nullptr) render_target_->Release();
    render_target_ = nullptr;
}

void OverlayWindow::loadFonts() {
    ImGuiIO& io = ImGui::GetIO();
    const char* candidates[] = {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simhei.ttf"};
    for (const char* path : candidates) {
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        if (io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr,
                                         io.Fonts->GetGlyphRangesChineseFull()) != nullptr) {
            return;
        }
    }
    io.Fonts->AddFontDefault();
}

void OverlayWindow::applyStyle() {
    theme::applyOverlayStyle();
}

void OverlayWindow::show() {
    if (window_ == nullptr) return;
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    visible_ = true;
}

void OverlayWindow::hide() {
    if (window_ == nullptr) return;
    ShowWindow(window_, SW_HIDE);
    visible_ = false;
}

void OverlayWindow::toggle() {
    if (visible_) {
        hide();
    } else {
        show();
    }
}

void OverlayWindow::setCallbacks(std::function<void()> on_translate,
                                 std::function<void()> on_close) {
    on_translate_ = std::move(on_translate);
    on_close_ = std::move(on_close);
}

void OverlayWindow::renderFrame(float level, const std::string& status, bool listening,
                                bool translating) {
    if (!visible_) return;
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(imgui_context_);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("overlay", nullptr, flags);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImVec2 origin(0, 18);
    const ImVec2 size(io.DisplaySize.x, io.DisplaySize.y - 36);
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                        theme::u32(theme::OverlaySurface), 20.0f);
    draw->AddRect(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                  theme::u32(theme::OverlayBorder), 20.0f, 0, 1.0f);

    const float left = 24.0f;
    const float center_y = origin.y + size.y * 0.5f;
    const ImU32 green = theme::u32(theme::Success);
    const ImU32 red = theme::u32(theme::Danger);
    const ImU32 dot = listening ? red : green;
    draw->AddCircleFilled(ImVec2(left, center_y), 7.0f, dot);

    const float bars_left = left + 22.0f;
    for (int index = 0; index < 9; ++index) {
        const float height = 6.0f + 22.0f * level * ((index % 3) + 1) / 3.0f;
        const ImVec2 bar(bars_left + index * 6.0f, center_y - height * 0.5f);
        draw->AddRectFilled(bar, ImVec2(bar.x + 3.0f, bar.y + height),
                            IM_COL32(198, 208, 222, 255), 1.5f);
    }

    const ImVec2 text_pos(bars_left + 70.0f, center_y - 9.0f);
    draw->AddText(text_pos, theme::u32(theme::OverlayText), status.c_str());

    ImGui::SetCursorScreenPos(ImVec2(io.DisplaySize.x - 62.0f, center_y - 16.0f));
    const bool close_clicked =
        ImGui::InvisibleButton("##close", ImVec2(34.0f, 34.0f));
    const bool close_hovered = ImGui::IsItemHovered();
    ImGui::SetCursorScreenPos(ImVec2(io.DisplaySize.x - 212.0f, center_y - 16.0f));
    const bool translate_clicked =
        ImGui::InvisibleButton("##translate", ImVec2(124.0f, 34.0f));
    const bool translate_hovered = ImGui::IsItemHovered();

    const ImVec2 chip(io.DisplaySize.x - 190.0f, center_y - 13.0f);
    const ImVec4 chip_color = translating ? theme::Accent
                                          : (translate_hovered ? ImVec4(0.239f, 0.286f, 0.365f, 1.0f)
                                                               : ImVec4(0.173f, 0.212f, 0.278f, 1.0f));
    draw->AddRectFilled(chip, ImVec2(chip.x + 96, chip.y + 26), theme::u32(chip_color), 13.0f);
    if (translating) {
        draw->AddCircleFilled(ImVec2(chip.x + 18, chip.y + 13), 3.0f,
                              theme::u32(theme::OverlayText));
    }
    draw->AddText(ImVec2(chip.x + 30, chip.y + 5), theme::u32(theme::OverlayText), "翻译");

    const ImVec2 close(io.DisplaySize.x - 44.0f, center_y - 11.0f);
    draw->AddCircleFilled(close, 11.0f,
                          close_hovered ? IM_COL32(92, 102, 118, 255)
                                        : IM_COL32(62, 72, 86, 255));
    draw->AddText(ImVec2(close.x - 4.0f, close.y - 9.0f), theme::u32(theme::OverlayText), "×");

    if (close_clicked && on_close_) on_close_();
    if (translate_clicked && on_translate_) on_translate_();

    ImGui::End();
    ImGui::Render();
    const float clear[4] = {1.0f / 255.0f, 2.0f / 255.0f, 3.0f / 255.0f, 1.0f};
    context_->OMSetRenderTargets(1, &render_target_, nullptr);
    context_->ClearRenderTargetView(render_target_, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    swap_chain_->Present(1, 0);
    ImGui::SetCurrentContext(previous);
}

} // namespace sa::imgui_ui
