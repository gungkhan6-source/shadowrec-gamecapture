// ═══════════════════════════════════════════════════════════════════════
// ShadowRec Hook DLL v0.6 — Universal Graphics API Hook
// 
// Destekler:
//   • DirectX 9    (IDirect3DDevice9::Present)
//   • DirectX 10   (IDXGISwapChain::Present)
//   • DirectX 11   (IDXGISwapChain::Present)
//   • DirectX 11.1 (IDXGISwapChain1::Present1) - flip model
//   • DirectX 12   (IDXGISwapChain3::Present + ExecuteCommandLists)
//   • OpenGL       (wglSwapBuffers)
// ═══════════════════════════════════════════════════════════════════════

#include <Windows.h>
#include <d3d9.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <MinHook.h>
#include <fstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "opengl32.lib")

// ═══════════════════════════════════════════════════════════════════════
// Shared memory
// ═══════════════════════════════════════════════════════════════════════
#define SHM_NAME L"ShadowRecFrameBuffer"
#define EVENT_NAME L"ShadowRecFrameEvent"
#define MAX_WIDTH 3840
#define MAX_HEIGHT 2160
#define MAX_FRAME_SIZE (MAX_WIDTH * MAX_HEIGHT * 4)

struct SharedFrameHeader {
    UINT32 width;
    UINT32 height;
    UINT32 frameNumber;
    UINT32 pixelFormat;  // 0=BGRA, 1=RGBA
    UINT64 timestamp;
    UINT32 dataSize;
    UINT32 reserved;
};

#define SHM_SIZE (sizeof(SharedFrameHeader) + MAX_FRAME_SIZE)

static HANDLE g_shmHandle = NULL;
static SharedFrameHeader* g_shmHeader = NULL;
static UINT8* g_shmPixels = NULL;
static HANDLE g_frameEvent = NULL;

void WriteLog(const std::string& m) {
    char tempPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tempPath);
    std::string logPath = std::string(tempPath) + "ShadowRec_HookLog.txt";
    std::ofstream log(logPath, std::ios::app);
    if (log.is_open()) {
        SYSTEMTIME st; GetLocalTime(&st);
        char ts[64];
        sprintf_s(ts, "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        log << ts << m << std::endl;
    }
}

bool InitSharedMemory() {
    g_shmHandle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
                                      0, SHM_SIZE, SHM_NAME);
    if (!g_shmHandle) { WriteLog("HATA: CreateFileMapping"); return false; }
    void* mapped = MapViewOfFile(g_shmHandle, FILE_MAP_ALL_ACCESS, 0, 0, SHM_SIZE);
    if (!mapped) { WriteLog("HATA: MapViewOfFile"); return false; }
    g_shmHeader = (SharedFrameHeader*)mapped;
    g_shmPixels = (UINT8*)mapped + sizeof(SharedFrameHeader);
    g_frameEvent = CreateEventW(NULL, FALSE, FALSE, EVENT_NAME);
    WriteLog("Shared memory hazir");
    return true;
}

void CleanupSharedMemory() {
    if (g_frameEvent) { CloseHandle(g_frameEvent); g_frameEvent = NULL; }
    if (g_shmHeader) { UnmapViewOfFile(g_shmHeader); g_shmHeader = NULL; }
    if (g_shmHandle) { CloseHandle(g_shmHandle); g_shmHandle = NULL; }
}

void WriteFrameToSHM(int w, int h, const void* pixels, int rowPitch, UINT32 fmt = 0) {
    if (!g_shmHeader || w <= 0 || h <= 0 || w > MAX_WIDTH || h > MAX_HEIGHT) return;
    
    UINT32 rowSize = w * 4;
    for (int y = 0; y < h; y++) {
        memcpy(g_shmPixels + y * rowSize, (UINT8*)pixels + y * rowPitch, rowSize);
    }
    g_shmHeader->width = w;
    g_shmHeader->height = h;
    g_shmHeader->frameNumber++;
    g_shmHeader->pixelFormat = fmt;
    g_shmHeader->dataSize = rowSize * h;
    LARGE_INTEGER qpc; QueryPerformanceCounter(&qpc);
    g_shmHeader->timestamp = qpc.QuadPart;
    if (g_frameEvent) SetEvent(g_frameEvent);
}

// ═══════════════════════════════════════════════════════════════════════
// DX9 CAPTURE
// ═══════════════════════════════════════════════════════════════════════
static IDirect3DDevice9* g_dx9Device = nullptr;
static IDirect3DSurface9* g_dx9Surface = nullptr;
static int g_dx9LastW = 0, g_dx9LastH = 0;

bool CaptureFrameDX9(IDirect3DDevice9* device) {
    if (!g_shmHeader) return false;
    
    IDirect3DSurface9* backBuffer = nullptr;
    HRESULT hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backBuffer);
    if (FAILED(hr) || !backBuffer) return false;
    
    D3DSURFACE_DESC desc;
    backBuffer->GetDesc(&desc);
    int w = desc.Width;
    int h = desc.Height;
    
    if (!g_dx9Surface || g_dx9LastW != w || g_dx9LastH != h) {
        if (g_dx9Surface) { g_dx9Surface->Release(); g_dx9Surface = nullptr; }
        hr = device->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8,
            D3DPOOL_SYSTEMMEM, &g_dx9Surface, nullptr);
        if (FAILED(hr)) { backBuffer->Release(); return false; }
        g_dx9LastW = w; g_dx9LastH = h;
        char info[64]; sprintf_s(info, "DX9 Staging: %dx%d", w, h);
        WriteLog(info);
    }
    
    hr = device->GetRenderTargetData(backBuffer, g_dx9Surface);
    backBuffer->Release();
    if (FAILED(hr)) return false;
    
    D3DLOCKED_RECT locked;
    hr = g_dx9Surface->LockRect(&locked, NULL, D3DLOCK_READONLY);
    if (FAILED(hr)) return false;
    
    WriteFrameToSHM(w, h, locked.pBits, locked.Pitch);
    g_dx9Surface->UnlockRect();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// DX11 CAPTURE (DX10 ile aynı arayüz - IDXGISwapChain)
// ═══════════════════════════════════════════════════════════════════════
static ID3D11Device* g_d3d11Device = nullptr;
static ID3D11DeviceContext* g_d3d11Context = nullptr;
static ID3D11Texture2D* g_d3d11StagingTex = nullptr;
static int g_d3d11LastW = 0, g_d3d11LastH = 0;

bool CaptureFrameDX11(IDXGISwapChain* swapChain) {
    if (!g_shmHeader) return false;
    
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&backBuffer);
    if (FAILED(hr)) return false;
    
    if (!g_d3d11Device) {
        swapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_d3d11Device);
        if (g_d3d11Device) g_d3d11Device->GetImmediateContext(&g_d3d11Context);
    }
    if (!g_d3d11Device || !g_d3d11Context) { backBuffer->Release(); return false; }
    
    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);
    
    // ⭐ Format tespiti — RGBA mı BGRA mı?
    // 0 = BGRA (B8G8R8A8_UNORM), 1 = RGBA (R8G8B8A8_UNORM)
    UINT32 pixFmt = 0;  // default BGRA
    if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
        desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
        desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) {
        pixFmt = 1;  // RGBA
    }
    
    if (!g_d3d11StagingTex || g_d3d11LastW != (int)desc.Width || g_d3d11LastH != (int)desc.Height) {
        if (g_d3d11StagingTex) { g_d3d11StagingTex->Release(); g_d3d11StagingTex = nullptr; }
        D3D11_TEXTURE2D_DESC sd = desc;
        sd.Usage = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        sd.BindFlags = 0;
        sd.MiscFlags = 0;
        hr = g_d3d11Device->CreateTexture2D(&sd, nullptr, &g_d3d11StagingTex);
        if (FAILED(hr)) { backBuffer->Release(); return false; }
        g_d3d11LastW = desc.Width; g_d3d11LastH = desc.Height;
        char info[96]; sprintf_s(info, "DX11 Staging: %dx%d format=%d (%s)",
            desc.Width, desc.Height, desc.Format, pixFmt == 1 ? "RGBA" : "BGRA");
        WriteLog(info);
    }
    
    g_d3d11Context->CopyResource(g_d3d11StagingTex, backBuffer);
    
    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = g_d3d11Context->Map(g_d3d11StagingTex, 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) { backBuffer->Release(); return false; }
    
    WriteFrameToSHM(g_d3d11LastW, g_d3d11LastH, mapped.pData, mapped.RowPitch, pixFmt);
    g_d3d11Context->Unmap(g_d3d11StagingTex, 0);
    backBuffer->Release();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// DX12 CAPTURE
// ═══════════════════════════════════════════════════════════════════════
static ID3D12Device* g_d3d12Device = nullptr;
static ID3D12CommandQueue* g_d3d12CommandQueue = nullptr;
static ID3D12CommandAllocator* g_d3d12CmdAlloc = nullptr;
static ID3D12GraphicsCommandList* g_d3d12CmdList = nullptr;
static ID3D12Resource* g_d3d12StagingBuffer = nullptr;
static ID3D12Fence* g_d3d12Fence = nullptr;
static UINT64 g_d3d12FenceValue = 0;
static HANDLE g_d3d12FenceEvent = NULL;
static int g_d3d12LastW = 0, g_d3d12LastH = 0;
static UINT64 g_d3d12RowPitch = 0;
static DXGI_FORMAT g_d3d12Format = DXGI_FORMAT_UNKNOWN;

bool InitDX12Capture(ID3D12Device* device, ID3D12CommandQueue* queue) {
    HRESULT hr;
    g_d3d12Device = device;
    g_d3d12CommandQueue = queue;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
        __uuidof(ID3D12CommandAllocator), (void**)&g_d3d12CmdAlloc);
    if (FAILED(hr)) return false;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        g_d3d12CmdAlloc, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g_d3d12CmdList);
    if (FAILED(hr)) return false;
    g_d3d12CmdList->Close();
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
        __uuidof(ID3D12Fence), (void**)&g_d3d12Fence);
    if (FAILED(hr)) return false;
    g_d3d12FenceEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    WriteLog("DX12 capture init OK");
    return true;
}

bool CaptureFrameDX12(IDXGISwapChain3* swapChain) {
    if (!g_shmHeader || !g_d3d12Device || !g_d3d12CommandQueue) return false;
    UINT bufferIndex = swapChain->GetCurrentBackBufferIndex();
    ID3D12Resource* backBuffer = nullptr;
    HRESULT hr = swapChain->GetBuffer(bufferIndex, __uuidof(ID3D12Resource), (void**)&backBuffer);
    if (FAILED(hr)) return false;
    
    D3D12_RESOURCE_DESC desc = backBuffer->GetDesc();
    int w = (int)desc.Width;
    int h = (int)desc.Height;
    
    if (!g_d3d12StagingBuffer || w != g_d3d12LastW || h != g_d3d12LastH || desc.Format != g_d3d12Format) {
        if (g_d3d12StagingBuffer) { g_d3d12StagingBuffer->Release(); g_d3d12StagingBuffer = nullptr; }
        g_d3d12RowPitch = (w * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        UINT64 bufferSize = g_d3d12RowPitch * h;
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bd = {};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = bufferSize; bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1;
        bd.Format = DXGI_FORMAT_UNKNOWN; bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = g_d3d12Device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
            &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
            __uuidof(ID3D12Resource), (void**)&g_d3d12StagingBuffer);
        if (FAILED(hr)) { backBuffer->Release(); return false; }
        g_d3d12LastW = w; g_d3d12LastH = h; g_d3d12Format = desc.Format;
        char info[64]; sprintf_s(info, "DX12 Staging: %dx%d", w, h);
        WriteLog(info);
    }
    
    g_d3d12CmdAlloc->Reset();
    g_d3d12CmdList->Reset(g_d3d12CmdAlloc, nullptr);
    
    D3D12_RESOURCE_BARRIER b = {};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = backBuffer;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    g_d3d12CmdList->ResourceBarrier(1, &b);
    
    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = backBuffer;
    src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = g_d3d12StagingBuffer;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format = desc.Format;
    dst.PlacedFootprint.Footprint.Width = w;
    dst.PlacedFootprint.Footprint.Height = h;
    dst.PlacedFootprint.Footprint.Depth = 1;
    dst.PlacedFootprint.Footprint.RowPitch = (UINT)g_d3d12RowPitch;
    g_d3d12CmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    g_d3d12CmdList->ResourceBarrier(1, &b);
    g_d3d12CmdList->Close();
    
    ID3D12CommandList* lists[] = { g_d3d12CmdList };
    g_d3d12CommandQueue->ExecuteCommandLists(1, lists);
    g_d3d12FenceValue++;
    g_d3d12CommandQueue->Signal(g_d3d12Fence, g_d3d12FenceValue);
    if (g_d3d12Fence->GetCompletedValue() < g_d3d12FenceValue) {
        g_d3d12Fence->SetEventOnCompletion(g_d3d12FenceValue, g_d3d12FenceEvent);
        WaitForSingleObject(g_d3d12FenceEvent, 100);
    }
    
    void* mapped = nullptr;
    D3D12_RANGE readRange = { 0, (size_t)(g_d3d12RowPitch * h) };
    if (SUCCEEDED(g_d3d12StagingBuffer->Map(0, &readRange, &mapped))) {
        // ⭐ Format tespiti — RGBA mı BGRA mı?
        UINT32 pixFmt = 0;  // default BGRA
        if (desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
            desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
            desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) {
            pixFmt = 1;  // RGBA
        }
        WriteFrameToSHM(w, h, mapped, (int)g_d3d12RowPitch, pixFmt);
        D3D12_RANGE writeRange = { 0, 0 };
        g_d3d12StagingBuffer->Unmap(0, &writeRange);
    }
    backBuffer->Release();
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// OPENGL CAPTURE
// ═══════════════════════════════════════════════════════════════════════
typedef void (WINAPI *glReadPixels_t)(int, int, int, int, unsigned int, unsigned int, void*);
typedef void (WINAPI *glGetIntegerv_t)(unsigned int, int*);

static glReadPixels_t pglReadPixels = nullptr;
static glGetIntegerv_t pglGetIntegerv = nullptr;
static std::vector<uint8_t> g_glBuffer;
static int g_glLastW = 0, g_glLastH = 0;

#define GL_RGBA 0x1908
#define GL_BGRA 0x80E1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_VIEWPORT 0x0BA2

bool LoadGLFunctions() {
    if (pglReadPixels && pglGetIntegerv) return true;
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) return false;
    pglReadPixels = (glReadPixels_t)GetProcAddress(gl, "glReadPixels");
    pglGetIntegerv = (glGetIntegerv_t)GetProcAddress(gl, "glGetIntegerv");
    return pglReadPixels && pglGetIntegerv;
}

bool CaptureFrameOpenGL(HDC hdc) {
    if (!g_shmHeader || !LoadGLFunctions()) return false;
    
    int viewport[4] = { 0, 0, 0, 0 };
    pglGetIntegerv(GL_VIEWPORT, viewport);
    int w = viewport[2];
    int h = viewport[3];
    if (w <= 0 || h <= 0) return false;
    
    if (w != g_glLastW || h != g_glLastH) {
        g_glLastW = w; g_glLastH = h;
        g_glBuffer.resize(w * h * 4);
        char info[64]; sprintf_s(info, "OpenGL Viewport: %dx%d", w, h);
        WriteLog(info);
    }
    
    pglReadPixels(0, 0, w, h, GL_BGRA, GL_UNSIGNED_BYTE, g_glBuffer.data());
    
    // OpenGL alttan üste tarar — flip et
    int rowSize = w * 4;
    std::vector<uint8_t> flipped(w * h * 4);
    for (int y = 0; y < h; y++) {
        memcpy(flipped.data() + y * rowSize,
               g_glBuffer.data() + (h - 1 - y) * rowSize,
               rowSize);
    }
    
    WriteFrameToSHM(w, h, flipped.data(), rowSize);
    return true;
}

// ═══════════════════════════════════════════════════════════════════════
// HOOK FUNCTIONS
// ═══════════════════════════════════════════════════════════════════════

// DX9
typedef HRESULT(STDMETHODCALLTYPE* DX9_Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
static DX9_Present_t Orig_DX9_Present = nullptr;
static int g_dx9FrameCount = 0;

HRESULT STDMETHODCALLTYPE Hook_DX9_Present(IDirect3DDevice9* This, const RECT* src, const RECT* dst, HWND hwnd, const RGNDATA* dirty) {
    g_dx9FrameCount++;
    if (g_dx9FrameCount == 1 || g_dx9FrameCount == 100 || g_dx9FrameCount % 600 == 0)
        WriteLog("DX9 Present Frame=" + std::to_string(g_dx9FrameCount));
    CaptureFrameDX9(This);
    return Orig_DX9_Present(This, src, dst, hwnd, dirty);
}

// DX11 Present
typedef HRESULT(STDMETHODCALLTYPE* DXGI_Present_t)(IDXGISwapChain*, UINT, UINT);
static DXGI_Present_t Orig_DXGI_Present = nullptr;
static int g_dx11FrameCount = 0;

HRESULT STDMETHODCALLTYPE Hook_DXGI_Present(IDXGISwapChain* sc, UINT sync, UINT flags) {
    g_dx11FrameCount++;
    if (g_dx11FrameCount == 1 || g_dx11FrameCount == 100 || g_dx11FrameCount % 600 == 0)
        WriteLog("DX11 Present Frame=" + std::to_string(g_dx11FrameCount));
    CaptureFrameDX11(sc);
    return Orig_DXGI_Present(sc, sync, flags);
}

// DX11.1 Present1 (flip model)
typedef HRESULT(STDMETHODCALLTYPE* DXGI_Present1_t)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
static DXGI_Present1_t Orig_DXGI_Present1 = nullptr;
static int g_dx11_1FrameCount = 0;

HRESULT STDMETHODCALLTYPE Hook_DXGI_Present1(IDXGISwapChain1* sc, UINT sync, UINT flags, const DXGI_PRESENT_PARAMETERS* params) {
    g_dx11_1FrameCount++;
    if (g_dx11_1FrameCount == 1 || g_dx11_1FrameCount == 100 || g_dx11_1FrameCount % 600 == 0)
        WriteLog("DX11.1 Present1 Frame=" + std::to_string(g_dx11_1FrameCount));
    CaptureFrameDX11((IDXGISwapChain*)sc);
    return Orig_DXGI_Present1(sc, sync, flags, params);
}

// DX12
typedef HRESULT(STDMETHODCALLTYPE* DX12_Present_t)(IDXGISwapChain3*, UINT, UINT);
typedef void(STDMETHODCALLTYPE* DX12_ExecCmd_t)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
static DX12_Present_t Orig_DX12_Present = nullptr;
static DX12_ExecCmd_t Orig_DX12_ExecCmd = nullptr;
static int g_dx12FrameCount = 0;
static ID3D12CommandQueue* g_capturedQueue = nullptr;

void STDMETHODCALLTYPE Hook_DX12_ExecCmd(ID3D12CommandQueue* This, UINT n, ID3D12CommandList* const* lists) {
    if (!g_capturedQueue) {
        D3D12_COMMAND_QUEUE_DESC d = This->GetDesc();
        if (d.Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
            g_capturedQueue = This;
            WriteLog("DX12 DIRECT Queue yakalandi");
        }
    }
    Orig_DX12_ExecCmd(This, n, lists);
}

HRESULT STDMETHODCALLTYPE Hook_DX12_Present(IDXGISwapChain3* sc, UINT sync, UINT flags) {
    g_dx12FrameCount++;
    if (g_dx12FrameCount == 1 || g_dx12FrameCount == 100 || g_dx12FrameCount % 600 == 0)
        WriteLog("DX12 Present Frame=" + std::to_string(g_dx12FrameCount));
    if (!g_d3d12Device && g_capturedQueue) {
        ID3D12Device* dev = nullptr;
        if (SUCCEEDED(sc->GetDevice(__uuidof(ID3D12Device), (void**)&dev))) {
            InitDX12Capture(dev, g_capturedQueue);
            dev->Release();
        }
    }
    if (g_d3d12Device && g_capturedQueue) CaptureFrameDX12(sc);
    return Orig_DX12_Present(sc, sync, flags);
}

// OpenGL wglSwapBuffers
typedef BOOL(WINAPI* wglSwapBuffers_t)(HDC);
static wglSwapBuffers_t Orig_wglSwapBuffers = nullptr;
static int g_glFrameCount = 0;

BOOL WINAPI Hook_wglSwapBuffers(HDC hdc) {
    g_glFrameCount++;
    if (g_glFrameCount == 1 || g_glFrameCount == 100 || g_glFrameCount % 600 == 0)
        WriteLog("OpenGL SwapBuffers Frame=" + std::to_string(g_glFrameCount));
    CaptureFrameOpenGL(hdc);
    return Orig_wglSwapBuffers(hdc);
}

// ═══════════════════════════════════════════════════════════════════════
// VTABLE ADDRESS RESOLVERS
// ═══════════════════════════════════════════════════════════════════════

void* GetDX9PresentAddress() {
    HMODULE d3d9 = GetModuleHandleW(L"d3d9.dll");
    if (!d3d9) return nullptr;
    
    typedef IDirect3D9*(WINAPI* Direct3DCreate9_t)(UINT);
    Direct3DCreate9_t pCreate = (Direct3DCreate9_t)GetProcAddress(d3d9, "Direct3DCreate9");
    if (!pCreate) return nullptr;
    
    IDirect3D9* d3d = pCreate(D3D_SDK_VERSION);
    if (!d3d) return nullptr;
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0,
                       GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"DummyDX9", NULL };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(L"DummyDX9", L"DummyDX9", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.hDeviceWindow = hwnd;
    
    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
        D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
    
    void* addr = nullptr;
    if (SUCCEEDED(hr) && device) {
        void** vtable = *reinterpret_cast<void***>(device);
        // IDirect3DDevice9::Present = vtable index 17
        addr = vtable[17];
        device->Release();
    }
    d3d->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(L"DummyDX9", wc.hInstance);
    return addr;
}

bool GetDXGIAddresses(void** outPresent, void** outPresent1) {
    *outPresent = nullptr;
    *outPresent1 = nullptr;
    
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    if (!d3d11) return false;
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0,
                       GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"DummyDXGI", NULL };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(L"DummyDXGI", L"DummyDXGI", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferCount = 1;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd;
    scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION,
        &scd, &sc, &dev, nullptr, &ctx);
    
    if (SUCCEEDED(hr) && sc) {
        void** vtable = *reinterpret_cast<void***>(sc);
        // IDXGISwapChain::Present = VTable index 8
        *outPresent = vtable[8];
        
        // Try IDXGISwapChain1::Present1
        IDXGISwapChain1* sc1 = nullptr;
        if (SUCCEEDED(sc->QueryInterface(__uuidof(IDXGISwapChain1), (void**)&sc1))) {
            void** vtable1 = *reinterpret_cast<void***>(sc1);
            // IDXGISwapChain1::Present1 = VTable index 22
            *outPresent1 = vtable1[22];
            sc1->Release();
        }
        sc->Release();
        ctx->Release();
        dev->Release();
    }
    
    DestroyWindow(hwnd);
    UnregisterClassW(L"DummyDXGI", wc.hInstance);
    return *outPresent != nullptr;
}

bool GetDX12Addresses(void** outPresent, void** outExecCmd) {
    *outPresent = nullptr;
    *outExecCmd = nullptr;
    
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll");
    if (!d3d12) return false;
    
    typedef HRESULT(WINAPI* D3D12CreateDevice_t)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    D3D12CreateDevice_t pCreate = (D3D12CreateDevice_t)GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!pCreate) return false;
    
    ID3D12Device* device = nullptr;
    if (FAILED(pCreate(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device)) || !device)
        return false;
    
    D3D12_COMMAND_QUEUE_DESC qd = {};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = nullptr;
    if (FAILED(device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&queue))) {
        device->Release(); return false;
    }
    
    void** qVT = *reinterpret_cast<void***>(queue);
    *outExecCmd = qVT[10];  // ExecuteCommandLists index 10
    
    WNDCLASSEXW wc = { sizeof(WNDCLASSEXW), CS_CLASSDC, DefWindowProcW, 0, 0,
                       GetModuleHandleW(NULL), NULL, NULL, NULL, NULL, L"DummyDX12", NULL };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(L"DummyDX12", L"DummyDX12", WS_OVERLAPPEDWINDOW,
                              0, 0, 100, 100, NULL, NULL, wc.hInstance, NULL);
    
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory))) {
        DestroyWindow(hwnd);
        UnregisterClassW(L"DummyDX12", wc.hInstance);
        queue->Release(); device->Release();
        return false;
    }
    
    DXGI_SWAP_CHAIN_DESC1 scd = {};
    scd.BufferCount = 2; scd.Width = 100; scd.Height = 100;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    
    IDXGISwapChain1* sc1 = nullptr;
    HRESULT hr = factory->CreateSwapChainForHwnd((IUnknown*)queue, hwnd, &scd, nullptr, nullptr, &sc1);
    if (SUCCEEDED(hr) && sc1) {
        void** scVT = *reinterpret_cast<void***>(sc1);
        *outPresent = scVT[8];
        sc1->Release();
    }
    factory->Release();
    DestroyWindow(hwnd);
    UnregisterClassW(L"DummyDX12", wc.hInstance);
    queue->Release(); device->Release();
    return *outPresent != nullptr;
}

void* GetOpenGLAddress() {
    HMODULE gl = GetModuleHandleW(L"opengl32.dll");
    if (!gl) return nullptr;
    return (void*)GetProcAddress(gl, "wglSwapBuffers");
}

// ═══════════════════════════════════════════════════════════════════════
// HOOK INSTALLATION
// ═══════════════════════════════════════════════════════════════════════
DWORD WINAPI HookThread(LPVOID) {
    WriteLog("=== HookThread: Universal Capture ===");
    
    if (!InitSharedMemory()) return 1;
    if (MH_Initialize() != MH_OK) { WriteLog("HATA: MH_Initialize"); return 1; }
    
    int hooked = 0;
    
    // DX9
    if (GetModuleHandleW(L"d3d9.dll")) {
        void* addr = GetDX9PresentAddress();
        if (addr) {
            char b[64]; sprintf_s(b, "DX9 Present: 0x%p", addr); WriteLog(b);
            if (MH_CreateHook(addr, &Hook_DX9_Present, (LPVOID*)&Orig_DX9_Present) == MH_OK) {
                MH_EnableHook(addr);
                WriteLog("DX9 Hook AKTIF"); hooked++;
            }
        }
    } else {
        WriteLog("d3d9.dll yok, DX9 atlandi");
    }
    
    // DXGI (DX10/11/11.1)
    if (GetModuleHandleW(L"d3d11.dll") || GetModuleHandleW(L"dxgi.dll")) {
        void* present = nullptr;
        void* present1 = nullptr;
        if (GetDXGIAddresses(&present, &present1)) {
            if (present) {
                char b[64]; sprintf_s(b, "DXGI Present: 0x%p", present); WriteLog(b);
                if (MH_CreateHook(present, &Hook_DXGI_Present, (LPVOID*)&Orig_DXGI_Present) == MH_OK) {
                    MH_EnableHook(present);
                    WriteLog("DXGI Present Hook AKTIF"); hooked++;
                }
            }
            if (present1) {
                char b[64]; sprintf_s(b, "DXGI Present1: 0x%p", present1); WriteLog(b);
                if (MH_CreateHook(present1, &Hook_DXGI_Present1, (LPVOID*)&Orig_DXGI_Present1) == MH_OK) {
                    MH_EnableHook(present1);
                    WriteLog("DXGI Present1 Hook AKTIF"); hooked++;
                }
            }
        }
    } else {
        WriteLog("d3d11.dll yok, DXGI atlandi");
    }
    
    // DX12
    if (GetModuleHandleW(L"d3d12.dll")) {
        void* present = nullptr;
        void* exec = nullptr;
        if (GetDX12Addresses(&present, &exec)) {
            char b[128]; sprintf_s(b, "DX12 Present: 0x%p, ExecCmd: 0x%p", present, exec); WriteLog(b);
            if (MH_CreateHook(exec, &Hook_DX12_ExecCmd, (LPVOID*)&Orig_DX12_ExecCmd) == MH_OK) {
                MH_EnableHook(exec);
                WriteLog("DX12 ExecCmd Hook AKTIF"); hooked++;
            }
            if (MH_CreateHook(present, &Hook_DX12_Present, (LPVOID*)&Orig_DX12_Present) == MH_OK) {
                MH_EnableHook(present);
                WriteLog("DX12 Present Hook AKTIF"); hooked++;
            }
        }
    } else {
        WriteLog("d3d12.dll yok, DX12 atlandi");
    }
    
    // OpenGL
    if (GetModuleHandleW(L"opengl32.dll")) {
        void* addr = GetOpenGLAddress();
        if (addr) {
            char b[64]; sprintf_s(b, "OpenGL wglSwapBuffers: 0x%p", addr); WriteLog(b);
            if (MH_CreateHook(addr, &Hook_wglSwapBuffers, (LPVOID*)&Orig_wglSwapBuffers) == MH_OK) {
                MH_EnableHook(addr);
                WriteLog("OpenGL Hook AKTIF"); hooked++;
            }
        }
    } else {
        WriteLog("opengl32.dll yok, OpenGL atlandi");
    }
    
    char summary[128];
    sprintf_s(summary, "Toplam %d hook kuruldu, frame bekleniyor", hooked);
    WriteLog(summary);
    return 0;
}

// ═══════════════════════════════════════════════════════════════════════
// DllMain
// ═══════════════════════════════════════════════════════════════════════
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
    {
        char proc[MAX_PATH] = { 0 };
        GetModuleFileNameA(NULL, proc, MAX_PATH);
        DWORD pid = GetCurrentProcessId();
        WriteLog("DLL v0.6 yuklendi. PID=" + std::to_string(pid) + ", Process=" + proc);
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, HookThread, NULL, 0, NULL);
    }
    break;
    case DLL_PROCESS_DETACH:
        WriteLog("DLL kaldirildi");
        if (g_d3d11StagingTex) g_d3d11StagingTex->Release();
        if (g_d3d11Context) g_d3d11Context->Release();
        if (g_d3d11Device) g_d3d11Device->Release();
        if (g_dx9Surface) g_dx9Surface->Release();
        if (g_d3d12StagingBuffer) g_d3d12StagingBuffer->Release();
        if (g_d3d12CmdList) g_d3d12CmdList->Release();
        if (g_d3d12CmdAlloc) g_d3d12CmdAlloc->Release();
        if (g_d3d12Fence) g_d3d12Fence->Release();
        if (g_d3d12FenceEvent) CloseHandle(g_d3d12FenceEvent);
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
        CleanupSharedMemory();
        break;
    }
    return TRUE;
}
