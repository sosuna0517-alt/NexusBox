// RetroCoreLoader.cpp — libretro frontend implementation.
#include "RetroCoreLoader.h"
#include "InputManager.h"   // PadButton bits

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace nb
{

RetroCoreLoader* RetroCoreLoader::s_active = nullptr;

static std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

void RetroCoreLoader::SetDirectories(const std::wstring& system, const std::wstring& saves)
{
    m_systemDir = ToUtf8(system);
    m_saveDir   = ToUtf8(saves);
}

bool RetroCoreLoader::Load(const wchar_t* corePath, std::wstring& error)
{
    Unload();
    // UWP/Xbox: only DLLs inside the app package can be loaded and
    // LoadLibraryExW is unavailable. Cores must be built for UWP x64.
    m_module = LoadPackagedLibrary(corePath, 0);
    if (!m_module)
    {
        error = L"Could not load core " + std::wstring(corePath) +
                L" (error " + std::to_wstring(GetLastError()) + L"). Make sure it is a UWP x64 build inside the package's cores folder.";
        return false;
    }

    using set_env_t   = void (RETRO_CALLCONV*)(retro_environment_t);
    using set_video_t = void (RETRO_CALLCONV*)(retro_video_refresh_t);
    using set_as_t    = void (RETRO_CALLCONV*)(retro_audio_sample_t);
    using set_asb_t   = void (RETRO_CALLCONV*)(retro_audio_sample_batch_t);
    using set_ip_t    = void (RETRO_CALLCONV*)(retro_input_poll_t);
    using set_is_t    = void (RETRO_CALLCONV*)(retro_input_state_t);
    using void_fn_t   = void (RETRO_CALLCONV*)();
    using api_ver_t   = unsigned (RETRO_CALLCONV*)();

    auto apiVersion = Sym<api_ver_t>("retro_api_version");
    auto setEnv     = Sym<set_env_t>("retro_set_environment");
    auto setVideo   = Sym<set_video_t>("retro_set_video_refresh");
    auto setAs      = Sym<set_as_t>("retro_set_audio_sample");
    auto setAsb     = Sym<set_asb_t>("retro_set_audio_sample_batch");
    auto setIp      = Sym<set_ip_t>("retro_set_input_poll");
    auto setIs      = Sym<set_is_t>("retro_set_input_state");
    auto init       = Sym<void_fn_t>("retro_init");
    if (!apiVersion || !setEnv || !setVideo || !setAs || !setAsb || !setIp || !setIs || !init ||
        !Sym<void_fn_t>("retro_run"))
    {
        error = L"The DLL is not a libretro core (missing retro_* exports).";
        Unload();
        return false;
    }
    if (apiVersion() != RETRO_API_VERSION)
    {
        error = L"Unsupported libretro API version.";
        Unload();
        return false;
    }

    s_active = this;
    m_pixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
    setEnv(&Environment);
    setVideo(&VideoRefresh);
    setAs(&AudioSample);
    setAsb(&AudioSampleBatch);
    setIp(&InputPoll);
    setIs(&InputState);
    init();

    retro_system_info si{};
    if (auto getSI = Sym<void (RETRO_CALLCONV*)(retro_system_info*)>("retro_get_system_info"))
        getSI(&si);
    m_libName = si.library_name ? si.library_name : "core";
    return true;
}

bool RetroCoreLoader::LoadRom(const wchar_t* romPath, std::wstring& error)
{
    if (!m_module) { error = L"No core loaded."; return false; }

    retro_system_info si{};
    if (auto getSI = Sym<void (RETRO_CALLCONV*)(retro_system_info*)>("retro_get_system_info"))
        getSI(&si);

    m_romPathUtf8 = ToUtf8(romPath);
    retro_game_info gi{};
    gi.path = m_romPathUtf8.c_str();

    if (!si.need_fullpath)
    {
        std::ifstream f(romPath, std::ios::binary);
        if (!f) { error = L"Could not open ROM file: " + std::wstring(romPath); return false; }
        m_romData.assign(std::istreambuf_iterator<char>(f), {});
        gi.data = m_romData.data();
        gi.size = m_romData.size();
    }

    auto load = Sym<bool (RETRO_CALLCONV*)(const retro_game_info*)>("retro_load_game");
    s_active = this;
    if (!load || !load(&gi))
    {
        error = L"The core rejected this ROM (wrong system, or missing BIOS in the system folder).";
        return false;
    }
    m_gameLoaded = true;

    retro_system_av_info av{};
    if (auto getAV = Sym<void (RETRO_CALLCONV*)(retro_system_av_info*)>("retro_get_system_av_info"))
        getAV(&av);
    ApplyAvInfo(av);

    // Default controller: standard joypad on ports 0..3.
    if (auto setDev = Sym<void (RETRO_CALLCONV*)(unsigned, unsigned)>("retro_set_controller_port_device"))
        for (unsigned p = 0; p < 4; ++p) setDev(p, RETRO_DEVICE_JOYPAD);
    return true;
}

void RetroCoreLoader::ApplyAvInfo(const retro_system_av_info& av)
{
    if (av.timing.fps > 1.0)         m_av.fps = av.timing.fps;
    if (av.timing.sample_rate > 1.0) m_av.sampleRate = av.timing.sample_rate;
    if (av.geometry.base_width)      m_av.baseW = av.geometry.base_width;
    if (av.geometry.base_height)     m_av.baseH = av.geometry.base_height;
    m_av.aspect = av.geometry.aspect_ratio > 0.0f
                ? av.geometry.aspect_ratio
                : (double)m_av.baseW / (double)(m_av.baseH ? m_av.baseH : 1);
}

void RetroCoreLoader::Unload()
{
    if (m_module)
    {
        s_active = this;
        if (m_gameLoaded)
            if (auto f = Sym<void (RETRO_CALLCONV*)()>("retro_unload_game")) f();
        if (auto f = Sym<void (RETRO_CALLCONV*)()>("retro_deinit")) f();
        FreeLibrary(m_module);
        m_module = nullptr;
    }
    if (s_active == this) s_active = nullptr;
    m_gameLoaded = false;
    m_romData.clear(); m_romData.shrink_to_fit();
    m_video.clear(); m_videoW = m_videoH = 0;
    m_newFrame = false;
}

void RetroCoreLoader::RunFrame()
{
    if (!m_gameLoaded) return;
    s_active = this;
    if (auto run = Sym<void (RETRO_CALLCONV*)()>("retro_run")) run();
}

void RetroCoreLoader::Reset()
{
    if (auto r = Sym<void (RETRO_CALLCONV*)()>("retro_reset")) r();
}

MemoryMap RetroCoreLoader::GetMemoryMap() const
{
    MemoryMap m{};
    if (!m_gameLoaded) return m;
    auto data = Sym<void* (RETRO_CALLCONV*)(unsigned)>("retro_get_memory_data");
    auto size = Sym<size_t (RETRO_CALLCONV*)(unsigned)>("retro_get_memory_size");
    if (data && size)
    {
        m.sram = (uint8_t*)data(RETRO_MEMORY_SAVE_RAM);
        m.sramSize = size(RETRO_MEMORY_SAVE_RAM);
        if (!m.sram) m.sramSize = 0;
    }
    return m;
}

RetroCoreLoader::FrameView RetroCoreLoader::VideoFrame() const
{
    return { m_video.empty() ? nullptr : m_video.data(), m_videoW * 4, m_videoW, m_videoH, 32 };
}

std::vector<uint8_t> RetroCoreLoader::Serialize() const
{
    auto sz  = Sym<size_t (RETRO_CALLCONV*)()>("retro_serialize_size");
    auto ser = Sym<bool (RETRO_CALLCONV*)(void*, size_t)>("retro_serialize");
    if (!m_gameLoaded || !sz || !ser) return {};
    std::vector<uint8_t> out(sz());
    if (out.empty() || !ser(out.data(), out.size())) return {};
    return out;
}

bool RetroCoreLoader::Deserialize(const std::vector<uint8_t>& blob)
{
    auto de = Sym<bool (RETRO_CALLCONV*)(const void*, size_t)>("retro_unserialize");
    return m_gameLoaded && de && de(blob.data(), blob.size());
}

size_t RetroCoreLoader::SerializeSize() const
{
    auto sz = Sym<size_t (RETRO_CALLCONV*)()>("retro_serialize_size");
    return (m_gameLoaded && sz) ? sz() : 0;
}

// ------------------------------------------------------------------ callbacks

bool RETRO_CALLCONV RetroCoreLoader::Environment(unsigned cmd, void* data)
{
    RetroCoreLoader* self = s_active;
    switch (cmd)
    {
    case RETRO_ENVIRONMENT_GET_CAN_DUPE:
        if (data) *(bool*)data = true;
        return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
    {
        if (!data || !self) return false;
        const auto fmt = *(const retro_pixel_format*)data;
        if (fmt != RETRO_PIXEL_FORMAT_0RGB1555 && fmt != RETRO_PIXEL_FORMAT_XRGB8888 &&
            fmt != RETRO_PIXEL_FORMAT_RGB565) return false;
        self->m_pixelFormat = fmt;
        return true;
    }
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        if (!data || !self) return false;
        *(const char**)data = self->m_systemDir.empty() ? nullptr : self->m_systemDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
        if (!data || !self) return false;
        *(const char**)data = self->m_saveDir.empty() ? nullptr : self->m_saveDir.c_str();
        return true;
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
        if (!data) return false;
        ((retro_log_callback*)data)->log = &Log;
        return true;
    case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
        if (data && self) self->ApplyAvInfo(*(const retro_system_av_info*)data);
        return true;
    case RETRO_ENVIRONMENT_SET_GEOMETRY:
        if (data && self)
        {
            retro_system_av_info av{};
            av.geometry = *(const retro_game_geometry*)data;
            self->ApplyAvInfo(av);
        }
        return true;
    case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
        return true;
    case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
        if (data) *(unsigned*)data = 0;   // legacy SET_VARIABLES path: defaults are used
        return true;
    case RETRO_ENVIRONMENT_GET_VARIABLE:
        return false;                      // every option keeps its default value
    case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
        if (data) *(bool*)data = false;
        return true;
    case RETRO_ENVIRONMENT_SET_VARIABLES:
    case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
    case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
    case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
    case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
    case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
    case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
    case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        return true;
    case RETRO_ENVIRONMENT_GET_LANGUAGE:
        if (data) *(unsigned*)data = RETRO_LANGUAGE_ENGLISH;
        return true;
    default:
        return false;   // includes SET_HW_RENDER: OpenGL/Vulkan cores are not supported
    }
}

void RETRO_CALLCONV RetroCoreLoader::VideoRefresh(const void* data, unsigned w, unsigned h, size_t pitch)
{
    RetroCoreLoader* self = s_active;
    if (!self || !data || !w || !h) return;    // data == nullptr: duplicate frame
    self->m_video.resize((size_t)w * h);
    self->m_videoW = w; self->m_videoH = h;
    uint32_t* dst = self->m_video.data();
    const uint8_t* src = (const uint8_t*)data;

    switch (self->m_pixelFormat)
    {
    case RETRO_PIXEL_FORMAT_XRGB8888:
        for (unsigned y = 0; y < h; ++y)
            std::memcpy(dst + (size_t)y * w, src + y * pitch, (size_t)w * 4);
        break;
    case RETRO_PIXEL_FORMAT_RGB565:
        for (unsigned y = 0; y < h; ++y)
        {
            const uint16_t* row = (const uint16_t*)(src + y * pitch);
            uint32_t* out = dst + (size_t)y * w;
            for (unsigned x = 0; x < w; ++x)
            {
                const uint16_t p = row[x];
                uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
                r = (r << 3) | (r >> 2); g = (g << 2) | (g >> 4); b = (b << 3) | (b >> 2);
                out[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        break;
    default: // 0RGB1555
        for (unsigned y = 0; y < h; ++y)
        {
            const uint16_t* row = (const uint16_t*)(src + y * pitch);
            uint32_t* out = dst + (size_t)y * w;
            for (unsigned x = 0; x < w; ++x)
            {
                const uint16_t p = row[x];
                uint32_t r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F, b = p & 0x1F;
                r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
                out[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
        break;
    }
    self->m_newFrame = true;
}

void RETRO_CALLCONV RetroCoreLoader::AudioSample(int16_t l, int16_t r)
{
    RetroCoreLoader* self = s_active;
    if (!self || !self->m_audioSink) return;
    const int16_t s[2] = { l, r };
    self->m_audioSink(s, 1);
}

size_t RETRO_CALLCONV RetroCoreLoader::AudioSampleBatch(const int16_t* data, size_t frames)
{
    RetroCoreLoader* self = s_active;
    if (self && self->m_audioSink && data) self->m_audioSink(data, frames);
    return frames;
}

void RETRO_CALLCONV RetroCoreLoader::InputPoll() {}   // CoreEngine pushes a snapshot per frame

int16_t RETRO_CALLCONV RetroCoreLoader::InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
    RetroCoreLoader* self = s_active;
    if (!self || port >= 4) return 0;
    const InputSnapshot& in = self->m_input;
    const uint16_t b = in.buttons[port];
    auto has = [&](PadButton pb) { return (b & (1u << (int)pb)) != 0; };

    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_JOYPAD)
    {
        // Positional mapping (SNES layout): libretro B = bottom face = Xbox A, etc.
        auto pressed = [&](unsigned rid) -> bool {
            switch (rid)
            {
            case RETRO_DEVICE_ID_JOYPAD_B:      return has(PadButton::A);
            case RETRO_DEVICE_ID_JOYPAD_A:      return has(PadButton::B);
            case RETRO_DEVICE_ID_JOYPAD_Y:      return has(PadButton::X);
            case RETRO_DEVICE_ID_JOYPAD_X:      return has(PadButton::Y);
            case RETRO_DEVICE_ID_JOYPAD_SELECT: return has(PadButton::View);
            case RETRO_DEVICE_ID_JOYPAD_START:  return has(PadButton::Menu);
            case RETRO_DEVICE_ID_JOYPAD_UP:     return has(PadButton::DPadUp)    || in.ly[port] >  0.5f;
            case RETRO_DEVICE_ID_JOYPAD_DOWN:   return has(PadButton::DPadDown)  || in.ly[port] < -0.5f;
            case RETRO_DEVICE_ID_JOYPAD_LEFT:   return has(PadButton::DPadLeft)  || in.lx[port] < -0.5f;
            case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return has(PadButton::DPadRight) || in.lx[port] >  0.5f;
            case RETRO_DEVICE_ID_JOYPAD_L:      return has(PadButton::LB);
            case RETRO_DEVICE_ID_JOYPAD_R:      return has(PadButton::RB);
            case RETRO_DEVICE_ID_JOYPAD_L2:     return in.lt[port] > 0.5f;
            case RETRO_DEVICE_ID_JOYPAD_R2:     return in.rt[port] > 0.5f;
            case RETRO_DEVICE_ID_JOYPAD_L3:     return has(PadButton::LS);
            case RETRO_DEVICE_ID_JOYPAD_R3:     return has(PadButton::RS);
            }
            return false;
        };
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
        {
            int16_t mask = 0;
            for (unsigned i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; ++i)
                if (pressed(i)) mask |= (int16_t)(1 << i);
            return mask;
        }
        return pressed(id) ? 1 : 0;
    }
    if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG)
    {
        auto to16 = [](float v) { return (int16_t)(std::max(-1.0f, std::min(1.0f, v)) * 32767.0f); };
        if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? to16(in.lx[port]) : to16(-in.ly[port]);
        if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return id == RETRO_DEVICE_ID_ANALOG_X ? to16(in.rx[port]) : to16(-in.ry[port]);
    }
    return 0;
}

void RETRO_CALLCONV RetroCoreLoader::Log(enum retro_log_level level, const char* fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    static const char* tags[] = { "[core DEBUG] ", "[core INFO] ", "[core WARN] ", "[core ERROR] " };
    OutputDebugStringA(tags[(level >= 0 && level <= 3) ? level : 1]);
    OutputDebugStringA(buf);
}

} // namespace nb
