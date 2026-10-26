#pragma once
// CoreEngine.h — the deterministic emulation dispatch loop.
// Owns frame pacing (60/120Hz ticks), binds: InputManager -> retro_run ->
// Renderer blit -> AudioEngine ring buffer. All cross-thread data flows through
// lock-free SPSC queues or seqlock snapshots; the emulation thread never blocks
// on GPU/audio.
#include <cstdint>
#include <memory>
#include <atomic>
#include <thread>
#include <chrono>

#include "core/RetroCoreLoader.h"
#include "save/SaveStateManager.h"
#include "util/Profiler.h"

namespace nb {
class Renderer; class InputManager; class AudioEngine; class Netplay;

enum class Mode { Dashboard, Running, Paused, Overlay };

struct FrameContext
{
    uint64_t  frameIndex = 0;
    double    targetFps  = 60.0;        // from retro_get_system_av_info
    double    audioSampleRate = 48000.0;
    bool      skipVideo  = false;       // fast-forward frames skip blit
    bool      rewindTick = false;       // rewind buffer captured this frame
};

class CoreEngine
{
public:
    void   Initialize(Renderer* renderer);
    void   Start();
    void   Shutdown();

    bool   LoadCore(const wchar_t* corePath, const wchar_t* romPath);
    void   UnloadCore();

    void   Tick();                       // called from the UI thread each Run() pass
    void   Pause();  void  Resume(bool newResume = false);

    // App lifecycle hooks (called from App on Suspending/Resuming).
    void   PrepareForSuspension();
    void   OnResume();
    void   SetFastForward(float speed);  // 1.0 = realtime
    void   SetRewindEnabled(bool on);
    bool   SaveStateNow(int slot);       // synchronous (user-invoked)
    bool   LoadStateNow(int slot);
    bool   HandleSystemBack();           // returns true if B was consumed by UI

    SaveStateManager& States()  { return m_states; }
    Profiler&         Profile() { return m_profiler; }
    Mode              GetMode() const { return m_mode; }
    ~CoreEngine();    // defined in CoreEngine.cpp (Netplay is complete there)

private:
    void   EmulationLoop();              // runs on its own high-priority thread
    void   SyncPacing(double targetFps);

    Renderer*           m_renderer = nullptr;
    std::unique_ptr<InputManager> m_input;
    std::unique_ptr<AudioEngine>  m_audio;
    std::unique_ptr<Netplay>      m_netplay;

    RetroCoreLoader     m_core;
    SaveStateManager    m_states;
    Profiler            m_profiler;

    Mode                m_mode = Mode::Dashboard;
    std::atomic<bool>   m_runThread{ false };
    std::atomic<float>  m_speed{ 1.0f };
    std::atomic<bool>   m_rewind{ false };
    std::thread         m_thread;

    double              m_nextTickMs = 0.0;   // drift-free pacing accumulator
};

} // namespace nb
