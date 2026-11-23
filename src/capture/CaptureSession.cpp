#include "pch.h"
#include "CaptureSession.h"
#include "../util/DisplayEnum.h"
#include "../util/CrashLog.h"

#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.Metadata.h>

using Microsoft::WRL::ComPtr;
namespace wgc = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;

namespace rm {
namespace {

ComPtr<ID3D11Texture2D> GetTextureFromSurface(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface)
{
    auto access = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    ComPtr<ID3D11Texture2D> texture;
    winrt::check_hresult(access->GetInterface(IID_PPV_ARGS(texture.GetAddressOf())));
    return texture;
}

bool TryDisableCursorCapture(wgc::GraphicsCaptureSession const& session)
{
    try {
        if (winrt::Windows::Foundation::Metadata::ApiInformation::IsPropertyPresent(
                L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsCursorCaptureEnabled")) {
            session.IsCursorCaptureEnabled(false);
            return true;
        }
    } catch (...) {
    }
    return false;
}

} // namespace

CaptureSession::CaptureSession(D3DDevice& device, Diagnostics& diagnostics)
    : m_device(device)
    , m_diagnostics(diagnostics)
{
}

CaptureSession::~CaptureSession()
{
    Stop();
}

bool CaptureSession::IsCaptureSupported()
{
    try {
        return wgc::GraphicsCaptureSession::IsSupported();
    } catch (...) {
        return false;
    }
}

bool CaptureSession::Start(wgc::GraphicsCaptureItem const& item, FrameSignal onFrame, ClosedSignal onClosed)
{
    Stop();
    if (!item || !m_device.IsValid()) {
        return false;
    }

    m_stopping = false;
    m_paused = false;
    m_onFrame = std::move(onFrame);
    m_onClosed = std::move(onClosed);
    m_item = item;
    m_isDisplayCapture = false;
    m_sourceMonitor = nullptr;
    m_dropped = 0;

    m_lastSize = item.Size();
    if (m_lastSize.Width < 1 || m_lastSize.Height < 1) {
        m_lastSize.Width = 1;
        m_lastSize.Height = 1;
    }

    try {
        // Use the UI-thread pool (requires DispatcherQueue on this STA thread).
        // Copy surfaces on this same thread — do not hand raw capture frames across threads.
        CrashLog(L"Create UI frame pool");
        m_framePool = wgc::Direct3D11CaptureFramePool::Create(
            m_device.WinrtDevice(),
            wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            m_lastSize);

        CrashLog(L"CreateCaptureSession");
        m_session = m_framePool.CreateCaptureSession(m_item);
        TryDisableCursorCapture(m_session);

        CrashLog(L"Subscribe FrameArrived/Closed");
        m_frameToken = m_framePool.FrameArrived({ this, &CaptureSession::OnFrameArrived });
        m_closedToken = m_item.Closed([this](auto&&, auto&&) {
            try {
                HandleItemClosed();
            } catch (...) {
            }
        });

        CrashLog(L"StartCapture");
        m_session.StartCapture();
        m_active = true;
        m_diagnostics.SetNote(L"Capture started");
        CrashLog(L"StartCapture returned");
        return true;
    } catch (winrt::hresult_error const& ex) {
        const std::wstring msg = std::wstring(L"Capture start failed: ") + std::wstring(ex.message());
        m_diagnostics.SetNote(msg);
        CrashLog(msg);
        Stop();
        return false;
    } catch (...) {
        m_diagnostics.SetNote(L"Capture start failed");
        CrashLog(L"Capture start failed (unknown)");
        Stop();
        return false;
    }
}

void CaptureSession::Stop()
{
    m_stopping = true;
    m_active = false;

    try {
        if (m_framePool && m_frameToken.value != 0) {
            m_framePool.FrameArrived(m_frameToken);
            m_frameToken = {};
        }
    } catch (...) {
    }

    try {
        if (m_item && m_closedToken.value != 0) {
            m_item.Closed(m_closedToken);
            m_closedToken = {};
        }
    } catch (...) {
    }

    try {
        if (m_session) {
            m_session.Close();
        }
    } catch (...) {
    }
    m_session = nullptr;

    try {
        if (m_framePool) {
            m_framePool.Close();
        }
    } catch (...) {
    }
    m_framePool = nullptr;
    m_item = nullptr;

    m_readableTexture.Reset();
    m_readableW = m_readableH = 0;
    m_readableContentW = m_readableContentH = 0;
    m_readableFormat = DXGI_FORMAT_UNKNOWN;
    m_hasReadable = false;
    m_onFrame = nullptr;
    m_onClosed = nullptr;
    m_paused = false;
    m_isDisplayCapture = false;
    m_sourceMonitor = nullptr;
}

winrt::hstring CaptureSession::SourceName() const
{
    if (m_item) {
        return m_item.DisplayName();
    }
    return L"";
}

void CaptureSession::RecreatePool(winrt::Windows::Graphics::SizeInt32 size)
{
    if (!m_framePool || !m_device.IsValid()) {
        return;
    }
    if (size.Width < 1) {
        size.Width = 1;
    }
    if (size.Height < 1) {
        size.Height = 1;
    }
    m_lastSize = size;
    m_framePool.Recreate(
        m_device.WinrtDevice(),
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        size);
}

void CaptureSession::DetectDisplayCapture(int contentW, int contentH)
{
    if (m_isDisplayCapture || contentW <= 0 || contentH <= 0) {
        return;
    }
    const auto displays = EnumerateDisplays();
    for (const auto& d : displays) {
        const int w = d.bounds.right - d.bounds.left;
        const int h = d.bounds.bottom - d.bounds.top;
        if (w == contentW && h == contentH) {
            m_isDisplayCapture = true;
            m_sourceMonitor = d.monitor;
            return;
        }
    }
}

bool CaptureSession::EnsureReadableTexture(DXGI_FORMAT format, int width, int height)
{
    if (m_readableTexture && m_readableW == width && m_readableH == height && m_readableFormat == format) {
        return true;
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    m_readableTexture.Reset();
    if (FAILED(m_device.Device()->CreateTexture2D(&desc, nullptr, m_readableTexture.GetAddressOf()))) {
        return false;
    }
    m_readableW = width;
    m_readableH = height;
    m_readableFormat = format;
    return true;
}

void CaptureSession::OnFrameArrived(
    wgc::Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const&)
{
    try {
        if (m_stopping.load() || !m_active.load()) {
            try {
                if (auto frame = sender.TryGetNextFrame()) {
                    frame.Close();
                }
            } catch (...) {
            }
            return;
        }

        wgc::Direct3D11CaptureFrame frame{ nullptr };
        try {
            frame = sender.TryGetNextFrame();
        } catch (...) {
            return;
        }
        if (!frame) {
            return;
        }

        // Drop stale queued frames; keep only the newest.
        for (;;) {
            wgc::Direct3D11CaptureFrame newer{ nullptr };
            try {
                newer = sender.TryGetNextFrame();
            } catch (...) {
                newer = nullptr;
            }
            if (!newer) {
                break;
            }
            try { frame.Close(); } catch (...) {}
            frame = std::move(newer);
            ++m_dropped;
            m_diagnostics.OnFrameDropped();
        }

        const auto contentSize = frame.ContentSize();
        ComPtr<ID3D11Texture2D> texture;
        try {
            texture = GetTextureFromSurface(frame.Surface());
        } catch (...) {
            try { frame.Close(); } catch (...) {}
            return;
        }

        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);

        if (static_cast<int>(desc.Width) < contentSize.Width ||
            static_cast<int>(desc.Height) < contentSize.Height ||
            contentSize.Width < 1 || contentSize.Height < 1) {
            try { frame.Close(); } catch (...) {}
            m_paused = true;
            return;
        }

        // UI-thread pool: safe to use the immediate context here.
        if (!EnsureReadableTexture(desc.Format, static_cast<int>(desc.Width), static_cast<int>(desc.Height))) {
            try { frame.Close(); } catch (...) {}
            return;
        }

        m_device.Context()->CopyResource(m_readableTexture.Get(), texture.Get());
        try { frame.Close(); } catch (...) {}

        m_readableContentW = contentSize.Width;
        m_readableContentH = contentSize.Height;
        m_hasReadable = true;

        DetectDisplayCapture(contentSize.Width, contentSize.Height);
        m_paused = false;
        m_diagnostics.OnFrameReceived();
        m_diagnostics.SetCaptureSize(contentSize.Width, contentSize.Height);

        if (contentSize.Width != m_lastSize.Width || contentSize.Height != m_lastSize.Height) {
            try {
                RecreatePool(contentSize);
            } catch (...) {
            }
        }

        if (m_onFrame) {
            m_onFrame();
        }
    } catch (...) {
        CrashLog(L"OnFrameArrived exception swallowed");
    }
}

void CaptureSession::HandleItemClosed()
{
    if (m_stopping.load()) {
        return;
    }
    m_active = false;
    m_paused = true;
    if (m_onClosed) {
        m_onClosed();
    }
}

CapturedFrame CaptureSession::TakeLatestForRender()
{
    CapturedFrame result{};
    if (!m_hasReadable || !m_readableTexture) {
        return result;
    }
    result.texture = m_readableTexture;
    result.format = m_readableFormat;
    result.textureWidth = m_readableW;
    result.textureHeight = m_readableH;
    result.contentWidth = m_readableContentW;
    result.contentHeight = m_readableContentH;
    result.valid = true;
    return result;
}

} // namespace rm
