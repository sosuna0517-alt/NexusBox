#pragma once
// Renderer.h — D3D12 presentation for Xbox (UWP CoreWindow swapchain).
// One frame in flight: each Render() records clear -> game video -> ImGui UI,
// presents, then waits for the GPU. Simple, robust, and latency stays low
// because the swapchain only has two buffers.
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <cstdint>
#include <mutex>
#include <vector>

#include <winrt/Windows.UI.Core.h>

struct ImDrawData;

namespace nb {

enum class ScaleMode { Nearest, Bilinear, Integer };

class Renderer
{
public:
    bool Initialize(winrt::Windows::UI::Core::CoreWindow const& window);
    void Shutdown();

    // Emulation thread: hands over the latest XRGB8888 frame (copied).
    void SubmitVideo(const uint32_t* xrgb, uint32_t width, uint32_t height, double aspect);
    void ClearVideo();

    // UI thread: draw everything and present.
    void Render(ImDrawData* ui);

    // ImGui font atlas upload (call after ImGui::CreateContext()).
    bool InitImGui();

    void SetScaleMode(ScaleMode m) { m_scale = m; }
    ScaleMode GetScaleMode() const { return m_scale; }
    void SetVsync(bool on)         { m_vsync = on; }
    bool GetVsync() const          { return m_vsync; }

    uint32_t Width()  const { return m_width; }
    uint32_t Height() const { return m_height; }
    bool     Ok()     const { return m_ok; }

private:
    bool CreateDevice();
    bool CreateSwapchain(winrt::Windows::UI::Core::CoreWindow const& window);
    bool CreatePipelines();
    void WaitForGpu();
    void UploadVideoIfDirty();
    void DrawVideo();
    void DrawImGui(ImDrawData* dd);
    void Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateBuffer(D3D12_HEAP_TYPE heap, UINT64 size, D3D12_RESOURCE_STATES state);
    Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture(UINT w, UINT h, DXGI_FORMAT fmt);

    template <class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    static constexpr UINT kBackBuffers = 2;

    ComPtr<IDXGIFactory4>             m_factory;
    ComPtr<ID3D12Device>              m_device;
    ComPtr<ID3D12CommandQueue>        m_queue;
    ComPtr<IDXGISwapChain3>           m_swapchain;
    ComPtr<ID3D12CommandAllocator>    m_alloc;
    ComPtr<ID3D12GraphicsCommandList> m_cmd;
    ComPtr<ID3D12DescriptorHeap>      m_rtvHeap;
    ComPtr<ID3D12DescriptorHeap>      m_srvHeap;   // [0] font, [1] video
    ComPtr<ID3D12Resource>            m_backBuffers[kBackBuffers];
    UINT                              m_rtvStride = 0, m_srvStride = 0;

    ComPtr<ID3D12RootSignature>       m_rootSig;
    ComPtr<ID3D12PipelineState>       m_blitPso, m_uiPso;

    ComPtr<ID3D12Fence>               m_fence;
    HANDLE                            m_fenceEvent = nullptr;
    UINT64                            m_fenceValue = 0;

    // Video texture + staging
    ComPtr<ID3D12Resource>            m_videoTex, m_videoUpload;
    uint32_t                          m_texW = 0, m_texH = 0;
    std::mutex                        m_videoMutex;
    std::vector<uint32_t>             m_videoPixels;
    uint32_t                          m_videoW = 0, m_videoH = 0;
    double                            m_videoAspect = 4.0 / 3.0;
    bool                              m_videoDirty = false, m_hasVideo = false;

    // ImGui resources
    ComPtr<ID3D12Resource>            m_fontTex;
    ComPtr<ID3D12Resource>            m_uiVB, m_uiIB;
    UINT64                            m_uiVBSize = 0, m_uiIBSize = 0;

    uint32_t  m_width = 1920, m_height = 1080;
    ScaleMode m_scale = ScaleMode::Integer;
    bool      m_vsync = true, m_tearing = false, m_ok = false;
};

} // namespace nb
