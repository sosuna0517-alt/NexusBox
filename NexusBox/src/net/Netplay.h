#pragma once
// Netplay.h — local peer-to-peer rollback foundation.
// Non-blocking Winsock UDP, deterministic lockstep with rollback: when a remote
// input arrives late, we rewind N frames from the rewind ring and re-simulate
// with the corrected inputs. This module wires the frame loop; the core must
// expose deterministic serialization (retro_serialize) for resim.
#include <cstdint>
#include <atomic>
#include <thread>
#include <array>

namespace nb {
class CoreEngine;

class Netplay
{
public:
    void Initialize(CoreEngine* engine);
    void Shutdown();

    bool Host(uint16_t port);           // bind + listen
    bool Join(const char* ip, uint16_t port);
    void Disconnect();

    void OnFrame(uint64_t frameIndex);  // called from the emulation thread
    bool Active() const { return m_active; }

private:
    void SocketThread();

    CoreEngine* m_engine = nullptr;
    std::atomic<bool> m_active{ false }, m_run{ false };
    std::thread m_thread;
    int m_socket = -1;
    uint64_t m_hostFrame = 0;      // authoritative (host) frame counter
    static constexpr int kMaxRollback = 8;
};

} // namespace nb
