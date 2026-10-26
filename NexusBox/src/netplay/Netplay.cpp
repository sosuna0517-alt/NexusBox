#include "Netplay.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>

#include "CoreEngine.h"

namespace nb
{
struct Netplay::Impl
{
    std::thread            reader;
    std::atomic<bool>      run{ false };

    std::mutex             lock;
    std::deque<FrameInput> remote;
    uint32_t               pingMs = 0;
    uint32_t               frameAdv = 0;
    uint32_t               rollbacks = 0;
};

Netplay::Netplay() : m_impl(std::make_unique<Impl>()) {}

void Netplay::Initialize(CoreEngine* owner)
{
    m_owner = owner;   // used in phase 2 to push inputs into the rollback window
}

Netplay::~Netplay()
{
    Disconnect();
    if (m_impl && m_impl->reader.joinable()) m_impl->reader.join();
}

bool Netplay::Host(uint16_t port)
{
    Disconnect();
    try
    {
        auto onConn = [this](const winrt::Windows::Networking::Sockets::StreamSocketListener&,
                             const winrt::Windows::Networking::Sockets::StreamSocketListenerConnectionReceivedEventArgs& args)
        {
            m_socket = args.Socket();
            m_impl->run = true;
            m_impl->reader = std::thread([this] { ReaderLoop(); });
        };
        m_listener = winrt::Windows::Networking::Sockets::StreamSocketListener();
        m_listener.ConnectionReceived(onConn);
        m_listener.BindServiceNameAsync(winrt::to_hstring(port)).get();
        m_mode = Mode::Host;
        return true;
    }
    catch (...) { return false; }
}

bool Netplay::Join(const std::wstring& hostAddr, uint16_t port)
{
    Disconnect();
    try
    {
        m_socket = winrt::Windows::Networking::Sockets::StreamSocket();
        m_socket.ConnectAsync(
            winrt::Windows::Networking::HostName(winrt::to_hstring(hostAddr)),
            winrt::to_hstring(port)).get();
        m_mode = Mode::Guest;
        m_impl->run = true;
        m_impl->reader = std::thread([this] { ReaderLoop(); });
        return true;
    }
    catch (...) { return false; }
}

void Netplay::Disconnect()
{
    m_impl->run = false;
    try { m_socket.Close(); } catch (...) {}
    try { m_listener.Close(); } catch (...) {}
    m_mode = Mode::Offline;
}

void Netplay::OnFrame(uint32_t frame)
{
    if (m_mode == Mode::Offline) return;
    // Stub: forward the engine's local input sample for this frame; a real
    // rollback implementation predicts, exchanges, and rewinds on divergence.
    FrameInput in{ frame, 0 };
    SendInput(in);
}

void Netplay::SendInput(const FrameInput& in)
{
    if (m_mode == Mode::Offline) return;
    // Frame: 4-byte magic 'NBIN' + frame + buttons. ReaderLoop peeks the magic
    // on the stream to resync after disconnects. Stub does synchronous send;
    // phase 2 moves this to a lock-free egress buffer.
    try
    {
        uint8_t buf[12];
        buf[0]='N'; buf[1]='B'; buf[2]='I'; buf[3]='N';
        memcpy(buf+4, &in.frame, 4);
        memcpy(buf+8, &in.buttons, 2);
        buf[10]=0; buf[11]=0;
        m_socket.OutputStream().WriteAsync(
            winrt::array_view<const uint8_t>(buf, buf+12)).get();
    }
    catch (...) { /* peer dropped; reconnect logic is phase 2 */ }
}

void Netplay::PollInputs(std::vector<FrameInput>& out)
{
    std::lock_guard<std::mutex> g(m_impl->lock);
    out.assign(m_impl->remote.begin(), m_impl->remote.end());
    m_impl->remote.clear();
}

Netplay::Stats Netplay::GetStats() const
{
    std::lock_guard<std::mutex> g(m_impl->lock);
    Stats s;
    s.pingMs = m_impl->pingMs;
    s.frameAdv = m_impl->frameAdv;
    s.rollbacks = m_impl->rollbacks;
    return s;
}

void Netplay::ReaderLoop()
{
    // Stub: a real impl reads the peer stream with DataReader.LoadAsync into a
    // partial-frame buffer and resyncs on the 'NBIN' magic.
    while (m_impl->run)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

} // namespace nb
