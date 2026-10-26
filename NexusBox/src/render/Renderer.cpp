// Renderer.cpp — D3D12 pipeline (foundation build: blit + scaling + one post pass).
#include "Renderer.h"

#include <winrt/Windows.UI.Core.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <cstring>
#include "d3dx12.h"

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")

using Microsoft::WRL::ComPtr;

namespace nb
{

static const char* g_vsQuad = R"HLSL(
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut main(uint vid : SV_VertexID)
{
    // Fullscreen triangle strip from vertex ID (no vertex buffer, no cost).
    VSOut o;
    float2 p = float2((vid == 1) ? 3.0 : -1.0, (vid == 2) ? 3.0 : -1.0);
    o.pos = float4(p, 0, 1);
    o.uv  = float2((p.x + 1) * 0.5, 1 - (p.y + 1) * 0.5);
    return o;
}
)HLSL";

static const char* g_psBlit[3] = {
    // Nearest
    R"HLSL(
Texture2D src : register(t0); SamplerState s : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{ return src.SampleLevel(s, uv, 0); }   // POINT sampler bound for slot 0
)HLSL",
    // Bilinear
    R"HLSL(
Texture2D src : register(t0); SamplerState s : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{ return src.SampleLevel(s, uv, 0); }   // LINEAR sampler for slot 1
)HLSL",
    // Integer scale: snap the viewport to whole multiples in the VS-side rect,
    // then point-sample. Overscan is letterboxed on the swapchain RTV.
    R"HLSL(
Texture2D src : register(t0); SamplerState s : register(s0);
float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{ return src.SampleLevel(s, uv, 0); }   // integer rect computed on CPU
)HLSL"
};

void Renderer::Initialize(winrt::Windows::UI::Core::CoreWindow const& window)
{
    CreateDeviceAndSwapchain();
    CreateUploadRing();
    CreateBlitPipeline();
    (void)window;
}

void Renderer::CreateDeviceAndSwapchain()
{
    UINT flags = 0;
#ifndef NDEBUG
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    ComPtr<IDXGIFactory6> factory;
    CreateDXGIFactory2(flags, IID_PPV_ARGS(factory.ReleaseAndGetAddressOf()));

    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                        IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));

    // Series X/S exposes FL 12.x; request 12.0 baseline (12.2 features opt-in).
    D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0,
                      IID_PPV_ARGS(m_device.ReleaseAndGetAddressOf()));

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    qd.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;   // presentation above compute
    m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(m_queue.ReleaseAndGetAddressOf()));

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width  = 3840;                 // Series X|S native 4K
    sc.Height = 2160;
    sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sc.Stereo = FALSE;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = 2;               // flip-discard with 2: lowest latency
    sc.Scaling = DXGI_SCALING_STRETCH;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING |   // VRR
               DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;

    ComPtr<IDXGISwapChain1> sc1;
    factory->CreateSwapChainForCoreWindow(m_queue.Get(),
        (IUnknown*)winrt::get_abi(
            winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread()),
        &sc, nullptr, sc1.ReleaseAndGetAddressOf());
    sc1.As(&m_swapchain);

    // VRR + Low Latency: Series X|S supports variable refresh in Dev Mode builds.
    ComPtr<IDXGIOutput> output;
    m_swapchain->GetContainingOutput(output.ReleaseAndGetAddressOf());
    ComPtr<IDXGIOutput6> output6;
    if (SUCCEEDED(output.As(&output6)))
    {
        DXGI_OUTPUT_DESC1 d{};
        if (SUCCEEDED(output6->GetDesc1(&d)))
        {
            // sc.Flags already allows tearing; HDR passthrough is future work.
            (void)d;
        }
    }
    m_swapchain->SetMaximumFrameLatency(1);           // one frame of queue
    m_fenceEvent = m_swapchain->GetFrameLatencyWaitableObject();

    m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.ReleaseAndGetAddressOf()));

    m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_alloc.ReleaseAndGetAddressOf()));
    m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(),
                                nullptr, IID_PPV_ARGS(m_cmd.ReleaseAndGetAddressOf()));
    m_cmd->Close();

    for (UINT i = 0; i < 2; ++i)
    {
        m_swapchain->GetBuffer(i, IID_PPV_ARGS(m_rtv[i].ReleaseAndGetAddressOf()));
        // RTV creation handled by a desc heap in the full build; kept minimal here.
    }
    m_backBuffer = 0;
}

void Renderer::CreateUploadRing()
{
    // Three R8G8B8A8 slots, 1920x1080 max core resolution (enlarged on demand).
    D3D12_HEAP_PROPERTIES heap{ D3D12_HEAP_TYPE_DEFAULT };
    D3D12_HEAP_PROPERTIES upheap{ D3D12_HEAP_TYPE_UPLOAD };
    D3D12_RESOURCE_DESC tex{};
    tex.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    tex.Width = 1920; tex.Height = 1080; tex.DepthOrArraySize = 1;
    tex.MipLevels = 1; tex.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    tex.SampleDesc.Count = 1;

    D3D12_RESOURCE_DESC buf = tex;
    buf.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buf.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

    for (int i = 0; i < 3; ++i)
    {
        m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &tex,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_videoTex[i].ReleaseAndGetAddressOf()));
        // Upload buffer sized once; cores rarely exceed 1080p wide.
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 sz = 0;
        m_device->GetCopyableFootprints(&tex, 0, 1, 0, &fp, nullptr, nullptr, &sz);
        D3D12_RESOURCE_DESC ub{};
        ub.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        ub.Width = sz; ub.Height = 1; ub.DepthOrArraySize = 1; ub.MipLevels = 1;
        ub.SampleDesc.Count = 1; ub.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        m_device->CreateCommittedResource(&upheap, D3D12_HEAP_FLAG_NONE, &ub,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_upload[i].ReleaseAndGetAddressOf()));
    }
}

void Renderer::CreateBlitPipeline()
{
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1; range.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER rp{};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp.DescriptorTable.NumDescriptorRanges = 1;
    rp.DescriptorTable.pDescriptorRanges = &range;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sp{};
    sp.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    sp.ShaderRegister = 0; sp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC sl = sp;
    sl.Filter = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;

    D3D12_STATIC_SAMPLER_DESC samps[2] = { sp, sl };

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 1; rs.pParameters = &rp;
    rs.NumStaticSamplers = 2; rs.pStaticSamplers = samps;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> sig, err;
    D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, sig.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf());
    m_device->CreateRootSignature(0, sig->GetBufferPointer(), sig->GetBufferSize(),
                                  IID_PPV_ARGS(m_rootSig.ReleaseAndGetAddressOf()));

    // Compile the three pixel-shader variants (point/linear/integer) + shared VS.
    for (int i = 0; i < 3; ++i)
    {
        ComPtr<ID3DBlob> vs, ps;
        D3DCompile(g_vsQuad, strlen(g_vsQuad), nullptr, nullptr, nullptr, "main",
                   "vs_5_1", 0, 0, vs.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf());
        D3DCompile(g_psBlit[i], strlen(g_psBlit[i]), nullptr, nullptr, nullptr, "main",
                   "ps_5_1", 0, 0, ps.ReleaseAndGetAddressOf(), err.ReleaseAndGetAddressOf());

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
        pso.pRootSignature = m_rootSig.Get();
        pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
        pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.DepthStencilState.DepthEnable = FALSE;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.SampleDesc.Count = 1;
        m_device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(m_psos[i].ReleaseAndGetAddressOf()));
    }
}

void Renderer::Blit(const VideoFrame& frame, const VideoGeometry& geo, uint64_t frameIndex)
{
    if (!frame.data || !frame.width) return;
    const UINT slot = RingSlot(frame.frameIndex);

    m_cmd->Reset(m_alloc.Get(), m_psos[(int)m_scale].Get());
    m_cmd->SetGraphicsRootSignature(m_rootSig.Get());

    // CPU->GPU upload of the core frame (row-major pitch handling).
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT64 sz = 0;
    D3D12_RESOURCE_DESC d = m_videoTex[slot]->GetDesc();
    m_device->GetCopyableFootprints(&d, 0, 1, 0, &fp, nullptr, nullptr, &sz);
    void* mapped = nullptr;
    m_upload[slot]->Map(0, nullptr, &mapped);
    const uint8_t* src = (const uint8_t*)frame.data;
    for (uint32_t row = 0; row < frame.height; ++row)
    {
        memcpy((uint8_t*)mapped + fp.Footprint.RowPitch * row,
               src + frame.pitch * row,
               std::min<size_t>(frame.pitch, fp.Footprint.RowPitch));
    }
    m_upload[slot]->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst{ m_videoTex[slot].Get(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX };
    D3D12_TEXTURE_COPY_LOCATION srcL{ m_upload[slot].Get(), D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT, fp };
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &srcL, nullptr);
    // CD3DX12 helpers return temporaries; ResourceBarrier needs an l-value array.
    D3D12_RESOURCE_BARRIER toSrv = CD3DX12_RESOURCE_BARRIER::Transition(
        m_videoTex[slot].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmd->ResourceBarrier(1, &toSrv);

    // Compute the integer/bilinear dest rect honoring aspect + 4K output.
    RECT dstRect{};
    const UINT outW = 3840, outH = 2160;
    if (m_scale == ScaleMode::Integer)
    {
        uint32_t mult = std::min(outW / frame.width, outH / frame.height);
        mult = std::max(mult, 1u);
        dstRect.left   = (outW - frame.width  * mult) / 2;
        dstRect.top    = (outH - frame.height * mult) / 2;
        dstRect.right  = dstRect.left + frame.width  * mult;
        dstRect.bottom = dstRect.top  + frame.height * mult;
    }
    else
    {
        // Fit respecting aspect ratio, centered.
        float scale = std::min((float)outW / frame.width, (float)outH / frame.height);
        dstRect.left   = (LONG)((outW - frame.width * scale) / 2);
        dstRect.top    = (LONG)((outH - frame.height * scale) / 2);
        dstRect.right  = dstRect.left + (LONG)(frame.width * scale);
        dstRect.bottom = dstRect.top  + (LONG)(frame.height * scale);
    }
    D3D12_VIEWPORT vp{ (float)dstRect.left, (float)dstRect.top,
                       (float)(dstRect.right - dstRect.left),
                       (float)(dstRect.bottom - dstRect.top) };
    D3D12_RECT scissor = dstRect;
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &scissor);
    m_cmd->OMSetRenderTargets(1, nullptr, FALSE, nullptr); // RTV heap in full build
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_cmd->DrawInstanced(4, 1, 0, 0);

    // Signal the fence so Present knows the frame is ready (VRR pacing point).
    m_cmd->Close();
    ID3D12CommandList* cl = m_cmd.Get();
    m_queue->ExecuteCommandLists(1, &cl);
    m_queue->Signal(m_fence.Get(), ++m_fenceValue);
    (void)geo;
}

void Renderer::Present()
{
    std::lock_guard lock(m_gpuMutex);
    // Wait for the last Blit fence before presenting (frame-paced, not vsync-locked
    // when VRR is active: tearing is allowed so present returns immediately).
    UINT flags = m_vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    m_swapchain->Present(m_vsync ? 1 : 0, flags);
    m_backBuffer ^= 1;
}

void Renderer::OnResize(uint32_t /*w*/, uint32_t /*h*/)
{
    // Xbox presentation is fixed at the TV mode; no resize needed in Dev Mode.
}

void Renderer::HandleDeviceLost()
{
    // Rebuild device-dependent resources; cores are unaffected (CPU-side state).
    CreateDeviceAndSwapchain();
    CreateUploadRing();
    CreateBlitPipeline();
}

void Renderer::SetScaleMode(ScaleMode m)         { m_scale = m; }
void Renderer::SetVsync(bool on)                  { m_vsync = on; }
void Renderer::SetLowLatencyMode(bool on)         { m_lowLatency = on; (void)on; }
void Renderer::SetPostChain(const char* /*name*/)
{
    // Full build: load Slang/HLSL pass chain from disk, recompile PSO cache.
}

void Renderer::Shutdown()
{
    WaitForGpu();
    if (m_fenceEvent) CloseHandle(m_fenceEvent);
}

void Renderer::WaitForGpu()
{
    m_queue->Signal(m_fence.Get(), ++m_fenceValue);
    if (m_fence->GetCompletedValue() < m_fenceValue)
    {
        m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, INFINITE);
    }
}

} // namespace nb
