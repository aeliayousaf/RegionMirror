#pragma once

#include "../util/Settings.h"
#include <windows.h>
#include <string>
#include <vector>

namespace rm {

class App;

class SettingsWindow {
public:
    explicit SettingsWindow(App& app);
    ~SettingsWindow();

    bool Create(HINSTANCE instance, HWND owner);
    void Destroy();
    void Show();
    void Hide();
    bool IsVisible() const;

    HWND Hwnd() const { return m_hwnd; }

    void SetMonitorList(const std::vector<std::wstring>& labels, int selectedIndex);
    void SetOutputMode(OutputMode mode);
    void SetOpacityPercent(int percent);
    int GetOpacityPercent() const;
    void SetAlwaysOnTop(bool enabled);
    bool GetAlwaysOnTop() const;
    void SetShowDebug(bool show);
    bool GetShowDebug() const;
    int GetSelectedMonitorIndex() const;
    void SetRetryEnabled(bool enabled);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateChildren();
    void Layout();
    void ApplyDarkTheme();
    void CenterOnOwner();

    App& m_app;
    HINSTANCE m_instance = nullptr;
    HWND m_owner = nullptr;
    HWND m_hwnd = nullptr;
    HFONT m_font = nullptr;
    HBRUSH m_bgBrush = nullptr;
    bool m_classRegistered = false;

    HWND m_monitorLabel = nullptr;
    HWND m_monitorCombo = nullptr;
    HWND m_modeWindowed = nullptr;
    HWND m_modeFullscreen = nullptr;
    HWND m_modeWindowedLabel = nullptr;
    HWND m_modeFullscreenLabel = nullptr;
    HWND m_opacityLabel = nullptr;
    HWND m_opacitySlider = nullptr;
    HWND m_opacityValue = nullptr;
    HWND m_alwaysOnTop = nullptr;
    HWND m_alwaysOnTopLabel = nullptr;
    HWND m_showDebug = nullptr;
    HWND m_showDebugLabel = nullptr;
    HWND m_retry = nullptr;
    HWND m_close = nullptr;
};

} // namespace rm
