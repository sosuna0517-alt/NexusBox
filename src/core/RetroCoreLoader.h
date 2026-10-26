#pragma once
// RetroCoreLoader.h — libretro-compatible loader.
// Binds the libretro frontend ABI (retro_init/run/unload...), owns the sandboxed
// arenas (video buffer, audio ring, save RAM, state buffer), and exposes the
// frame/poll callbacks consumed by CoreEngine. No OpenGL; video comes out as a
// pixel buffer for the D3D12 blit path (set_pixel_format ARGB8888/RGB565).
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

namespace nb {

// Minimal libretro ABI surface (subset used by the engine).
using retro_proc_t = void*;

struct AvInfo
{
    double fps = 60.0;
    double aspect = 4.0 / 3.0;
    unsigned baseW = 320, baseH = 240;
};

struct MemoryMap
{
    uint8_t* sram   = nullptr; size_t sramSize   = 0;   // retro_memory_save_ram
    uint8_t* vram   = nullptr; size_t vramSize   = 0;
    uint8_t* rtc    = nullptr; size_t rtcSize    = 0;
};

struct InputSnapshot
{
    uint16_t buttons[4]{};
    float    analog[4][4]{}; // lx, ly, rx, triggers
};

class RetroCoreLoader
{
public:
    bool Load(const wchar_t* corePath);
    bool LoadRom(const wchar_t* romPath);
    void Unload();

    bool Loaded() const { return m_module != nullptr; }

    void RunFrame();
    void Reset();

    // Accessors renamed (Get*) — a member function named the same as the type
    // it returns shadows that type inside the class body (C3646/C4430 cascade).
    AvInfo    GetAvInfo() const { return m_av; }
    double    AudioRate() const { return m_audioRate; }
    uint32_t  AudioChannels() const { return 2; }
    MemoryMap GetMemoryMap() const { return m_mem; }

    // Frame view valid until the next RunFrame().
    struct FrameView { const void* data; uint32_t pitch, width, height, bpp; };
    FrameView   VideoFrame() const;

    void SetInput(const InputSnapshot& snap);
    void SetAudioSink(std::function<size_t(const int16_t*, size_t)> sink);

    // Serialize/Deserialize the full core state (retro_serialize).
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& blob);
    size_t SerializeSize() const;

private:
    bool BindSymbols();
    static bool Environment(unsigned cmd, void* data); // retro_environment_t

    HMODULE        m_module = nullptr;
    retro_proc_t   m_api[32]{};             // bound libretro entry points
    std::wstring   m_romPath;
    std::vector<uint8_t> m_romData;
    AvInfo         m_av;
    double         m_audioRate = 48000.0;
    MemoryMap      m_mem;
    uint32_t       m_videoW = 0, m_videoH = 0, m_videoPitch = 0, m_videoBpp = 32;
    std::vector<uint8_t> m_videoBuffer;     // owned copy of the last frame
    std::vector<uint8_t> m_stateBuffer;
    InputSnapshot  m_input{};
    std::function<size_t(const int16_t*, size_t)> m_audioSink;
    // static: Environment() is a plain retro_environment_t callback.
    static bool    m_supportNoGame;
};

} // namespace nb
