#pragma once

#include "../util/RegionMath.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdint>

namespace rm {

struct RenderOverlay {
    bool enabled = false;
    FloatRect normalizedSelection{}; // relative to content
    bool selecting = false;
};

class SwapChainTarget {
public:
    bool Create(ID3D11Device* device, IDXGIFactory2* factory, HWND hwnd, int width, int height);
    bool Resize(ID3D11Device* device, int width, int height);
    void Destroy();

    bool IsValid() const { return m_swapChain != nullptr; }
    IDXGISwapChain1* SwapChain() const { return m_swapChain.Get(); }
    ID3D11RenderTargetView* RTV() const { return m_rtv.Get(); }
    int Width() const { return m_width; }
    int Height() const { return m_height; }
    HWND Hwnd() const { return m_hwnd; }

    HRESULT Present(UINT syncInterval = 1);

private:
    bool CreateSizeDependent(ID3D11Device* device, int width, int height);

    HWND m_hwnd = nullptr;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swapChain;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_rtv;
    int m_width = 0;
    int m_height = 0;
};

class GpuRenderer {
public:
    bool Create(ID3D11Device* device);
    void Reset();

    // Renders crop into the target with aspect-preserving letterbox/pillarbox.
    // If overlay.enabled, dims outside the selection and draws a border (preview editing).
    bool Draw(
        ID3D11DeviceContext* context,
        SwapChainTarget& target,
        ID3D11Texture2D* sourceTexture,
        SizeI contentSize,
        SizeI textureSize,
        FloatRect normalizedCrop,
        const RenderOverlay* overlay,
        bool clearOnly);

private:
    bool CreateShaders(ID3D11Device* device);

    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_vs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_ps;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constants;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sampler;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> m_rasterizer;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blend;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_srvTexture; // tracks which texture the SRV belongs to
};

} // namespace rm
