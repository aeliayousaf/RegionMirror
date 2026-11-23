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
    IdStopMirror = 1002,
    IdSettings = 1003,
    IdPreview = 1004,
    IdStatus = 1005,
    IdDebug = 1006,
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
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);

    if (LoadAppIcons(instance, &m_iconLarge, &m_iconSmall)) {
        m_iconsOwned = true;
    } else {
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
    const int height = 680;
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
    m_widgets.stopMirror = makeButton(IdStopMirror, L"Stop mirror");
    m_widgets.settings = makeButton(IdSettings, L"Settings");

    m_widgets.status = makeStatic(IdStatus, L"Status: Ready");
    m_widgets.debug = makeStatic(IdDebug, L"");

    m_widgets.preview = CreateWindowExW(
        0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | SS_BLACKRECT | SS_NOTIFY,
        0, 0, 10, 10, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IdPreview)), m_instance, nullptr);
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
    const int gap = 8;
    const int topBarH = btnH + margin * 2;
    const int bottomH = 56 + (m_showDebug ? 70 : 0);

    int x = margin;
    int y = margin;
    auto place = [&](HWND hwnd, int width) {
        if (hwnd) {
            SetWindowPos(hwnd, nullptr, x, y, width, btnH, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        x += width + gap;
    };

    place(m_widgets.chooseSource, 140);
    place(m_widgets.stopMirror, 130);
    place(m_widgets.settings, 110);

    const int previewTop = topBarH;
    const int previewBottom = h - bottomH;
    const int previewH = std::max(120, previewBottom - previewTop);
    SetWindowPos(m_widgets.preview, nullptr, margin, previewTop, std::max(100, w - margin * 2), previewH,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    y = previewBottom + margin;
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
    EnumChildWindows(m_hwnd, [](HWND child, LPARAM) -> BOOL {
        SetWindowTheme(child, L"DarkMode_Explorer", nullptr);
        return TRUE;
    }, 0);
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

void MainWindow::SetSourceButtonLabel(const std::wstring& text)
{
    if (m_widgets.chooseSource) {
        SetWindowTextW(m_widgets.chooseSource, text.c_str());
    }
}

void MainWindow::SetControlsEnabled(bool chooseSource, bool stopMirror, bool settings)
{
    EnableWindow(m_widgets.chooseSource, chooseSource ? TRUE : FALSE);
    EnableWindow(m_widgets.stopMirror, stopMirror ? TRUE : FALSE);
    EnableWindow(m_widgets.settings, settings ? TRUE : FALSE);
}

void MainWindow::ShowDebug(bool show)
{
    m_showDebug = show;
    Layout();
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
        if (code == BN_CLICKED || code == 0) {
            switch (id) {
            case IdChooseSource: m_app.OnChooseSource(); break;
            case IdStopMirror: m_app.OnStopMirror(); break;
            case IdSettings: m_app.OnOpenSettings(); break;
            default:
                break;
            }
        }
        return 0;
    }
    case WM_SIZE:
        Layout();
        m_app.OnDpiChanged();
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
        SetTextColor(hdc, RGB(235, 238, 245));
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

    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace rm
