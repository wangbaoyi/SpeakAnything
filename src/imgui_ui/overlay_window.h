#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <functional>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct IDXGISwapChain;
struct ImGuiContext;

namespace sa::imgui_ui {

class OverlayWindow {
public:
    bool create(HINSTANCE instance, const wchar_t* title);
    void destroy();
    void show();
    void hide();
    void toggle();
    [[nodiscard]] bool visible() const { return visible_; }
    [[nodiscard]] HWND handle() const { return window_; }
    [[nodiscard]] ImGuiContext* imguiContext() const { return imgui_context_; }

    void setCallbacks(std::function<void()> on_translate, std::function<void()> on_close);

    // Render one frame. level is 0..1, status is UTF-8.
    void renderFrame(float level, const std::string& status, bool listening, bool translating);

private:
    void createDevice();
    void createRenderTarget();
    void cleanupRenderTarget();
    void loadFonts();
    void applyStyle();

    HWND window_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGISwapChain* swap_chain_ = nullptr;
    ID3D11RenderTargetView* render_target_ = nullptr;
    ImGuiContext* imgui_context_ = nullptr;
    bool visible_ = false;
    std::function<void()> on_translate_;
    std::function<void()> on_close_;
};

} // namespace sa::imgui_ui
