#pragma once

#include "D3DDevice.h"
#include "../util/Diagnostics.h"

#include <atomic>
#include <functional>
#include <mutex>

#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>

namespace rm {

struct CapturedFrame {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    int textureWidth = 0;
    int textureHeight = 0;
    int contentWidth = 0;
    int contentHeight = 0;
    bool valid = false;
};

class CaptureSession {
public:
    using FrameSignal = std::function<void()>;
    using ClosedSignal = std::function<void()>;

    explicit CaptureSession(D3DDevice& device, Diagnostics& diagnostics);
    ~CaptureSession();

    CaptureSession(const CaptureSession&) = delete;
    CaptureSession& operator=(const CaptureSession&) = delete;

    static bool IsCaptureSupported();

    bool Start(winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
               FrameSignal onFrame,
               ClosedSignal onClosed);
    void Stop();

    bool IsActive() const { return m_active.load(); }
    bool IsPaused() const { return m_paused.load(); }

    // UI-thread only. Returns the last GPU-copied shader-readable texture.
    CapturedFrame TakeLatestForRender();

    winrt::hstring SourceName() const;
    bool IsDisplayCapture() const { return m_isDisplayCapture; }
    HMONITOR SourceMonitor() const { return m_sourceMonitor; }

private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const&);
    void RecreatePool(winrt::Windows::Graphics::SizeInt32 size);
    void HandleItemClosed();
    void DetectDisplayCapture(int contentW, int contentH);
    bool EnsureReadableTexture(DXGI_FORMAT format, int width, int height);

    D3DDevice& m_device;
    Diagnostics& m_diagnostics;

    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_lastSize{};
    winrt::event_token m_frameToken{};
    winrt::event_token m_closedToken{};

    FrameSignal m_onFrame;
    ClosedSignal m_onClosed;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_readableTexture;
    DXGI_FORMAT m_readableFormat = DXGI_FORMAT_UNKNOWN;
    int m_readableW = 0;
    int m_readableH = 0;
    int m_readableContentW = 0;
    int m_readableContentH = 0;
    bool m_hasReadable = false;
    uint64_t m_dropped = 0;

    std::atomic<bool> m_active{ false };
    std::atomic<bool> m_paused{ false };
    std::atomic<bool> m_stopping{ false };

    bool m_isDisplayCapture = false;
    HMONITOR m_sourceMonitor = nullptr;
};

} // namespace rm
