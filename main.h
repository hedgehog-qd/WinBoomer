#pragma once
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <wincodec.h>
#include <d3dcompiler.h>
#include <math.h>
#include "resource.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

#define WM_TRAY      (WM_APP + 1)
#define HOTKEY_START 1
#define IDM_ABOUT    2001
#define IDM_QUIT     2002
#define IDM_CLAMP 2003

static const char* g_shaderSrc = R"(
cbuffer Xform : register(b0) {
    float2 uvScale;
    float2 uvOffset;
};
Texture2D    tex : register(t0);
SamplerState smp : register(s0);

struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID) {
    VSOut o;
    float2 baseUV = float2((id << 1) & 2, id & 2);   // (0,0)(2,0)(0,2)
    o.uv  = baseUV * uvScale + uvOffset;             // Transform
    o.pos = float4(baseUV.x * 2 - 1, 1 - baseUV.y * 2, 0, 1);
    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    return float4(tex.Sample(smp, i.uv).rgb, 1);
}
)";

struct Capture {
    ComPtr<ID3D11Device>        dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<IDXGIOutput1>        output1;   // Main monitor's output
    UINT width = 0, height = 0;
    int originX = 0, originY = 0;

    bool InitDevice() {
        D3D_FEATURE_LEVEL fl;
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            0, nullptr, 0, D3D11_SDK_VERSION, &dev, &fl, &ctx);
        if (FAILED(hr)) return false;

        ComPtr<IDXGIDevice>  dxgiDev;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIOutput>  output;
        if (FAILED(dev.As(&dxgiDev)))               return false;
        if (FAILED(dxgiDev->GetAdapter(&adapter)))  return false;
        if (FAILED(adapter->EnumOutputs(0, &output)))return false; // 0 = Main screen
        DXGI_OUTPUT_DESC od; output->GetDesc(&od);
        originX = od.DesktopCoordinates.left;
        originY = od.DesktopCoordinates.top;
        width = od.DesktopCoordinates.right - od.DesktopCoordinates.left;
        height = od.DesktopCoordinates.bottom - od.DesktopCoordinates.top;
        return SUCCEEDED(output.As(&output1));
    }


    bool CaptureFrame(ComPtr<ID3D11Texture2D>& outTex) {
        ComPtr<IDXGIOutputDuplication> dupl;
        if (FAILED(output1->DuplicateOutput(dev.Get(), &dupl))) return false;

        DXGI_OUTDUPL_FRAME_INFO info;
        DWORD start = GetTickCount();
        while (GetTickCount() - start < 1500) {           // Wait for 1.5s for maximum
            ComPtr<IDXGIResource> res;
            HRESULT hr = dupl->AcquireNextFrame(100, &info, &res);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
            if (FAILED(hr)) return false;

            if (info.LastPresentTime.QuadPart == 0) {     // metadata - drop this
                dupl->ReleaseFrame();
                continue;                                   // wait for the maximum
            }

            ComPtr<ID3D11Texture2D> frame;
            res.As(&frame);

            D3D11_TEXTURE2D_DESC desc; frame->GetDesc(&desc);
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0; desc.MiscFlags = 0;
            ComPtr<ID3D11Texture2D> copy;
            if (FAILED(dev->CreateTexture2D(&desc, nullptr, &copy))) { dupl->ReleaseFrame(); return false; }
            ctx->CopyResource(copy.Get(), frame.Get());   // copy before ReleaseFrame
            dupl->ReleaseFrame();
            outTex = copy;
            return true;
        }
        return false;   // 1500ms timeout
    }
};

struct Renderer {
    ComPtr<IDXGISwapChain1>          swap;
    ComPtr<ID3D11RenderTargetView>  rtv;
    ComPtr<ID3D11VertexShader>      vs;
    ComPtr<ID3D11PixelShader>       ps;
    ComPtr<ID3D11SamplerState>      samp;
    ComPtr<ID3D11ShaderResourceView> srv;   // current frame
    UINT vpW = 0, vpH = 0;
    ComPtr<ID3D11Buffer> cb;
    float scale = 1.0f;
    // render constant for now
    float offX = 0.0f, offY = 0.0f;     // offset of capture
    // animation constant
    float tScale = 1.0f, tOffX = 0.0f, tOffY = 0.0f;
    UINT texW = 0, texH = 0;            // size of image
    bool clamp = true;                  // clamp

    bool Init(HWND hwnd, ID3D11Device* dev, UINT w, UINT h) {
        vpW = w; vpH = h;
        texW = w; texH = h;
        ComPtr<IDXGIDevice>  dxgiDev;  dev->QueryInterface(IID_PPV_ARGS(&dxgiDev));
        ComPtr<IDXGIAdapter> adapter;  dxgiDev->GetAdapter(&adapter);
        ComPtr<IDXGIFactory2> factory; adapter->GetParent(IID_PPV_ARGS(&factory));

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = w; sd.Height = h;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.SampleDesc.Count = 1;
        if (FAILED(factory->CreateSwapChainForHwnd(dev, hwnd, &sd, nullptr, nullptr, &swap)))
            return false;

        ComPtr<ID3D11Texture2D> back;
        swap->GetBuffer(0, IID_PPV_ARGS(&back));
        if (FAILED(dev->CreateRenderTargetView(back.Get(), nullptr, &rtv))) return false;

        ComPtr<ID3DBlob> vsb, psb, err;
        if (FAILED(D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
            "VSMain", "vs_4_0", 0, 0, &vsb, &err))) return false;
        if (FAILED(D3DCompile(g_shaderSrc, strlen(g_shaderSrc), nullptr, nullptr, nullptr,
            "PSMain", "ps_4_0", 0, 0, &psb, &err))) return false;
        dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
        dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);

        D3D11_SAMPLER_DESC smp = {};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;        // linear
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        dev->CreateSamplerState(&smp, &samp);

        D3D11_BUFFER_DESC bd = {};
        bd.ByteWidth = 16;                    // 2 * float = 16b
        bd.Usage = D3D11_USAGE_DYNAMIC;
        bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        dev->CreateBuffer(&bd, nullptr, &cb);

        return true;
    }

    void ClampTarget() {
        if (!clamp) return;                       // if CLAMP not select then ignore
        float scaledW = texW * tScale;
        float scaledH = texH * tScale;
        if (scaledW >= vpW) {
            if (tOffX > 0)               tOffX = 0;
            if (tOffX < vpW - scaledW)   tOffX = vpW - scaledW;
        }
        else {
            tOffX = (vpW - scaledW) * 0.5f;
        }
        if (scaledH >= vpH) {
            if (tOffY > 0)               tOffY = 0;
            if (tOffY < vpH - scaledH)   tOffY = vpH - scaledH;
        }
        else {
            tOffY = (vpH - scaledH) * 0.5f;
        }
    }

    void Reset() {
        scale = tScale = 1.0f;
        offX = offY = tOffX = tOffY = 0.0f;
    }

    bool NeedsRedraw() const {
        return fabsf(tScale - scale) > 0.0005f
            || fabsf(tOffX - offX) > 0.05f
            || fabsf(tOffY - offY) > 0.05f;
    }

    void UpdateCB(ID3D11DeviceContext* ctx) {
        struct CB { float uvScaleX, uvScaleY, uvOffX, uvOffY; } data;
        data.uvScaleX = 1.0f / scale;
        data.uvScaleY = 1.0f / scale;
        data.uvOffX = -offX / (scale * texW);
        data.uvOffY = -offY / (scale * texH);
        D3D11_MAPPED_SUBRESOURCE m;
        ctx->Map(cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
        memcpy(m.pData, &data, sizeof(data));
        ctx->Unmap(cb.Get(), 0);
    }

    // mouse wheel
    void ZoomAt(float mx, float my, float factor) {
        tOffX = mx - (mx - tOffX) * factor;
        tOffY = my - (my - tOffY) * factor;
        tScale *= factor;
        if (tScale < 1.0f) { tScale = 1.0f; tOffX = tOffY = 0.0f; }
        ClampTarget();
    }

    // mouse drag: pan
    void Pan(float dx, float dy) {
        tOffX += dx; tOffY += dy;
        ClampTarget();
    }

    void ResetView() {                  // reset view
        tScale = 1.0f; tOffX = 0.0f; tOffY = 0.0f;
    }


    bool Animate() {
        const float k = 0.25f;          // 0~1
        float ds = tScale - scale;
        float dx = tOffX - offX;
        float dy = tOffY - offY;
        scale += ds * k;
        offX += dx * k;
        offY += dy * k;
        if (fabsf(ds) < 0.0005f && fabsf(dx) < 0.05f && fabsf(dy) < 0.05f) {
            scale = tScale; offX = tOffX; offY = tOffY;
            return false;
        }
        return true;
    }

    bool SetFrame(ID3D11Device* dev, ID3D11Texture2D* tex) {
        srv.Reset();
        return SUCCEEDED(dev->CreateShaderResourceView(tex, nullptr, &srv));
    }

    void Draw(ID3D11DeviceContext* ctx) {
        D3D11_VIEWPORT vp = { 0, 0, (float)vpW, (float)vpH, 0, 1 };
        ctx->RSSetViewports(1, &vp);
        ctx->OMSetRenderTargets(1, rtv.GetAddressOf(), nullptr);
        float clear[4] = { 0,0,0,1 };
        UpdateCB(ctx);

        ctx->ClearRenderTargetView(rtv.Get(), clear);

        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->PSSetShader(ps.Get(), nullptr, 0);
        ctx->PSSetShaderResources(0, 1, srv.GetAddressOf());
        ctx->PSSetSamplers(0, 1, samp.GetAddressOf());
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->IASetInputLayout(nullptr);

        ctx->VSSetShader(vs.Get(), nullptr, 0);
        ctx->VSSetConstantBuffers(0, 1, cb.GetAddressOf());
        ctx->PSSetShader(ps.Get(), nullptr, 0);

        ctx->Draw(3, 0);                          // triangle

        swap->Present(1, 0);                      // 1 = vsync
    }
};


static void ShowOverlay();
static void HideOverlay();
static void ShowTrayMenu(HWND hwnd);
static bool SaveTextureToPng(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D* tex, const wchar_t* path);
// Hot Keys
#define IDC_CAP   101
#define IDC_EXIT  102
#define IDC_RESET 103
#define IDC_OK    104
#define IDC_CANCEL 105
#define IDM_SETTINGS 2004
struct Config {
    UINT keyCapture = 'Z';        // capture trigger
    UINT keyExit = VK_ESCAPE;  // release
    UINT keyReset = '0';        // reset
};
static void SaveConfig();
static void LoadConfig();
static void KeyName(UINT vk, wchar_t* buf, int cb);
static void RefreshDlgButtons();
LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void OpenSettings(HINSTANCE hi);
