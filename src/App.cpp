#include "pch.h"
#include "App.h"
#include "util/UiDispatcher.h"
#include "util/CrashLog.h"

#include <windowsx.h>
#include <CommCtrl.h>
#include <windows.graphics.capture.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <ShObjIdl.h>

#include <sstream>
#include <algorithm>

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "CoreMessaging.lib")

namespace rm {
namespace {

LRESULT CALLBACK PreviewSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                     UINT_PTR /*id*/, DWORD_PTR data)
{
    auto* app = reinterpret_cast<App*>(data);
    switch (msg) {
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app->OnPreviewLButtonDown(pt);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app->OnPreviewLButtonUp(pt);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        app->OnPreviewMouseMove(pt);
        return 0;
    }
    case WM_CAPTURECHANGED:
        app->OnPreviewCaptureLost();
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE && app->OnPreviewKeyEscape()) {
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (app->IsSelectingRegion()) {
            SetCursor(LoadCursorW(nullptr, IDC_CROSS));
            return TRUE;
        }
        break;
    default:
        break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

RECT ClientRect(HWND hwnd)
{
    RECT rc{};
    GetClientRect(hwnd, &rc);
    return rc;
}

} // namespace

App::App(HINSTANCE instance)
    : m_instance(instance)
    , m_main(*this)
    , m_capture(m_d3d, m_diagnostics)
{
}

App::~App()
{
    Shutdown();
}

int App::Run()
{
    winrt::init_apartment(winrt::apartment_type::single_threaded);

    try {
        m_dispatcherController = CreateUiDispatcherQueueController();
        CrashLog(L"DispatcherQueue created");
    } catch (winrt::hresult_error const& ex) {
        CrashLog(std::wstring(L"DispatcherQueue failed: ") + std::wstring(ex.message()));
        MessageBoxW(nullptr,
                    L"Failed to create the UI DispatcherQueue required for Windows Graphics Capture.",
                    L"Region Mirror",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    if (!Initialize()) {
        MessageBoxW(nullptr,
                    L"Region Mirror failed to start. See the status text after relaunch, or check that "
                    L"Windows.Graphics.Capture and Direct3D 11 are available.",
                    L"Region Mirror",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Shutdown();
    return static_cast<int>(msg.wParam);
}

bool App::Initialize()
{
    m_settings = SettingsStore::Load();
    m_captureSupported = CaptureSession::IsCaptureSupported();

    if (!m_main.Create(m_instance)) {
        return false;
    }
    // Output window is created lazily on Start mirror so launch only shows the control UI.

    if (!SetWindowSubclass(m_main.PreviewHwnd(), PreviewSubclassProc, 1, reinterpret_cast<DWORD_PTR>(this))) {
        m_error = L"Failed to attach preview input handler.";
    }

    if (!EnsureGraphics()) {
        m_error = L"Direct3D 11 initialization failed. Click Retry graphics.";
    }

    if (!m_captureSupported) {
        m_error = L"Windows Graphics Capture is not supported on this system.";
    }

    RefreshMonitorList(true);
    m_main.SetOutputMode(m_settings.outputMode);
    m_main.SetOpacityPercent(m_settings.opacityPercent);
    m_main.ShowDebug(m_settings.showDebug);

    m_status = m_captureSupported ? L"Choose a source to begin." : m_error;
    UpdateControlState();
    UpdateStatus();
    return true;
}

void App::Shutdown()
{
    OnStopMirror();
    m_capture.Stop();
    if (m_main.PreviewHwnd()) {
        RemoveWindowSubclass(m_main.PreviewHwnd(), PreviewSubclassProc, 1);
    }
    m_previewTarget.Destroy();
    m_mirror.Destroy();
    TearDownGraphics();
    PersistSettings();
}

bool App::EnsureGraphics()
{
    if (m_d3d.IsValid() && !m_d3d.IsDeviceLost()) {
        return true;
    }
    TearDownGraphics();
    if (!m_d3d.Create()) {
        return false;
    }
    if (!m_renderer.Create(m_d3d.Device())) {
        TearDownGraphics();
        return false;
    }

    RECT rc = ClientRect(m_main.PreviewHwnd());
    const int w = std::max(1L, rc.right - rc.left);
    const int h = std::max(1L, rc.bottom - rc.top);
    if (!m_previewTarget.Create(m_d3d.Device(), m_d3d.Factory(), m_main.PreviewHwnd(), w, h)) {
        TearDownGraphics();
        return false;
    }
    // Initial clear so the preview pane is opaque immediately.
    m_renderer.Draw(m_d3d.Context(), m_previewTarget, nullptr, {}, {}, {}, nullptr, true);
    m_previewTarget.Present(1);
    m_deviceRecoveryAttempted = false;
    return true;
}

void App::TearDownGraphics()
{
    m_capture.Stop();
    m_previewTarget.Destroy();
    m_mirror.Target().Destroy();
    m_renderer.Reset();
    m_d3d.Reset();
    m_lastFrame = {};
}

bool App::RecoverGraphicsOnce()
{
    if (m_deviceRecoveryAttempted) {
        return false;
    }
    m_deviceRecoveryAttempted = true;
    const bool wasMirroring = m_mirroring;
    const bool hadItem = static_cast<bool>(m_item);
    OnStopMirror();
    m_capture.Stop();
    TearDownGraphics();
    if (!EnsureGraphics()) {
        m_error = L"Graphics device recovery failed. Click Retry graphics or choose another source.";
        UpdateControlState();
        UpdateStatus();
        return false;
    }
    if (hadItem) {
        if (!m_capture.Start(
                m_item,
                [this]() { OnCaptureFrameSignal(); },
                [this]() { PostMessageW(m_main.Hwnd(), WM_APP_SOURCE_CLOSED, 0, 0); })) {
            m_error = L"Capture could not restart after graphics recovery.";
        }
    }
    if (wasMirroring && m_hasSelection) {
        OnStartMirror();
    }
    UpdateControlState();
    UpdateStatus();
    return true;
}

void App::OnRetryGraphics()
{
    m_deviceRecoveryAttempted = false;
    m_error.clear();
    if (!EnsureGraphics()) {
        m_error = L"Direct3D 11 initialization failed.";
    } else {
        m_status = L"Graphics ready.";
    }
    UpdateControlState();
    UpdateStatus();
}

void App::OnChooseSource()
{
    OnChangeSource();
}

void App::OnChangeSource()
{
    if (!m_captureSupported) {
        m_error = L"Windows Graphics Capture is not supported.";
        UpdateStatus();
        return;
    }
    if (!EnsureGraphics()) {
        m_error = L"Graphics device is not ready.";
        UpdateStatus();
        return;
    }
    if (m_pickerBusy.exchange(true)) {
        return;
    }

    HWND hwnd = m_main.Hwnd();
    CrashLog(L"Opening capture picker");

    try {
        // Avoid coroutines / apartment_context for the picker — those were failfasting
        // (0x80000003) on completion. Keep the picker alive in a member until Completed.
        m_picker = winrt::Windows::Graphics::Capture::GraphicsCapturePicker();
        auto initializeWithWindow = m_picker.as<IInitializeWithWindow>();
        winrt::check_hresult(initializeWithWindow->Initialize(hwnd));

        auto operation = m_picker.PickSingleItemAsync();
        operation.Completed([this, hwnd](
                                winrt::Windows::Foundation::IAsyncOperation<
                                    winrt::Windows::Graphics::Capture::GraphicsCaptureItem> const& op,
                                winrt::Windows::Foundation::AsyncStatus status) {
            try {
                CrashLog(L"Picker Completed callback, status=" + std::to_wstring(static_cast<int>(status)));

                if (status != winrt::Windows::Foundation::AsyncStatus::Completed) {
                    m_pickerBusy = false;
                    m_picker = nullptr;
                    if (status == winrt::Windows::Foundation::AsyncStatus::Canceled) {
                        CrashLog(L"Picker cancelled");
                    } else {
                        m_pendingPickError = L"Capture picker did not complete.";
                        PostMessageW(hwnd, WM_APP_SOURCE_PICK_FAILED, 0, 0);
                    }
                    return;
                }

                auto item = op.GetResults();
                if (!item) {
                    m_pickerBusy = false;
                    m_picker = nullptr;
                    CrashLog(L"Picker returned empty item");
                    return;
                }

                m_agilePickItem = winrt::agile_ref{ item };
                m_hasAgilePickItem = true;
                if (!PostMessageW(hwnd, WM_APP_SOURCE_PICKED, 0, 0)) {
                    CrashLog(L"PostMessage WM_APP_SOURCE_PICKED failed");
                    m_agilePickItem = {};
                    m_hasAgilePickItem = false;
                    m_pickerBusy = false;
                    m_picker = nullptr;
                }
            } catch (winrt::hresult_error const& ex) {
                m_pendingPickError = std::wstring(L"Capture picker error: ") + std::wstring(ex.message());
                CrashLog(m_pendingPickError);
                PostMessageW(hwnd, WM_APP_SOURCE_PICK_FAILED, 0, 0);
            } catch (...) {
                m_pendingPickError = L"Capture picker failed.";
                CrashLog(m_pendingPickError);
                PostMessageW(hwnd, WM_APP_SOURCE_PICK_FAILED, 0, 0);
            }
        });
    } catch (winrt::hresult_error const& ex) {
        m_pickerBusy = false;
        m_picker = nullptr;
        m_pendingPickError = std::wstring(L"Capture picker error: ") + std::wstring(ex.message());
        CrashLog(m_pendingPickError);
        OnSourcePickFailed();
    } catch (...) {
        m_pickerBusy = false;
        m_picker = nullptr;
        m_pendingPickError = L"Capture picker failed.";
        CrashLog(m_pendingPickError);
        OnSourcePickFailed();
    }
}

void App::OnSourcePicked()
{
    CrashLog(L"OnSourcePicked on UI thread");
    m_pickerBusy = false;
    m_picker = nullptr;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{ nullptr };
    try {
        if (m_hasAgilePickItem) {
            item = m_agilePickItem.get();
            m_agilePickItem = {};
            m_hasAgilePickItem = false;
        }
    } catch (winrt::hresult_error const& ex) {
        m_error = std::wstring(L"Failed to marshal capture item: ") + std::wstring(ex.message());
        CrashLog(m_error);
        m_hasAgilePickItem = false;
        UpdateControlState();
        UpdateStatus();
        return;
    } catch (...) {
        m_error = L"Failed to marshal capture item.";
        CrashLog(m_error);
        m_hasAgilePickItem = false;
        UpdateControlState();
        UpdateStatus();
        return;
    }

    if (!item) {
        CrashLog(L"OnSourcePicked with null item");
        return;
    }

    try {
        OnStopMirror();
        m_capture.Stop();
        m_item = item;
        m_hasSelection = false;
        m_normalizedCrop = { 0.f, 0.f, 1.f, 1.f };
        m_selectMode = SelectMode::Idle;
        m_sourceClosed = false;
        m_lastFrame = {};
        m_error.clear();

        if (!EnsureGraphics()) {
            m_error = L"Graphics device is not ready.";
            m_item = nullptr;
            CrashLog(m_error);
            UpdateControlState();
            UpdateStatus();
            return;
        }

        CrashLog(L"CaptureSession::Start begin");
        const bool ok = m_capture.Start(
            m_item,
            [this]() { OnCaptureFrameSignal(); },
            [this]() { PostMessageW(m_main.Hwnd(), WM_APP_SOURCE_CLOSED, 0, 0); });
        CrashLog(ok ? L"CaptureSession::Start ok" : L"CaptureSession::Start returned false");

        if (!ok) {
            m_error = L"Could not start capture for the selected source. "
                      L"If this is a game, try borderless windowed mode or choose the display instead.";
            m_item = nullptr;
        } else {
            m_status = L"Source selected. Draw a region, then start the mirror.";
            m_previewNeedsPaint = true;
        }
    } catch (winrt::hresult_error const& ex) {
        m_error = std::wstring(L"Capture start error: ") + std::wstring(ex.message());
        CrashLog(m_error);
        m_item = nullptr;
        m_capture.Stop();
    } catch (...) {
        m_error = L"Capture start failed.";
        CrashLog(m_error);
        m_item = nullptr;
        m_capture.Stop();
    }

    UpdateControlState();
    UpdateStatus();
    if (m_previewNeedsPaint) {
        RenderTick();
    }
}

void App::OnSourcePickFailed()
{
    m_pickerBusy = false;
    m_picker = nullptr;
    m_agilePickItem = {};
    m_hasAgilePickItem = false;
    m_error = m_pendingPickError.empty() ? L"Capture picker failed." : m_pendingPickError;
    m_pendingPickError.clear();
    UpdateControlState();
    UpdateStatus();
}

void App::OnSelectRegion()
{
    if (!m_capture.IsActive()) {
        return;
    }
    m_selectMode = SelectMode::Selecting;
    m_dragActive = false;
    m_draftNormalized = {};
    m_status = L"Drag on the preview to select a region. Esc cancels.";
    UpdateControlState();
    UpdateStatus();
    m_previewNeedsPaint = true;
}

void App::OnEditRegion()
{
    OnSelectRegion();
}

void App::OnStartMirror()
{
    m_error.clear();
    if (!EnsureGraphics()) {
        m_error = L"Graphics device is not ready.";
        UpdateStatus();
        return;
    }
    if (!m_capture.IsActive() || !m_hasSelection) {
        m_error = L"Select a source and a nonempty region first.";
        UpdateStatus();
        return;
    }
    if (m_selectedDisplayIndex < 0 || m_selectedDisplayIndex >= static_cast<int>(m_displays.size())) {
        m_error = L"Choose an output monitor.";
        UpdateStatus();
        return;
    }

    const auto& display = m_displays[m_selectedDisplayIndex];
    if (m_capture.IsDisplayCapture() && IsSameMonitor(m_capture.SourceMonitor(), display.monitor)) {
        m_error = L"Display capture would feed back into itself on this monitor. "
                  L"Choose a different output monitor.";
        UpdateStatus();
        return;
    }

    const IntRect crop = CurrentCropPixels();
    if (crop.empty()) {
        m_error = L"The selected region is empty.";
        UpdateStatus();
        return;
    }

    if (!m_mirror.Hwnd()) {
        if (!m_mirror.Create(m_instance)) {
            m_error = L"Could not create the output window.";
            UpdateStatus();
            return;
        }
        m_mirror.SetClosedCallback([this]() {
            PostMessageW(m_main.Hwnd(), WM_APP_MIRROR_CLOSED, 0, 0);
        });
        m_mirror.SetEscapeCallback([this]() { OnEscapeFullscreen(); });
        m_mirror.SetResizeCallback([this]() { OnMirrorResized(); });
        m_mirror.ExcludeFromCapture(true);
    } else {
        m_mirror.SetResizeCallback([this]() { OnMirrorResized(); });
    }

    m_mirror.SetOpacityPercent(m_settings.opacityPercent);
    m_mirror.ShowOnDisplay(display, m_settings.outputMode, crop.width(), crop.height());

    RECT rc = ClientRect(m_mirror.Hwnd());
    const int w = std::max(1L, rc.right - rc.left);
    const int h = std::max(1L, rc.bottom - rc.top);
    if (!m_mirror.Target().IsValid()) {
        if (!m_mirror.Target().Create(m_d3d.Device(), m_d3d.Factory(), m_mirror.Hwnd(), w, h)) {
            m_error = L"Could not create the output swap chain.";
            m_mirror.Hide();
            UpdateStatus();
            return;
        }
    } else if (!m_mirror.Target().Resize(m_d3d.Device(), w, h)) {
        m_error = L"Could not resize the output swap chain.";
        UpdateStatus();
        return;
    }

    m_mirroring = true;
    m_status = L"Mirroring.";
    PersistSettings();
    UpdateControlState();
    UpdateStatus();
    RenderMirror();
}

void App::OnStopMirror()
{
    if (!m_mirroring && !m_mirror.IsVisible()) {
        return;
    }
    m_mirroring = false;
    ClearMirrorOutput();
    m_mirror.Hide();
    m_status = m_capture.IsActive() ? L"Mirror stopped." : m_status;
    UpdateControlState();
    UpdateStatus();
}

void App::OnOutputMonitorChanged(int index)
{
    m_selectedDisplayIndex = index;
    if (index >= 0 && index < static_cast<int>(m_displays.size())) {
        m_settings.outputMonitorId = m_displays[index].preferenceId();
        PersistSettings();
        if (m_mirroring) {
            const IntRect crop = CurrentCropPixels();
            m_mirror.SetMode(m_settings.outputMode, m_displays[index], crop.width(), crop.height());
            RECT rc = ClientRect(m_mirror.Hwnd());
            m_mirror.Target().Resize(m_d3d.Device(), std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top));
        }
    }
    UpdateControlState();
}

void App::OnOutputModeChanged(OutputMode mode)
{
    m_settings.outputMode = mode;
    PersistSettings();
    if (m_mirroring && m_selectedDisplayIndex >= 0 &&
        m_selectedDisplayIndex < static_cast<int>(m_displays.size())) {
        const IntRect crop = CurrentCropPixels();
        m_mirror.SetMode(mode, m_displays[m_selectedDisplayIndex], crop.width(), crop.height());
        RECT rc = ClientRect(m_mirror.Hwnd());
        m_mirror.Target().Resize(m_d3d.Device(), std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top));
        RenderMirror();
    }
    UpdateControlState();
}

void App::OnOpacityChanged(int percent)
{
    if (percent < 10) {
        percent = 10;
    }
    if (percent > 100) {
        percent = 100;
    }
    m_settings.opacityPercent = percent;
    m_main.SetOpacityPercent(percent);
    m_mirror.SetOpacityPercent(percent);
    PersistSettings();
}

void App::OnToggleDebug()
{
    m_settings.showDebug = !m_settings.showDebug;
    m_main.ShowDebug(m_settings.showDebug);
    PersistSettings();
    UpdateStatus();
}

void App::OnCaptureFrameSignal()
{
    m_framePending = true;
    if (m_main.Hwnd()) {
        PostMessageW(m_main.Hwnd(), WM_APP_FRAME, 0, 0);
    }
}

void App::OnCaptureClosed()
{
    m_sourceClosed = true;
    m_capture.Stop();
    OnStopMirror();
    m_item = nullptr;
    m_lastFrame = {};
    m_hasSelection = false;
    m_selectMode = SelectMode::Idle;
    m_status = L"Source closed. Choose another source.";
    ClearMirrorOutput();
    m_previewNeedsPaint = true;
    RenderPreview(true);
    UpdateControlState();
    UpdateStatus();
}

void App::OnMirrorClosed()
{
    m_mirroring = false;
    m_status = L"Output window closed. Mirror stopped.";
    UpdateControlState();
    UpdateStatus();
}

void App::OnMirrorResized()
{
    if (!m_mirroring || !m_mirror.Hwnd() || !m_d3d.IsValid()) {
        return;
    }
    RECT rc{};
    GetClientRect(m_mirror.Hwnd(), &rc);
    const int w = std::max(1L, rc.right - rc.left);
    const int h = std::max(1L, rc.bottom - rc.top);
    if (!m_mirror.Target().IsValid()) {
        if (!m_mirror.Target().Create(m_d3d.Device(), m_d3d.Factory(), m_mirror.Hwnd(), w, h)) {
            return;
        }
    } else if (!m_mirror.Target().Resize(m_d3d.Device(), w, h)) {
        return;
    }
    RenderMirror();
}

void App::OnEscapeFullscreen()
{
    if (m_settings.outputMode == OutputMode::Fullscreen) {
        OnOutputModeChanged(OutputMode::Windowed);
        m_main.SetOutputMode(OutputMode::Windowed);
        m_status = L"Exited fullscreen.";
        UpdateStatus();
    }
}

void App::OnDisplayChange()
{
    RefreshMonitorList(true);
    if (m_mirroring) {
        if (m_selectedDisplayIndex < 0 ||
            m_selectedDisplayIndex >= static_cast<int>(m_displays.size())) {
            OnStopMirror();
            m_error = L"The output display was disconnected. Choose another monitor.";
        } else {
            const IntRect crop = CurrentCropPixels();
            m_mirror.SetMode(m_settings.outputMode, m_displays[m_selectedDisplayIndex], crop.width(), crop.height());
        }
    }
    UpdateControlState();
    UpdateStatus();
}

void App::OnDpiChanged()
{
    if (!m_d3d.IsValid()) {
        return;
    }
    RECT rc = ClientRect(m_main.PreviewHwnd());
    m_previewTarget.Resize(m_d3d.Device(), std::max(1L, rc.right - rc.left), std::max(1L, rc.bottom - rc.top));
    if (m_mirroring && m_mirror.Hwnd()) {
        RECT mrc = ClientRect(m_mirror.Hwnd());
        m_mirror.Target().Resize(m_d3d.Device(), std::max(1L, mrc.right - mrc.left), std::max(1L, mrc.bottom - mrc.top));
    }
    m_previewNeedsPaint = true;
    RenderTick();
}

void App::RenderTick()
{
    if (m_d3d.IsDeviceLost()) {
        if (!RecoverGraphicsOnce()) {
            UpdateStatus();
        }
        return;
    }

    if (m_framePending.exchange(false) || m_previewNeedsPaint) {
        auto frame = m_capture.TakeLatestForRender();
        if (frame.valid) {
            // Preserve normalized crop across content size changes for the same source.
            m_lastFrame = std::move(frame);
            m_lastContentSize = { m_lastFrame.contentWidth, m_lastFrame.contentHeight };
        }
    }

    // Keep the mirror independent of preview repainting. Suspend preview only when the
    // control window is minimized/hidden; the mirror path still presents.
    const bool iconified = IsIconic(m_main.Hwnd()) != FALSE;
    m_previewSuspended = iconified;

    if (!m_previewSuspended) {
        RenderPreview(false);
    }

    if (m_mirroring) {
        RenderMirror();
    }
}

void App::RenderPreview(bool force)
{
    if (!m_previewTarget.IsValid() || !m_d3d.IsValid()) {
        return;
    }

    RenderOverlay overlay{};
    FloatRect crop = EffectiveCrop();
    if (m_selectMode == SelectMode::Selecting) {
        overlay.enabled = true;
        overlay.selecting = m_dragActive;
        overlay.normalizedSelection = m_dragActive ? m_draftNormalized : (m_hasSelection ? m_normalizedCrop : FloatRect{});
        if (overlay.normalizedSelection.empty() && m_hasSelection) {
            overlay.normalizedSelection = m_normalizedCrop;
        }
        // While selecting with no draft yet, show previous selection if any.
        if (overlay.normalizedSelection.empty()) {
            overlay.enabled = m_hasSelection;
            overlay.normalizedSelection = m_normalizedCrop;
        }
        crop = { 0.f, 0.f, 1.f, 1.f }; // show full source while editing
    } else if (m_hasSelection) {
        // Show full frame with selection overlay for clarity in control UI.
        overlay.enabled = true;
        overlay.normalizedSelection = m_normalizedCrop;
        crop = { 0.f, 0.f, 1.f, 1.f };
    }

    const bool clearOnly = !m_lastFrame.valid;
    m_renderer.Draw(
        m_d3d.Context(),
        m_previewTarget,
        clearOnly ? nullptr : m_lastFrame.texture.Get(),
        { m_lastFrame.contentWidth, m_lastFrame.contentHeight },
        { m_lastFrame.textureWidth, m_lastFrame.textureHeight },
        crop,
        overlay.enabled ? &overlay : nullptr,
        clearOnly);

    m_previewTarget.Present(1);
    m_diagnostics.OnPresent();
    m_previewNeedsPaint = false;
    (void)force;
}

void App::RenderMirror()
{
    if (!m_mirror.Target().IsValid() || !m_d3d.IsValid()) {
        return;
    }
    if (!m_lastFrame.valid || !m_hasSelection) {
        ClearMirrorOutput();
        return;
    }

    // Skip unsafe transitional sizes vs crop.
    IntRect cropPixels{};
    if (!NormalizedToPixelRect(m_normalizedCrop,
                               { m_lastFrame.contentWidth, m_lastFrame.contentHeight },
                               cropPixels)) {
        return;
    }
    if (m_lastFrame.textureWidth < m_lastFrame.contentWidth ||
        m_lastFrame.textureHeight < m_lastFrame.contentHeight) {
        return;
    }

    m_diagnostics.SetCropSize(cropPixels.width(), cropPixels.height());
    m_renderer.Draw(
        m_d3d.Context(),
        m_mirror.Target(),
        m_lastFrame.texture.Get(),
        { m_lastFrame.contentWidth, m_lastFrame.contentHeight },
        { m_lastFrame.textureWidth, m_lastFrame.textureHeight },
        m_normalizedCrop,
        nullptr,
        false);
    m_mirror.Target().Present(1);
    m_diagnostics.OnPresent();
}

void App::ClearMirrorOutput()
{
    if (!m_mirror.Target().IsValid() || !m_d3d.IsValid()) {
        return;
    }
    m_renderer.Draw(
        m_d3d.Context(),
        m_mirror.Target(),
        nullptr,
        {},
        {},
        {},
        nullptr,
        true);
    m_mirror.Target().Present(1);
}

void App::RefreshMonitorList(bool keepSelection)
{
    m_displays = EnumerateDisplays();
    std::vector<std::wstring> labels;
    labels.reserve(m_displays.size());
    for (const auto& d : m_displays) {
        labels.push_back(d.label());
    }

    int selected = -1;
    if (keepSelection) {
        if (const auto* found = FindDisplayByPreferenceId(m_displays, m_settings.outputMonitorId)) {
            selected = static_cast<int>(found - m_displays.data());
        }
    }
    if (selected < 0) {
        if (const auto* def = DefaultOutputDisplay(m_displays)) {
            selected = static_cast<int>(def - m_displays.data());
            m_settings.outputMonitorId = def->preferenceId();
        }
    }
    m_selectedDisplayIndex = selected;
    m_main.SetMonitorList(labels, selected);
}

void App::UpdateControlState()
{
    const bool hasSource = m_capture.IsActive() && !m_sourceClosed;
    const bool graphicsOk = m_d3d.IsValid() && !m_d3d.IsDeviceLost();
    m_main.SetControlsEnabled(
        m_captureSupported && graphicsOk,
        hasSource && graphicsOk,
        hasSource && graphicsOk && m_selectMode != SelectMode::Selecting,
        hasSource && graphicsOk && m_hasSelection,
        hasSource && graphicsOk && m_hasSelection && !m_mirroring,
        m_mirroring,
        !m_displays.empty(),
        true,
        !graphicsOk);
}

void App::UpdateStatus()
{
    std::wstringstream ss;
    ss << L"Status: ";
    if (!m_error.empty()) {
        ss << m_error;
    } else if (m_capture.IsPaused()) {
        ss << L"Source paused (minimized or no usable frames). Waiting to resume. ";
        ss << m_status;
    } else {
        ss << m_status;
    }

    if (m_lastFrame.valid) {
        ss << L" | Source " << m_lastFrame.contentWidth << L"x" << m_lastFrame.contentHeight;
    }
    if (m_hasSelection) {
        const IntRect crop = CurrentCropPixels();
        if (!crop.empty()) {
            ss << L" | Crop " << crop.width() << L"x" << crop.height();
        }
    }
    if (m_mirroring) {
        ss << L" | Output " << (m_settings.outputMode == OutputMode::Fullscreen ? L"fullscreen" : L"windowed");
    }

    if (m_item) {
        ss << L" | " << std::wstring(m_capture.SourceName());
    }

    m_main.SetStatusText(ss.str());

    if (m_settings.showDebug) {
        const auto snap = m_diagnostics.Snapshot();
        std::wstringstream d;
        d << L"Debug: capture " << snap.captureWidth << L"x" << snap.captureHeight
          << L" crop " << snap.cropWidth << L"x" << snap.cropHeight
          << L" | capture FPS " << static_cast<int>(snap.captureFps + 0.5)
          << L" | present FPS " << static_cast<int>(snap.presentFps + 0.5)
          << L" | dropped " << snap.droppedFrames
          << L" | frame age (app clock) ";
        if (snap.lastFrameAgeMs >= 0.0) {
            d << static_cast<int>(snap.lastFrameAgeMs) << L" ms";
        } else {
            d << L"n/a";
        }
        d << L" - not end-to-end latency";
        if (!snap.note.empty()) {
            d << L" | " << snap.note;
        }
        m_main.SetDebugText(d.str());
    }
}

void App::PersistSettings()
{
    SettingsStore::Save(m_settings);
}

bool App::HasValidSelection() const
{
    return m_hasSelection && !m_normalizedCrop.empty();
}

IntRect App::CurrentCropPixels() const
{
    IntRect r{};
    if (!m_lastFrame.valid) {
        if (m_lastContentSize.width > 0) {
            NormalizedToPixelRect(m_normalizedCrop, m_lastContentSize, r);
        }
        return r;
    }
    NormalizedToPixelRect(m_normalizedCrop, { m_lastFrame.contentWidth, m_lastFrame.contentHeight }, r);
    return r;
}

FloatRect App::EffectiveCrop() const
{
    if (m_hasSelection) {
        return m_normalizedCrop;
    }
    return { 0.f, 0.f, 1.f, 1.f };
}

void App::OnPreviewLButtonDown(POINT clientPt)
{
    if (m_selectMode != SelectMode::Selecting || !m_lastFrame.valid) {
        return;
    }
    RECT rc = ClientRect(m_main.PreviewHwnd());
    const FloatRect fitted = FitAspect(
        { static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top) },
        { static_cast<float>(m_lastFrame.contentWidth), static_cast<float>(m_lastFrame.contentHeight) });

    PointF content{};
    if (!ViewportToContent({ static_cast<float>(clientPt.x), static_cast<float>(clientPt.y) },
                           fitted,
                           { m_lastFrame.contentWidth, m_lastFrame.contentHeight },
                           content)) {
        // Ignore drags that begin in letterbox bars.
        m_dragStartedInVideo = false;
        return;
    }

    m_dragStartedInVideo = true;
    m_dragActive = true;
    m_dragStartContent = content;
    m_dragCurrentContent = content;
    SetFocus(m_main.PreviewHwnd());
    SetCapture(m_main.PreviewHwnd());
    m_previewNeedsPaint = true;
    RenderPreview(true);
}

void App::OnPreviewMouseMove(POINT clientPt)
{
    if (!m_dragActive || !m_dragStartedInVideo || !m_lastFrame.valid) {
        return;
    }
    RECT rc = ClientRect(m_main.PreviewHwnd());
    const FloatRect fitted = FitAspect(
        { static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top) },
        { static_cast<float>(m_lastFrame.contentWidth), static_cast<float>(m_lastFrame.contentHeight) });

    PointF content{};
    // Clamp to video rect when pointer leaves it.
    const float x = std::clamp(static_cast<float>(clientPt.x), fitted.left, fitted.right);
    const float y = std::clamp(static_cast<float>(clientPt.y), fitted.top, fitted.bottom);
    ViewportToContent({ x, y }, fitted, { m_lastFrame.contentWidth, m_lastFrame.contentHeight }, content);
    m_dragCurrentContent = ClampToContent(content, { m_lastFrame.contentWidth, m_lastFrame.contentHeight });

    FloatRect draft{};
    if (NormalizeCropFromCorners(m_dragStartContent, m_dragCurrentContent,
                                 { m_lastFrame.contentWidth, m_lastFrame.contentHeight }, draft)) {
        m_draftNormalized = draft;
        IntRect pixels{};
        NormalizedToPixelRect(draft, { m_lastFrame.contentWidth, m_lastFrame.contentHeight }, pixels);
        std::wstringstream ss;
        ss << L"Selecting " << pixels.width() << L" x " << pixels.height() << L" source pixels. Esc cancels.";
        m_status = ss.str();
        UpdateStatus();
    }
    m_previewNeedsPaint = true;
    RenderPreview(true);
}

void App::OnPreviewLButtonUp(POINT clientPt)
{
    if (!m_dragActive) {
        return;
    }
    ReleaseCapture();
    m_dragActive = false;

    if (!m_dragStartedInVideo || !m_lastFrame.valid) {
        return;
    }

    OnPreviewMouseMove(clientPt);

    FloatRect normalized{};
    if (!NormalizeCropFromCorners(m_dragStartContent, m_dragCurrentContent,
                                  { m_lastFrame.contentWidth, m_lastFrame.contentHeight }, normalized)) {
        m_status = L"Selection was empty. Drag again.";
        m_selectMode = SelectMode::Selecting;
        UpdateStatus();
        m_previewNeedsPaint = true;
        return;
    }

    m_normalizedCrop = normalized;
    m_hasSelection = true;
    m_selectMode = SelectMode::Idle;
    const IntRect crop = CurrentCropPixels();
    std::wstringstream ss;
    ss << L"Region set to " << crop.width() << L" x " << crop.height() << L" source pixels.";
    m_status = ss.str();

    if (m_mirroring) {
        // Applying a new valid selection updates the running output.
        if (m_selectedDisplayIndex >= 0 && m_selectedDisplayIndex < static_cast<int>(m_displays.size())) {
            m_mirror.SetMode(m_settings.outputMode, m_displays[m_selectedDisplayIndex], crop.width(), crop.height());
        }
    }

    UpdateControlState();
    UpdateStatus();
    m_previewNeedsPaint = true;
    RenderTick();
}

void App::OnPreviewCaptureLost()
{
    if (m_dragActive) {
        m_dragActive = false;
        m_status = L"Selection cancelled.";
        UpdateStatus();
        m_previewNeedsPaint = true;
    }
}

bool App::OnPreviewKeyEscape()
{
    if (m_selectMode == SelectMode::Selecting) {
        m_dragActive = false;
        m_selectMode = SelectMode::Idle;
        ReleaseCapture();
        m_status = m_hasSelection ? L"Selection cancelled. Previous region kept." : L"Selection cancelled.";
        UpdateControlState();
        UpdateStatus();
        m_previewNeedsPaint = true;
        RenderPreview(true);
        return true;
    }
    return false;
}

} // namespace rm
