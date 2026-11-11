#pragma once
// CoreEngine.h — owns the loaded libretro core and runs it on a dedicated
// emulation thread paced to the core's native frame rate. The UI thread only
// calls the public methods below; m_coreMutex serializes core access.
#include <cstdint>
#include <memory>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "core/RetroCoreLoader.h"
#include "util/Profiler.h"

namespace nb {
class Renderer; class InputManager; class AudioEngine;

enum class Mode { Dashboard, Running, Paused };

class CoreEngine
{
public:
    CoreEngine();
    ~CoreEngine();

    void Initialize(Renderer* renderer, InputManager* input);
    void Start();
    void Shutdown();

    // corePath is package-relative (cores\\name_libretro.dll).
    bool LoadGame(const std::wstring& corePath, const std::wstring& romPath, std::wstring& error);
    void UnloadGame();

    void Pause();
    void Resume();
    void ResetGame();
    Mode GetMode() const { return m_mode.load(); }
    bool HasGame() const { return m_hasGame.load(); }

    bool SaveState(int slot, std::wstring& message);
    bool LoadState(int slot, std::wstring& message);
    void SetFastForward(bool on) { m_fastForward = on; }
    bool FastForward() const     { return m_fastForward; }

    // Lifecycle (App Suspending / Resuming).
    void PrepareForSuspension();
    void OnResume();

    std::string CoreName() const { return m_coreName; }
    std::wstring GameTitle() const { return m_gameTitle; }
    Profiler& Profile() { return m_profiler; }

    // Directories (LocalState sub-folders).
    std::wstring SystemDir() const { return m_localDir + L"\\system"; }
    std::wstring SavesDir()  const { return m_localDir + L"\\saves"; }
    std::wstring RomsDir()   const { return m_localDir + L"\\roms"; }
    std::wstring LocalDir()  const { return m_localDir; }

private:
    void EmulationLoop();
    void LoadSram();
    void SaveSram();
    std::wstring StatePath(int slot) const;
    std::wstring SramPath() const;

    Renderer*                     m_renderer = nullptr;
    InputManager*                 m_input = nullptr;
    std::unique_ptr<AudioEngine>  m_audio;
    RetroCoreLoader               m_core;
    Profiler                      m_profiler;
    std::mutex                    m_coreMutex;

    std::atomic<Mode>             m_mode{ Mode::Dashboard };
    std::atomic<bool>             m_hasGame{ false };
    std::atomic<bool>             m_runThread{ false };
    std::atomic<bool>             m_fastForward{ false };
    std::thread                   m_thread;

    std::wstring                  m_localDir, m_romPath, m_gameTitle;
    std::string                   m_coreName;
};

} // namespace nb
