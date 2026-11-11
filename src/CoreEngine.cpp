// CoreEngine.cpp — emulation thread, game load/unload, SRAM and save states.
#include "CoreEngine.h"
#include "render/Renderer.h"
#include "InputManager.h"
#include "audio/AudioEngine.h"
#include "util/LzCpp.h"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <cmath>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;
using namespace std::chrono;

namespace nb
{

CoreEngine::CoreEngine()  = default;
CoreEngine::~CoreEngine() { Shutdown(); }

void CoreEngine::Initialize(Renderer* renderer, InputManager* input)
{
    m_renderer = renderer;
    m_input    = input;
    m_audio    = std::make_unique<AudioEngine>();
    m_audio->Initialize();

    m_localDir = winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    std::error_code ec;
    fs::create_directories(SystemDir(), ec);
    fs::create_directories(SavesDir(), ec);
    fs::create_directories(RomsDir(), ec);
    m_core.SetDirectories(SystemDir(), SavesDir());
}

void CoreEngine::Start()
{
    if (m_runThread) return;
    m_runThread = true;
    m_thread = std::thread([this] { EmulationLoop(); });
    SetThreadPriority(m_thread.native_handle(), THREAD_PRIORITY_HIGHEST);
}

void CoreEngine::Shutdown()
{
    m_runThread = false;
    if (m_thread.joinable()) m_thread.join();
    UnloadGame();
    if (m_audio) m_audio->Shutdown();
}

bool CoreEngine::LoadGame(const std::wstring& corePath, const std::wstring& romPath, std::wstring& error)
{
    UnloadGame();
    std::lock_guard lock(m_coreMutex);

    if (!m_core.Load(corePath.c_str(), error)) return false;
    m_core.SetAudioSink([this](const int16_t* s, size_t frames) { m_audio->WriteFrames(s, frames); });
    if (!m_core.LoadRom(romPath.c_str(), error)) { m_core.Unload(); return false; }

    m_romPath   = romPath;
    m_gameTitle = fs::path(romPath).stem().wstring();
    m_coreName  = m_core.LibraryName();
    m_audio->Configure((uint32_t)std::lround(m_core.AudioRate()));
    LoadSram();

    m_hasGame = true;
    m_mode = Mode::Running;
    return true;
}

void CoreEngine::UnloadGame()
{
    std::lock_guard lock(m_coreMutex);
    if (m_hasGame) SaveSram();
    m_mode = Mode::Dashboard;
    m_hasGame = false;
    m_core.Unload();
    if (m_audio) m_audio->Clear();
    if (m_renderer) m_renderer->ClearVideo();
}

void CoreEngine::Pause()  { if (m_mode == Mode::Running) { m_mode = Mode::Paused; if (m_audio) m_audio->SuspendVoice(); } }
void CoreEngine::Resume() { if (m_mode == Mode::Paused && m_hasGame) { if (m_audio) { m_audio->Clear(); m_audio->ResumeVoice(); } m_mode = Mode::Running; } }

void CoreEngine::ResetGame()
{
    std::lock_guard lock(m_coreMutex);
    if (m_hasGame) m_core.Reset();
}

// ------------------------------------------------------------------ emulation thread

void CoreEngine::EmulationLoop()
{
    using clock = steady_clock;
    auto next = clock::now();
    auto last = clock::now();

    while (m_runThread)
    {
        if (m_mode != Mode::Running) { Sleep(5); next = clock::now(); continue; }

        {
            std::lock_guard lock(m_coreMutex);
            if (!m_hasGame || m_mode != Mode::Running) continue;

            // 1) Input snapshot -> core
            const auto pads = m_input->Snapshot(0);
            InputSnapshot in{};
            for (int p = 0; p < 4; ++p)
            {
                in.buttons[p] = pads[p].buttons;
                in.lx[p] = pads[p].lx; in.ly[p] = pads[p].ly;
                in.rx[p] = pads[p].rx; in.ry[p] = pads[p].ry;
                in.lt[p] = pads[p].lt; in.rt[p] = pads[p].rt;
            }
            m_core.SetInput(in);

            // 2) Emulate one frame (audio callbacks push into the XAudio2 ring)
            m_profiler.BeginCpu();
            m_core.RunFrame();
            m_profiler.EndCpu();

            // 3) Hand the new frame to the renderer (copied; UI thread presents)
            if (m_core.ConsumeNewFrame())
            {
                const auto fv = m_core.VideoFrame();
                m_renderer->SubmitVideo((const uint32_t*)fv.data, fv.width, fv.height, m_core.GetAvInfo().aspect);
            }
        }

        // 4) Pace to the core's frame rate (fast-forward runs uncapped up to 4x).
        const double fps = std::clamp(m_core.GetAvInfo().fps, 10.0, 240.0) * (m_fastForward ? 4.0 : 1.0);
        next += duration_cast<clock::duration>(duration<double>(1.0 / fps));
        auto now = clock::now();
        if (next > now)
        {
            const auto waitMs = duration<double, std::milli>(next - now).count();
            if (waitMs > 2.0) Sleep((DWORD)(waitMs - 1.0));
            while (clock::now() < next) YieldProcessor();
        }
        else if (now - next > milliseconds(100))
        {
            next = now;   // fell far behind (e.g. after a pause): re-anchor
        }
        now = clock::now();
        m_profiler.EndFrame(duration<double, std::milli>(now - last).count());
        last = now;
    }
}

// ------------------------------------------------------------------ saves

std::wstring CoreEngine::SramPath() const  { return SavesDir() + L"\\" + m_gameTitle + L".srm"; }
std::wstring CoreEngine::StatePath(int slot) const
{ return SavesDir() + L"\\" + m_gameTitle + L".state" + std::to_wstring(slot); }

void CoreEngine::LoadSram()
{
    const MemoryMap mem = m_core.GetMemoryMap();
    if (!mem.sram || !mem.sramSize) return;
    std::ifstream f(SramPath(), std::ios::binary);
    if (f) f.read((char*)mem.sram, (std::streamsize)mem.sramSize);
}

void CoreEngine::SaveSram()
{
    const MemoryMap mem = m_core.GetMemoryMap();
    if (!mem.sram || !mem.sramSize) return;
    std::ofstream f(SramPath(), std::ios::binary | std::ios::trunc);
    if (f) f.write((const char*)mem.sram, (std::streamsize)mem.sramSize);
}

bool CoreEngine::SaveState(int slot, std::wstring& message)
{
    std::lock_guard lock(m_coreMutex);
    if (!m_hasGame) { message = L"No game running."; return false; }
    const auto state = m_core.Serialize();
    if (state.empty()) { message = L"This core does not support save states."; return false; }
    const auto packed = lz::Compress(state);
    std::ofstream f(StatePath(slot), std::ios::binary | std::ios::trunc);
    if (!f) { message = L"Could not write the save state file."; return false; }
    f.write((const char*)packed.data(), (std::streamsize)packed.size());
    message = L"Saved state to slot " + std::to_wstring(slot) + L".";
    return true;
}

bool CoreEngine::LoadState(int slot, std::wstring& message)
{
    std::lock_guard lock(m_coreMutex);
    if (!m_hasGame) { message = L"No game running."; return false; }
    std::ifstream f(StatePath(slot), std::ios::binary);
    if (!f) { message = L"Slot " + std::to_wstring(slot) + L" is empty."; return false; }
    std::vector<uint8_t> packed((std::istreambuf_iterator<char>(f)), {});
    std::vector<uint8_t> state;
    if (!lz::Decompress(packed, state) || !m_core.Deserialize(state))
    { message = L"The save state could not be loaded."; return false; }
    message = L"Loaded state from slot " + std::to_wstring(slot) + L".";
    return true;
}

void CoreEngine::PrepareForSuspension()
{
    Pause();
    std::lock_guard lock(m_coreMutex);
    if (m_hasGame) SaveSram();
}

void CoreEngine::OnResume()
{
    // Stay paused; the dashboard overlay lets the player resume.
}

} // namespace nb
