#pragma once
// Dashboard.h — console-grade 10-foot UI shell.
// - Grid cover flow with focus easing ( Xbox dashboard-like navigation).
// - Systems tab bar, Recently Played row, Settings entry.
// - Drives the Scraper to fill box art / metadata into the library cache.
// - Same D3D12 command queue as the game blit; drawn between blit and present
//   (no second swapchain), so switching Dashboard <-> Running is seamless.
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

#include "storage/StorageManager.h"

struct ImGuiContext;

namespace nb {
class Renderer; class CoreEngine; class InputManager; class Scraper;

struct LibraryEntry
{
    StorageManager::GameEntry game;
    std::wstring displayName;
    std::wstring system;          // "SNES", "NES", "GBA"...
    std::wstring coreId;
    std::string  boxartUv;        // cached texture key ("game:<hash>")
    uint64_t     lastPlayed = 0;  // unix seconds
    int          playCount = 0;
    float        rating = 0.0f;   // user stars 0..5
};

enum class Screen { Boot, Library, GameDetail, Settings, Netplay, Overlay };

class Dashboard
{
public:
    void Initialize(Renderer* renderer, CoreEngine* engine);
    void Shutdown();

    // One UI frame: consumes the input snapshot, animates, draws.
    // Called from the UI thread; safe while a core is running (overlay path).
    void Update(float dtSeconds, const std::vector<struct PadState>& pads);

    void ShowLibrary();
    void ShowOverlay();
    void HideOverlay();

    Screen GetScreen() const { return m_screen; }
    bool   WantsExitToDashboard() const { return m_wantsExit; }
    void   ClearWantsExit() { m_wantsExit = false; }

private:
    void DrawLibrary(float dt);
    void DrawGameDetail();
    void DrawSettings();
    void DrawOverlay();
    void DrawBootSplash(float dt);
    void NavigateGrid(int dx, int dy);
    void Launch(const LibraryEntry& e);

    Renderer*             m_renderer = nullptr;
    CoreEngine*           m_engine = nullptr;
    std::unique_ptr<Scraper> m_scraper;

    std::vector<LibraryEntry> m_library;
    std::vector<std::wstring> m_systemTabs;   // "" = All
    int    m_activeTab = 0;
    int    m_focusX = 0, m_focusY = 0;
    float  m_focusAnimX = 0, m_focusAnimY = 0; // eased focus position
    float  m_scrollAnim = 0;
    Screen m_screen = Screen::Boot;
    bool   m_wantsExit = false;
    float  m_bootTimer = 0;
    bool   m_overlayOpen = false;
    // Filtered view of the active tab
    std::vector<int> m_visibleRows;
    uint64_t m_lastNavFrame = 0;
};

} // namespace nb
