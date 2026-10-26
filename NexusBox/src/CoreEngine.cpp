// CoreEngine.cpp — deterministic frame dispatch.
#include "netplay/Netplay.h"
#include "CoreEngine.h"
#include "render/Renderer.h"
#include "InputManager.h"
#include "audio/AudioEngine.h"

#include <windows.h>
#include <algorithm>

using namespace std::chrono;

namespace nb
{

void CoreEngine::Initialize(Renderer* renderer)
{
    m_renderer = renderer;
    m_input    = std::make_unique<InputManager>();
    m_audio    = std::make_unique<AudioEngine>();
    m_netplay  = std::make_unique<Netplay>();
    m_input->Initialize();
    m_audio->Initialize(48000, 2);
    m_netplay->Initialize(this);
}

void CoreEngine::Start()
{
    m_runThread = true;
    m_thread = std::thread([this] { EmulationLoop(); });
    SetThreadPriority(m_thread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
    SetThreadAffinityMask(m_thread.native_handle(), 0x2); // core 1: keep emulation off core 0 (OS)
}

void CoreEngine::Shutdown()
{
    m_runThread = false;
    if (m_thread.joinable()) m_thread.join();
    UnloadCore();
}

bool CoreEngine::LoadCore(const wchar_t* corePath, const wchar_t* romPath)
{
    if (!m_core.Load(corePath))   return false;
    if (!m_core.LoadRom(romPath)) return false;
    m_states.Initialize(m_core.GetMemoryMap());        // SRAM/state arenas registered
    m_audio->Configure((uint32_t)m_core.AudioRate(), m_core.AudioChannels());
    Resume();
    return true;
}

void CoreEngine::UnloadCore()
{
    m_states.FlushAll();                   // synchronous SRAM write before teardown
    m_core.Unload();
}

// ------------------------------------------------------------------ dispatch

void CoreEngine::EmulationLoop()
{
    using clock = steady_clock;
    auto last = clock::now();
    uint64_t frame = 0;

    while (m_runThread)
    {
        auto t0 = clock::now();
        double dtMs = duration<double, std::milli>(t0 - last).count();
        last = t0;

        if (m_mode != Mode::Running) { Sleep(4); continue; }

        FrameContext fc;
        fc.frameIndex = frame++;
        const auto av = m_core.GetAvInfo();
        float speed = m_speed.load(std::memory_order_relaxed);
        fc.targetFps       = av.fps * speed;
        fc.audioSampleRate = m_core.AudioRate();
        fc.skipVideo       = speed > 2.0f && (frame & 1); // FF: skip odd blits
        fc.rewindTick      = m_rewind.load() && ((frame % 2) == 0); // 30Hz rewind granularity

        // 1) Input: seqlock snapshot from the polling thread (never blocks)
        // Convert pad states to the libretro-facing InputSnapshot.
        auto pads = m_input->Snapshot(fc.frameIndex);
        InputSnapshot in{};
        for (size_t p = 0; p < pads.size() && p < 4; ++p)
        {
            in.buttons[p] = pads[p].buttons;
            in.analog[p][0] = pads[p].lx; in.analog[p][1] = pads[p].ly;
            in.analog[p][2] = pads[p].rx; in.analog[p][3] = pads[p].rt;
        }
        m_core.SetInput(in);

        // 2) Rewind capture (before run, so load-state is one frame of work)
        if (fc.rewindTick) m_states.CaptureRewind();

        // 3) Emulate one video frame (audio sample callbacks stream into XAudio2 ring)
        m_profiler.BeginCpu();
        m_core.RunFrame();
        m_profiler.EndCpu();

        // 4) Netplay (rollback resim, if active)
        m_netplay->OnFrame(static_cast<uint32_t>(fc.frameIndex));

        // 5) Publish: GPU blit is queued, audio already streamed by the core
        if (!fc.skipVideo)
        {
            auto fv = m_core.VideoFrame();
            VideoFrame vf{ fv.data, fv.pitch, fv.width, fv.height, fv.bpp, fc.frameIndex };
            const auto av2 = m_core.GetAvInfo();
            VideoGeometry geo{ fv.width, fv.height, av2.aspect, 0.0f };
            m_renderer->Blit(vf, geo, fc.frameIndex);
        }

        // 6) Drift-free pacing: schedule next tick, never sleep to catch-up
        SyncPacing(fc.targetFps);
        m_profiler.EndFrame(dtMs);
    }
}

void CoreEngine::SyncPacing(double targetFps)
{
    if (targetFps <= 1.0) { Sleep(4); return; }
    const double stepMs = 1000.0 / targetFps;
    if (m_nextTickMs <= 0.0)
    {
        m_nextTickMs = duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
    m_nextTickMs += stepMs;
    const double nowMs = duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    const double wait  = m_nextTickMs - nowMs;
    if (wait > 0.0)
    {
        // Sleep down to 1ms granularity, spin the last fraction for precision.
        if (wait > 1.5) Sleep(static_cast<DWORD>(wait - 1.0));
        const double endMs = m_nextTickMs;
        while (duration<double, std::milli>(steady_clock::now().time_since_epoch()).count() < endMs)
            YieldProcessor();
    }
    else
    {
        // Behind: re-anchor, drop the backlog (never burst-run).
        m_nextTickMs = nowMs;
    }
}

void CoreEngine::Tick()
{
    // UI-thread tick: poll hotkeys, drive the overlay; the emulation thread
    // never touches the window system.
    if (!m_input) return;
    m_input->ProcessHotkeys(*this);
}

void CoreEngine::Pause()  { if (m_mode == Mode::Running) m_mode = Mode::Paused;  }
void CoreEngine::Resume(bool) { if (m_mode == Mode::Paused) m_mode = Mode::Running; }

void CoreEngine::SetFastForward(float speed) { m_speed = std::clamp(speed, 0.1f, 8.0f); }
void CoreEngine::SetRewindEnabled(bool on)   { m_rewind = on; }

bool CoreEngine::SaveStateNow(int slot)  { return m_states.Save(slot, m_core.Serialize()); }
bool CoreEngine::LoadStateNow(int slot)
{
    auto blob = m_states.Load(slot);
    if (!blob) return false;
    return m_core.Deserialize(*blob);
}

void CoreEngine::PrepareForSuspension()
{
    // Xbox may kill us shortly after Suspending fires: flush everything now.
    m_states.FlushAll();
    m_audio->SuspendVoice();
    m_mode = Mode::Paused;
}

void CoreEngine::OnResume()
{
    m_audio->ResumeVoice();
    m_renderer->HandleDeviceLost();
    if (m_core.Loaded()) m_mode = Mode::Running;
}

bool CoreEngine::HandleSystemBack()
{
    if (m_mode == Mode::Overlay) { m_mode = Mode::Running; return true; }
    if (m_mode == Mode::Running) { m_mode = Mode::Overlay;  return true; }
    return false;
}

} // namespace nb
