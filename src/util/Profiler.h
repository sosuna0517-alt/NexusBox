#pragma once
// Profiler.h — ring-buffered frametime/CPU/mem counters feeding the OSD.
// Read every Nth frame by the overlay; writes are single-producer (emulation).
#include <cstdint>
#include <array>
#include <atomic>

namespace nb {

class Profiler
{
public:
    static constexpr int kRing = 60;   // 60fps = 1 second of history
    void BeginCpu()  { m_cpuStart = (uint64_t)NowUs(); }
    void EndCpu()    { m_cpuUs = (uint64_t)NowUs() - m_cpuStart; }
    void EndFrame(double dtMs)
    {
        FrameSample s{ dtMs, (double)m_cpuUs / 1000.0, MemMB() };
        m_ring[m_idx] = s;
        m_idx = (m_idx + 1) % kRing;
        m_lastMs.store(dtMs, std::memory_order_relaxed);
    }

    struct FrameSample { double dtMs; double cpuMs; double memMB; };

    // Overlay reads: copy the last kRing samples (60fps = 1 second of history).
    void CopyHistory(std::array<FrameSample, kRing>& out) const
    {
        for (int i = 0; i < kRing; ++i) out[i] = m_ring[(m_idx + i) % kRing];
    }
    double LastFrameMs() const { return m_lastMs.load(std::memory_order_relaxed); }
    double AvgFps() const
    {
        double sum = 0; int n = 0;
        for (int i = 0; i < kRing; ++i) if (m_ring[i].dtMs > 0) { sum += 1000.0 / m_ring[i].dtMs; ++n; }
        return n ? sum / n : 0.0;
    }

    static double NowUs();
    static double MemMB();   // AppMemoryReport via Windows.System.MemoryManager (UWP budget)

private:
    std::array<FrameSample, kRing> m_ring{};
    int m_idx = 0;
    uint64_t m_cpuStart = 0;
    uint64_t m_cpuUs = 0;
    std::atomic<double> m_lastMs{ 0 };
};

} // namespace nb
