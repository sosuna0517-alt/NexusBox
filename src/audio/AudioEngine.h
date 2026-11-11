#pragma once
// AudioEngine.h — XAudio2 streaming voice fed by a lock-free SPSC ring buffer.
// The libretro audio callbacks (emulation thread) push interleaved stereo
// int16 frames; XAudio2's voice callback pulls them on its own thread and keeps
// a few small buffers queued. XAudio2 resamples the core rate (e.g. 32040 Hz
// for SNES) to the console's output rate.
#include <xaudio2.h>
#include <wrl/client.h>
#include <atomic>
#include <array>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <memory>

namespace nb {

template <typename T, size_t CapacityPow2>
class SpscRing
{
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "must be power of two");
public:
    size_t Push(const T* items, size_t n)
    {
        const size_t w = m_write.load(std::memory_order_relaxed);
        const size_t r = m_read.load(std::memory_order_acquire);
        n = std::min(n, CapacityPow2 - (w - r));        // full: drop the overflow
        for (size_t i = 0; i < n; ++i) m_buf[(w + i) & (CapacityPow2 - 1)] = items[i];
        m_write.store(w + n, std::memory_order_release);
        return n;
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
    void Reset() { m_read.store(0); m_write.store(0); }
private:
    std::array<T, CapacityPow2> m_buf{};
    std::atomic<size_t> m_read{ 0 }, m_write{ 0 };
};

class AudioEngine
{
public:
    ~AudioEngine() { Shutdown(); }
    bool Initialize();                              // creates engine + mastering voice
    void Configure(uint32_t sampleRate);            // (re)creates the stereo source voice
    void Shutdown();

    // Emulation thread: interleaved stereo frames.
    void WriteFrames(const int16_t* samples, size_t frames);

    void SuspendVoice();
    void ResumeVoice();
    void Clear() { m_ring.Reset(); }

private:
    class VoiceCallback : public IXAudio2VoiceCallback
    {
    public:
        AudioEngine* owner = nullptr;
        void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override { if (owner) owner->Pump(); }
        void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
        void STDMETHODCALLTYPE OnStreamEnd() override {}
        void STDMETHODCALLTYPE OnBufferStart(void*) override {}
        void STDMETHODCALLTYPE OnBufferEnd(void*) override {}
        void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
        void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
    };

    void Pump();   // XAudio2 thread: keep kBuffers chunks queued

    Microsoft::WRL::ComPtr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_master = nullptr;
    IXAudio2SourceVoice*    m_voice  = nullptr;

    static constexpr size_t kRingSamples  = 1 << 15;     // int16 samples (16k stereo frames)
    static constexpr size_t kChunkFrames  = 512;         // ~10ms at 48k
    static constexpr size_t kBuffers      = 4;
    SpscRing<int16_t, kRingSamples> m_ring;
    std::array<std::array<int16_t, kChunkFrames * 2>, kBuffers> m_chunks{};
    size_t m_nextChunk = 0;

    VoiceCallback     m_cb;
    uint32_t          m_rate = 0;
    std::atomic<bool> m_suspended{ false };
};

} // namespace nb
