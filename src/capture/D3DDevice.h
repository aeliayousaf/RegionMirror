#pragma once

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

namespace rm {

class D3DDevice {
public:
    bool Create();
    void Reset();

    bool IsValid() const { return m_device != nullptr; }

    ID3D11Device* Device() const { return m_device.Get(); }
    ID3D11DeviceContext* Context() const { return m_context.Get(); }
    IDXGIFactory2* Factory() const { return m_factory.Get(); }
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& WinrtDevice() const { return m_winrtDevice; }

    bool IsDeviceLost() const;
    HRESULT GetDeviceRemovedReason() const;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> m_device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
    Microsoft::WRL::ComPtr<IDXGIFactory2> m_factory;
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_winrtDevice{ nullptr };
};

} // namespace rm
