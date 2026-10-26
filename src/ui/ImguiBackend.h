#pragma once
// ImguiBackend.h — thin bridge so Dashboard/Renderer stay decoupled from ImGui.
// Renderer implements these three: create backend (Win32 + DX12), per-frame
// render of the draw data into the game's command queue, and texture registry
// for box art / OSD icons.
#include <string>

struct ImDrawData;

namespace nb {

class IImGuiHost
{
public:
    virtual ~IImGuiHost() = default;
    virtual void InitImGuiBackend() = 0;                       // ImGui_ImplDX12_Init etc.
    virtual void RenderImGui(ImDrawData* data) = 0;            // queue render, pre-present
    virtual void* TextureId(const std::string& key) = 0;       // cached SRV handle
};

} // namespace nb
