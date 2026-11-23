#include "pch.h"
#include "D3DDevice.h"

#include <windows.graphics.directx.direct3d11.interop.h>

using Microsoft::WRL::ComPtr;

namespace rm {

bool D3DDevice::Create()
{
    Reset();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };
    D3D_FEATURE_LEVEL obtained = D3D_FEATURE_LEVEL_11_0;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        &obtained,
        context.GetAddressOf());

#if defined(_DEBUG)
    if (FAILED(hr)) {
        flags &= ~D3D11_CREATE_DEVICE_DEBUG;
        hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            flags,
            featureLevels,
            ARRAYSIZE(featureLevels),
            D3D11_SDK_VERSION,
            device.GetAddressOf(),
            &obtained,
            context.GetAddressOf());
    }
#endif

    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = device.As(&dxgiDevice);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(adapter.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIFactory2> factory;
    hr = adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), inspectable.put());
    if (FAILED(hr)) {
        return false;
    }

    m_device = std::move(device);
    m_context = std::move(context);
    m_factory = std::move(factory);
    m_winrtDevice = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
    return true;
}

void D3DDevice::Reset()
{
    m_winrtDevice = nullptr;
    m_factory.Reset();
    m_context.Reset();
    m_device.Reset();
}

bool D3DDevice::IsDeviceLost() const
{
    if (!m_device) {
        return true;
    }
    const HRESULT reason = m_device->GetDeviceRemovedReason();
    return FAILED(reason);
}

HRESULT D3DDevice::GetDeviceRemovedReason() const
{
    if (!m_device) {
        return DXGI_ERROR_DEVICE_REMOVED;
    }
    return m_device->GetDeviceRemovedReason();
}

} // namespace rm
