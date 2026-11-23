#pragma once

#include "../render/GpuRenderer.h"
#include "../util/DisplayEnum.h"
#include "../util/Settings.h"
#include <windows.h>
#include <functional>

namespace rm {

class MirrorWindow {
public:
    using ClosedCallback = std::function<void()>;

    MirrorWindow();
    ~MirrorWindow();

    bool Create(HINSTANCE instance);
    void Destroy();

    bool ShowOnDisplay(const DisplayInfo& display, OutputMode mode, int cropW, int cropH);
    void Hide();
    void SetMode(OutputMode mode, const DisplayInfo& display, int cropW, int cropH);

    HWND Hwnd() const { return m_hwnd; }
    bool IsVisible() const { return m_visible; }
    OutputMode Mode() const { return m_mode; }
    SwapChainTarget& Target() { return m_target; }

    void SetClosedCallback(ClosedCallback cb) { m_onClosed = std::move(cb); }
    void SetEscapeCallback(std::function<void()> cb) { m_onEscape = std::move(cb); }

    void ExcludeFromCapture(bool exclude);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void ApplyPlacement(const DisplayInfo& display, OutputMode mode, int cropW, int cropH);

    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    bool m_visible = false;
    bool m_classRegistered = false;
    OutputMode m_mode = OutputMode::Windowed;
    SwapChainTarget m_target;
    ClosedCallback m_onClosed;
    std::function<void()> m_onEscape;
    HICON m_iconLarge = nullptr;
    HICON m_iconSmall = nullptr;
    bool m_iconsOwned = false;
};

} // namespace rm
