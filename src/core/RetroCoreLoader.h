#pragma once
// RetroCoreLoader.h — libretro frontend for a single loaded core.
// Implements the libretro callback ABI (environment, video, audio, input) using
// the official libretro.h. Video is converted to XRGB8888 (BGRA8 in memory) on
// the CPU so the D3D12 path only has to deal with one texture format.
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

#include "libretro.h"

namespace nb {

struct AvInfo
{
    double fps = 60.0;
    double sampleRate = 48000.0;
    double aspect = 4.0 / 3.0;
    unsigned baseW = 320, baseH = 240;
};

struct MemoryMap
{
    uint8_t* sram = nullptr; size_t sramSize = 0;   // RETRO_MEMORY_SAVE_RAM
};

// Per-port pad state in the frontend's terms; mapped to libretro ids at query time.
struct InputSnapshot
{
    uint16_t buttons[4]{};        // PadButton bit mask (see InputManager.h)
    float    lx[4]{}, ly[4]{}, rx[4]{}, ry[4]{};
    float    lt[4]{}, rt[4]{};
};

class RetroCoreLoader
{
public:
    // corePath: package-relative path, e.g. L"cores\\snes9x_libretro.dll".
    bool Load(const wchar_t* corePath, std::wstring& error);
    bool LoadRom(const wchar_t* romPath, std::wstring& error);
    void Unload();

    bool Loaded() const   { return m_module != nullptr; }
    bool GameLoaded() const { return m_gameLoaded; }

    void RunFrame();
    void Reset();

    AvInfo    GetAvInfo() const { return m_av; }
    double    AudioRate() const { return m_av.sampleRate; }
    uint32_t  AudioChannels() const { return 2; }
    MemoryMap GetMemoryMap() const;

    // Latest converted frame (XRGB8888, tightly packed: pitch = width * 4).
    struct FrameView { const void* data; uint32_t pitch, width, height, bpp; };
    FrameView VideoFrame() const;
    bool      ConsumeNewFrame() { bool n = m_newFrame; m_newFrame = false; return n; }

    void SetInput(const InputSnapshot& snap) { m_input = snap; }
    void SetAudioSink(std::function<void(const int16_t*, size_t frames)> sink) { m_audioSink = std::move(sink); }

    // Directories handed to the core (UTF-8, must outlive the core).
    void SetDirectories(const std::wstring& system, const std::wstring& saves);

    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& blob);
    size_t SerializeSize() const;

    std::string LibraryName() const { return m_libName; }

private:
    template <typename T> T Sym(const char* name) const
    { return reinterpret_cast<T>(GetProcAddress(m_module, name)); }

    // libretro callbacks (static: the ABI has no user-data pointer).
    static bool    RETRO_CALLCONV Environment(unsigned cmd, void* data);
    static void    RETRO_CALLCONV VideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch);
    static void    RETRO_CALLCONV AudioSample(int16_t l, int16_t r);
    static size_t  RETRO_CALLCONV AudioSampleBatch(const int16_t* data, size_t frames);
    static void    RETRO_CALLCONV InputPoll();
    static int16_t RETRO_CALLCONV InputState(unsigned port, unsigned device, unsigned index, unsigned id);
    static void    RETRO_CALLCONV Log(enum retro_log_level level, const char* fmt, ...);

    void ApplyAvInfo(const retro_system_av_info& av);

    HMODULE                  m_module = nullptr;
    bool                     m_gameLoaded = false;
    std::vector<uint8_t>     m_romData;
    std::string              m_romPathUtf8;
    std::string              m_libName;
    std::string              m_systemDir, m_saveDir;
    AvInfo                   m_av;
    retro_pixel_format       m_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;

    std::vector<uint32_t>    m_video;          // converted XRGB8888
    uint32_t                 m_videoW = 0, m_videoH = 0;
    bool                     m_newFrame = false;

    InputSnapshot            m_input{};
    std::function<void(const int16_t*, size_t)> m_audioSink;

    static RetroCoreLoader*  s_active;         // the core currently being driven
};

} // namespace nb
