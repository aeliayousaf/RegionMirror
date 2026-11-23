#include "pch.h"
#include "SettingsWindow.h"
#include "../App.h"

#include <CommCtrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

namespace rm {
namespace {

constexpr wchar_t kSettingsClass[] = L"RegionMirror.SettingsWindow";

enum ControlId : int {
    IdMonitorCombo = 2001,
    IdModeWindowed = 2002,
    IdModeFullscreen = 2003,
    IdModeWindowedLabel = 2004,
    IdModeFullscreenLabel = 2005,
    IdOpacityLabel = 2006,
    IdOpacitySlider = 2007,
    IdOpacityValue = 2008,
    IdAlwaysOnTop = 2009,
    IdAlwaysOnTopLabel = 2010,
    IdShowDebug = 2011,
    IdShowDebugLabel = 2012,
    IdRetry = 2013,
    IdClose = 2014,
};

void EnableDarkTitleBar(HWND hwnd)
{
    const BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
}

} // namespace

SettingsWindow::SettingsWindow(App& app)
    : m_app(app)
{
}

SettingsWindow::~SettingsWindow()
{
    Destroy();
}

bool SettingsWindow::Create(HINSTANCE instance, HWND owner)
{
    m_instance = instance;
    m_owner = owner;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    if (!m_classRegistered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = SettingsWindow::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hbrBackground = CreateSolidBrush(RGB(28, 30, 34));
        wc.lpszClassName = kSettingsClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        m_classRegistered = true;
        m_bgBrush = wc.hbrBackground;
    }

    m_font = CreateFontW(
        -16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kSettingsClass,
        L"Mirror settings",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        420,
        360,
        owner,
        nullptr,
        instance,
        this);

    if (!m_hwnd) {
        return false;
    }

    CreateChildren();
    ApplyDarkTheme();
    Layout();
    CenterOnOwner();
    return true;
}

void SettingsWindow::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
}

void SettingsWindow::Show()
{
    if (!m_hwnd) {
        return;
    }
    CenterOnOwner();
    ShowWindow(m_hwnd, SW_SHOW);
    SetForegroundWindow(m_hwnd);
}

void SettingsWindow::Hide()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
}

bool SettingsWindow::IsVisible() const
{
    return m_hwnd && IsWindowVisible(m_hwnd);
}

void SettingsWindow::CenterOnOwner()
{
    if (!m_hwnd) {
        return;
    }
    RECT self{};
    GetWindowRect(m_hwnd, &self);
    const int w = self.right - self.left;
    const int h = self.bottom - self.top;

    RECT owner{};
    if (m_owner && GetWindowRect(m_owner, &owner)) {
        const int ox = owner.left + ((owner.right - owner.left) - w) / 2;
        const int oy = owner.top + ((owner.bottom - owner.top) - h) / 2;
        SetWindowPos(m_hwnd, nullptr, ox, oy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void SettingsWindow::CreateChildren()
{
    auto makeStatic = [&](ControlId id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW(
            0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return h;
    };

    auto makeButton = [&](ControlId id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return h;
    };

    m_monitorLabel = makeStatic(static_cast<ControlId>(0), L"Output monitor");
    m_monitorCombo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdMonitorCombo)), m_instance, nullptr);
    SendMessageW(m_monitorCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_modeWindowed = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeWindowed)), m_instance, nullptr);
    m_modeWindowedLabel = CreateWindowExW(
        0, L"STATIC", L"Windowed",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeWindowedLabel)), m_instance, nullptr);
    m_modeFullscreen = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeFullscreen)), m_instance, nullptr);
    m_modeFullscreenLabel = CreateWindowExW(
        0, L"STATIC", L"Fullscreen",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeFullscreenLabel)), m_instance, nullptr);
    SendMessageW(m_modeWindowedLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_modeFullscreenLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_modeWindowed, BM_SETCHECK, BST_CHECKED, 0);

    m_opacityLabel = makeStatic(IdOpacityLabel, L"Opacity");
    m_opacitySlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ | TBS_TOOLTIPS,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacitySlider)), m_instance, nullptr);
    m_opacityValue = makeStatic(IdOpacityValue, L"100%");
    SendMessageW(m_opacitySlider, TBM_SETRANGEMIN, FALSE, 10);
    SendMessageW(m_opacitySlider, TBM_SETRANGEMAX, TRUE, 100);
    SendMessageW(m_opacitySlider, TBM_SETPOS, TRUE, 100);
    SendMessageW(m_opacitySlider, TBM_SETTICFREQ, 10, 0);

    m_alwaysOnTop = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdAlwaysOnTop)), m_instance, nullptr);
    m_alwaysOnTopLabel = CreateWindowExW(
        0, L"STATIC", L"Always on top",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdAlwaysOnTopLabel)), m_instance, nullptr);
    SendMessageW(m_alwaysOnTopLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_showDebug = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdShowDebug)), m_instance, nullptr);
    m_showDebugLabel = CreateWindowExW(
        0, L"STATIC", L"Show debug stats",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdShowDebugLabel)), m_instance, nullptr);
    SendMessageW(m_showDebugLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    m_retry = makeButton(IdRetry, L"Retry graphics");
    m_close = makeButton(IdClose, L"Close");
}

void SettingsWindow::Layout()
{
    if (!m_hwnd) {
        return;
    }

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right - rc.left;
    const int margin = 16;
    const int rowH = 32;
    const int gap = 12;
    const int radioSize = 22;

    int y = margin;
    int x = margin;

    SetWindowPos(m_monitorLabel, nullptr, x, y + 6, 130, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(m_monitorCombo, nullptr, x + 130, y, std::max(160, w - x - 130 - margin), 220,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    y += rowH + gap;
    x = margin;
    SetWindowPos(m_modeWindowed, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_modeWindowedLabel, nullptr, x, y + 6, 90, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    x += 98;
    SetWindowPos(m_modeFullscreen, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_modeFullscreenLabel, nullptr, x, y + 6, 100, 20, SWP_NOZORDER | SWP_NOACTIVATE);

    y += rowH + gap;
    x = margin;
    SetWindowPos(m_opacityLabel, nullptr, x, y + 6, 70, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    x += 74;
    const int valueW = 48;
    const int sliderW = std::max(120, w - x - valueW - margin - 8);
    SetWindowPos(m_opacitySlider, nullptr, x, y, sliderW, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
    x += sliderW + 8;
    SetWindowPos(m_opacityValue, nullptr, x, y + 6, valueW, 20, SWP_NOZORDER | SWP_NOACTIVATE);

    y += rowH + gap;
    x = margin;
    SetWindowPos(m_alwaysOnTop, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_alwaysOnTopLabel, nullptr, x, y + 6, 160, 20, SWP_NOZORDER | SWP_NOACTIVATE);

    y += rowH + gap;
    x = margin;
    SetWindowPos(m_showDebug, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_showDebugLabel, nullptr, x, y + 6, 180, 20, SWP_NOZORDER | SWP_NOACTIVATE);

    y += rowH + gap + 8;
    x = margin;
    SetWindowPos(m_retry, nullptr, x, y, 140, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowPos(m_close, nullptr, w - margin - 100, y, 100, rowH, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SettingsWindow::ApplyDarkTheme()
{
    EnableDarkTitleBar(m_hwnd);
    EnumChildWindows(m_hwnd, [](HWND child, LPARAM selfPtr) -> BOOL {
        auto* self = reinterpret_cast<SettingsWindow*>(selfPtr);
        if (child == self->m_modeWindowedLabel ||
            child == self->m_modeFullscreenLabel ||
            child == self->m_modeWindowed ||
            child == self->m_modeFullscreen ||
            child == self->m_opacityLabel ||
            child == self->m_opacityValue ||
            child == self->m_alwaysOnTop ||
            child == self->m_alwaysOnTopLabel ||
            child == self->m_showDebug ||
            child == self->m_showDebugLabel ||
            child == self->m_monitorLabel) {
            SetWindowTheme(child, L"", L"");
            return TRUE;
        }
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
}

void SettingsWindow::SetMonitorList(const std::vector<std::wstring>& labels, int selectedIndex)
{
    if (!m_monitorCombo) {
        return;
    }
    SendMessageW(m_monitorCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& label : labels) {
        SendMessageW(m_monitorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!labels.empty()) {
        const int idx = std::clamp(selectedIndex, 0, static_cast<int>(labels.size()) - 1);
        SendMessageW(m_monitorCombo, CB_SETCURSEL, idx, 0);
    }
}

void SettingsWindow::SetOutputMode(OutputMode mode)
{
    SendMessageW(m_modeWindowed, BM_SETCHECK, mode == OutputMode::Windowed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_modeFullscreen, BM_SETCHECK, mode == OutputMode::Fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
}

void SettingsWindow::SetOpacityPercent(int percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (m_opacitySlider) {
        SendMessageW(m_opacitySlider, TBM_SETPOS, TRUE, percent);
    }
    if (m_opacityValue) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d%%", percent);
        SetWindowTextW(m_opacityValue, buf);
    }
}

int SettingsWindow::GetOpacityPercent() const
{
    if (!m_opacitySlider) {
        return 100;
    }
    return static_cast<int>(SendMessageW(m_opacitySlider, TBM_GETPOS, 0, 0));
}

void SettingsWindow::SetAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop) {
        SendMessageW(m_alwaysOnTop, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool SettingsWindow::GetAlwaysOnTop() const
{
    if (!m_alwaysOnTop) {
        return false;
    }
    return SendMessageW(m_alwaysOnTop, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SettingsWindow::SetShowDebug(bool show)
{
    if (m_showDebug) {
        SendMessageW(m_showDebug, BM_SETCHECK, show ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool SettingsWindow::GetShowDebug() const
{
    if (!m_showDebug) {
        return false;
    }
    return SendMessageW(m_showDebug, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

int SettingsWindow::GetSelectedMonitorIndex() const
{
    return static_cast<int>(SendMessageW(m_monitorCombo, CB_GETCURSEL, 0, 0));
}

void SettingsWindow::SetRetryEnabled(bool enabled)
{
    if (m_retry) {
        EnableWindow(m_retry, enabled ? TRUE : FALSE);
    }
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    SettingsWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<SettingsWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT SettingsWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == BN_CLICKED || code == CBN_SELCHANGE || code == 0) {
            switch (id) {
            case IdMonitorCombo:
                if (code == CBN_SELCHANGE) {
                    m_app.OnOutputMonitorChanged(GetSelectedMonitorIndex());
                }
                break;
            case IdModeWindowed:
            case IdModeWindowedLabel:
                SendMessageW(m_modeWindowed, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(m_modeFullscreen, BM_SETCHECK, BST_UNCHECKED, 0);
                m_app.OnOutputModeChanged(OutputMode::Windowed);
                break;
            case IdModeFullscreen:
            case IdModeFullscreenLabel:
                SendMessageW(m_modeFullscreen, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(m_modeWindowed, BM_SETCHECK, BST_UNCHECKED, 0);
                m_app.OnOutputModeChanged(OutputMode::Fullscreen);
                break;
            case IdAlwaysOnTop:
            case IdAlwaysOnTopLabel: {
                if (id == IdAlwaysOnTopLabel) {
                    SetAlwaysOnTop(!GetAlwaysOnTop());
                }
                m_app.OnAlwaysOnTopChanged(GetAlwaysOnTop());
                break;
            }
            case IdShowDebug:
            case IdShowDebugLabel: {
                if (id == IdShowDebugLabel) {
                    SetShowDebug(!GetShowDebug());
                }
                m_app.OnShowDebugChanged(GetShowDebug());
                break;
            }
            case IdRetry:
                m_app.OnRetryGraphics();
                break;
            case IdClose:
                Hide();
                break;
            default:
                break;
            }
        }
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == m_opacitySlider) {
            m_app.OnOpacityChanged(GetOpacityPercent());
            return 0;
        }
        break;
    case WM_SIZE:
        Layout();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == m_modeWindowedLabel ||
            control == m_modeFullscreenLabel ||
            control == m_opacityLabel ||
            control == m_opacityValue ||
            control == m_alwaysOnTopLabel ||
            control == m_showDebugLabel ||
            control == m_monitorLabel) {
            SetTextColor(hdc, RGB(196, 200, 208));
        } else {
            SetTextColor(hdc, RGB(235, 238, 245));
        }
        SetBkColor(hdc, RGB(28, 30, 34));
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(m_bgBrush ? m_bgBrush : GetStockObject(DKGRAY_BRUSH));
    }
    case WM_CLOSE:
        Hide();
        return 0;
    case WM_DESTROY:
        m_hwnd = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace rm
