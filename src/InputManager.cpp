// InputManager.cpp
#include "InputManager.h"
#include "CoreEngine.h"

#include <windows.h>
#include <cmath>
#include <cstring>

// cppwinrt include order matters: Foundation + Collections must come before
// Gaming.Input, otherwise consumer templates (Size/GetAt) are seen undefined.
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>

using namespace winrt::Windows::Gaming::Input;

namespace nb
{

namespace {
    constexpr float kTriggerThreshold = 0.5f;

    inline float RadialDeadzone(float x, float y, float dz)
    {
        const float mag = std::sqrt(x * x + y * y);
        if (mag <= dz) return 0.0f;
        // Radial deadzone + saturation: preserves direction, full throw at edge.
        const float scaled = (mag - dz) / (1.0f - dz);
        return std::min(scaled / mag, 1.0f);
    }

    inline int ButtonBit(GamepadButtons b)
    {
        switch (b)
        {
        case GamepadButtons::A:        return (int)PadButton::A;
        case GamepadButtons::B:        return (int)PadButton::B;
        case GamepadButtons::X:        return (int)PadButton::X;
        case GamepadButtons::Y:        return (int)PadButton::Y;
        case GamepadButtons::View:     return (int)PadButton::View;
        case GamepadButtons::Menu:     return (int)PadButton::Menu;
        case GamepadButtons::DPadUp:   return (int)PadButton::DPadUp;
        case GamepadButtons::DPadDown: return (int)PadButton::DPadDown;
        case GamepadButtons::DPadLeft: return (int)PadButton::DPadLeft;
        case GamepadButtons::DPadRight:return (int)PadButton::DPadRight;
        case GamepadButtons::LeftShoulder:  return (int)PadButton::LB;
        case GamepadButtons::RightShoulder: return (int)PadButton::RB;
        case GamepadButtons::LeftThumbstick:  return (int)PadButton::LS;
        case GamepadButtons::RightThumbstick: return (int)PadButton::RS;
        }
        return -1;
    }
}

void InputManager::Initialize()
{
    m_run = true;
    m_pollThread = std::thread([this] { PollLoop(); });
    SetThreadPriority(m_pollThread.native_handle(), THREAD_PRIORITY_HIGHEST);
}

void InputManager::Shutdown()
{
    m_run = false;
    if (m_pollThread.joinable()) m_pollThread.join();
}

void InputManager::PollLoop()
{
    // 1kHz poll: 1ms latency ceiling, far below the 8.33ms (120Hz) frame budget.
    const DWORD kPollMs = 1;
    while (m_run)
    {
        const auto pads = Gamepad::Gamepads();

        uint64_t seq = m_seq.load(std::memory_order_relaxed);
        m_seq.store(seq + 1, std::memory_order_relaxed);   // odd = writing

        int idx = 0;
        const uint32_t count = pads.Size();
        for (uint32_t i = 0; i < count; ++i)
        {
            if (idx >= kMaxPads) break;
            MapPad(pads.GetAt(i), m_state[idx]);
            m_state[idx].player = idx;
            ++idx;
        }
        for (; idx < kMaxPads; ++idx) m_state[idx] = PadState{};

        m_seq.store(seq + 2, std::memory_order_release);   // even = stable
        Sleep(kPollMs);
    }
}

void InputManager::MapPad(Gamepad const& pad, PadState& out)
{
    const GamepadReading r = pad.GetCurrentReading();

    out.lx = RadialDeadzone((float)r.LeftThumbstickX, (float)r.LeftThumbstickY,  m_deadzone)
             * (float)r.LeftThumbstickX;
    out.ly = RadialDeadzone((float)r.LeftThumbstickX, (float)r.LeftThumbstickY,  m_deadzone)
             * (float)r.LeftThumbstickY;
    out.rx = RadialDeadzone((float)r.RightThumbstickX,(float)r.RightThumbstickY, m_deadzone)
             * (float)r.RightThumbstickX;
    out.ry = RadialDeadzone((float)r.RightThumbstickX,(float)r.RightThumbstickY, m_deadzone)
             * (float)r.RightThumbstickY;
    out.lt = (float)r.LeftTrigger;    // impulse triggers: analog pressure
    out.rt = (float)r.RightTrigger;

    uint16_t mask = 0;
    auto set = [&](GamepadButtons b) {
        if ((r.Buttons & b) == b) { int i = ButtonBit(b); if (i >= 0) mask |= (1u << i); }
    };
    set(GamepadButtons::A); set(GamepadButtons::B); set(GamepadButtons::X);
    set(GamepadButtons::Y); set(GamepadButtons::View); set(GamepadButtons::Menu);
    set(GamepadButtons::DPadUp); set(GamepadButtons::DPadDown);
    set(GamepadButtons::DPadLeft); set(GamepadButtons::DPadRight);
    set(GamepadButtons::LeftShoulder); set(GamepadButtons::RightShoulder);
    set(GamepadButtons::LeftThumbstick); set(GamepadButtons::RightThumbstick);
    out.buttons = mask;

    // Impulse trigger rumble for core-driven feedback (retro_set_rumble).
    // Throttled to 10Hz by the caller to avoid GUID/IO overhead.
    winrt::Windows::Gaming::Input::GamepadVibration vib{};
    vib.LeftMotor    = (double)out.lt * 0.5;
    vib.RightMotor   = (double)out.rt * 0.5;
    vib.LeftTrigger  = 0.0;
    vib.RightTrigger = 0.0;
    pad.Vibration(vib);
}

std::array<PadState, kMaxPads> InputManager::Snapshot(uint64_t frame)
{
    // Seqlock read loop: retry if the writer raced us (rare at 1kHz vs 60-120Hz).
    std::array<PadState, kMaxPads> out{};
    for (;;)
    {
        const uint64_t s1 = m_seq.load(std::memory_order_acquire);
        if (s1 & 1) continue;
        std::memcpy(out.data(), m_state.data(), sizeof(out));
        const uint64_t s2 = m_seq.load(std::memory_order_acquire);
        if (s1 == s2) break;
    }
    (void)frame;
    return out;
}

void InputManager::SetHotkeyPadCombo(uint16_t btnMask, HotkeyEvent::Action a)
{
    for (auto& hk : m_hotkeys)
        if (hk.mask == 0) { hk.mask = btnMask; hk.action = a; hk.holdMs = 400; return; }
}

void InputManager::ProcessHotkeys(CoreEngine& engine)
{
    // Combos are evaluated from the last snapshot on the UI thread.
    auto pads = Snapshot(0);
    if (pads.empty()) return;

    const uint16_t mask = pads[0].buttons;

    // L3 + R3 => Quick-Access Overlay (per spec)
    const uint16_t overlayMask =
        (1u << (int)PadButton::LS) | (1u << (int)PadButton::RS);
    const uint16_t saveMask  = (1u << (int)PadButton::LB);
    const uint16_t loadMask  = (1u << (int)PadButton::RB);

    if (mask == overlayMask && m_lastMask != overlayMask)
        engine.HandleSystemBack();     // toggles Overlay <-> Running
    if (mask == saveMask && m_lastMask != saveMask)
        engine.SaveStateNow(1);
    if (mask == loadMask && m_lastMask != loadMask)
        engine.LoadStateNow(1);

    m_lastMask = mask;
}

void InputManager::SetRemap(const char* /*json*/)
{
    // TODO: parse per-core remap profile ({"a":"x","lb":"hold=rewind",...})
}

} // namespace nb
