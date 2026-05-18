// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Test App — Minimal DX11 uygulamasi
// Pencere acar, sürekli Present() cagirir, hook test icin idealdir
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <math.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, WndProc, 0, 0,
                       hInst, NULL, NULL, NULL, NULL, L"TestAppClass", NULL };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"ShadowRec DX11 Test App",
                              WS_OVERLAPPEDWINDOW, 100, 100, 800, 600,
                              NULL, NULL, hInst, NULL);

    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 800;
    scd.BufferDesc.Height = 600;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;

    D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &scd, &swapChain, &device, nullptr, &context);

    ID3D11Texture2D* backBuffer = nullptr;
    swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    ID3D11RenderTargetView* rtv = nullptr;
    device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
    backBuffer->Release();

    ShowWindow(hwnd, SW_SHOW);

    MSG msg = {};
    float t = 0.0f;
    while (msg.message != WM_QUIT) {
        if (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            t += 0.01f;
            float r = (sinf(t) + 1) * 0.5f;
            float g = (sinf(t + 2) + 1) * 0.5f;
            float b = (sinf(t + 4) + 1) * 0.5f;
            float color[4] = { r, g, b, 1.0f };
            context->ClearRenderTargetView(rtv, color);
            
            swapChain->Present(1, 0);
        }
    }

    rtv->Release();
    context->Release();
    device->Release();
    swapChain->Release();
    return 0;
}