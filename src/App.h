#pragma once

#include "capture/CaptureSession.h"
#include "capture/D3DDevice.h"
#include "render/GpuRenderer.h"
#include "ui/MainWindow.h"
#include "ui/MirrorWindow.h"
#include "util/Diagnostics.h"
#include "util/DisplayEnum.h"
#include "util/RegionMath.h"
#include "util/Settings.h"

#include <atomic>
#include <vector>
#include <winrt/Windows.System.h>

namespace rm {

class App {
public:
    explicit App(HINSTANCE instance);
    ~App();

    int Run();

    // Invoked by MainWindow / preview subclass.
    void OnChooseSource();
    void OnChangeSource();
    void OnSelectRegion();
    void OnEditRegion();
    void OnStartMirror();
    void OnStopMirror();
    void OnOutputMonitorChanged(int index);
    void OnOutputModeChanged(OutputMode mode);
    void OnOpacityChanged(int percent);
    void OnToggleDebug();
    void OnRetryGraphics();
    void OnCaptureClosed();
    void OnMirrorClosed();
    void OnMirrorResized();
    void OnDisplayChange();
    void OnDpiChanged();
    void RenderTick();
    void OnSourcePicked();
    void OnSourcePickFailed();

    void OnPreviewLButtonDown(POINT clientPt);
    void OnPreviewLButtonUp(POINT clientPt);
    void OnPreviewMouseMove(POINT clientPt);
    void OnPreviewCaptureLost();
    bool OnPreviewKeyEscape();
    bool IsSelectingRegion() const { return m_selectMode == SelectMode::Selecting; }

    static constexpr UINT WM_APP_FRAME = WM_APP + 1;
    static constexpr UINT WM_APP_SOURCE_CLOSED = WM_APP + 2;
    static constexpr UINT WM_APP_MIRROR_CLOSED = WM_APP + 3;
    static constexpr UINT WM_APP_SOURCE_PICKED = WM_APP + 4;
    static constexpr UINT WM_APP_SOURCE_PICK_FAILED = WM_APP + 5;

private:
    bool Initialize();
    void Shutdown();

    void OnCaptureFrameSignal();
    void OnEscapeFullscreen();

    void RenderPreview(bool force);
    void RenderMirror();
    void ClearMirrorOutput();

    void RefreshMonitorList(bool keepSelection);
    void UpdateControlState();
    void UpdateStatus();
    void PersistSettings();

    bool EnsureGraphics();
    bool RecoverGraphicsOnce();
    void TearDownGraphics();

    bool HasValidSelection() const;
    IntRect CurrentCropPixels() const;
    FloatRect EffectiveCrop() const;

    HINSTANCE m_instance = nullptr;
    MainWindow m_main;
    MirrorWindow m_mirror;
    winrt::Windows::System::DispatcherQueueController m_dispatcherController{ nullptr };

    D3DDevice m_d3d;
    GpuRenderer m_renderer;
    CaptureSession m_capture;
    Diagnostics m_diagnostics;
    AppSettings m_settings;

    SwapChainTarget m_previewTarget;

    std::vector<DisplayInfo> m_displays;
    int m_selectedDisplayIndex = -1;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCapturePicker m_picker{ nullptr };
    winrt::agile_ref<winrt::Windows::Graphics::Capture::GraphicsCaptureItem> m_agilePickItem;
    bool m_hasAgilePickItem = false;
    std::wstring m_pendingPickError;
    std::atomic<bool> m_pickerBusy{ false };
    FloatRect m_normalizedCrop{ 0.f, 0.f, 1.f, 1.f };
    bool m_hasSelection = false;

    enum class SelectMode { Idle, Selecting, EditingReady };
    SelectMode m_selectMode = SelectMode::Idle;
    bool m_dragActive = false;
    bool m_dragStartedInVideo = false;
    PointF m_dragStartContent{};
    PointF m_dragCurrentContent{};
    FloatRect m_draftNormalized{};

    bool m_mirroring = false;
    bool m_sourceClosed = false;
    bool m_deviceRecoveryAttempted = false;
    bool m_captureSupported = false;

    CapturedFrame m_lastFrame{};
    SizeI m_lastContentSize{};

    std::atomic<bool> m_framePending{ false };
    std::atomic<bool> m_previewNeedsPaint{ true };
    bool m_previewSuspended = false;

    std::wstring m_status;
    std::wstring m_error;
};

} // namespace rm
