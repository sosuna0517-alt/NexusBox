#pragma once
// AudioEngine.h — XAudio2 low-latency voice fed by a lock-free SPSC ring buffer.
// The libretro audio callback (running on the emulation thread) writes samples;
// the XAudio2 source voice reads them via a streaming voice callback. No locks
// on the audio path, no allocation, fixed capacity (power-of-two).
#include <xaudio2.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <memory>

namespace nb {

template <typename T, size_t CapacityPow2>
class SpscRing
{
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "must be power of two");
public:
    bool Push(const T* items, size_t n)
    {
        const size_t w = m_write.load(std::memory_order_relaxed);
        const size_t r = m_read.load(std::memory_order_acquire);
        if (n > CapacityPow2 - (w - r)) return false;   // full: drop (underrun policy)
        for (size_t i = 0; i < n; ++i) m_buf[(w + i) & (CapacityPow2 - 1)] = items[i];
        m_write.store(w + n, std::memory_order_release);
        return true;
    }
    size_t Pop(T* out, size_t n)
    {
        const size_t r = m_read.load(std::memory_order_relaxed);
        const size_t w = m_write.load(std::memory_order_acquire);
        const size_t avail = std::min(n, w - r);
        for (size_t i = 0; i < avail; ++i) out[i] = m_buf[(r + i) & (CapacityPow2 - 1)];
        m_read.store(r + avail, std::memory_order_release);
        return avail;
    }
    size_t Available() const
    {
        return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed);
    }
    void Reset() { m_read.store(0, std::memory_order_relaxed); m_write.store(0, std::memory_order_relaxed); }
private:
    std::array<T, CapacityPow2> m_buf{};
    std::atomic<size_t> m_read{ 0 }, m_write{ 0 };
};

class VoiceCallback; // fwd

class AudioEngine
{
public:
    void Initialize(uint32_t sampleRate, uint32_t channels);
    void Configure(uint32_t sampleRate, uint32_t channels);
    void Shutdown();

    // Called from the emulation thread (retro_audio_sample_batch_t).
    size_t WriteSamples(const int16_t* samples, size_t count);

    void SuspendVoice();
    void ResumeVoice();

    size_t Buffered() const { return m_ring.Available(); }
    void   SetTargetLatencyMs(float ms) { m_targetLatencyMs = ms; }

private:
    class VoiceCallback : public IXAudio2VoiceCallback
    {
    public:
        AudioEngine* owner = nullptr;
        void OnVoiceProcessingPassStart(UINT32) override {}
        void OnVoiceProcessingPassEnd() override {}
        void OnStreamEnd() override {}
        void OnBufferStart(void*) override {}
        void OnBufferEnd(void*) override { if (owner) owner->Refill(); }
        void OnLoopEnd(void*) override {}
        void OnVoiceError(void*, HRESULT) override {}
    };

    void Refill();

    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SourceVoice*    m_voice  = nullptr;

    static constexpr size_t kRingFrames = 1 << 14;    // 16384 frames (~340ms @48k)
    SpscRing<int16_t, kRingFrames * 2> m_ring;        // stereo interleaved

    std::unique_ptr<VoiceCallback> m_cb;
    std::array<uint8_t, kRingFrames * 2 * 2> m_refillBuf{}; // refill staging
    uint32_t m_rate = 48000, m_channels = 2;
    float    m_targetLatencyMs = 20.0f;
    std::atomic<bool> m_suspended{ false };
};

} // namespace nb
