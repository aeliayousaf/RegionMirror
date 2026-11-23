#pragma once

#include "../util/Settings.h"
#include <windows.h>
#include <string>
#include <vector>
#include <functional>

namespace rm {

class App;

struct MainWindowWidgets {
    HWND chooseSource = nullptr;
    HWND changeSource = nullptr;
    HWND selectRegion = nullptr;
    HWND editRegion = nullptr;
    HWND startMirror = nullptr;
    HWND stopMirror = nullptr;
    HWND monitorCombo = nullptr;
    HWND modeWindowed = nullptr;
    HWND modeFullscreen = nullptr;
    HWND status = nullptr;
    HWND debug = nullptr;
    HWND retry = nullptr;
    HWND preview = nullptr;
    HWND toggleDebug = nullptr;
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
    void SetMonitorList(const std::vector<std::wstring>& labels, int selectedIndex);
    void SetOutputMode(OutputMode mode);
    void SetControlsEnabled(
        bool chooseSource,
        bool changeSource,
        bool selectRegion,
        bool editRegion,
        bool startMirror,
        bool stopMirror,
        bool monitors,
        bool modes,
        bool retry);
    void ShowDebug(bool show);

    void ExcludeFromCapture(bool exclude);

    int GetSelectedMonitorIndex() const;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildren();
    void Layout();
    void ApplyDarkTheme();
    HFONT UiFont() const { return m_font; }

    App& m_app;
    HINSTANCE m_instance = nullptr;
    HWND m_hwnd = nullptr;
    MainWindowWidgets m_widgets{};
    HFONT m_font = nullptr;
    HBRUSH m_bgBrush = nullptr;
    bool m_classRegistered = false;
    bool m_showDebug = false;
};

} // namespace rm
