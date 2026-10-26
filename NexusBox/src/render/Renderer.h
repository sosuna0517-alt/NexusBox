#pragma once
// Renderer.h — D3D12 presentation pipeline.
// - Flip-discard swapchain, 4K/120Hz, VRR (DXGI_FEATURE_PRESENT_ALLOW_TEARING),
//   DXGI Low Latency Mode via D3D12 with variable refresh.
// - Core video is uploaded as a R8G8B8A8_UNORM (or 16BPPP for hi-color cores)
//   texture ring; blit queue scales: Nearest / Bilinear / Integer.
// - Post-processing chain: per-pass root signature + PSO cache for Slang/HLSL
//   (CRT-Royale, scanlines). Passes are rebuilt only when settings change.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <memory>
#include <array>
#include <mutex>

#include <winrt/Windows.UI.Core.h>

namespace nb {

struct VideoGeometry
{
    uint32_t srcW = 0, srcH = 0;
    double   aspect = 4.0 / 3.0;     // from retro_get_system_av_info
    float    rotation = 0.0f;
};

struct VideoFrame
{
    const void*  data = nullptr;      // core-owned, valid until next retro_run
    uint32_t     pitch = 0;           // bytes
    uint32_t     width = 0, height = 0;
    uint32_t     bitsPerPixel = 32;
    uint64_t     frameIndex = 0;
};

enum class ScaleMode { Nearest, Bilinear, Integer };

class Renderer
{
public:
    void Initialize(winrt::Windows::UI::Core::CoreWindow const& window);
    void Shutdown();

    // Called from the emulation thread: queues an upload + scale + present-ready
    // fence signal. Never blocks; uses a 3-slot texture ring.
    void Blit(const VideoFrame& frame, const VideoGeometry& geo, uint64_t frameIndex);

    // Called from the UI thread.
    void Present();
    void OnResize(uint32_t w, uint32_t h);
    void HandleDeviceLost();

    void SetScaleMode(ScaleMode m);
    void SetVsync(bool on);                    // off => tearing allowed (VRR)
    void SetLowLatencyMode(bool on);           // D3D12 low-latency present
    void SetPostChain(const char* shaderName); // "scanlines" | "crt-royale" | nullptr

private:
    void CreateDeviceAndSwapchain();
    void CreateUploadRing();
    void CreateBlitPipeline();
    void WaitForGpu();
    uint32_t RingSlot(uint64_t i) const { return (uint32_t)(i % 3); }

    Microsoft::WRL::ComPtr<ID3D12Device>        m_device;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue>  m_queue;
    Microsoft::WRL::ComPtr<IDXGISwapChain4>     m_swapchain;
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator>   m_alloc;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_cmd;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSig;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> m_psos[3]; // per ScaleMode
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_rtv[2];
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_videoTex[3]; // 3-slot ring
    Microsoft::WRL::ComPtr<ID3D12Resource>      m_upload[3];

    HANDLE m_fenceEvent = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;
    UINT     m_backBuffer = 0;

    ScaleMode m_scale = ScaleMode::Integer;
    bool      m_vsync = true;
    bool      m_lowLatency = true;
    std::mutex m_gpuMutex;   // serializes Blit vs Present against the fence
};

} // namespace nb
