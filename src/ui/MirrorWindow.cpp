#include "pch.h"
#include "MirrorWindow.h"
#include "../util/AppIcon.h"

#include <windowsx.h>
#include <algorithm>

namespace rm {
namespace {

constexpr wchar_t kMirrorClass[] = L"RegionMirror.OutputWindow";

void SetExcludeFromCapture(HWND hwnd, bool exclude)
{
    // WDA_EXCLUDEFROMCAPTURE = 0x00000011 (Windows 10 2004+)
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif
    SetWindowDisplayAffinity(hwnd, exclude ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

} // namespace

MirrorWindow::MirrorWindow() = default;

MirrorWindow::~MirrorWindow()
{
    Destroy();
}

bool MirrorWindow::Create(HINSTANCE instance)
{
    m_instance = instance;
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
        wc.lpfnWndProc = MirrorWindow::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = m_iconLarge ? m_iconLarge : LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = m_iconSmall ? m_iconSmall : wc.hIcon;
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kMirrorClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        m_classRegistered = true;
    }

    // Borderless popup: no title bar / caption / close button. Stop via the main window.
    // WS_EX_TOOLWINDOW keeps the output out of the taskbar / Alt+Tab by default.
    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kMirrorClass,
        L"Region Mirror Output",
        WS_POPUP,
        0,
        0,
        640,
        480,
        nullptr,
        nullptr,
        instance,
        this);

    if (!m_hwnd) {
        return false;
    }

    ApplyWindowIcons(m_hwnd, m_iconLarge, m_iconSmall);
    ExcludeFromCapture(true);
    SetOpacityPercent(m_opacityPercent);
    // Ensure the output window never appears until Start mirror.
    SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void MirrorWindow::Destroy()
{
    m_target.Destroy();
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    m_visible = false;
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

void MirrorWindow::ExcludeFromCapture(bool exclude)
{
    if (m_hwnd) {
        SetExcludeFromCapture(m_hwnd, exclude);
    }
}

bool MirrorWindow::ShowOnDisplay(const DisplayInfo& display, OutputMode mode, int cropW, int cropH)
{
    if (!m_hwnd) {
        return false;
    }
    ApplyPlacement(display, mode, cropW, cropH);
    SetOpacityPercent(m_opacityPercent);
    SetAlwaysOnTop(m_alwaysOnTop);
    // Avoid stealing focus where practical.
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(
        m_hwnd,
        m_alwaysOnTop ? HWND_TOPMOST : HWND_TOP,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    m_visible = true;
    return true;
}

void MirrorWindow::Hide()
{
    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_HIDE);
    }
    m_visible = false;
}

void MirrorWindow::SetMode(OutputMode mode, const DisplayInfo& display, int cropW, int cropH)
{
    ApplyPlacement(display, mode, cropW, cropH);
}

void MirrorWindow::SetAlwaysOnTop(bool enabled)
{
    m_alwaysOnTop = enabled;
    if (!m_hwnd) {
        return;
    }
    SetWindowPos(
        m_hwnd,
        enabled ? HWND_TOPMOST : HWND_NOTOPMOST,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void MirrorWindow::SetOpacityPercent(int percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    m_opacityPercent = percent;
    if (!m_hwnd) {
        return;
    }

    // Constant per-window alpha works with HWND swap chains via layered style.
    LONG_PTR ex = GetWindowLongPtrW(m_hwnd, GWL_EXSTYLE);
    if ((ex & WS_EX_LAYERED) == 0) {
        SetWindowLongPtrW(m_hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    const BYTE alpha = static_cast<BYTE>((percent * 255 + 50) / 100);
    SetLayeredWindowAttributes(m_hwnd, 0, alpha, LWA_ALPHA);
}

void MirrorWindow::ApplyPlacement(const DisplayInfo& display, OutputMode mode, int cropW, int cropH)
{
    m_mode = mode;
    if (!m_hwnd) {
        return;
    }

    // Borderless popup. Keep WS_VISIBLE if we are already showing — clearing it hides the window.
    LONG_PTR style = WS_POPUP;
    if (m_visible || IsWindowVisible(m_hwnd)) {
        style |= WS_VISIBLE;
    }
    SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);

    if (mode == OutputMode::Fullscreen) {
        const HWND insertAfter = m_alwaysOnTop ? HWND_TOPMOST : HWND_TOP;
        SetWindowPos(
            m_hwnd,
            insertAfter,
            display.bounds.left,
            display.bounds.top,
            display.bounds.right - display.bounds.left,
            display.bounds.bottom - display.bounds.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        const int workW = display.workArea.right - display.workArea.left;
        const int workH = display.workArea.bottom - display.workArea.top;

        int winW = workW * 2 / 3;
        int winH = workH * 2 / 3;
        if (cropW > 0 && cropH > 0) {
            const double aspect = static_cast<double>(cropW) / static_cast<double>(cropH);
            winH = static_cast<int>(winW / aspect);
            if (winH > workH * 2 / 3) {
                winH = workH * 2 / 3;
                winW = static_cast<int>(winH * aspect);
            }
        }
        winW = std::max(320, std::min(winW, workW - 40));
        winH = std::max(240, std::min(winH, workH - 40));

        int x = display.workArea.left + (workW - winW) / 2;
        int y = display.workArea.top + (workH - winH) / 2;
        // When already visible (live crop edit), keep the current center instead of jumping.
        if (m_visible) {
            RECT rc{};
            if (GetWindowRect(m_hwnd, &rc)) {
                const int cx = (rc.left + rc.right) / 2;
                const int cy = (rc.top + rc.bottom) / 2;
                x = cx - winW / 2;
                y = cy - winH / 2;
                x = std::clamp(x, static_cast<int>(display.workArea.left),
                               static_cast<int>(display.workArea.right) - winW);
                y = std::clamp(y, static_cast<int>(display.workArea.top),
                               static_cast<int>(display.workArea.bottom) - winH);
            }
        }

        const HWND insertAfter = m_alwaysOnTop ? HWND_TOPMOST : HWND_TOP;
        SetWindowPos(
            m_hwnd,
            insertAfter,
            x,
            y,
            winW,
            winH,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
}

int MirrorWindow::ResizeBorderPx() const
{
    UINT dpi = 96;
    if (m_hwnd) {
        dpi = GetDpiForWindow(m_hwnd);
    }
    // ~8px at 96 DPI — wide enough to grab without a visible frame.
    return std::max(6, MulDiv(8, static_cast<int>(dpi), 96));
}

LRESULT MirrorWindow::HitTestBorderless(LPARAM lParam) const
{
    if (!m_hwnd || m_mode == OutputMode::Fullscreen) {
        return HTCLIENT;
    }

    POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    POINT client = pt;
    ScreenToClient(m_hwnd, &client);

    const int border = ResizeBorderPx();
    const bool onLeft = client.x < border;
    const bool onRight = client.x >= (rc.right - border);
    const bool onTop = client.y < border;
    const bool onBottom = client.y >= (rc.bottom - border);

    if (onTop && onLeft) {
        return HTTOPLEFT;
    }
    if (onTop && onRight) {
        return HTTOPRIGHT;
    }
    if (onBottom && onLeft) {
        return HTBOTTOMLEFT;
    }
    if (onBottom && onRight) {
        return HTBOTTOMRIGHT;
    }
    if (onLeft) {
        return HTLEFT;
    }
    if (onRight) {
        return HTRIGHT;
    }
    if (onTop) {
        return HTTOP;
    }
    if (onBottom) {
        return HTBOTTOM;
    }

    // Drag by clicking anywhere in the content area.
    return HTCAPTION;
}

LRESULT CALLBACK MirrorWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    MirrorWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<MirrorWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    } else {
        self = reinterpret_cast<MirrorWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) {
        return self->HandleMessage(msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MirrorWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
    case WM_NCHITTEST:
        return HitTestBorderless(lParam);
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        const int border = ResizeBorderPx() * 2;
        mmi->ptMinTrackSize.x = 320 + border;
        mmi->ptMinTrackSize.y = 180 + border;
        return 0;
    }
    case WM_NCLBUTTONDBLCLK:
        // Borderless content uses HTCAPTION for drag; block caption double-click maximize.
        return 0;
    case WM_SYSCOMMAND: {
        const WPARAM cmd = wParam & 0xFFF0;
        if (cmd == SC_MAXIMIZE || cmd == SC_MINIMIZE) {
            return 0;
        }
        break;
    }
    case WM_SIZE:
        if (m_onResize && m_visible) {
            m_onResize();
        }
        return 0;
    case WM_CLOSE:
        Hide();
        if (m_onClosed) {
            m_onClosed();
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            if (m_mode == OutputMode::Fullscreen && m_onEscape) {
                m_onEscape();
            }
            return 0;
        }
        break;
    case WM_DESTROY:
        m_hwnd = nullptr;
        m_visible = false;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(m_hwnd, msg, wParam, lParam);
}

} // namespace rm
