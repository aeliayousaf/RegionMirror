#include "pch.h"
#include "MainWindow.h"
#include "../App.h"
#include "../util/AppIcon.h"

#include <CommCtrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Dwmapi.lib")
#pragma comment(lib, "UxTheme.lib")

namespace rm {
namespace {

constexpr wchar_t kMainClass[] = L"RegionMirror.MainWindow";

enum ControlId : int {
    IdChooseSource = 1001,
    IdChangeSource = 1002,
    IdSelectRegion = 1003,
    IdEditRegion = 1004,
    IdStartMirror = 1005,
    IdStopMirror = 1006,
    IdMonitorCombo = 1007,
    IdModeWindowed = 1008,
    IdModeFullscreen = 1009,
    IdRetry = 1010,
    IdToggleDebug = 1011,
    IdPreview = 1012,
    IdStatus = 1013,
    IdDebug = 1014,
    IdModeWindowedLabel = 1015,
    IdModeFullscreenLabel = 1016,
    IdOpacityLabel = 1017,
    IdOpacitySlider = 1018,
    IdOpacityValue = 1019,
};

void SetExcludeFromCapture(HWND hwnd, bool exclude)
{
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    SetWindowDisplayAffinity(hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

void EnableDarkTitleBar(HWND hwnd)
{
    const BOOL useDark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
}

} // namespace

MainWindow::MainWindow(App& app)
    : m_app(app)
{
}

MainWindow::~MainWindow()
{
    Destroy();
}

bool MainWindow::Create(HINSTANCE instance)
{
    m_instance = instance;
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    if (LoadAppIcons(instance, &m_iconLarge, &m_iconSmall)) {
        m_iconsOwned = true;
    } else {
        // Keep going with the default application icon if the resource is missing.
        m_iconLarge = LoadIconW(nullptr, IDI_APPLICATION);
        m_iconSmall = m_iconLarge;
        m_iconsOwned = false;
    }

    if (!m_classRegistered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MainWindow::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = m_iconLarge ? m_iconLarge : LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = m_iconSmall ? m_iconSmall : wc.hIcon;
        wc.hbrBackground = CreateSolidBrush(RGB(28, 30, 34));
        wc.lpszClassName = kMainClass;
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

    const int width = 960;
    const int height = 720;
    // Do not use WS_EX_NOREDIRECTIONBITMAP on the control window: the swap chain
    // lives on the preview child, and without a redirection bitmap the whole UI
    // appears as a transparent hole.
    m_hwnd = CreateWindowExW(
        0,
        kMainClass,
        L"Region Mirror",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        width,
        height,
        nullptr,
        nullptr,
        instance,
        this);

    if (!m_hwnd) {
        return false;
    }

    ApplyWindowIcons(m_hwnd, m_iconLarge, m_iconSmall);
    CreateChildren();
    ApplyDarkTheme();
    ExcludeFromCapture(true);
    Layout();
    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);
    return true;
}

void MainWindow::Destroy()
{
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    if (m_font) {
        DeleteObject(m_font);
        m_font = nullptr;
    }
    if (m_iconsOwned) {
        if (m_iconLarge) {
            DestroyIcon(m_iconLarge);
        }
        if (m_iconSmall && m_iconSmall != m_iconLarge) {
            DestroyIcon(m_iconSmall);
        }
    }
    m_iconLarge = nullptr;
    m_iconSmall = nullptr;
    m_iconsOwned = false;
    // brush owned by class; leave it
}

void MainWindow::ExcludeFromCapture(bool exclude)
{
    if (m_hwnd) {
        SetExcludeFromCapture(m_hwnd, exclude);
    }
    if (m_widgets.preview) {
        SetExcludeFromCapture(m_widgets.preview, exclude);
    }
}

void MainWindow::CreateChildren()
{
    auto makeButton = [&](ControlId id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return h;
    };

    auto makeStatic = [&](ControlId id, const wchar_t* text) -> HWND {
        HWND h = CreateWindowExW(
            0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), m_instance, nullptr);
        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        return h;
    };

    m_widgets.chooseSource = makeButton(IdChooseSource, L"Choose source");
    m_widgets.changeSource = makeButton(IdChangeSource, L"Change source");
    m_widgets.selectRegion = makeButton(IdSelectRegion, L"Select region");
    m_widgets.editRegion = makeButton(IdEditRegion, L"Edit region");

    m_widgets.status = makeStatic(IdStatus, L"Status: Ready");
    m_widgets.debug = makeStatic(IdDebug, L"");

    m_widgets.preview = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_BLACKRECT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdPreview)), m_instance, nullptr);

    HWND monitorLabel = makeStatic(static_cast<ControlId>(0), L"Output monitor");
    SetWindowLongPtrW(monitorLabel, GWLP_ID, 0);

    m_widgets.monitorCombo = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdMonitorCombo)), m_instance, nullptr);
    SendMessageW(m_widgets.monitorCombo, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);

    // Radio circles only; labels are STATIC so light-grey text is reliable under dark UI.
    m_widgets.modeWindowed = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeWindowed)), m_instance, nullptr);
    m_widgets.modeWindowedLabel = CreateWindowExW(
        0, L"STATIC", L"Windowed",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeWindowedLabel)), m_instance, nullptr);
    m_widgets.modeFullscreen = CreateWindowExW(
        0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeFullscreen)), m_instance, nullptr);
    m_widgets.modeFullscreenLabel = CreateWindowExW(
        0, L"STATIC", L"Fullscreen",
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdModeFullscreenLabel)), m_instance, nullptr);
    SendMessageW(m_widgets.modeWindowedLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_widgets.modeFullscreenLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_widgets.modeWindowed, BM_SETCHECK, BST_CHECKED, 0);

    m_widgets.opacityLabel = CreateWindowExW(
        0, L"STATIC", L"Opacity",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacityLabel)), m_instance, nullptr);
    m_widgets.opacitySlider = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | TBS_AUTOTICKS | TBS_HORZ | TBS_TOOLTIPS,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacitySlider)), m_instance, nullptr);
    m_widgets.opacityValue = CreateWindowExW(
        0, L"STATIC", L"100%",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdOpacityValue)), m_instance, nullptr);
    SendMessageW(m_widgets.opacityLabel, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_widgets.opacityValue, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
    SendMessageW(m_widgets.opacitySlider, TBM_SETRANGEMIN, FALSE, 10);
    SendMessageW(m_widgets.opacitySlider, TBM_SETRANGEMAX, TRUE, 100);
    SendMessageW(m_widgets.opacitySlider, TBM_SETPOS, TRUE, 100);
    SendMessageW(m_widgets.opacitySlider, TBM_SETTICFREQ, 10, 0);

    m_widgets.startMirror = makeButton(IdStartMirror, L"Start mirror");
    m_widgets.stopMirror = makeButton(IdStopMirror, L"Stop mirror");
    m_widgets.retry = makeButton(IdRetry, L"Retry graphics");
    m_widgets.toggleDebug = makeButton(IdToggleDebug, L"Debug");

    // Store label hwnd via GWLP_USERDATA unused; layout uses fixed positions.
    SetWindowLongPtrW(monitorLabel, GWLP_USERDATA, 1);
}

void MainWindow::Layout()
{
    if (!m_hwnd) {
        return;
    }

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int margin = 12;
    const int btnH = 32;
    const int btnW = 140;
    const int gap = 8;
    const int topBarH = btnH + margin * 2;
    const int bottomH = 160 + (m_showDebug ? 70 : 0);

    int x = margin;
    int y = margin;
    auto place = [&](HWND hwnd, int width) {
        if (hwnd) {
            SetWindowPos(hwnd, nullptr, x, y, width, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        x += width + gap;
    };

    place(m_widgets.chooseSource, btnW);
    place(m_widgets.changeSource, btnW);
    place(m_widgets.selectRegion, btnW);
    place(m_widgets.editRegion, btnW);
    place(m_widgets.toggleDebug, 90);

    const int previewTop = topBarH;
    const int previewBottom = h - bottomH;
    const int previewH = std::max(120, previewBottom - previewTop);
    SetWindowPos(m_widgets.preview, nullptr, margin, previewTop, std::max(100, w - margin * 2), previewH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    y = previewBottom + margin;
    x = margin;
    // Monitor label as static near combo — find child with USERDATA 1
    HWND child = GetWindow(m_hwnd, GW_CHILD);
    while (child) {
        if (GetWindowLongPtrW(child, GWLP_USERDATA) == 1) {
            SetWindowPos(child, nullptr, x, y + 6, 130, 20, SWP_NOZORDER | SWP_NOACTIVATE);
            break;
        }
        child = GetWindow(child, GW_HWNDNEXT);
    }
    x = margin + 130;
    SetWindowPos(m_widgets.monitorCombo, nullptr, x, y, std::max(200, w - x - margin), 220,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    y += btnH + gap;
    x = margin;
    const int radioSize = 22;
    const int labelW = 88;
    SetWindowPos(m_widgets.modeWindowed, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_widgets.modeWindowedLabel, nullptr, x, y + 6, labelW, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    x += labelW + gap;
    SetWindowPos(m_widgets.modeFullscreen, nullptr, x, y + 5, radioSize, radioSize, SWP_NOZORDER | SWP_NOACTIVATE);
    x += radioSize + 4;
    SetWindowPos(m_widgets.modeFullscreenLabel, nullptr, x, y + 6, labelW, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    x += labelW + gap;
    place(m_widgets.startMirror, btnW);
    place(m_widgets.stopMirror, btnW);
    place(m_widgets.retry, 140);

    y += btnH + gap;
    x = margin;
    SetWindowPos(m_widgets.opacityLabel, nullptr, x, y + 6, 70, 20, SWP_NOZORDER | SWP_NOACTIVATE);
    x += 74;
    const int valueW = 48;
    const int sliderW = std::max(120, w - x - valueW - margin - gap);
    SetWindowPos(m_widgets.opacitySlider, nullptr, x, y, sliderW, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
    x += sliderW + gap;
    SetWindowPos(m_widgets.opacityValue, nullptr, x, y + 6, valueW, 20, SWP_NOZORDER | SWP_NOACTIVATE);

    y += btnH + gap;
    SetWindowPos(m_widgets.status, nullptr, margin, y, std::max(100, w - margin * 2), 40,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (m_showDebug) {
        y += 44;
        ShowWindow(m_widgets.debug, SW_SHOW);
        SetWindowPos(m_widgets.debug, nullptr, margin, y, std::max(100, w - margin * 2), 60,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
        ShowWindow(m_widgets.debug, SW_HIDE);
    }
}

void MainWindow::ApplyDarkTheme()
{
    EnableDarkTitleBar(m_hwnd);
    // Prefer dark explorer theme for common controls where available.
    EnumChildWindows(m_hwnd, [](HWND child, LPARAM selfPtr) -> BOOL {
        auto* self = reinterpret_cast<MainWindow*>(selfPtr);
        // Keep mode labels unthemed so WM_CTLCOLORSTATIC light-grey text sticks.
        if (child == self->m_widgets.modeWindowedLabel ||
            child == self->m_widgets.modeFullscreenLabel ||
            child == self->m_widgets.modeWindowed ||
            child == self->m_widgets.modeFullscreen ||
            child == self->m_widgets.opacityLabel ||
            child == self->m_widgets.opacityValue) {
            SetWindowTheme(child, L"", L"");
            return TRUE;
        }
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        return TRUE;
    }, reinterpret_cast<LPARAM>(this));
}

void MainWindow::SetStatusText(const std::wstring& text)
{
    if (m_widgets.status) {
        SetWindowTextW(m_widgets.status, text.c_str());
    }
}

void MainWindow::SetDebugText(const std::wstring& text)
{
    if (m_widgets.debug) {
        SetWindowTextW(m_widgets.debug, text.c_str());
    }
}

void MainWindow::SetMonitorList(const std::vector<std::wstring>& labels, int selectedIndex)
{
    if (!m_widgets.monitorCombo) {
        return;
    }
    SendMessageW(m_widgets.monitorCombo, CB_RESETCONTENT, 0, 0);
    for (const auto& label : labels) {
        SendMessageW(m_widgets.monitorCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    if (!labels.empty()) {
        const int idx = std::clamp(selectedIndex, 0, static_cast<int>(labels.size()) - 1);
        SendMessageW(m_widgets.monitorCombo, CB_SETCURSEL, idx, 0);
    }
}

void MainWindow::SetOutputMode(OutputMode mode)
{
    SendMessageW(m_widgets.modeWindowed, BM_SETCHECK, mode == OutputMode::Windowed ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(m_widgets.modeFullscreen, BM_SETCHECK, mode == OutputMode::Fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
}

void MainWindow::SetOpacityPercent(int percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (m_widgets.opacitySlider) {
        SendMessageW(m_widgets.opacitySlider, TBM_SETPOS, TRUE, percent);
    }
    if (m_widgets.opacityValue) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d%%", percent);
        SetWindowTextW(m_widgets.opacityValue, buf);
    }
}

int MainWindow::GetOpacityPercent() const
{
    if (!m_widgets.opacitySlider) {
        return 100;
    }
    return static_cast<int>(SendMessageW(m_widgets.opacitySlider, TBM_GETPOS, 0, 0));
}

void MainWindow::SetControlsEnabled(
    bool chooseSource,
    bool changeSource,
    bool selectRegion,
    bool editRegion,
    bool startMirror,
    bool stopMirror,
    bool monitors,
    bool modes,
    bool retry)
{
    EnableWindow(m_widgets.chooseSource, chooseSource ? TRUE : FALSE);
    EnableWindow(m_widgets.changeSource, changeSource ? TRUE : FALSE);
    EnableWindow(m_widgets.selectRegion, selectRegion ? TRUE : FALSE);
    EnableWindow(m_widgets.editRegion, editRegion ? TRUE : FALSE);
    EnableWindow(m_widgets.startMirror, startMirror ? TRUE : FALSE);
    EnableWindow(m_widgets.stopMirror, stopMirror ? TRUE : FALSE);
    EnableWindow(m_widgets.monitorCombo, monitors ? TRUE : FALSE);
    EnableWindow(m_widgets.modeWindowed, modes ? TRUE : FALSE);
    EnableWindow(m_widgets.modeFullscreen, modes ? TRUE : FALSE);
    EnableWindow(m_widgets.modeWindowedLabel, modes ? TRUE : FALSE);
    EnableWindow(m_widgets.modeFullscreenLabel, modes ? TRUE : FALSE);
    EnableWindow(m_widgets.opacityLabel, TRUE);
    EnableWindow(m_widgets.opacitySlider, TRUE);
    EnableWindow(m_widgets.opacityValue, TRUE);
    EnableWindow(m_widgets.retry, retry ? TRUE : FALSE);
}

void MainWindow::ShowDebug(bool show)
{
    m_showDebug = show;
    Layout();
}

int MainWindow::GetSelectedMonitorIndex() const
{
    return static_cast<int>(SendMessageW(m_widgets.monitorCombo, CB_GETCURSEL, 0, 0));
}

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MainWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MainWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (code == BN_CLICKED || code == CBN_SELCHANGE || code == 0) {
            switch (id) {
            case IdChooseSource: m_app.OnChooseSource(); break;
            case IdChangeSource: m_app.OnChangeSource(); break;
            case IdSelectRegion: m_app.OnSelectRegion(); break;
            case IdEditRegion: m_app.OnEditRegion(); break;
            case IdStartMirror: m_app.OnStartMirror(); break;
            case IdStopMirror: m_app.OnStopMirror(); break;
            case IdRetry: m_app.OnRetryGraphics(); break;
            case IdToggleDebug: m_app.OnToggleDebug(); break;
            case IdMonitorCombo:
                if (code == CBN_SELCHANGE) {
                    m_app.OnOutputMonitorChanged(GetSelectedMonitorIndex());
                }
                break;
            case IdModeWindowed:
            case IdModeWindowedLabel:
                SendMessageW(m_widgets.modeWindowed, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(m_widgets.modeFullscreen, BM_SETCHECK, BST_UNCHECKED, 0);
                m_app.OnOutputModeChanged(OutputMode::Windowed);
                break;
            case IdModeFullscreen:
            case IdModeFullscreenLabel:
                SendMessageW(m_widgets.modeFullscreen, BM_SETCHECK, BST_CHECKED, 0);
                SendMessageW(m_widgets.modeWindowed, BM_SETCHECK, BST_UNCHECKED, 0);
                m_app.OnOutputModeChanged(OutputMode::Fullscreen);
                break;
            default:
                break;
            }
        }
        return 0;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == m_widgets.opacitySlider) {
            m_app.OnOpacityChanged(GetOpacityPercent());
            return 0;
        }
        break;
    case WM_SIZE:
        Layout();
        m_app.OnDpiChanged(); // also used as resize signal for swapchain
        return 0;
    case WM_DPICHANGED: {
        const RECT* suggested = reinterpret_cast<RECT*>(lParam);
        if (suggested) {
            SetWindowPos(m_hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        m_app.OnDpiChanged();
        return 0;
    }
    case WM_DISPLAYCHANGE:
        m_app.OnDisplayChange();
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == m_widgets.modeWindowedLabel ||
            control == m_widgets.modeFullscreenLabel ||
            control == m_widgets.opacityLabel ||
            control == m_widgets.opacityValue) {
            SetTextColor(hdc, RGB(196, 200, 208)); // light grey
        } else {
            SetTextColor(hdc, RGB(235, 238, 245));
        }
        SetBkColor(hdc, RGB(28, 30, 34));
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(m_bgBrush ? m_bgBrush : GetStockObject(DKGRAY_BRUSH));
    }
    case App::WM_APP_FRAME:
        m_app.RenderTick();
        return 0;
    case App::WM_APP_SOURCE_CLOSED:
        m_app.OnCaptureClosed();
        return 0;
    case App::WM_APP_MIRROR_CLOSED:
        m_app.OnMirrorClosed();
        return 0;
    case App::WM_APP_SOURCE_PICKED:
        m_app.OnSourcePicked();
        return 0;
    case App::WM_APP_SOURCE_PICK_FAILED:
        m_app.OnSourcePickFailed();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (m_app.OnPreviewKeyEscape()) {
                return 0;
            }
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }

    // Preview mouse routing: if message is for preview child, parent still gets some via subclassing.
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace rm
