#include "pch.h"
#include "MirrorWindow.h"

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
    if (!m_classRegistered) {
        WNDCLASSEXW wc{ sizeof(wc) };
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = MirrorWindow::WndProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kMirrorClass;
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }
        m_classRegistered = true;
    }

    // Owner is set later via SetWindowLongPtr if needed. Avoid WS_EX_NOREDIRECTIONBITMAP
    // unless using DirectComposition; classic HWND swap chains present more reliably without it.
    m_hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kMirrorClass,
        L"Region Mirror Output",
        WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX,
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

    ExcludeFromCapture(true);
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
    // Avoid stealing focus where practical.
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    SetWindowPos(m_hwnd, HWND_TOP, 0, 0, 0, 0,
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

void MirrorWindow::ApplyPlacement(const DisplayInfo& display, OutputMode mode, int cropW, int cropH)
{
    m_mode = mode;
    if (!m_hwnd) {
        return;
    }

    if (mode == OutputMode::Fullscreen) {
        const LONG_PTR style = WS_POPUP;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);
        SetWindowPos(
            m_hwnd,
            HWND_TOP,
            display.bounds.left,
            display.bounds.top,
            display.bounds.right - display.bounds.left,
            display.bounds.bottom - display.bounds.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        const LONG_PTR style = WS_POPUP | WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX;
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style);

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

        const int x = display.workArea.left + (workW - winW) / 2;
        const int y = display.workArea.top + (workH - winH) / 2;

        SetWindowPos(
            m_hwnd,
            HWND_TOP,
            x,
            y,
            winW,
            winH,
            SWP_FRAMECHANGED | SWP_NOACTIVATE);
    }
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
