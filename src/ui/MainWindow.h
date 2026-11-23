#pragma once

#include <windows.h>
#include <string>

namespace rm {

class App;

struct MainWindowWidgets {
    HWND chooseSource = nullptr;
    HWND stopMirror = nullptr;
    HWND settings = nullptr;
    HWND status = nullptr;
    HWND debug = nullptr;
    HWND preview = nullptr;
};

class MainWindow {
public:
    explicit MainWindow(App& app);
    ~MainWindow();

    bool Create(HINSTANCE instance);
    void Destroy();

    HWND Hwnd() const { return m_hwnd; }
    HWND PreviewHwnd() const { return m_widgets.preview; }
    MainWindowWidgets& Widgets() { return m_widgets; }

    void SetStatusText(const std::wstring& text);
    void SetDebugText(const std::wstring& text);
    void SetSourceButtonLabel(const std::wstring& text);
    void SetControlsEnabled(bool chooseSource, bool stopMirror, bool settings);
    void ShowDebug(bool show);

    void ExcludeFromCapture(bool exclude);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildren();
    void Layout();
    void ApplyDarkTheme();

    App& m_app;
    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    MainWindowWidgets m_widgets{};
    HFONT m_font = nullptr;
    HBRUSH m_bgBrush = nullptr;
    HICON m_iconLarge = nullptr;
    HICON m_iconSmall = nullptr;
    bool m_iconsOwned = false;
    bool m_classRegistered = false;
    bool m_showDebug = false;
};

} // namespace rm
