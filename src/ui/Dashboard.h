#pragma once
// Dashboard.h — 10-foot ImGui shell: game library, settings, in-game menu.
// Gamepad-only navigation (ImGui gamepad nav). Draw data is rendered by the
// Renderer in the same frame as the game video.
#include <string>
#include <vector>
#include <cstdint>

#include "InputManager.h"

struct ImDrawData;

namespace nb {
class Renderer; class CoreEngine;

struct GameEntry
{
    std::wstring path;
    std::wstring title;
    std::wstring system;
    std::vector<std::wstring> cores;   // candidate core DLL names, in preference order
};

class Dashboard
{
public:
    enum class Screen { Library, Settings, InGameMenu, Hidden };

    bool Initialize(Renderer* renderer, CoreEngine* engine);
    void Shutdown();

    // Builds one UI frame; returns draw data (may be empty when Hidden).
    ImDrawData* Update(float dt, const PadState& pad);

    void OpenInGameMenu();
    Screen GetScreen() const { return m_screen; }

private:
    void FeedGamepad(const PadState& pad);
    void ScanLibrary();
    void DrawLibrary();
    void DrawSettings();
    void DrawInGameMenu();
    void DrawToast(float dt);
    void Launch(const GameEntry& g);
    void Toast(const std::wstring& msg, float seconds = 5.0f);
    bool CoreInstalled(const std::wstring& dll) const;

    Renderer*   m_renderer = nullptr;
    CoreEngine* m_engine = nullptr;
    Screen      m_screen = Screen::Library;
    std::vector<GameEntry> m_games;
    std::vector<std::wstring> m_scannedDirs;
    std::wstring m_coresDir;
    std::string m_toast;
    float       m_toastTime = 0.0f;
    bool        m_focusFirst = true;
    int         m_stateSlot = 1;
    uint16_t    m_prevButtons = 0;
};

} // namespace nb
