#include "pch.h"
#include "GpuRenderer.h"

#include <d3dcompiler.h>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace rm {
namespace {

constexpr char kShaderSource[] = R"(
cbuffer Constants : register(b0)
{
    float4 cropUV;          // left, top, right, bottom in texture UV space
    float4 selectionUV;     // selection in texture UV (for overlay)
    float2 contentScale;    // contentSize / textureSize
    float overlayMode;      // 0 = off, 1 = show selection overlay
    float pad0;
    float4 barColor;
    float4 dimColor;
    float4 borderColor;
};

Texture2D srcTex : register(t0);
SamplerState samp : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    float2 uv = lerp(cropUV.xy, cropUV.zw, i.uv);
    float4 color = srcTex.Sample(samp, uv);

    if (overlayMode > 0.5)
    {
        // Map current sample UV back into content-normalized space for selection tests.
        float2 contentUV = float2(
            contentScale.x > 0 ? (uv.x / contentScale.x) : 0,
            contentScale.y > 0 ? (uv.y / contentScale.y) : 0);

        float2 selMin = float2(
            contentScale.x > 0 ? (selectionUV.x / contentScale.x) : 0,
            contentScale.y > 0 ? (selectionUV.y / contentScale.y) : 0);
        float2 selMax = float2(
            contentScale.x > 0 ? (selectionUV.z / contentScale.x) : 0,
            contentScale.y > 0 ? (selectionUV.w / contentScale.y) : 0);

        bool inside =
            contentUV.x >= selMin.x && contentUV.x <= selMax.x &&
            contentUV.y >= selMin.y && contentUV.y <= selMax.y;

        if (!inside)
        {
            color.rgb = lerp(color.rgb, dimColor.rgb, dimColor.a);
        }
        else
        {
            // Border in content UV space (~2px feel via derivative-ish constant thickness).
            float2 d = fwidth(contentUV);
            float2 edge = max(d * 1.5, float2(0.0015, 0.0015));
            bool onBorder =
                contentUV.x < selMin.x + edge.x || contentUV.x > selMax.x - edge.x ||
                contentUV.y < selMin.y + edge.y || contentUV.y > selMax.y - edge.y;
            if (onBorder)
            {
                color = borderColor;
            }
        }
    }

    return color;
}
)";

struct alignas(16) Constants {
    float cropUV[4];
    float selectionUV[4];
    float contentScale[2];
    float overlayMode;
    float pad0;
    float barColor[4];
    float dimColor[4];
    float borderColor[4];
};

} // namespace

bool SwapChainTarget::Create(ID3D11Device* device, IDXGIFactory2* factory, HWND hwnd, int width, int height)
{
    Destroy();
    if (!device || !factory || !hwnd || width < 1 || height < 1) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> swapChain;
    HRESULT hr = factory->CreateSwapChainForHwnd(
        device,
        hwnd,
        &desc,
        nullptr,
        nullptr,
        swapChain.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    m_hwnd = hwnd;
    m_swapChain = std::move(swapChain);
    return CreateSizeDependent(device, width, height);
}

bool SwapChainTarget::CreateSizeDependent(ID3D11Device* device, int width, int height)
{
    m_rtv.Reset();
    if (!m_swapChain || !device) {
        return false;
    }

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        return false;
    }

    hr = device->CreateRenderTargetView(backBuffer.Get(), nullptr, m_rtv.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    m_width = width;
    m_height = height;
    return true;
}

bool SwapChainTarget::Resize(ID3D11Device* device, int width, int height)
{
    if (!m_swapChain || !device) {
        return false;
    }
    if (width < 1 || height < 1) {
        return false;
    }
    if (width == m_width && height == m_height && m_rtv) {
        return true;
    }

    m_rtv.Reset();
    HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        return false;
    }
    return CreateSizeDependent(device, width, height);
}

void SwapChainTarget::Destroy()
{
    m_rtv.Reset();
    m_swapChain.Reset();
    m_hwnd = nullptr;
    m_width = m_height = 0;
}

HRESULT SwapChainTarget::Present(UINT syncInterval)
{
    if (!m_swapChain) {
        return E_FAIL;
    }
    return m_swapChain->Present(syncInterval, 0);
}

bool GpuRenderer::Create(ID3D11Device* device)
{
    Reset();
    if (!device) {
        return false;
    }
    if (!CreateShaders(device)) {
        return false;
    }

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(Constants);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(device->CreateBuffer(&cbd, nullptr, m_constants.GetAddressOf()))) {
        return false;
    }

    D3D11_SAMPLER_DESC sd{};
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sd.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sd, m_sampler.GetAddressOf()))) {
        return false;
    }

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rd, m_rasterizer.GetAddressOf()))) {
        return false;
    }

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&bd, m_blend.GetAddressOf()))) {
        return false;
    }

    return true;
}

void GpuRenderer::Reset()
{
    m_vs.Reset();
    m_ps.Reset();
    m_constants.Reset();
    m_sampler.Reset();
    m_rasterizer.Reset();
    m_blend.Reset();
    m_srv.Reset();
    m_srvTexture.Reset();
}

bool GpuRenderer::CreateShaders(ID3D11Device* device)
{
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psBlob;
    ComPtr<ID3DBlob> errors;

    HRESULT hr = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "RegionMirror.hlsl",
        nullptr,
        nullptr,
        "VSMain",
        "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        vsBlob.GetAddressOf(),
        errors.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    errors.Reset();
    hr = D3DCompile(
        kShaderSource,
        sizeof(kShaderSource) - 1,
        "RegionMirror.hlsl",
        nullptr,
        nullptr,
        "PSMain",
        "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS,
        0,
        psBlob.GetAddressOf(),
        errors.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vs.GetAddressOf()))) {
        return false;
    }
    if (FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_ps.GetAddressOf()))) {
        return false;
    }
    return true;
}

bool GpuRenderer::Draw(
    ID3D11DeviceContext* context,
    SwapChainTarget& target,
    ID3D11Texture2D* sourceTexture,
    SizeI contentSize,
    SizeI textureSize,
    FloatRect normalizedCrop,
    const RenderOverlay* overlay,
    bool clearOnly)
{
    if (!context || !target.IsValid() || !m_vs || !m_ps) {
        return false;
    }

    auto* rtv = target.RTV();
    context->OMSetRenderTargets(1, &rtv, nullptr);

    const float clear[4] = { 0.06f, 0.07f, 0.09f, 1.f };
    context->ClearRenderTargetView(rtv, clear);

    if (clearOnly || !sourceTexture || contentSize.width <= 0 || contentSize.height <= 0) {
        return true;
    }

    IntRect cropPixels{};
    if (!NormalizedToPixelRect(normalizedCrop, contentSize, cropPixels)) {
        // Fall back to full content if crop is invalid.
        cropPixels = { 0, 0, contentSize.width, contentSize.height };
    }

    const SizeF viewport{
        static_cast<float>(target.Width()),
        static_cast<float>(target.Height())
    };
    const SizeF cropSize{
        static_cast<float>(cropPixels.width()),
        static_cast<float>(cropPixels.height())
    };
    const FloatRect fitted = FitAspect(viewport, cropSize);

    D3D11_VIEWPORT vp{};
    vp.TopLeftX = fitted.left;
    vp.TopLeftY = fitted.top;
    vp.Width = fitted.width();
    vp.Height = fitted.height();
    vp.MinDepth = 0.f;
    vp.MaxDepth = 1.f;
    context->RSSetViewports(1, &vp);

    if (!m_srv || m_srvTexture.Get() != sourceTexture) {
        m_srv.Reset();
        ComPtr<ID3D11Device> device;
        sourceTexture->GetDevice(device.GetAddressOf());
        D3D11_TEXTURE2D_DESC td{};
        sourceTexture->GetDesc(&td);
        D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
        srvd.Format = td.Format;
        srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvd.Texture2D.MipLevels = 1;
        if (FAILED(device->CreateShaderResourceView(sourceTexture, &srvd, m_srv.GetAddressOf()))) {
            return false;
        }
        m_srvTexture = sourceTexture;
    }

    const FloatRect cropUV = PixelCropToTextureUV(cropPixels, contentSize, textureSize);

    FloatRect selectionUV = cropUV;
    float overlayMode = 0.f;
    if (overlay && overlay->enabled && !overlay->normalizedSelection.empty()) {
        IntRect selPixels{};
        if (NormalizedToPixelRect(overlay->normalizedSelection, contentSize, selPixels)) {
            selectionUV = PixelCropToTextureUV(selPixels, contentSize, textureSize);
            overlayMode = 1.f;
        }
    }

    Constants cb{};
    cb.cropUV[0] = cropUV.left;
    cb.cropUV[1] = cropUV.top;
    cb.cropUV[2] = cropUV.right;
    cb.cropUV[3] = cropUV.bottom;
    cb.selectionUV[0] = selectionUV.left;
    cb.selectionUV[1] = selectionUV.top;
    cb.selectionUV[2] = selectionUV.right;
    cb.selectionUV[3] = selectionUV.bottom;
    cb.contentScale[0] = textureSize.width > 0
        ? static_cast<float>(contentSize.width) / static_cast<float>(textureSize.width)
        : 1.f;
    cb.contentScale[1] = textureSize.height > 0
        ? static_cast<float>(contentSize.height) / static_cast<float>(textureSize.height)
        : 1.f;
    cb.overlayMode = overlayMode;
    cb.barColor[0] = clear[0];
    cb.barColor[1] = clear[1];
    cb.barColor[2] = clear[2];
    cb.barColor[3] = 1.f;
    cb.dimColor[0] = 0.f;
    cb.dimColor[1] = 0.f;
    cb.dimColor[2] = 0.f;
    cb.dimColor[3] = 0.55f;
    cb.borderColor[0] = 0.95f;
    cb.borderColor[1] = 0.85f;
    cb.borderColor[2] = 0.25f;
    cb.borderColor[3] = 1.f;

    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(m_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        return false;
    }
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    context->Unmap(m_constants.Get(), 0);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->IASetInputLayout(nullptr);
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    context->PSSetShader(m_ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[] = { m_constants.Get() };
    context->VSSetConstantBuffers(0, 1, cbs);
    context->PSSetConstantBuffers(0, 1, cbs);
    ID3D11ShaderResourceView* srvs[] = { m_srv.Get() };
    context->PSSetShaderResources(0, 1, srvs);
    ID3D11SamplerState* samplers[] = { m_sampler.Get() };
    context->PSSetSamplers(0, 1, samplers);
    context->RSSetState(m_rasterizer.Get());
    context->OMSetBlendState(m_blend.Get(), nullptr, 0xffffffff);

    context->Draw(3, 0);

    ID3D11ShaderResourceView* nullSrv[] = { nullptr };
    context->PSSetShaderResources(0, 1, nullSrv);
    return true;
}

} // namespace rm
