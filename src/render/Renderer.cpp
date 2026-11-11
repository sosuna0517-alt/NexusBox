// Renderer.cpp — D3D12 clear + game blit + ImGui, presented on the CoreWindow.
#include "Renderer.h"

#include <algorithm>
#include <cstring>
#include <cmath>

#ifdef NEXUSBOX_HAS_IMGUI
#include "imgui.h"
#endif

// Shader bytecode compiled at build time by FXC (see CMakeLists.txt).
#include "BlitVS.h"
#include "BlitPS.h"
#include "ImGuiVS.h"
#include "ImGuiPS.h"

using Microsoft::WRL::ComPtr;

namespace nb
{

static void Debug(const char* msg) { OutputDebugStringA("[NexusBox] "); OutputDebugStringA(msg); OutputDebugStringA("\n"); }

bool Renderer::Initialize(winrt::Windows::UI::Core::CoreWindow const& window)
{
    m_ok = CreateDevice() && CreateSwapchain(window) && CreatePipelines();
    if (!m_ok) Debug("Renderer initialization failed");
    return m_ok;
}

bool Renderer::CreateDevice()
{
    UINT flags = 0;
#ifndef NDEBUG
    ComPtr<ID3D12Debug> dbg;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dbg)))) dbg->EnableDebugLayer();
    flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
    if (FAILED(CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory))))
        if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&m_factory)))) return false;

    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
    { Debug("D3D12CreateDevice failed"); return false; }

    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(m_device->CreateCommandQueue(&qd, IID_PPV_ARGS(&m_queue)))) return false;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_alloc)))) return false;
    if (FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_alloc.Get(), nullptr, IID_PPV_ARGS(&m_cmd)))) return false;
    m_cmd->Close();

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) return false;
    m_fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);

    D3D12_DESCRIPTOR_HEAP_DESC rh{};
    rh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rh.NumDescriptors = kBackBuffers;
    if (FAILED(m_device->CreateDescriptorHeap(&rh, IID_PPV_ARGS(&m_rtvHeap)))) return false;
    m_rtvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC sh{};
    sh.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    sh.NumDescriptors = 2;
    sh.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(m_device->CreateDescriptorHeap(&sh, IID_PPV_ARGS(&m_srvHeap)))) return false;
    m_srvStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool Renderer::CreateSwapchain(winrt::Windows::UI::Core::CoreWindow const& window)
{
    ComPtr<IDXGIFactory5> f5;
    BOOL allowTearing = FALSE;
    if (SUCCEEDED(m_factory.As(&f5)))
        f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    m_tearing = allowTearing == TRUE;

    // Xbox UWP apps render at 1920x1080; the console scales to the TV mode.
    m_width = 1920; m_height = 1080;

    DXGI_SWAP_CHAIN_DESC1 sc{};
    sc.Width = m_width;
    sc.Height = m_height;
    sc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sc.SampleDesc.Count = 1;
    sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sc.BufferCount = kBackBuffers;
    sc.Scaling = DXGI_SCALING_STRETCH;
    sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    sc.Flags = m_tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> sc1;
    HRESULT hr = m_factory->CreateSwapChainForCoreWindow(m_queue.Get(),
        static_cast<IUnknown*>(winrt::get_abi(window)), &sc, nullptr, &sc1);
    if (FAILED(hr)) { Debug("CreateSwapChainForCoreWindow failed"); return false; }
    if (FAILED(sc1.As(&m_swapchain))) return false;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    for (UINT i = 0; i < kBackBuffers; ++i)
    {
        if (FAILED(m_swapchain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i])))) return false;
        m_device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, rtv);
        rtv.ptr += m_rtvStride;
    }
    return true;
}

bool Renderer::CreatePipelines()
{
    // Root signature shared by the blit and UI passes:
    //   [0] 17 root constants (b0): float4x4 projection + uint linear flag
    //   [1] SRV table (t0)
    //   static samplers s0 = point, s1 = linear
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[2]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = 17;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &range;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (int i = 0; i < 2; ++i)
    {
        samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = (UINT)i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        samplers[i].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
    }

    D3D12_ROOT_SIGNATURE_DESC rs{};
    rs.NumParameters = 2; rs.pParameters = params;
    rs.NumStaticSamplers = 2; rs.pStaticSamplers = samplers;
    rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &err)))
    { Debug(err ? (const char*)err->GetBufferPointer() : "root signature serialize failed"); return false; }
    if (FAILED(m_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&m_rootSig))))
        return false;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = m_rootSig.Get();
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pd.DepthStencilState.DepthEnable = FALSE;
    pd.SampleMask = UINT_MAX;
    pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_B8G8R8A8_UNORM;
    pd.SampleDesc.Count = 1;

    // Game blit: no input layout, opaque.
    pd.VS = { g_BlitVS, sizeof(g_BlitVS) };
    pd.PS = { g_BlitPS, sizeof(g_BlitPS) };
    if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_blitPso))))
    { Debug("blit PSO failed"); return false; }

    // ImGui: pos/uv/color vertices, alpha blending, scissor.
    static const D3D12_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    pd.InputLayout = { layout, 3 };
    pd.VS = { g_ImGuiVS, sizeof(g_ImGuiVS) };
    pd.PS = { g_ImGuiPS, sizeof(g_ImGuiPS) };
    auto& bt = pd.BlendState.RenderTarget[0];
    bt.BlendEnable = TRUE;
    bt.SrcBlend = D3D12_BLEND_SRC_ALPHA;   bt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA; bt.BlendOp = D3D12_BLEND_OP_ADD;
    bt.SrcBlendAlpha = D3D12_BLEND_ONE;    bt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA; bt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    if (FAILED(m_device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&m_uiPso))))
    { Debug("ImGui PSO failed"); return false; }
    return true;
}

ComPtr<ID3D12Resource> Renderer::CreateBuffer(D3D12_HEAP_TYPE heapType, UINT64 size, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = heapType;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    d.Width = size; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> r;
    m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&r));
    return r;
}

ComPtr<ID3D12Resource> Renderer::CreateTexture(UINT w, UINT h, DXGI_FORMAT fmt)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d{};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w; d.Height = h; d.DepthOrArraySize = 1; d.MipLevels = 1;
    d.Format = fmt; d.SampleDesc.Count = 1;
    ComPtr<ID3D12Resource> r;
    m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &d,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&r));
    return r;
}

void Renderer::Transition(ID3D12Resource* r, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
{
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = r;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = from;
    b.Transition.StateAfter = to;
    m_cmd->ResourceBarrier(1, &b);
}

void Renderer::WaitForGpu()
{
    if (!m_queue || !m_fence) return;
    const UINT64 v = ++m_fenceValue;
    m_queue->Signal(m_fence.Get(), v);
    if (m_fence->GetCompletedValue() < v)
    {
        m_fence->SetEventOnCompletion(v, m_fenceEvent);
        WaitForSingleObject(m_fenceEvent, 2000);
    }
}

// ------------------------------------------------------------------ video

void Renderer::SubmitVideo(const uint32_t* xrgb, uint32_t w, uint32_t h, double aspect)
{
    if (!xrgb || !w || !h) return;
    std::lock_guard lock(m_videoMutex);
    m_videoPixels.assign(xrgb, xrgb + (size_t)w * h);
    m_videoW = w; m_videoH = h;
    m_videoAspect = aspect > 0.0 ? aspect : (double)w / h;
    m_videoDirty = true;
}

void Renderer::ClearVideo()
{
    std::lock_guard lock(m_videoMutex);
    m_hasVideo = false;
    m_videoDirty = false;
}

void Renderer::UploadVideoIfDirty()
{
    std::lock_guard lock(m_videoMutex);
    if (!m_videoDirty) return;
    m_videoDirty = false;

    if (m_videoW != m_texW || m_videoH != m_texH || !m_videoTex)
    {
        // GPU is idle here (one frame in flight), so resources can be replaced.
        m_videoTex = CreateTexture(m_videoW, m_videoH, DXGI_FORMAT_B8G8R8A8_UNORM);
        const UINT64 rowPitch = (m_videoW * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(UINT64)(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
        m_videoUpload = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, rowPitch * m_videoH, D3D12_RESOURCE_STATE_GENERIC_READ);
        if (!m_videoTex || !m_videoUpload) { m_texW = m_texH = 0; return; }
        m_texW = m_videoW; m_texH = m_videoH;

        D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
        sv.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        sv.Texture2D.MipLevels = 1;
        D3D12_CPU_DESCRIPTOR_HANDLE h = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        h.ptr += m_srvStride;   // slot 1
        m_device->CreateShaderResourceView(m_videoTex.Get(), &sv, h);
    }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    const D3D12_RESOURCE_DESC td = m_videoTex->GetDesc();
    m_device->GetCopyableFootprints(&td, 0, 1, 0, &fp, nullptr, nullptr, nullptr);

    uint8_t* mapped = nullptr;
    D3D12_RANGE noRead{ 0, 0 };
    if (FAILED(m_videoUpload->Map(0, &noRead, (void**)&mapped))) return;
    for (uint32_t y = 0; y < m_videoH; ++y)
        std::memcpy(mapped + fp.Footprint.RowPitch * y, m_videoPixels.data() + (size_t)y * m_videoW, (size_t)m_videoW * 4);
    m_videoUpload->Unmap(0, nullptr);

    Transition(m_videoTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = m_videoTex.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = m_videoUpload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = fp;
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(m_videoTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_hasVideo = true;
}

void Renderer::DrawVideo()
{
    if (!m_hasVideo || !m_videoTex) return;

    const float W = (float)m_width, H = (float)m_height;
    const float aspect = (float)m_videoAspect;
    float dh = H, dw = H * aspect;
    if (dw > W) { dw = W; dh = W / aspect; }
    if (m_scale == ScaleMode::Integer && m_texH > 0)
    {
        const float mult = std::max(1.0f, std::floor(dh / (float)m_texH));
        dh = mult * (float)m_texH;
        dw = dh * aspect;
        if (dw > W) { dw = W; }
    }
    D3D12_VIEWPORT vp{ (W - dw) * 0.5f, (H - dh) * 0.5f, dw, dh, 0.0f, 1.0f };
    D3D12_RECT sc{ 0, 0, (LONG)m_width, (LONG)m_height };

    m_cmd->SetPipelineState(m_blitPso.Get());
    uint32_t consts[17]{};
    consts[16] = m_scale == ScaleMode::Bilinear ? 1u : 0u;
    m_cmd->SetGraphicsRoot32BitConstants(0, 17, consts, 0);
    D3D12_GPU_DESCRIPTOR_HANDLE srv = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
    srv.ptr += m_srvStride;
    m_cmd->SetGraphicsRootDescriptorTable(1, srv);
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->RSSetScissorRects(1, &sc);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_cmd->DrawInstanced(3, 1, 0, 0);
}

// ------------------------------------------------------------------ ImGui

bool Renderer::InitImGui()
{
#ifdef NEXUSBOX_HAS_IMGUI
    if (!m_ok) return false;
    ImGuiIO& io = ImGui::GetIO();
    unsigned char* pixels = nullptr; int w = 0, h = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);

    m_fontTex = CreateTexture((UINT)w, (UINT)h, DXGI_FORMAT_R8G8B8A8_UNORM);
    const UINT64 rowPitch = ((UINT64)w * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(UINT64)(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    auto upload = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, rowPitch * h, D3D12_RESOURCE_STATE_GENERIC_READ);
    if (!m_fontTex || !upload) return false;

    uint8_t* mapped = nullptr;
    upload->Map(0, nullptr, (void**)&mapped);
    for (int y = 0; y < h; ++y) std::memcpy(mapped + rowPitch * y, pixels + (size_t)y * w * 4, (size_t)w * 4);
    upload->Unmap(0, nullptr);

    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);
    Transition(m_fontTex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = m_fontTex.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM, (UINT)w, (UINT)h, 1, (UINT)rowPitch };
    m_cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    Transition(m_fontTex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    m_cmd->Close();
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);
    WaitForGpu();

    D3D12_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    sv.Texture2D.MipLevels = 1;
    m_device->CreateShaderResourceView(m_fontTex.Get(), &sv, m_srvHeap->GetCPUDescriptorHandleForHeapStart());

    io.Fonts->SetTexID((ImTextureID)m_srvHeap->GetGPUDescriptorHandleForHeapStart().ptr);
    io.DisplaySize = ImVec2((float)m_width, (float)m_height);
    io.BackendRendererName = "nexusbox_dx12";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    return true;
#else
    return false;
#endif
}

void Renderer::DrawImGui(ImDrawData* dd)
{
#ifdef NEXUSBOX_HAS_IMGUI
    if (!dd || dd->TotalVtxCount == 0 || !m_fontTex) return;

    const UINT64 vbNeeded = (UINT64)dd->TotalVtxCount * sizeof(ImDrawVert);
    const UINT64 ibNeeded = (UINT64)dd->TotalIdxCount * sizeof(ImDrawIdx);
    if (vbNeeded > m_uiVBSize) { m_uiVBSize = vbNeeded + 64 * 1024; m_uiVB = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, m_uiVBSize, D3D12_RESOURCE_STATE_GENERIC_READ); }
    if (ibNeeded > m_uiIBSize) { m_uiIBSize = ibNeeded + 32 * 1024; m_uiIB = CreateBuffer(D3D12_HEAP_TYPE_UPLOAD, m_uiIBSize, D3D12_RESOURCE_STATE_GENERIC_READ); }
    if (!m_uiVB || !m_uiIB) return;

    ImDrawVert* vtx = nullptr; ImDrawIdx* idx = nullptr;
    D3D12_RANGE noRead{ 0, 0 };
    m_uiVB->Map(0, &noRead, (void**)&vtx);
    m_uiIB->Map(0, &noRead, (void**)&idx);
    for (int n = 0; n < dd->CmdListsCount; ++n)
    {
        const ImDrawList* cl = dd->CmdLists[n];
        std::memcpy(vtx, cl->VtxBuffer.Data, cl->VtxBuffer.Size * sizeof(ImDrawVert));
        std::memcpy(idx, cl->IdxBuffer.Data, cl->IdxBuffer.Size * sizeof(ImDrawIdx));
        vtx += cl->VtxBuffer.Size; idx += cl->IdxBuffer.Size;
    }
    m_uiVB->Unmap(0, nullptr);
    m_uiIB->Unmap(0, nullptr);

    // Orthographic projection (column-major for mul(proj, v) in HLSL).
    const float L = dd->DisplayPos.x, R = dd->DisplayPos.x + dd->DisplaySize.x;
    const float T = dd->DisplayPos.y, B = dd->DisplayPos.y + dd->DisplaySize.y;
    float consts[17] = {
        2.0f / (R - L), 0.0f, 0.0f, 0.0f,
        0.0f, 2.0f / (T - B), 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        (R + L) / (L - R), (T + B) / (B - T), 0.5f, 1.0f,
        0.0f };

    D3D12_VIEWPORT vp{ 0, 0, (float)m_width, (float)m_height, 0.0f, 1.0f };
    D3D12_VERTEX_BUFFER_VIEW vbv{ m_uiVB->GetGPUVirtualAddress(), (UINT)m_uiVBSize, sizeof(ImDrawVert) };
    D3D12_INDEX_BUFFER_VIEW  ibv{ m_uiIB->GetGPUVirtualAddress(), (UINT)m_uiIBSize,
                                  sizeof(ImDrawIdx) == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT };
    m_cmd->SetPipelineState(m_uiPso.Get());
    m_cmd->SetGraphicsRoot32BitConstants(0, 17, consts, 0);
    m_cmd->RSSetViewports(1, &vp);
    m_cmd->IASetVertexBuffers(0, 1, &vbv);
    m_cmd->IASetIndexBuffer(&ibv);
    m_cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    int vtxOffset = 0, idxOffset = 0;
    const ImVec2 clipOff = dd->DisplayPos;
    for (int n = 0; n < dd->CmdListsCount; ++n)
    {
        const ImDrawList* cl = dd->CmdLists[n];
        for (int c = 0; c < cl->CmdBuffer.Size; ++c)
        {
            const ImDrawCmd& cmd = cl->CmdBuffer[c];
            if (cmd.UserCallback) { if (cmd.UserCallback != ImDrawCallback_ResetRenderState) cmd.UserCallback(cl, &cmd); continue; }
            const LONG x0 = (LONG)std::max(0.0f, cmd.ClipRect.x - clipOff.x);
            const LONG y0 = (LONG)std::max(0.0f, cmd.ClipRect.y - clipOff.y);
            const LONG x1 = (LONG)std::min((float)m_width,  cmd.ClipRect.z - clipOff.x);
            const LONG y1 = (LONG)std::min((float)m_height, cmd.ClipRect.w - clipOff.y);
            if (x1 <= x0 || y1 <= y0) continue;
            D3D12_RECT r{ x0, y0, x1, y1 };
            m_cmd->RSSetScissorRects(1, &r);
            D3D12_GPU_DESCRIPTOR_HANDLE tex{ (UINT64)cmd.GetTexID() };
            m_cmd->SetGraphicsRootDescriptorTable(1, tex);
            m_cmd->DrawIndexedInstanced(cmd.ElemCount, 1, cmd.IdxOffset + idxOffset, cmd.VtxOffset + vtxOffset, 0);
        }
        vtxOffset += cl->VtxBuffer.Size;
        idxOffset += cl->IdxBuffer.Size;
    }
#else
    (void)dd;
#endif
}

// ------------------------------------------------------------------ frame

void Renderer::Render(ImDrawData* ui)
{
    if (!m_ok) return;
    const UINT bb = m_swapchain->GetCurrentBackBufferIndex();

    m_alloc->Reset();
    m_cmd->Reset(m_alloc.Get(), nullptr);

    UploadVideoIfDirty();

    Transition(m_backBuffers[bb].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += (SIZE_T)bb * m_rtvStride;
    const float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    m_cmd->ClearRenderTargetView(rtv, clear, 0, nullptr);
    m_cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    m_cmd->SetDescriptorHeaps(1, heaps);
    m_cmd->SetGraphicsRootSignature(m_rootSig.Get());

    DrawVideo();
    DrawImGui(ui);

    Transition(m_backBuffers[bb].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    m_cmd->Close();
    ID3D12CommandList* lists[] = { m_cmd.Get() };
    m_queue->ExecuteCommandLists(1, lists);

    const UINT flags = (!m_vsync && m_tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = m_swapchain->Present(m_vsync ? 1 : 0, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        Debug("GPU device removed");
        m_ok = false;
        return;
    }
    WaitForGpu();
}

void Renderer::Shutdown()
{
    WaitForGpu();
    if (m_fenceEvent) { CloseHandle(m_fenceEvent); m_fenceEvent = nullptr; }
    m_ok = false;
}

} // namespace nb
