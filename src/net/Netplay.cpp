// Netplay.cpp
#include "Netplay.h"
#include "CoreEngine.h"
#include "save/SaveStateManager.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <memory>

#pragma comment(lib, "ws2_32.lib")

namespace nb
{

void Netplay::Initialize(CoreEngine* engine) { m_engine = engine; }

bool Netplay::Host(uint16_t port)
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    m_socket = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket < 0) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(m_socket, (sockaddr*)&addr, sizeof(addr)) != 0) return false;

    u_long nonblocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonblocking);   // never block the frame loop
    m_run = true;
    m_thread = std::thread([this] { SocketThread(); });
    m_active = true;
    return true;
}

bool Netplay::Join(const char* ip, uint16_t port)
{
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
    m_socket = (int)socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    InetPtonA(AF_INET, ip, &addr.sin_addr);
    connect(m_socket, (sockaddr*)&addr, sizeof(addr));
    u_long nonblocking = 1;
    ioctlsocket(m_socket, FIONBIO, &nonblocking);
    m_run = true;
    m_thread = std::thread([this] { SocketThread(); });
    m_active = true;
    return true;
}

void Netplay::OnFrame(uint64_t frameIndex)
{
    if (!m_active) return;
    // Send our input for this frame; expect the peer's input for frame-2
    // (2-frame delay). If it is late, roll back via the rewind ring and resim.
    // Foundation: frame exchange scaffolding is in place; deterministic resim
    // requires each core's retro_serialize coverage to be verified first.
    (void)frameIndex;
}

void Netplay::SocketThread()
{
    char buf[64];
    while (m_run)
    {
        int n = recv(m_socket, buf, sizeof(buf), 0);
        if (n <= 0) { Sleep(1); continue; }
        // TODO: parse input packet, apply rollback if frame > current-2.
    }
}

void Netplay::Disconnect()
{
    m_run = false;
    if (m_thread.joinable()) m_thread.join();
    if (m_socket >= 0) { closesocket(m_socket); m_socket = -1; }
    WSACleanup();
    m_active = false;
}

void Netplay::Shutdown() { Disconnect(); }

} // namespace nb
