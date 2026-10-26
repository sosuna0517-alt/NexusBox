#pragma once
// InputManager.h — Windows.Gaming.Input polling thread.
// - High-priority poller at 1kHz producing seqlock-snapshotted pad states.
// - Radial deadzone + saturation mapping for analog sticks, impulse triggers.
// - Per-core remappable profiles (JSON), hotkey combos (Guide+View, L3+R3).
#include <atomic>
#include <thread>
#include <array>
#include <cstdint>
#include <functional>

#include <winrt/Windows.Gaming.Input.h>

namespace nb {
class CoreEngine;

static constexpr int kMaxPads = 4;

enum class PadButton : uint16_t
{
    A, B, X, Y, View, Menu, DPadUp, DPadDown, DPadLeft, DPadRight,
    LB, RB, LS, RS,
    Count
};

struct PadState    // 48 bytes, fits a cache line with the stick pair
{
    float lx = 0, ly = 0, rx = 0, ry = 0;   // -1..1 after deadzone
    float lt = 0, rt = 0;                    //  0..1 (analog; impulse-rumbled)
    uint16_t buttons = 0;                    // bit i = PadButton(i)
    int32_t  player  = -1;
};

struct HotkeyEvent
{
    enum class Action { SaveState, LoadState, Overlay, Rewind, FastForward,
                        ToggleShader, ExitToDashboard } action;
    int      slot = 0;
    uint64_t frame = 0;
};

class InputManager
{
public:
    void Initialize();
    void Shutdown();

    // Called from the emulation thread: consistent, non-blocking snapshot.
    std::array<PadState, kMaxPads> Snapshot(uint64_t frame);

    // Called from the UI thread.
    void ProcessHotkeys(CoreEngine& engine);

    void SetDeadzone(float radial)        { m_deadzone = radial; }
    void SetRemap(const char* json);      // per-core profile
    void SetHotkeyPadCombo(uint16_t btnMask, HotkeyEvent::Action a);

private:
    void PollLoop();
    void MapPad(winrt::Windows::Gaming::Input::Gamepad const& pad, PadState& out);

    std::thread              m_pollThread;
    std::atomic<bool>        m_run{ false };
    std::atomic<uint64_t>    m_seq{ 0 };              // seqlock writer
    std::array<PadState, kMaxPads> m_state{};         // read by Snapshot under seqlock

    float m_deadzone = 0.15f;
    struct HotkeyDef { uint16_t mask; uint32_t holdMs; HotkeyEvent::Action action; };
    std::array<HotkeyDef, 8> m_hotkeys{};
    uint64_t m_comboStartMs = 0;
    uint16_t m_lastMask = 0;
};

} // namespace nb
