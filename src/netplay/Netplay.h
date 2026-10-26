#pragma once
// Netplay.h — netplay transport layer (stub).
// Design target: guest-peer rollback over WinRT StreamSocket over TCP.
// The emulated-core frame loop produces deterministic frames, so rollback is
// straightforward: each peer simulates N frames ahead, exchanges input deltas,
// and rewinds + re-simulates on divergence (classic GGPO-style model).
// This stub provides the session lifecycle and message framing; the rollback
// window integration into CoreEngine::EmulationLoop is phase-2 work.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.Sockets.h>

namespace nb
{
class Netplay
{
public:
    enum class Mode { Offline, Host, Guest };

    struct FrameInput
    {
        uint32_t frame;
        uint16_t buttons;
    };

    struct Stats
    {
        uint32_t pingMs      = 0;
        uint32_t frameAdv    = 0;   // frames guest is behind host
        uint32_t rollbacks   = 0;
    };

    Netplay();
    ~Netplay();

    // Host: listen on port (Xbox->Xbox over LAN; Dev Mode apps are sandboxed
    // to private networks by default, so LAN peers are reachable).
    bool Host(uint16_t port);
    // Guest: connect to a host.
    bool Join(const std::wstring& hostAddr, uint16_t port);
    void Disconnect();

    void SendInput(const FrameInput& in);
    // Non-blocking poll: returns remote inputs received since last call.
    void PollInputs(std::vector<FrameInput>& out);

    Mode  NetMode() const { return m_mode; }
    Stats GetStats() const;

private:
    void ReaderLoop();

    Mode m_mode = Mode::Offline;

    winrt::Windows::Networking::Sockets::StreamSocketListener m_listener{ nullptr };
    winrt::Windows::Networking::Sockets::StreamSocket         m_socket{ nullptr };

    struct Impl;
    std::unique_ptr<Impl> m_impl;   // PIMPL: keeps atomics/queue out of the header
};

} // namespace nb
