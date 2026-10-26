// RetroCoreLoader.cpp — libretro ABI binding and sandboxed arenas.
#include "RetroCoreLoader.h"

#include <fstream>
#include <cstring>
#include <stdexcept>

namespace nb
{
bool RetroCoreLoader::m_supportNoGame = false;

// --- libretro function-pointer types (subset) -------------------------------
using retro_environment_t = bool(*)(unsigned, void*);
using retro_video_refresh_t = void(*)(const void*, unsigned, unsigned, size_t);
using retro_audio_sample_batch_t = size_t(*)(const int16_t*, size_t);
using retro_input_poll_t = void(*)();
using retro_input_state_t = int16_t(*)(unsigned, unsigned, unsigned, unsigned);
using retro_set_ = void(*)(retro_proc_t);

template <typename T> static T sym(HMODULE m, const char* name)
{
    return reinterpret_cast<T>(GetProcAddress(m, name));
}

bool RetroCoreLoader::Load(const wchar_t* corePath)
{
    Unload();
    // LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | search application dir | search system32
    m_module = LoadLibraryExW(corePath, nullptr,
        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
    if (!m_module) return false;
    if (!BindSymbols()) { Unload(); return false; }

    auto retroInit = sym<retro_proc_t>(m_module, "retro_init");
    if (retroInit) reinterpret_cast<void(*)()>(retroInit)();

    // Query AV info via retro_get_system_av_info.
    struct RetroSystemInfo { const char* library_name; const char* library_version;
                             const char* valid_extensions; bool need_fullpath;
                             bool block_extract; } si{};
    auto getSI = sym<retro_proc_t>(m_module, "retro_get_system_info");
    if (getSI) reinterpret_cast<void(*)(void*)>(getSI)(&si);

    struct RetroSystemAvInfo { struct { unsigned w, h; } geometry; double timing; } av{};
    auto getAV = sym<retro_proc_t>(m_module, "retro_get_system_av_info");
    if (getAV) reinterpret_cast<void(*)(void*)>(getAV)(&av);
    m_av.fps = av.timing; m_av.baseW = av.geometry.w; m_av.baseH = av.geometry.h;
    return true;
}

bool RetroCoreLoader::LoadRom(const wchar_t* romPath)
{
    std::ifstream f(romPath, std::ios::binary);
    if (!f) return false;
    m_romData.assign(std::istreambuf_iterator<char>(f), {});
    m_romPath = romPath;

    // retro_load_game(&info) with ROM in memory (need_fullpath=false for small ROMs;
    // disc-based systems stream from the StorageManager file view).
    struct RetroGameInfo { const void* data; size_t size; const char* path; } gi{};
    gi.data = m_romData.data(); gi.size = m_romData.size(); gi.path = nullptr;
    auto load = sym<retro_proc_t>(m_module, "retro_load_game");
    bool ok = load && reinterpret_cast<bool(*)(void*)>(load)(&gi);
    return ok;
}

void RetroCoreLoader::Unload()
{
    if (m_module)
    {
        if (auto f = sym<retro_proc_t>(m_module, "retro_unload"))
            reinterpret_cast<void(*)()>(f)();
        if (auto f = sym<retro_proc_t>(m_module, "retro_deinit"))
            reinterpret_cast<void(*)()>(f)();
        FreeLibrary(m_module);
        m_module = nullptr;
    }
    m_romData.clear(); m_romData.shrink_to_fit();
}

bool RetroCoreLoader::BindSymbols()
{
    if (!sym<retro_proc_t>(m_module, "retro_run")) return false;
    if (!sym<retro_proc_t>(m_module, "retro_serialize")) return false;
    return true;
}

void RetroCoreLoader::RunFrame()
{
    auto run = sym<retro_proc_t>(m_module, "retro_run");
    if (run) reinterpret_cast<void(*)()>(run)();
}

void RetroCoreLoader::Reset()
{
    auto r = sym<retro_proc_t>(m_module, "retro_reset");
    if (r) reinterpret_cast<void(*)()>(r)();
}

RetroCoreLoader::FrameView RetroCoreLoader::VideoFrame() const
{
    return { m_videoBuffer.data(), m_videoPitch, m_videoW, m_videoH, m_videoBpp };
}

void RetroCoreLoader::SetInput(const InputSnapshot& snap) { m_input = snap; }

void RetroCoreLoader::SetAudioSink(std::function<size_t(const int16_t*, size_t)> sink)
{ m_audioSink = std::move(sink); }

std::vector<uint8_t> RetroCoreLoader::Serialize() const
{
    auto sz = sym<retro_proc_t>(m_module, "retro_serialize_size");
    auto ser = sym<retro_proc_t>(m_module, "retro_serialize");
    if (!sz || !ser) return {};
    const size_t n = reinterpret_cast<size_t(*)()>(sz)();
    std::vector<uint8_t> out(n);
    reinterpret_cast<bool(*)(void*, size_t)>(ser)(out.data(), out.size());
    return out;
}

bool RetroCoreLoader::Deserialize(const std::vector<uint8_t>& blob)
{
    auto de = sym<retro_proc_t>(m_module, "retro_unserialize");
    return de && reinterpret_cast<bool(*)(const void*, size_t)>(de)(blob.data(), blob.size());
}

size_t RetroCoreLoader::SerializeSize() const
{
    auto sz = sym<retro_proc_t>(m_module, "retro_serialize_size");
    return sz ? reinterpret_cast<size_t(*)()>(sz)() : 0;
}

// --- retro_environment_t ------------------------------------------------------
bool RetroCoreLoader::Environment(unsigned cmd, void* data)
{
    switch (cmd)
    {
    case 2: { // RETRO_ENVIRONMENT_SET_PIXEL_FORMAT (2)
        if (!data) return false;
        const unsigned* fmt = (const unsigned*)data;
        // 0 = 0RGB1555, 1 = XRGB8888, 2 = RGB565. Convert to RGBA8 on the GPU path.
        return true; }
    case 15: { // RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME
        RetroCoreLoader::m_supportNoGame = data ? *reinterpret_cast<bool*>(data) : false;
        return true; }
    case 21: { // RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY
        *reinterpret_cast<const char**>(data) = "D:\\nexusbox\\system";
        return true; }
    case 22: { // RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY
        *reinterpret_cast<const char**>(data) = "D:\\nexusbox\\saves";
        return true; }
    case 27: { // RETRO_ENVIRONMENT_SET_FRAME_TIME_CALLBACK
        return true; }
    case 34: { // RETRO_ENVIRONMENT_GET_VARIABLE — core options default
        return false; }
    default:
        return false;
    }
}

} // namespace nb
